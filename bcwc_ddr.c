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

#define MEM_VERIFY_BASE		0x1000
#define MEM_VERIFY_NUM		128
#define MEM_VERIFY_NUM_FULL	(1 * 1024 * 1024)

int bcwc_ddr_verify_mem(struct bcwc_private *dev_priv, u32 base, int count)
{
	u32 i, val, val_read;
	int failed_bits = 0;
	struct rnd_state state;

	prandom_seed_state(&state, 0x12345678);

	for (i = 0; i < count; i++) {
		val = prandom_u32_state(&state);
		BCWC_S2_MEM_WRITE(val, i * 4 + MEM_VERIFY_BASE);
	}

	prandom_seed_state(&state, 0x12345678);

	for (i = 0; i < count; i++) {
		val = prandom_u32_state(&state);
		val_read = BCWC_S2_MEM_READ(i * 4 + MEM_VERIFY_BASE);

		failed_bits |= val ^ val_read;
	}

	return ((failed_bits & 0xffff) | ((failed_bits >> 16) & 0xffff));
}

static int bcwc_ddr_calibrate_rd_data_dly_fifo(struct bcwc_private *dev_priv)
{
	u32 fifo_status[2];
	u32 rden_bytes[3];
	u32 rden_wl_setting, rden_bl_setting, setting, tmp;
	u32 byte0_pass, byte1_pass;
	int passed;

	rden_bytes[0] = BCWC_S2_REG_READ(S2_DDR40_RDEN_BYTE);
	rden_bytes[1] = BCWC_S2_REG_READ(S2_DDR40_RDEN_BYTE0);
	rden_bytes[2] = BCWC_S2_REG_READ(S2_DDR40_RDEN_BYTE1);

	BCWC_S2_REG_WRITE(0x30000, S2_DDR40_RDEN_BYTE);
	BCWC_S2_REG_WRITE(0x30100, S2_DDR40_RDEN_BYTE0);
	BCWC_S2_REG_WRITE(0x30100, S2_DDR40_RDEN_BYTE1);

	passed = 0;
	setting = 1;
	byte0_pass = 0;
	byte1_pass = 0;
	rden_wl_setting = 0;
	rden_bl_setting = 0;

	while (passed == 0) {
		BCWC_S2_REG_WRITE(setting & 0x7, S2_DDR40_WL_RD_DATA_DLY);

		bcwc_ddr_verify_mem(dev_priv, 0, MEM_VERIFY_NUM);

		tmp = BCWC_S2_REG_READ(S2_DDR40_WL_READ_FIFO_STATUS);
		fifo_status[0] = tmp & 0xf;
		fifo_status[1] = (tmp & 0xf0) >> 4;

		BCWC_S2_REG_WRITE(1, S2_DDR40_WL_READ_FIFO_CLEAR);

		if (fifo_status[0] == 0) {
			if (byte0_pass == 0)
				byte0_pass = setting;
		}

		if (fifo_status[1] == 0) {
			if (byte1_pass == 0)
				byte1_pass = setting;
		}

		if (rden_wl_setting < 57)
			rden_wl_setting += 7;
		else
			rden_bl_setting += 7;

		if (rden_bl_setting > 63) {
			setting++;
			rden_wl_setting = 0;
			rden_bl_setting = 0;
		}

		if (byte0_pass != 0 && byte1_pass != 0)
			passed = 1;

		/* Seems we default to setting=7 if no pass is found */
		if (setting > 7) {
			passed = 1;
			setting = 7;

			if (byte0_pass == 0)
				byte0_pass = setting;

			if (byte1_pass == 0)
				byte1_pass = setting;
		}

		/* Write new setting */
		BCWC_S2_REG_WRITE(rden_wl_setting | 0x30000,
				  S2_DDR40_RDEN_BYTE);
		BCWC_S2_REG_WRITE(rden_bl_setting | 0x30100,
				  S2_DDR40_RDEN_BYTE0);
		BCWC_S2_REG_WRITE(rden_bl_setting | 0x30100,
				  S2_DDR40_RDEN_BYTE1);
	}

	setting = byte0_pass;

	if (byte1_pass > setting)
		setting = byte1_pass;

	setting++;

	if (setting > 7)
		setting = 7;

	/* Restore settings */
	BCWC_S2_REG_WRITE(rden_bytes[0], S2_DDR40_RDEN_BYTE);
	BCWC_S2_REG_WRITE(rden_bytes[1], S2_DDR40_RDEN_BYTE0);
	BCWC_S2_REG_WRITE(rden_bytes[2], S2_DDR40_RDEN_BYTE1);

	if (setting < 7)
		setting++;

	BCWC_S2_REG_WRITE(setting, S2_DDR40_WL_RD_DATA_DLY);
	dev_info(&dev_priv->pdev->dev, "RD_DATA_DLY: 0x%x\n", setting);

	return 0;
}

static int bcwc_ddr_calibrate_one_re_fifo(struct bcwc_private *dev_priv,
			u32 *rden_byte, u32 *rden_byte0, u32 *rden_byte1)
{
	u32 fifo_status[2];
	u32 wl_start;
	u32 bl_pass[2] = {0, 0};
	u32 bl_start[2] = {0, 0};
	u32 word_setting, byte_setting, passed, delta;
	u32 tmp;

	delta = ((BCWC_S2_REG_READ(S2_DDR40_PHY_VDL_STATUS) & 0xffc) >> 2) / 4;

	/* Start with word and byte setting at 0 */
	BCWC_S2_REG_WRITE(0x30000, S2_DDR40_RDEN_BYTE);
	BCWC_S2_REG_WRITE(0x30100, S2_DDR40_RDEN_BYTE0);
	BCWC_S2_REG_WRITE(0x30100, S2_DDR40_RDEN_BYTE0);

	bcwc_ddr_verify_mem(dev_priv, 0, MEM_VERIFY_NUM);

	BCWC_S2_REG_WRITE(1, S2_DDR40_WL_READ_FIFO_CLEAR);

	word_setting = 0;
	byte_setting = 0;
	passed = 0;

	while (passed == 0) {
		bcwc_ddr_verify_mem(dev_priv, 0, MEM_VERIFY_NUM);

		fifo_status[0] =
			BCWC_S2_REG_READ(S2_DDR40_WL_READ_FIFO_STATUS) & 0xf;
		fifo_status[1] =
			(BCWC_S2_REG_READ(S2_DDR40_WL_READ_FIFO_STATUS) &
			 0xf0) >> 4;

		BCWC_S2_REG_WRITE(1, S2_DDR40_WL_READ_FIFO_CLEAR);

		if (fifo_status[0] == 0) {
			if (bl_pass[0] == 0)
				bl_pass[0] = 1;
			else
				passed = 1;
		}

		if (fifo_status[1] == 0) {
			if (bl_pass[1] == 0)
				bl_pass[1] = 1;
			else
				passed = 1;
		}

		/* Still not passed */
		if (passed == 0) {
			if (word_setting < 63) {
				word_setting++;
				BCWC_S2_REG_WRITE(0x30000 | (word_setting & 0x3f),
						  S2_DDR40_RDEN_BYTE);
			} else {
				byte_setting++;
				BCWC_S2_REG_WRITE(0x30100 | (byte_setting & 0x3f),
						  S2_DDR40_RDEN_BYTE0);
				BCWC_S2_REG_WRITE(0x30100 | (byte_setting & 0x3f),
						  S2_DDR40_RDEN_BYTE1);

				if (word_setting > 64) {
					dev_err(&dev_priv->pdev->dev,
						"RDEN byte timeout\n");
					return -EIO;
				}
			}
		}
	}

	wl_start = BCWC_S2_REG_READ(S2_DDR40_RDEN_BYTE) & 0x3f;

	if (bl_pass[0] == 1) {
		bl_start[0] = BCWC_S2_REG_READ(S2_DDR40_RDEN_BYTE0) & 0x3f;
		passed = 0;

		while (passed == 0) {
			byte_setting++;
			if (byte_setting > 64) {
				dev_err(&dev_priv->pdev->dev,
					"RDEN BYTE1 timeout\n");
				return -EIO;
			}

			BCWC_S2_REG_WRITE(0x30100 | (byte_setting & 0x3f),
					  S2_DDR40_RDEN_BYTE1);

			bcwc_ddr_verify_mem(dev_priv, 0, MEM_VERIFY_NUM);

			fifo_status[0] = BCWC_S2_REG_READ(
					S2_DDR40_WL_READ_FIFO_STATUS) & 0xf;
			fifo_status[1] =
				(BCWC_S2_REG_READ(S2_DDR40_WL_READ_FIFO_STATUS) &
				 0xf0) >> 4;
			BCWC_S2_REG_WRITE(1, S2_DDR40_WL_READ_FIFO_CLEAR);

			if (fifo_status[1] == 0)
				passed = 1;
		}

		bl_start[1] = BCWC_S2_REG_READ(S2_DDR40_RDEN_BYTE1) & 0x3f;
	}
	if (bl_pass[1] == 1) {
		bl_start[1] = BCWC_S2_REG_READ(S2_DDR40_RDEN_BYTE1) & 0x3f;
		passed = 0;

		while (passed == 0) {
			byte_setting++;
			if (byte_setting > 64) {
				dev_err(&dev_priv->pdev->dev,
					"RDEN BYTE0 timeout\n");
				return -EIO;
			}

			BCWC_S2_REG_WRITE(0x30100 | (byte_setting & 0x3f),
					  S2_DDR40_RDEN_BYTE0);

			bcwc_ddr_verify_mem(dev_priv, 0, MEM_VERIFY_NUM);

			fifo_status[0] = BCWC_S2_REG_READ(
					S2_DDR40_WL_READ_FIFO_STATUS) & 0xf;
			fifo_status[1] = BCWC_S2_REG_READ(
					S2_DDR40_WL_READ_FIFO_STATUS) & 0xf0;
			BCWC_S2_REG_WRITE(1, S2_DDR40_WL_READ_FIFO_CLEAR);

			if (fifo_status[0] == 0)
				passed = 1;
		}

		bl_start[0] = BCWC_S2_REG_READ(S2_DDR40_RDEN_BYTE0) & 0x3f;
	}

	*rden_byte = wl_start + delta;

	if (*rden_byte > 63) {
		tmp = *rden_byte - 63;
		*rden_byte = 63;
		*rden_byte0 = bl_start[0] + tmp;
		*rden_byte1 = bl_start[1] + tmp;
	} else {
		*rden_byte0 = bl_start[0];
		*rden_byte1 = bl_start[1];
	}

	if (*rden_byte0 > 63) {
		*rden_byte0 = 63;
	}
	if (*rden_byte1 > 63) {
		*rden_byte1 = 63;
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

/* Set default/generic read data strobe */
static int bcwc_ddr_generic_shmoo_rd_dqs(struct bcwc_private *dev_priv,
					 u32 *fail_bits)
{
	u32 retries, setting, tmp, offset;
	u32 bytes[S2_DDR40_NUM_BYTE_LANES];
	int i, j, ret, fail;

	/* Save the current byte lanes */
	for (i = 0; i < S2_DDR40_NUM_BYTE_LANES; i++) {
		tmp = BCWC_S2_REG_READ(S2_DDR40_RDEN_BYTE0 +
				       (i * S2_DDR40_BYTE_LANE_SIZE));
		bytes[i] = tmp & 0x3f;
	}

	/* Clear all byte lanes */
	for (i = 0; i < S2_DDR40_NUM_BYTE_LANES; i++) {
		for (j = 0; j < 8; j++) {
			offset = S2_DDR40_2A38 + (i * 0xa0) + (j * 8);

			BCWC_S2_REG_WRITE(0x30000, offset - 4);
			BCWC_S2_REG_WRITE(0x30000, offset);
		}
	}

	setting = (BCWC_S2_REG_READ(S2_DDR40_PHY_DQ_CALIB_STATUS) >> 20) & 0x3f;

	retries = 1000;
	fail = 0;

	while (retries-- > 0 && !fail) {
		ret = bcwc_ddr_verify_mem(dev_priv, 0, MEM_VERIFY_NUM);
		fail_bits[0] = ret;

		if (ret == 0xffff) {
			fail = 1;
			break;
		}

		setting++;
		tmp = (setting & 0x3f) | 0x30100;

		/* Byte 0 */
		BCWC_S2_REG_WRITE(tmp, S2_DDR40_2A08);
		BCWC_S2_REG_WRITE(tmp, S2_DDR40_2A0C);

		/* Byte 1 */
		BCWC_S2_REG_WRITE(tmp, S2_DDR40_2AA8);
		BCWC_S2_REG_WRITE(tmp, S2_DDR40_2AAC);

		if (setting > 62)
			fail = 1;

		offset = S2_DDR40_RDEN_BYTE0;

		/* Write byte lane settings */
		for (i = 0; i < 2; i++) {
			bytes[i]++;

			if (bytes[i] > 62)
				fail = 1;

			BCWC_S2_REG_WRITE((bytes[i] & 0x3f) | 0x30100, offset);

			offset += 0xa0;
		}
	}

	if (retries == 0) {
		dev_err(&dev_priv->pdev->dev, "Generic shmoo RD DQS timeout\n");
		ret = -EIO;
	}

	if (fail)
		dev_info(&dev_priv->pdev->dev, "Generic RD DQS failed\n");
	else
		dev_info(&dev_priv->pdev->dev, "Generic RD DQS succeeded\n");

	/* It always fails, so just pass success */
	return 0;
}

static int bcwc_ddr_calibrate_rd_dqs(struct bcwc_private *dev_priv,
				     u32 *fails, u32 *settings)
{
	s32 pass_len[16];
	u32 pass_start[16]; // u32 var_b0[16];
	u32 pass_end[16]; // u32 var_f0[16];
	int fail_sum, i, j, bit;
	s32 setting;
	printk(KERN_CONT "\n");

	for (bit = 0; bit < 16; bit++) {
		pass_start[bit] = 64;
		pass_end[bit] = 64;

		printk(KERN_CONT "%.2d: ", bit);

		/* Start looking for start of pass */
		for (i = 0; i < 63; i++) {
			fail_sum = 0;

			/* We check ahead the 6 next fail bits */
			for (j = 0; (j < 6) && ((i + j) < 64); j++)
				fail_sum += fails[i + j] & (1 << bit);

			if (fail_sum) {
				printk(KERN_CONT ".");
			} else {
				printk(KERN_CONT "O");

				pass_start[bit] = i;
				break;
			}
		}

		/* Start looking for end of pass */
		for (; i < 63; i++) {
			if (fails[i] & (1 << bit)) {
				if (pass_end[bit] == 64)
					pass_end[bit] = i;

				printk(KERN_CONT ".");
			} else {
				printk(KERN_CONT "O");
			}
		}

		/* Calculate pass length */
		pass_len[bit] = pass_end[bit] - pass_start[bit];

		/* Calculate new setting */
		setting = (pass_len[bit] / 2) + pass_start[bit];
		if (setting < 0)
			setting = 0;
		else if (setting > 63)
			setting = 63;
		settings[bit] = setting;

		printk(KERN_CONT " : start=%d end=%d len=%d new=%d\n", pass_start[bit],
		       pass_end[bit], pass_len[bit], settings[bit]);
	}

	for (bit = 0; bit < 16; bit++) {

	}

	// Some global stuff that I need to figure out

	return 0;
}

static int bcwc_ddr_wr_dqs_setting(struct bcwc_private *dev_priv, int set_bits,
				   u32 *fail_bits, u32 *settings)
{
	u32 var_58, var_2c, var_30, var_34, var_48, var_5c;
	u32 a, b, c, d, r12, r13, r15;
	u32 setting, byte, bit, offset, tmp, start, inc, reg;
	int i;

	var_5c = S2_DDR40_PHY_BASE;
	var_58 = S2_DDR40_2A38;
	a = set_bits;
	c = a & 0x2;
	var_2c = c;
	a = a & 1;
	var_30 = a;

	for (setting = 0; setting < 64; setting++) {
		for (byte = 0; byte < 2; byte++) {
			for (bit = 0; bit < 8; bit++) {
				offset = S2_DDR40_2A38 + (byte * 0xa0) +
					 (bit * 8);
				tmp = setting | 0x30000;

				if (set_bits & 1)
					BCWC_S2_REG_WRITE(tmp, offset - 4);

				if (set_bits & 2)
					BCWC_S2_REG_WRITE(tmp, offset);
			}
		}
		fail_bits[setting] = bcwc_ddr_verify_mem(dev_priv, 0, MEM_VERIFY_NUM);
	}

	b = 1;
	r13 = 0;
	a = set_bits;
	r12 = r13;
/*
	if (set_bits != 3) {
		r12 = (a != 1) ? 1 : 0;
		b = 2;
	}
*/

	if (set_bits == 3) {
		start = 0;
		inc = 1;
	} else if (set_bits == 2) {
		start = 0;
		inc = 2;
	} else {
		start = 1;
		inc = 2;
	}

	var_48 = r12;

	bcwc_ddr_calibrate_rd_dqs(dev_priv, fail_bits, settings);

	a = var_5c;
	// c = S2_DDR40_2A34 + r12 * 4;
	reg = S2_DDR40_2A34 + r12 * 4;
	a = b * 4;
	var_2c = a;
	// d = r13;

	offset = 0;

	for (d = 0; d < 2; d++) {
		/*
		var_34 = d;
		var_30 = c;
		r15 = c;
		*/

		// while (r12 < 0x10) {
		for (i = start; i < 16; i += inc) {
			//a = r13;

			if (settings[offset] == 0 || settings[offset] >= 63) {
				dev_err(&dev_priv->pdev->dev,
					"Bad VDL. Step %d = 0x%x\n",
					offset, settings[offset]);
				return -EINVAL;
			}

			// d = r15;
			// c = (c & 0x3f) | 0x30000;
			tmp = (settings[offset] & 0x3f) | 0x30000;
			BCWC_S2_REG_WRITE(tmp, reg);
			if (set_bits == 3) {
				if (i & 1)
					offset++;
				//a = r12;
				//a = a & 1;
				//r13 += a;
			} else {
				// r13++;
				offset++;
			}

			//r15 += var_2c;
			reg += inc;
		}

/*
		c = var_30;
		c += S2_DDR40_BYTE_LANE_SIZE;
		d = var_34;
		a = 0;
		r12 = var_48;
*/
	}

	return 0;
}

static int bcwc_ddr_calibrate_create_result(struct bcwc_private *dev_priv)
{
	return 0;
}

static int bcwc_ddr_generic_shmoo_calibrate_rd_dqs(
						struct bcwc_private *dev_priv)
{
	u32 settings[64]; /* Don't know the real size yet */
	u32 fails[64]; /* Number of fails on a setting */
	int ret;

	ret = bcwc_ddr_generic_shmoo_rd_dqs(dev_priv, fails);
	if (ret)
		return ret;

	ret = bcwc_ddr_wr_dqs_setting(dev_priv, 3, fails, settings);
	if (ret)
		return ret;

	ret = bcwc_ddr_wr_dqs_setting(dev_priv, 1, fails, settings);
	if (ret)
		return ret;


	ret = bcwc_ddr_wr_dqs_setting(dev_priv, 2, fails, settings);
	if (ret)
		return ret;

	/*
	ret = bcwc_ddr_calibrate_create_result(dev_priv);
	if (ret)
		return ret;
	*/

	/* FIXME: Continue here... */

	return 0;
}

static int bcwc_ddr_generic_shmoo_calibrate_wr_dq(struct bcwc_private *dev_priv)
{
	return 0;
}

static int bcwc_ddr_generic_shmoo_calibrate_wr_dm(struct bcwc_private *dev_priv)
{
	return 0;
}

static int bcwc_ddr_generic_shmoo_calibrate_addr(struct bcwc_private *dev_priv)
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

	ret = bcwc_ddr_generic_shmoo_calibrate_rd_dqs(dev_priv);
	if (ret)
		return ret;

	ret = bcwc_ddr_generic_shmoo_calibrate_wr_dq(dev_priv);
	if (ret)
		return ret;

	ret = bcwc_ddr_generic_shmoo_calibrate_wr_dm(dev_priv);
	if (ret)
		return ret;

	ret = bcwc_ddr_generic_shmoo_calibrate_addr(dev_priv);
	if (ret)
		return ret;

	ret = bcwc_ddr_verify_mem(dev_priv, 0, MEM_VERIFY_NUM_FULL);
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
