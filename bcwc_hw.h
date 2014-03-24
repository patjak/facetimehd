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

#ifndef _BCWC_HW_H
#define _BCWC_HW_H

#define BCWC_LINK_REG_READ(offset) ioread32(dev_priv->link_io + (offset))
#define BCWC_LINK_REG_WRITE(val, offset) iowrite32((val), \
						   dev_priv->link_io + (offset))
#define BCWC_DEV_REG_READ(offset) ioread32(dev_priv->dev_io + (offset))
#define BCWC_DEV_REG_WRITE(val, offset) iowrite32((val), \
						   dev_priv->dev_io + (offset))

extern int bcwc_hw_init(struct bcwc_private *dev_priv);

#endif
