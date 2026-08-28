#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "virtio.h"
#include "virtio_config.h"
#include "virtio_mmio.h"
#include "virtio_wrapper.h"
#include "utils.h"

/*
 * gbus management transport for picker-backed RTL.
 *
 * The RTL owns the guest-visible virtio-mmio register file.  This transport
 * mirrors the RTL's gbus CSR shadows into the existing C virtio_mmio_dev so the
 * current blk vring walker and backend glue can be reused unchanged.
 */

struct virtio_gbus_queue_shadow {
	uint32_t num;
	uint32_t align;
	uint32_t pfn;
	uint32_t ready;
	uint32_t notify_seq;
	uint64_t desc;
	uint64_t avail;
	uint64_t used;
};

struct virtio_gbus_dev {
	struct list_head list;
	struct virtio_mmio_dev *mdev;
	const struct virtio_gbus_ops *ops;
	void *opaque;
	bool valid;
	uint32_t update_seq;
	uint32_t status;
	uint32_t driver_features[2];
	uint32_t guest_page_size;
	uint32_t reset_seq;
	struct virtio_gbus_queue_shadow queues[VMM_VIRTIO_MMIO_MAX_VQ];
};

static LIST_HEAD(virtio_gbus_dev_list);

static struct virtio_gbus_dev *virtio_gbus_find(virtio_handle_t handle)
{
	struct virtio_gbus_dev *gdev;

	list_for_each_entry(gdev, &virtio_gbus_dev_list, list) {
		if (gdev->mdev == (struct virtio_mmio_dev *)handle) {
			return gdev;
		}
	}

	return NULL;
}

static int virtio_gbus_read_csr(struct virtio_gbus_dev *gdev,
				uint32_t addr, uint32_t *val)
{
	if (!gdev || !gdev->ops || !gdev->ops->read || !val) {
		return -1;
	}

	return gdev->ops->read(gdev->opaque, addr, val);
}

static int virtio_gbus_write_csr(struct virtio_gbus_dev *gdev,
				 uint32_t addr, uint32_t val)
{
	if (!gdev || !gdev->ops || !gdev->ops->write) {
		return -1;
	}

	return gdev->ops->write(gdev->opaque, addr, val);
}

static bool virtio_gbus_feature_selected(uint32_t features0, uint32_t features1,
					 uint32_t feature)
{
	if (feature < 32) {
		return !!(features0 & (1U << feature));
	}
	if (feature < 64) {
		return !!(features1 & (1U << (feature - 32)));
	}
	return false;
}

static uint64_t virtio_gbus_make_addr(uint32_t low, uint32_t high)
{
	return ((uint64_t)high << 32) | low;
}

static int virtio_gbus_mmio_write(struct virtio_gbus_dev *gdev,
				  uint32_t offset, uint32_t val,
				  int *is_doorbell)
{
	int doorbell = 0;
	int ret;

	if (is_doorbell) {
		*is_doorbell = 0;
	}
	ret = virtio_dev_mmio_write(gdev->mdev, offset, val, sizeof(val),
				    is_doorbell ? is_doorbell : &doorbell);
	return ret;
}

static void virtio_gbus_sync_features(struct virtio_gbus_dev *gdev,
				      uint32_t features0, uint32_t features1)
{
	int doorbell;

	if (!gdev->valid || gdev->driver_features[0] != features0) {
		virtio_gbus_mmio_write(gdev, VMM_VIRTIO_MMIO_GUEST_FEATURES_SEL,
				       0, &doorbell);
		virtio_gbus_mmio_write(gdev, VMM_VIRTIO_MMIO_GUEST_FEATURES,
				       features0, &doorbell);
		gdev->driver_features[0] = features0;
	}

	if (!gdev->valid || gdev->driver_features[1] != features1) {
		virtio_gbus_mmio_write(gdev, VMM_VIRTIO_MMIO_GUEST_FEATURES_SEL,
				       1, &doorbell);
		virtio_gbus_mmio_write(gdev, VMM_VIRTIO_MMIO_GUEST_FEATURES,
				       features1, &doorbell);
		gdev->driver_features[1] = features1;
	}
}

static void virtio_gbus_sync_status(struct virtio_gbus_dev *gdev,
				    uint32_t status, bool force_reset)
{
	int doorbell;

	if (force_reset) {
		virtio_gbus_mmio_write(gdev, VMM_VIRTIO_MMIO_STATUS, 0,
				       &doorbell);
		gdev->status = 0;
		memset(gdev->queues, 0, sizeof(gdev->queues));
	}

	if (!gdev->valid || gdev->status != status) {
		virtio_gbus_mmio_write(gdev, VMM_VIRTIO_MMIO_STATUS, status,
				       &doorbell);
		gdev->status = status;
	}
}

static void virtio_gbus_sync_queue(struct virtio_gbus_dev *gdev,
				   uint32_t qid, bool modern,
				   uint32_t num, uint32_t align, uint32_t pfn,
				   uint64_t desc, uint64_t avail, uint64_t used,
				   uint32_t ready, uint32_t notify_seq,
				   bool page_size_changed)
{
	struct virtio_gbus_queue_shadow *shadow = &gdev->queues[qid];
	bool queue_cfg_changed;
	bool pfn_changed;
	bool addr_changed;
	bool ready_changed;
	int doorbell = 0;

	queue_cfg_changed = !gdev->valid || shadow->num != num ||
			    shadow->align != align;
	pfn_changed = !gdev->valid || shadow->pfn != pfn ||
		      page_size_changed || queue_cfg_changed;
	addr_changed = !gdev->valid || shadow->desc != desc ||
		       shadow->avail != avail || shadow->used != used ||
		       queue_cfg_changed;
	ready_changed = !gdev->valid || shadow->ready != ready;

	virtio_gbus_mmio_write(gdev, VMM_VIRTIO_MMIO_QUEUE_SEL, qid, &doorbell);

	if (queue_cfg_changed) {
		virtio_gbus_mmio_write(gdev, VMM_VIRTIO_MMIO_QUEUE_NUM, num,
				       &doorbell);
	}

	if (modern) {
		if (addr_changed) {
			virtio_gbus_mmio_write(gdev, VMM_VIRTIO_MMIO_QUEUE_DESC_LOW,
					       (uint32_t)desc, &doorbell);
			virtio_gbus_mmio_write(gdev, VMM_VIRTIO_MMIO_QUEUE_DESC_HIGH,
					       (uint32_t)(desc >> 32), &doorbell);
			virtio_gbus_mmio_write(gdev, VMM_VIRTIO_MMIO_QUEUE_AVAIL_LOW,
					       (uint32_t)avail, &doorbell);
			virtio_gbus_mmio_write(gdev, VMM_VIRTIO_MMIO_QUEUE_AVAIL_HIGH,
					       (uint32_t)(avail >> 32), &doorbell);
			virtio_gbus_mmio_write(gdev, VMM_VIRTIO_MMIO_QUEUE_USED_LOW,
					       (uint32_t)used, &doorbell);
			virtio_gbus_mmio_write(gdev, VMM_VIRTIO_MMIO_QUEUE_USED_HIGH,
					       (uint32_t)(used >> 32), &doorbell);
		}
		if (ready_changed || (ready && addr_changed)) {
			virtio_gbus_mmio_write(gdev, VMM_VIRTIO_MMIO_QUEUE_READY,
					       ready & 1U, &doorbell);
		}
	} else {
		if (queue_cfg_changed) {
			virtio_gbus_mmio_write(gdev, VMM_VIRTIO_MMIO_QUEUE_ALIGN, align,
					       &doorbell);
		}

		if (pfn_changed) {
			virtio_gbus_mmio_write(gdev, VMM_VIRTIO_MMIO_QUEUE_PFN, pfn,
					       &doorbell);
		}
	}

	if (shadow->notify_seq != notify_seq) {
		virtio_gbus_mmio_write(gdev, VMM_VIRTIO_MMIO_QUEUE_NOTIFY, qid,
				       &doorbell);
		if (doorbell) {
			virtio_process_req((virtio_handle_t)gdev->mdev);
		}
	}

	shadow->num = num;
	shadow->align = align;
	shadow->pfn = pfn;
	shadow->ready = ready;
	shadow->notify_seq = notify_seq;
	shadow->desc = desc;
	shadow->avail = avail;
	shadow->used = used;
}

static int virtio_gbus_dev_notify(struct virtio_device *dev, uint32_t vq)
{
	struct virtio_gbus_dev *gdev = dev->vn_data;

	(void)vq;

	if (!gdev || !gdev->mdev) {
		return -1;
	}

	gdev->mdev->config.interrupt_state |= VMM_VIRTIO_MMIO_INT_VRING;
	return virtio_gbus_write_csr(gdev, VIRTIO_GBUS_CSR_HOST_IRQ_SET,
				     VIRTIO_GBUS_HOST_IRQ_VRING);
}

static struct virtio_notify gbus_notify = {
	.name = "virtio_gbus",
	.notify = virtio_gbus_dev_notify,
};

virtio_handle_t virtio_gbus_create_ex(const char *name, uint64_t start, int len,
				      struct libvirtio_ops *ops, void *priv,
				      const struct virtio_gbus_ops *gbus_ops,
				      void *gbus_opaque,
				      const struct virtio_mmio_options *opts)
{
	struct virtio_gbus_dev *gdev;
	virtio_handle_t handle;

	if (!ops || !ops->mm_alloc || !ops->mm_free || !gbus_ops ||
	    !gbus_ops->read || !gbus_ops->write) {
		return NULL;
	}

	gdev = (struct virtio_gbus_dev *)ops->mm_alloc(sizeof(*gdev));
	if (!gdev) {
		return NULL;
	}
	memset(gdev, 0, sizeof(*gdev));

	handle = virtio_mmio_create_ex(name, start, len, ops, priv, opts);
	if (!handle) {
		ops->mm_free((uint64_t)(uintptr_t)gdev, sizeof(*gdev));
		return NULL;
	}

	gdev->mdev = (struct virtio_mmio_dev *)handle;
	gdev->ops = gbus_ops;
	gdev->opaque = gbus_opaque;
	gdev->mdev->dev.vn = &gbus_notify;
	gdev->mdev->dev.vn_data = gdev;
	INIT_LIST_HEAD(&gdev->list);
	list_add_tail(&gdev->list, &virtio_gbus_dev_list);

	return handle;
}

virtio_handle_t virtio_gbus_create(const char *name, uint64_t start, int len,
				   struct libvirtio_ops *ops, void *priv,
				   const struct virtio_gbus_ops *gbus_ops,
				   void *gbus_opaque)
{
	return virtio_gbus_create_ex(name, start, len, ops, priv, gbus_ops,
				     gbus_opaque, NULL);
}

int virtio_gbus_poll(virtio_handle_t handle)
{
	struct virtio_gbus_dev *gdev = virtio_gbus_find(handle);
	uint32_t features0 = 0;
	uint32_t features1 = 0;
	uint32_t page_size = 0;
	uint32_t reset_seq = 0;
	uint32_t status = 0;
	uint32_t update_seq = 0;
	bool force_reset = false;
	bool page_size_changed = false;
	bool modern;
	int doorbell;

	if (!gdev) {
		return -1;
	}

	if (virtio_gbus_read_csr(gdev, VIRTIO_GBUS_CSR_UPDATE_SEQ,
				 &update_seq) < 0) {
		return -1;
	}
	if (gdev->valid && gdev->update_seq == update_seq) {
		return 0;
	}

	if (virtio_gbus_read_csr(gdev, VIRTIO_GBUS_CSR_RESET_SEQ,
				 &reset_seq) < 0 ||
	    virtio_gbus_read_csr(gdev, VIRTIO_GBUS_CSR_STATUS, &status) < 0 ||
	    virtio_gbus_read_csr(gdev, VIRTIO_GBUS_CSR_DRIVER_FEATURES_0,
				 &features0) < 0 ||
	    virtio_gbus_read_csr(gdev, VIRTIO_GBUS_CSR_DRIVER_FEATURES_1,
				 &features1) < 0 ||
	    virtio_gbus_read_csr(gdev, VIRTIO_GBUS_CSR_GUEST_PAGE_SIZE,
				 &page_size) < 0) {
		return -1;
	}

	force_reset = gdev->valid && gdev->reset_seq != reset_seq;
	gdev->reset_seq = reset_seq;

	if (force_reset) {
		virtio_gbus_sync_status(gdev, 0, true);
	}
	virtio_gbus_sync_features(gdev, features0, features1);
	virtio_gbus_sync_status(gdev, status, false);

	page_size_changed = !gdev->valid || gdev->guest_page_size != page_size;
	if (page_size_changed) {
		virtio_gbus_mmio_write(gdev, VMM_VIRTIO_MMIO_GUEST_PAGE_SIZE,
				       page_size, &doorbell);
		gdev->guest_page_size = page_size;
	}

	for (uint32_t qid = 0; qid < VMM_VIRTIO_MMIO_MAX_VQ; qid++) {
		uint32_t base = VIRTIO_GBUS_CSR_QUEUE_BASE +
				qid * VIRTIO_GBUS_CSR_QUEUE_STRIDE;
		uint32_t num = 0;
		uint32_t align = 0;
		uint32_t pfn = 0;
		uint32_t ready = 0;
		uint32_t notify_seq = 0;
		uint32_t desc_low = 0;
		uint32_t desc_high = 0;
		uint32_t avail_low = 0;
		uint32_t avail_high = 0;
		uint32_t used_low = 0;
		uint32_t used_high = 0;

		if (virtio_gbus_read_csr(gdev, base + VIRTIO_GBUS_CSR_QUEUE_NUM,
					 &num) < 0 ||
		    virtio_gbus_read_csr(gdev, base + VIRTIO_GBUS_CSR_QUEUE_ALIGN,
					 &align) < 0 ||
		    virtio_gbus_read_csr(gdev, base + VIRTIO_GBUS_CSR_QUEUE_PFN,
					 &pfn) < 0 ||
		    virtio_gbus_read_csr(gdev, base + VIRTIO_GBUS_CSR_QUEUE_READY,
					 &ready) < 0 ||
		    virtio_gbus_read_csr(gdev,
					 base + VIRTIO_GBUS_CSR_QUEUE_NOTIFY_SEQ,
					 &notify_seq) < 0 ||
		    virtio_gbus_read_csr(gdev,
					 base + VIRTIO_GBUS_CSR_QUEUE_DESC_LOW,
					 &desc_low) < 0 ||
		    virtio_gbus_read_csr(gdev,
					 base + VIRTIO_GBUS_CSR_QUEUE_DESC_HIGH,
					 &desc_high) < 0 ||
		    virtio_gbus_read_csr(gdev,
					 base + VIRTIO_GBUS_CSR_QUEUE_AVAIL_LOW,
					 &avail_low) < 0 ||
		    virtio_gbus_read_csr(gdev,
					 base + VIRTIO_GBUS_CSR_QUEUE_AVAIL_HIGH,
					 &avail_high) < 0 ||
		    virtio_gbus_read_csr(gdev,
					 base + VIRTIO_GBUS_CSR_QUEUE_USED_LOW,
					 &used_low) < 0 ||
		    virtio_gbus_read_csr(gdev,
					 base + VIRTIO_GBUS_CSR_QUEUE_USED_HIGH,
					 &used_high) < 0) {
			return -1;
		}

		modern = virtio_gbus_feature_selected(features0, features1,
						      VMM_VIRTIO_F_VERSION_1);
		virtio_gbus_sync_queue(gdev, qid, modern, num, align, pfn,
				       virtio_gbus_make_addr(desc_low, desc_high),
				       virtio_gbus_make_addr(avail_low, avail_high),
				       virtio_gbus_make_addr(used_low, used_high),
				       ready, notify_seq, page_size_changed);
	}

	gdev->update_seq = update_seq;
	gdev->valid = true;
	return 0;
}
