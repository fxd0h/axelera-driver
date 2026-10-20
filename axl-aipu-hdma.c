/*
 * Copyright (c) 2025 Axelera AI. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include <linux/version.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/cdev.h>
#include <linux/debugfs.h>
#include <linux/dma-mapping.h>
#include <linux/aer.h>
#include <linux/msi.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/dma-buf.h>
#include <linux/poll.h>

#include "axl-aipu-dmabuf.h"
#include "axl-aipu.h"
#include "axl-aipu-pcie-hdma.h"
#include "axl-aipu-hdma-core.h"

/* Module params */
extern unsigned int enable_dmabuf_sync;

static inline struct dw_hdma_ll_buf *
axl_aipu_hdma_get_ll_desc_base(struct axl_pcie_aipu_dev *axldev)
{
	return (struct dw_hdma_ll_buf *)axldev->desc_base;
}

static void axl_aipu_hdma_enable_ctrl(struct axl_pcie_aipu_dev *axldev)
{
	struct dw_hdma_v0_regs *hdma = axldev->dma;
	int i;

	for (i = 0; i < HDMA_V0_MAX_NR_CH; i++) {
		writel(BIT(0), &hdma->ch[i].rd.ch_en);
		writel(BIT(0), &hdma->ch[i].wr.ch_en);
	}
}

static void axl_aipu_hdma_dev_dynmem_init(struct axl_pcie_aipu_dev *axldev)
{
	struct device_dma_sg_desc_t *dma_sg_desc;
	struct device_sys_ctl_t *dsctl;

	dsctl = axldev->vbase;
	dma_sg_desc = axl_aipu_get_dma_sg_desc_area(axldev);
	if (dma_sg_desc) {
		axldev->desc_base = dma_sg_desc->dma_sg_desc_buf_ref.addr;
		axldev->desc_offset =
			axldev->desc_base - dsctl->memory_map[MEMORY_AREA_0];
		pr_debug(
			"HDMA DMA SG descriptor area found at 0x%llx (offset 0x%llx)\n",
			axldev->desc_base, axldev->desc_offset);
	} else {
		pr_debug(
			"HDMA SG descriptor area not found, falling back to fixed area\n");
		axldev->desc_offset =
			HDMA_DESC_SIZE - sizeof(struct dw_hdma_ll_buf);
		axldev->desc_base = HDMA_DESC_BASE + axldev->desc_offset;
	}
	axl_aipu_hdma_enable_ctrl(axldev);
	axldev->dma_vm = (dsctl->dma_vm == DMA_VM_SUPPORTED) ? 1 : 0;
}

/**
 * axl_aipu_hdma_dma_irq_ck() - Ask a DMA channel whether it just terminated
 * @axldev: Device context
 * @id: MSI slot of the channel, as computed by axl_aipu_ch2id()
 *
 * Single-MSI mode: every source shares one vector, so the handler scans all
 * eight DMA slots and asks each channel in turn. Slots outside the DMA block
 * are not ours. HDMA counterpart of axl_aipu_edma_dma_irq_ck().
 *
 * STATUS_REG_STOPPED is the *resting* state of a channel that has finished a
 * transfer, so it reads STOPPED on idle channels too; dma_wait_irq() clears
 * ch_stat before ringing the doorbell so that a channel which has been started
 * but not yet completed reads 0 or RUNNING here. An idle channel still answers
 * STOPPED, so ch_stat is read only for a slot that is already armed and the wake
 * is committed by an ARMED->DONE cmpxchg. A waiter arming concurrently therefore
 * cannot inherit a completion this callback started for the transfer before it -
 * the source of the "spurious wakeup" seen under virtualization, where the MMIO
 * writes below stretch that window to microseconds. Reaching DONE is also what
 * lets the waiter tell its own completion from a wake caused by another source
 * sharing the vector, since ch_stat alone cannot.
 *
 * ch_stat is deliberately not cleared here: dma_wait_poll() spins on it and a
 * polling channel shares this vector with the interrupt-driven ones.
 *
 * Return: 1 if an armed channel stopped or aborted, 0 otherwise.
 */
static int axl_aipu_hdma_dma_irq_ck(struct axl_pcie_aipu_dev *axldev, int id)
{
	volatile struct dw_hdma_v0_regs *hdma = axldev->dma;
	struct pci_dev *pdev = axldev->pdev;
	int slot = id - axldev->dma_msi_base;
	enum dw_hdma_dir dir;
	u32 ch_stat;
	int ch;

	if (!hdma || slot < 0 || slot >= 2 * HDMA_V0_MAX_NR_CH)
		return 0;

	if (slot < HDMA_V0_MAX_NR_CH) {
		dir = DW_HDMA_DIR_READ;
		ch = slot;
	} else {
		dir = DW_HDMA_DIR_WRITE;
		ch = slot - HDMA_V0_MAX_NR_CH;
	}

	/*
	 * Read the status only once the slot is known to be armed: dma_wait_irq()
	 * arms after it has cleared ch_stat, so an ARMED slot can no longer be
	 * showing the resting STOPPED of the transfer before it.
	 */
	if (!dma_wait_is_armed(axldev, id))
		return 0;

	ch_stat = GET_RW_32_CH(hdma, dir, ch_stat, ch) & HDMA_V0_CH_STATUS_MASK;
	if (ch_stat != STATUS_REG_STOPPED && ch_stat != STATUS_REG_ABORTED)
		return 0;

	/* The completion truth dma_wait_irq() consumes, against a timing-out waiter */
	if (!dma_wait_claim(axldev, id,
			    ch_stat == STATUS_REG_ABORTED ? DMA_WAIT_ABORTED :
							    DMA_WAIT_DONE))
		return 0;

	if (ch_stat == STATUS_REG_ABORTED)
		dev_err(&pdev->dev, "DMA %s CH%d aborted (irq %d)\n",
			dir == DW_HDMA_DIR_WRITE ? "WR" : "RD", ch, id);
	else
		dev_dbg(&pdev->dev, "wake (%d) DMA %s CH%d done\n", id,
			dir == DW_HDMA_DIR_WRITE ? "WR" : "RD", ch);

	/* Both outcomes end the transfer, so the waiter is released either way */
	SET_RW_32_CH(hdma, dir, int_clear, ch,
		     HDMA_V0_ABORT_INT_MASK | HDMA_V0_STOP_INT_MASK);
	return 1;
}

static void axl_aipu_hdma_int_setup(struct dma_wrk *dma_wrk)
{
	u32 setup;
	u64 tmp;
	struct axl_pcie_aipu_dev *axldev = dma_wrk->axldev;
	volatile struct dw_hdma_v0_regs *hdma = axldev->dma;
	enum dw_hdma_dir dir = dma_wrk->flags & DMABUF_XFER_FLAG_READ ?
				       DW_HDMA_DIR_WRITE :
				       DW_HDMA_DIR_READ;
	volatile struct dw_hdma_ll_buf *hwlldch =
		(struct dw_hdma_ll_buf *)axl_aipu_hdma_get_ll_desc_base(axldev);

	setup = GET_RW_32_CH(hdma, dir, int_setup, dma_wrk->channel);
	setup &= ~(HDMA_V0_STOP_INT_MASK | HDMA_V0_ABORT_INT_MASK);
	if (!dma_poll_mode(dma_wrk)) {
		setup |= HDMA_V0_REMOTE_STOP_INT_EN |
			 HDMA_V0_REMOTE_ABORT_INT_EN;
	}
	SET_RW_32_CH(hdma, dir, int_setup, dma_wrk->channel, setup);
	SET_RW_32_CH(hdma, dir, control1, dma_wrk->channel,
		     HDMA_V0_LINKLIST_EN);
	tmp = __get_ll_base(hwlldch, dir, dma_wrk->channel);
	SET_RW_32_CH(hdma, dir, llp.lsb, dma_wrk->channel, lower_32_bits(tmp));
	SET_RW_32_CH(hdma, dir, llp.msb, dma_wrk->channel, upper_32_bits(tmp));
}
static void axl_aipu_hdma_init_imwr(struct axl_pcie_aipu_dev *axldev)
{
	volatile struct dw_hdma_v0_regs *hdma = axldev->dma;
	int i;
	enum dw_hdma_dir dir;
	u32 setup;
	u64 tmp;
	volatile struct dw_hdma_ll_buf *hwlldch =
		(struct dw_hdma_ll_buf *)axl_aipu_hdma_get_ll_desc_base(axldev);

	for (i = 0; i < HDMA_V0_MAX_NR_CH; i++) {
		dir = DW_HDMA_DIR_READ;
		if (!axldev->dma_vm) {
			SET_RW_32_CH(hdma, dir, msi_stop.lsb, i,
				     axldev->irq_msi.address_lo);
			SET_RW_32_CH(hdma, dir, msi_stop.msb, i,
				     axldev->irq_msi.address_hi);
			SET_RW_32_CH(hdma, dir, msi_abort.lsb, i,
				     axldev->irq_msi.address_lo);
			SET_RW_32_CH(hdma, dir, msi_abort.msb, i,
				     axldev->irq_msi.address_hi);
			SET_RW_32_CH(hdma, dir, msi_watermark.lsb, i,
				     axldev->irq_msi.address_lo);
			SET_RW_32_CH(hdma, dir, msi_watermark.msb, i,
				     axldev->irq_msi.address_hi);

			if (axldev->nmsi == 1) {
				SET_RW_32_CH(hdma, dir, msi_msgdata, i,
					     axldev->irq_msi.data);
			} else {
				SET_RW_32_CH(hdma, dir, msi_msgdata, i,
					     PMSI_DMA_RD_CH0 + i);
			}
		}
		setup = GET_RW_32_CH(hdma, dir, int_setup, i);
		setup &= ~(HDMA_V0_STOP_INT_MASK | HDMA_V0_ABORT_INT_MASK);
		setup |= HDMA_V0_REMOTE_STOP_INT_EN |
			 HDMA_V0_REMOTE_ABORT_INT_EN;
		SET_RW_32_CH(hdma, dir, int_setup, i, setup);
		SET_RW_32_CH(hdma, dir, control1, i, HDMA_V0_LINKLIST_EN);

		tmp = __get_ll_base(hwlldch, dir, i);
		SET_RW_32_CH(hdma, dir, llp.lsb, i, lower_32_bits(tmp));
		SET_RW_32_CH(hdma, dir, llp.msb, i, upper_32_bits(tmp));
	}
	for (i = 0; i < HDMA_V0_MAX_NR_CH; i++) {
		dir = DW_HDMA_DIR_WRITE;
		if (!axldev->dma_vm) {
			SET_RW_32_CH(hdma, dir, msi_stop.lsb, i,
				     axldev->irq_msi.address_lo);
			SET_RW_32_CH(hdma, dir, msi_stop.msb, i,
				     axldev->irq_msi.address_hi);
			SET_RW_32_CH(hdma, dir, msi_abort.lsb, i,
				     axldev->irq_msi.address_lo);
			SET_RW_32_CH(hdma, dir, msi_abort.msb, i,
				     axldev->irq_msi.address_hi);
			SET_RW_32_CH(hdma, dir, msi_watermark.lsb, i,
				     axldev->irq_msi.address_lo);
			SET_RW_32_CH(hdma, dir, msi_watermark.msb, i,
				     axldev->irq_msi.address_hi);

			if (axldev->nmsi == 1) {
				SET_RW_32_CH(hdma, dir, msi_msgdata, i,
					     axldev->irq_msi.data);
			} else {
				SET_RW_32_CH(hdma, dir, msi_msgdata, i,
					     PMSI_DMA_WR_CH0 + i);
			}
		}
		setup = GET_RW_32_CH(hdma, dir, int_setup, i);
		setup &= ~(HDMA_V0_STOP_INT_MASK | HDMA_V0_ABORT_INT_MASK);
		setup |= HDMA_V0_REMOTE_STOP_INT_EN |
			 HDMA_V0_REMOTE_ABORT_INT_EN;
		SET_RW_32_CH(hdma, dir, int_setup, i, setup);
		SET_RW_32_CH(hdma, dir, control1, i, HDMA_V0_LINKLIST_EN);

		tmp = __get_ll_base(hwlldch, dir, i);
		SET_RW_32_CH(hdma, dir, llp.lsb, i, lower_32_bits(tmp));
		SET_RW_32_CH(hdma, dir, llp.msb, i, upper_32_bits(tmp));
	}
}

static inline int dma_wait_irq(struct axl_pcie_aipu_dev *axldev,
			       struct dma_wrk *dma_wrk)
{
	volatile struct dw_hdma_v0_regs *hdma = axldev->dma;
	struct pci_dev *pdev = axldev->pdev;
	struct sysctrl_ctx *sctx = dma_wrk->sctx;
	int err, state = DMA_WAIT_DONE;
	u32 ch_stat;
	const char *mode = dma_wrk->flags & DMABUF_XFER_FLAG_READ ? "WR" : "RD";

	enum dw_hdma_dir dir = dma_wrk->flags & DMABUF_XFER_FLAG_READ ?
				       DW_HDMA_DIR_WRITE :
				       DW_HDMA_DIR_READ;

	if (axldev->nmsi == 1) {
		/*
		 * Clear ch_stat (and flush the write) so the shared-vector scan
		 * cannot read the resting STOPPED of the previous transfer as
		 * this one, then arm the slot last: the check callback wakes an
		 * ARMED slot only, so nothing it started before this point can
		 * be inherited by the wait below.
		 */
		SET_RW_32_CH(hdma, dir, ch_stat, dma_wrk->channel, 0);
		(void)GET_RW_32_CH(hdma, dir, ch_stat, dma_wrk->channel);
		reinit_completion(&axldev->irq_wrk[dma_wrk->id].irq_done);
		dma_wait_arm(axldev, dma_wrk->id);
	}

	SET_RW_32_CH(hdma, dir, cycle_sync, dma_wrk->channel,
		     HDMA_V0_CONSUMER_CYCLE_STAT | HDMA_V0_CONSUMER_CYCLE_BIT);

	ch_stat = GET_RW_32_CH(hdma, dir, ch_stat, dma_wrk->channel);

	dev_dbg(&pdev->dev, "DMA %s CH%d (irq %d, status 0x%x)\n", mode,
		dma_wrk->channel, dma_wrk->id, ch_stat);

	SET_RW_32_CH(hdma, dir, doorbell, dma_wrk->channel,
		     HDMA_V0_DOORBELL_START);

	/*
	 * A chunk whose cycle bit landed while the channel was still running can
	 * finish before the slot above was armed, and the check callback only ever
	 * runs on the next interrupt. Ask the channel once here so such a transfer
	 * is not waited on for the full timeout; if the handler got there first,
	 * the completion is already pending and the wait returns at once.
	 */
	if (axldev->nmsi == 1 && axl_aipu_hdma_dma_irq_ck(axldev, dma_wrk->id))
		err = 1;
	else
		err = wait_for_completion_timeout(
			&axldev->irq_wrk[dma_wrk->id].irq_done,
			axldev->irq_wrk[dma_wrk->id].timeout);

	ch_stat = GET_RW_32_CH(hdma, dir, ch_stat, dma_wrk->channel) &
		  HDMA_V0_CH_STATUS_MASK;
	SET_RW_32_CH(hdma, dir, int_clear, dma_wrk->channel,
		     (HDMA_V0_ABORT_INT_MASK | HDMA_V0_STOP_INT_MASK));
	if (axldev->nmsi == 1)
		state = dma_wait_consume(axldev, dma_wrk->id);

	if (err == 0) {
		/* A transfer that landed right on the deadline is not a timeout */
		bool done = axldev->nmsi == 1 ? state == DMA_WAIT_DONE :
						ch_stat == STATUS_REG_STOPPED;

		dev_err(&pdev->dev,
			"DMA %s CH%d timeout (irq %d, status 0x%x)\n", mode,
			dma_wrk->channel, dma_wrk->id, ch_stat);
		if (!done) {
			dma_wrk->status = -ETIMEDOUT;
			sctx_set_dma_xfer_state(sctx, DMA_XFER_TIMEOUT);
			dma_wrk->qctrl->num_err++;
			return -1;
		}
	} else if (axldev->nmsi == 1) {
		/*
		 * ch_stat is not re-read to validate the transfer: a linked-list
		 * channel is back to RUNNING by now, polling the next descriptor.
		 * The outcome the check recorded is the only stable answer.
		 */
		if (state != DMA_WAIT_DONE) {
			dev_err(&pdev->dev,
				"DMA %s CH%d %s (irq %d, status 0x%x)\n", mode,
				dma_wrk->channel,
				state == DMA_WAIT_ABORTED ? "aborted" :
							    "unclaimed wakeup",
				dma_wrk->id, ch_stat);
			dma_wrk->status = -EIO;
			sctx_set_dma_xfer_state(sctx, DMA_XFER_FAIL);
			dma_wrk->qctrl->num_err++;
			return -1;
		}
	} else if (ch_stat != STATUS_REG_STOPPED) {
		dev_err(&pdev->dev, "DMA error %s CH%d (status 0x%x)\n", mode,
			dma_wrk->channel, ch_stat);
		dma_wrk->status = -EIO;
		sctx_set_dma_xfer_state(sctx, DMA_XFER_FAIL);
		dma_wrk->qctrl->num_err++;
		return -1;
	}
	get_max_duration(dma_wrk);
	return 0;
}
#define DMA_POLL_TIMEOUT 100000
#define DMA_POLL_SPINS	 1000
static inline int dma_wait_poll(struct axl_pcie_aipu_dev *axldev,
				struct dma_wrk *dma_wrk)
{
	volatile struct dw_hdma_v0_regs *hdma = axldev->dma;
	struct pci_dev *pdev = axldev->pdev;
	struct sysctrl_ctx *sctx = dma_wrk->sctx;
	u32 timeout = DMA_POLL_TIMEOUT;
	unsigned long deadline;
	u32 spins = 0;
	u32 ch_stat;
	const char *mode = dma_wrk->flags & DMABUF_XFER_FLAG_READ ? "WR" : "RD";

	enum dw_hdma_dir dir = dma_wrk->flags & DMABUF_XFER_FLAG_READ ?
				       DW_HDMA_DIR_WRITE :
				       DW_HDMA_DIR_READ;

	SET_RW_32_CH(hdma, dir, cycle_sync, dma_wrk->channel,
		     HDMA_V0_CONSUMER_CYCLE_STAT | HDMA_V0_CONSUMER_CYCLE_BIT);

	SET_RW_32_CH(hdma, dir, ch_stat, dma_wrk->channel, 0);
	ch_stat = GET_RW_32_CH(hdma, dir, ch_stat, dma_wrk->channel);
	SET_RW_32_CH(hdma, dir, doorbell, dma_wrk->channel,
		     HDMA_V0_DOORBELL_START);
	dev_dbg(&pdev->dev, "DMA %s CH%d (irq %d, stat 0x%x)\n", mode,
		dma_wrk->channel, dma_wrk->id, ch_stat);

	deadline = jiffies + msecs_to_jiffies(dma_timeout * MSEC_PER_SEC);
	do {
		ch_stat = GET_RW_32_CH(hdma, dir, ch_stat, dma_wrk->channel);
		if (ch_stat == U32_MAX)
			return axl_aipu_dma_dev_gone(axldev, dma_wrk);
		ch_stat &= HDMA_V0_CH_STATUS_MASK;
		if (ch_stat == STATUS_REG_ABORTED)
			break;
		if (!--timeout || time_after(jiffies, deadline)) {
			timeout = 0;
			break;
		}
		if (++spins < DMA_POLL_SPINS)
			udelay(10);
		else
			usleep_range(50, 100);
	} while (ch_stat != STATUS_REG_STOPPED);
	dev_dbg(&pdev->dev, "DMA %s CH%d  timeout %d (irq %d, stat 0x%x)\n",
		mode, dma_wrk->channel, timeout, dma_wrk->id, ch_stat);

	ch_stat = GET_RW_32_CH(hdma, dir, ch_stat, dma_wrk->channel);
	if (ch_stat == U32_MAX)
		return axl_aipu_dma_dev_gone(axldev, dma_wrk);
	ch_stat &= HDMA_V0_CH_STATUS_MASK;
	if ((ch_stat != STATUS_REG_STOPPED) && !timeout) {
		dma_wrk->status = -ETIMEDOUT;
		sctx_set_dma_xfer_state(sctx, DMA_XFER_TIMEOUT);
		dma_wrk->qctrl->num_err++;
		return -1;
	}
	if (ch_stat != STATUS_REG_STOPPED) {
		dev_err(&pdev->dev, "DMA Poll error %s CH%d (stat 0x%x %d)\n",
			mode, dma_wrk->channel, ch_stat, timeout);
		dma_wrk->status = -EIO;
		sctx_set_dma_xfer_state(sctx, DMA_XFER_FAIL);
		dma_wrk->qctrl->num_err++;
		return -1;
	}
	return 0;
}
static inline int dma_wait(struct axl_pcie_aipu_dev *axldev,
			   struct dma_wrk *dma_wrk)
{
	if (dma_poll_mode(dma_wrk))
		return dma_wait_poll(axldev, dma_wrk);

	return dma_wait_irq(axldev, dma_wrk);
}

static void axl_aipu_hdma_dma_job(struct dma_wrk *dma_wrk)
{
	struct axl_pcie_aipu_dev *axldev = dma_wrk->axldev;
	struct pci_dev *pdev = axldev->pdev;

	volatile struct dw_hdma_ll_buf *hwlldch;
	volatile struct dw_hdma_ll_buf *lldch;
	volatile struct dw_hdma_v0_llp *llp;

	struct sg_table *table = dma_wrk->table;
	struct scatterlist *sg;
	int i, n, channel, id;
	size_t tr_size = 0, total_size = 0;
	u64 axi;
	u64 next_llp;
	enum dw_hdma_dir dir = dma_wrk->flags & DMABUF_XFER_FLAG_READ ?
				       DW_HDMA_DIR_WRITE :
				       DW_HDMA_DIR_READ;
	const char *mode = dma_wrk->flags & DMABUF_XFER_FLAG_READ ? "WR" : "RD";

	hwlldch =
		(struct dw_hdma_ll_buf *)axl_aipu_hdma_get_ll_desc_base(axldev);
	lldch = axldev->vbase + axldev->desc_offset;

	dma_wrk->status = 0;

	axi = dma_wrk->axi;
	sg = table->sgl;
	channel = dma_wrk->channel;
	id = dma_wrk->id;

	dev_dbg(&pdev->dev, "DMA %s CH%d work (%p)\n", mode, dma_wrk->channel,
		dma_wrk);

	for (n = dma_wrk->num_sgt; n < table->nents; n += i) {
		dev_dbg(&pdev->dev, "DMA %s CH%d (%d of %d) lldsc 0x%llx\n",
			mode, channel, n, table->nents,
			__get_ll_base(hwlldch, dir, channel));
		axl_aipu_hdma_int_setup(dma_wrk);

		total_size = 0;
		for (i = 0; i < DW_HDMA_LL_MAX_NUM && i < (table->nents - n);
		     sg = sg_next(sg)) {
			__u64 haddr;
			if (sg == NULL) {
				dev_err(&pdev->dev, "SG NULL\n");
				return;
			}
			if (dma_wrk->offset > sg_dma_len(sg)) {
				dma_wrk->offset -= sg_dma_len(sg);
				n++;
				continue;
			}
			if (dma_wrk->offset) {
				haddr = sg_dma_address(sg) + dma_wrk->offset;
				tr_size =
					min_t(size_t,
					      sg_dma_len(sg) - dma_wrk->offset,
					      dma_wrk->size - total_size);
				dma_wrk->offset = 0;
			} else {
				tr_size = min_t(size_t, sg_dma_len(sg),
						dma_wrk->size - total_size);
				haddr = sg_dma_address(sg);
			}
			LL_SET_RW_32_CH(lldch, dir, channel, i, control,
					DW_HDMA_V0_CB);
			LL_SET_RW_32_CH(lldch, dir, channel, i, transfer_size,
					tr_size);
			if (dir == DW_HDMA_DIR_WRITE) {
				LL_SET_RW_64_CH(lldch, dir, channel, i, sar,
						axi);
				LL_SET_RW_64_CH(lldch, dir, channel, i, dar,
						haddr);
			} else {
				LL_SET_RW_64_CH(lldch, dir, channel, i, sar,
						haddr);
				LL_SET_RW_64_CH(lldch, dir, channel, i, dar,
						axi);
			}
			dev_dbg(&pdev->dev,
				"CH%d CTRL=0x%x | SIZE=0x%x | SAR=0x%llx | DAR=0x%llx (%d)\n",
				channel,
				LL_GET_RW_32_CH(lldch, dir, channel, i,
						control),
				LL_GET_RW_32_CH(lldch, dir, channel, i,
						transfer_size),
				LL_GET_RW_64_CH(lldch, dir, channel, i,
						sar.reg),
				LL_GET_RW_64_CH(lldch, dir, channel, i,
						dar.reg),
				i);

			axi += tr_size;
			total_size += tr_size;
			if (total_size == dma_wrk->size) {
				i++;
				break;
			}
			i++;
		}
		dma_wrk->size -= total_size;

		if (i == DW_HDMA_LL_MAX_NUM)
			dev_dbg(&pdev->dev,
				"GO OUT of max channel desc number %d 0x%lx 0x%lx desc\n",
				i, total_size, dma_wrk->size);

		llp = __get_llp(lldch, dir, channel, i);
		writel(DW_HDMA_V0_LLP | DW_HDMA_V0_RIE | DW_HDMA_V0_TCB |
			       DW_HDMA_V0_CB,
		       &llp->control);
		next_llp = (uint64_t)__get_llp(hwlldch, dir, channel, i);
		writeq(next_llp, &llp->llp.reg);
		LL_SET_RW_32_CH(lldch, dir, channel, i + 1, control, 0);

		mb();
		LL_GET_RW_32_CH(lldch, dir, channel, i, control);
		if (!dma_poll_mode(dma_wrk)) {
			reinit_completion(
				&axldev->irq_wrk[dma_wrk->id].irq_done);
		}

		if (dma_wait(axldev, dma_wrk))
			break;

		if (dma_wrk->status || (dma_wrk->size == 0))
			break;
	}
	if ((dir == DW_HDMA_DIR_WRITE) && enable_dmabuf_sync)
		dma_sync_sg_for_cpu(&pdev->dev, table->sgl, table->nents,
				    DMA_FROM_DEVICE);
	get_max_duration(dma_wrk);
}

static void axl_aipu_hdma_dma_p2p_job(struct dma_wrk *dma_wrk)
{
	struct axl_pcie_aipu_dev *axldev = dma_wrk->axldev;
	struct pci_dev *pdev = axldev->pdev;

	volatile struct dw_hdma_v0_regs *hdma = axldev->dma;

	int channel, id;
	__u64 axi, p2pphy;
	enum dw_hdma_dir dir = dma_wrk->flags & DMABUF_XFER_FLAG_READ ?
				       DW_HDMA_DIR_WRITE :
				       DW_HDMA_DIR_READ;
	const char *mode = dma_wrk->flags & DMABUF_XFER_FLAG_READ ? "WR" : "RD";

	dma_wrk->status = 0;

	axi = dma_wrk->axi;
	p2pphy = dma_wrk->p2pphy;
	channel = dma_wrk->channel;
	id = dma_wrk->id;

	dev_dbg(&pdev->dev, "DMA P2P %s CH%d work (%p)\n", mode,
		dma_wrk->channel, dma_wrk);
	SET_RW_32_CH(hdma, dir, watermark_en, channel, WATERMARK_RWIE);
	SET_RW_32_CH(hdma, dir, transfer_size, channel, dma_wrk->size);
	SET_RW_32_CH(hdma, dir, control1, channel, 0);

	if (dir == DW_HDMA_DIR_WRITE) {
		SET_RW_32_CH(hdma, dir, sar.lsb, channel, _LSB(axi));
		SET_RW_32_CH(hdma, dir, sar.msb, channel, _MSB(axi));
		SET_RW_32_CH(hdma, dir, dar.lsb, channel, _LSB(p2pphy));
		SET_RW_32_CH(hdma, dir, dar.msb, channel, _MSB(p2pphy));
	} else {
		SET_RW_32_CH(hdma, dir, dar.lsb, channel, _LSB(axi));
		SET_RW_32_CH(hdma, dir, dar.msb, channel, _MSB(axi));
		SET_RW_32_CH(hdma, dir, sar.lsb, channel, _LSB(p2pphy));
		SET_RW_32_CH(hdma, dir, sar.msb, channel, _MSB(p2pphy));
	}
	dev_dbg(&pdev->dev,
		"CH%d CTRL=0x%x | SIZE=0x%x | SAR=0x%x%08x | DAR=0x%x%08x\n",
		channel, GET_RW_32_CH(hdma, dir, control1, channel),
		GET_RW_32_CH(hdma, dir, transfer_size, channel),
		GET_RW_32_CH(hdma, dir, sar.msb, channel),
		GET_RW_32_CH(hdma, dir, sar.lsb, channel),
		GET_RW_32_CH(hdma, dir, dar.msb, channel),
		GET_RW_32_CH(hdma, dir, dar.lsb, channel));
	if (!dma_poll_mode(dma_wrk)) {
		reinit_completion(&axldev->irq_wrk[dma_wrk->id].irq_done);
	}

	(void)dma_wait(axldev, dma_wrk);
}
static struct axl_dev_fops axl_aipu_hdma_fops = {
	.dma_irq_ck = axl_aipu_hdma_dma_irq_ck,
	.dma_enable_ctrl = axl_aipu_hdma_enable_ctrl,
	.dma_job_submit = axl_aipu_hdma_dma_job,
	.dma_init_imwr = axl_aipu_hdma_init_imwr,
	.dma_p2p_job_submit = axl_aipu_hdma_dma_p2p_job,
	.dev_dynmem_init = axl_aipu_hdma_dev_dynmem_init,

	.dev_debugfs_init = axl_aipu_hdma_dev_debugfs_init,
	.dev_debugfs_exit = axl_aipu_hdma_dev_debugfs_exit,
};

void axl_aipu_hdma_register_dev_fops(struct axl_pcie_aipu_dev *axldev)
{
	axldev->fops = &axl_aipu_hdma_fops;
}
