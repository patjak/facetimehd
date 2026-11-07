// SPDX-License-Identifier: GPL-2.0-only
/*
 * FacetimeHD camera driver
 *
 * Copyright (C) 2015 Sven Schnelle <svens@stackframe.org>
 *
 */

#include <linux/kernel.h>
#include <linux/gcd.h>
#include <linux/math64.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/videodev2.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/videobuf2-dma-sg.h>
#include "fthd_drv.h"
#include "fthd_hw.h"
#include "fthd_isp.h"
#include "fthd_ringbuf.h"
#include "fthd_buffer.h"

#define FTHD_MAX_WIDTH 1920
#define FTHD_MAX_HEIGHT 1080
#define FTHD_MIN_WIDTH 320
#define FTHD_MIN_HEIGHT 240
#define FTHD_SENSOR_WIDTH 1280
#define FTHD_SENSOR_HEIGHT 720
#define FTHD_NUM_FORMATS 1

static int fthd_buffer_queue_setup(struct vb2_queue *vq,
				   unsigned int *nbuffers,
				   unsigned int *nplanes,
				   unsigned int sizes[],
				   struct device *alloc_devs[])
{
	struct fthd_private *dev_priv = vb2_get_drv_priv(vq);
	struct v4l2_pix_format *cur_fmt = &dev_priv->fmt.fmt;
	unsigned long budget = 16 * 1024 * 1024;
	unsigned long size;
	unsigned int i, allocated, slots, limit;
	bool create = *nplanes != 0;

	allocated = vb2_get_num_buffers(vq);
	slots = vq->max_num_buffers;

	if (create) {
		if (*nplanes != 1 || sizes[0] < cur_fmt->sizeimage)
			return -EINVAL;
	} else {
		*nplanes = 1;
		sizes[0] = cur_fmt->sizeimage;
	}

	size = PAGE_ALIGN((unsigned long)sizes[0]);
	if (!size)
		return -ENOMEM;

	if (create) {
		/*
		 * CREATE_BUFS asks for buffers on top of the ones already
		 * there, so those are charged against the budget and against
		 * the context count.
		 */
		for (i = 0; i < slots; i++) {
			struct vb2_buffer *vb;
			unsigned long used;

			vb = vb2_get_buffer(vq, i);
			if (!vb)
				continue;
			used = PAGE_ALIGN((unsigned long)vb2_plane_size(vb, 0));
			if (!used || used >= budget)
				return -ENOMEM;
			budget -= used;
		}
		if (allocated >= FTHD_BUFFERS)
			return -ENOBUFS;
		if (size > budget)
			return -ENOMEM;
		limit = min_t(unsigned int, FTHD_BUFFERS - allocated,
			      budget / size);
		*nbuffers = min(*nbuffers, limit);
		if (!*nbuffers)
			return -ENOMEM;
	} else {
		/*
		 * REQBUFS asks for a total. vb2 calls back a second time to
		 * confirm a partial allocation, and the buffers it asks about
		 * then are the ones already allocated, so charging them here
		 * would reject a count that is in fact usable.
		 */
		if (size > budget)
			return -ENOMEM;
		limit = min_t(unsigned int, FTHD_BUFFERS, budget / size);
		*nbuffers = clamp_t(unsigned int, *nbuffers, 4U, limit);
		if (*nbuffers < 2)
			return -ENOMEM;
	}

	alloc_devs[0] = &dev_priv->pdev->dev;
	return 0;
}

static void fthd_buffer_cleanup(struct vb2_buffer *vb)
{
	struct fthd_private *dev_priv = vb2_get_drv_priv(vb->vb2_queue);
	struct h2t_buf_ctx *ctx = NULL;
	int i;

	pr_debug("%p\n", vb);
	for (i = 0; i < FTHD_BUFFERS; i++) {
		if (dev_priv->h2t_bufs[i].vb == vb) {
			ctx = dev_priv->h2t_bufs + i;
			break;
		}
	}
	if (!ctx || ctx->state == BUF_FREE)
		return;

	ctx->state = BUF_FREE;
	ctx->vb = NULL;
	isp_mem_destroy(ctx->dma_desc_obj);
	for (i = 0; i < dev_priv->fmt.planes; i++) {
		iommu_free(dev_priv, ctx->plane[i]);
		ctx->plane[i] = NULL;
	}
	ctx->dma_desc_obj = NULL;
}

static int fthd_send_h2t_buffer(struct fthd_private *dev_priv, struct h2t_buf_ctx *ctx)
{
	u32 entry;
	int ret;

	pr_debug("sending buffer %p size %ld, ctx %p\n", ctx->vb, sizeof(ctx->dma_desc_list), ctx);
	FTHD_S2_MEMCPY_TOIO(ctx->dma_desc_obj->offset, &ctx->dma_desc_list, sizeof(ctx->dma_desc_list));
	ret = fthd_channel_ringbuf_send(dev_priv, dev_priv->channel_buf_h2t,
					ctx->dma_desc_obj->offset, 0x180, 0x30000000, &entry);

	if (ret) {
		pr_err("%s: fthd_channel_ringbuf_send: %d\n", __func__, ret);
		return ret;
	}
	return fthd_channel_wait_ready(dev_priv, dev_priv->channel_buf_h2t, entry, 2000);
}

static void fthd_buffer_queue(struct vb2_buffer *vb)
{
	struct fthd_private *dev_priv = vb2_get_drv_priv(vb->vb2_queue);
	struct dma_descriptor_list *list;
	struct h2t_buf_ctx *ctx = NULL;
	int i;

	pr_debug("vb = %p\n", vb);
	for (i = 0; i < FTHD_BUFFERS; i++) {
		if (dev_priv->h2t_bufs[i].vb == vb) {
			ctx = dev_priv->h2t_bufs + i;
			break;
		}
	}

	if (!ctx)
		return;

	if (ctx->state != BUF_ALLOC)
		return;

	if (!vb->vb2_queue->streaming) {
		ctx->state = BUF_DRV_QUEUED;
	} else {
		list = &ctx->dma_desc_list;
		list->field0 = 1;
		ctx->state = BUF_HW_QUEUED;
		/* Ensure descriptor flags are updated before hardware transmission */
		wmb();
		pr_debug("%d: field0: %d, count %d, pool %d, addr0 0x%08x, addr1 0x%08x tag 0x%08llx vb = %p\n", i, list->field0,
			 list->desc[0].count, list->desc[0].pool, list->desc[0].addr0, list->desc[0].addr1, list->desc[0].tag, ctx->vb);

		if (fthd_send_h2t_buffer(dev_priv, ctx)) {
			vb2_buffer_done(vb, VB2_BUF_STATE_ERROR);
			ctx->state = BUF_ALLOC;
		}
	}
}

static int fthd_buffer_prepare(struct vb2_buffer *vb)
{
	struct fthd_private *dev_priv = vb2_get_drv_priv(vb->vb2_queue);
	struct sg_table *sgtable;
	struct h2t_buf_ctx *ctx = NULL;
	struct dma_descriptor_list *dma_list;
	int i;

	pr_debug("%p\n", vb);
	for (i = 0; i < FTHD_BUFFERS; i++) {
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

		ctx->vb = vb;
		ctx->state = BUF_ALLOC;

		for (i = 0; i < dev_priv->fmt.planes; i++) {
			sgtable = vb2_dma_sg_plane_desc(vb, i);
			ctx->plane[i] = iommu_allocate_sgtable(dev_priv, sgtable);
			if (!ctx->plane[i])
				goto err_free;
		}
	}

	vb2_set_plane_payload(vb, 0, dev_priv->fmt.fmt.sizeimage);

	dma_list = &ctx->dma_desc_list;
	memset(dma_list, 0, 0x180);

	dma_list->field0 = 1;
	dma_list->count = 1;
	dma_list->desc[0].count = 1;
	dma_list->desc[0].pool = 0x02;
	dma_list->desc[0].addr0 = ((ctx->plane[0]->offset << 12) | 0xc0000000) +
		ctx->plane[0]->byte_offset;

	if (dev_priv->fmt.planes >= 2)
		dma_list->desc[0].addr1 = ((ctx->plane[1]->offset << 12) | 0xc0000000) +
		ctx->plane[1]->byte_offset;
	if (dev_priv->fmt.planes >= 3)
		dma_list->desc[0].addr2 = ((ctx->plane[2]->offset << 12) | 0xc0000000) +
		ctx->plane[2]->byte_offset;

	dma_list->desc[0].tag = (u64)ctx;
	init_waitqueue_head(&ctx->wq);
	return 0;

err_free:
	for (i = 0; i < dev_priv->fmt.planes; i++) {
		iommu_free(dev_priv, ctx->plane[i]);
		ctx->plane[i] = NULL;
	}
	isp_mem_destroy(ctx->dma_desc_obj);
	ctx->dma_desc_obj = NULL;
	ctx->vb = NULL;
	ctx->state = BUF_FREE;
	return -ENOMEM;
}

void fthd_buffer_return_handler(struct fthd_private *dev_priv, u32 offset, int size)
{
	struct dma_descriptor_list list;
	struct h2t_buf_ctx *ctx;
	int i;

	FTHD_S2_MEMCPY_FROMIO(&list, offset, sizeof(list));

	for (i = 0; i < list.count; i++) {
		ctx = (struct h2t_buf_ctx *)list.desc[i].tag;
		pr_debug("%d: field0: %d, count %d, pool %d, addr0 0x%08x, addr1 0x%08x tag 0x%08llx vb = %p, ctx = %p\n", i, list.field0,
			 list.desc[i].count, list.desc[i].pool, list.desc[i].addr0, list.desc[i].addr1, list.desc[i].tag, ctx->vb, ctx);

		if (ctx->state == BUF_HW_QUEUED || ctx->state == BUF_DRV_QUEUED) {
			struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(ctx->vb);

			vbuf->sequence = dev_priv->sequence++;
			vbuf->vb2_buf.timestamp = ktime_get_ns();
			vbuf->field = V4L2_FIELD_NONE;

			ctx->state = BUF_ALLOC;
			ctx->vb->timestamp = ktime_get_ns();
			vbuf = to_vb2_v4l2_buffer(ctx->vb);
			vbuf->sequence = dev_priv->sequence++;
			vbuf->field = V4L2_FIELD_NONE;
			vb2_buffer_done(ctx->vb, VB2_BUF_STATE_DONE);
		}

	}
}

static int fthd_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct fthd_private *dev_priv = vb2_get_drv_priv(vq);
	struct h2t_buf_ctx *ctx;
	int i, ret;

	pr_debug("count = %d\n", count);
	dev_priv->sequence = 0;
	ret = fthd_start_channel(dev_priv, 0);
	if (ret) {
		/* vb2 owns the buffers again if the queue never starts. */
		for (i = 0; i < FTHD_BUFFERS; i++) {
			ctx = dev_priv->h2t_bufs + i;
			if (ctx->state != BUF_DRV_QUEUED)
				continue;
			vb2_buffer_done(ctx->vb, VB2_BUF_STATE_QUEUED);
			ctx->state = BUF_ALLOC;
		}
		return ret;
	}

	dev_priv->channel_started = true;
	/* Starting the channel resets the ISP, so push the control values down. */
	v4l2_ctrl_handler_setup(&dev_priv->v4l2_ctrl_handler);

	for (i = 0; i < FTHD_BUFFERS; i++) {
		ctx = dev_priv->h2t_bufs + i;
		if (ctx->state != BUF_DRV_QUEUED)
			continue;

		if (fthd_send_h2t_buffer(dev_priv, ctx)) {
			vb2_buffer_done(ctx->vb, VB2_BUF_STATE_ERROR);
			ctx->state = BUF_ALLOC;
			continue;
		}
		ctx->state = BUF_HW_QUEUED;
	}
	return 0;
}

static void fthd_stop_streaming(struct vb2_queue *vq)
{
	struct fthd_private *dev_priv = vb2_get_drv_priv(vq);
	struct h2t_buf_ctx *ctx;
	int ret, i;

	dev_priv->channel_started = false;
	ret = fthd_stop_channel(dev_priv, 0);

	/* Buffers never sent to the firmware will not come back from it */
	for (i = 0; i < FTHD_BUFFERS; i++) {
		ctx = dev_priv->h2t_bufs + i;
		if (ctx->state == BUF_DRV_QUEUED) {
			ctx->state = BUF_ALLOC;
			vb2_buffer_done(ctx->vb, VB2_BUF_STATE_ERROR);
		}
	}

	if (!ret) {
		pr_debug("waiting for buffers...\n");
		vb2_wait_for_all_buffers(vq);
		pr_debug("done\n");
	} else {
		/* Firmware doesn't respond. */
		for (i = 0; i < FTHD_BUFFERS; i++) {
			ctx = dev_priv->h2t_bufs + i;
			if (ctx->state == BUF_DRV_QUEUED || ctx->state == BUF_HW_QUEUED) {
				vb2_buffer_done(ctx->vb, VB2_BUF_STATE_ERROR);
				ctx->state = BUF_ALLOC;
			}
		}
	}
}

static const struct vb2_ops vb2_queue_ops = {
	.queue_setup            = fthd_buffer_queue_setup,
	.buf_prepare            = fthd_buffer_prepare,
	.buf_cleanup            = fthd_buffer_cleanup,
	.start_streaming        = fthd_start_streaming,
	.stop_streaming         = fthd_stop_streaming,
	.buf_queue              = fthd_buffer_queue,
#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 0, 0)
	.wait_prepare           = vb2_ops_wait_prepare,
	.wait_finish            = vb2_ops_wait_finish,
#endif
};

static int fthd_v4l2_open(struct file *filp)
{
	struct fthd_private *dev_priv = video_drvdata(filp);
	int ret;

	ret = v4l2_fh_open(filp);
	if (!ret)
		atomic_inc(&dev_priv->users);

	return ret;
}

static int fthd_v4l2_release(struct file *filp)
{
	struct fthd_private *dev_priv = video_drvdata(filp);
	int ret;

	ret = vb2_fop_release(filp);
	atomic_dec(&dev_priv->users);

	return ret;
}

static const struct v4l2_file_operations fthd_vdev_fops = {
	.owner          = THIS_MODULE,
	.open           = fthd_v4l2_open,

	.read		= vb2_fop_read,
	.release        = fthd_v4l2_release,
	.poll           = vb2_fop_poll,
	.mmap           = vb2_fop_mmap,
	.unlocked_ioctl = video_ioctl2
};

static int fthd_v4l2_ioctl_enum_input(struct file *filp, void *priv,
				      struct v4l2_input *input)
{
	if (input->index != 0)
		return -EINVAL;

	memset(input, 0, sizeof(*input));
	strscpy(input->name, "Camera", sizeof(input->name));
	input->type = V4L2_INPUT_TYPE_CAMERA;
	input->std = 0;

	return 0;
}

static int fthd_v4l2_ioctl_g_input(struct file *filp, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int fthd_v4l2_ioctl_s_input(struct file *filp, void *priv, unsigned int i)
{
	if (i != 0)
		return -EINVAL;
	return 0;
}

static int fthd_v4l2_ioctl_querycap(struct file *filp, void *priv,
				    struct v4l2_capability *cap)
{
	struct fthd_private *dev_priv = video_drvdata(filp);

	strscpy(cap->driver, "facetimehd", sizeof(cap->driver));
	strscpy(cap->card, "Apple Facetime HD", sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "PCI:%s",
		 pci_name(dev_priv->pdev));

	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE |
			   V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int fthd_v4l2_ioctl_enum_fmt_vid_cap(struct file *filp, void *priv,
					   struct v4l2_fmtdesc *fmt)
{
	if (fmt->index > 0)
		return -EINVAL;

	fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt->pixelformat = V4L2_PIX_FMT_YUYV;
	strscpy(fmt->description, "YUYV 4:2:2", sizeof(fmt->description));

	return 0;
}

static int fthd_v4l2_adjust_format(struct fthd_private *dev_priv,
				   struct v4l2_pix_format *pix)
{
	/* Upper bound is the sensor's native resolution (e.g. 1280x720 on
	 * MacBookPro, 848x588 on the 12-inch MacBook); fall back to the generic
	 * ceiling if it hasn't been detected yet.
	 */
	unsigned int max_w = dev_priv->sensor_width ? : FTHD_MAX_WIDTH;
	unsigned int max_h = dev_priv->sensor_height ? : FTHD_MAX_HEIGHT;

	pix->pixelformat = V4L2_PIX_FMT_YUYV;

	if (pix->width < FTHD_MIN_WIDTH)
		pix->width = FTHD_MIN_WIDTH;
	if (pix->width > max_w)
		pix->width = max_w;
	if (pix->height < FTHD_MIN_HEIGHT)
		pix->height = FTHD_MIN_HEIGHT;
	if (pix->height > max_h)
		pix->height = max_h;

	pix->colorspace = V4L2_COLORSPACE_SRGB;
	pix->field = V4L2_FIELD_NONE;
	pix->width = ALIGN(pix->width, 8);

	pix->bytesperline = pix->width * 2;
	pix->sizeimage = pix->bytesperline * pix->height;

	return 0;
}

static int fthd_v4l2_ioctl_try_fmt_vid_cap(struct file *filp, void *_priv,
					   struct v4l2_format *fmt)
{
	struct fthd_private *dev_priv = video_drvdata(filp);

	pr_debug("%s: %dx%d\n", __func__, fmt->fmt.pix.width, fmt->fmt.pix.height);

	if (fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	return fthd_v4l2_adjust_format(dev_priv, &fmt->fmt.pix);
}

static int fthd_v4l2_ioctl_g_fmt_vid_cap(struct file *filp, void *priv,
					 struct v4l2_format *fmt)
{
	struct fthd_private *dev_priv = video_drvdata(filp);

	fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt->fmt.pix = dev_priv->fmt.fmt;

	return 0;
}

static int fthd_v4l2_ioctl_s_fmt_vid_cap(struct file *filp, void *priv,
					 struct v4l2_format *fmt)
{
	struct fthd_private *dev_priv = video_drvdata(filp);
	int ret;

	if (fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	/* FIXME: Check if hardware is busy */

	ret = fthd_v4l2_adjust_format(dev_priv, &fmt->fmt.pix);
	if (ret)
		return ret;

	pr_debug("%c%c%c%c\n", fmt->fmt.pix.pixelformat, fmt->fmt.pix.pixelformat >> 8,
		 fmt->fmt.pix.pixelformat >> 16, fmt->fmt.pix.pixelformat >> 24);

	dev_priv->fmt.fmt = fmt->fmt.pix;
	dev_priv->fmt.planes = 1;

	return 0;
}

static int fthd_v4l2_ioctl_g_parm(struct file *filp, void *priv,
				 struct v4l2_streamparm *parm)
{
	struct fthd_private *dev_priv = video_drvdata(filp);
	u32 rate = READ_ONCE(dev_priv->frame_rate);
	u32 div = gcd(rate, FTHD_FRAME_RATE_SCALE);

	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	parm->parm.capture.readbuffers = FTHD_BUFFERS;
	parm->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	/*
	 * S_PARM returns this too, and PipeWire's V4L2 source takes the stream
	 * rate from it, so report the interval the sensor is set to. A value
	 * that disagreed with the delivered rate (25 fps from frametime/1000
	 * against a real 30 fps) made pipewiresrc compute negative frame
	 * durations and freeze. Keep the fraction reduced as well: PipeWire
	 * without the fixes in 1.4.10 (1.4.2, for one) gets pipewiresrc's
	 * clock and latency wrong on 256/7680 instead of 1/30, and capture
	 * stalls (#358).
	 */
	parm->parm.capture.timeperframe.numerator = FTHD_FRAME_RATE_SCALE / div;
	parm->parm.capture.timeperframe.denominator = rate / div;
	return 0;
}

static int fthd_v4l2_ioctl_s_parm(struct file *filp, void *priv,
				 struct v4l2_streamparm *parm)
{
	struct fthd_private *dev_priv = video_drvdata(filp);
	struct v4l2_fract *tpf = &parm->parm.capture.timeperframe;
	u64 rate = FTHD_FRAME_RATE_MAX;
	int ret;

	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (tpf->numerator && tpf->denominator)
		rate = div_u64((u64)tpf->denominator * FTHD_FRAME_RATE_SCALE,
			       tpf->numerator);
	rate = clamp_t(u64, rate, FTHD_FRAME_RATE_MIN, FTHD_FRAME_RATE_MAX);

	ret = mutex_lock_interruptible(&dev_priv->vb2_queue_lock);
	if (ret)
		return ret;
	/* Rate commands take effect at channel start. Do not promise a live change. */
	if (vb2_is_streaming(&dev_priv->vb2_queue)) {
		ret = -EBUSY;
	} else {
		WRITE_ONCE(dev_priv->frame_rate, (u32)rate);
		ret = fthd_v4l2_ioctl_g_parm(filp, priv, parm);
	}
	mutex_unlock(&dev_priv->vb2_queue_lock);
	return ret;
}

static int fthd_v4l2_ioctl_enum_framesizes(struct file *filp, void *priv,
					   struct v4l2_frmsizeenum *sizes)
{
	struct fthd_private *dev_priv = video_drvdata(filp);

	if (sizes->index)
		return -EINVAL;

	if (sizes->pixel_format != V4L2_PIX_FMT_YUYV)
		return -EINVAL;

	sizes->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	sizes->stepwise.min_width  = FTHD_MIN_WIDTH;
	sizes->stepwise.max_width  = dev_priv->sensor_width ? : FTHD_MAX_WIDTH;
	sizes->stepwise.step_width = 8;
	sizes->stepwise.min_height  = FTHD_MIN_HEIGHT;
	sizes->stepwise.max_height  = dev_priv->sensor_height ? : FTHD_MAX_HEIGHT;
	sizes->stepwise.step_height = 1;

	return 0;
}

static int fthd_v4l2_ioctl_enum_frameintervals(struct file *filp, void *priv,
					       struct v4l2_frmivalenum *interval)
{
	struct fthd_private *dev_priv = video_drvdata(filp);
	unsigned int max_w = dev_priv->sensor_width ? : FTHD_MAX_WIDTH;
	unsigned int max_h = dev_priv->sensor_height ? : FTHD_MAX_HEIGHT;

	if (interval->index != 0)
		return -EINVAL;

	if (interval->pixel_format != V4L2_PIX_FMT_YUYV)
		return -EINVAL;

	if ((interval->width & 7) ||
	    interval->width > max_w ||
	    interval->height > max_h)
		return -EINVAL;

	interval->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	interval->discrete.numerator = 1;
	interval->discrete.denominator = 30;

	return 0;
}

static int fthd_v4l2_ioctl_subscribe_event(struct v4l2_fh *fh,
		const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_CTRL:
		return v4l2_ctrl_subscribe_event(fh, sub);
	}

	return -EINVAL;
}

/*
 * ============================================================================
 * Digital Zoom, Pan and Tilt (ePTZ) Hardware Cropping
 * ============================================================================
 * Educational Note for Developers:
 * The BCM1570 ISP coprocessor contains a dedicated silicon scaling and cropping
 * engine. Instead of burdening the host CPU, we instruct the ISP directly to
 * extract a sub-window of the physical sensor array (1280x720) and scale it to
 * the active video stream size.
 *
 * This provides lossless digital zoom, pan, and tilt (ePTZ) compatible with
 * standard Linux tools (OBS Studio, GStreamer, OpenCV, v4l2-ctl).
 */
static int fthd_v4l2_ioctl_g_selection(struct file *filp, void *fh,
				       struct v4l2_selection *s)
{
	struct fthd_private *dev_priv = video_drvdata(filp);

	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	switch (s->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_CROP_DEFAULT:
		/* Physical sensor bounds available for cropping */
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = FTHD_SENSOR_WIDTH;
		s->r.height = FTHD_SENSOR_HEIGHT;
		break;
	case V4L2_SEL_TGT_CROP:
		/* Currently active crop rectangle */
		s->r.left = dev_priv->crop_x1;
		s->r.top = dev_priv->crop_y1;
		s->r.width = dev_priv->crop_x2 - dev_priv->crop_x1;
		s->r.height = dev_priv->crop_y2 - dev_priv->crop_y1;
		break;
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE:
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = dev_priv->fmt.fmt.width;
		s->r.height = dev_priv->fmt.fmt.height;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int fthd_v4l2_ioctl_s_selection(struct file *filp, void *fh,
				       struct v4l2_selection *s)
{
	struct fthd_private *dev_priv = video_drvdata(filp);
	int ret;

	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	switch (s->target) {
	case V4L2_SEL_TGT_COMPOSE:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = dev_priv->fmt.fmt.width;
		s->r.height = dev_priv->fmt.fmt.height;
		return 0;
	case V4L2_SEL_TGT_CROP:
		break;
	default:
		return -EINVAL;
	}

	/*
	 * Clamp crop coordinates within valid physical sensor bounds (1280x720)
	 * and enforce even alignment for horizontal/vertical chroma pairs (4:2:2).
	 */
	s->r.left = clamp_t(u32, s->r.left, 0, FTHD_SENSOR_WIDTH - FTHD_MIN_WIDTH) & ~1u;
	s->r.top = clamp_t(u32, s->r.top, 0, FTHD_SENSOR_HEIGHT - FTHD_MIN_HEIGHT) & ~1u;
	s->r.width = clamp_t(u32, s->r.width, FTHD_MIN_WIDTH, FTHD_SENSOR_WIDTH - s->r.left) & ~1u;
	s->r.height = clamp_t(u32, s->r.height, FTHD_MIN_HEIGHT, FTHD_SENSOR_HEIGHT - s->r.top) & ~1u;

	dev_priv->crop_x1 = s->r.left;
	dev_priv->crop_y1 = s->r.top;
	dev_priv->crop_x2 = s->r.left + s->r.width;
	dev_priv->crop_y2 = s->r.top + s->r.height;

	pr_info("fthd: Applying hardware ePTZ crop: %ux%u at [%u, %u]\n",
		s->r.width, s->r.height, s->r.left, s->r.top);

	/*
	 * Note: BCM1570 CISP_CMD_CH_CROP_SET parameters:
	 * x, y, width, height (NOT x1, y1, x2, y2).
	 */
	ret = fthd_isp_cmd_channel_crop_set(dev_priv, 0,
					    s->r.left, s->r.top,
					    s->r.width, s->r.height);
	return ret;
}

static const struct v4l2_ioctl_ops fthd_ioctl_ops = {
	.vidioc_enum_input      = fthd_v4l2_ioctl_enum_input,
	.vidioc_g_input         = fthd_v4l2_ioctl_g_input,
	.vidioc_s_input         = fthd_v4l2_ioctl_s_input,
	.vidioc_enum_fmt_vid_cap = fthd_v4l2_ioctl_enum_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap = fthd_v4l2_ioctl_try_fmt_vid_cap,

	.vidioc_g_fmt_vid_cap   = fthd_v4l2_ioctl_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap   = fthd_v4l2_ioctl_s_fmt_vid_cap,
	.vidioc_querycap        = fthd_v4l2_ioctl_querycap,

	.vidioc_reqbufs         = vb2_ioctl_reqbufs,
	.vidioc_create_bufs     = vb2_ioctl_create_bufs,
	.vidioc_querybuf        = vb2_ioctl_querybuf,
	.vidioc_qbuf            = vb2_ioctl_qbuf,
	.vidioc_dqbuf           = vb2_ioctl_dqbuf,
	.vidioc_expbuf          = vb2_ioctl_expbuf,
	.vidioc_streamon        = vb2_ioctl_streamon,
	.vidioc_streamoff       = vb2_ioctl_streamoff,

	.vidioc_g_parm          = fthd_v4l2_ioctl_g_parm,
	.vidioc_s_parm          = fthd_v4l2_ioctl_s_parm,
	.vidioc_g_selection     = fthd_v4l2_ioctl_g_selection,
	.vidioc_enum_framesizes = fthd_v4l2_ioctl_enum_framesizes,
	.vidioc_enum_frameintervals = fthd_v4l2_ioctl_enum_frameintervals,

	/* Hardware ePTZ digital zoom & pan/tilt cropping */
	.vidioc_g_selection     = fthd_v4l2_ioctl_g_selection,
	.vidioc_s_selection     = fthd_v4l2_ioctl_s_selection,

	.vidioc_subscribe_event	= fthd_v4l2_ioctl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static int fthd_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	pr_debug("id = %x\n", ctrl->id);
	return -EINVAL;
}

static int fthd_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct fthd_private *dev_priv =
		container_of(ctrl->handler, struct fthd_private, v4l2_ctrl_handler);
	int ret = 0;

	pr_debug("id = %x, val = %d\n", ctrl->id, ctrl->val);

	if (!dev_priv->channel_started)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_CONTRAST:
		ret = fthd_isp_cmd_channel_contrast_set(dev_priv, 0, ctrl->val);
		break;
	case V4L2_CID_BRIGHTNESS:
		ret = fthd_isp_cmd_channel_brightness_set(dev_priv, 0, ctrl->val);
		break;
	case V4L2_CID_SATURATION:
		ret = fthd_isp_cmd_channel_saturation_set(dev_priv, 0, ctrl->val);
		break;
	case V4L2_CID_HUE:
		ret = fthd_isp_cmd_channel_hue_set(dev_priv, 0, ctrl->val);
		break;
	case V4L2_CID_AUTO_WHITE_BALANCE:
		ret = fthd_isp_cmd_channel_awb(dev_priv, 0, ctrl->val);
		break;
	case V4L2_CID_EXPOSURE_AUTO:
		/* The firmware runs auto exposure unconditionally. */
		ret = 0;
		break;
	case V4L2_CID_EXPOSURE_AUTO_PRIORITY:
		ret = fthd_isp_cmd_channel_frame_rate_min(dev_priv, 0,
							  fthd_isp_ae_frame_rate_min(dev_priv, ctrl->val));
		if (!ret)
			dev_priv->exposure_auto_priority = ctrl->val;
		break;
	case V4L2_CID_EXPOSURE_METERING:
		/*
		 * Auto-Exposure Metering Mode:
		 * Configures on-chip photometer sampling window:
		 * 0 = Average (evaluates the whole scene)
		 * 1 = Center-Weighted (prioritizes center)
		 * 2 = Spot (sharp spot metering for faces/backlit subjects)
		 * 3 = Matrix (evaluates multi-zone grid)
		 */
		ret = fthd_isp_cmd_channel_ae_metering_mode_set(dev_priv, 0, ctrl->val);
		break;
	case V4L2_CID_WIDE_DYNAMIC_RANGE:
		/*
		 * Dynamic Range Compression (DRC / HDR):
		 * Enables hardware tone curve adaptation to balance extreme highlights
		 * and deep shadows without clipping.
		 */
		fthd_drc_enable = ctrl->val;
		if (ctrl->val) {
			ret = fthd_isp_cmd_channel_drc_start(dev_priv, 0);
			if (!ret)
				ret = fthd_isp_cmd_channel_tone_curve_adaptation_start(dev_priv, 0);
		} else {
			ret = 0;
		}
		break;

	case V4L2_CID_ILLUMINATORS_1:
		/*
		 * Camera Status / Privacy LED Indicator:
		 * 0 = Auto (illuminated while video streaming is active)
		 * 1 = Force Off (stealth mode / indicator disabled)
		 * 2 = Force On (indicator continuously illuminated)
		 */
		fthd_led_mode = ctrl->val;
		ret = fthd_isp_cmd_set_led(dev_priv, 0, ctrl->val);
		break;
	default:
		break;

	}
	pr_debug("ret = %d\n", ret);
	return ret;
}

static const struct v4l2_ctrl_ops fthd_ctrl_ops = {
	.g_volatile_ctrl = fthd_g_volatile_ctrl,
	.s_ctrl = fthd_s_ctrl,
};

int fthd_v4l2_register(struct fthd_private *dev_priv)
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
	q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF | VB2_READ;
	q->drv_priv = dev_priv;
	q->ops = &vb2_queue_ops;
	q->mem_ops = &vb2_dma_sg_memops;
	q->buf_struct_size = 0;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->min_queued_buffers = 1;
	q->lock = &dev_priv->vb2_queue_lock;

	ret = vb2_queue_init(q);
	if (ret)
		goto fail;

	v4l2_ctrl_handler_init(&dev_priv->v4l2_ctrl_handler, 10);
	v4l2_ctrl_new_std(&dev_priv->v4l2_ctrl_handler, &fthd_ctrl_ops,
			  V4L2_CID_BRIGHTNESS, 0, 0xff, 1, 0x80);
	v4l2_ctrl_new_std(&dev_priv->v4l2_ctrl_handler, &fthd_ctrl_ops,
			  V4L2_CID_CONTRAST, 0, 0xff, 1, 0x80);
	v4l2_ctrl_new_std(&dev_priv->v4l2_ctrl_handler, &fthd_ctrl_ops,
			  V4L2_CID_SATURATION, 0, 0xff, 1, 0x80);
	v4l2_ctrl_new_std(&dev_priv->v4l2_ctrl_handler, &fthd_ctrl_ops,
			  V4L2_CID_HUE, 0, 0xff, 1, 0x80);
	v4l2_ctrl_new_std(&dev_priv->v4l2_ctrl_handler, &fthd_ctrl_ops,
			  V4L2_CID_AUTO_WHITE_BALANCE, 0, 1, 1, 1);
	v4l2_ctrl_new_std_menu(&dev_priv->v4l2_ctrl_handler, &fthd_ctrl_ops,
			       V4L2_CID_EXPOSURE_AUTO,
			       V4L2_EXPOSURE_APERTURE_PRIORITY,
			       ~(1 << V4L2_EXPOSURE_AUTO), V4L2_EXPOSURE_AUTO);
	v4l2_ctrl_new_std(&dev_priv->v4l2_ctrl_handler, &fthd_ctrl_ops,
			  V4L2_CID_EXPOSURE_AUTO_PRIORITY, 0, 1, 1, 0);

	/* Auto-Exposure Metering: Average (0), Center-Weighted (1), Spot (2), Matrix (3) */
	v4l2_ctrl_new_std_menu(&dev_priv->v4l2_ctrl_handler, &fthd_ctrl_ops,
			       V4L2_CID_EXPOSURE_METERING,
			       V4L2_EXPOSURE_METERING_MATRIX, 0,
			       fthd_ae_metering);

	/* Wide Dynamic Range (HDR / Dynamic Range Compression in hardware) */
	v4l2_ctrl_new_std(&dev_priv->v4l2_ctrl_handler, &fthd_ctrl_ops,
			  V4L2_CID_WIDE_DYNAMIC_RANGE, 0, 1, 1, fthd_drc_enable);

	/* Camera status LED / indicator control (0 = Auto, 1 = Force Off, 2 = Force On) */
	v4l2_ctrl_new_std(&dev_priv->v4l2_ctrl_handler, &fthd_ctrl_ops,
			  V4L2_CID_ILLUMINATORS_1, 0, 2, 1, fthd_led_mode);

	if (dev_priv->v4l2_ctrl_handler.error) {
		pr_err("failed to setup control handlers\n");
		v4l2_ctrl_handler_free(&dev_priv->v4l2_ctrl_handler);
		goto fail;
	}

	vdev->v4l2_dev = v4l2_dev;
	strscpy(vdev->name, "Apple Facetime HD", sizeof(vdev->name));
	vdev->vfl_dir = VFL_DIR_RX;
	vdev->fops = &fthd_vdev_fops;
	vdev->ioctl_ops = &fthd_ioctl_ops;
	vdev->queue = q;
	vdev->release = video_device_release;
	vdev->ctrl_handler = &dev_priv->v4l2_ctrl_handler;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE |
			    V4L2_CAP_STREAMING;

	video_set_drvdata(vdev, dev_priv);
	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		video_device_release(vdev);
		goto fail_vdev;
	}
	/* Default to the sensor's native resolution (detected at probe), or module
	 * parameters / generic ceiling if detection didn't run.
	 */
	dev_priv->fmt.fmt.pixelformat = V4L2_PIX_FMT_YUYV;
	dev_priv->fmt.fmt.width = dev_priv->sensor_width ? : fthd_default_width;
	dev_priv->fmt.fmt.height = dev_priv->sensor_height ? : fthd_default_height;
	dev_priv->fmt.fmt.sizeimage = dev_priv->fmt.fmt.width * dev_priv->fmt.fmt.height * 2;
	dev_priv->fmt.planes = 1;

	fthd_v4l2_adjust_format(dev_priv, &dev_priv->fmt.fmt);

	return 0;
fail_vdev:
	v4l2_ctrl_handler_free(&dev_priv->v4l2_ctrl_handler);
fail:
	v4l2_device_unregister(&dev_priv->v4l2_dev);
	return ret;
}

void fthd_v4l2_unregister(struct fthd_private *dev_priv)
{
	v4l2_ctrl_handler_free(&dev_priv->v4l2_ctrl_handler);
	video_unregister_device(dev_priv->videodev);
	v4l2_device_unregister(&dev_priv->v4l2_dev);
}
