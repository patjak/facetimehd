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
	dev_info(&dev_priv->pdev->dev, "%s: dumping %d [%s]\n", __FUNCTION__, chan->type, chan->name);
	for( i = 0; i < chan->size; i++) {
		if (chan->ringbuf.send_idx == i && chan->ringbuf.recv_idx == i)
			pos = '*';
		else if (chan->ringbuf.send_idx == i)
			pos = 'S';
		else if (chan->ringbuf.recv_idx == i)
			pos = 'R';
		else
			pos = ' ';
	    entry = dev_priv->s2_mem + chan->offset + i * sizeof(struct bcwc_ringbuf_entry);
	    dev_info(&dev_priv->pdev->dev, "%c%3.3d: ADDRESS %08x REQUEST_SIZE %08x RESPONSE_SIZE %08x\n", pos, i, entry->address_flags,
			 entry->request_size, entry->response_size);

	}
}

void bcwc_channel_ringbuf_init(struct bcwc_private *dev_priv, struct fw_channel *chan)
{
	struct bcwc_ringbuf_entry *entry;
	int i;

	chan->ringbuf.recv_idx = 0;
	chan->ringbuf.send_idx = 0;
	chan->ringbuf.sent_cnt = 0;
	chan->ringbuf.recv_cnt = 0;
	chan->ringbuf.phys_offset = chan->offset;
	chan->ringbuf.virt_addr = dev_priv->s2_mem + chan->offset;

	if (chan->type == RINGBUF_TYPE_H2T) {
		entry = (struct bcwc_ringbuf_entry *)chan->ringbuf.virt_addr;
		dev_info(&dev_priv->pdev->dev, "clearing ringbuf %s at %p (size %d)\n", chan->name, entry, chan->size);
		for(i = 0; i < chan->size; i++) {
			entry->address_flags = 1;
			entry->request_size = 0;
			entry->response_size = 0;
			entry++;
		}
	}
}

int bcwc_channel_ringbuf_send(struct bcwc_private *dev_priv, struct fw_channel *chan,
			      u32 data_offset, u32 request_size, u32 response_size)
{
	struct bcwc_ringbuf_entry *entry;
	u32 val;

	entry = get_entry_addr(dev_priv, chan, chan->ringbuf.send_idx++);
	dev_info(&dev_priv->pdev->dev, "%s: entry %p offset %08x\n", __FUNCTION__, entry, data_offset);
	entry->address_flags = data_offset | (chan->type == 0 ? 0 : 1);
	entry->request_size = request_size;
	entry->response_size = response_size;
	dev_info(&dev_priv->pdev->dev, "%s: address_flags %x, request size %x response size %x\n", __FUNCTION__,
		 entry->address_flags, entry->request_size, entry->response_size);
	wmb();
	val = 0x10 << chan->source;
	dev_info(&dev_priv->pdev->dev, "%s: val %08x\n", __FUNCTION__, val);
	BCWC_ISP_REG_WRITE(val, ISP_REG_41020);
	return 0;
}

struct bcwc_ringbuf_entry *bcwc_channel_ringbuf_get_entry(struct bcwc_private *dev_priv,
							struct fw_channel *chan)
{
	struct bcwc_ringbuf_entry *entry;

	entry = get_entry_addr(dev_priv, chan, chan->ringbuf.recv_idx);
	if (chan->ringbuf.recv_idx > chan->size)
		chan->ringbuf.recv_idx = 0;
	return entry;
}

void bcwc_channel_ringbuf_mark_entry_available(struct bcwc_private *dev_priv,
					       struct fw_channel *chan)
{
	struct bcwc_ringbuf_entry *entry;

	entry = get_entry_addr(dev_priv, chan, chan->ringbuf.recv_idx++);
	entry->address_flags = 1;
	entry->request_size = 0;
	entry->response_size = 0;
}

int bcwc_channel_ringbuf_entry_available(struct bcwc_private *dev_priv,
					 struct fw_channel *chan)
{
	struct bcwc_ringbuf_entry *entry;

	entry = get_entry_addr(dev_priv, chan, chan->ringbuf.recv_idx);	
	return (!(entry->address_flags & 1));
}

