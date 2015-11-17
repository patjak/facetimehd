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

#ifndef _BCWC_PCIE_H
#define _PCWC_PCIE_H

#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <media/videobuf-dma-sg.h>
#include <media/v4l2-device.h>

struct bcwc_private;
extern int bcwc_v4l2_register(struct bcwc_private *dev_priv);
extern void bcwc_v4l2_unregister(struct bcwc_private *dev_priv);

#endif
