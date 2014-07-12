/*
 * Broadcom PCIe 1570 webcam driver
 *
 * Copyright (C) 2014 Patrik Jakobsson (patrik.r.jakobsson@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 */

#ifndef _BCWC_PCIE_H
#define _PCWC_PCIE_H

#include <linux/pci.h>
#include "bcwc_reg.h"

#define BCWC_PCI_S2_IO  0
#define BCWC_PCI_S2_MEM 2
#define BCWC_PCI_ISP_IO 4

struct bcwc_reg {
	u32 offset;
	u32 value;
};

struct bcwc_private {
	struct pci_dev *pdev;
	unsigned int dma_mask;

	/* Mapped PCI resources */
	void *s2_io;
	u32 s2_io_len;
	void *isp_io;
	u32 isp_io_len;

	struct work_struct irq_work;

	/* Hardware info */
	u32 core_clk;
	u32 ddr_model;
	u32 ddr_speed;
	u32 vdl_step_size;

	/* DDR_PHY saved registers. Offsets need to be initialized somewhere */
	u32 ddr_phy_num_regs;
	struct bcwc_reg ddr_phy_reg_map[DDR_PHY_NUM_REGS];
};

#endif
