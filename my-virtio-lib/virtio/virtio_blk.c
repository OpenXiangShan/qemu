#include <stddef.h>
#include <string.h>
#include "virtio_wrapper.h"
#include "virtio.h"
#include "virtio_mmio.h"
#include "virtio_blk.h"
#include "virtio_ids.h"
#include "virtio_ring.h"
#include "utils.h"
#include "fifo.h"

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

#define VIRTIO_BLK_QUEUE_SIZE		128
#define VIRTIO_BLK_IO_QUEUE		0
#define VIRTIO_BLK_NUM_QUEUES		1
#define VIRTIO_BLK_SECTOR_SIZE		512
#define VIRTIO_BLK_DISK_SEG_MAX		(VIRTIO_BLK_QUEUE_SIZE - 2)

struct virtio_blk_dev_req {
	struct virtio_queue		*vq;
	uint16_t			head;
	struct virtio_iovec		*read_iov;
	uint32_t			read_iov_cnt;
	uint32_t			len;
	struct virtio_iovec		status_iov;
	void				*data;
	//struct vdisk_request		r;
};

struct virtio_blk_dev {
	struct virtio_device		*vdev;

	struct virtio_queue		vqs[VIRTIO_BLK_NUM_QUEUES];
	struct virtio_iovec		iov[VIRTIO_BLK_QUEUE_SIZE];
	struct virtio_blk_dev_req	reqs[VIRTIO_BLK_QUEUE_SIZE];
	uint64_t			features;

	struct virtio_blk_config 	config;

	struct fifo *req_process;
};

static void virtio_blk_req_complete(struct virtio_device *dev,
				    struct virtio_blk_dev *vbdev,
				    struct virtio_blk_dev_req *req,
				    uint8_t flags, uint8_t status)
{
	int queueid = req->vq - vbdev->vqs;

	if (req->read_iov && req->len && req->data &&
	    (flags == MY_BLK_REQ_READ)) {
		virtio_buf_to_iovec_write(dev,
					  req->read_iov,
					  req->read_iov_cnt,
					  req->data,
					  req->len);
	}

	if (req->read_iov) {
		my_free(dev, (uint64_t)req->read_iov,
			req->read_iov_cnt * sizeof(struct virtio_iovec));
		req->read_iov = NULL;
		req->read_iov_cnt = 0;
	}
	if (req->data) {
		my_free(dev, (uint64_t)req->data, req->len);
		req->data = NULL;
	}

	virtio_buf_to_iovec_write(dev, &req->status_iov, 1, &status, 1);

	virtio_queue_set_used_elem(req->vq, req->head, req->len);

	if (virtio_queue_should_signal(req->vq)) {
		//my_print(dev, "####### %s need signal!!!\n", __FUNCTION__);
		if (dev->vn && dev->vn->notify)
			dev->vn->notify(dev, queueid);
	}
}

static void virtio_blk_req_process(void *data)
{
	int rc;
	uint16_t head, thead;
	uint32_t i, iov_cnt, len;
	struct virtio_blk_dev *vbdev = (struct virtio_blk_dev *)data;
	struct virtio_device *dev = vbdev->vdev;
	struct virtio_queue *vq = &vbdev->vqs[VIRTIO_BLK_IO_QUEUE];
	struct virtio_blk_dev_req *req;
	struct virtio_blk_outhdr hdr;

	while (virtio_queue_available(vq)) {
		thead = virtio_queue_pop(vq);
		req = &vbdev->reqs[thead];
		rc = virtio_queue_get_head_iovec(vq, thead, vbdev->iov,
						 &iov_cnt, &len, &head);
		if (rc) {
			my_print(dev, "%s: failed to get iovec (error %d)\n",
				 __FUNCTION__, rc);
			continue;
		}

		req->vq = vq;
		req->head = head;
		req->read_iov = NULL;
		req->read_iov_cnt = 0;
		req->len = 0;
		for (i = 1; i < (iov_cnt - 1); i++) {
			req->len += vbdev->iov[i].len;
		}
		req->status_iov.addr = vbdev->iov[iov_cnt - 1].addr;
		req->status_iov.len = vbdev->iov[iov_cnt - 1].len;

		len = virtio_iovec_to_buf_read(dev, &vbdev->iov[0], 1,
					       &hdr, sizeof(hdr));
		if (len < sizeof(hdr)) {
			virtio_queue_set_used_elem(req->vq, req->head, 0);
			continue;
		}

		switch (hdr.type) {
		case VMM_VIRTIO_BLK_T_IN:
			//my_print(dev, "######## %s VMM_VIRTIO_BLK_T_IN\n", __FUNCTION__);
			req->data = (void *)my_alloc(dev, req->len);
			if (!req->data) {
				my_print(dev, "%s: %s malloc failed, len:%d\n", __FUNCTION__, VMM_VIRTIO_BLK_T_IN, req->len);
				continue;
			}
			len = sizeof(struct virtio_iovec) * (iov_cnt - 2);
			req->read_iov = (struct virtio_iovec *)my_alloc(dev, len);
			if (!req->read_iov) {
				my_print(dev, "%s: %s malloc failed, len:%d\n", __FUNCTION__, VMM_VIRTIO_BLK_T_IN, req->len);
				continue;
			}
			req->read_iov_cnt = iov_cnt - 2;
			for (i = 0; i < req->read_iov_cnt; i++) {
				req->read_iov[i].addr = vbdev->iov[i + 1].addr;
				req->read_iov[i].len = vbdev->iov[i + 1].len;
			}
			//my_print(dev, "%s: VIRTIO_BLK_T_IN dev=%s "
			//	 "hdr.sector=0x%lx req->len=%d\n",
			//	 __FUNCTION__, dev->name,
			//	 (u64)hdr.sector, req->len);
			if (my_submit_blk_request(dev, hdr.sector, req->data, req->len, MY_BLK_REQ_READ)) {
				my_print(dev, "%s: submit_blk_request failed\n", __FUNCTION__);
				continue;
			}
			virtio_blk_req_complete(dev, vbdev, req, MY_BLK_REQ_READ, VMM_VIRTIO_BLK_S_OK);

			break;
		case VMM_VIRTIO_BLK_T_OUT:
			//my_print(dev, "######## %s VMM_VIRTIO_BLK_T_OUT\n", __FUNCTION__);
			req->data = (void *)my_alloc(dev, req->len);
			if (!req->data) {
				my_print(dev, "%s: %s malloc failed, len:%d\n", __FUNCTION__, VMM_VIRTIO_BLK_T_IN, req->len);
				continue;
			}
			virtio_iovec_to_buf_read(dev, &vbdev->iov[1], iov_cnt - 2, req->data, req->len);
			//my_print(dev, "%s: VIRTIO_BLK_T_OUT dev=%s "
			//	 "hdr.sector=0x%lx req->len=%d\n",
			//	 __FUNCTION__, dev->name,
			//	 (u64)hdr.sector, req->len);
			if (my_submit_blk_request(dev, hdr.sector, req->data, req->len, MY_BLK_REQ_WRITE)) {
				my_print(dev, "%s: submit_blk_request failed\n", __FUNCTION__);
				continue;
			}
			virtio_blk_req_complete(dev, vbdev, req, MY_BLK_REQ_WRITE, VMM_VIRTIO_BLK_S_OK);

			break;
		case VMM_VIRTIO_BLK_T_FLUSH:
			//my_print(dev, "######## %s VMM_VIRTIO_BLK_T_FLUSH\n", __FUNCTION__);
			virtio_blk_req_complete(dev, vbdev, req, MY_BLK_REQ_FLUSH, VMM_VIRTIO_BLK_S_OK);
			break;
		case VMM_VIRTIO_BLK_T_GET_ID:
		{
			const char id[VMM_VIRTIO_BLK_ID_BYTES] = "my-virtio-blk";

			if (iov_cnt > 2) {
				virtio_buf_to_iovec_write(dev, &vbdev->iov[1],
							  iov_cnt - 2,
							  (void *)id, sizeof(id));
			}
			virtio_blk_req_complete(dev, vbdev, req, 0,
						VMM_VIRTIO_BLK_S_OK);
			break;
		}
		default:
			my_print(dev, "%s: unhandled hdr.type=%d\n",
				 __FUNCTION__, hdr.type);
			virtio_blk_req_complete(dev, vbdev, req, 0,
						VMM_VIRTIO_BLK_S_UNSUPP);
			break;
		}
	}

}

static uint64_t virtio_blk_get_host_features(struct virtio_device *dev)
{
	return	1UL << VMM_VIRTIO_BLK_F_SEG_MAX
		| 1UL << VMM_VIRTIO_BLK_F_BLK_SIZE
		| 1UL << VMM_VIRTIO_BLK_F_FLUSH
		| 1UL << VMM_VIRTIO_RING_F_EVENT_IDX;
#if 0
		| 1UL << VMM_VIRTIO_RING_F_INDIRECT_DESC;
#endif
}

static void virtio_blk_set_guest_features(struct virtio_device *dev,
					  uint32_t select, uint32_t features)
{
	struct virtio_blk_dev *vbdev = dev->emu_data;

	//my_print(dev, "%s select:0x%x features:0x%x\n", __FUNCTION__, select, features);

	if (1 < select)
		return;

	vbdev->features &= ~((u64)UINT_MAX << (select * 32));
	vbdev->features |= ((u64)features << (select * 32));
}

static int virtio_blk_init_vq(struct virtio_device *dev,
			      uint32_t vq, uint32_t page_size, uint32_t align,
			      uint32_t pfn)
{
	int ret;
	struct virtio_blk_dev *vbdev = dev->emu_data;

	//my_print(dev, "%s vq:0x%x page_size:0x%x align:0x%x pfn:0x%x\n", __FUNCTION__, vq, page_size, align, pfn);

	switch (vq) {
	case VIRTIO_BLK_IO_QUEUE:
		ret = virtio_queue_setup(dev, &vbdev->vqs[vq], pfn, page_size,
					 VIRTIO_BLK_QUEUE_SIZE, align);
		break;
	default:
		ret = -1;
		break;
	};

	return ret;
}

static int virtio_blk_init_vq_addr(struct virtio_device *dev, uint32_t vq,
				   uint64_t desc_addr, uint64_t avail_addr,
				   uint64_t used_addr, uint32_t size)
{
	int ret;
	struct virtio_blk_dev *vbdev = dev->emu_data;

	switch (vq) {
	case VIRTIO_BLK_IO_QUEUE:
		ret = virtio_queue_setup_split(dev, &vbdev->vqs[vq], desc_addr,
					       avail_addr, used_addr, size);
		break;
	default:
		ret = -1;
		break;
	};

	return ret;
}

static void virtio_blk_reset_vq(struct virtio_device *dev, uint32_t vq)
{
	struct virtio_blk_dev *vbdev = dev->emu_data;

	if (!vbdev || vq >= VIRTIO_BLK_NUM_QUEUES)
		return;

	my_virtio_queue_reset(&vbdev->vqs[vq]);
}

static int virtio_blk_get_pfn_vq(struct virtio_device *dev, uint32_t vq)
{
	int ret = 0;
	struct virtio_blk_dev *vbdev = dev->emu_data;

	//my_print(dev, "%s vq:0x%x\n", __FUNCTION__, vq);

	switch (vq) {
	case VIRTIO_BLK_IO_QUEUE:
		ret = virtio_queue_guest_pfn(&vbdev->vqs[vq]);
		break;
	default:
		ret = -1;
		break;
	};
	return ret;
}

static int virtio_blk_get_size_vq(struct virtio_device *dev, uint32_t vq)
{
	int ret;

	//my_print(dev, "%s vq:0x%x\n", __FUNCTION__, vq);

	switch (vq) {
	case VIRTIO_BLK_IO_QUEUE:
		ret = VIRTIO_BLK_QUEUE_SIZE;
		break;
	default:
		ret = 0;
		break;
	};

	return ret;
}

static int virtio_blk_set_size_vq(struct virtio_device *dev,
				  uint32_t vq, int size)
{
	//my_print(dev, "%s vq:0x%x size:0x%x warning !!!\n", __FUNCTION__, vq, size);
	return size;
}

static int virtio_blk_notify_vq(struct virtio_device *dev, uint32_t vq)
{
	int ret = 0;

	//my_print(dev, "%s vq:0x%x\n", __FUNCTION__, vq);

	switch (vq) {
	case VIRTIO_BLK_IO_QUEUE:
		ret = 0;
		break;
	default:
		ret = -1;
		break;
	};

	return ret;
}

static void virtio_blk_status_changed(struct virtio_device *dev,
				      uint32_t new_status)
{
	//my_print(dev, "%s new_status:0x%x\n", __FUNCTION__, new_status);
}

static int virtio_blk_reset(struct virtio_device *dev)
{
	uint32_t i;
	struct virtio_blk_dev *vbdev = dev->emu_data;

	if (!vbdev)
		return 0;

	for (i = 0; i < VIRTIO_BLK_NUM_QUEUES; i++)
		virtio_blk_reset_vq(dev, i);

	for (i = 0; i < VIRTIO_BLK_QUEUE_SIZE; i++) {
		struct virtio_blk_dev_req *req = &vbdev->reqs[i];

		if (req->read_iov) {
			my_free(dev, (uint64_t)(uintptr_t)req->read_iov,
				req->read_iov_cnt * sizeof(struct virtio_iovec));
		}
		if (req->data) {
			my_free(dev, (uint64_t)(uintptr_t)req->data, req->len);
		}
		memset(req, 0, sizeof(*req));
	}

	vbdev->features = 0;
	fifo_clear(vbdev->req_process);

	return 0;
}

static int virtio_blk_write_config(struct virtio_device *dev,
				   uint32_t offset, void *src, uint32_t src_len)
{
	uint32_t i;
	struct virtio_blk_dev *vbdev = dev->emu_data;
	uint8_t *dst = (uint8_t *)&vbdev->config;

	//my_print(dev, "%s: dev=%s offset=%d src=%p src_len=%d\n",
	//	 __FUNCTION__, dev->name, offset, src, src_len);

	for (i = 0; (i < src_len) && ((offset + i) < sizeof(vbdev->config)); i++) {
		dst[offset + i] = ((uint8_t *)src)[i];
	}

	return 0;
}

static int virtio_blk_read_config(struct virtio_device *dev,
				  uint32_t offset, void *dst, uint32_t dst_len)
{
	uint32_t i;
	struct virtio_blk_dev *vbdev = dev->emu_data;
	uint8_t *src = (uint8_t *)&vbdev->config;

	//my_print(dev, "%s: dev=%s offset=%d dst=%p dst_len=%d\n",
	//	 __FUNCTION__, dev->name, offset, dst, dst_len);

	for (i = 0; (i < dst_len) && ((offset + i) < sizeof(vbdev->config)); i++) {
		((uint8_t *)dst)[i] = src[offset + i];
	}

	return 0;
}

static int virtio_blk_connect(struct virtio_device *dev,
			      struct virtio_emulator *emu)
{
	struct virtio_blk_dev *vbdev;
	struct virtio_mmio_dev *mdev = container_of(dev, struct virtio_mmio_dev, dev);

	vbdev = (struct virtio_blk_dev *)my_alloc(dev, sizeof(struct virtio_blk_dev));
	if (!vbdev)
		return -1;
	memset(vbdev, 0, sizeof(*vbdev));

	vbdev->vdev = dev;

	vbdev->config.capacity = my_get_blk_capacity(dev);
	if (vbdev->config.capacity == -1)
		vbdev->config.capacity = 1 * 1024 * 1024 * 1024 / VIRTIO_BLK_SECTOR_SIZE;
	vbdev->config.seg_max = VIRTIO_BLK_DISK_SEG_MAX,
	vbdev->config.blk_size = VIRTIO_BLK_SECTOR_SIZE;

	vbdev->req_process = fifo_alloc(dev, sizeof(uint16_t), VIRTIO_BLK_QUEUE_SIZE);
	if (!vbdev->req_process) {
		my_print(dev, "%s -- fifo alloc failed\n", __FUNCTION__);
		my_free(dev, (uint64_t)vbdev, sizeof(struct virtio_blk_dev));
		return -1;
	}

	dev->emu_data = vbdev;

	mdev->cb.process_req = virtio_blk_req_process;
	mdev->cb.data = vbdev;

	return 0;
}

static void virtio_blk_disconnect(struct virtio_device *dev)
{
}

static struct virtio_device_id virtio_blk_emu_id[] = {
	{ .type = VMM_VIRTIO_ID_BLOCK },
	{ },
};

static struct virtio_emulator virtio_blk = {
	.name = "virtio_blk",
	.id_table = virtio_blk_emu_id,

	/* VirtIO operations */
	.get_host_features      = virtio_blk_get_host_features,
	.set_guest_features     = virtio_blk_set_guest_features,
	.init_vq                = virtio_blk_init_vq,
	.init_vq_addr           = virtio_blk_init_vq_addr,
	.reset_vq               = virtio_blk_reset_vq,
	.get_pfn_vq             = virtio_blk_get_pfn_vq,
	.get_size_vq            = virtio_blk_get_size_vq,
	.set_size_vq            = virtio_blk_set_size_vq,
	.notify_vq              = virtio_blk_notify_vq,
	.status_changed         = virtio_blk_status_changed,

	/* Emulator operations */
	.read_config = virtio_blk_read_config,
	.write_config = virtio_blk_write_config,
	.reset = virtio_blk_reset,
	.connect = virtio_blk_connect,
	.disconnect = virtio_blk_disconnect,
};

struct virtio_emulator *virtio_blk_emulator_create(void)
{
	return &virtio_blk;
}
