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

#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/cdev.h>
#include <linux/msi.h>
#include <linux/completion.h>
#include <linux/io.h>

#include "axl-aipu-dmabuf.h"
#include "axl-aipu.h"

/* Per-notification wait when the caller passes 0, not a whole-command budget. */
#define AXL_SYSMSG_DEF_TIMEOUT_S 10
#define AXL_SYSMSG_MAX_TIMEOUT_S 600

static bool sysmsg_status_final(u32 status)
{
	switch (status) {
	case CMD_IDLE:
	case CMD_DONE:
	case CMD_FAIL:
	case CMD_UNSUPPORTED:
	case CMD_UNKNOWN: return true;
	default: return false;
	}
}

/**
 * axl_aipu_send_msg() - Run one firmware system-message round trip
 * @axldev: Device context
 * @cmd: Command to send; overwritten with the device reply on success
 * @timeout: Seconds per firmware notification, 0 for the default
 * @status: Out, optional. Last status seen, set even on -ETIMEDOUT
 *
 * Return: 0 whenever the device answered, CMD_FAIL included - that is reported
 * through @status with the firmware's text left in @cmd. Negative only for
 * transport failures.
 */
int axl_aipu_send_msg(struct axl_pcie_aipu_dev *axldev, struct cmd_t *cmd,
		      unsigned int timeout, u32 *status)
{
	struct pci_dev *pdev = axldev->pdev;
	struct completion *msg_done;
	void __iomem *cmd_io;
	unsigned long deadline;
	unsigned long wait;
	u32 sts = CMD_IDLE;
	u32 opcode = cmd->opcode;
	long ret;

	if (!timeout)
		timeout = AXL_SYSMSG_DEF_TIMEOUT_S;

	if (timeout > AXL_SYSMSG_MAX_TIMEOUT_S)
		return -EINVAL;

	if (axldev->dev_state || !axldev->vbase)
		return -ENODEV;

	if (AXL_SYSMSG_MSI >= axldev->max_msi) {
		dev_err(&pdev->dev, "MSG slot %d beyond max_msi %d\n",
			AXL_SYSMSG_MSI, axldev->max_msi);
		return -ENODEV;
	}

	if (mutex_lock_interruptible(&axldev->msg_mutex))
		return -ERESTARTSYS;

	cmd_io = axldev->vbase + offsetof(struct device_sys_ctl_t, cmd);
	msg_done = &axldev->irq_wrk[AXL_SYSMSG_MSI].irq_done;

	/* Arm before sending: a leftover notification would satisfy the wait. */
	reinit_completion(msg_done);

	/*
	 * Firmware polls cmd.opcode to spot a new command, so the payload has to
	 * land first. A forward copy of cmd_t would publish opcode first.
	 */
	iowrite32(cmd->size, cmd_io + offsetof(struct cmd_t, size));
	memcpy_toio(cmd_io + offsetof(struct cmd_t, msg), cmd->msg, MAX_MSG);
	wmb();
	iowrite32(opcode, cmd_io + offsetof(struct cmd_t, opcode));

	dev_dbg(&pdev->dev, "sysmsg 0x%x size %u timeout %us\n", opcode,
		cmd->size, timeout);

	wait = msecs_to_jiffies(timeout * 1000);
	deadline = jiffies + msecs_to_jiffies(AXL_SYSMSG_MAX_TIMEOUT_S * 1000);

	while (time_before(jiffies, deadline)) {
		ret = wait_for_completion_killable_timeout(msg_done, wait);
		if (ret < 0) {
			ret = -EINTR;
			goto out;
		}

		sts = ioread32(cmd_io + offsetof(struct cmd_t, opcode));
		if (sysmsg_status_final(sts)) {
			rmb();
			cmd->opcode = sts;
			cmd->size =
				ioread32(cmd_io + offsetof(struct cmd_t, size));
			memcpy_fromio(cmd->msg,
				      cmd_io + offsetof(struct cmd_t, msg),
				      MAX_MSG);
			ret = 0;
			goto out;
		}

		if (!ret) {
			dev_dbg(&pdev->dev,
				"sysmsg 0x%x timeout after %us (status 0x%x)\n",
				opcode, timeout, sts);
			ret = -ETIMEDOUT;
			goto out;
		}
	}

	dev_err(&pdev->dev,
		"sysmsg 0x%x: no final status after %us (last 0x%x)\n", opcode,
		AXL_SYSMSG_MAX_TIMEOUT_S, sts);
	ret = -ETIMEDOUT;

out:
	mutex_unlock(&axldev->msg_mutex);

	if (status)
		*status = sts;

	return ret;
}
