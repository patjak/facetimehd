/*
 * Broadcom PCIe 1570 webcam driver
 * Some of the register defines are taken from the crystalhd driver
 *
 * Copyright (C) 2014 Patrik Jakobsson (patrik.r.jakobsson@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 */

#ifndef _ISP_H
#define _ISP_H

/* ISP memory types */
#define FTHD_MEM_FIRMWARE	1
#define FTHD_MEM_HEAP		2
#define FTHD_MEM_IPC_QUEUE	3

struct isp_mem_obj {
	struct resource base;
	unsigned int type;
	resource_size_t size;
	unsigned long offset;
};

#define to_isp_mem_obj(x) container_of((x), struct isp_mem_obj, base)

extern int isp_init(struct bcwc_private *dev_priv);

extern int isp_mem_init(struct bcwc_private *dev_priv);
extern struct isp_mem_obj *isp_mem_create(struct bcwc_private *dev_priv,
					  unsigned int type,
					  resource_size_t size);
extern int isp_mem_destroy(struct isp_mem_obj *obj);

#endif
