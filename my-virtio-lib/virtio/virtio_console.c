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
 * @file virtio_console.c
 * @author Anup Patel (anup@brainfault.org)
 * @brief VirtIO based console Emulator.
 */

#include <stddef.h>
#include <string.h>
#include "virtio_wrapper.h"
#include "virtio.h"
#include "virtio_mmio.h"
#include "virtio_console.h"
#include "virtio_ids.h"
#include "virtio_ring.h"
#include "utils.h"

#define VIRTIO_CONSOLE_QUEUE_SIZE	128
#define VIRTIO_CONSOLE_NUM_QUEUES	2
#define VIRTIO_CONSOLE_RX_QUEUE		0
#define VIRTIO_CONSOLE_TX_QUEUE		1

struct virtio_console_dev {
	struct virtio_device *vdev;

	struct virtio_queue vqs[VIRTIO_CONSOLE_NUM_QUEUES];
	struct virtio_iovec rx_iov[VIRTIO_CONSOLE_QUEUE_SIZE];
	struct virtio_iovec tx_iov[VIRTIO_CONSOLE_QUEUE_SIZE];
	struct virtio_console_config config;
	uint64_t features;
	uint32_t pending_queues;

	char name[64];
};

static uint64_t virtio_console_get_host_features(struct virtio_device *dev)
{
	return 1UL << VMM_VIRTIO_RING_F_EVENT_IDX |
	       1UL << VMM_VIRTIO_CONSOLE_F_EMERG_WRITE;
}

static void virtio_console_set_guest_features(struct virtio_device *dev,
					      uint32_t select, uint32_t features)
{

}

static int virtio_console_init_vq(struct virtio_device *dev,
				  uint32_t vq, uint32_t page_size, uint32_t align,
				  uint32_t pfn)
{
	int rc;
	struct virtio_console_dev *cdev = dev->emu_data;

	switch (vq) {
	case VIRTIO_CONSOLE_RX_QUEUE:
	case VIRTIO_CONSOLE_TX_QUEUE:
		rc = virtio_queue_setup(dev, &cdev->vqs[vq], pfn,
					page_size, VIRTIO_CONSOLE_QUEUE_SIZE, align);
		break;
	default:
		rc = -1;
		break;
	};

	return rc;
}

static int virtio_console_init_vq_addr(struct virtio_device *dev, uint32_t vq,
				       uint64_t desc_addr, uint64_t avail_addr,
				       uint64_t used_addr, uint32_t size)
{
	int rc;
	struct virtio_console_dev *cdev = dev->emu_data;

	switch (vq) {
	case VIRTIO_CONSOLE_RX_QUEUE:
	case VIRTIO_CONSOLE_TX_QUEUE:
		rc = virtio_queue_setup_addr(dev, &cdev->vqs[vq], desc_addr,
					    avail_addr, used_addr, size);
		break;
	default:
		rc = -1;
		break;
	};

	return rc;
}

static void virtio_console_reset_vq(struct virtio_device *dev, uint32_t vq)
{
	struct virtio_console_dev *cdev = dev->emu_data;

	if (!cdev || vq >= VIRTIO_CONSOLE_NUM_QUEUES)
		return;

	my_virtio_queue_reset(&cdev->vqs[vq]);
}

static int virtio_console_get_pfn_vq(struct virtio_device *dev, uint32_t vq)
{
	int rc;
	struct virtio_console_dev *cdev = dev->emu_data;

	switch (vq) {
	case VIRTIO_CONSOLE_RX_QUEUE:
	case VIRTIO_CONSOLE_TX_QUEUE:
		rc = virtio_queue_guest_pfn(&cdev->vqs[vq]);
		break;
	default:
		rc = -1;
		break;
	};

	return rc;
}

static int virtio_console_get_size_vq(struct virtio_device *dev, uint32_t vq)
{
	int rc;

	switch (vq) {
	case VIRTIO_CONSOLE_RX_QUEUE:
	case VIRTIO_CONSOLE_TX_QUEUE:
		rc = VIRTIO_CONSOLE_QUEUE_SIZE;
		break;
	default:
		rc = 0;
		break;
	};

	return rc;
}

static int virtio_console_set_size_vq(struct virtio_device *dev,
				      uint32_t vq, int size)
{
	return size;
}

static int virtio_console_do_tx(struct virtio_device *dev,
				struct virtio_console_dev *cdev)
{
	int rc;
	uint8_t buf[8];
	uint16_t head = 0;
	uint32_t i, len, iov_cnt = 0, total_len = 0;
	struct virtio_queue *vq = &cdev->vqs[VIRTIO_CONSOLE_TX_QUEUE];
	struct virtio_iovec *iov = cdev->tx_iov;
	struct virtio_iovec tiov;

	while (virtio_queue_available(vq)) {
		rc = virtio_queue_get_iovec(vq, iov,
					    &iov_cnt, &total_len, &head);
		if (rc) {
			my_print(dev, "%s: failed to get iovec (error %d)\n",
				 __FUNCTION__, rc);
			continue;
		}

		for (i = 0; i < iov_cnt; i++) {
			memcpy(&tiov, &iov[i], sizeof(tiov));
			while (tiov.len) {
				len = virtio_iovec_to_buf_read(dev, &tiov,
							1, &buf, sizeof(buf));
				my_console_send(dev, buf, len);
				tiov.addr += len;
				tiov.len -= len;
			}
		}

		virtio_queue_set_used_elem(vq, head, total_len);
	}

	if (virtio_queue_should_signal(vq)) {
		if (dev->vn && dev->vn->notify)
			dev->vn->notify(dev, VIRTIO_CONSOLE_TX_QUEUE);
	}

	return 0;
}

static int virtio_console_receive(void *buf, int len, void *priv)
{
	int rc;
	uint32_t copied, pos = 0;
	uint16_t head = 0;
	uint32_t iov_cnt = 0, total_len = 0;
	struct virtio_console_dev *cdev = priv;
	struct virtio_queue *vq = &cdev->vqs[VIRTIO_CONSOLE_RX_QUEUE];
	struct virtio_iovec *iov = cdev->rx_iov;
	struct virtio_device *dev = cdev->vdev;

	while (pos < len && virtio_queue_available(vq)) {
		rc = virtio_queue_get_iovec(vq, iov,
					    &iov_cnt, &total_len, &head);
		if (rc) {
			my_print(dev, "%s: failed to get iovec\n",
				 __FUNCTION__);
			return rc;
		}
		if (iov_cnt) {
			copied = virtio_buf_to_iovec_write(dev, iov, iov_cnt,
							   buf + pos,
							   len - pos);
			if (!copied)
				break;

			pos += copied;
			virtio_queue_set_used_elem(vq, head, copied);

			if (virtio_queue_should_signal(vq)) {
				if (dev->vn && dev->vn->notify) {
					dev->vn->notify(dev, VIRTIO_CONSOLE_RX_QUEUE);
				}
			}
		}
	}

	return pos;
}

static int virtio_console_notify_vq(struct virtio_device *dev, uint32_t vq)
{
	int rc = 0;
	struct virtio_console_dev *cdev = dev->emu_data;

	switch (vq) {
	case VIRTIO_CONSOLE_TX_QUEUE:
	case VIRTIO_CONSOLE_RX_QUEUE:
		cdev->pending_queues |= 1U << vq;
		break;
	default:
		rc = -1;
		break;
	}

	return rc;
}

static void virtio_console_req_process(void *data)
{
	struct virtio_console_dev *cdev = data;
	uint32_t pending;

	if (!cdev)
		return;

	pending = cdev->pending_queues;
	cdev->pending_queues = 0;

	if (pending & (1U << VIRTIO_CONSOLE_TX_QUEUE))
		virtio_console_do_tx(cdev->vdev, cdev);
}

static void virtio_console_status_changed(struct virtio_device *dev,
					  uint32_t new_status)
{

}

static int virtio_console_read_config(struct virtio_device *dev,
				      uint32_t offset, void *dst, uint32_t dst_len)
{
	struct virtio_console_dev *cdev = dev->emu_data;
	uint8_t *src = (uint8_t *)&cdev->config;
	uint32_t i, src_len = sizeof(cdev->config);

	if (offset == offsetof(struct virtio_console_config, emerg_wr)) {
		my_print(dev, "%s ToDo: emerg_wr\n", __FUNCTION__);
	} else {
		for (i = 0; (i < dst_len) && ((offset + i) < src_len); i++) {
			*((uint8_t *)dst + i) = src[offset + i];
		}
	}

	return 0;
}

static int virtio_console_write_config(struct virtio_device *dev,
				       uint32_t offset, void *src, uint32_t src_len)
{
	uint32_t data;
	uint8_t ch;

	if (offset == offsetof(struct virtio_console_config, emerg_wr)) {
		switch (src_len) {
		case 1:
			data = *(u8 *)src;
			break;
		case 2:
			data = *(u16 *)src;
			break;
		case 4:
			data = *(u32 *)src;
			break;
		default:
			data = 0x0;
			break;
		};
		ch = data;
		my_console_send(dev, &ch, sizeof(ch));
	}

	return 0;
}

static int virtio_console_reset(struct virtio_device *dev)
{
	uint32_t i;
	struct virtio_console_dev *cdev = dev->emu_data;

	if (!cdev)
		return 0;

	for (i = 0; i < VIRTIO_CONSOLE_NUM_QUEUES; i++)
		virtio_console_reset_vq(dev, i);

	cdev->features = 0;
	cdev->pending_queues = 0;

	return 0;
}

static int virtio_console_connect(struct virtio_device *dev,
				  struct virtio_emulator *emu)
{
	struct virtio_console_dev *cdev;
	struct virtio_mmio_dev *mdev = container_of(dev, struct virtio_mmio_dev, dev);

	cdev = (struct virtio_console_dev *)my_zalloc(dev, sizeof(*cdev));
	if (!cdev) {
		my_print(dev, "%s alloc virtio_console_dev failed\n", __FUNCTION__);
		return -1;
	}

	cdev->config.cols = 80;
	cdev->config.rows = 24;
	cdev->config.max_nr_ports = 1;
	cdev->vdev = dev;

	dev->emu_data = cdev;

	mdev->cb.receive = virtio_console_receive;
	mdev->cb.process_req = virtio_console_req_process;
	mdev->cb.data = cdev;

	return 0;
}

static void virtio_console_disconnect(struct virtio_device *dev)
{

}

static struct virtio_device_id virtio_console_emu_id[] = {
	{ .type = VMM_VIRTIO_ID_CONSOLE },
	{ },
};

static struct virtio_emulator virtio_console = {
	.name = "virtio_console",
	.id_table = virtio_console_emu_id,

	/* VirtIO operations */
	.get_host_features      = virtio_console_get_host_features,
	.set_guest_features     = virtio_console_set_guest_features,
	.init_vq                = virtio_console_init_vq,
	.init_vq_addr           = virtio_console_init_vq_addr,
	.reset_vq               = virtio_console_reset_vq,
	.get_pfn_vq             = virtio_console_get_pfn_vq,
	.get_size_vq            = virtio_console_get_size_vq,
	.set_size_vq            = virtio_console_set_size_vq,
	.notify_vq              = virtio_console_notify_vq,
	.status_changed         = virtio_console_status_changed,

	/* Emulator operations */
	.read_config = virtio_console_read_config,
	.write_config = virtio_console_write_config,
	.reset = virtio_console_reset,
	.connect = virtio_console_connect,
	.disconnect = virtio_console_disconnect,
};

struct virtio_emulator *virtio_console_emulator_create(void)
{
	return &virtio_console;
}
