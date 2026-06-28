#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "virtio_wrapper.h"
#include "virtio_mmio.h"
#include "utils.h"

/**
 * Copyright (c) 2013 Anup Patel.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * @file virtio_blk.c
 * @author Anup Patel (anup@brainfault.org)
 * @brief VirtIO based block device Emulator.
 */

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

static LIST_HEAD(virtio_mmio_dev_list);

static int virtio_mmio_dev_notify(struct virtio_device *dev, uint32_t vq)
{
	struct virtio_mmio_dev *mdev = (void *)dev->vn_data;

	mdev->config.interrupt_state |= VMM_VIRTIO_MMIO_INT_VRING;

	return my_set_irq(dev);
}

static void virtio_mmio_reset_queue(struct virtio_mmio_dev *mdev, uint32_t vq)
{
	if (mdev->dev.emu && mdev->dev.emu->reset_vq) {
		mdev->dev.emu->reset_vq(&mdev->dev, vq);
	}
}

static void virtio_mmio_reset_transport(struct virtio_mmio_dev *mdev)
{
	if (mdev->dev.emu && mdev->dev.emu->reset) {
		mdev->dev.emu->reset(&mdev->dev);
	}

	virtio_clear_addr_trans_tables(&mdev->dev);

	mdev->config.host_features = 0;
	mdev->config.host_features_sel = 0;
	mdev->config.guest_features = 0;
	mdev->config.guest_features_sel = 0;
	mdev->config.queue_sel = 0;
	mdev->config.queue_num = 0;
	mdev->config.queue_align = 0;
	mdev->config.queue_pfn = 0;
	mdev->config.queue_notify = 0;
	mdev->config.interrupt_state = 0;
	mdev->config.interrupt_ack = 0;
	mdev->config.status = 0;
}

static int virtio_mmio_config_read(struct virtio_mmio_dev *mdev,
				   uint32_t offset, void *dst, uint32_t dst_len)
{
	if (dst_len != 4) {
		my_print(&mdev->dev, "%s: %s invalid length=%d\n", __FUNCTION__, mdev->dev.name, dst_len);
		return -1;
	}

	switch (offset) {
	case VMM_VIRTIO_MMIO_MAGIC_VALUE:
		*(u32 *)dst = *((u32 *)((void *)&mdev->config.magic[0]));
		break;
	case VMM_VIRTIO_MMIO_VERSION:
		*(u32 *)dst = *((u32 *)((void *)&mdev->config.version));
		break;
	case VMM_VIRTIO_MMIO_DEVICE_ID:
		*(u32 *)dst = *((u32 *)((void *)&mdev->config.device_id));
		break;
	case VMM_VIRTIO_MMIO_VENDOR_ID:
		*(u32 *)dst = *((u32 *)((void *)&mdev->config.vendor_id));
		break;
	case VMM_VIRTIO_MMIO_INTERRUPT_STATUS:
		*(u32 *)dst = *((u32 *)((void *)&mdev->config.interrupt_state));
		break;
	case VMM_VIRTIO_MMIO_HOST_FEATURES:
		if (mdev->config.host_features_sel == 0)
			*(u32 *)dst =
			(u32)mdev->dev.emu->get_host_features(&mdev->dev);
		else
			*(u32 *)dst =
			(u32)(mdev->dev.emu->get_host_features(&mdev->dev) >> 32);
		break;
	case VMM_VIRTIO_MMIO_QUEUE_PFN:
		*(u32 *)dst = mdev->dev.emu->get_pfn_vq(&mdev->dev,
					     mdev->config.queue_sel);
		break;
	case VMM_VIRTIO_MMIO_QUEUE_NUM_MAX:
		*(u32 *)dst = mdev->dev.emu->get_size_vq(&mdev->dev,
					      mdev->config.queue_sel);
		break;
	case VMM_VIRTIO_MMIO_SHM_LEN_LOW:
	case VMM_VIRTIO_MMIO_SHM_LEN_HIGH:
		*(u32 *)dst = UINT_MAX;
		break;
	case VMM_VIRTIO_MMIO_SHM_BASE_LOW:
	case VMM_VIRTIO_MMIO_SHM_BASE_HIGH:
		*(u32 *)dst = 0;
		break;
	case VMM_VIRTIO_MMIO_STATUS:
		*(u32 *)dst = *((u32 *)((void *)&mdev->config.status));
		break;
	default:
		my_print(&mdev->dev, "%s: %s invalid offset=0x%x\n",
			 __FUNCTION__, mdev->dev.name, offset);
		return -1;
	}

	return 0;
}

static int virtio_mmio_config_write(struct virtio_mmio_dev *mdev,
				    uint32_t offset, void *src,
				    uint32_t src_len, int *is_doorbell)
{
	int ret = 0;
	u32 val = *(u32 *)(src);

	if (src_len != 4) {
		my_print(&mdev->dev, "%s: guest=%s invalid length=%d\n",
			 __FUNCTION__, mdev->dev.name, src_len);
		return -1;
	}

	switch (offset) {
	case VMM_VIRTIO_MMIO_HOST_FEATURES_SEL:
		mdev->config.host_features_sel = val;
		break;
	case VMM_VIRTIO_MMIO_GUEST_FEATURES_SEL:
		mdev->config.guest_features_sel = val;
		break;
	case VMM_VIRTIO_MMIO_GUEST_FEATURES:
		virtio_device_set_guest_features(&mdev->dev,
					mdev->config.guest_features_sel, val);
		mdev->dev.emu->set_guest_features(&mdev->dev,
					mdev->config.guest_features_sel, val);
		break;
	case VMM_VIRTIO_MMIO_GUEST_PAGE_SIZE:
		mdev->config.guest_page_size = val;
		break;
	case VMM_VIRTIO_MMIO_QUEUE_SEL:
		mdev->config.queue_sel = val;
		break;
	case VMM_VIRTIO_MMIO_QUEUE_NUM:
		mdev->config.queue_num = val;
		mdev->dev.emu->set_size_vq(&mdev->dev,
					mdev->config.queue_sel,
					mdev->config.queue_num);
		break;
	case VMM_VIRTIO_MMIO_QUEUE_ALIGN:
		mdev->config.queue_align = val;
		break;
	case VMM_VIRTIO_MMIO_QUEUE_PFN:
		if (!val) {
			virtio_mmio_reset_queue(mdev, mdev->config.queue_sel);
			mdev->config.queue_pfn = 0;
			break;
		}
		mdev->config.queue_pfn = val;
		ret = mdev->dev.emu->init_vq(&mdev->dev,
				    mdev->config.queue_sel,
				    mdev->config.guest_page_size,
				    mdev->config.queue_align,
				    val);
		break;
	case VMM_VIRTIO_MMIO_QUEUE_NOTIFY:
		if (!mdev->dev.emu->notify_vq(&mdev->dev, val))
			*is_doorbell = 1;
		break;
	case VMM_VIRTIO_MMIO_SHM_SEL:
		break;
	case VMM_VIRTIO_MMIO_INTERRUPT_ACK:
		mdev->config.interrupt_state &= ~val;
		//vmm_devemu_emulate_irq(mdev->guest, mdev->irq, 0);
		break;
	case VMM_VIRTIO_MMIO_STATUS:
		if (!val) {
			virtio_mmio_reset_transport(mdev);
			break;
		}
		if (val != mdev->config.status) {
			mdev->dev.emu->status_changed(&mdev->dev, val);
		}
		mdev->config.status = val;
		break;
	default:
		my_print(&mdev->dev, "%s: guest=%s invalid offset=0x%x\n",
			 __FUNCTION__, mdev->dev.name, offset);
		ret = -1;
		break;
	};

	return ret;
}

static int virtio_config_read(struct virtio_mmio_dev *mdev,
			      uint32_t offset, void *dst, uint32_t dst_len)
{
	struct virtio_device *vdev = &mdev->dev;

	if (vdev->emu && vdev->emu->read_config)
		return vdev->emu->read_config(vdev, offset, dst, dst_len);

	return 0;
}

static int virtio_config_write(struct virtio_mmio_dev *mdev,
			       uint32_t offset, void *src, uint32_t src_len)
{
	struct virtio_device *vdev = &mdev->dev;

	if (vdev->emu && vdev->emu->write_config)
		return vdev->emu->write_config(vdev, offset, src, src_len);

	return 0;
}

int virtio_dev_mmio_write(struct virtio_mmio_dev *mdev,
			  uint32_t offset, uint32_t val,
			  uint32_t len, int *is_doorbell)
{
//	my_print(&mdev->dev, "%s offset:0x%x val:0x%x len:%d\n", __FUNCTION__, offset, val, len);

	/* Device specific config write */
	if (offset >= VMM_VIRTIO_MMIO_CONFIG) {
		offset -= VMM_VIRTIO_MMIO_CONFIG;
		return virtio_config_write(mdev, offset, &val, len);
	}

	return virtio_mmio_config_write(mdev, offset, &val, len, is_doorbell);
}

int virtio_dev_mmio_read(struct virtio_mmio_dev *mdev,
			 uint32_t offset, uint32_t *val, uint32_t len)
{
//	my_print(&mdev->dev, "%s offset:0x%x len:%d\n", __FUNCTION__, offset, len);

	/* Device specific config write */
	if (offset >= VMM_VIRTIO_MMIO_CONFIG) {
		offset -= VMM_VIRTIO_MMIO_CONFIG;
		return virtio_config_read(mdev, offset, val, len);
	}

	return virtio_mmio_config_read(mdev, offset, val, len);
}

void virtio_mmio_dev_connect(const char *name, struct virtio_mmio_dev *mdev,
			     struct virtio_emulator *emu)
{
	strcpy(mdev->dev.name, name);
	mdev->dev.emu = emu;
	mdev->dev.guest_features = 0;
	mdev->config.device_id = emu->id_table[0].type;
	if (emu && emu->connect)
		emu->connect(&mdev->dev, emu);
}

static struct virtio_notify mmio_notify = {
	.name = "virtio_mmio",
	.notify = virtio_mmio_dev_notify,
};

void virtio_mmio_dev_show_all(void)
{
	struct virtio_mmio_dev *mdev;
	int idx = 0;

	list_for_each_entry(mdev, &virtio_mmio_dev_list, list) {
		struct virtio_device *dev = &mdev->dev;
		struct virtio_mmio_config *config = &mdev->config;

		my_print(dev, "####### virtio_%d #######\n", idx++);
		my_print(dev, "%s [0x%lx - 0x%lx]\n", dev->name, mdev->start, mdev->end);
		my_print(dev, "config:\n");
		my_print(dev, "       magic: \"%c\" \"%c\" \"%c\" \"%c\"\n",
			 config->magic[0], config->magic[1], config->magic[2], config->magic[3]);
		my_print(dev, "       version: %d\n", config->version);
		my_print(dev, "       device_id: 0x%x\n", config->device_id);
		my_print(dev, "       vendor_id: 0x%x\n", config->vendor_id);
		my_print(dev, "\n");
	}
}

struct virtio_mmio_dev *virtio_dev_mmio_create(uint64_t start, uint64_t end,
					       struct libvirtio_ops *ops)
{
	struct virtio_mmio_dev *mdev;

	list_for_each_entry(mdev, &virtio_mmio_dev_list, list) {
		if (memory_region_is_overlay(mdev->start, mdev->end,
					     start, end))
			return NULL;
	}

	if (!ops || !ops->mm_alloc)
		return NULL;
	mdev = (struct virtio_mmio_dev *)ops->mm_alloc(sizeof(*mdev));
	if (!mdev)
		return NULL;

	mdev->start = start;
	mdev->end = end;

	mdev->ops = ops;

	mdev->dev.vn = &mmio_notify;
	mdev->dev.vn_data = (void *)mdev;

	mdev->config = (struct virtio_mmio_config) {
		.magic          = {'v', 'i', 'r', 't'},
		.version        = 1,
		.vendor_id      = 0x52535658, /* XVSR */
		.queue_num_max  = 256,
	};

	INIT_LIST_HEAD(&mdev->dev.addr_trans_tables);

	list_add_tail(&mdev->list, &virtio_mmio_dev_list);

	return mdev;
}
