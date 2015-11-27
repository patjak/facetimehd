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
#ifndef FTHD_BUFFER_H
#define FTHD_BUFFER_H

#include <linux/scatterlist.h>
#include "fthd_buffer.h"

enum fthd_buffer_state {
	BUF_FREE,
	BUF_ALLOC,
	BUF_DRV_QUEUED,
	BUF_HW_QUEUED,
};

struct dma_descriptor {
	u32 addr0;
	u32 addr1;
	u32 addr2;
	u32 field_c;
	u32 field_10;
	u32 field_14;
	u32 count;
	u32 pool;
	u64 tag;
} __attribute__((packed));

struct dma_descriptor_list {
	u32 field0;
    	u32 count;
	struct dma_descriptor desc[4];
} __attribute__((packed));

struct iommu_obj {
	struct resource base;
	int size;
	int offset;
};

struct fthd_plane {
	u8 *virt;
	u64 phys;
	dma_addr_t dma;	
	int len;
	struct iommu_obj *iommu;
};

struct h2t_buf_ctx {
	enum fthd_buffer_state state;
	struct vb2_buffer *vb;
	struct iommu_obj *plane[4];
	struct isp_mem_obj *dma_desc_obj;
	struct dma_descriptor_list *dma_desc_list;	
	/* waitqueue for signaling buffer completion */
	wait_queue_head_t wq;
	int done;
};

extern int setup_buffers(struct fthd_private *dev_priv);
extern int fthd_buffer_init(struct fthd_private *dev_priv);
extern void fthd_buffer_exit(struct fthd_private *dev_priv);
extern void fthd_buffer_return_handler(struct fthd_private *dev_priv, struct dma_descriptor_list *list, int size);
extern void fthd_buffer_queued_handler(struct fthd_private *dev_priv, struct dma_descriptor_list *list);
extern struct iommu_obj *iommu_allocate_sgtable(struct fthd_private *dev_priv, struct sg_table *);
extern void iommu_free(struct fthd_private *dev_priv, struct iommu_obj *obj);
#endif
