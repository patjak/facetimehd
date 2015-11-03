/*
 * Broadcom PCIe 1570 webcam driver
 *
 * Copyright (C) 2014 Patrik Jakobsson (patrik.r.jakobsson@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation.
 *
 */

#ifndef _BCWC_PCIE_H
#define _PCWC_PCIE_H

#include <linux/pci.h>
#include <linux/wait.h>

#include "bcwc_reg.h"
#include "bcwc_ringbuf.h"

#define BCWC_PCI_S2_IO  0
#define BCWC_PCI_S2_MEM 2
#define BCWC_PCI_ISP_IO 4

#define MAX(a, b) ((a)>(b)?(a):(b))

struct bcwc_reg {
	u32 offset;
	u32 value;
};

struct fw_channel {
	u32 offset;
	u32 size;
	u32 source;
	u32 type;
	struct bcwc_ringbuf ringbuf;
	char *name;
};

struct bcwc_private {
	struct pci_dev *pdev;
	unsigned int dma_mask;

	/* waitqueue for signaling command completion */
	wait_queue_head_t wq;
	
	/* Mapped PCI resources */
	void *s2_io;
	u32 s2_io_len;

	void *s2_mem;
	u32 s2_mem_len;

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

	/* Root resource for memory management */
	struct resource *mem;

	/* ISP memory objects */
	struct isp_mem_obj *firmware;
	struct isp_mem_obj *ipc_queue;
	struct isp_mem_obj *heap;

	/* Firmware channels */
	int num_channels;
	struct fw_channel **channels;
	struct fw_channel *channel_terminal;
	struct fw_channel *channel_io;
	struct fw_channel *channel_debug;
	struct fw_channel *channel_buf_h2t;
	struct fw_channel *channel_buf_t2h;
	struct fw_channel *channel_shared_malloc;
	struct fw_channel *channel_io_t2h;

};

#endif
