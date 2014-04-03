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

#include <linux/delay.h>
#include "bcwc_drv.h"
#include "bcwc_hw.h"

/* Used after most PCI Link IO writes */
inline void bcwc_hw_pci_post(struct bcwc_private *dev_priv)
{
	pci_write_config_dword(dev_priv->pdev, 0, 0x12345678);
}

static int bcwc_hw_set_core_clk(struct bcwc_private *dev_priv)
{
	return 0;
}

static int bcwc_hw_s2_pll_reset(struct bcwc_private *dev_priv)
{
	BCWC_DEV_REG_WRITE(0x40, DDR_PHY_2C);
	bcwc_hw_pci_post(dev_priv);

	return 0;
}

static int bcwc_hw_s2_init_pcie_link(struct bcwc_private *dev_priv)
{
	u32 reg;

	BCWC_DEV_REG_WRITE(S2_PCIE_LINK_D000, 0x10);
	bcwc_hw_pci_post(dev_priv);

	BCWC_DEV_REG_WRITE(S2_PCIE_LINK_D120, 0x1804);
	bcwc_hw_pci_post(dev_priv);

	BCWC_DEV_REG_WRITE(S2_PCIE_LINK_D124, 0xac5800);
	bcwc_hw_pci_post(dev_priv);

	BCWC_DEV_REG_WRITE(S2_PCIE_LINK_D120, 0x1804);
	bcwc_hw_pci_post(dev_priv);

	reg = BCWC_DEV_REG_READ(S2_PCIE_LINK_D124);
	if (reg == 0xac5800) {
	}

	return 0;
}

static int bcwc_hw_s2_pll_init(struct bcwc_private *dev_priv, u32 ddr_speed)
{
	u32 ref_clk_25;
	u32 reg;
	int retries = 0;

	ref_clk_25 = (BCWC_DEV_REG_READ(S2_PLL_STATUS_04) && 0x8);

	if (ref_clk_25)
		dev_info(&dev_priv->pdev->dev, "Refclk: 25MHz\n");
	else
		dev_info(&dev_priv->pdev->dev, "Refclk: 24MHz\n");

	if (ddr_speed == 400) {
		if (ref_clk_25) {
			/* Ref clk 25 */
			BCWC_DEV_REG_WRITE(0x00400078, S2_PLL_CTRL_510);
			bcwc_hw_pci_post(dev_priv);
			BCWC_DEV_REG_WRITE(0x19280804, S2_PLL_CTRL_24);
		} else {
			/* Ref clk 24 */
			BCWC_DEV_REG_WRITE(0x03200000, S2_PLL_CTRL_20);
			bcwc_hw_pci_post(dev_priv);
			BCWC_DEV_REG_WRITE(0x14280603, S2_PLL_CTRL_24);
		}
	} else if (ddr_speed == 300) {
		if (ref_clk_25) {
			/* Ref clk 25 */
			BCWC_DEV_REG_WRITE(0x03200000, S2_PLL_CTRL_20);
			bcwc_hw_pci_post(dev_priv);
			BCWC_DEV_REG_WRITE(0x14280804, S2_PLL_CTRL_24);
		} else {
			/* Ref clk 24 */
			BCWC_DEV_REG_WRITE(0x00480078, S2_PLL_CTRL_510);
			bcwc_hw_pci_post(dev_priv);
			BCWC_DEV_REG_WRITE(0x19280c06, S2_PLL_CTRL_24);
		}
	} else if (ddr_speed == 200) {
		if (ref_clk_25) {
			/* Ref clk 25 */
			BCWC_DEV_REG_WRITE(0x03200000, S2_PLL_CTRL_20);
			bcwc_hw_pci_post(dev_priv);
			BCWC_DEV_REG_WRITE(0x14280c06, S2_PLL_CTRL_24);
		} else {
			/* Ref clk 24 */
			BCWC_DEV_REG_WRITE(0x00400078, S2_PLL_CTRL_510);
			bcwc_hw_pci_post(dev_priv);
			BCWC_DEV_REG_WRITE(0x19281008, S2_PLL_CTRL_24);
		}
	} else {
		if (ddr_speed != 450) {
			dev_err(&dev_priv->pdev->dev,
				"Unsupported DDR speed %uMHz, using 450MHz\n",
				ddr_speed);
		}
		ddr_speed = 450;

		if (ref_clk_25) {
			/* Ref clk 25 */
			BCWC_DEV_REG_WRITE(0x04b00000, S2_PLL_CTRL_20);
			bcwc_hw_pci_post(dev_priv);
			BCWC_DEV_REG_WRITE(0x14280904, S2_PLL_CTRL_24);
		} else {
			/* Ref clk 24 */
			BCWC_DEV_REG_WRITE(0x0048007d, S2_PLL_CTRL_510);
			bcwc_hw_pci_post(dev_priv);
			BCWC_DEV_REG_WRITE(0x19280904, S2_PLL_CTRL_24);
		}
	}

	bcwc_hw_pci_post(dev_priv);
	bcwc_hw_s2_pll_reset(dev_priv);

	dev_info(&dev_priv->pdev->dev, "Waiting for PLL to lock\n");

	do {
		reg = BCWC_DEV_REG_READ(S2_PLL_STATUS_0C) & 0x80;
		udelay(10);
		retries++;
	} while (!reg && retries <= 10000);

	if (retries > 10000) {
		dev_info(&dev_priv->pdev->dev, "Failed to lock PLL\n");
		return -EINVAL;
	} else {
		dev_info(&dev_priv->pdev->dev, "PLL is locked\n");
	}

	reg = BCWC_DEV_REG_READ(S2_PLL_STATUS_A8);
	BCWC_DEV_REG_WRITE(reg | 0x1, S2_PLL_STATUS_A8);
	bcwc_hw_pci_post(dev_priv);
	udelay(10000);

	reg = BCWC_DEV_REG_READ(S2_PLL_STATUS_A8);
	if (reg & 0x1) {
		dev_info(&dev_priv->pdev->dev, "PLL is in bypass mode\n");
	} else {
		dev_info(&dev_priv->pdev->dev, "PLL is in non-bypass mode\n");
	}

	return 0;
}

static int bcwc_hw_s2_init_ddr_controller_soc(struct bcwc_private *dev_priv)
{
	u32 cmd;
	u32 reg;
	int ret;

	/* Read PCI config command register */
	ret = pci_read_config_dword(dev_priv->pdev, 4, &cmd);
	if (!ret) {
		dev_err(&dev_priv->pdev->dev, "Failed to read PCI config\n");
		return -EIO;
	}

	if ((cmd & 0x07) == 0) {
		dev_err(&dev_priv->pdev->dev,
			"PCI link in illegal state, cfg_cmd_reg: 0x%x\n", cmd);
		return -EIO;
	}

	reg = BCWC_DEV_REG_READ(DDR_PHY_9C);
	reg &= 0xfffffcff;

	BCWC_DEV_REG_WRITE(reg, DDR_PHY_9C);
	bcwc_hw_pci_post(dev_priv);

	BCWC_DEV_REG_WRITE(reg | 0x300, DDR_PHY_9C);
	bcwc_hw_pci_post(dev_priv);

	/*
	 *  FIXME: Need to find out the correct DDR speed.
	 * Just using 200 MHz for now
	 */
	bcwc_hw_s2_pll_init(dev_priv, 200);

	/* FIXME: Unfinished */

	return 0;
}

static int bcwc_hw_save_ddr_phy_regs(struct bcwc_private *dev_priv)
{
	u32 reg, offset;
	int i;

	if (dev_priv->ddr_phy_num_regs == 0)
		return -ENOENT;

	for (i = 0; i < dev_priv->ddr_phy_num_regs; i++) {
		offset = dev_priv->ddr_reg_map[i].offset;
		reg = BCWC_DEV_REG_READ(offset + DDR_PHY_REG_BASE);
		dev_priv->ddr_reg_map[i].value = reg;
	}

	return 0;
}

static int bcwc_hw_irq_init(struct bcwc_private *dev_priv)
{
	u32 num_channels, queue_size;
	u32 reg;
	int i, retries;

	BCWC_LINK_REG_WRITE(IRQ_IPC_NUM_CHAN, 0);
	bcwc_hw_pci_post(dev_priv);

	BCWC_LINK_REG_WRITE(IRQ_IPC_QUEUE_SIZE, 0);
	bcwc_hw_pci_post(dev_priv);

	BCWC_LINK_REG_WRITE(IRQ_REG_08, 0);
	bcwc_hw_pci_post(dev_priv);

	BCWC_LINK_REG_WRITE(IRQ_REG_0C, 0);
	bcwc_hw_pci_post(dev_priv);

	BCWC_LINK_REG_WRITE(IRQ_REG_10, 0);
	bcwc_hw_pci_post(dev_priv);

	BCWC_LINK_REG_WRITE(IRQ_REG_14, 0);
	bcwc_hw_pci_post(dev_priv);

	BCWC_LINK_REG_WRITE(IRQ_REG_18, 0);
	bcwc_hw_pci_post(dev_priv);

	BCWC_LINK_REG_WRITE(IRQ_REG_1C, 0);
	bcwc_hw_pci_post(dev_priv);

	BCWC_LINK_REG_WRITE(IRQ_REG_41024, 0xffffffff);
	bcwc_hw_pci_post(dev_priv);

	/*
	 * Probably the IPC queue
	 * FIXME: Check if we can do 64bit writes on PCIe
	 */
	for (i = IRQ_REG_RANGE_START; i <= IRQ_REG_RANGE_END; i += 8) {
		BCWC_LINK_REG_WRITE(0xffffff, i);
		BCWC_LINK_REG_WRITE(0x000000, i + 4);
	}
	bcwc_hw_pci_post(dev_priv);

	BCWC_LINK_REG_WRITE(IRQ_REG_40008, 0x80000000);
	bcwc_hw_pci_post(dev_priv);

	BCWC_LINK_REG_WRITE(IRQ_REG_40004, 0x1);
	bcwc_hw_pci_post(dev_priv);


	for (retries = 0; retries < 1000; retries++) {
		reg = BCWC_LINK_REG_READ(IRQ_REG_40004);
		if ((reg & 0xff) == 0xf0)
			break;
		udelay(10);
	}

	if (retries >= 1000) {
		dev_info(&dev_priv->pdev->dev, "Init failed! No wake signal\n");
		return -EIO;
	}

	BCWC_LINK_REG_WRITE(0xffffffff, IRQ_REG_41024);

	num_channels = BCWC_LINK_REG_READ(IRQ_IPC_NUM_CHAN) + 1;
	queue_size = BCWC_LINK_REG_READ(IRQ_IPC_QUEUE_SIZE);

	dev_info(&dev_priv->pdev->dev,
		 "Number of IPC channels: %u, queue size: %u\n",
		 num_channels, queue_size);

	if (num_channels > 32) {
		dev_info(&dev_priv->pdev->dev, "Too many IPC channels: %u\n",
			 num_channels);
		return -EIO;
	}

	/*
	allocate_device_memory(queue_size, &ret, 0);
	*/
}

static int bcwc_hw_irq_enable(struct bcwc_private *dev_priv)
{
	return 0;
}

static int bcwc_hw_irq_disable(struct bcwc_private *dev_priv)
{
	return 0;
}

static int bcwc_hw_sensor_enable(struct bcwc_private *dev_priv)
{
	return 0;
}

static int bcwc_hw_sensor_disable(struct bcwc_private *dev_priv)
{
	return 0;
}

static int bcwc_hw_s2chip_enable(struct bcwc_private *dev_priv)
{
	return 0;
}

static int bcwc_hw_s2chip_disable(struct bcwc_private *dev_priv)
{
	return 0;
}

/* What I've figured out about the power on sequence so far */
static int bcwc_hw_power_on(struct bcwc_private *dev_priv)
{
	bcwc_hw_s2chip_enable(dev_priv);
	bcwc_hw_sensor_enable(dev_priv);
	bcwc_hw_irq_enable(dev_priv);

	return 0;
}

/* What I've figured out about the power off sequence so far */
static int bcwc_hw_power_off(struct bcwc_private *dev_priv)
{
	bcwc_hw_irq_disable(dev_priv);
	bcwc_hw_sensor_disable(dev_priv);
	bcwc_hw_s2chip_disable(dev_priv);

	return 0;
}

int bcwc_hw_init(struct bcwc_private *dev_priv)
{
	return 0;
}
