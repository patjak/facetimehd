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
static inline void bcwc_ddr_mem_pattern(u32 index, u32 *addr, u32 *val)
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

int bcwc_ddr_verify_mem_full(struct bcwc_private *dev_priv, u32 base)
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

int bcwc_ddr_verify_mem(struct bcwc_private *dev_priv, u32 base)
{
	u32 i, addr, val, val_read;
	int fails = 0;

	for (i = 0; i < 0x400; i += 4) {
		bcwc_ddr_mem_pattern(i, &addr, &val);
		BCWC_S2_MEM_WRITE(val, base + addr);
	}

	for (i = 0; i < 0x400; i +=4) {
		bcwc_ddr_mem_pattern(i, &addr, &val);
		val_read = BCWC_S2_MEM_READ(base + addr);

		if (val_read != val)
			fails++;
	}

	if (fails > 0)
		return -EIO;

	return 0;
}


/* FIXME: Make some more sense out of this */
static int bcwc_ddr_calibrate_rd_data_dly_fifo(struct bcwc_private *dev_priv)
{
	u32 rden_byte_save, rden_byte0_save, rden_byte1_save;
	u32 rden_byte; /* Value for RDEN BYTE */
	u32 rden_byte01; /* Value for RDEN BYTE0 and BYTE1 */

	u32 a, c, d, r15;
	u32 var_2c, var_30, fifo_delay, status;

	int ret, i;

	/* Save current register values */
	rden_byte_save = BCWC_S2_REG_READ(S2_DDR40_RDEN_BYTE);
	rden_byte0_save = BCWC_S2_REG_READ(S2_DDR40_RDEN_BYTE0);
	rden_byte1_save = BCWC_S2_REG_READ(S2_DDR40_RDEN_BYTE1);

	BCWC_S2_REG_WRITE(0x30000, S2_DDR40_RDEN_BYTE);
	BCWC_S2_REG_WRITE(0x30100, S2_DDR40_RDEN_BYTE0);
	BCWC_S2_REG_WRITE(0x30100, S2_DDR40_RDEN_BYTE1);

	fifo_delay = 1;
	rden_byte01 = 0;
	var_30 = 0;
	var_2c = 0;

	for (i = 1000; i > 0; i--) {
		r15 = rden_byte01;

		BCWC_S2_REG_WRITE((fifo_delay & 0x7),
				  S2_DDR40_RD_DATA_DLY_FIFO);

		/*
		 * How do we know if verification was successful?
		 * OSX doesn't check any return values from it's verification so
		 * perhaps controller can detect this itself and set some regs.
		 */
		bcwc_ddr_verify_mem(dev_priv, 0);

		BCWC_S2_REG_WRITE(1, S2_DDR40_TIMING_CTL);

		rden_byte = (rden_byte01 >= 57) ? rden_byte01 :
						  (rden_byte01 + 7);

		a = r15 + 7;
		rden_byte01 = a;
		if (rden_byte01 < 57)
			rden_byte01 = r15;

		if (rden_byte01 > 63) {
			a = 1;
			rden_byte01 = 0;
			rden_byte = 0;
		}

		c = i - 1;
		status = BCWC_S2_REG_READ(S2_DDR40_TIMING_STATUS);

		if (((status & 0xf) | var_2c) == 0)
			var_2c = fifo_delay;

		if (var_2c == 0)
			c = 1;

		if (((status & 0xf0) | var_30) == 0)
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

		BCWC_S2_REG_WRITE((rden_byte & 0x3f) | 0x30000,
				  S2_DDR40_RDEN_BYTE);
		BCWC_S2_REG_WRITE((rden_byte01 & 0x3f) | 0x30100,
				  S2_DDR40_RDEN_BYTE0);
		BCWC_S2_REG_WRITE((rden_byte01 & 0x3f) | 0x30100,
				  S2_DDR40_RDEN_BYTE1);

		if (r15 != 0)
			break;
	}

	if (i == 0) {
		dev_err(&dev_priv->pdev->dev, "rd_data_dly_fifo timed out\n\n");
		ret = -EIO;
		goto out;
	}

	dev_info(&dev_priv->pdev->dev, "rd_data_dly_fifo succeeded\n");

	/* Restore read enable bytes */
	BCWC_S2_REG_WRITE(rden_byte_save, S2_DDR40_RDEN_BYTE);
	BCWC_S2_REG_WRITE(rden_byte0_save, S2_DDR40_RDEN_BYTE0);
	BCWC_S2_REG_WRITE(rden_byte1_save, S2_DDR40_RDEN_BYTE1);

	if (var_30 > var_2c)
		var_2c = var_30;

	var_2c++;
	var_30 = 7;

	if (var_2c <= 7)
		var_30 = var_2c;

	if (var_30 < 7)
		var_30++;

	BCWC_S2_REG_WRITE(var_30, S2_DDR40_RD_DATA_DLY_FIFO);

	ret = 0;
out:
	return ret;
}

static int bcwc_ddr_calibrate_one_re_fifo(struct bcwc_private *dev_priv,
			u32 *rden_byte, u32 *rden_byte0, u32 *rden_byte1)
{
	u32 vdl_bits, vdl_status;
	int i;

	u32 var_2c, var_44, var_48;
	u32 si, a, c, r13, r14, r15;

	vdl_status = BCWC_S2_REG_READ(S2_DDR40_PHY_VDL_STATUS);
	vdl_bits = (vdl_status >> 4) & 0xff;

	BCWC_S2_REG_WRITE(0x30000, S2_DDR40_RDEN_BYTE);
	BCWC_S2_REG_WRITE(0x30100, S2_DDR40_RDEN_BYTE0);
	BCWC_S2_REG_WRITE(0x30100, S2_DDR40_RDEN_BYTE1);

	/* Still don't know why we do this */
	bcwc_ddr_verify_mem(dev_priv, 0);

	BCWC_S2_REG_WRITE(1, S2_DDR40_TIMING_CTL);

	var_48 = 0;
	var_2c = 0;
	r15 = 0;

	a = 0;
	var_44 = 0;

	for (i = 10000; i >= 0 && a == 0; i--) {
		bcwc_ddr_verify_mem(dev_priv, 0);

		r13 = BCWC_S2_REG_READ(S2_DDR40_TIMING_STATUS);

		BCWC_S2_REG_WRITE(1, S2_DDR40_TIMING_CTL);

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

			BCWC_S2_REG_WRITE(r13, S2_DDR40_RDEN_BYTE0);
			BCWC_S2_REG_WRITE(r13, S2_DDR40_RDEN_BYTE1);

			if (r13 >= 0x41) {
				dev_err(&dev_priv->pdev->dev,
					"First RDEN byte timeout\n");
				return -EIO;
			}
		} else {
			var_48++;
			BCWC_S2_REG_WRITE((var_48 & 0x3f) | 0x30000,
					  S2_DDR40_RDEN_BYTE);
		}
	}

	if (i <= 0) {
		dev_err(&dev_priv->pdev->dev, "WL FIFO timeout\n");
		return -EIO;
	}

	var_48 = BCWC_S2_REG_READ(S2_DDR40_RDEN_BYTE);
	si = 0;

	if (r15 == 0) {
		r15 = BCWC_S2_REG_READ(S2_DDR40_RDEN_BYTE0) & 0x3f;
		a = var_44 + 1;

		for (i = 1000; i >= 0; i--) {

			if (a >= 65) {
				dev_err(&dev_priv->pdev->dev,
					"RDEN byte1 TO timeout\n");
				return -EIO;
			}

			var_44 = a;
			BCWC_S2_REG_WRITE((a & 0x3f) | 0x30100,
					  S2_DDR40_RDEN_BYTE1);

			bcwc_ddr_verify_mem(dev_priv, 0);

			r13 = BCWC_S2_REG_READ(S2_DDR40_TIMING_STATUS);
			BCWC_S2_REG_WRITE(0x1, S2_DDR40_TIMING_CTL);

			if (!(r13 & 0xf0))
				break;
		}

		if (i <= 0) {
			dev_err(&dev_priv->pdev->dev, "RDEN byte1 timeout\n");
			return -EIO;
		}

		si = BCWC_S2_REG_READ(S2_DDR40_RDEN_BYTE1) & 0x3f;
	} else {
		r15 = 0;
	}

	if (var_2c == 1) {
		var_2c = BCWC_S2_REG_READ(S2_DDR40_RDEN_BYTE1) & 0x3f;
		r14 = var_44 + 1;

		for (i = 10000; i > 0; i--) {
			if (r14 >= 65) {
				dev_err(&dev_priv->pdev->dev,
					"RDEN byte0 TO timeout\n");
				return -EIO;
			}

			r14 = (r14 & 0x3f) | 0x30100;
			BCWC_S2_REG_WRITE(r14, S2_DDR40_RDEN_BYTE0);

			bcwc_ddr_verify_mem(dev_priv, 0);

			r13 = BCWC_S2_REG_READ(S2_DDR40_TIMING_STATUS);
			BCWC_S2_REG_WRITE(1, S2_DDR40_TIMING_CTL);

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

		r15 = BCWC_S2_REG_READ(S2_DDR40_RDEN_BYTE0) & 0x3f;
		si = var_2c;
	}

	c = (var_48 & 0x3f) + vdl_bits;
	*rden_byte = c;

	if (c > 63) {
		*rden_byte = 63;
		a = r15 + (c - 63);

		if (a >= 64)
			a = 63;

		c = si + (c - 63);

		if (c >= 64)
			c = 63;

		*rden_byte0 = a;
		*rden_byte1 = c;
	} else {
		*rden_byte0 = r15;
		*rden_byte1 = si;
	}

	return 0;
}

static int bcwc_ddr_calibrate_re_byte_fifo(struct bcwc_private *dev_priv)
{
	u32 rden_byte = 0;
	u32 rden_byte0 = 0;
	u32 rden_byte1 = 0;
	int ret;

	ret = bcwc_ddr_calibrate_one_re_fifo(dev_priv, &rden_byte, &rden_byte0, &rden_byte1);
	if (ret)
		return ret;

	rden_byte = (rden_byte & 0x3f) | 0x30000;
	BCWC_S2_REG_WRITE(rden_byte, S2_DDR40_RDEN_BYTE);

	rden_byte0 = (rden_byte0 & 0x3f) | 0x30100;
	BCWC_S2_REG_WRITE(rden_byte0, S2_DDR40_RDEN_BYTE0);

	rden_byte1 = (rden_byte1 & 0x3f) | 0x30100;
	BCWC_S2_REG_WRITE(rden_byte1, S2_DDR40_RDEN_BYTE1);

	dev_info(&dev_priv->pdev->dev,
		 "RE BYTE FIFO success: b0 = 0x%x, b1 = 0x%x, b = 0x%x\n",
		 rden_byte0, rden_byte1, rden_byte);

	return 0;
}

static int bcwc_ddr_generic_rd_dqs(struct bcwc_private *dev_priv)
{
	return 0;
}

static int bcwc_ddr_wr_dqs_setting(struct bcwc_private *dev_priv)
{
	return 0;
}

static int bcwc_ddr_calibrate_create_result(struct bcwc_private *dev_priv)
{
	return 0;
}

static int bcwc_ddr_calibrate_rd_dqs(struct bcwc_private *dev_priv)
{
	int ret;

	ret = bcwc_ddr_generic_rd_dqs(dev_priv);
	if (ret)
		return ret;

	ret = bcwc_ddr_wr_dqs_setting(dev_priv);
	if (ret)
		return ret;

	ret = bcwc_ddr_calibrate_create_result(dev_priv);
	if (ret)
		return ret;

	/* FIXME: Continue here... */

	return 0;
}

static int bcwc_ddr_calibrate_wr_dq(struct bcwc_private *dev_priv)
{
	return 0;
}

static int bcwc_ddr_calibrate_wr_dm(struct bcwc_private *dev_priv)
{
	return 0;
}

static int bcwc_ddr_calibrate_addr(struct bcwc_private *dev_priv)
{
	return 0;
}

int bcwc_ddr_calibrate(struct bcwc_private *dev_priv)
{
	u32 reg;
	int ret, i;

	BCWC_S2_REG_WRITE(0, S2_DDR40_PHY_VDL_CTL);
	BCWC_S2_REG_WRITE(0x200, S2_DDR40_PHY_VDL_CTL);

	for (i = 0 ; i <= 50; i++) {
		reg = BCWC_S2_REG_READ(S2_DDR40_PHY_VDL_STATUS);
		if (reg & 0x1)
			break;
		/* We don't handle errors here, maybe we should */
	}

	ret = bcwc_ddr_calibrate_rd_data_dly_fifo(dev_priv);
	if (ret)
		return ret;

	ret = bcwc_ddr_calibrate_re_byte_fifo(dev_priv);
	if (ret)
		return ret;

	ret = bcwc_ddr_calibrate_rd_dqs(dev_priv);
	if (ret)
		return ret;

	ret = bcwc_ddr_calibrate_wr_dq(dev_priv);
	if (ret)
		return ret;

	ret = bcwc_ddr_calibrate_wr_dm(dev_priv);
	if (ret)
		return ret;

	ret = bcwc_ddr_calibrate_addr(dev_priv);
	if (ret)
		return ret;

	ret = bcwc_ddr_verify_mem_full(dev_priv, 0);
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
