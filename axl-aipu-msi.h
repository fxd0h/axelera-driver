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

#ifndef __AXL_AIPU_MSI_H__
#define __AXL_AIPU_MSI_H__

struct axl_pcie_aipu_dev;

/**
 * axl_aipu_register_msi_fops() - Register MSI function pointers
 * @axldev: Device context
 *
 * Registers the VMSI handlers for every generation: the trigger map is used
 * when firmware publishes one, and the 1:1 mapping - all Metis ever uses -
 * otherwise.
 */
void axl_aipu_register_msi_fops(struct axl_pcie_aipu_dev *axldev);

/**
 * is_vmsi_enabled() - Check if VMSI is enabled for device
 * @axldev: Device context
 *
 * Return: true if VMSI is enabled, false otherwise
 */
bool is_vmsi_enabled(struct axl_pcie_aipu_dev *axldev);

/**
 * get_vmsi_count() - Get VMSI interrupt count
 * @axldev: Device context
 * @id: VMSI index
 *
 * Return: Number of interrupts received for this VMSI
 */
int get_vmsi_count(struct axl_pcie_aipu_dev *axldev, int id);

/**
 * clear_vmsi_count() - Clear VMSI interrupt count
 * @axldev: Device context
 * @id: VMSI index
 */
void clear_vmsi_count(struct axl_pcie_aipu_dev *axldev, int id);

/* Module parameters */
extern unsigned int irq_timeout;

#endif /* __AXL_AIPU_MSI_H__ */
