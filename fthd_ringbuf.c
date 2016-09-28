/*
 * FacetimeHD camera driver
 *
 * Copyright (C) 2015 Sven Schnelle <svens@stackframe.org>
 *		 2016 Patrik Jakobsson <patrik.r.jakobsson@gmail.com>
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
#include "fthd_drv.h"
#include "fthd_hw.h"
#include "fthd_ringbuf.h"
#include "fthd_isp.h"

void fthd_channel_ringbuf_dump(struct fthd_private *dev_priv, struct fw_channel *chan)
{
	u32 entry;
	char pos;
	int i;

	for (i = 0; i < chan->size; i++) {
		if (chan->ringbuf.idx == i)
			pos = '*';
		else
			pos = ' ';
		entry = FTHD_ENTRY_ADDR(chan, i);
		pr_debug("%s: %c%3.3d: ADDRESS %08x REQUEST_SIZE %08x RESPONSE_SIZE %08x\n",
			 chan->name, pos, i,
			 FTHD_S2_MEM_READ(entry + FTHD_RINGBUF_ADDRESS_FLAGS),
			 FTHD_S2_MEM_READ(entry + FTHD_RINGBUF_REQUEST_SIZE),
			 FTHD_S2_MEM_READ(entry + FTHD_RINGBUF_RESPONSE_SIZE));
	}
}

void fthd_channel_ringbuf_init(struct fthd_private *dev_priv,
			       struct fw_channel *chan)
{
	u32 entry;
	int i;

	chan->ringbuf.idx = 0;

	if (chan->type == RINGBUF_TYPE_H2T) {
		pr_debug("clearing ringbuf %s at %08x (size %d)\n",
			 chan->name, chan->offset, chan->size);

		spin_lock_irq(&chan->lock);
		for (i = 0; i < chan->size; i++) {
			entry = FTHD_ENTRY_ADDR(chan, i);
			FTHD_S2_MEM_WRITE(1, entry + FTHD_RINGBUF_ADDRESS_FLAGS);
			FTHD_S2_MEM_WRITE(0, entry + FTHD_RINGBUF_REQUEST_SIZE);
			FTHD_S2_MEM_WRITE(0, entry + FTHD_RINGBUF_RESPONSE_SIZE);
			entry += FTHD_RINGBUF_ENTRY_SIZE;
		}
		spin_unlock_irq(&chan->lock);
	}
}

int fthd_channel_ringbuf_send(struct fthd_private *dev_priv,
			      struct fw_channel *chan, u32 data_offset,
			      u32 request_size, u32 response_size, u32 *entryp)
{
	u32 entry;

	spin_lock_irq(&chan->lock);
	entry = FTHD_ENTRY_ADDR(chan, chan->ringbuf.idx);

	if (++chan->ringbuf.idx >= chan->size)
		chan->ringbuf.idx = 0;

	if (!(FTHD_S2_MEM_READ(entry + FTHD_RINGBUF_ADDRESS_FLAGS) & 1) ^
	    (chan->type != RINGBUF_TYPE_H2T)) {
		spin_unlock_irq(&chan->lock);
		return -EAGAIN;
	}

	FTHD_S2_MEM_WRITE(request_size, entry + FTHD_RINGBUF_REQUEST_SIZE);
	FTHD_S2_MEM_WRITE(response_size, entry + FTHD_RINGBUF_RESPONSE_SIZE);
	wmb();
	FTHD_S2_MEM_WRITE(data_offset | (chan->type == 0 ? 0 : 1),
			  entry + FTHD_RINGBUF_ADDRESS_FLAGS);
	spin_unlock_irq(&chan->lock);

	spin_lock_irq(&dev_priv->io_lock);
	FTHD_ISP_REG_WRITE(0x10 << chan->source, ISP_REG_41020);
	spin_unlock_irq(&dev_priv->io_lock);
	if (entryp)
		*entryp = entry;

	return 0;
}

u32 fthd_channel_ringbuf_receive(struct fthd_private *dev_priv,
				 struct fw_channel *chan)
{
	u32 entry, ret = (u32)-1;

	spin_lock_irq(&chan->lock);

	entry = FTHD_ENTRY_ADDR(chan, chan->ringbuf.idx);

	if (!(FTHD_S2_MEM_READ(entry + FTHD_RINGBUF_ADDRESS_FLAGS) & 1) ^
	    (chan->type != RINGBUF_TYPE_H2T))
		goto out;

	ret = entry;

	if (chan->type == FW_CHAN_TYPE_OUT && ++chan->ringbuf.idx >= chan->size)
		chan->ringbuf.idx = 0;

out:
	spin_unlock_irq(&chan->lock);
	return ret;
}

int fthd_channel_wait_ready(struct fthd_private *dev_priv, struct fw_channel *chan, u32 entry, int timeout)
{
	if (wait_event_interruptible_timeout(chan->wq,
			(FTHD_S2_MEM_READ(entry + FTHD_RINGBUF_ADDRESS_FLAGS) & 1) ^
			(chan->type != RINGBUF_TYPE_H2T), msecs_to_jiffies(timeout)) <= 0) {
		pr_err("%s: timeout\n", chan->name);
		fthd_channel_ringbuf_dump(dev_priv, chan);

		return -ETIMEDOUT;
	}
	return 0;
}
