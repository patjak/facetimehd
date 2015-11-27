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

#ifndef _FTHD_V4L2_H
#define _FTHD_V4L2_H

#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <media/videobuf-dma-sg.h>
#include <media/v4l2-device.h>

struct fthd_fmt {
	struct v4l2_pix_format fmt;
	const char *desc;
	int range; /* CISP_COMMAND_CH_OUTPUT_CONFIG_SET */
	int planes;
	int x1; /* for CISP_CMD_CH_CROP_SET */
	int y1;
	int x2;
	int y2;
};

struct fthd_private;
extern int fthd_v4l2_register(struct fthd_private *dev_priv);
extern void fthd_v4l2_unregister(struct fthd_private *dev_priv);

#endif
