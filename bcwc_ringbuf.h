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

#ifndef _BCWC_RINGBUF_H
#define _BCWC_RINGBUF_H

enum ringbuf_type_t {
	RINGBUF_TYPE_H2T=0,
	RINGBUF_TYPE_T2H=1,
	RINGBUF_TYPE_UNIDIRECTIONAL,
};

struct bcwc_ringbuf {
	void *doorbell;
	u32 phys_offset;
	int idx;
        u8 *virt_addr;
};

struct bcwc_ringbuf_entry {
	u32 address_flags;
	u32 request_size;
	u32 response_size;
	u32 __unused[13];
} __attribute__((packed));

struct fw_channel;
struct bcwc_private;
extern void bcwc_channel_ringbuf_dump(struct bcwc_private *dev_priv, struct fw_channel *chan);
extern void bcwc_channel_ringbuf_init(struct bcwc_private *dev_priv, struct fw_channel *chan);
extern struct bcwc_ringbuf_entry *bcwc_channel_ringbuf_get_entry(struct bcwc_private *, struct fw_channel *);
extern struct bcwc_ringbuf_entry *bcwc_channel_ringbuf_send(struct bcwc_private *dev_priv, struct fw_channel *chan,
				     u32 data_offset, u32 request_size, u32 response_size);

extern void bcwc_channel_ringbuf_mark_entry_available(struct bcwc_private *dev_priv,
						      struct fw_channel *chan);
extern int bcwc_channel_ringbuf_entry_available(struct bcwc_private *dev_priv,
						struct fw_channel *chan);

#endif
