/*
 * Broadcom PCIe 1570 webcam driver
 * Some of the register defines are taken from the crystalhd driver
 *
 * Copyright (C) 2014 Patrik Jakobsson (patrik.r.jakobsson@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 */

#ifndef _BCWC_REG_H
#define _BCWC_REG_H

/* S2 IO reg */

#define S2_PCIE_LINK_D000	0xd000
#define S2_PCIE_LINK_D120	0xd120
#define S2_PCIE_LINK_D124	0xd124
#define S2_PCIE_LINK_D128	0xd128
#define S2_PCIE_LINK_D12C	0xd12c

/* These are written to 0x203 before DDR soc init */
#define S2_DDR_REG_1100		0x1100
#define S2_DDR_REG_1104		0x1104
#define S2_DDR_REG_1108		0x1108
#define S2_DDR_REG_110C		0x110c
#define S2_DDR_REG_1110		0x1110
#define S2_DDR_REG_1114		0x1114
#define S2_DDR_REG_1118		0x1118
#define S2_DDR_REG_111C		0x111c

#define DDR_PHY_REG_BASE	0x2800
#define DDR_PHY_NUM_REGS	127 /* Found in AppleCamIn::Start() */

#define S2_PLL_STATUS_04	0x04
#define S2_PLL_REFCLK		(1 << 3) /* 1 = 25MHz, 0 = 24MHz */

#define S2_PLL_STATUS_0C	0x0c	/* Register is called CMU_R_PLL_STS_MEMADDR */
#define S2_PLL_STATUS_LOCKED	(1 << 15) /* 1 = PLL locked, 0 = PLL not locked */

#define S2_PLL_STATUS_A8	0xa8
#define S2_PLL_BYPASS		(1 << 0) /* 1 = bypass, 0 = non-bypass */

#define S2_PLL_CTRL_14		0x0014
#define S2_PLL_CTRL_20		0x0020
#define S2_PLL_CTRL_24		0x0024
#define S2_PLL_CTRL_2C		0x002c
#define S2_PLL_CTRL_9C		0x009c
#define S2_PLL_CTRL_100		0x0100
#define S2_PLL_CTRL_510		0x0510

/* On iomem with pointer at 0x0ff0 (Bar 4: 1MB) */
#define IRQ_IPC_NUM_CHAN	0xc3000
#define IRQ_IPC_QUEUE_SIZE	0xc3004
#define IRQ_REG_08		0xc3008
#define IRQ_FW_HEAP_SIZE	0xc300c
#define IRQ_REG_10		0xc3010
#define IRQ_REG_14		0xc3014
#define IRQ_REG_18		0xc3018
#define IRQ_REG_1C		0xc301c
#define IRQ_REG_40004		0x40004
#define IRQ_REG_40008		0x40008
#define IRQ_REG_41000		0x41000
#define IRQ_REG_41024		0x41024

#define IRQ_REG_RANGE_START	0x0128
#define IRQ_REG_RANGE_END	0x0220

#endif
