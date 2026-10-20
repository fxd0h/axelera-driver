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
 * SPDX-License-Identifier: GPL-2.0
 *
 */

#include <linux/version.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/cdev.h>
#include <linux/msi.h>
#include <linux/completion.h>
#include <linux/dma-buf.h>
#include <linux/poll.h>
#include "axl-aipu-dmabuf.h"
#include "axl-aipu.h"
#include "axl-aipu-msi.h"
#include "axl-aipu-fwtrace.h"

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

static irqreturn_t axl_aipu_irq_fn(int irq, void *data);
static irqreturn_t axl_aipu_irq_common_fn(int irq, void *data);
static irqreturn_t axl_aipu_irq_handler(int irq, void *data);

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * irq_poll_check() - Wake up poll waiters for MSI
 * @axldev: Device context
 * @msi: MSI/VMSI index
 */
static void irq_poll_check(struct axl_pcie_aipu_dev *axldev, int msi)
{
	struct sysctrl_ctx *sys_ctx;
	struct irq_wrk *irq_wrk = axldev->irq_wrk;

	spin_lock(&axldev->msi_lock);
	list_for_each_entry(sys_ctx, &irq_wrk[msi].sctx_list, node)
	{
		dev_dbg(&axldev->pdev->dev, "Wake up poll at %p for MSI%d\n",
			sys_ctx, msi);
		atomic_inc(&sys_ctx->poll_event_cnt);
		wake_up_interruptible_poll(&sys_ctx->poll_wait_queue, EPOLLIN);
	}
	spin_unlock(&axldev->msi_lock);
}

bool is_vmsi_enabled(struct axl_pcie_aipu_dev *axldev)
{
	return axldev->hdrv_base != NULL;
}

static bool is_metis(struct axl_pcie_aipu_dev *axldev)
{
	return axldev->dev_info->hw_gen == AXL_HW_GEN_METIS;
}

/**
 * krn_irq_ck() - Ask a Metis kernel slot whether its context finished
 * @axldev: Device context
 * @id: MSI slot, which on Metis is the context index
 *
 * Metis publishes no VMSI trigger map, so a kernel slot has no token to test on
 * a shared vector; the context status word is what says whether it completed.
 *
 * Return: 1 if the context is no longer running, 0 otherwise.
 */
static int krn_irq_ck(struct axl_pcie_aipu_dev *axldev, int id)
{
	struct device_sys_ctl_t *dsctl = axldev->vbase;
	struct device_ctx_t *devctx =
		(struct device_ctx_t *)((uintptr_t)dsctl +
					dsctl->ctx_mem_ref.offset);
	struct pci_dev *pdev = axldev->pdev;
	int sts = readl(&devctx[id].sts);

	if (sts > 0)
		return 0;

	dev_dbg(&pdev->dev, "wake KRN %d (sts %s)\n", id,
		sts == 0 ? "done" : "fail");

	return 1;
}

/* ============================================================================
 * Shared MSI Machinery (used by every hardware generation)
 * ============================================================================ */

/**
 * axl_aipu_msi_wrk_init() - Reset every irq_wrk slot
 * @axldev: Device context
 *
 * Covers the whole array, so it serves every hardware generation regardless of
 * how many slots that generation goes on to use. Check callbacks are cleared;
 * axl_aipu_msi_map() installs the ones the generation needs (single-MSI only).
 */
static void axl_aipu_msi_wrk_init(struct axl_pcie_aipu_dev *axldev)
{
	int i;

	for (i = 0; i < MAX_VIRT_MSI; i++) {
		axldev->irq_wrk[i].axldev = axldev;
		axldev->irq_wrk[i].id = i;
		axldev->irq_wrk[i].timeout = get_timeout_ms(irq_timeout);
		spin_lock_init(&axldev->irq_wrk[i].irq_lock);
		init_completion(&axldev->irq_wrk[i].irq_done);
		atomic_set(&axldev->irq_wrk[i].dma_done, 0);
		INIT_LIST_HEAD(&axldev->irq_wrk[i].sctx_list);

		/* check callbacks are only used in single-MSI mode */
		axldev->irq_wrk[i].check = NULL;
	}
}

/**
 * axl_aipu_msi_request_single() - Register the one handler of single-MSI mode
 * @axldev: Device context
 * @thread_fn: Threaded handler that scans the MSI slots
 *
 * Return: 0 on success, negative error code on failure
 */
static int axl_aipu_msi_request_single(struct axl_pcie_aipu_dev *axldev,
				       irq_handler_t thread_fn)
{
	struct pci_dev *pdev = axldev->pdev;
	char irq_name[NAME_SIZE];
	char *msi_name;

	dev_dbg(&pdev->dev, "Init irq handler for single msi\n");

	snprintf(irq_name, NAME_SIZE - 1, "msi-%s-%d", axldev->name, 0);
	msi_name = devm_kstrdup(&pdev->dev, irq_name, GFP_KERNEL);

	return devm_request_threaded_irq(&pdev->dev, pdev->irq,
					 axl_aipu_irq_handler, thread_fn,
					 IRQF_SHARED | IRQF_ONESHOT, msi_name,
					 &axldev->irq_wrk[0]);
}

/**
 * axl_aipu_vmsi_signal() - Release the waiters on one VMSI slot
 * @axldev: Device context
 * @vmsi_id: VMSI slot to signal
 *
 * For callers that already hold evidence the slot fired: a VMSI token that was
 * set, or a vector that identifies the source by itself. No check callback is
 * consulted, so a token-bearing slot is never gated on one.
 */
static void axl_aipu_vmsi_signal(struct axl_pcie_aipu_dev *axldev, int vmsi_id)
{
	struct irq_wrk *irq_wrk = &axldev->irq_wrk[vmsi_id];

	complete(&irq_wrk->irq_done);
	irq_poll_check(axldev, vmsi_id);
}

/**
 * axl_aipu_vmsi_signal_hw() - Ask a HW source whether it fired, then wake it
 * @axldev: Device context
 * @vmsi_id: VMSI slot to poll and signal
 *
 * For the single-MSI sweeps, which visit every slot on every interrupt because
 * the shared vector names no source. A HW source leaves no token, so its check
 * callback is the only evidence available, and a slot without one cannot be
 * attributed - waking it regardless is what fabricates the completions that a
 * DMA waiter then reports as an unclaimed wakeup.
 *
 * Return: true if the source confirmed and the waiters were released.
 */
static bool axl_aipu_vmsi_signal_hw(struct axl_pcie_aipu_dev *axldev,
				    int vmsi_id)
{
	struct irq_wrk *irq_wrk = &axldev->irq_wrk[vmsi_id];

	if (!irq_wrk->check || !irq_wrk->check(axldev, vmsi_id))
		return false;

	axl_aipu_vmsi_signal(axldev, vmsi_id);
	return true;
}

/**
 * axl_aipu_msi_scan_flat() - Single-MSI scan of a 1:1 PMSI-to-VMSI mapping
 * @axldev: Device context
 * @msg_slots: Slots to wake unconditionally when the token area is not live
 * @num_msg_slots: Number of entries in @msg_slots
 *
 * Visits every slot in turn - VMSI token first while the token area is live,
 * then the slot's check callback. Used by the generations that have no firmware
 * trigger map to narrow the scan with: Metis always, and Europa while the
 * firmware has exported no VMSI config area.
 */
static void axl_aipu_msi_scan_flat(struct axl_pcie_aipu_dev *axldev,
				   const u8 *msg_slots, int num_msg_slots)
{
	struct device_virt_msi_t *vmsi =
		(struct device_virt_msi_t *)axldev->dma_va;
	bool vmsi_en = is_vmsi_enabled(axldev);
	int id, i;

	/*
	 * Without the token area there is nothing to tell whether a message
	 * slot fired, so those are woken on every interrupt; every other
	 * token-less slot is left to its check callback, if it has one.
	 */
	if (!vmsi_en) {
		for (i = 0; i < num_msg_slots; i++) {
			id = msg_slots[i];
			if (id >= axldev->max_msi)
				continue;
			axl_aipu_vmsi_signal(axldev, id);
		}
	}

	for (id = 0; id < axldev->max_msi; id++) {
		if (vmsi_en && (vmsi->msi[id] & VMSI_IRQ_EN)) {
			dev_dbg(&axldev->pdev->dev, "VMSI (%d)\n", id);
			vmsi->msi[id] = 0;
			atomic_inc(&axldev->vmsi_count[id]);
			axl_aipu_vmsi_signal(axldev, id);
			if (fwtrace_vmsi_is_source(&axldev->fwtrace, id))
				axl_fwtrace_msi_handler(axldev, id);
			continue;
		}
		/* Token-less slot: only its own source can confirm it fired */
		axl_aipu_vmsi_signal_hw(axldev, id);
	}
}

int get_vmsi_count(struct axl_pcie_aipu_dev *axldev, int id)
{
	return atomic_read(&axldev->vmsi_count[id]);
}

void clear_vmsi_count(struct axl_pcie_aipu_dev *axldev, int id)
{
	atomic_set(&axldev->vmsi_count[id], 0);
}

/**
 * hw_pmsi_to_vmsi() - Resolve the VMSI slot a HW-sourced PMSI signals
 * @axldev: Device context
 * @pmsi_id: Physical MSI (PMSI) index of a HW source (e.g. a DMA channel)
 *
 * Only HW sources have a unique 1:1 PMSI-to-VMSI mapping; this returns the
 * firmware-assigned VMSI slot for such a PMSI, mirroring the HW-source path in
 * the IRQ handlers so DMA waiters block on the slot the handler completes.
 * Falls back to a 1:1 mapping when no firmware VMSI config is present.
 *
 * Not for FW sources: those span a VMSI range (vmsi_range), not a single slot,
 * and a FW-sourced @pmsi_id is returned unchanged.
 */
static int hw_pmsi_to_vmsi(struct axl_pcie_aipu_dev *axldev, int pmsi_id)
{
	struct device_vmsi_config_t *msi_cfg = axldev->msi_cfg;
	struct vmsi_info_t *info;

	if (!axl_aipu_has_vmsi_cfg(axldev) || pmsi_id < 0 ||
	    pmsi_id >= msi_cfg->num_pmsi)
		return pmsi_id;

	info = &msi_cfg->pmsi_to_vmsi_info[pmsi_id];
	if (info->enabled && info->source == VMSI_SOURCE_HW)
		return info->vmsi_id;

	return pmsi_id;
}

/**
 * axl_aipu_dma_set_check() - Set (or clear) the DMA check callback on the block
 * @axldev: Device context
 * @check: Callback to install, or NULL to clear
 *
 * Applies @check to every slot of the contiguous DMA MSI block
 * anchored at the current dma_msi_base. Single-MSI only.
 */
static void axl_aipu_dma_set_check(struct axl_pcie_aipu_dev *axldev,
				   int (*check)(struct axl_pcie_aipu_dev *,
						int))
{
	const int n_dma = PMSI_DMA_WR_CH3 - PMSI_DMA_RD_CH0 + 1;
	int i;

	if (axldev->nmsi != 1)
		return;

	for (i = 0; i < n_dma; i++) {
		int slot = axldev->dma_msi_base + i;

		if (slot < axldev->max_msi)
			axldev->irq_wrk[slot].check = check;
	}

	dev_info(&axldev->pdev->dev,
		 "DMA check %s on VMSI %d-%d (max_msi %d)\n",
		 check ? "installed" : "cleared", axldev->dma_msi_base,
		 axldev->dma_msi_base + n_dma - 1, axldev->max_msi);
}

/**
 * axl_aipu_msi_map() - Derive DMA MSI state from the firmware VMSI config
 * @axldev: Device context
 *
 * Traces the PMSI->VMSI mapping, resolves dma_msi_base, and installs the DMA
 * check callbacks. Used at init time, where the check callbacks start clear.
 */
static void axl_aipu_msi_map(struct axl_pcie_aipu_dev *axldev)
{
	struct device_vmsi_config_t *msi_cfg = axldev->msi_cfg;
	int i;

	for (i = 0; axl_aipu_has_vmsi_cfg(axldev) && i < msi_cfg->num_pmsi;
	     i++) {
		struct vmsi_info_t *info = &msi_cfg->pmsi_to_vmsi_info[i];

		if (!info->enabled)
			continue;
		if (info->source == VMSI_SOURCE_FW)
			dev_dbg(&axldev->pdev->dev,
				"PMSI %d -> VMSI [%d-%d] (FW)\n", i,
				info->vmsi_range.start, info->vmsi_range.end);
		else
			dev_dbg(&axldev->pdev->dev, "PMSI %d -> VMSI %u (HW)\n",
				i, info->vmsi_id);
	}

	axldev->dma_msi_base = hw_pmsi_to_vmsi(axldev, PMSI_DMA_RD_CH0);
	axl_aipu_dma_set_check(axldev, axl_aipu_dma_irq_ck);

	/*
	 * Metis kernel slots answer through the context status word. Assigned
	 * either way so that the reload path, which re-runs this, leaves no stale
	 * callback behind; with the token area live the token is the evidence and
	 * no callback is wanted.
	 */
	if (is_metis(axldev) && axldev->nmsi == 1)
		for (i = PMSI_METIS_KRN_0; i <= PMSI_METIS_KRN_3; i++)
			axldev->irq_wrk[i].check =
				is_vmsi_enabled(axldev) ? NULL : krn_irq_ck;

	/*
	 * A HW-sourced trigger leaves no token, so without a check callback
	 * axl_aipu_vmsi_signal_hw() has no evidence and will not wake its slot.
	 * Report the gap here rather than let the waiters stall in silence.
	 */
	for (i = 0; axldev->nmsi == 1 && axl_aipu_has_vmsi_cfg(axldev) &&
		    i < msi_cfg->num_pmsi;
	     i++) {
		struct vmsi_info_t *info = &msi_cfg->pmsi_to_vmsi_info[i];

		if (!info->enabled || info->source != VMSI_SOURCE_HW)
			continue;
		if (info->vmsi_id < axldev->max_msi &&
		    !axldev->irq_wrk[info->vmsi_id].check)
			dev_warn(
				&axldev->pdev->dev,
				"PMSI %d -> VMSI %u is HW-sourced with no check callback: its waiters will not be woken\n",
				i, info->vmsi_id);
	}
}

/**
 * axl_aipu_msi_remap() - Re-derive DMA MSI state after a config change
 * @axldev: Device context
 *
 * Reload path (msi_fops.remap, invoked from config_dev_msi): the VMSI config may
 * have changed, so drop the check callbacks on the stale block before re-mapping
 * onto the freshly resolved one.
 */
static void axl_aipu_msi_remap(struct axl_pcie_aipu_dev *axldev)
{
	axl_aipu_dma_set_check(axldev, NULL);
	axl_aipu_msi_map(axldev);
}

/* ============================================================================
 * MSI Initialization with Trigger Mapping
 * ============================================================================ */

/**
 * axl_aipu_msi_init() - Initialize and register IRQ handlers with trigger mapping
 * @axldev: Device context
 *
 * Registers IRQ handlers based on trigger group configuration.
 * For single-MSI mode: registers one handler for all triggers.
 * For multi-MSI mode: registers handlers for each configured trigger range.
 *
 * Return: 0 on success, negative error code on failure
 */
static int axl_aipu_msi_init(struct axl_pcie_aipu_dev *axldev)
{
	struct pci_dev *pdev = axldev->pdev;
	struct device_vmsi_config_t *msi_cfg = axldev->msi_cfg;
	int err, i;

	if (axl_aipu_has_vmsi_cfg(axldev))
		dev_info(
			&pdev->dev,
			"Initializing MSI with VMSI config (nmsi=%d, pmsi=%d)\n",
			axldev->nmsi, msi_cfg->num_pmsi);
	else
		dev_info(
			&pdev->dev,
			"Initializing MSI without VMSI config (nmsi=%d, max_msi=%d)\n",
			axldev->nmsi, axldev->max_msi);

	axl_aipu_msi_wrk_init(axldev);

	/* Resolve DMA base slots + single-MSI check callbacks from msi_cfg. */
	axl_aipu_msi_map(axldev);

	if (axldev->nmsi == 1) {
		/* Single-MSI mode: one handler scans all VMSI entries */
		err = axl_aipu_msi_request_single(axldev,
						  axl_aipu_irq_common_fn);
		if (err)
			return err;
	} else if (axl_aipu_has_vmsi_cfg(axldev)) {
		/* Multi-MSI mode with VMSI config: use trigger mapping */
		dev_dbg(&pdev->dev,
			"Init irq handlers for multi-msi with VMSI mapping\n");

		for (i = 0; i < PMSI_MAX && i < axldev->nmsi; i++) {
			char irq_name[NAME_SIZE];
			char *msi_name;

			snprintf(irq_name, NAME_SIZE - 1, "msi-%s-p%d",
				 axldev->name, i);
			msi_name =
				devm_kstrdup(&pdev->dev, irq_name, GFP_KERNEL);
			err = devm_request_threaded_irq(
				&pdev->dev, pdev->irq + i, axl_aipu_irq_handler,
				axl_aipu_irq_fn, IRQF_SHARED | IRQF_ONESHOT,
				msi_name, &axldev->irq_wrk[i]);
			if (err)
				return err;
		}
	} else {
		/* Multi-MSI mode without VMSI config: 1:1 PMSI-to-VMSI mapping */
		dev_dbg(&pdev->dev,
			"Init irq handlers for multi-msi with 1:1 mapping\n");

		for (i = 0; i < axldev->nmsi; i++) {
			char irq_name[NAME_SIZE];
			char *msi_name;

			snprintf(irq_name, NAME_SIZE - 1, "msi-%s-p%d",
				 axldev->name, i);
			msi_name =
				devm_kstrdup(&pdev->dev, irq_name, GFP_KERNEL);
			/*
			 * Metis takes these in hard IRQ context, as it always
			 * has: it publishes no trigger map, so this handler
			 * cannot later start walking VMSI ranges the way it does
			 * on Europa once firmware exports one.
			 */
			if (is_metis(axldev))
				err = devm_request_irq(&pdev->dev,
						       pdev->irq + i,
						       axl_aipu_irq_fn,
						       IRQF_SHARED, msi_name,
						       &axldev->irq_wrk[i]);
			else
				err = devm_request_threaded_irq(
					&pdev->dev, pdev->irq + i,
					axl_aipu_irq_handler, axl_aipu_irq_fn,
					IRQF_SHARED | IRQF_ONESHOT, msi_name,
					&axldev->irq_wrk[i]);
			if (err)
				return err;
		}
	}

	get_cached_msi_msg(axldev->irq_vec, &axldev->irq_msi);
	axl_aipu_dma_init_imwr(axldev);

	return 0;
}

/* ============================================================================
 * MSI Function Operations Registration
 * ============================================================================ */

static struct axl_msi_fops msi_fops = {
	.init = axl_aipu_msi_init,
	.remap = axl_aipu_msi_remap,
};

void axl_aipu_register_msi_fops(struct axl_pcie_aipu_dev *axldev)
{
	axldev->msi_fops = &msi_fops;
}

/* ============================================================================
 * VMSI Interrupt Handlers (with trigger config)
 * ============================================================================ */

/**
 * axl_aipu_irq_handler() - Hard IRQ handler
 * @irq: IRQ number
 * @data: Pointer to irq_wrk structure
 *
 * Quick handler that wakes the threaded handler.
 *
 * Return: IRQ_WAKE_THREAD
 */
static irqreturn_t axl_aipu_irq_handler(int irq, void *data)
{
	struct irq_wrk *irwq_elem = data;
	struct axl_pcie_aipu_dev *axldev = irwq_elem->axldev;
	struct pci_dev *pdev = axldev->pdev;

	/* Quick operations only - just acknowledge and wake thread */
	dev_dbg(&pdev->dev, "Hard IRQ %d triggered\n", irq);

	/* Return IRQ_WAKE_THREAD to run the threaded handler */
	return IRQ_WAKE_THREAD;
}

/**
 * axl_aipu_irq_fn() - Multi-MSI interrupt handler with VMSI config
 * @irq: IRQ number
 * @data: Pointer to irq_wrk structure
 *
 * Handles interrupts when using multi-MSI mode with trigger grouping.
 * Uses firmware-configured trigger mapping to efficiently scan VMSI ranges.
 *
 * Return: IRQ_HANDLED
 */
static irqreturn_t axl_aipu_irq_fn(int irq, void *data)
{
	struct irq_wrk *irwq_elem = data;
	struct axl_pcie_aipu_dev *axldev = irwq_elem->axldev;
	struct pci_dev *pdev = axldev->pdev;
	int id = irq - axldev->irq_vec;
	struct device_virt_msi_t *vmsi =
		(struct device_virt_msi_t *)axldev->dma_va;
	struct device_vmsi_config_t *msi_cfg = axldev->msi_cfg;
	struct vmsi_info_t *info;

	dev_dbg(&pdev->dev, "%d interrupt (%d)\n", id, irq);

	if (id >= PMSI_MAX) {
		dev_err(&pdev->dev, "Invalid MSI id %d (max %d)\n", id,
			PMSI_MAX);
		return IRQ_HANDLED;
	}

	if (!axl_aipu_has_vmsi_cfg(axldev)) {
		/*
		 * No VMSI config - always the case on Metis: 1:1 PMSI-to-VMSI
		 * mapping. The vector already names the source, so the slot is
		 * woken either way, and a token is consumed on the way past if
		 * the area is live.
		 */
		if (id < axldev->max_msi) {
			if (vmsi->msi[id] & VMSI_IRQ_EN) {
				vmsi->msi[id] = 0;
				atomic_inc(&axldev->vmsi_count[id]);
			}
			complete(&axldev->irq_wrk[id].irq_done);
			irq_poll_check(axldev, id);
			if (fwtrace_vmsi_is_source(&axldev->fwtrace, id))
				axl_fwtrace_msi_handler(axldev, id);
		}
		return IRQ_HANDLED;
	}

	info = &msi_cfg->pmsi_to_vmsi_info[id];

	if (!info->enabled)
		return IRQ_HANDLED;

	if (info->source == VMSI_SOURCE_HW) {
		/* HW-sourced: no VMSI_IRQ_EN check */
		int vmsi_id = info->vmsi_id;

		if (vmsi_id < axldev->max_msi) {
			atomic_inc(&axldev->vmsi_count[vmsi_id]);
			complete(&axldev->irq_wrk[vmsi_id].irq_done);
			irq_poll_check(axldev, vmsi_id);
			dev_dbg(&pdev->dev, "HW VMSI %d (trigger %d)\n",
				vmsi_id, id);
		}
		axl_fwtrace_hw_pmsi_handler(axldev, id);
	} else {
		/* FW-sourced: scan range for VMSI_IRQ_EN */
		int vmsi_id;

		for (vmsi_id = info->vmsi_range.start;
		     vmsi_id <= info->vmsi_range.end; vmsi_id++) {
			if (vmsi_id >= axldev->max_msi)
				break;
			if (vmsi->msi[vmsi_id] & VMSI_IRQ_EN) {
				vmsi->msi[vmsi_id] = 0;
				atomic_inc(&axldev->vmsi_count[vmsi_id]);
				complete(&axldev->irq_wrk[vmsi_id].irq_done);
				irq_poll_check(axldev, vmsi_id);
				if (fwtrace_vmsi_is_source(&axldev->fwtrace,
							   vmsi_id))
					axl_fwtrace_msi_handler(axldev,
								vmsi_id);
				dev_dbg(&pdev->dev,
					"FW VMSI %d in trigger %d\n", vmsi_id,
					id);
			}
		}
	}

	return IRQ_HANDLED;
}

/* Slots woken on every interrupt when no VMSI token area is live */
static const u8 europa_msg_slots[] = { PMSI_MSG };
static const u8 metis_msg_slots[] = { PMSI_METIS_MSG };

/*
 * Metis has no firmware trigger map, so its single-MSI handler always lands
 * here; Europa does until firmware publishes one.
 */
static void axl_aipu_msi_scan_gen(struct axl_pcie_aipu_dev *axldev)
{
	if (is_metis(axldev))
		axl_aipu_msi_scan_flat(axldev, metis_msg_slots,
				       ARRAY_SIZE(metis_msg_slots));
	else
		axl_aipu_msi_scan_flat(axldev, europa_msg_slots,
				       ARRAY_SIZE(europa_msg_slots));
}

/**
 * axl_aipu_irq_common_fn() - Single-MSI interrupt handler
 * @irq: IRQ number
 * @data: Pointer to irq_wrk structure
 *
 * All sources share one vector, so the source that fired is unknown and every
 * slot has to be visited. Uses the firmware trigger mapping when present; when
 * it is not - which is also the state between MSI registration and the first
 * firmware boot - the PMSI-to-VMSI mapping is 1:1, exactly as in
 * axl_aipu_irq_fn(), and the shared flat scan applies.
 *
 * Return: IRQ_HANDLED
 */
static irqreturn_t axl_aipu_irq_common_fn(int irq, void *data)
{
	struct irq_wrk *irwq_elem = data;
	struct axl_pcie_aipu_dev *axldev = irwq_elem->axldev;
	struct pci_dev *pdev = axldev->pdev;
	int id = irq - axldev->irq_vec;
	struct device_virt_msi_t *vmsi =
		(struct device_virt_msi_t *)axldev->dma_va;
	struct device_vmsi_config_t *msi_cfg = axldev->msi_cfg;
	int trigger_id, num_pmsi;

	dev_dbg(&pdev->dev, "%d interrupt (%d)  hdrv %p\n", id, irq,
		axldev->hdrv_base);

	if (!msi_cfg) {
		axl_aipu_msi_scan_gen(axldev);
		return IRQ_HANDLED;
	}

	/*
	 * Loop through configured PMSI mappings. Only the first num_pmsi entries
	 * were written by firmware: past that the table holds whatever the area
	 * came up with, and an entry that happens to read back enabled would send
	 * this sweep at a slot no source stands behind.
	 */
	num_pmsi = msi_cfg->num_pmsi;
	if (num_pmsi > PMSI_MAX)
		num_pmsi = PMSI_MAX;

	for (trigger_id = 0; trigger_id < num_pmsi; trigger_id++) {
		struct vmsi_info_t *info =
			&msi_cfg->pmsi_to_vmsi_info[trigger_id];

		if (!info->enabled)
			continue;

		if (info->source == VMSI_SOURCE_HW) {
			/*
			 * HW-sourced: no VMSI_IRQ_EN token to test, and this
			 * sweep runs for every trigger on every interrupt, so
			 * the check callback is the only evidence that this
			 * source is the one that fired.
			 */
			int vmsi_id = info->vmsi_id;

			if (vmsi_id >= axldev->max_msi)
				continue;

			if (axl_aipu_vmsi_signal_hw(axldev, vmsi_id))
				atomic_inc(&axldev->vmsi_count[vmsi_id]);
			axl_fwtrace_hw_pmsi_handler(axldev, trigger_id);
		} else {
			/* FW-sourced: scan range for VMSI_IRQ_EN */
			int vmsi_id;

			for (vmsi_id = info->vmsi_range.start;
			     vmsi_id <= info->vmsi_range.end; vmsi_id++) {
				if (vmsi_id >= axldev->max_msi)
					break;
				if (!(vmsi->msi[vmsi_id] & VMSI_IRQ_EN))
					continue;

				dev_dbg(&pdev->dev, "VMSI %d in PMSI %d\n",
					vmsi_id, trigger_id);
				vmsi->msi[vmsi_id] = 0;
				atomic_inc(&axldev->vmsi_count[vmsi_id]);
				axl_aipu_vmsi_signal(axldev, vmsi_id);
				if (fwtrace_vmsi_is_source(&axldev->fwtrace,
							   vmsi_id))
					axl_fwtrace_msi_handler(axldev,
								vmsi_id);
			}
		}
	}

	return IRQ_HANDLED;
}
