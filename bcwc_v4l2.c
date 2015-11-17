
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
#include <linux/videodev2.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf-dma-sg.h>
#include "bcwc_drv.h"
#include "bcwc_hw.h"
#include "bcwc_isp.h"
#include "bcwc_ringbuf.h"
#include "bcwc_buffer.h"

static int bcwc_vb_buf_setup(struct videobuf_queue *q,
			     unsigned int *count, unsigned int *size)
{
	struct bcwc_private *priv = q->priv_data;
	pr_debug("%s: %d\n", __FUNCTION__, priv->user_format.sizeimage);
	*size = priv->user_format.sizeimage;
	if (*count == 0 || *count > 6)
		*count = 6;
	return 0;
}

static int bcwc_vb_buf_prepare(struct videobuf_queue *q,
				 struct videobuf_buffer *vb, enum v4l2_field field)
{
	struct bcwc_private *priv = q->priv_data;
	int ret;

	pr_debug("%s\n", __FUNCTION__);

	vb->size = priv->user_format.sizeimage;
	vb->width = priv->user_format.width;
	vb->height = priv->user_format.height;
	vb->field = field;

	if (vb->state == VIDEOBUF_NEEDS_INIT) {
		ret = videobuf_iolock(q, vb, NULL);
		if (ret)
			return ret;
	}
	vb->state = VIDEOBUF_PREPARED;
	return 0;
}

static void bcwc_vb_buf_queue(struct videobuf_queue *q,
				struct videobuf_buffer *vb)
{
	struct bcwc_private *priv = q->priv_data;
	pr_debug("%s\n", __FUNCTION__);
	vb->state = VIDEOBUF_QUEUED;
	list_add_tail(&vb->queue, &priv->buffer_queue);
}

static void bcwc_vb_buf_release(struct videobuf_queue *q,
				  struct videobuf_buffer *vb)
{
	struct bcwc_private *priv = q->priv_data;
	pr_debug("%s\n", __FUNCTION__);
	videobuf_dma_unmap(&priv->pdev->dev, videobuf_to_dma(vb));
	videobuf_dma_free(videobuf_to_dma(vb));
	vb->state = VIDEOBUF_NEEDS_INIT;
}

static const struct videobuf_queue_ops bcwc_vb_ops = {
	.buf_setup      = bcwc_vb_buf_setup,
	.buf_prepare    = bcwc_vb_buf_prepare,
	.buf_queue      = bcwc_vb_buf_queue,
	.buf_release    = bcwc_vb_buf_release,
};

static int bcwc_v4l2_open(struct file *filp)
{
	struct bcwc_private *priv = video_drvdata(filp);
	pr_info("%s: %p\n", __FUNCTION__, priv);

	mutex_lock(&priv->ioctl_lock);
	videobuf_queue_sg_init(&priv->vb_queue, &bcwc_vb_ops,
			       &priv->pdev->dev, &priv->vb_queue_lock,
			       V4L2_BUF_TYPE_VIDEO_CAPTURE, V4L2_FIELD_NONE,
			       sizeof(struct videobuf_buffer), priv, NULL);

	if (!priv->users++) {
		bcwc_isp_cmd_channel_start(priv);
	}

	mutex_unlock(&priv->ioctl_lock);
	return 0;
}

static int bcwc_v4l2_release(struct file *filp)
{
	struct bcwc_private *priv = video_drvdata(filp);
	pr_info("%s\n", __FUNCTION__);
	mutex_lock(&priv->ioctl_lock);
	if (!--priv->users) {
		bcwc_isp_cmd_channel_stop(priv);
	}

	videobuf_stop(&priv->vb_queue);
	videobuf_mmap_free(&priv->vb_queue);
	mutex_unlock(&priv->ioctl_lock);
	return 0;
}

static ssize_t bcwc_v4l2_read(struct file *filp, char __user *buffer,
			   size_t len, loff_t *pos)
{
	struct bcwc_private *priv = video_drvdata(filp);
	pr_info("%s: buf %p, size %d, pos %p\n", __FUNCTION__, buffer, (int)len, pos);
	return videobuf_read_stream(&priv->vb_queue, buffer, len, pos, 0,
				   filp->f_flags & O_NONBLOCK);
}

static unsigned int bcwc_v4l2_poll(struct file *filp, struct poll_table_struct *pt)
{
	struct bcwc_private *priv = video_drvdata(filp);
	pr_debug("%s\n", __FUNCTION__);
	return videobuf_poll_stream(filp, &priv->vb_queue, pt);
}

static int bcwc_v4l2_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct bcwc_private *priv = video_drvdata(filp);
	pr_debug("%s\n", __FUNCTION__);
	return videobuf_mmap_mapper(&priv->vb_queue, vma);
}

static struct v4l2_file_operations bcwc_vdev_fops = {
	.owner          = THIS_MODULE,
	.open           = bcwc_v4l2_open,
	.release        = bcwc_v4l2_release,
	.read           = bcwc_v4l2_read,
	.poll           = bcwc_v4l2_poll,
	.mmap           = bcwc_v4l2_mmap,
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

static struct bcwc_format {
	__u8 *desc;
	__u32 pixelformat;
	int bpp;   /* Bytes per pixel */
	u32 mbus_code;
} bcwc_formats[] = {
	{
		.desc           = "YUYV 4:2:2",
		.pixelformat    = V4L2_PIX_FMT_YUYV,
		.mbus_code      = MEDIA_BUS_FMT_YUYV8_2X8,
		.bpp            = 2,
	},
};
#if 0
static struct bcwc_format *bcwc_find_format(u32 pixelformat)
{
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(bcwc_formats); i++)
		if (bcwc_formats[i].pixelformat == pixelformat)
			return bcwc_formats + i;
	/* Not found? Then return the first format. */
	return bcwc_formats;
}
#endif
static const struct v4l2_pix_format bcwc_def_pix_format = {
	.width          = 1280,
	.height         = 720,
	.pixelformat    = V4L2_PIX_FMT_YUYV,
	.field          = V4L2_FIELD_NONE,
	.bytesperline   = 1280 * 2,
	.sizeimage      = 1280 * 720 * 2,
};

static const u32 bcwc_def_mbus_code = MEDIA_BUS_FMT_YUYV8_2X8;

static int bcwc_v4l2_ioctl_enum_fmt_vid_cap(struct file *filp, void *priv,
				   struct v4l2_fmtdesc *fmt)
{
	if (fmt->index >= ARRAY_SIZE(bcwc_formats))
		return -EINVAL;
	strlcpy(fmt->description, bcwc_formats[fmt->index].desc,
		sizeof(fmt->description));
	fmt->pixelformat = bcwc_formats[fmt->index].pixelformat;
	return 0;
}

static int bcwc_v4l2_ioctl_try_fmt_vid_cap(struct file *filp, void *_priv,
					   struct v4l2_format *fmt)
{
	struct bcwc_private *priv = video_drvdata(filp);
	pr_info("%s: %dx%d\n", __FUNCTION__, fmt->fmt.pix.width, fmt->fmt.pix.height);

	if (fmt->fmt.pix.height != 720 ||
	    fmt->fmt.pix.width != 1280 ||
	    fmt->fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV)
		return -EINVAL;

	priv->user_format.height = fmt->fmt.pix.height;
	priv->user_format.width = fmt->fmt.pix.width;
	priv->user_format.sizeimage = priv->user_format.height * priv->user_format.width * 2;
	return 0;
}

static int bcwc_v4l2_ioctl_g_fmt_vid_cap(struct file *filp, void *priv,
					 struct v4l2_format *fmt)
{
	fmt->fmt.pix.height = 720;
	fmt->fmt.pix.width = 1280;
	fmt->fmt.pix.sizeimage = 1280 * 720;
	fmt->fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
	return 0;
}

static int bcwc_v4l2_ioctl_s_fmt_vid_cap(struct file *filp, void *priv,
					 struct v4l2_format *fmt)
{
	pr_info("%s: %dx%d\n", __FUNCTION__, fmt->fmt.pix.width, fmt->fmt.pix.height);
	return 0;
}

static int bcwc_v4l2_ioctl_reqbufs(struct file *filp, void *_priv,
		struct v4l2_requestbuffers *rb)
{
	struct bcwc_private *priv = video_drvdata(filp);
	return videobuf_reqbufs(&priv->vb_queue, rb);
}

static int bcwc_v4l2_ioctl_querybuf(struct file *filp, void *_priv,
		struct v4l2_buffer *buf)
{
	struct bcwc_private *priv = video_drvdata(filp);
	return videobuf_querybuf(&priv->vb_queue, buf);
}

static int bcwc_v4l2_ioctl_qbuf(struct file *filp, void *_priv,
				struct v4l2_buffer *buf)
{
	struct bcwc_private *priv = video_drvdata(filp);
	return videobuf_qbuf(&priv->vb_queue, buf);
}

static int bcwc_v4l2_ioctl_dqbuf(struct file *filp, void *_priv,
				 struct v4l2_buffer *buf)
{
	struct bcwc_private *priv = video_drvdata(filp);
	return videobuf_dqbuf(&priv->vb_queue, buf, filp->f_flags & O_NONBLOCK);
}

static int bcwc_v4l2_ioctl_streamon(struct file *filp, void *priv, enum v4l2_buf_type t)
{
	pr_debug("%s\n", __FUNCTION__);
	return -ENODEV;
}

static int bcwc_v4l2_ioctl_streamoff(struct file *filp, void *priv, enum v4l2_buf_type t)
{
	pr_debug("%s\n", __FUNCTION__);
	return -ENODEV;
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
	pr_debug("%s\n", __FUNCTION__);
	return -ENODEV;
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
	.vidioc_reqbufs         = bcwc_v4l2_ioctl_reqbufs,

	.vidioc_querybuf        = bcwc_v4l2_ioctl_querybuf,
	.vidioc_qbuf            = bcwc_v4l2_ioctl_qbuf,
	.vidioc_dqbuf           = bcwc_v4l2_ioctl_dqbuf,

	.vidioc_streamon        = bcwc_v4l2_ioctl_streamon,
	.vidioc_streamoff       = bcwc_v4l2_ioctl_streamoff,
	.vidioc_g_parm          = bcwc_v4l2_ioctl_g_parm,
	.vidioc_s_parm          = bcwc_v4l2_ioctl_s_parm,
	.vidioc_enum_framesizes = bcwc_v4l2_ioctl_enum_framesizes,
	.vidioc_enum_frameintervals = bcwc_v4l2_ioctl_enum_frameintervals,
};

int bcwc_v4l2_register(struct bcwc_private *dev_priv)
{
	struct v4l2_device *v4l2_dev = &dev_priv->v4l2_dev;
	struct video_device *vdev;
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

	vdev->v4l2_dev = v4l2_dev;
	strcpy(vdev->name, "Apple Facetime HD"); // XXX: Length?
	vdev->vfl_dir = VFL_DIR_RX;
	vdev->fops = &bcwc_vdev_fops;
	vdev->ioctl_ops = &bcwc_ioctl_ops;
	vdev->release = video_device_release;
	pr_info("%s: dev_priv %p\n", __FUNCTION__, dev_priv);
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
	video_unregister_device(dev_priv->videodev);
	v4l2_device_unregister(&dev_priv->v4l2_dev);
}
