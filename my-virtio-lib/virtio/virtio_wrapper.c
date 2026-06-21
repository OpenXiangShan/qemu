#include <stdint.h>
#include <string.h>
#include "virtio.h"
#include "virtio_wrapper.h"
#include "virtio_mmio.h"
#include "virtio_pci.h"
#include "virtio_blk.h"
#include "virtio_net.h"
#include "virtio_console.h"
#include "virtio_gpu.h"
#include "virtio_input.h"

/*
 * Copyright (c) 2026 Beijing Institute of Open Source Chip (BOSC)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

struct virtio_wrapper_dev_info {
	const char name[64];
	struct virtio_emulator *(*create)(void);
	struct virtio_pci_dev_type_info pci_info;
};

static struct virtio_wrapper_dev_info virtio_dev_info[] = {
	{
		VIRTIO_EMU_NAME_BLK,
		virtio_blk_emulator_create,
		{ .class_code = 0x01, .subclass = 0x80,
		  .device_cfg_len = sizeof(struct virtio_blk_config) },
	},
	{
		VIRTIO_EMU_NAME_NET,
		virtio_net_emulator_create,
		{ .class_code = 0x02,
		  .device_cfg_len = sizeof(struct virtio_net_config) },
	},
	{
		VIRTIO_EMU_NAME_CONSOLE,
		virtio_console_emulator_create,
		{ .class_code = 0x07, .subclass = 0x80,
		  .device_cfg_len = sizeof(struct virtio_console_config) },
	},
	{
		VIRTIO_EMU_NAME_GPU,
		virtio_gpu_emulator_create,
		{ .class_code = 0x03, .subclass = 0x80,
		  .device_cfg_len = sizeof(struct virtio_gpu_config) },
	},
	{
		VIRTIO_EMU_NAME_KEYBOARD,
		virtio_keyboard_emulator_create,
		{ .class_code = 0x09, .subclass = 0x00,
		  .device_cfg_len = sizeof(struct virtio_input_config) },
	},
	{
		VIRTIO_EMU_NAME_MOUSE,
		virtio_mouse_emulator_create,
		{ .class_code = 0x09, .subclass = 0x02,
		  .device_cfg_len = sizeof(struct virtio_input_config) },
	},
	{
		VIRTIO_EMU_NAME_TABLET,
		virtio_tablet_emulator_create,
		{ .class_code = 0x09, .subclass = 0x02,
		  .device_cfg_len = sizeof(struct virtio_input_config) },
	},
};

#define VIRTIO_DEV_INFO_CNT (sizeof(virtio_dev_info) / sizeof(virtio_dev_info[0]))

static const struct virtio_wrapper_dev_info *virtio_find_dev_info(const char *name)
{
	int i;

	if (!name)
		return NULL;

	for (i = 0; i < VIRTIO_DEV_INFO_CNT; i++) {
		if (!strcmp(virtio_dev_info[i].name, name))
			return &virtio_dev_info[i];
	}

	return NULL;
}

void virtio_process_req(virtio_handle_t handle)
{
	struct virtio_mmio_dev *dev = handle;
	struct libvirtio_callback *cb;

	if (!dev)
		return;

	cb = &dev->cb;
	if (!cb || !cb->process_req)
		return;

	cb->process_req(cb->data);
}

int virtio_receive(virtio_handle_t handle, void *buf, int len)
{
	struct virtio_mmio_dev *dev = handle;
	struct libvirtio_callback *cb;

	if (!dev)
		return -1;

	cb = &dev->cb;
	if (!cb || !cb->receive)
		return -1;

	return cb->receive(buf, len, cb->data);
}

int virtio_mmio_read(virtio_handle_t handle, uint64_t addr,
		     uint32_t *val, int len)
{
	struct virtio_mmio_dev *dev = handle;

	if (!dev)
		return -1;

	return virtio_dev_mmio_read(dev, (uint32_t)(addr - dev->start), val, len);
}

int virtio_mmio_write(virtio_handle_t handle, uint64_t addr, uint32_t val,
		      int len, int *is_doorbell)
{
	struct virtio_mmio_dev *dev = handle;

	if (!dev)
		return -1;

	return virtio_dev_mmio_write(dev, (uint32_t)(addr - dev->start), val, len,
				     is_doorbell);
}

void virtio_mmio_show_all(void)
{
	virtio_mmio_dev_show_all();
}

virtio_handle_t virtio_mmio_create(const char *name, uint64_t start, int len,
				   struct libvirtio_ops *ops, void *priv)
{
	struct virtio_mmio_dev *dev;
	const struct virtio_wrapper_dev_info *dev_info;
	struct virtio_emulator *emu;

	dev = virtio_dev_mmio_create(start, start + len, ops);
	if (!dev)
		return NULL;

	dev->priv = priv;

	dev_info = virtio_find_dev_info(name);
	emu = dev_info ? dev_info->create() : NULL;
	if (!emu) {
		if (ops && ops->mm_free)
			ops->mm_free((uint64_t)(uintptr_t)dev, sizeof(*dev));
		return NULL;
	}

	virtio_mmio_dev_connect(name, dev, emu);

	return dev;
}

virtio_handle_t virtio_pci_ecam_create(const char *name, uint64_t ecam_base,
				       uint64_t ecam_size,
				       uint64_t bar_base, uint64_t bar_size,
				       struct libvirtio_ops *ops, void *priv)
{
	return virtio_pci_ecam_dev_create(name, ecam_base, ecam_size, bar_base,
					  bar_size, ops, priv);
}

int virtio_pci_ecam_read(virtio_handle_t handle, uint64_t offset,
			 void *dst, int len)
{
	return virtio_pci_ecam_dev_read(handle, offset, dst, len);
}

int virtio_pci_ecam_write(virtio_handle_t handle, uint64_t offset,
			  void *src, int len)
{
	return virtio_pci_ecam_dev_write(handle, offset, src, len);
}

int virtio_pci_bar_read(virtio_handle_t ecam_handle,
			uint64_t offset, void *dst, int len)
{
	return virtio_pci_ecam_dev_bar_read(ecam_handle, offset, dst, len);
}

int virtio_pci_bar_write(virtio_handle_t ecam_handle,
			 uint64_t offset, void *src, int len,
			 int *is_doorbell)
{
	return virtio_pci_ecam_dev_bar_write(ecam_handle, offset, src, len,
					     is_doorbell);
}

virtio_handle_t virtio_pci_create(const char *name, virtio_handle_t ecam_handle,
				  uint32_t bdf)
{
	const struct virtio_wrapper_dev_info *dev_info;
	struct virtio_emulator *emu;

	dev_info = virtio_find_dev_info(name);
	if (!dev_info)
		return NULL;

	emu = dev_info->create();
	if (!emu)
		return NULL;

	return virtio_pci_dev_create(name, ecam_handle, bdf, emu,
				     &dev_info->pci_info);
}
