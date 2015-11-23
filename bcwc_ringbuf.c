/*
 * Broadcom PCIe 1570 webcam driver
 *
 * Copyright (C) 2015 Sven Schnelle <svens@stackframe.org>
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/delay.h>
#include "bcwc_drv.h"
#include "bcwc_hw.h"
#include "bcwc_ringbuf.h"
#include "bcwc_isp.h"

static struct bcwc_ringbuf_entry *get_entry_addr(struct bcwc_private *dev_priv,
					       struct fw_channel *chan, int num)
{
	return (struct bcwc_ringbuf_entry *)(chan->ringbuf.virt_addr \
					     + num * sizeof(struct bcwc_ringbuf_entry));
}

void bcwc_channel_ringbuf_dump(struct bcwc_private *dev_priv, struct fw_channel *chan)
{
	struct bcwc_ringbuf_entry *entry;
	char pos;
	int i;

	for( i = 0; i < chan->size; i++) {
		if (chan->ringbuf.idx == i)
			pos = '*';
		else
			pos = ' ';
	    entry = dev_priv->s2_mem + chan->offset + i * sizeof(struct bcwc_ringbuf_entry);
	    pr_debug("%s: %c%3.3d: ADDRESS %08x REQUEST_SIZE %08x RESPONSE_SIZE %08x\n", chan->name,
		     pos, i, entry->address_flags, entry->request_size, entry->response_size);

	}
}

void bcwc_channel_ringbuf_init(struct bcwc_private *dev_priv, struct fw_channel *chan)
{
	struct bcwc_ringbuf_entry *entry;
	int i;

	chan->ringbuf.idx = 0;
	chan->ringbuf.phys_offset = chan->offset;
	chan->ringbuf.virt_addr = dev_priv->s2_mem + chan->offset;

	if (chan->type == RINGBUF_TYPE_H2T) {
		entry = (struct bcwc_ringbuf_entry *)chan->ringbuf.virt_addr;
		pr_debug("clearing ringbuf %s at %p (size %d)\n", chan->name, entry, chan->size);

		spin_lock_irq(&chan->lock);
		for(i = 0; i < chan->size; i++) {
			entry->address_flags = 1;
			entry->request_size = 0;
			entry->response_size = 0;
			entry++;
		}
		spin_unlock_irq(&chan->lock);
	}
}

struct bcwc_ringbuf_entry *bcwc_channel_ringbuf_send(struct bcwc_private *dev_priv, struct fw_channel *chan,
			      u32 data_offset, u32 request_size, u32 response_size)
{
	struct bcwc_ringbuf_entry *entry;

	pr_debug("send %08x\n", data_offset);

	spin_lock_irq(&chan->lock);
	entry = get_entry_addr(dev_priv, chan, chan->ringbuf.idx);

	if (chan->tx_lock) {
		spin_unlock_irq(&chan->lock);
		return NULL;
	}

	if (chan->type != FW_CHAN_TYPE_OUT && ++chan->ringbuf.idx >= chan->size)
		chan->ringbuf.idx = 0;

	chan->tx_lock = 1;
	chan->rx_lock = 0;

	entry->request_size = request_size;
	entry->response_size = response_size;
	wmb();
	entry->address_flags = data_offset | (chan->type == 0 ? 0 : 1);
	spin_unlock_irq(&chan->lock);

	spin_lock_irq(&dev_priv->io_lock);
	BCWC_ISP_REG_WRITE(0x10 << chan->source, ISP_REG_41020);
	spin_unlock_irq(&dev_priv->io_lock);
	return entry;
}

struct bcwc_ringbuf_entry *bcwc_channel_ringbuf_receive(struct bcwc_private *dev_priv,
							struct fw_channel *chan)
{
	struct bcwc_ringbuf_entry *entry, *ret = NULL;

	spin_lock_irq(&chan->lock);
	if (chan->rx_lock)
		goto out;

	entry = get_entry_addr(dev_priv, chan, chan->ringbuf.idx);

	if (!(entry->address_flags & 1) ^ (chan->type != 0))
		goto out;

	ret = entry;

	if (chan->type == FW_CHAN_TYPE_OUT && ++chan->ringbuf.idx >= chan->size)
		chan->ringbuf.idx = 0;

	chan->rx_lock = 1;
	chan->tx_lock = 0;
out:
	spin_unlock_irq(&chan->lock);
	return ret;
}
