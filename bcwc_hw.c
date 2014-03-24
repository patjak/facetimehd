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

#include "bcwc_drv.h"
#include "bcwc_hw.h"

#if 0
static int bcwc_hw_set_core_clk(struct bcwc_private *dev_priv)
{
	return 0;
}

static int bcwc_hw_aspm_enable(struct bcwc_private *dev_priv)
{
	return 0;
}

static int bcwc_hw_aspm_disable(struct bcwc_private *dev_priv)
{
	return 0;
}
#endif

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
	u32 reg;

	return 0;
}
