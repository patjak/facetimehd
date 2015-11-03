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

#include <linux/delay.h>
#include <linux/acpi.h>
#include <linux/firmware.h>
#include "bcwc_drv.h"
#include "bcwc_hw.h"
#include "bcwc_reg.h"
#include "bcwc_ringbuf.h"
#include "bcwc_isp.h"

int isp_mem_init(struct bcwc_private *dev_priv)
{
	struct resource *root = &dev_priv->pdev->resource[BCWC_PCI_S2_MEM];

        dev_priv->mem = kzalloc(sizeof(struct resource), GFP_KERNEL);
	if (!dev_priv->mem)
	    return -ENOMEM;

	dev_priv->mem->start = root->start;
	dev_priv->mem->end = root->end;

	/* Preallocate 8mb for the firmware */
	dev_priv->firmware = isp_mem_create(dev_priv, FTHD_MEM_FIRMWARE,
					    FTHD_MEM_FW_SIZE);

	if (!dev_priv->firmware) {
		dev_err(&dev_priv->pdev->dev,
			"Failed to preallocate firmware memory\n");
		return -ENOMEM;
	}
	return 0;
}

struct isp_mem_obj *isp_mem_create(struct bcwc_private *dev_priv,
				   unsigned int type, resource_size_t size)
{
	struct isp_mem_obj *obj;
	struct resource *root = dev_priv->mem;
	int ret;

	obj = kzalloc(sizeof(struct isp_mem_obj), GFP_KERNEL);
	if (!obj)
		return NULL;

	obj->type = type;
	obj->base.name = "S2 ISP";
	ret = allocate_resource(root, &obj->base, size, root->start, root->end,
				PAGE_SIZE, NULL, NULL);
	if (ret) {
		dev_err(&dev_priv->pdev->dev,
			"Failed to allocate resource (size: %Ld, start: %Ld, end: %Ld)\n",
			size, root->start, root->end);
		kfree(obj);
		obj = NULL;
	}

	obj->offset = obj->base.start - root->start;
	obj->size = size;
	obj->size_aligned = obj->base.end - obj->base.start;
	return obj;
}

int isp_mem_destroy(struct isp_mem_obj *obj)
{
	if (obj) {
		release_resource(&obj->base);
		kfree(obj);
		obj = NULL;
	}

	return 0;
}

int isp_acpi_set_power(struct bcwc_private *dev_priv, int power)
{
	acpi_status status;
	acpi_handle handle;
	struct acpi_object_list arg_list;
	struct acpi_buffer buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	union acpi_object args[1];
	union acpi_object *result;
	int ret = 0;


	handle = ACPI_HANDLE(&dev_priv->pdev->dev);
	if(!handle) {
		dev_err(&dev_priv->pdev->dev,
			"Failed to get S2 CMPE ACPI handle\n");
		ret = -ENODEV;
		goto out;
	}

	args[0].type = ACPI_TYPE_INTEGER;
	args[0].integer.value = power;

	arg_list.count = 1;
	arg_list.pointer = args;

	status = acpi_evaluate_object(handle, "CMPE", &arg_list, &buffer);
	if (ACPI_FAILURE(status)) {
		dev_err(&dev_priv->pdev->dev,
			"Failed to execute S2 CMPE ACPI method\n");
		ret = -ENODEV;
		goto out;
	}

	result = buffer.pointer;

	if (result->type != ACPI_TYPE_INTEGER || result->integer.value != 0) {
		dev_err(&dev_priv->pdev->dev,
			"Invalid ACPI response (len: %Ld)\n", buffer.length);
		ret = -EINVAL;
	}

out:
	kfree(buffer.pointer);
	return ret;
}

static int isp_enable_sensor(struct bcwc_private *dev_priv)
{
	return 0;
}

static int isp_load_firmware(struct bcwc_private *dev_priv)
{
	const struct firmware *fw;
	int ret = 0;

	ret = request_firmware(&fw, "fthd.bin", &dev_priv->pdev->dev);
	if (ret)
		return ret;

	/* Firmware memory is preallocated at init time */
	if (!dev_priv->firmware)
		return -ENOMEM;

	if (dev_priv->firmware->base.start != dev_priv->mem->start) {
		dev_err(&dev_priv->pdev->dev,
			"Misaligned firmware memory object (offset: %lu)\n",
			dev_priv->firmware->offset);
		isp_mem_destroy(dev_priv->firmware);
		return -EBUSY;
	}

	memcpy(dev_priv->s2_mem + dev_priv->firmware->offset, fw->data,
	       fw->size);

	/* Might need a flush here if we map ISP memory cached */

	dev_info(&dev_priv->pdev->dev, "Loaded firmware, size: %lukb\n",
		 fw->size / 1024);

	release_firmware(fw);

	return ret;
}

static void isp_free_channel_info(struct bcwc_private *priv)
{
	struct fw_channel *chan;
	int i;
	for(i = 0; i < priv->num_channels; i++) {
		chan = priv->channels[i];
		if (!chan)
			continue;

		kfree(chan->name);
		kfree(chan);
		priv->channels[i] = NULL;
	}
	kfree(priv->channels);
	priv->channels = NULL;
}

static struct fw_channel *isp_get_chan_index(struct bcwc_private *priv, const char *name)
{
	int i;
	for(i = 0; i < priv->num_channels; i++) {
		if (!strcasecmp(priv->channels[i]->name, name))
			return priv->channels[i];
	}
	return NULL;
}

static int isp_fill_channel_info(struct bcwc_private *priv, int offset, int num_channels)
{
	struct isp_channel_info *info;
	struct fw_channel *chan;
	int i;

	if (!num_channels)
		return 0;

	priv->num_channels = num_channels;
	priv->channels = kzalloc(num_channels * sizeof(struct fw_channel *), GFP_KERNEL);
	if (!priv->channels)
		goto out;

	for(i = 0; i < num_channels; i++) {
		info = (struct isp_channel_info *)(priv->s2_mem + offset + i * 256);

		chan = kzalloc(sizeof(struct fw_channel), GFP_KERNEL);
		if (!chan)
			goto out;

		priv->channels[i] = chan;

		pr_debug("Channel %d: %s, type %d, source %d, size %d, offset %x\n",
			 i, info->name, info->type, info->source, info->size, info->offset);

		chan->name = kstrdup(info->name, GFP_KERNEL);
		if (!chan->name)
			goto out;

		chan->type = info->type;
		chan->source = info->source;
		chan->size = info->size;
		chan->offset = info->offset;
	}

	priv->channel_terminal = isp_get_chan_index(priv, "TERMINAL");
	priv->channel_debug = isp_get_chan_index(priv, "DEBUG");
	priv->channel_shared_malloc = isp_get_chan_index(priv, "SHAREDMALLOC");
	priv->channel_io = isp_get_chan_index(priv, "IO");
	priv->channel_buf_h2t = isp_get_chan_index(priv, "BUF_H2T");
	priv->channel_buf_t2h = isp_get_chan_index(priv, "BUF_T2H");
	priv->channel_io_t2h = isp_get_chan_index(priv, "IO_T2H");

	if (!priv->channel_terminal || !priv->channel_debug
	    || !priv->channel_shared_malloc || !priv->channel_io
	    || !priv->channel_buf_h2t || !priv->channel_buf_t2h
	    || !priv->channel_io_t2h) {
		dev_err(&priv->pdev->dev, "did not find all of the required channels\n");
		goto out;
	}
	return 0;
out:
	isp_free_channel_info(priv);
	return -ENOMEM;
}

int bcwc_isp_cmd(struct bcwc_private *dev_priv, enum bcwc_isp_cmds command, void *in,
			int request_len, void *out, int *response_len)
{
	struct isp_mem_obj *request;
	struct isp_cmd_hdr *cmd;
	struct bcwc_ringbuf_entry *entry;
	int len, ret;

	if (response_len) {
		len = MAX(request_len, *response_len);
		/* XXX: not needed, for debugging */
		memset(out, 0, *response_len);
	} else {
		len = request_len;
	}
	pr_debug("sending cmd %d to firmware\n", command);

	request = isp_mem_create(dev_priv, FTHD_MEM_CMD, sizeof(cmd) + len);
	if (!request) {
		dev_err(&dev_priv->pdev->dev, "failed to allocate cmd memory object\n");
		return -ENOMEM;
	}

	cmd = dev_priv->s2_mem + request->offset;
	memset(cmd, 0, len);
	cmd->opcode = command;

	if (request_len)
		memcpy(cmd + sizeof(struct isp_cmd_hdr), in, request_len);

	entry = bcwc_channel_ringbuf_send(dev_priv, dev_priv->channel_io,
		request->offset, request_len + 8, (response_len ? *response_len : 0) + 8);

	if (command == CISP_CMD_POWER_DOWN) {
		/* powerdown doesn't seem to generate a response */
		ret = 0;
		goto out;
	}

	if (!wait_event_interruptible_timeout(dev_priv->wq, (entry->address_flags & 1), 5 * HZ)) {
		dev_err(&dev_priv->pdev->dev, "timeout wait for command %d\n", cmd->opcode);
		bcwc_channel_ringbuf_dump(dev_priv, dev_priv->channel_io);
		if (response_len)
			*response_len = 0;
		ret = -ETIMEDOUT;
		goto out;
	}
	/* XXX: response size in the ringbuf is zero after command completion, how is buffer size
	        verification done? */
	if (response_len && *response_len)
		memcpy(out, (entry->address_flags & ~3) + dev_priv->s2_mem, *response_len);

	pr_debug("status %04x, request_len %d response len %d address_flags %x", cmd->status,
		entry->request_size, entry->response_size, entry->address_flags);

	ret = 0;
out:
	isp_mem_destroy(request);
	return ret;
}



int bcwc_isp_cmd_start(struct bcwc_private *dev_priv)
{
	pr_debug("sending start cmd to firmware\n");
	return bcwc_isp_cmd(dev_priv, CISP_CMD_START, NULL, 0, NULL, NULL);
}

int bcwc_isp_cmd_stop(struct bcwc_private *dev_priv)
{
	return bcwc_isp_cmd(dev_priv, CISP_CMD_STOP, NULL, 0, NULL, NULL);
}

int bcwc_isp_cmd_powerdown(struct bcwc_private *dev_priv)
{
	return bcwc_isp_cmd(dev_priv, CISP_CMD_POWER_DOWN, NULL, 0, NULL, NULL);
}

int isp_uninit(struct bcwc_private *dev_priv)
{
	int retries;
	u32 reg;
	BCWC_ISP_REG_WRITE(0xf7fbdff9, 0xc3000);
	bcwc_isp_cmd_powerdown(dev_priv);
	for (retries = 0; retries < 1000; retries++) {
		reg = BCWC_ISP_REG_READ(0xc3000);
		if (reg == 0x8042006)
			break;
		mdelay(10);
	}

	if (retries >= 1000) {
		dev_info(&dev_priv->pdev->dev, "deinit failed!\n");
	}

	BCWC_ISP_REG_WRITE(0xffffffff, 0xc0008);
	BCWC_ISP_REG_WRITE(0xffffffff, 0xc000c);
	BCWC_ISP_REG_WRITE(0xffffffff, 0xc0010);
	BCWC_ISP_REG_WRITE(0, 0xc0c04);
	BCWC_ISP_REG_WRITE(0xffffffff, 0xc0c0c);
	BCWC_ISP_REG_WRITE(0, 0xc0c14);
	BCWC_ISP_REG_WRITE(0xffffffff, 0xc0c1c);
	BCWC_ISP_REG_WRITE(0xffffffff, 0xc0c24);
	mdelay(1);

	BCWC_ISP_REG_WRITE(0, 0xc0000);
	BCWC_ISP_REG_WRITE(0, 0xc0004);
	BCWC_ISP_REG_WRITE(0, 0xc0008);
	BCWC_ISP_REG_WRITE(0, 0xc000c);
	BCWC_ISP_REG_WRITE(0, 0xc0010);
	BCWC_ISP_REG_WRITE(0, 0xc0014);
	BCWC_ISP_REG_WRITE(0, 0xc0018);
	BCWC_ISP_REG_WRITE(0, 0xc001c);
	BCWC_ISP_REG_WRITE(0, 0xc0020);
	BCWC_ISP_REG_WRITE(0, 0xc0024);

	BCWC_ISP_REG_WRITE(0xffffffff, 0x41024);
	isp_free_channel_info(dev_priv);
	kfree(dev_priv->mem);
	return 0;
}


int bcwc_isp_cmd_print_enable(struct bcwc_private *dev_priv, int enable)
{
	struct isp_cmd_print_enable cmd;

	cmd.enable = enable;

	return bcwc_isp_cmd(dev_priv, CISP_CMD_PRINT_ENABLE, &cmd, sizeof(cmd), NULL, NULL);
	print_hex_dump_bytes("PE RES", DUMP_PREFIX_OFFSET, &cmd, sizeof(cmd));
	return 0;
}

int bcwc_isp_cmd_set_loadfile(struct bcwc_private *dev_priv)
{
	struct isp_cmd_set_loadfile cmd;
	struct isp_mem_obj *file;

	pr_debug("set loadfile\n");

	memset(&cmd, 0, sizeof(cmd));

	file = isp_mem_create(dev_priv, FTHD_MEM_CMD, 1024*1024*16);
	if (!file) {
		dev_err(&dev_priv->pdev->dev, "failed to allocate cmd memory object\n");
		return -ENOMEM;
	}

	cmd.addr = file->offset;
	cmd.length = 16 * 1024 * 1024;
	return bcwc_isp_cmd(dev_priv, CISP_CMD_CH_SET_FILE_LOAD, &cmd, sizeof(cmd), NULL, NULL);
}

int bcwc_isp_cmd_channel_info(struct bcwc_private *dev_priv)
{
	struct isp_cmd_channel_info cmd, info;
	int ret, len;

	pr_debug("sending ch info\n");

	memset(&cmd, 0, sizeof(cmd));
	len = sizeof(info);
	ret = bcwc_isp_cmd(dev_priv, CISP_CMD_CH_INFO_GET, &cmd, 0, &info, &len);
	print_hex_dump_bytes("CHINFO ", DUMP_PREFIX_OFFSET, &info, sizeof(info));
	return ret;
}

int isp_init(struct bcwc_private *dev_priv)
{
	struct isp_mem_obj *ipc_queue, *heap, *fw_args;
	struct isp_fw_args *fw_args_data;
	u32 num_channels, queue_size, heap_size, reg, offset;
	int i, retries, ret;

	ret = isp_mem_init(dev_priv);
	if (ret)
		return ret;

	ret = isp_load_firmware(dev_priv);
	if (ret)
		return ret;

	isp_acpi_set_power(dev_priv, 1);
	mdelay(20);

	pci_set_power_state(dev_priv->pdev, PCI_D0);
	mdelay(10);

	isp_enable_sensor(dev_priv);
	BCWC_ISP_REG_WRITE(0, ISP_IPC_NUM_CHAN);
	BCWC_ISP_REG_WRITE(0, ISP_IPC_QUEUE_SIZE);
	BCWC_ISP_REG_WRITE(0, ISP_FW_SIZE);
	BCWC_ISP_REG_WRITE(0, ISP_FW_HEAP_SIZE);
	BCWC_ISP_REG_WRITE(0, ISP_FW_HEAP_ADDR);
	BCWC_ISP_REG_WRITE(0, ISP_FW_HEAP_SIZE2);
	BCWC_ISP_REG_WRITE(0, ISP_REG_C3018);
	BCWC_ISP_REG_WRITE(0, ISP_REG_C301C);

	BCWC_ISP_REG_WRITE(0xffffffff, ISP_REG_41024);

	/*
	 * Probably the IPC queue
	 * FIXME: Check if we can do 64bit writes on PCIe
	 */
	for (i = ISP_IPC_CHAN_START; i <= ISP_IPC_CHAN_END; i += 8) {
		BCWC_ISP_REG_WRITE(0xffffffff, i);
		BCWC_ISP_REG_WRITE(0, i + 4);
	}

	BCWC_ISP_REG_WRITE(0x80000000, ISP_REG_40008);
	BCWC_ISP_REG_WRITE(0x1, ISP_REG_40004);

	for (retries = 0; retries < 1000; retries++) {
		reg = BCWC_ISP_REG_READ(ISP_REG_41000);
		if ((reg & 0xf0) > 0)
			break;
		mdelay(10);
	}

	if (retries >= 1000) {
		dev_info(&dev_priv->pdev->dev, "Init failed! No wake signal\n");
		return -EIO;
	}

	dev_info(&dev_priv->pdev->dev, "ISP woke up after %dms\n",
		 (retries - 1) * 10);

	BCWC_ISP_REG_WRITE(0xffffffff, ISP_REG_41024);

	num_channels = BCWC_ISP_REG_READ(ISP_IPC_NUM_CHAN);
	queue_size = BCWC_ISP_REG_READ(ISP_IPC_QUEUE_SIZE) + 1;

	dev_info(&dev_priv->pdev->dev,
		 "Number of IPC channels: %u, queue size: %u\n",
		 num_channels, queue_size);

	if (num_channels > 32) {
		dev_info(&dev_priv->pdev->dev, "Too many IPC channels: %u\n",
			 num_channels);
		return -EIO;
	}

	ipc_queue = isp_mem_create(dev_priv, FTHD_MEM_IPC_QUEUE, queue_size);
	if (!ipc_queue)
		return -ENOMEM;

	/* Firmware heap max size is 4mb */
	heap_size = BCWC_ISP_REG_READ(ISP_FW_HEAP_SIZE);

	if (heap_size == 0) {
		BCWC_ISP_REG_WRITE(0, ISP_IPC_NUM_CHAN);
		BCWC_ISP_REG_WRITE(ipc_queue->offset, ISP_IPC_QUEUE_SIZE);
		BCWC_ISP_REG_WRITE(dev_priv->firmware->size_aligned, ISP_FW_SIZE);
		BCWC_ISP_REG_WRITE(0x10000000 - dev_priv->firmware->size_aligned,
				   ISP_FW_HEAP_SIZE);
		BCWC_ISP_REG_WRITE(0, ISP_FW_HEAP_ADDR);
		BCWC_ISP_REG_WRITE(0, ISP_FW_HEAP_SIZE2);
	} else {
		/* Must be at least 0x1000 bytes */
		heap_size = (heap_size < 0x1000) ? 0x1000 : heap_size;

		if (heap_size > 0x400000) {
			dev_info(&dev_priv->pdev->dev,
				 "Firmware heap request size too big (%ukb)\n",
				 heap_size / 1024);
			return -ENOMEM;
		}

		dev_info(&dev_priv->pdev->dev, "Firmware requested heap size: %ukb\n",
			 heap_size / 1024);

		heap = isp_mem_create(dev_priv, FTHD_MEM_HEAP, heap_size);
		if (!heap)
			return -ENOMEM;

		BCWC_ISP_REG_WRITE(0, ISP_IPC_NUM_CHAN);

		/* Set IPC queue base addr */
		BCWC_ISP_REG_WRITE(ipc_queue->offset, ISP_IPC_QUEUE_SIZE);

		BCWC_ISP_REG_WRITE(FTHD_MEM_FW_SIZE, ISP_FW_SIZE);

		BCWC_ISP_REG_WRITE(0x10000000 - FTHD_MEM_FW_SIZE, ISP_FW_HEAP_SIZE);

		BCWC_ISP_REG_WRITE(heap->offset, ISP_FW_HEAP_ADDR);

		BCWC_ISP_REG_WRITE(heap->size, ISP_FW_HEAP_SIZE2);

		/* Set FW args */
		fw_args = isp_mem_create(dev_priv, FTHD_MEM_FW_ARGS, sizeof(struct isp_fw_args));
		if (!fw_args)
			return -ENOMEM;

		fw_args_data = dev_priv->s2_mem + fw_args->offset;

		fw_args_data->__unknown = 2;
		fw_args_data->fw_arg = 0;
		fw_args_data->full_stats_mode = 0;

		BCWC_ISP_REG_WRITE(fw_args->offset, ISP_REG_C301C);

		BCWC_ISP_REG_WRITE(0x10, ISP_REG_41020);

		for (retries = 0; retries < 1000; retries++) {
			reg = BCWC_ISP_REG_READ(ISP_REG_41000);
			if ((reg & 0xf0) > 0)
				break;
			mdelay(10);
		}

		if (retries >= 1000) {
			dev_info(&dev_priv->pdev->dev, "Init failed! No second int\n");
			return -EIO;
		} /* FIXME: free on error path */

		dev_info(&dev_priv->pdev->dev, "ISP second int after %dms\n",
			 (retries - 1) * 10);

		offset = BCWC_ISP_REG_READ(ISP_IPC_NUM_CHAN);
		dev_info(&dev_priv->pdev->dev, "Channel description table at %08x\n", offset);
		ret = isp_fill_channel_info(dev_priv, offset, num_channels);
		if (ret)
			return ret;

		bcwc_channel_ringbuf_init(dev_priv, dev_priv->channel_terminal);
		bcwc_channel_ringbuf_init(dev_priv, dev_priv->channel_io);
		bcwc_channel_ringbuf_init(dev_priv, dev_priv->channel_debug);
		bcwc_channel_ringbuf_init(dev_priv, dev_priv->channel_buf_h2t);
		bcwc_channel_ringbuf_init(dev_priv, dev_priv->channel_buf_t2h);
		bcwc_channel_ringbuf_init(dev_priv, dev_priv->channel_shared_malloc);
		bcwc_channel_ringbuf_init(dev_priv, dev_priv->channel_io_t2h);

		BCWC_ISP_REG_WRITE(0x8042006, ISP_FW_HEAP_SIZE);

		for (retries = 0; retries < 1000; retries++) {
			reg = BCWC_ISP_REG_READ(ISP_FW_HEAP_SIZE);
			if (!reg)
				break;
			mdelay(10);
		}

		if (retries >= 1000) {
			dev_info(&dev_priv->pdev->dev, "Init failed! No magic value\n");
			isp_uninit(dev_priv);
			return -EIO;
		} /* FIXME: free on error path */
		dev_info(&dev_priv->pdev->dev, "magic value: %08x after %d ms\n", reg, (retries - 1) * 10);
	}

	return 0;
}
