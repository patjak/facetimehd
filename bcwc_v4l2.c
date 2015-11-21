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
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/videodev2.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-dma-sg.h>
#include "bcwc_drv.h"
#include "bcwc_hw.h"
#include "bcwc_isp.h"
#include "bcwc_ringbuf.h"
#include "bcwc_buffer.h"

#define BCWC_FMT(_desc, _x, _y, _sizeimage, _planes, _pixfmt, _range, _x1, _y1, _x2, _y2) \
	{								\
		.fmt.width = (_x),					\
		.fmt.height = (_y),					\
		.fmt.sizeimage = (_sizeimage),				\
		.fmt.pixelformat = (_pixfmt),				\
		.planes = (_planes),					\
		.desc = (_desc),					\
		.range = (_range),					\
		.x1 = (_x1),						\
		.y1 = (_y1),						\
		.x2 = (_x2),						\
		.y2 = (_y2)						\
	}

struct bcwc_fmt bcwc_formats[] = {
	BCWC_FMT("1280x720 YUYV (4:2:2)", 1280, 720, 1280 * 720 * 2, 1, V4L2_PIX_FMT_YUYV, 0, 0, 0, 1280, 720),
	BCWC_FMT("1280x720 YVYU (4:2:2)", 1280, 720, 1280 * 720 * 2, 1, V4L2_PIX_FMT_YVYU, 0, 0, 0, 1280, 720),
	BCWC_FMT("1280x720 NV16", 1280, 720, 1280 * 720, 2, V4L2_PIX_FMT_NV16, 0, 0, 0, 1280, 720),
	BCWC_FMT("640x480 YUYV (4:2:2)", 640,  480, 640 * 480 * 2, 1, V4L2_PIX_FMT_YUYV, 0, 160, 0, 960, 720),
	BCWC_FMT("640x480 YVYU (4:2:2)", 640,  480, 640 * 480 * 2, 1, V4L2_PIX_FMT_YVYU, 0, 160, 0, 960, 720),
	BCWC_FMT("640x480 NV16", 640,  480, 640 * 480, 2, V4L2_PIX_FMT_NV16, 0, 160, 0, 960, 720),
	BCWC_FMT("320x240 YUYV (4:2:2)", 320,  240, 320 * 240 * 2, 1, V4L2_PIX_FMT_YUYV, 0, 160, 0, 960, 720),
	BCWC_FMT("320x240 YVYU (4:2:2)", 320,  240, 320 * 240 * 2, 1, V4L2_PIX_FMT_YVYU, 0, 160, 0, 960, 720),
	BCWC_FMT("320x240 NV16", 320,  240, 320 * 240, 2, V4L2_PIX_FMT_NV16, 0, 160, 0, 960, 720),
};

static int bcwc_buffer_queue_setup(struct vb2_queue *vq,
				   const struct v4l2_format *fmt,
				   unsigned int *nbuffers, unsigned int *nplanes,
				   unsigned int sizes[], void *alloc_ctxs[])
{
	struct bcwc_private *dev_priv = vb2_get_drv_priv(vq);
	int i;
	pr_debug("%s: nbuffers %d, nplanes %d, sizes[0]: %d\n", __FUNCTION__,
		 *nbuffers, *nplanes, sizes[0]);

	if (!dev_priv->fmt)
	  return -EINVAL;

	*nplanes = dev_priv->fmt->planes;
	for(i = 0; i < *nplanes; i++) {
		sizes[i] = dev_priv->fmt->fmt.sizeimage;
		alloc_ctxs[i] = dev_priv->alloc_ctx;
	}
	*nbuffers = BCWC_BUFFERS;
	return 0;
}

static void bcwc_buffer_cleanup(struct vb2_buffer *vb)
{
	struct bcwc_private *dev_priv = vb2_get_drv_priv(vb->vb2_queue);
	struct h2t_buf_ctx *ctx = NULL;
	int i;

	pr_debug("%s: vb %p\n", __FUNCTION__, vb);
	for(i = 0; i < BCWC_BUFFERS; i++) {
		if (dev_priv->h2t_bufs[i].vb == vb) {
			ctx = dev_priv->h2t_bufs + i;
			break;
		};
	}
	if (!ctx) {
		pr_err("buffer not found\n");
		return;
	}

	if (ctx->state == BUF_FREE)
		return;

	ctx->state = BUF_FREE;
	ctx->vb = NULL;
	isp_mem_destroy(ctx->dma_desc_obj);
	for(i = 0; i < dev_priv->fmt->planes; i++) {
		iommu_free(dev_priv, ctx->plane[i]);
		ctx->plane[i] = NULL;
	}
	ctx->dma_desc_obj = NULL;
}

static void bcwc_buffer_queue(struct vb2_buffer *vb)
{
	struct bcwc_private *dev_priv = vb2_get_drv_priv(vb->vb2_queue);
	struct dma_descriptor_list *list;
	struct h2t_buf_ctx *ctx = NULL;

	int i;

	pr_debug("%s: vb %p\n", __FUNCTION__, vb);
	for(i = 0; i < BCWC_BUFFERS; i++) {
		if (dev_priv->h2t_bufs[i].vb == vb) {
			ctx = dev_priv->h2t_bufs + i;
			break;
		};
	}

	if (!ctx) {
		pr_err("buffer not found\n");
		return;
	}

	if (ctx->state != BUF_ALLOC) {
		pr_err("buffer busy\n");
		return;
	}

	if (!vb->vb2_queue->streaming) {
		pr_debug("not streaming\n");
		ctx->state = BUF_DRV_QUEUED;
	} else {
		list = ctx->dma_desc_list;
		list->field0 = 1;
		ctx->state = BUF_HW_QUEUED;
		wmb();
		pr_debug("%d: field0: %d, count %d, pool %d, addr0 0x%08x, addr1 0x%08x tag 0x%08llx vb = %p\n", i, list->field0,
			 list->desc[i].count, list->desc[i].pool, list->desc[i].addr0, list->desc[i].addr1, list->desc[i].tag, ctx->vb);

		bcwc_channel_ringbuf_send(dev_priv, dev_priv->channel_buf_h2t,
					  ctx->dma_desc_obj->offset, 0x180, 0x30000000);
	}

}

static int bcwc_buffer_prepare(struct vb2_buffer *vb)
{
	struct bcwc_private *dev_priv = vb2_get_drv_priv(vb->vb2_queue);
	struct sg_table *sgtable;
	struct h2t_buf_ctx *ctx = NULL;
	struct dma_descriptor_list *dma_list;
	int i;

	pr_debug("vb = %p\n", vb);

	for(i = 0; i < BCWC_BUFFERS; i++) {
		if (dev_priv->h2t_bufs[i].state == BUF_FREE ||
		    (dev_priv->h2t_bufs[i].state == BUF_ALLOC && dev_priv->h2t_bufs[i].vb == vb)) {
			ctx = dev_priv->h2t_bufs + i;
			break;
		}
	}

	if (!ctx)
		return -ENOBUFS;

	if (ctx->state == BUF_FREE) {
		pr_debug("allocating new entry\n");
		ctx->dma_desc_obj = isp_mem_create(dev_priv, FTHD_MEM_BUFFER, 0x180);
		if (!ctx->dma_desc_obj)
			return -ENOMEM;

		ctx->dma_desc_list = dev_priv->s2_mem + ctx->dma_desc_obj->offset;
		ctx->vb = vb;
		ctx->state = BUF_ALLOC;

		for(i = 0; i < dev_priv->fmt->planes; i++) {
		  sgtable = vb2_dma_sg_plane_desc(vb, i);
		  ctx->plane[i] = iommu_allocate_sgtable(dev_priv, sgtable);
		}
	}

	vb2_set_plane_payload(vb, 0, dev_priv->fmt->fmt.sizeimage);

	dma_list = ctx->dma_desc_list;
	memset(dma_list, 0, 0x180);

	dma_list->field0 = 1;
	dma_list->count = 1;
	dma_list->desc[0].count = 1;
	dma_list->desc[0].pool = 0x02;
	dma_list->desc[0].addr0 = (ctx->plane[0]->offset << 12) | 0xc0000000;

	if (dev_priv->fmt->planes >= 2)
		dma_list->desc[0].addr1 = (ctx->plane[1]->offset << 12) | 0xc0000000;
	if (dev_priv->fmt->planes >= 3)
		dma_list->desc[0].addr2 = (ctx->plane[2]->offset << 12) | 0xc0000000;

	dma_list->desc[0].tag = (u64)ctx;
	return 0;
}

void bcwc_buffer_return_handler(struct bcwc_private *dev_priv, struct dma_descriptor_list *list, int size)
{
	struct h2t_buf_ctx *ctx;
	int i;

	for(i = 0; i < list->count; i++) {
		ctx = (struct h2t_buf_ctx *)list->desc[i].tag;
		pr_debug("%d: field0: %d, count %d, pool %d, addr0 0x%08x, addr1 0x%08x tag 0x%08llx vb = %p\n", i, list->field0,
			 list->desc[i].count, list->desc[i].pool, list->desc[i].addr0, list->desc[i].addr1, list->desc[i].tag, ctx->vb);

		if (ctx->state == BUF_HW_QUEUED || ctx->state == BUF_DRV_QUEUED) {
			ctx->state = BUF_ALLOC;
			vb2_buffer_done(ctx->vb, VB2_BUF_STATE_DONE);
		}

	}
}

static int bcwc_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct bcwc_private *dev_priv = vb2_get_drv_priv(vq);
	struct h2t_buf_ctx *ctx;
	int i, pixelformat;

	pr_debug("%s: %d\n", __FUNCTION__, count);

	bcwc_isp_cmd_channel_crop_set(dev_priv, 0,
				      dev_priv->fmt->x1,
				      dev_priv->fmt->y1,
				      dev_priv->fmt->x2,
				      dev_priv->fmt->y2);

	switch(dev_priv->fmt->fmt.pixelformat) {
	case V4L2_PIX_FMT_YUYV:
		pixelformat = 1;
		break;
	case V4L2_PIX_FMT_YVYU:
		pixelformat = 2;
		break;
	case V4L2_PIX_FMT_NV16:
		pixelformat = 0;
		break;
	default:
		pixelformat = 1;
		WARN_ON(1);
	}
	bcwc_isp_cmd_channel_output_config_set(dev_priv, 0,
					       dev_priv->fmt->fmt.width,
					       dev_priv->fmt->fmt.height,
					       pixelformat);

	bcwc_isp_cmd_channel_start(dev_priv);
	mdelay(1000); /* Needed to settle AE */
	for(i = 0; i < BCWC_BUFFERS && count; i++, count--) {
		ctx = dev_priv->h2t_bufs + i;
		if (ctx->state != BUF_DRV_QUEUED)
			continue;

		ctx->state = BUF_HW_QUEUED;
		bcwc_channel_ringbuf_send(dev_priv, dev_priv->channel_buf_h2t,
					  ctx->dma_desc_obj->offset, 0x180, 0x30000000);
	}

	return 0;
}

static void bcwc_stop_streaming(struct vb2_queue *vq)
{
	struct bcwc_private *dev_priv = vb2_get_drv_priv(vq);

	pr_debug("%s\n", __FUNCTION__);

	bcwc_isp_cmd_channel_buffer_return(dev_priv, 0);
	pr_debug("waiting for buffers...\n");
	vb2_wait_for_all_buffers(vq);
	pr_debug("done\n");
	bcwc_isp_cmd_channel_stop(dev_priv);
}

static struct vb2_ops vb2_queue_ops = {
	.queue_setup            = bcwc_buffer_queue_setup,
	.buf_prepare            = bcwc_buffer_prepare,
	.buf_cleanup            = bcwc_buffer_cleanup,
	.start_streaming        = bcwc_start_streaming,
	.stop_streaming         = bcwc_stop_streaming,
	.buf_queue              = bcwc_buffer_queue,
	.wait_prepare           = vb2_ops_wait_prepare,
	.wait_finish            = vb2_ops_wait_finish,
};

static struct v4l2_file_operations bcwc_vdev_fops = {
	.owner          = THIS_MODULE,
	.open           = v4l2_fh_open,

	.release        = vb2_fop_release,
	.poll           = vb2_fop_poll,
	.mmap           = vb2_fop_mmap,
	.unlocked_ioctl = video_ioctl2
};

static int bcwc_v4l2_ioctl_enum_input(struct file *filp, void *priv,
				      struct v4l2_input *input)
{
	if (input->index != 0)
		return -EINVAL;

	input->type = V4L2_INPUT_TYPE_CAMERA;
	input->std = V4L2_STD_ALL;
	strcpy(input->name, "Apple Facetime");
	return 0;
}

static int bcwc_v4l2_ioctl_g_input(struct file *filp, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int bcwc_v4l2_ioctl_s_input(struct file *filp, void *priv, unsigned int i)
{
	if (i != 0)
		return -EINVAL;
	return 0;
}

static int bcwc_v4l2_ioctl_s_std(struct file *filp, void *priv, v4l2_std_id std)
{
	return 0;
}

static int bcwc_v4l2_ioctl_g_std(struct file *filp, void *priv, v4l2_std_id *std)
{
	*std = V4L2_STD_NTSC_M;
	return 0;
}

static int bcwc_v4l2_ioctl_querycap(struct file *filp, void *priv,
				    struct v4l2_capability *cap)
{
	strcpy(cap->driver, "bcwc");
	strcpy(cap->card, "facetimehd");
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE |
		V4L2_CAP_READWRITE | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int bcwc_v4l2_ioctl_enum_fmt_vid_cap(struct file *filp, void *priv,
				   struct v4l2_fmtdesc *fmt)
{
	if (fmt->index >= ARRAY_SIZE(bcwc_formats))
		return -EINVAL;
	strlcpy(fmt->description, bcwc_formats[fmt->index].desc,
		sizeof(fmt->description));
	fmt->pixelformat = bcwc_formats[fmt->index].fmt.pixelformat;
	return 0;
}

static int bcwc_v4l2_ioctl_try_fmt_vid_cap(struct file *filp, void *_priv,
					   struct v4l2_format *fmt)
{
	struct bcwc_private *dev_priv = video_drvdata(filp);
	const struct bcwc_fmt *p, *p1;
	int i, width, height, wanted_width, wanted_height, dist, maxdist;

	pr_info("%s: %dx%d\n", __FUNCTION__, fmt->fmt.pix.width, fmt->fmt.pix.height);

	for(i = 0; i < ARRAY_SIZE(bcwc_formats); i++) {
		p = bcwc_formats + i;
		if (p->fmt.height == fmt->fmt.pix.height &&
		    p->fmt.width == fmt->fmt.pix.width &&
		    p->fmt.pixelformat == fmt->fmt.pix.pixelformat) {
			dev_priv->fmt = p;
			return 0;
		}
	}

	wanted_width = fmt->fmt.pix.width;
	wanted_height = fmt->fmt.pix.height;
	maxdist = 0x7fffffff;
	p1 = NULL;
	for (i = 0; i < ARRAY_SIZE(bcwc_formats); i++) {
		p = bcwc_formats + i;

		if (p->fmt.pixelformat != fmt->fmt.pix.pixelformat)
			continue;

		width = p->fmt.width;
		height = p->fmt.height;

		dist = min(width, wanted_width) * min(height, wanted_height);
		dist = width * height + wanted_width * wanted_height - 2 * dist;

		pr_debug("%dx%d, dist %d\n", width, height, dist);

		if (dist < maxdist) {
			maxdist = dist;
			p1 = p;
			pr_debug("assign\n");
		}

		if (maxdist == 0)
			break;
	}

	if (!p1) {
		pr_debug("no assign\n");
		return -EINVAL;
	}

	fmt->fmt.pix.width = p1->fmt.width;
	fmt->fmt.pix.height = p1->fmt.height;
	fmt->fmt.pix.field = V4L2_FIELD_NONE;

	switch(p1->fmt.pixelformat) {
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
		fmt->fmt.pix.bytesperline = p1->fmt.width * 2;
		break;
	case V4L2_PIX_FMT_NV16:
		fmt->fmt.pix.bytesperline = p1->fmt.width;
		break;
	default:
		WARN_ON(1);
	}
	return 0;
}

static int bcwc_v4l2_ioctl_g_fmt_vid_cap(struct file *filp, void *priv,
					 struct v4l2_format *fmt)
{
	struct bcwc_private *dev_priv = video_drvdata(filp);
	fmt->fmt.pix = dev_priv->fmt->fmt;
	return 0;
}

static int bcwc_v4l2_ioctl_s_fmt_vid_cap(struct file *filp, void *priv,
					 struct v4l2_format *fmt)
{
	struct bcwc_private *dev_priv = video_drvdata(filp);
	struct bcwc_fmt *p = NULL;
	int i;

	pr_info("%s: %dx%d %c%c%c%c\n", __FUNCTION__,
		fmt->fmt.pix.width, fmt->fmt.pix.height,
		fmt->fmt.pix.pixelformat,
		fmt->fmt.pix.pixelformat >> 8,
		fmt->fmt.pix.pixelformat >> 16,
		fmt->fmt.pix.pixelformat >> 24);

	for(i = 0; i < ARRAY_SIZE(bcwc_formats); i++) {
		p = bcwc_formats + i;
		if (p->fmt.width == fmt->fmt.pix.width &&
		    p->fmt.height == fmt->fmt.pix.height &&
		    p->fmt.pixelformat == fmt->fmt.pix.pixelformat) {
			break;
		}
	}

	if (i == ARRAY_SIZE(bcwc_formats))
		return -EINVAL;

	dev_priv->fmt = p;
	return 0;
}


static int bcwc_v4l2_ioctl_g_parm(struct file *filp, void *priv,
		struct v4l2_streamparm *parm)
{
	pr_debug("%s\n", __FUNCTION__);
	return -ENODEV;
}

static int bcwc_v4l2_ioctl_s_parm(struct file *filp, void *priv,
		struct v4l2_streamparm *parm)
{
	pr_debug("%s\n", __FUNCTION__);
	return -ENODEV;
}

static int bcwc_v4l2_ioctl_enum_framesizes(struct file *filp, void *priv,
		struct v4l2_frmsizeenum *sizes)
{
	struct bcwc_fmt *p = NULL;
	int i, j;
	pr_debug("format %c%c%c%c, index %d\n",
		 sizes->pixel_format >> 24,
		 sizes->pixel_format >> 16,
		 sizes->pixel_format >> 8,
		 sizes->pixel_format,
		 sizes->index);

	for(i = 0, j = 0; i < ARRAY_SIZE(bcwc_formats); i++) {
		if (bcwc_formats[i].fmt.pixelformat == sizes->pixel_format) {
			if (j++ == sizes->index) {
				p = bcwc_formats + i;
				break;
			}
		}
	}

	if (!p)
		return -EINVAL;

	sizes->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	sizes->discrete.width = p->fmt.width;
	sizes->discrete.height = p->fmt.height;
	pr_debug("%dx%d\n", sizes->discrete.width, sizes->discrete.height);
	return 0;
}

static int bcwc_v4l2_ioctl_enum_frameintervals(struct file *filp, void *priv,
		struct v4l2_frmivalenum *interval)
{
	pr_debug("%s\n", __FUNCTION__);
	return -ENODEV;
}

static struct v4l2_ioctl_ops bcwc_ioctl_ops = {
	.vidioc_enum_input      = bcwc_v4l2_ioctl_enum_input,
	.vidioc_g_input         = bcwc_v4l2_ioctl_g_input,
	.vidioc_s_input         = bcwc_v4l2_ioctl_s_input,
	.vidioc_s_std           = bcwc_v4l2_ioctl_s_std,
	.vidioc_g_std           = bcwc_v4l2_ioctl_g_std,
	.vidioc_enum_fmt_vid_cap = bcwc_v4l2_ioctl_enum_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap = bcwc_v4l2_ioctl_try_fmt_vid_cap,

	.vidioc_g_fmt_vid_cap   = bcwc_v4l2_ioctl_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap   = bcwc_v4l2_ioctl_s_fmt_vid_cap,
	.vidioc_querycap        = bcwc_v4l2_ioctl_querycap,


        .vidioc_reqbufs         = vb2_ioctl_reqbufs,
	.vidioc_create_bufs     = vb2_ioctl_create_bufs,
	.vidioc_querybuf        = vb2_ioctl_querybuf,
	.vidioc_qbuf            = vb2_ioctl_qbuf,
	.vidioc_dqbuf           = vb2_ioctl_dqbuf,
	.vidioc_expbuf          = vb2_ioctl_expbuf,
	.vidioc_streamon        = vb2_ioctl_streamon,
	.vidioc_streamoff       = vb2_ioctl_streamoff,

	.vidioc_g_parm          = bcwc_v4l2_ioctl_g_parm,
	.vidioc_s_parm          = bcwc_v4l2_ioctl_s_parm,
	.vidioc_enum_framesizes = bcwc_v4l2_ioctl_enum_framesizes,
	.vidioc_enum_frameintervals = bcwc_v4l2_ioctl_enum_frameintervals,
};

int bcwc_v4l2_register(struct bcwc_private *dev_priv)
{
	struct v4l2_device *v4l2_dev = &dev_priv->v4l2_dev;
	struct video_device *vdev;
	struct vb2_queue *q;
	int ret;

	ret = v4l2_device_register(&dev_priv->pdev->dev, v4l2_dev);
	if (ret) {
		pr_err("v4l2_device_register: %d\n", ret);
		return ret;
	}

	vdev = video_device_alloc();
	if (!vdev) {
		ret = -ENOMEM;
		goto fail;
	}
	dev_priv->videodev = vdev;

	q = &dev_priv->vb2_queue;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	q->drv_priv = dev_priv;
	q->ops = &vb2_queue_ops;
	q->mem_ops = &vb2_dma_sg_memops;
	q->buf_struct_size = 0;//sizeof(struct vpif_cap_buffer);
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->min_buffers_needed = 1;
	q->lock = &dev_priv->vb2_queue_lock;

	ret = vb2_queue_init(q);
	if (ret)
		goto fail;

	dev_priv->alloc_ctx = vb2_dma_sg_init_ctx(&dev_priv->pdev->dev);
	dev_priv->fmt = bcwc_formats;
	vdev->v4l2_dev = v4l2_dev;
	strcpy(vdev->name, "Apple Facetime HD"); // XXX: Length?
	vdev->vfl_dir = VFL_DIR_RX;
	vdev->fops = &bcwc_vdev_fops;
	vdev->ioctl_ops = &bcwc_ioctl_ops;
	vdev->queue = q;
	vdev->release = video_device_release;

	video_set_drvdata(vdev, dev_priv);
	ret = video_register_device(vdev, VFL_TYPE_GRABBER, -1);
	if (ret) {
		video_device_release(vdev);
		goto fail;
	}
	return 0;
fail:
	v4l2_device_unregister(&dev_priv->v4l2_dev);
	return ret;
}

void bcwc_v4l2_unregister(struct bcwc_private *dev_priv)
{

	vb2_dma_sg_cleanup_ctx(dev_priv->alloc_ctx);
	video_unregister_device(dev_priv->videodev);
	v4l2_device_unregister(&dev_priv->v4l2_dev);
}
