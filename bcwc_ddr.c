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

#include <linux/random.h>
#include "bcwc_drv.h"
#include "bcwc_hw.h"

/* Memory test pattern inspired by ramtest in CoreBoot */
static inline void bcwc_hw_mem_pattern(u32 index, u32 *addr, u32 *val)
{
	int a, b;

	a = (index >> 8) + 1;
	b = (index >> 4) & 0xf;
	*addr = index & 0xf;
	*addr |= a << (4 * b);
	*addr &= 0x0fffffff;
	*val = 0x01010101 << (a & 7);

	if (a & 8)
		*val = ~(*val);
}

int bcwc_hw_verify_mem_full(struct bcwc_private *dev_priv, u32 base)
{
	struct rnd_state state;
	u32 val, val_read, addr;
	int fails = 0;
	int num = 1024 * 128;
	int i;

	prandom_seed_state(&state, 0x12345678);

	for (i = 0; i < num; i++) {
		val = prandom_u32_state(&state);
		addr = prandom_u32_state(&state);
		addr &= 0xfffffff;

		BCWC_S2_MEM_WRITE(val, addr);
		bcwc_hw_pci_post(dev_priv);
	}

	prandom_seed_state(&state, 0x12345678);

	for (i = 0; i < num; i++) {
		val = prandom_u32_state(&state);
		addr = prandom_u32_state(&state);
		addr &= 0xfffffff;

		val_read = BCWC_S2_MEM_READ(addr);

		if (val_read != val)
			fails++;
	}

	return fails;
}

int bcwc_hw_verify_mem(struct bcwc_private *dev_priv, u32 base)
{
	u32 i, addr, val, val_read;
	int fails = 0;

	for (i = 0; i < 0x400; i += 4) {
		bcwc_hw_mem_pattern(i, &addr, &val);
		BCWC_S2_MEM_WRITE(val, base + addr);
		bcwc_hw_pci_post(dev_priv);
	}

	for (i = 0; i < 0x400; i +=4) {
		bcwc_hw_mem_pattern(i, &addr, &val);
		val_read = BCWC_S2_MEM_READ(base + addr);

		if (val_read != val)
			fails++;
	}

	if (fails > 0)
		return -EIO;

	return 0;
}


/* FIXME: Make some more sense out of this */
static int bcwc_hw_ddr_calibrate_rd_data_dly_fifo(struct bcwc_private *dev_priv)
{
	u32 base = 0x2800;
	u32 offset_1 = base + 0x200;
	u32 offset_2 = base + 0x274;
	u32 offset_3 = base + 0x314;
	u32 delay_reg = base + 0x360;
	u32 offset_5 = base + 0x390;
	u32 offset_6 = base + 0x394;
	u32 reg_saved_1, reg_saved_2, reg_saved_3;

	u32 a, b, c, d, r8, r12, r14, r15;
	u32 var_2c, var_30, fifo_delay, var_38;

	int ret;

	/* Save current register values */
	reg_saved_1 = BCWC_S2_REG_READ(offset_1);
	reg_saved_2 = BCWC_S2_REG_READ(offset_2);
	reg_saved_3 = BCWC_S2_REG_READ(offset_3);

	BCWC_S2_REG_WRITE(0x30000, offset_1);
	bcwc_hw_pci_post(dev_priv);

	BCWC_S2_REG_WRITE(0x30100, offset_2);
	bcwc_hw_pci_post(dev_priv);

	BCWC_S2_REG_WRITE(0x30100, offset_3);
	bcwc_hw_pci_post(dev_priv);

	fifo_delay = 1;
	a = 1000;
	r15 = 0;
	b = 0;
	var_30 = 0;
	var_2c = 0;

	do {
		var_38 = a;

		BCWC_S2_REG_WRITE((fifo_delay & 0x7), delay_reg);
		bcwc_hw_pci_post(dev_priv);

		/*
		 * How do we know if verification was successful?
		 * OSX doesn't check any return values from it's verification so
		 * perhaps controller can detect this itself and set some regs.
		 */
		bcwc_hw_verify_mem(dev_priv, 0);

		BCWC_S2_REG_WRITE(1, offset_6);
		bcwc_hw_pci_post(dev_priv);

		r8 = (b >= 57) ? b : (b + 7);

		a = r15 + 7;
		b = a;
		if (b < 57)
			b = r15;

		if (b > 63) {
			a = 1;
			b = 0;
			r8 = 0;
		}

		c = var_38 - 1;

		r14 = (BCWC_S2_REG_READ(offset_5) & 0xf) | var_2c;
		if (r14 == 0)
			var_2c = fifo_delay;
		if (var_2c == 0)
			c = 1;

		r12 = (BCWC_S2_REG_READ(offset_5) & 0xf0) | var_30;
		if (r12 == 0)
			var_30 = fifo_delay;
		if (var_30 == 0)
			d = 1;

		a += fifo_delay;
		if (a < 8) {
			r15 = (c | d) ^ 1;
			fifo_delay = a;
		} else {
			if (var_30 == 0)
				var_30 = 7;
			if (var_2c == 0)
				var_2c = 7;
			fifo_delay = 7;
			r15 = 1;
		}

		BCWC_S2_REG_WRITE((r8 & 0x3f) | 0x30000, offset_1);
		bcwc_hw_pci_post(dev_priv);

		BCWC_S2_REG_WRITE((b & 0x3f) | 0x30100, offset_2);
		bcwc_hw_pci_post(dev_priv);

		BCWC_S2_REG_WRITE((b & 0x3f) | 0x30100, offset_3);
		bcwc_hw_pci_post(dev_priv);

		if (var_38 == 0)
			break;

		a = var_38 - 1;
		if (r15 != 0)
			break;
		r15 = b;
	} while(1);

	if (var_38 == 0) {
		dev_err(&dev_priv->pdev->dev, "rd_data_dly_fifo timed out\n\n");
		ret = -EIO;
		goto out;
	}

	dev_info(&dev_priv->pdev->dev, "rd_data_dly_fifo succeeded\n");

	BCWC_S2_REG_WRITE(reg_saved_1, offset_1);
	bcwc_hw_pci_post(dev_priv);

	BCWC_S2_REG_WRITE(reg_saved_2, offset_2);
	bcwc_hw_pci_post(dev_priv);

	BCWC_S2_REG_WRITE(reg_saved_3, offset_3);
	bcwc_hw_pci_post(dev_priv);

	if (var_30 > var_2c)
		var_2c = var_30;

	var_2c++;
	var_30 = 7;

	if (var_2c <= 7)
		var_30 = var_2c;

	if (var_30 < 7)
		var_30++;

	BCWC_S2_REG_WRITE(var_30, delay_reg);
	bcwc_hw_pci_post(dev_priv);

	ret = 0;
out:
	return ret;
}

static int bcwc_hw_ddr_calibrate_one_re_fifo(struct bcwc_private *dev_priv,
				u32 base, u32 *var_68, u32 *var_6c, u32 *var_70)
{
	u32 vdl_bits, vdl_status;
	int i;

	u32 var_2c, var_44, var_48;
	u32 si, a, c, r13, r14, r15;

	u32 offset_2 = base + 0x200; /* stored in var_60 */
	u32 offset_3 = base + 0x274; /* stored in var_50 */
	u32 offset_4 = base + 0x314; /* stored in var_58 */
	u32 offset_5 = base + 0x394; /* stored in var_40 */
	u32 offset_6 = base + 0x390; /* stored in var_38 */


	vdl_status = BCWC_S2_REG_READ(S2_DDR40_PHY_VDL_STATUS);
	vdl_bits = (vdl_status >> 4) & 0xff;

	BCWC_S2_REG_WRITE(0x30000, offset_2);
	bcwc_hw_pci_post(dev_priv);

	BCWC_S2_REG_WRITE(0x30100, offset_3);
	bcwc_hw_pci_post(dev_priv);

	BCWC_S2_REG_WRITE(0x30100, offset_4);
	bcwc_hw_pci_post(dev_priv);

	/* Still don't know why we do this */
	bcwc_hw_verify_mem(dev_priv, 0);

	BCWC_S2_REG_WRITE(1, offset_5);
	bcwc_hw_pci_post(dev_priv);

	var_48 = 0;
	var_2c = 0;
	r15 = 0;

	a = 0;
	var_44 = 0;

	for (i = 10000; i >= 0 && a == 0; i--) {
		bcwc_hw_verify_mem(dev_priv, 0);

		r13 = BCWC_S2_REG_READ(offset_6);

		BCWC_S2_REG_WRITE(1, offset_5);
		bcwc_hw_pci_post(dev_priv);

		if ((r13 & 0xf) == 0) {
			if (r15 == 0)
				r15 = 1;
			else
				a = 1;
		}

		if ((r13 & 0xf0) == 0) {
			if (var_2c == 0)
				var_2c = 1;
			else
				a = 1;
		}

		if (var_48 > 0x3e) {
			r13 = ((var_44 + 1) & 0x3f) | 0x30100;

			BCWC_S2_REG_WRITE(r13, offset_3);
			bcwc_hw_pci_post(dev_priv);

			BCWC_S2_REG_WRITE(r13, offset_4);
			bcwc_hw_pci_post(dev_priv);

			if (r13 >= 0x41) {
				dev_err(&dev_priv->pdev->dev,
					"First RDEN byte timeout\n");
				return -EIO;
			}
		} else {
			var_48++;
			BCWC_S2_REG_WRITE((var_48 & 0x3f) | 0x30000, offset_2);
			bcwc_hw_pci_post(dev_priv);
		}
	}

	if (i <= 0) {
		dev_err(&dev_priv->pdev->dev, "WL FIFO timeout\n");
		return -EIO;
	}

	var_48 = BCWC_S2_REG_READ(offset_2);
	si = 0;

	if (r15 == 0) {
		r15 = BCWC_S2_REG_READ(offset_3) & 0x3f;
		a = var_44 + 1;

		for (i = 1000; i >= 0; i--) {

			if (a >= 65) {
				dev_err(&dev_priv->pdev->dev,
					"RDEN byte1 TO timeout\n");
				return -EIO;
			}

			var_44 = a;
			BCWC_S2_REG_WRITE((a & 0x3f) | 0x30100, offset_4);
			bcwc_hw_pci_post(dev_priv);

			bcwc_hw_verify_mem(dev_priv, 0);

			r13 = BCWC_S2_REG_READ(offset_6);
			BCWC_S2_REG_WRITE(0x1, offset_5);
			bcwc_hw_pci_post(dev_priv);

			if (!(r13 & 0xf0))
				break;
		}

		if (i <= 0) {
			dev_err(&dev_priv->pdev->dev, "RDEN byte1 timeout\n");
			return -EIO;
		}

		si = BCWC_S2_REG_READ(offset_4) & 0x3f;
	} else {
		r15 = 0;
	}

	if (var_2c == 1) {
		var_2c = BCWC_S2_REG_READ(offset_4) & 0x3f;
		r14 = var_44 + 1;

		for (i = 10000; i > 0; i--) {
			if (r14 >= 65) {
				dev_err(&dev_priv->pdev->dev,
					"RDEN byte0 TO timeout\n");
				return -EIO;
			}

			r14 = (r14 & 0x3f) | 0x30100;
			BCWC_S2_REG_WRITE(r14, offset_3);
			bcwc_hw_pci_post(dev_priv);

			bcwc_hw_verify_mem(dev_priv, 0);

			r13 = BCWC_S2_REG_READ(offset_6);
			BCWC_S2_REG_WRITE(1, offset_5);
			bcwc_hw_pci_post(dev_priv);

			if (i > 0)
				r14++;

			if (!(r13 && 0x3f))
				break;
		}

		if (i <= 0) {
			dev_err(&dev_priv->pdev->dev,
				"Second RDEN byte timeout\n");
			return -EIO;
		}

		r15 = BCWC_S2_REG_READ(offset_3) & 0x3f;
		si = var_2c;
	}

	c = (var_48 & 0x3f) + vdl_bits;
	*var_70 = c;

	if (c > 63) {
		*var_70 = 63;
		a = r15 + (c - 63);

		if (a >= 64)
			a = 63;

		c = si + (c - 63);

		if (c >= 64)
			c = 63;

		*var_68 = a;
		*var_6c = c;
	} else {
		*var_68 = r15;
		*var_6c = si;
	}

	return 0;
}

static int bcwc_hw_ddr_calibrate_re_byte_fifo(struct bcwc_private *dev_priv)
{
	u32 base = 0x2800;
	u32 var_28 = 0;
	u32 var_3c = 0;
	u32 var_40 = 0;
	int ret;

	u32 offset_1 = base + 0x200;
	u32 offset_2 = base + 0x274;
	u32 offset_3 = base + 0x314;

	/* FIXME: Check that _40 and _3c aren't mixed up */
	ret = bcwc_hw_ddr_calibrate_one_re_fifo(dev_priv, base, &var_40,
						&var_3c, &var_28);
	if (ret)
		return ret;

	var_28 = (var_28 & 0x3f) | 0x30000;
	BCWC_S2_REG_WRITE(var_28, offset_1);
	bcwc_hw_pci_post(dev_priv);

	var_40 = (var_40 & 0x3f) | 0x30100;
	BCWC_S2_REG_WRITE(var_40, offset_2);
	bcwc_hw_pci_post(dev_priv);

	var_3c = (var_3c & 0x3f) | 0x30100;
	BCWC_S2_REG_WRITE(var_3c, offset_3);
	bcwc_hw_pci_post(dev_priv);

	dev_info(&dev_priv->pdev->dev,
		 "RE BYTE FIFO success: 0x%x, 0x%x, 0x%x\n",
		 var_40, var_3c, var_28);

	return 0;
}

static int bcwc_hw_ddr_calibrate_rd_dqs(struct bcwc_private *dev_priv)
{
	return 0;
}

static int bcwc_hw_ddr_calibrate_wr_dq(struct bcwc_private *dev_priv)
{
	return 0;
}

static int bcwc_hw_ddr_calibrate_wr_dm(struct bcwc_private *dev_priv)
{
	return 0;
}

static int bcwc_hw_ddr_calibrate_addr(struct bcwc_private *dev_priv)
{
	return 0;
}

int bcwc_hw_ddr_calibrate(struct bcwc_private *dev_priv)
{
	u32 reg;
	int ret, i;

	BCWC_S2_REG_WRITE(0, S2_DDR40_PHY_VDL_CTL);
	bcwc_hw_pci_post(dev_priv);

	BCWC_S2_REG_WRITE(0x200, S2_DDR40_PHY_VDL_CTL);
	bcwc_hw_pci_post(dev_priv);

	for (i = 0 ; i <= 50; i++) {
		reg = BCWC_S2_REG_READ(S2_DDR40_PHY_VDL_STATUS);
		if (reg & 0x1)
			break;
		/* We don't handle errors here, maybe we should */
	}

	ret = bcwc_hw_ddr_calibrate_rd_data_dly_fifo(dev_priv);
	if (ret)
		return ret;

	ret = bcwc_hw_ddr_calibrate_re_byte_fifo(dev_priv);
	if (ret)
		return ret;

	ret = bcwc_hw_ddr_calibrate_rd_dqs(dev_priv);
	if (ret)
		return ret;

	ret = bcwc_hw_ddr_calibrate_wr_dq(dev_priv);
	if (ret)
		return ret;

	ret = bcwc_hw_ddr_calibrate_wr_dm(dev_priv);
	if (ret)
		return ret;

	ret = bcwc_hw_ddr_calibrate_addr(dev_priv);
	if (ret)
		return ret;

	ret = bcwc_hw_verify_mem_full(dev_priv, 0);
	if (ret) {
		dev_err(&dev_priv->pdev->dev,
			"Full memory verification failed! (%d)\n", ret);
		return -EIO;
	} else {
		dev_info(&dev_priv->pdev->dev,
			 "Full memory verification succeeded! (%d)\n", ret);
	}

	return 0;
}


