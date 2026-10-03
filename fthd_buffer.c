/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * FacetimeHD camera driver
 *
 * Copyright (C) 2015 Sven Schnelle <svens@stackframe.org>
 *
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/printk.h>
#include <linux/scatterlist.h>
#include "fthd_drv.h"
#include "fthd_isp.h"
#include "fthd_hw.h"
#include "fthd_buffer.h"

#define GET_IOMMU_PAGES(_x) (((_x) + 4095)/4096)

struct buf_ctx {
	struct fthd_plane plane[4];
	struct isp_mem_obj *isphdr;
};

static int iommu_allocator_init(struct fthd_private *dev_priv)
{
        dev_priv->iommu = kzalloc(sizeof(struct resource), GFP_KERNEL);
	if (!dev_priv->iommu)
	    return -ENOMEM;

	dev_priv->iommu->start = 0;
	dev_priv->iommu->end = 4095;
	return 0;
}

struct iommu_obj *iommu_allocate_sgtable(struct fthd_private *dev_priv, struct sg_table *sgtable)
{
	struct iommu_obj *obj;
	struct resource *root = dev_priv->iommu;
	struct scatterlist *sg;
	int ret, i, pos;
	int total_len = 0;
	dma_addr_t dma_addr, dma_end, page;

	/*
	 * Large buffers use chained scatterlists, so the entries must be
	 * walked with for_each_sg() and not by indexing sgtable->sgl.
	 *
	 * The S2 IOMMU maps whole pages. The firmware accepts a byte address
	 * though, so a buffer may start inside its first page (USERPTR
	 * buffers from PipeWire start 0x40-0x100 bytes into a page): all
	 * pages it touches are mapped and the offset is kept in byte_offset.
	 * A gap inside the buffer can't be expressed, so only the start of
	 * the first segment and the end of the last one may be unaligned.
	 */
	for_each_sg(sgtable->sgl, sg, sgtable->nents, i) {
		dma_addr = sg_dma_address(sg);
		dma_end = dma_addr + sg_dma_len(sg);

		if ((i > 0 && (dma_addr & 0xfff)) ||
		    (i < sgtable->nents - 1 && (dma_end & 0xfff))) {
			dev_err(&dev_priv->pdev->dev,
				"Buffer segment %d is not page aligned (%pad, len %u), refusing\n",
				i, &dma_addr, sg_dma_len(sg));
			return NULL;
		}
		total_len += GET_IOMMU_PAGES(dma_end) - (dma_addr >> 12);
	}

	if (!total_len)
		return NULL;

	obj = kzalloc(sizeof(struct iommu_obj), GFP_KERNEL);
	if (!obj)
		return NULL;

	obj->base.name = "S2 IOMMU";
	ret = allocate_resource(root, &obj->base, total_len, root->start, root->end,
				1, NULL, NULL);
	if (ret) {
		dev_err(&dev_priv->pdev->dev,
			"Failed to allocate resource (size: %d, start: %Ld, end: %Ld)\n",
			total_len, root->start, root->end);
		kfree(obj);
		obj = NULL;
		return NULL;
	}

	obj->offset = obj->base.start - root->start;
	obj->size = total_len;
	obj->byte_offset = sg_dma_address(sgtable->sgl) & 0xfff;

	pos = 0x9000 + obj->offset * 4;
	for_each_sg(sgtable->sgl, sg, sgtable->nents, i) {
		dma_addr = sg_dma_address(sg);
		dma_end = dma_addr + sg_dma_len(sg);

		for(page = dma_addr >> 12; page < GET_IOMMU_PAGES(dma_end); page++) {
			FTHD_S2_REG_WRITE(page, pos);
			pos += 4;
		}
	}

	pr_debug("allocated %d pages @ %p / offset %d\n", obj->size, obj, obj->offset);
	return obj;
}

void iommu_free(struct fthd_private *dev_priv, struct iommu_obj *obj)
{
	int i;
	pr_debug("freeing %p\n", obj);

	if (!obj)
		return;
	
 	for (i = obj->offset; i < obj->offset + obj->size; i++)
		FTHD_S2_REG_WRITE(0, 0x9000 + i * 4);

	release_resource(&obj->base);
	kfree(obj);
	obj = NULL;
}

static void iommu_allocator_destroy(struct fthd_private *dev_priv)
{
	kfree(dev_priv->iommu);
}

int fthd_buffer_init(struct fthd_private *dev_priv)
{
	int i;
	for(i = 0; i < 0x1000; i++)
		FTHD_S2_REG_WRITE(0, 0x9000 + i * 4);

	return iommu_allocator_init(dev_priv);
}

void fthd_buffer_exit(struct fthd_private *dev_priv)
{
	iommu_allocator_destroy(dev_priv);
}
