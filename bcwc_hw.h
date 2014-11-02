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

#ifndef _BCWC_HW_H
#define _BCWC_HW_H

#include <linux/pci.h>

/* Used after most PCI Link IO writes */
static inline void bcwc_hw_pci_post(struct bcwc_private *dev_priv)
{
	pci_write_config_dword(dev_priv->pdev, 0, 0x12345678);
}

#define BCWC_S2_REG_READ(offset) _BCWC_S2_REG_READ(dev_priv, (offset))
#define BCWC_S2_REG_WRITE(val, offset) _BCWC_S2_REG_WRITE(dev_priv, (val), (offset))

#define BCWC_S2_MEM_READ(offset) _BCWC_S2_MEM_READ(dev_priv, (offset))
#define BCWC_S2_MEM_WRITE(val, offset) _BCWC_S2_MEM_WRITE(dev_priv, (val), (offset))

#define BCWC_ISP_REG_READ(offset) _BCWC_ISP_REG_READ(dev_priv, (offset))
#define BCWC_ISP_REG_WRITE(val, offset) _BCWC_ISP_REG_WRITE(dev_priv, (val), (offset))

static inline u32 _BCWC_S2_REG_READ(struct bcwc_private *dev_priv, u32 offset)
{
	if (offset >= dev_priv->s2_io_len) {
		dev_err(&dev_priv->pdev->dev,
			"S2 IO read out of range at %u\n", offset);
		return 0;
	}

	// dev_info(&dev_priv->pdev->dev, "Link IO read at %u\n", offset);
	return ioread32(dev_priv->s2_io + offset);
}

static inline void _BCWC_S2_REG_WRITE(struct bcwc_private *dev_priv, u32 val,
				      u32 offset)
{
	if (offset >= dev_priv->s2_io_len) {
		dev_err(&dev_priv->pdev->dev,
			"S2 IO write out of range at %u\n", offset);
		return;
	}

	// dev_info(&dev_priv->pdev->dev, "S2 IO write at %u\n", offset);
	iowrite32(val, dev_priv->s2_io + offset);
	bcwc_hw_pci_post(dev_priv);
}

static inline u32 _BCWC_S2_MEM_READ(struct bcwc_private *dev_priv, u32 offset)
{
	if (offset >= dev_priv->s2_mem_len) {
		dev_err(&dev_priv->pdev->dev,
			"S2 MEM read out of range at %u\n", offset);
		return 0;
	}

	// dev_info(&dev_priv->pdev->dev, "Link IO read at %u\n", offset);
	return ioread32(dev_priv->s2_mem + offset);
}

static inline void _BCWC_S2_MEM_WRITE(struct bcwc_private *dev_priv, u32 val,
				      u32 offset)
{
	if (offset >= dev_priv->s2_mem_len) {
		dev_err(&dev_priv->pdev->dev,
			"S2 MEM write out of range at %u\n", offset);
		return;
	}

	// dev_info(&dev_priv->pdev->dev, "S2 IO write at %u\n", offset);
	iowrite32(val, dev_priv->s2_mem + offset);
}

static inline u32 _BCWC_ISP_REG_READ(struct bcwc_private *dev_priv, u32 offset)
{
	if (offset >= dev_priv->isp_io_len) {
		dev_err(&dev_priv->pdev->dev,
			"ISP IO read out of range at %u\n", offset);
		return 0;
	}

	// dev_info(&dev_priv->pdev->dev, "ISP IO read at %u\n", offset);
	return ioread32(dev_priv->isp_io + offset);
}

static inline void _BCWC_ISP_REG_WRITE(struct bcwc_private *dev_priv, u32 val,
				       u32 offset)
{
	if (offset >= dev_priv->isp_io_len) {
		dev_err(&dev_priv->pdev->dev,
			"ISP IO write out of range at %u\n", offset);
		return;
	}

	// dev_info(&dev_priv->pdev->dev, "Dev IO write at %u\n", offset);
	iowrite32(val, dev_priv->isp_io + offset);
}

extern int bcwc_hw_init(struct bcwc_private *dev_priv);

#endif
