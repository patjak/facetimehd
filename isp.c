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

#include <linux/delay.h>
#include "bcwc_drv.h"
#include "bcwc_hw.h"
#include "bcwc_reg.h"
#include "isp.h"

int isp_init(struct bcwc_private *dev_priv)
{
	u32 num_channels, queue_size;
	u32 reg;
	int i, retries;

	BCWC_ISP_REG_WRITE(0, ISP_IPC_NUM_CHAN);
	bcwc_hw_pci_post(dev_priv);

	BCWC_ISP_REG_WRITE(0, ISP_IPC_QUEUE_SIZE);
	bcwc_hw_pci_post(dev_priv);

	BCWC_ISP_REG_WRITE(0, ISP_REG_C3008);
	bcwc_hw_pci_post(dev_priv);

	BCWC_ISP_REG_WRITE(0, ISP_FW_HEAP_SIZE);
	bcwc_hw_pci_post(dev_priv);

	BCWC_ISP_REG_WRITE(0, ISP_REG_C3010);
	bcwc_hw_pci_post(dev_priv);

	BCWC_ISP_REG_WRITE(0, ISP_REG_C3014);
	bcwc_hw_pci_post(dev_priv);

	BCWC_ISP_REG_WRITE(0, ISP_REG_C3018);
	bcwc_hw_pci_post(dev_priv);

	BCWC_ISP_REG_WRITE(0, ISP_REG_C301C);
	bcwc_hw_pci_post(dev_priv);

	BCWC_ISP_REG_WRITE(0xffffffff, ISP_REG_41024);
	bcwc_hw_pci_post(dev_priv);

	/*
	 * Probably the IPC queue
	 * FIXME: Check if we can do 64bit writes on PCIe
	 */
	for (i = ISP_IPC_CHAN_START; i <= ISP_IPC_CHAN_END; i += 8) {
		BCWC_ISP_REG_WRITE(0xffffffff, i);
		BCWC_ISP_REG_WRITE(0, i + 4);
	}
	bcwc_hw_pci_post(dev_priv);

	BCWC_ISP_REG_WRITE( 0x80000000, ISP_REG_40008);
	bcwc_hw_pci_post(dev_priv);

	BCWC_ISP_REG_WRITE(0x1, ISP_REG_40004);
	bcwc_hw_pci_post(dev_priv);

	for (retries = 0; retries < 1000; retries++) {
		reg = BCWC_ISP_REG_READ(ISP_REG_40004);
		if ((reg & 0xff) == 0xf0)
			break;
		udelay(10);
	}

	if (retries >= 1000) {
		dev_info(&dev_priv->pdev->dev, "Init failed! No wake signal\n");
		return -EIO;
	}

	BCWC_ISP_REG_WRITE(0xffffffff, ISP_REG_41024);

	num_channels = BCWC_ISP_REG_READ(ISP_IPC_NUM_CHAN) + 1;
	queue_size = BCWC_ISP_REG_READ(ISP_IPC_QUEUE_SIZE);

	dev_info(&dev_priv->pdev->dev,
		 "Number of IPC channels: %u, queue size: %u\n",
		 num_channels, queue_size);

	if (num_channels > 32) {
		dev_info(&dev_priv->pdev->dev, "Too many IPC channels: %u\n",
			 num_channels);
		return -EIO;
	}

	/*
	bcwc_alloc_dev_mem(queue_size, &ret, 0);
	*/

	/* Firmware must fit in 4194304 bytes */
	reg = BCWC_ISP_REG_READ(ISP_FW_HEAP_SIZE);
	if (reg > 0x400000) {
		dev_info(&dev_priv->pdev->dev,
			 "Firmware request size too big (%u bytes)\n",
			 reg);
		return -ENOMEM;
	}

	dev_info(&dev_priv->pdev->dev, "Firmware request size: %u\n", reg);

	return 0;
}


int isp_uninit(struct bcwc_private *dev_priv)
{
	return 0;
}
