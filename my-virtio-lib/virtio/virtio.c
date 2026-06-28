#include <stdbool.h>
#include <string.h>
#include "utils.h"
#include "virtio.h"
#include "list.h"

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

static int virtio_setup_gphys_hphys_pair(struct virtio_device *dev, uint64_t gphys_addr,
					 uint64_t hphys_addr, uint64_t len)
{
	struct addr_trans_pair *pair;

	//my_print(dev, "%s: gphys_addr:0x%lx hphys_addr:0x%lx size:0x%lx\n",
	//	 __FUNCTION__, gphys_addr, hphys_addr, len);

	list_for_each_entry(pair, &dev->addr_trans_tables, list) {
		if (memory_region_is_overlay(pair->gphys, pair->gphys + pair->size,
					     gphys_addr, gphys_addr + len)) {
			my_print(dev, "%s: warning!! memory region is overlay\n", __FUNCTION__);
			return -1;
		}
	}

	pair = (struct addr_trans_pair *)my_alloc(dev, sizeof(struct addr_trans_pair));
	if (!pair) {
		my_print(dev, "%s: alloc addr_trans_pair failed\n", __FUNCTION__);
		return -1;
	}

	pair->gphys = gphys_addr;
	pair->hphys = hphys_addr;
	pair->size = len;

	list_add_tail(&pair->list, &dev->addr_trans_tables);

	return 0;
}

uint64_t virtio_get_gphys_from_hphys(struct virtio_device *dev, uint64_t hphys)
{
	struct addr_trans_pair *pair;

	list_for_each_entry(pair, &dev->addr_trans_tables, list) {
		if (hphys >= pair->hphys && (hphys < pair->hphys + pair->size))
			return pair->gphys + (hphys - pair->hphys);
	}

	return -1;
}

uint64_t virtio_get_hphys_from_gphys(struct virtio_device *dev, uint64_t gphys)
{
	struct addr_trans_pair *pair;

	list_for_each_entry(pair, &dev->addr_trans_tables, list) {
		if (gphys >= pair->gphys && (gphys < pair->gphys + pair->size))
			return pair->hphys + (gphys - pair->gphys);
	}

	return -1;
}

void virtio_clear_addr_trans_tables(struct virtio_device *dev)
{
	struct addr_trans_pair *pair, *tmp;

	if (!dev) {
		return;
	}

	list_for_each_entry_safe(pair, tmp, &dev->addr_trans_tables, list) {
		list_del(&pair->list);
		my_free(dev, (uint64_t)(uintptr_t)pair, sizeof(*pair));
	}
}

bool virtio_device_has_feature(struct virtio_device *dev, uint32_t feature)
{
	if (!dev || feature >= 64) {
		return false;
	}

	return dev->guest_features & (1ULL << feature);
}

void virtio_device_set_guest_features(struct virtio_device *dev,
				      uint32_t select, uint32_t features)
{
	if (!dev || select > 1) {
		return;
	}

	dev->guest_features &= ~((uint64_t)UINT_MAX << (select * 32));
	dev->guest_features |= ((uint64_t)features << (select * 32));
}

unsigned int virtio_queue_desc_count(struct virtio_queue *vq)
{
	return (vq) ? vq->desc_count : 0;
}

unsigned int virtio_queue_align(struct virtio_queue *vq)
{
	return (vq) ? vq->align : 0;
}

uint64_t virtio_queue_guest_pfn(struct virtio_queue *vq)
{
	return (vq) ? vq->guest_pfn : 0;
}

uint64_t virtio_queue_guest_page_size(struct virtio_queue *vq)
{
	return (vq) ? vq->guest_page_size : 0;
}

uint64_t virtio_queue_guest_addr(struct virtio_queue *vq)
{
	return (vq) ? vq->guest_addr : 0;
}

uint64_t virtio_queue_host_addr(struct virtio_queue *vq)
{
	return (vq) ? vq->host_addr : 0;
}

uint64_t virtio_queue_total_size(struct virtio_queue *vq)
{
	return (vq) ? vq->total_size : 0;
}

unsigned int virtio_queue_max_desc(struct virtio_queue *vq)
{
	if (!vq) {
		return 0;
	}

	return vq->desc_count;
}

void virtio_queue_set_avail_event(struct virtio_queue *vq)
{
	uint16_t val;
	uint32_t ret;
	uint64_t avail_evt_pa;

	if (!vq) {
		return;
	}

	val = vq->last_avail_idx;
	avail_evt_pa = vq->vring.used_pa +
		  offsetof(struct vring_used, ring[vq->vring.num]);
	ret = my_guest_physical_write(vq->vdev, avail_evt_pa, &val, sizeof(val));
	if (ret != sizeof(val)) {
		my_print(vq->vdev, "%s: write failed at avail_evt_pa=0x%lx\n",
			 __FUNCTION__, avail_evt_pa);
	}
}

void virtio_queue_set_used_elem(struct virtio_queue *vq,
				uint32_t head, uint32_t len)
{
	uint32_t ret;
	uint16_t used_idx;
	struct vring_used_elem used_elem;
	uint64_t used_idx_pa, used_elem_pa;

	if (!vq) {
		return;
	}

	used_idx_pa = vq->vring.used_pa +
		      offsetof(struct vring_used, idx);
	ret = my_guest_physical_read(vq->vdev, used_idx_pa, &used_idx, sizeof(used_idx));
	if (ret != sizeof(used_idx)) {
		my_print(vq->vdev, "%s: read failed at used_idx_pa=0x%lx\n",
			 __FUNCTION__, used_idx_pa);
	}

	used_elem.id = head;
	used_elem.len = len;
	ret = umod32(used_idx, vq->vring.num);
	used_elem_pa = vq->vring.used_pa +
		       offsetof(struct vring_used, ring[ret]);
	ret = my_guest_physical_write(vq->vdev, used_elem_pa, &used_elem, sizeof(used_elem));
	if (ret != sizeof(used_elem)) {
		my_print(vq->vdev, "%s: write failed at used_elem_pa=0x%lx\n",
			 __FUNCTION__, used_elem_pa);
	}

	used_idx++;
	ret = my_guest_physical_write(vq->vdev, used_idx_pa, &used_idx, sizeof(used_idx));
	if (ret != sizeof(used_idx)) {
		my_print(vq->vdev, "%s: write failed at used_idx_pa=0x%lx\n",
			 __FUNCTION__, used_idx_pa);
	}
}

void my_virtio_queue_reset(struct virtio_queue *vq)
{
	if (!vq) {
		return;
	}

	memset(vq, 0, sizeof(*vq));
}

int virtio_queue_get_desc(struct virtio_queue *vq, unsigned short indx,
			  struct vring_desc *desc)
{
	uint32_t ret;
	uint64_t desc_pa;

	if (!vq || !desc) {
		return -1;
	}

	desc_pa = vq->vring.desc_pa + indx * sizeof(*desc);
	ret = my_guest_physical_read(vq->vdev, desc_pa, desc, sizeof(*desc));
	if (ret != sizeof(*desc)) {
		my_print(vq->vdev, "%s: read failed at avail_pa=0x%lx\n",
			 __FUNCTION__, desc_pa);
		return -1;
	}

	return 0;
}

unsigned short virtio_queue_pop(struct virtio_queue *vq)
{
	uint16_t val;
	uint32_t ret;
	uint64_t avail_pa;

	if (!vq) {
		return 0;
	}

	ret = umod32((uint32_t)vq->last_avail_idx++, vq->desc_count);

	avail_pa = vq->vring.avail_pa +
		   offsetof(struct vring_avail, ring[ret]);
	ret = my_guest_physical_read(vq->vdev, avail_pa, &val, sizeof(val));
	if (ret != sizeof(val)) {
		my_print(vq->vdev, "%s: read failed at avail_pa=0x%lx\n",
			 __FUNCTION__, avail_pa);
		return 0;
	}

	return val;
}

bool virtio_queue_available(struct virtio_queue *vq)
{
	uint16_t val;
	uint32_t ret;
	uint64_t avail_pa;

	if (!vq || !vq->vdev || !vq->desc_count ||
	    !vq->vring.desc_pa || !vq->vring.avail_pa ||
	    !vq->vring.used_pa) {
		return false;
	}

	avail_pa = vq->vring.avail_pa +
		   offsetof(struct vring_avail, idx);
	ret = my_guest_physical_read(vq->vdev, avail_pa, &val, sizeof(val));
	if (ret != sizeof(val)) {
		my_print(vq->vdev, "%s: read failed at avail_pa=0x%lx\n",
			 __FUNCTION__, avail_pa);
		return false;
	}

	return val != vq->last_avail_idx;
}

bool virtio_queue_should_signal(struct virtio_queue *vq)
{
	uint32_t ret;
	uint16_t old_idx, new_idx, event_idx, avail_flags;
	uint64_t used_pa, avail_pa;

	if (!vq) {
		return false;
	}

	old_idx = vq->last_used_signalled;

	used_pa = vq->vring.used_pa +
		  offsetof(struct vring_used, idx);
	ret = my_guest_physical_read(vq->vdev, used_pa, &new_idx, sizeof(new_idx));
	if (ret != sizeof(new_idx)) {
		my_print(vq->vdev, "%s: read failed at used_pa=0x%lx\n",
			 __FUNCTION__, used_pa);
		return false;
	}

	if (virtio_device_has_feature(vq->vdev, VMM_VIRTIO_RING_F_EVENT_IDX)) {
		avail_pa = vq->vring.avail_pa +
			   offsetof(struct vring_avail, ring[vq->vring.num]);
		ret = my_guest_physical_read(vq->vdev, avail_pa, &event_idx,
					     sizeof(event_idx));
		if (ret != sizeof(event_idx)) {
			my_print(vq->vdev, "%s: read failed at avail_pa=0x%lx\n",
				 __FUNCTION__, avail_pa);
			return false;
		}

		if (!vring_need_event(event_idx, new_idx, old_idx)) {
			return false;
		}
	} else {
		avail_pa = vq->vring.avail_pa +
			   offsetof(struct vring_avail, flags);
		ret = my_guest_physical_read(vq->vdev, avail_pa, &avail_flags,
					     sizeof(avail_flags));
		if (ret != sizeof(avail_flags)) {
			my_print(vq->vdev, "%s: read failed at avail_pa=0x%lx\n",
				 __FUNCTION__, avail_pa);
			return false;
		}

		if (avail_flags & VMM_VRING_AVAIL_F_NO_INTERRUPT) {
			return false;
		}
	}

	if (new_idx != old_idx) {
		vq->last_used_signalled = new_idx;
		return true;
	}

	return false;
}

int virtio_queue_setup(struct virtio_device *dev, struct virtio_queue *vq,
		       uint64_t guest_pfn, uint64_t guest_page_size,
		       uint32_t desc_count, uint32_t align)
{
	uint64_t gphys_addr, hphys_addr;
	uint64_t gphys_size, avail_size;

	gphys_addr = guest_pfn * guest_page_size;
	gphys_size = vring_size(desc_count, align);

	if (!my_guest_physical_map(dev, gphys_addr, gphys_size, &hphys_addr, &avail_size)) {
		if (avail_size < gphys_size) {
			my_print(dev, "%s: available size less than required size\n", __FUNCTION__);
			return -1;
		}
		if (virtio_setup_gphys_hphys_pair(dev, gphys_addr, hphys_addr, avail_size)) {
			my_print(dev, "%s: virtio_setup_gphys_hphys_pair\n", __FUNCTION__);
			return -1;
		}
	}

	vring_init(&vq->vring, desc_count, NULL, gphys_addr, align);

	vq->desc_count = desc_count;
	vq->align = align;
	vq->guest_pfn = guest_pfn;
	vq->guest_page_size = guest_page_size;

	vq->guest_addr = gphys_addr;
	vq->host_addr = hphys_addr;
	vq->total_size = gphys_size;

	vq->vdev = dev;

	return 0;
}

int virtio_queue_setup_split(struct virtio_device *dev, struct virtio_queue *vq,
			     uint64_t desc_addr, uint64_t avail_addr,
			     uint64_t used_addr, uint32_t desc_count)
{
	uint64_t used_end;

	if (!dev || !vq || !desc_count || !desc_addr || !avail_addr || !used_addr)
		return -1;

	used_end = used_addr + sizeof(uint16_t) * 2 +
		   sizeof(struct vring_used_elem) * desc_count;

	vq->last_avail_idx = 0;
	vq->last_used_signalled = 0;

	vq->vring.num = desc_count;
	vq->vring.desc = NULL;
	vq->vring.desc_pa = desc_addr;
	vq->vring.avail = NULL;
	vq->vring.avail_pa = avail_addr;
	vq->vring.used = NULL;
	vq->vring.used_pa = used_addr;

	vq->desc_count = desc_count;
	vq->align = 0;
	vq->guest_pfn = 0;
	vq->guest_page_size = 0;
	vq->guest_addr = desc_addr;
	vq->host_addr = 0;
	vq->total_size = (used_end > desc_addr) ? (used_end - desc_addr) : 0;
	vq->vdev = dev;

	return 0;
}

static unsigned next_desc(struct virtio_queue *vq,
			  struct vring_desc *desc,
			  uint32_t i, uint32_t max)
{
	int rc;
	uint32_t next;

	if (!(desc->flags & VMM_VRING_DESC_F_NEXT)) {
		return max;
	}

	next = desc->next;

	rc = virtio_queue_get_desc(vq, next, desc);
	if (rc) {
		my_print(vq->vdev, "%s: failed to get descriptor next=%d error=%d\n",
			 __FUNCTION__, next, rc);
		return max;
	}

	return next;
}

int virtio_queue_get_head_iovec(struct virtio_queue *vq,
				uint16_t head, struct virtio_iovec *iov,
				uint32_t *ret_iov_cnt, uint32_t *ret_total_len,
				uint16_t *ret_head)
{
	int i, rc = 0;
	uint16_t idx, max;
	struct vring_desc desc;

	if (!vq || !iov) {
		goto fail;
	}

	idx = head;

	if (ret_iov_cnt) {
		*ret_iov_cnt = 0;
	}
	if (ret_total_len) {
		*ret_total_len = 0;
	}
	if (ret_head) {
		*ret_head = 0;
	}

	max = virtio_queue_max_desc(vq);

	rc = virtio_queue_get_desc(vq, idx, &desc);
	if (rc) {
		my_print(vq->vdev, "%s: failed to get descriptor idx=%d error=%d\n",
			 __FUNCTION__, idx, rc);
		goto fail;
	}

	if (desc.flags & VMM_VRING_DESC_F_INDIRECT) {
#if 0
		max = desc[idx].len / sizeof(struct vring_desc);
		desc = guest_flat_to_host(kvm, desc[idx].addr);
		idx = 0;
#endif
		my_print(vq->vdev, "%s: indirect descriptor not supported idx=%d\n",
			 __FUNCTION__, idx);
		rc = VIRTIO_ENOTSUPP;
		goto fail;
	}

	i = 0;
	do {
		iov[i].addr = desc.addr;
		iov[i].len = desc.len;

		if (ret_total_len) {
			*ret_total_len += desc.len;
		}

		if (desc.flags & VMM_VRING_DESC_F_WRITE) {
			iov[i].flags = 1;  /* Write */
		} else {
			iov[i].flags = 0; /* Read */
		}

		i++;
	} while ((idx = next_desc(vq, &desc, idx, max)) != max);

	if (ret_iov_cnt) {
		*ret_iov_cnt = i;
	}

	virtio_queue_set_avail_event(vq);

	if (ret_head) {
		*ret_head = head;
	}

	return 0;

fail:
	if (ret_iov_cnt) {
		*ret_iov_cnt = 0;
	}
	if (ret_total_len) {
		*ret_total_len = 0;
	}
	return rc;
}

int virtio_queue_get_iovec(struct virtio_queue *vq,
			   struct virtio_iovec *iov,
			   uint32_t *ret_iov_cnt, uint32_t *ret_total_len,
			   uint16_t *ret_head)
{
	uint16_t head = virtio_queue_pop(vq);

	return virtio_queue_get_head_iovec(vq, head, iov,
					   ret_iov_cnt, ret_total_len,
					   ret_head);
}

uint32_t virtio_iovec_to_buf_read(struct virtio_device *dev,
				  struct virtio_iovec *iov,
				  uint32_t iov_cnt, void *buf,
				  uint32_t buf_len)
{
	uint32_t i = 0, pos = 0, len = 0;

	for (i = 0; i < iov_cnt && pos < buf_len; i++) {
		len = ((buf_len - pos) < iov[i].len) ?
				(buf_len - pos) : iov[i].len;

		len = my_guest_physical_read(dev, iov[i].addr, buf + pos, len);
		if (!len) {
			break;
		}

		pos += len;
	}

	return pos;
}

uint32_t virtio_buf_to_iovec_write(struct virtio_device *dev,
				   struct virtio_iovec *iov,
				   uint32_t iov_cnt, void *buf,
				   uint32_t buf_len)
{
	uint32_t i = 0, pos = 0, len = 0;

	for (i = 0; i < iov_cnt && pos < buf_len; i++) {
		len = ((buf_len - pos) < iov[i].len) ?
					(buf_len - pos) : iov[i].len;

		len = my_guest_physical_write(dev, iov[i].addr, buf + pos, len);
		if (!len) {
			break;
		}

		pos += len;
	}

	return pos;
}
