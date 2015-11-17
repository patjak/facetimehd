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
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include "bcwc_drv.h"
#include "bcwc_hw.h"
#include "bcwc_isp.h"
#include "bcwc_ringbuf.h"
#include "bcwc_v4l2.h"

static int bcwc_pci_reserve_mem(struct bcwc_private *dev_priv)
{
	unsigned long start;
	unsigned long len;
	int ret;

	/* Reserve resources */
	ret = pci_request_region(dev_priv->pdev, BCWC_PCI_S2_IO, "S2 IO");
	if (ret) {
		dev_err(&dev_priv->pdev->dev, "Failed to request S2 IO\n");
		return ret;
	}

	ret = pci_request_region(dev_priv->pdev, BCWC_PCI_ISP_IO, "ISP IO");
	if (ret) {
		dev_err(&dev_priv->pdev->dev, "Failed to request ISP IO\n");
		pci_release_region(dev_priv->pdev, BCWC_PCI_S2_IO);
		return ret;
	}

	ret = pci_request_region(dev_priv->pdev, BCWC_PCI_S2_MEM, "S2 MEM");
	if (ret) {
		pci_release_region(dev_priv->pdev, BCWC_PCI_ISP_IO);
		pci_release_region(dev_priv->pdev, BCWC_PCI_S2_IO);
		return ret;
	}

	/* S2 IO */
	start = pci_resource_start(dev_priv->pdev, BCWC_PCI_S2_IO);
	len = pci_resource_len(dev_priv->pdev, BCWC_PCI_S2_IO);
	dev_priv->s2_io = ioremap_nocache(start, len);
	dev_priv->s2_io_len = len;

	/* S2 MEM */
	start = pci_resource_start(dev_priv->pdev, BCWC_PCI_S2_MEM);
	len = pci_resource_len(dev_priv->pdev, BCWC_PCI_S2_MEM);
	dev_priv->s2_mem = ioremap_nocache(start, len);
	dev_priv->s2_mem_len = len;

	/* ISP IO */
	start = pci_resource_start(dev_priv->pdev, BCWC_PCI_ISP_IO);
	len = pci_resource_len(dev_priv->pdev, BCWC_PCI_ISP_IO);
	dev_priv->isp_io = ioremap_nocache(start, len);
	dev_priv->isp_io_len = len;

	pr_debug("Allocated S2 regs (BAR %d). %u bytes at 0x%p",
		 BCWC_PCI_S2_IO, dev_priv->s2_io_len, dev_priv->s2_io);

	pr_debug("Allocated S2 mem (BAR %d). %u bytes at 0x%p",
		 BCWC_PCI_S2_MEM, dev_priv->s2_mem_len, dev_priv->s2_mem);

	pr_debug("Allocated ISP regs (BAR %d). %u bytes at 0x%p",
		 BCWC_PCI_ISP_IO, dev_priv->isp_io_len, dev_priv->isp_io);

	return 0;
}

static void bcwc_irq_disable(struct bcwc_private *dev_priv)
{
	free_irq(dev_priv->pdev->irq, dev_priv);
}

static void bcwc_handle_irq(struct bcwc_private *dev_priv, struct fw_channel *chan)
{
	struct isp_mem_obj *obj;
	struct bcwc_ringbuf_entry *entry;
	int request_size;
	u32 address;

	pr_debug("Interrupt from channel source %d, type %d [%s]\n", chan->source, chan->type, chan->name);
	bcwc_channel_ringbuf_dump(dev_priv, chan);
	if (chan == dev_priv->channel_io) {
		pr_debug("command ready\n");
		wake_up_interruptible(&dev_priv->wq);
	}

	while(bcwc_channel_ringbuf_entry_available(dev_priv, chan)) {
		entry = bcwc_channel_ringbuf_get_entry(dev_priv, chan);
		if (!entry)
			goto next;

		request_size = entry->request_size;
		address = entry->address_flags & ~ 3;

		if (chan == dev_priv->channel_shared_malloc) {
			if (address) {
				pr_debug("Firmware wants to free memory at %08x\n", address);
			} else {
				if (!request_size)
					goto next;
				obj = isp_mem_create(dev_priv, FTHD_MEM_SHAREDMALLOC, request_size);
				if (!obj)
					goto next; /* FIXME */
				pr_debug("Firmware allocated %d bytes at %08lx\n", request_size, obj->offset);
				bcwc_channel_ringbuf_send(dev_priv, chan, obj->offset, 0, 0);
			}
		} else if (chan == dev_priv->channel_terminal) {
			if (!address) {
				dev_err(&dev_priv->pdev->dev, "%s: no address\n", __FUNCTION__);
				goto next;
			}
			if (!request_size)
				goto next;
			pr_debug("FWMSG: %.*s", request_size, (char *)(dev_priv->s2_mem + address));
		}
	next:
			bcwc_channel_ringbuf_mark_entry_available(dev_priv, chan);
	}
}

static void bcwc_irq_work(struct work_struct *work)
{
	struct bcwc_private *dev_priv = container_of(work, struct bcwc_private, irq_work);
	struct fw_channel *chan;

	u32 pending;
	int i = 0;


	while(i++ < 500) {
		pending = BCWC_ISP_REG_READ(ISP_REG_41000);
		pr_debug("interrupt: %08x\n", pending);

		if (!(pending & 0xf0))
			break;

		pci_write_config_dword(dev_priv->pdev, 0x94, 0);
		BCWC_ISP_REG_WRITE(pending, ISP_REG_41024);
		pci_write_config_dword(dev_priv->pdev, 0x90, 0x200);

		/* FIXME: locking */
		for(i = 0; i < dev_priv->num_channels; i++) {
			chan = dev_priv->channels[i];


			BUG_ON(chan->source > 3);
			if (!((0x10 << chan->source) & pending))
				continue;
			bcwc_handle_irq(dev_priv, chan);
		}
	}

	if (i >= 500) {
		dev_err(&dev_priv->pdev->dev, "irq stuck, disabling\n");
//		bcwc_hw_irq_disable(dev_priv);
		bcwc_irq_disable(dev_priv);
		return;
	}

	pci_write_config_dword(dev_priv->pdev, 0x94, 0x200);
}

static irqreturn_t bcwc_irq_handler(int irq, void *arg)
{
	struct bcwc_private *dev_priv = arg;
	u32 pending;
	unsigned long flags;
	
	spin_lock_irqsave(&dev_priv->io_lock, flags);
	pending = BCWC_ISP_REG_READ(ISP_REG_41000);
	spin_unlock_irqrestore(&dev_priv->io_lock, flags);
	
	if (!(pending & 0xf0))
		return IRQ_NONE;

	schedule_work(&dev_priv->irq_work);

	return IRQ_HANDLED;
}

static int bcwc_irq_enable(struct bcwc_private *dev_priv)
{
	int ret;

	ret = request_irq(dev_priv->pdev->irq, bcwc_irq_handler, IRQF_SHARED,
			  KBUILD_MODNAME, (void *)dev_priv);

	if (ret)
		dev_err(&dev_priv->pdev->dev, "Failed to request IRQ\n");

	return ret;
}


static int bcwc_pci_set_dma_mask(struct bcwc_private *dev_priv,
				 unsigned int mask)
{
	int ret;

	ret = pci_set_dma_mask(dev_priv->pdev, DMA_BIT_MASK(mask));
	if (ret) {
		dev_err(&dev_priv->pdev->dev, "Failed to set %u pci dma mask\n",
			mask);
		return ret;
	}

	dev_priv->dma_mask = mask;

	return 0;
}

static int bcwc_pci_probe(struct pci_dev *pdev,
			  const struct pci_device_id *entry)
{
	struct bcwc_private *dev_priv;
	int ret;

	dev_info(&pdev->dev, "Found Broadcom PCIe webcam with device id: %x\n",
		 pdev->device);

	dev_priv = kzalloc(sizeof(struct bcwc_private), GFP_KERNEL);
	if (!dev_priv) {
		dev_err(&pdev->dev, "Failed to allocate memory\n");
		return -ENOMEM;
	}

	dev_priv->pdev = pdev;

	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to enable device\n");
		goto fail_free;
	}

	ret = bcwc_pci_reserve_mem(dev_priv);
	if (ret)
		goto fail_enable;

	ret = pci_enable_msi(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to enable MSI\n");
		goto fail_enable;
	}

	INIT_WORK(&dev_priv->irq_work, bcwc_irq_work);

	ret = bcwc_irq_enable(dev_priv);
	if (ret)
		goto fail_msi;

	ret = bcwc_pci_set_dma_mask(dev_priv, 64);
	if (ret)
		ret = bcwc_pci_set_dma_mask(dev_priv, 32);

	if (ret)
		goto fail_irq;

	dev_info(&pdev->dev, "Setting %ubit DMA mask\n", dev_priv->dma_mask);
	pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(dev_priv->dma_mask));

	pci_set_master(pdev);
	pci_set_drvdata(pdev, dev_priv);

	dev_priv->ddr_model = 4;
	dev_priv->ddr_speed = 450;

	if (!(ret = bcwc_hw_init(dev_priv))) {
		mdelay(100);
		bcwc_isp_cmd_start(dev_priv);
		bcwc_isp_cmd_print_enable(dev_priv, 1);
		bcwc_isp_cmd_set_loadfile(dev_priv);
		bcwc_isp_cmd_channel_info(dev_priv);
		bcwc_isp_cmd_channel_camera_config(dev_priv);
		bcwc_isp_cmd_channel_camera_config_select(dev_priv, 0, 0);
		bcwc_isp_cmd_channel_crop_set(dev_priv, 0, 0, 0, 1280, 720);
		bcwc_isp_cmd_channel_output_config_set(dev_priv, 0, 1280, 720);
		bcwc_isp_cmd_channel_recycle_mode(dev_priv, 0, 1);
		bcwc_isp_cmd_channel_recycle_start(dev_priv, 0);
		bcwc_isp_cmd_channel_drc_start(dev_priv, 0);
		bcwc_isp_cmd_channel_tone_curve_adaptation_start(dev_priv, 0);
		bcwc_isp_cmd_channel_sif_pixel_format(dev_priv, 0, 1, 1);
		bcwc_isp_cmd_channel_error_handling_config(dev_priv, 0, 2, 1);
		bcwc_isp_cmd_channel_streaming_mode(dev_priv, 0, 0);
		bcwc_isp_cmd_channel_frame_rate_max(dev_priv, 0, 7672);
		bcwc_isp_cmd_channel_frame_rate_min(dev_priv, 0, 3072);
		bcwc_isp_cmd_channel_start(dev_priv);
		return 0;
	}

	bcwc_hw_deinit(dev_priv);
	isp_mem_destroy(dev_priv->firmware);
	pci_release_region(pdev, BCWC_PCI_S2_IO);
	pci_release_region(pdev, BCWC_PCI_S2_MEM);
	pci_release_region(pdev, BCWC_PCI_ISP_IO);

fail_irq:
	bcwc_irq_disable(dev_priv);
fail_msi:
	pci_disable_msi(pdev);
fail_enable:
	pci_disable_device(pdev);
fail_free:
	kfree(dev_priv);
	return ret;
}

static void bcwc_pci_remove(struct pci_dev *pdev)
{
	struct bcwc_private *dev_priv;

	dev_priv = pci_get_drvdata(pdev);
	if (dev_priv) {
		bcwc_isp_cmd_stop(dev_priv);
		isp_powerdown(dev_priv);
		bcwc_irq_disable(dev_priv);
		cancel_work_sync(&dev_priv->irq_work);
		isp_uninit(dev_priv);
		bcwc_hw_deinit(dev_priv);

		isp_mem_destroy(dev_priv->firmware);
		bcwc_buffer_exit(dev_priv);
		pci_disable_msi(pdev);

		if (dev_priv->s2_io)
			iounmap(dev_priv->s2_io);
		if (dev_priv->s2_mem)
			iounmap(dev_priv->s2_mem);
		if (dev_priv->isp_io)
			iounmap(dev_priv->isp_io);

		pci_release_region(pdev, BCWC_PCI_S2_IO);
		pci_release_region(pdev, BCWC_PCI_S2_MEM);
		pci_release_region(pdev, BCWC_PCI_ISP_IO);
	}

	pci_disable_device(pdev);
}

#ifdef CONFIG_PM
static int bcwc_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	return 0;
}

static int bcwc_pci_resume(struct pci_dev *pdev)
{
	return 0;
}
#endif /* CONFIG_PM */

static const struct pci_device_id bcwc_pci_id_table[] = {
	{ PCI_VDEVICE(BROADCOM, 0x1570), 4 },
	{ 0, },
};

static struct pci_driver bcwc_pci_driver = {
	.name = KBUILD_MODNAME,
	.probe = bcwc_pci_probe,
	.remove = bcwc_pci_remove,
	.id_table = bcwc_pci_id_table,
#ifdef CONFIG_PM
	.suspend = bcwc_pci_suspend,
	.resume = bcwc_pci_resume,
#endif
};

static int __init bcwc_init(void)
{
	int ret = 0;

	ret = pci_register_driver(&bcwc_pci_driver);

	if (ret)
		pr_err("Couldn't find any devices (ret=%d)\n", ret);

	return ret;
}

static void __exit bcwc_exit(void)
{
	pci_unregister_driver(&bcwc_pci_driver);
}

module_init(bcwc_init);
module_exit(bcwc_exit);

MODULE_AUTHOR("Patrik Jakobsson <patrik.r.jakobsson@gmail.com>");
MODULE_DESCRIPTION("Broadcom PCIe 1570 webcam driver");
MODULE_LICENSE("GPL");
