/*
 * Broadcom PCIe 1570 webcam driver
 *
 * Copyright (C) 2014 Patrik Jakobsson (patrik.r.jakobsson@gmail.com)
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

#ifndef _ISP_H
#define _ISP_H

/* ISP memory types */
#define FTHD_MEM_FIRMWARE	1
#define FTHD_MEM_HEAP		2
#define FTHD_MEM_IPC_QUEUE	3
#define FTHD_MEM_FW_ARGS        4
#define FTHD_MEM_CMD            5
#define FTHD_MEM_SHAREDMALLOC   6

#define FTHD_MEM_SIZE		0x8000000	/* 128mb */
#define FTHD_MEM_FW_SIZE	0x800000	/* 8mb */

enum bcwc_isp_cmds {
    CISP_CMD_START=0,
    CISP_CMD_STOP=1,
    CISP_CMD_PRINT_ENABLE=4,
    CISP_CMD_POWER_DOWN=10,
};

struct isp_mem_obj {
	struct resource base;
	unsigned int type;
	resource_size_t size;
	resource_size_t size_aligned;
	unsigned long offset;
};

struct isp_fw_args {
	u32 __unknown;
	u32 fw_arg;
	u32 full_stats_mode;
};

struct isp_channel_info {
	char name[64]; /* really that big? */
	u32 type;
	u32 source;
	u32 size;
	u32 offset;
};

struct isp_cmd_hdr {
	u32 unknown0;
	enum bcwc_isp_cmds opcode;
} __attribute__((packed));

struct isp_cmd_print_enable {
	struct isp_cmd_hdr hdr;
	u32 enable;
} __attribute__((packed));

#define to_isp_mem_obj(x) container_of((x), struct isp_mem_obj, base)

extern int isp_init(struct bcwc_private *dev_priv);
extern int isp_uninit(struct bcwc_private *dev_priv);

extern int isp_mem_init(struct bcwc_private *dev_priv);
extern struct isp_mem_obj *isp_mem_create(struct bcwc_private *dev_priv,
					  unsigned int type,
					  resource_size_t size);
extern int isp_mem_destroy(struct isp_mem_obj *obj);
extern int bcwc_isp_cmd_start(struct bcwc_private *dev_priv);
extern int bcwc_isp_cmd_stop(struct bcwc_private *dev_priv);
extern int bcwc_isp_cmd_print_enable(struct bcwc_private *dev_priv, int enable);
#endif
