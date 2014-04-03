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

#define BCWC_PCI_DEV_IO  0
#define BCWC_PCI_DEV_MEM 2
#define BCWC_PCI_LINK_IO 4

struct bcwc_private {
	struct pci_dev *pdev;
	unsigned int dma_mask;

	/* Mapped PCI resources */
	void *link_io;
	u32 link_io_len;
	void *dev_io;
	u32 dev_io_len;

	struct work_struct irq_work;

	/* Hardware info */
	u32 core_clk;
};

#endif
