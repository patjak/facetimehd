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

#include <linux/delay.h>
#include "bcwc_drv.h"
#include "bcwc_hw.h"
#include "bcwc_ddr.h"
#include "isp.h"

/* FIXME: Double check these */
static u32 ddr_phy_reg_map[] = {
	0x0000, 0x0004, 0x0010, 0x0014, 0x0018, 0x001c, 0x0020, 0x0030,
	0x0034, 0x0038, 0x003c, 0x0040, 0x0044, 0x0048, 0x004c, 0x0050,
	0x0054, 0x0058, 0x005c, 0x0060, 0x0064, 0x0068, 0x006c, 0x0070,
	0x0074, 0x0078, 0x007c, 0x0080, 0x0084, 0x0090, 0x0094, 0x0098,
	0x009c, 0x00a0, 0x00a4, 0x00b0, 0x00b4, 0x00b8, 0x00bc, 0x00c0,
	0x0200, 0x0204, 0x0208, 0x020c, 0x0210, 0x0214, 0x0218, 0x021c,
	0x0220, 0x0224, 0x0228, 0x022c, 0x0230, 0x0234, 0x0238, 0x023c,
	0x0240, 0x0244, 0x0248, 0x024c, 0x0250, 0x0254, 0x0258, 0x025c,
	0x0260, 0x0264, 0x0268, 0x026c, 0x0270, 0x0274, 0x02a4, 0x02a8,
	0x02ac, 0x02b0, 0x02b4, 0x02b8, 0x02bc, 0x02c0, 0x02c4, 0x02c8,
	0x02cc, 0x02d0, 0x02d4, 0x02d8, 0x02dc, 0x02e0, 0x02e4, 0x02e8,
	0x02ec, 0x02f0, 0x02f4, 0x02f8, 0x02fc, 0x0300, 0x0304, 0x0308,
	0x030c, 0x0310, 0x0314, 0x0328, 0x032c, 0x0330, 0x0334, 0x0338,
	0x033c, 0x0348, 0x034c, 0x0350, 0x0354, 0x0358, 0x035c, 0x0360,
	0x0364, 0x0370, 0x0374, 0x0378, 0x037c, 0x0380, 0x0384, 0x0388,
	0x038c, 0x0390, 0x0394, 0x03a0, 0x03a4, 0x03a8, 0x03ac,
};

static int bcwc_hw_set_core_clk(struct bcwc_private *dev_priv)
{
	return 0;
}

static int bcwc_hw_s2_pll_reset(struct bcwc_private *dev_priv)
{
	BCWC_S2_REG_WRITE(0x40, S2_PLL_CTRL_2C);
	BCWC_S2_REG_WRITE(0x0, S2_PLL_CTRL_2C);
	BCWC_S2_REG_WRITE(0xbcbc1500, S2_PLL_CTRL_100);
	BCWC_S2_REG_WRITE(0x0, S2_PLL_CTRL_14);

	udelay(10000);

	BCWC_S2_REG_WRITE(0x3, S2_PLL_CTRL_14);

	dev_info(&dev_priv->pdev->dev, "PLL reset finished\n");

	return 0;
}

static int bcwc_hw_s2_init_pcie_link(struct bcwc_private *dev_priv)
{
	u32 reg;

	reg = BCWC_S2_REG_READ(S2_PCIE_LINK_D000);
	BCWC_S2_REG_WRITE(reg | 0x10, S2_PCIE_LINK_D000);

	BCWC_S2_REG_WRITE(0x1804, S2_PCIE_LINK_D120);
	BCWC_S2_REG_WRITE(0xac5800, S2_PCIE_LINK_D124);
	BCWC_S2_REG_WRITE(0x1804, S2_PCIE_LINK_D120);

	/* Check if PLL is powered down when S2 PCIe link is in L1 state */
	reg = BCWC_S2_REG_READ(S2_PCIE_LINK_D124);
	if (reg != 0xac5800) {
		dev_err(&dev_priv->pdev->dev,
			"Failed to init S2 PCIe link: %u\n", reg);
		return -EIO;
	}

	/* PLL is powered down */
	dev_info(&dev_priv->pdev->dev, "S2 PCIe link init succeeded\n");

	BCWC_S2_REG_WRITE(0x1f08, S2_PCIE_LINK_D128);
	BCWC_S2_REG_WRITE(0x80008610, S2_PCIE_LINK_D12C);
	BCWC_S2_REG_WRITE(0x1608, S2_PCIE_LINK_D128);
	BCWC_S2_REG_WRITE(0x8000fc00, S2_PCIE_LINK_D12C);
	BCWC_S2_REG_WRITE(0x1f08, S2_PCIE_LINK_D128);
	BCWC_S2_REG_WRITE(0x80008610, S2_PCIE_LINK_D12C);
	BCWC_S2_REG_WRITE(0x1708, S2_PCIE_LINK_D128);
	BCWC_S2_REG_WRITE(0x800005bf, S2_PCIE_LINK_D12C);

	return 0;
}

static int bcwc_hw_s2_pll_init(struct bcwc_private *dev_priv, u32 ddr_speed)
{
	u32 ref_clk_25;
	u32 reg;
	int retries = 0;

	reg = BCWC_S2_REG_READ(S2_PLL_REFCLK);
	ref_clk_25 = reg & S2_PLL_REFCLK_25MHZ ? 1 : 0;

	if (ref_clk_25)
		dev_info(&dev_priv->pdev->dev, "Refclk: 25MHz (0x%x)\n", reg);
	else
		dev_info(&dev_priv->pdev->dev, "Refclk: 24MHz (0x%x\n", reg);

	if (ddr_speed == 400) {
		if (ref_clk_25) {
			/* Ref clk 25 */
			BCWC_S2_REG_WRITE(0x00400078, S2_PLL_CTRL_510);
			BCWC_S2_REG_WRITE(0x19280804, S2_PLL_CTRL_24);
		} else {
			/* Ref clk 24 */
			BCWC_S2_REG_WRITE(0x03200000, S2_PLL_CTRL_20);
			BCWC_S2_REG_WRITE(0x14280603, S2_PLL_CTRL_24);
		}
	} else if (ddr_speed == 300) {
		if (ref_clk_25) {
			/* Ref clk 25 */
			BCWC_S2_REG_WRITE(0x00480078, S2_PLL_CTRL_510);
			BCWC_S2_REG_WRITE(0x19280c06, S2_PLL_CTRL_24);
		} else {
			/* Ref clk 24 */
			BCWC_S2_REG_WRITE(0x03200000, S2_PLL_CTRL_20);
			BCWC_S2_REG_WRITE(0x14280804, S2_PLL_CTRL_24);
		}
	} else if (ddr_speed == 200) {
		if (ref_clk_25) {
			/* Ref clk 25 */
			BCWC_S2_REG_WRITE(0x00400078, S2_PLL_CTRL_510);
			BCWC_S2_REG_WRITE(0x19281008, S2_PLL_CTRL_24);
		} else {
			/* Ref clk 24 */
			BCWC_S2_REG_WRITE(0x03200000, S2_PLL_CTRL_20);
			BCWC_S2_REG_WRITE(0x14280c06, S2_PLL_CTRL_24);
		}
	} else {
		if (ddr_speed != 450) {
			dev_err(&dev_priv->pdev->dev,
				"Unsupported DDR speed %uMHz, using 450MHz\n",
				ddr_speed);
			ddr_speed = 450;
		}

		if (ref_clk_25) {
			/* Ref clk 25 */
			BCWC_S2_REG_WRITE(0x0048007d, S2_PLL_CTRL_510);
			BCWC_S2_REG_WRITE(0x19280904, S2_PLL_CTRL_24);
		} else {
			/* Ref clk 24 */
			BCWC_S2_REG_WRITE(0x04b00000, S2_PLL_CTRL_20);
			BCWC_S2_REG_WRITE(0x14280904, S2_PLL_CTRL_24);

		}
	}

	bcwc_hw_s2_pll_reset(dev_priv);

	dev_info(&dev_priv->pdev->dev, "Waiting for S2 PLL to lock at %d MHz\n",
		 ddr_speed);

	do {
		reg = BCWC_S2_REG_READ(S2_PLL_CMU_STATUS);
		udelay(10);
		retries++;
	} while (((reg & 0xff00) & S2_PLL_CMU_STATUS_LOCKED) && retries <= 10000);

	if (retries > 10000) {
		dev_info(&dev_priv->pdev->dev, "Failed to lock S2 PLL: 0x%x\n",
			 reg);
		return -EINVAL;
	} else {
		dev_info(&dev_priv->pdev->dev, "S2 PLL is locked after %d us\n",
			 (retries * 10));
	}

	reg = BCWC_S2_REG_READ(S2_PLL_STATUS_A8);
	BCWC_S2_REG_WRITE(reg | S2_PLL_BYPASS, S2_PLL_STATUS_A8);
	udelay(10000);

	reg = BCWC_S2_REG_READ(S2_PLL_STATUS_A8);
	if (reg & S2_PLL_BYPASS)
		dev_info(&dev_priv->pdev->dev, "S2 PLL is in bypass mode\n");
	else
		dev_info(&dev_priv->pdev->dev, "S2 PLL is in non-bypass mode\n");

	return 0;
}

static int bcwc_hw_s2_preinit_ddr_controller_soc(struct bcwc_private *dev_priv)
{
	/* Wingardium leviosa */
	BCWC_S2_REG_WRITE(0x203, S2_DDR_REG_1100);
	BCWC_S2_REG_WRITE(0x203, S2_DDR_REG_1104);
	BCWC_S2_REG_WRITE(0x203, S2_DDR_REG_1108);
	BCWC_S2_REG_WRITE(0x203, S2_DDR_REG_110C);
	BCWC_S2_REG_WRITE(0x203, S2_DDR_REG_1110);
	BCWC_S2_REG_WRITE(0x203, S2_DDR_REG_1114);
	BCWC_S2_REG_WRITE(0x203, S2_DDR_REG_1118);
	BCWC_S2_REG_WRITE(0x203, S2_DDR_REG_111C);

	return 0;
}

static int bcwc_hw_ddr_phy_soft_reset(struct bcwc_private *dev_priv)
{
	/* Clear status bits? */
	BCWC_S2_REG_WRITE(0x281, S2_PLL_STATUS_A8);

	BCWC_S2_REG_WRITE(0xfffff, S2_PLL_CTRL_9C);

	udelay(10000);

	BCWC_S2_REG_WRITE(0xffbff, S2_PLL_CTRL_9C);

	return 0;
}

static inline int bcwc_hw_ddr_status_busy(struct bcwc_private *dev_priv,
					  int retries, int delay)
{
	int reg, i;

	for (i = 0; i < retries; i++) {
		reg = BCWC_S2_REG_READ(S2_DDR_STATUS_2018);
		if (!(reg & S2_DDR_STATUS_BUSY))
			break;

		if (delay > 0)
			udelay(delay);
	}

	if (i >= retries) {
		dev_err(&dev_priv->pdev->dev,
			"S2_DDR_STATUS_2018 busy: retries=%d, udelay=%d, reg=0x%08x\n",
			retries, delay, reg);
		return -EBUSY;
	}

	return 0;
}

static int bcwc_hw_ddr_rewrite_mode_regs(struct bcwc_private *dev_priv)
{
	int ret, val;

	BCWC_S2_REG_WRITE(0x02000802, S2_DDR_2014);

	ret = bcwc_hw_ddr_status_busy(dev_priv, 500, 5);
	if (ret != 0)
		return ret;

	BCWC_S2_REG_WRITE(0x3, S2_DDR_2014);

	ret = bcwc_hw_ddr_status_busy(dev_priv, 500, 5);
	if (ret != 0)
		return ret;

	BCWC_S2_REG_WRITE(0x1, S2_DDR_2014);

	ret = bcwc_hw_ddr_status_busy(dev_priv, 500, 5);
	if (ret != 0)
		return ret;

	if (dev_priv->ddr_speed == 450)
		val = 0x16003000;
	else
		val = 0x16002000;

	BCWC_S2_REG_WRITE(val, S2_DDR_2014);

	ret = bcwc_hw_ddr_status_busy(dev_priv, 500, 5);
	if (ret != 0)
		return ret;

	dev_info(&dev_priv->pdev->dev,
		 "Rewrite DDR mode registers succeeded\n");

	return 0;
}

static int bcwc_hw_s2_init_ddr_controller_soc(struct bcwc_private *dev_priv)
{
	u32 cmd;
	u32 val;
	u32 reg;
	u32 step_size, vdl_fine, vdl_coarse;
	u32 vtt_cons, vtt_ovr;
	int ret, i;

	/* Read PCI config command register */
	ret = pci_read_config_dword(dev_priv->pdev, 4, &cmd);
	if (ret) {
		dev_err(&dev_priv->pdev->dev, "Failed to read PCI config\n");
		return -EIO;
	}

	if ((cmd & 0x07) == 0) {
		dev_err(&dev_priv->pdev->dev,
			"PCI link in illegal state, cfg_cmd_reg: 0x%x\n", cmd);
		return -EIO;
	}

	reg = BCWC_S2_REG_READ(S2_PLL_CTRL_9C);
	BCWC_S2_REG_WRITE(reg & 0xfffffcff, S2_PLL_CTRL_9C);
	BCWC_S2_REG_WRITE(reg | 0x300, S2_PLL_CTRL_9C);

	bcwc_hw_s2_pll_init(dev_priv, dev_priv->ddr_speed);

	bcwc_hw_ddr_phy_soft_reset(dev_priv);

	BCWC_S2_REG_WRITE(0x2, S2_DDR40_WL_DRV_PAD_CTL);
	BCWC_S2_REG_WRITE(0x2, S2_DDR40_WL_CLK_PAD_DISABLE);

	/* Disable the hardware frequency change function */
	BCWC_S2_REG_WRITE(0x3f4, S2_20F8);

	/* Setup the PLL */
	BCWC_S2_REG_WRITE(0x40, S2_2434);
	BCWC_S2_REG_WRITE(0x10000000, S2_2438);
	BCWC_S2_REG_WRITE(0x4, S2_2424);
	BCWC_S2_REG_WRITE(0x1f37291, S2_2430);

	/* Wait for DDR PLL to lock */
	for (i = 0; i <= 10000; i++) {
		reg = BCWC_S2_REG_READ(S2_DDR_PLL_STATUS_2444);
		if (reg & S2_DDR_PLL_STATUS_2444_LOCKED)
			break;
		udelay(10);
	}

	if (i > 10000) {
		dev_err(&dev_priv->pdev->dev,
			"Failed to lock DDR PHY PLL in stage 1\n");
		return -EIO;
	}

	BCWC_S2_REG_WRITE(0x1f37205, S2_2430);

	for (i = 0; i <= 10000; i++) {
		reg = BCWC_S2_REG_READ(S2_DDR_PLL_STATUS_241C);
		if (reg & S2_DDR_PLL_STATUS_241C_LOCKED)
			break;
		udelay(10);
	}

	if (i > 10000) {
		dev_err(&dev_priv->pdev->dev,
			"Failed to lock DDR PHY PLL in stage 2\n");
		return -EIO;
	}

	udelay(10000);

	/* WL */
	BCWC_S2_REG_WRITE(0x0c10, S2_DDR40_PHY_PLL_DIV);
	BCWC_S2_REG_WRITE(0x0010, S2_DDR40_PHY_PLL_CFG);

	for (i = 0; i <= 10000; i++) {
		reg = BCWC_S2_REG_READ(S2_DDR40_PHY_PLL_STATUS);
		if (reg & S2_DDR40_PHY_PLL_STATUS_LOCKED)
			break;
		udelay(10);
	}

	if (i > 10000) {
		dev_err(&dev_priv->pdev->dev,
			"Failed to lock DDR PHY PLL in stage 3\n");
		return -EIO;
	}

	dev_info(&dev_priv->pdev->dev,
		 "DDR40 PHY PLL locked on safe settings\n");

	/* Default is DDR model 4 */
	switch (dev_priv->ddr_model) {
	case 4:
		val = 0x46a00c2;
		break;
	case 2:
		val = 0x42500c2;
		break;
	default:
		val = 0;
	}

	BCWC_S2_REG_WRITE(0x10737545, S2_DDR_20A0);
	BCWC_S2_REG_WRITE(0x12643173, S2_DDR_20A4);
	BCWC_S2_REG_WRITE(0xff3f, S2_DDR_20A8);
	BCWC_S2_REG_WRITE(val, S2_DDR_20B0);
	BCWC_S2_REG_WRITE(0x101f, S2_DDR_2118);
	BCWC_S2_REG_WRITE(0x1c0, S2_DDR40_PHY_AUX_CTL);

	switch (dev_priv->ddr_model) {
	case 4:
		val = 0x2159518;
		break;
	case 2:
		val = 0x2155558;
		break;
	}

	BCWC_S2_REG_WRITE(val, S2_DDR40_STRAP_CTL);

	if (dev_priv->ddr_speed == 450)
		val = 0x108307;
	else
		val = 0x108286;

	BCWC_S2_REG_WRITE(val, S2_DDR40_STRAP_CTL_2);

	/* Strap control */
	BCWC_S2_REG_WRITE(0x2159559, S2_DDR40_STRAP_CTL);

	/* Polling for STRAP valid */
	for (i = 0; i < 10000; i++) {
		reg = BCWC_S2_REG_READ(S2_DDR40_STRAP_STATUS);
		if (reg & 0x1)
			break;
		udelay(10);
	}

	if (i >= 10000) {
		dev_err(&dev_priv->pdev->dev,
			"Timeout waiting for STRAP valid\n");
		return -ENODEV;
	} else {
		dev_info(&dev_priv->pdev->dev, "STRAP valid\n");
	}

	/* Manual DDR40 PHY init */
	if (dev_priv->ddr_speed != 450) {
		dev_warn(&dev_priv->pdev->dev,
			 "DDR frequency is %u (should be 450 MHz)",
			 dev_priv->ddr_speed);
	}

	dev_info(&dev_priv->pdev->dev,
		 "Configuring DDR PLLs for %u MHz\n", dev_priv->ddr_speed);

	if ((dev_priv->ddr_speed * 2) < 500)
		val = 0x2040;
	else
		val = 0x810;

	/* Start programming the DDR PLL */

	reg = BCWC_S2_REG_READ(S2_DDR40_PHY_PLL_DIV);
	reg &= 0xffffc700;
	val |= reg;

	BCWC_S2_REG_WRITE(val, S2_DDR40_PHY_PLL_DIV);

	reg = BCWC_S2_REG_READ(S2_DDR40_PHY_PLL_CFG);
	reg &= 0xfffffffd;
	BCWC_S2_REG_WRITE(reg, S2_DDR40_PHY_PLL_CFG);

	/* Start polling for the lock */
	for (i = 0; i < 100; i++) {
		reg = BCWC_S2_REG_READ(S2_DDR40_PHY_PLL_STATUS);
		if (reg & S2_DDR40_PHY_PLL_STATUS_LOCKED)
			break;
		udelay(1);
	}

	if (i >= 100) {
		dev_err(&dev_priv->pdev->dev, "Failed to lock the DDR PLL\n");
		return -ENODEV;
	}

	dev_info(&dev_priv->pdev->dev, "DDR40 PLL is locked after %d us\n", i);

	/* Configure DDR40 VDL */
	BCWC_S2_REG_WRITE(0, S2_DDR40_PHY_VDL_CTL);
	BCWC_S2_REG_WRITE(0x103, S2_DDR40_PHY_VDL_CTL);

	/* Poll for VDL calibration */
	for (i = 0; i < 100; i++) {
		reg = BCWC_S2_REG_READ(S2_DDR40_PHY_VDL_STATUS);
		if (reg & 0x1)
			break;
		udelay(1);
	}

	if (reg & 0x1) {
		dev_info(&dev_priv->pdev->dev,
			 "First DDR40 VDL calibration completed after %d us",
			 i);

		if ((reg & 0x2) == 0) {
			dev_info(&dev_priv->pdev->dev,
				 "...but failed to lock\n");
		}

	} else {
		dev_err(&dev_priv->pdev->dev,
			"First DDR40 VDL calibration failed\n");
	}

	BCWC_S2_REG_WRITE(0, S2_DDR40_PHY_VDL_CTL);
	BCWC_S2_REG_WRITE(0, S2_DDR40_PHY_VDL_CTL); /* Needed? */
	BCWC_S2_REG_WRITE(0x200, S2_DDR40_PHY_VDL_CTL); /* calib steps */

	for (i = 0; i < 1000; i++) {
		reg = BCWC_S2_REG_READ(S2_DDR40_PHY_VDL_STATUS);
		if (reg & 0x1)
			break;
		udelay(1);
	}

	dev_info(&dev_priv->pdev->dev,
		 "Second DDR40 VDL calibration completed after %d us\n", i);

	if (reg & 0x2) {
		step_size = (reg & S2_DDR40_PHY_VDL_STEP_MASK) >>
			    S2_DDR40_PHY_VDL_STEP_SHIFT;
		dev_info(&dev_priv->pdev->dev, "Using step size %u\n",
			 step_size);
	} else {

		val = 1000000 / dev_priv->ddr_speed;
		step_size = (val * 0x4ec4ec4f) >> 22;
		dev_info(&dev_priv->pdev->dev, "Using default step size (%u)\n",
			 step_size);
	}

	dev_priv->vdl_step_size = step_size;

	vdl_fine = BCWC_S2_REG_READ(S2_DDR40_PHY_VDL_CHAN_STATUS);

	/* lock = 1 and byte_sel = 1 */
	if ((vdl_fine & 2) == 0) {
		vdl_fine = (vdl_fine >> 8) & 0x3f;
		vdl_fine |= 0x10100;

		BCWC_S2_REG_WRITE(vdl_fine, S2_DDR40_PHY_VDL_OVR_FINE);

		vdl_coarse = 0x10000;

		step_size >>= 4;
		step_size += step_size * 2;

		if (step_size > 10) {
			step_size = (step_size + 118) >> 1;
			step_size &= 0x3f;
			step_size |= 0x10000;
			vdl_coarse = step_size;
		}

		BCWC_S2_REG_WRITE(vdl_coarse, S2_DDR40_PHY_VDL_OVR_COARSE);

		dev_info(&dev_priv->pdev->dev,
			 "VDL set to: coarse=0x%x, fine=0x%x\n",
			 vdl_coarse, vdl_fine);
	}

	/* Configure Virtual VTT connections and override */

	vtt_cons = 0x1cf7fff;
	BCWC_S2_REG_WRITE(vtt_cons, S2_DDR40_PHY_VTT_CONNECTIONS);

	vtt_ovr = 0x77fff;
	BCWC_S2_REG_WRITE(vtt_ovr, S2_DDR40_PHY_VTT_OVERRIDE);

	BCWC_S2_REG_WRITE(0x4, S2_DDR40_PHY_VTT_CTL);

	dev_info(&dev_priv->pdev->dev, "Virtual VTT enabled");

	/* Process, Voltage and Temperature compensation */
	BCWC_S2_REG_WRITE(0xc0fff, S2_DDR40_PHY_ZQ_PVT_COMP_CTL);
	BCWC_S2_REG_WRITE(0x2, S2_DDR40_PHY_DRV_PAD_CTL);
	BCWC_S2_REG_WRITE(0x2, S2_DDR40_WL_DRV_PAD_CTL);

	val = 1000000 / dev_priv->ddr_speed;
	reg = 4;

	if (val >= 400) {
		if (val > 900)
			reg = 1;

		reg += 5;
	}

	/* DDR read FIFO delay? */
	BCWC_S2_REG_WRITE(reg, S2_DDR40_WL_RD_DATA_DLY);
	BCWC_S2_REG_WRITE(0x2, S2_DDR40_WL_READ_CTL); /* le_adj, te_adj */
	BCWC_S2_REG_WRITE(0x3, S2_DDR40_WL_WR_PREAMBLE_MODE); /* mode, long */

	/* dq_oeb, dq_reb, dq_iddq, dq_rxenb */
	reg = BCWC_S2_REG_READ(S2_DDR40_WL_IDLE_PAD_CTL);
	BCWC_S2_REG_WRITE(reg & 0xff0fffff, S2_DDR40_WL_IDLE_PAD_CTL);
	udelay(500);

	BCWC_S2_REG_WRITE(0, S2_DDR_2004);
	udelay(10000);

	BCWC_S2_REG_WRITE(0xab0a, S2_DDR_2014);

	/* Polling for BUSY */
	ret = bcwc_hw_ddr_status_busy(dev_priv, 10000, 10);
	if (ret != 0)
		return -EBUSY;

	udelay(10000);

	BCWC_S2_REG_WRITE(0, S2_3204);

	/* Read DRAM mem address (FIXME: Need to mask a few bits here) */
	reg = BCWC_S2_REG_READ(S2_DDR40_STRAP_STATUS);
	dev_info(&dev_priv->pdev->dev,
		 "S2 DRAM memory address: 0x%08x\n", reg);

	switch (dev_priv->ddr_model) {
	case 4:
		val = 0x1fffffff;
		break;
	case 2:
		val = 0x0fffffff;
		break;
	default:
		/* Probably just invalid model */
		val = dev_priv->ddr_model;
	}

	BCWC_S2_REG_WRITE(val, S2_3208);
	BCWC_S2_REG_WRITE(0x1040, S2_3200);

	bcwc_hw_ddr_rewrite_mode_regs(dev_priv);

	BCWC_S2_REG_WRITE(0x20000, S2_DDR_2014);
	BCWC_S2_REG_WRITE(1, S2_DDR_2008);

	return 0;
}

static int bcwc_hw_ddr_phy_save_regs(struct bcwc_private *dev_priv)
{
	u32 reg, offset;
	int i;

	if (dev_priv->ddr_phy_num_regs == 0)
		return -ENOENT;

	for (i = 0; i < dev_priv->ddr_phy_num_regs; i++) {
		offset = dev_priv->ddr_phy_reg_map[i].offset;
		reg = BCWC_ISP_REG_READ(offset + DDR_PHY_REG_BASE);
		dev_priv->ddr_phy_reg_map[i].value = reg;
	}

	return 0;
}

static int bcwc_hw_irq_enable(struct bcwc_private *dev_priv)
{
	return 0;
}

static int bcwc_hw_irq_disable(struct bcwc_private *dev_priv)
{
	return 0;
}

int bcwc_hw_init(struct bcwc_private *dev_priv)
{
	u32 val;
	int ret, i;

	ret = bcwc_hw_s2_init_pcie_link(dev_priv);
	if (ret)
		goto out;

	bcwc_hw_s2_preinit_ddr_controller_soc(dev_priv);
	bcwc_hw_s2_init_ddr_controller_soc(dev_priv);

	/* Initialize the reg map */
	for (i = 0; i < DDR_PHY_NUM_REGS; i++)
		dev_priv->ddr_phy_reg_map[i].offset = ddr_phy_reg_map[i];

/*
	dev_info(&dev_priv->pdev->dev,
		 "Dumping DDR PHY reg map before shmoo\n");

	for (i = 0; i < DDR_PHY_NUM_REGS; i++) {
		if (!(i % 3) && i >  0)
			printk("\n");

		val = BCWC_S2_REG_READ(ddr_phy_reg_map[i]);
		printk(KERN_CONT "0x%.3x = 0x%.8x\t",
			 ddr_phy_reg_map[i], val);
	}
*/

	ret = bcwc_ddr_verify_mem(dev_priv, 0, MEM_VERIFY_NUM_FULL);
	if (ret) {
		dev_err(&dev_priv->pdev->dev,
			"Full memory verification failed! (%d)\n", ret);
		/*
		 * Here we should do a shmoo calibration but it's not yet
		 * fully implemented.
		 */

		/* bcwc_ddr_calibrate(dev_priv); */
	} else {
		dev_info(&dev_priv->pdev->dev,
			 "Full memory verification succeeded! (%d)\n", ret);
	}

	/* Save our working configuration */
	bcwc_hw_ddr_phy_save_regs(dev_priv);

	BCWC_S2_REG_WRITE(0x8, S2_D108);
	BCWC_S2_REG_WRITE(0xc, S2_D104);

	BCWC_ISP_REG_WRITE(0, ISP_REG_40004);

	isp_init(dev_priv);

out:
	return ret;
}
