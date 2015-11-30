/*
 * FacetimeHD camera driver
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
#include <media/v4l2-ctrls.h>
#include <media/videobuf2-dma-sg.h>
#include "fthd_drv.h"
#include "fthd_sysfs.h"
#include "fthd_isp.h"
#include "fthd_ringbuf.h"
#include "fthd_hw.h"

static ssize_t fthd_store_debug(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct fthd_isp_debug_cmd cmd;
	struct fthd_private *dev_priv = dev_get_drvdata(dev);
	int ret, opcode;

	if (count == 0)
		return 0;

	if (count > 64)
		return -EINVAL;

	memset(&cmd, 0, sizeof(cmd));

	if (!strcmp(buf, "ps"))
		opcode = CISP_CMD_DEBUG_PS;
	else if (!strcmp(buf, "banner"))
		opcode = CISP_CMD_DEBUG_BANNER;
	else if (!strcmp(buf, "get_root"))
		opcode = CISP_CMD_DEBUG_GET_ROOT_HANDLE;
	else if (!strcmp(buf, "heap"))
		opcode = CISP_CMD_DEBUG_HEAP_STATISTICS;
	else if (!strcmp(buf, "irq"))
		opcode = CISP_CMD_DEBUG_IRQ_STATISTICS;
	else if (!strcmp(buf, "semaphore"))
		opcode = CISP_CMD_DEBUG_SHOW_SEMAPHORE_STATUS;
	else if (!strcmp(buf, "wiring"))
		opcode = CISP_CMD_DEBUG_SHOW_WIRING_OPERATIONS;
	else if (sscanf(buf, "get_object_by_name %s", (char *)&cmd.arg) == 1)
		opcode = CISP_CMD_DEBUG_GET_OBJECT_BY_NAME;
	else if (sscanf(buf, "dump_object %x", &cmd.arg[0]) == 1)
		opcode = CISP_CMD_DEBUG_DUMP_OBJECT;
	else if (!strcmp(buf, "dump_objects"))
		opcode = CISP_CMD_DEBUG_DUMP_ALL_OBJECTS;
	else if (!strcmp(buf, "show_objects"))
		opcode = CISP_CMD_DEBUG_SHOW_OBJECT_GRAPH;
	else if (sscanf(buf, "get_debug_level %i", &cmd.arg[0]) == 1)
		opcode = CISP_CMD_DEBUG_GET_DEBUG_LEVEL;
	else if (sscanf(buf, "set_debug_level %x %i", &cmd.arg[0], &cmd.arg[1]) == 2)
		opcode = CISP_CMD_DEBUG_SET_DEBUG_LEVEL;
	else if (sscanf(buf, "set_debug_level_rec %x %i", &cmd.arg[0], &cmd.arg[1]) == 2)
		opcode = CISP_CMD_DEBUG_SET_DEBUG_LEVEL_RECURSIVE;
	else if (!strcmp(buf, "get_fsm_count"))
		opcode = CISP_CMD_DEBUG_GET_FSM_COUNT;
	else if (sscanf(buf, "get_fsm_by_name %s", (char *)&cmd.arg[0]) == 1)
		opcode = CISP_CMD_DEBUG_GET_FSM_BY_NAME;
	else if (sscanf(buf, "get_fsm_by_index %i", &cmd.arg[0]) == 1)
		opcode = CISP_CMD_DEBUG_GET_FSM_BY_INDEX;
	else if (sscanf(buf, "get_fsm_debug_level %x", &cmd.arg[0]) == 1)
		opcode = CISP_CMD_DEBUG_GET_FSM_DEBUG_LEVEL;
	else if (sscanf(buf, "set_fsm_debug_level %x", &cmd.arg[0]) == 2)
		opcode = CISP_CMD_DEBUG_SET_FSM_DEBUG_LEVEL;

	else if (sscanf(buf, "%i %i\n", &opcode, &cmd.arg[0]) != 2)
		return -EINVAL;
	cmd.show_errors = 1;

	ret = fthd_isp_debug_cmd(dev_priv, opcode, &cmd, sizeof(cmd), NULL);
	if (ret)
		return ret;

	return count;
}


static ssize_t fthd_dump_channel(struct device *dev, struct fw_channel *chan,
				 char *buf)
{
	struct fthd_private *dev_priv = dev_get_drvdata(dev);
	int i;
	char pos;
	u32 entry;
	ssize_t ret = 0, len;
	spin_lock_irq(&chan->lock);
	for( i = 0; i < chan->size; i++) {
		if (chan->ringbuf.idx == i)
			pos = '*';
		else
			pos = ' ';
		entry = get_entry_addr(dev_priv, chan, i);
		len = sprintf(buf+ret, "%c%3.3d: ADDRESS %08x REQUEST_SIZE %08x RESPONSE_SIZE %08x\n",
			 pos, i,
			 FTHD_S2_MEM_READ(entry + FTHD_RINGBUF_ADDRESS_FLAGS),
			 FTHD_S2_MEM_READ(entry + FTHD_RINGBUF_REQUEST_SIZE),
			 FTHD_S2_MEM_READ(entry + FTHD_RINGBUF_RESPONSE_SIZE));
		if (len < 0) {
			ret = len;
			break;
		} else {
			ret += len;
		}
	}
	spin_unlock_irq(&chan->lock);
	return ret;
}

static ssize_t channel_terminal_show(struct device *dev, struct device_attribute *attr,
				     char *buf)
{
	struct fthd_private *dev_priv = dev_get_drvdata(dev);
	return fthd_dump_channel(dev, dev_priv->channel_io, buf);
}

static ssize_t channel_sharedmalloc_show(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	struct fthd_private *dev_priv = dev_get_drvdata(dev);
	return fthd_dump_channel(dev, dev_priv->channel_shared_malloc, buf);
}

static ssize_t channel_io_show(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	struct fthd_private *dev_priv = dev_get_drvdata(dev);
	return fthd_dump_channel(dev, dev_priv->channel_io, buf);
}

static ssize_t channel_debug_show(struct device *dev, struct device_attribute *attr,
				  char *buf)
{
	struct fthd_private *dev_priv = dev_get_drvdata(dev);
	return fthd_dump_channel(dev, dev_priv->channel_debug, buf);
}

static ssize_t channel_buf_h2t_show(struct device *dev, struct device_attribute *attr,
				    char *buf)
{
	struct fthd_private *dev_priv = dev_get_drvdata(dev);
	return fthd_dump_channel(dev, dev_priv->channel_buf_h2t, buf);
}

static ssize_t channel_buf_t2h_show(struct device *dev, struct device_attribute *attr,
				    char *buf)
{
	struct fthd_private *dev_priv = dev_get_drvdata(dev);
	return fthd_dump_channel(dev, dev_priv->channel_buf_t2h, buf);
}

static ssize_t channel_io_t2h_show(struct device *dev, struct device_attribute *attr,
				   char *buf)
{
	struct fthd_private *dev_priv = dev_get_drvdata(dev);
	return fthd_dump_channel(dev, dev_priv->channel_io_t2h, buf);
}

static DEVICE_ATTR(debug, S_IWUSR | S_IRUGO, NULL, fthd_store_debug);
static DEVICE_ATTR_RO(channel_terminal);
static DEVICE_ATTR_RO(channel_sharedmalloc);
static DEVICE_ATTR_RO(channel_io);
static DEVICE_ATTR_RO(channel_debug);
static DEVICE_ATTR_RO(channel_buf_h2t);
static DEVICE_ATTR_RO(channel_buf_t2h);
static DEVICE_ATTR_RO(channel_io_t2h);

static struct attribute *fthd_attributes[] = {
	&dev_attr_debug.attr,
	&dev_attr_channel_terminal.attr,
	&dev_attr_channel_sharedmalloc.attr,
	&dev_attr_channel_io.attr,
	&dev_attr_channel_debug.attr,
	&dev_attr_channel_buf_h2t.attr,
	&dev_attr_channel_buf_t2h.attr,
	&dev_attr_channel_io_t2h.attr,
	NULL,
};

static struct attribute_group fthd_attribute_group = {
	.attrs = fthd_attributes,
};

int fthd_sysfs_init(struct fthd_private *dev_priv)
{
	return sysfs_create_group(&dev_priv->pdev->dev.kobj, &fthd_attribute_group);
}

void fthd_sysfs_exit(struct fthd_private *dev_priv)
{
	sysfs_remove_group(&dev_priv->pdev->dev.kobj, &fthd_attribute_group);
}
