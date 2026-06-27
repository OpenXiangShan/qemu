#include <stddef.h>
#include <string.h>
#include "virtio_wrapper.h"
#include "virtio.h"
#include "virtio_mmio.h"
#include "virtio_gpu.h"
#include "virtio_ids.h"
#include "virtio_ring.h"
#include "virtio_config.h"
#include "utils.h"

#define VIRTIO_GPU_MAX_CMD_SIZE		(4U * 1024U * 1024U)
#define VIRTIO_GPU_RESP_BUF_SIZE	sizeof(struct virtio_gpu_resp_display_info)

struct virtio_gpu_queue {
	struct virtio_queue vq;
	struct virtio_iovec iov[VIRTIO_GPU_QUEUE_SIZE];
};

struct virtio_gpu_dev {
	struct virtio_device *vdev;
	struct virtio_gpu_queue vqs[VIRTIO_GPU_NUM_QUEUES];
	struct virtio_gpu_config config;
	uint64_t features;
	uint32_t pending_queues;
	uint32_t width;
	uint32_t height;
	uint32_t max_outputs;
};

static uint64_t virtio_gpu_get_host_features(struct virtio_device *dev)
{
	return (1ULL << VMM_VIRTIO_RING_F_EVENT_IDX) |
	       (1ULL << VMM_VIRTIO_F_VERSION_1);
}

static void virtio_gpu_set_guest_features(struct virtio_device *dev,
					  uint32_t select, uint32_t features)
{
	struct virtio_gpu_dev *gdev = dev->emu_data;

	if (1 < select)
		return;

	gdev->features &= ~((uint64_t)UINT_MAX << (select * 32));
	gdev->features |= ((uint64_t)features << (select * 32));
}

static int virtio_gpu_queue_size(uint32_t vq)
{
	switch (vq) {
	case VIRTIO_GPU_CONTROL_QUEUE:
		return VIRTIO_GPU_QUEUE_SIZE;
	case VIRTIO_GPU_CURSOR_QUEUE:
		return VIRTIO_GPU_CURSOR_QUEUE_SIZE;
	default:
		return 0;
	}
}

static int virtio_gpu_init_vq(struct virtio_device *dev, uint32_t vq,
			      uint32_t page_size, uint32_t align,
			      uint32_t pfn)
{
	struct virtio_gpu_dev *gdev = dev->emu_data;
	int size = virtio_gpu_queue_size(vq);

	if (!size)
		return -1;

	return virtio_queue_setup(dev, &gdev->vqs[vq].vq, pfn, page_size,
				  size, align);
}

static int virtio_gpu_init_vq_addr(struct virtio_device *dev, uint32_t vq,
				   uint64_t desc_addr, uint64_t avail_addr,
				   uint64_t used_addr, uint32_t size)
{
	struct virtio_gpu_dev *gdev = dev->emu_data;

	if (!virtio_gpu_queue_size(vq))
		return -1;

	return virtio_queue_setup_split(dev, &gdev->vqs[vq].vq, desc_addr,
					avail_addr, used_addr, size);
}

static int virtio_gpu_get_pfn_vq(struct virtio_device *dev, uint32_t vq)
{
	struct virtio_gpu_dev *gdev = dev->emu_data;

	if (!virtio_gpu_queue_size(vq))
		return -1;

	return virtio_queue_guest_pfn(&gdev->vqs[vq].vq);
}

static int virtio_gpu_get_size_vq(struct virtio_device *dev, uint32_t vq)
{
	return virtio_gpu_queue_size(vq);
}

static int virtio_gpu_set_size_vq(struct virtio_device *dev,
				  uint32_t vq, int size)
{
	return size;
}

static int gpu_iovec_read_len(struct virtio_iovec *iov, uint32_t iov_cnt,
			      uint32_t *ret_len)
{
	uint32_t i;
	uint64_t len = 0;

	for (i = 0; i < iov_cnt; i++) {
		if (iov[i].flags)
			continue;
		len += iov[i].len;
		if (len > VIRTIO_GPU_MAX_CMD_SIZE)
			return -1;
	}

	*ret_len = (uint32_t)len;
	return 0;
}

static uint32_t gpu_iovec_write_cap(struct virtio_iovec *iov, uint32_t iov_cnt)
{
	uint32_t i, len = 0;

	for (i = 0; i < iov_cnt; i++) {
		if (!iov[i].flags)
			continue;
		if (iov[i].len > VIRTIO_GPU_RESP_BUF_SIZE - len)
			return VIRTIO_GPU_RESP_BUF_SIZE;
		len += iov[i].len;
	}

	return len;
}

static uint32_t gpu_iovec_read(struct virtio_device *dev,
			       struct virtio_iovec *iov, uint32_t iov_cnt,
			       void *buf, uint32_t buf_len)
{
	uint32_t i, pos = 0;

	for (i = 0; i < iov_cnt && pos < buf_len; i++) {
		uint32_t len;
		int ret;

		if (iov[i].flags)
			continue;
		len = (buf_len - pos < iov[i].len) ?
		      (buf_len - pos) : iov[i].len;
		ret = my_guest_physical_read(dev, iov[i].addr,
					     (uint8_t *)buf + pos, len);
		if (ret != (int)len)
			break;
		pos += len;
	}

	return pos;
}

static uint32_t gpu_iovec_write(struct virtio_device *dev,
				struct virtio_iovec *iov, uint32_t iov_cnt,
				void *buf, uint32_t buf_len)
{
	uint32_t i, pos = 0;

	for (i = 0; i < iov_cnt && pos < buf_len; i++) {
		uint32_t len;
		int ret;

		if (!iov[i].flags)
			continue;
		len = (buf_len - pos < iov[i].len) ?
		      (buf_len - pos) : iov[i].len;
		ret = my_guest_physical_write(dev, iov[i].addr,
					      (uint8_t *)buf + pos, len);
		if (ret != (int)len)
			break;
		pos += len;
	}

	return pos;
}

static void virtio_gpu_process_queue(struct virtio_gpu_dev *gdev,
				     uint32_t qnum)
{
	struct virtio_gpu_queue *q = &gdev->vqs[qnum];
	struct virtio_device *dev = gdev->vdev;
	struct virtio_queue *vq = &q->vq;
	uint8_t resp_buf[VIRTIO_GPU_RESP_BUF_SIZE];
	int (*submit)(struct virtio_device *dev, void *cmd, int cmd_len,
		      void *resp, int resp_cap, int *resp_len);

	submit = (qnum == VIRTIO_GPU_CURSOR_QUEUE) ?
		 my_gpu_submit_cursor : my_gpu_submit_ctrl;

	while (virtio_queue_available(vq)) {
		uint16_t head = 0;
		uint32_t iov_cnt = 0, total_len = 0;
		uint32_t cmd_len, cmd_alloc_len, resp_cap;
		uint8_t *cmd_buf;
		int resp_len = 0;
		int ret;

		ret = virtio_queue_get_iovec(vq, q->iov, &iov_cnt,
					     &total_len, &head);
		if (ret) {
			my_print(dev, "%s: failed to get iovec ret=%d\n",
				 __FUNCTION__, ret);
			continue;
		}

		ret = gpu_iovec_read_len(q->iov, iov_cnt, &cmd_len);
		resp_cap = gpu_iovec_write_cap(q->iov, iov_cnt);
		if (ret || !cmd_len || !resp_cap) {
			virtio_queue_set_used_elem(vq, head, 0);
			continue;
		}

		cmd_alloc_len = cmd_len;
		cmd_buf = (uint8_t *)(uintptr_t)my_zalloc(dev, (int)cmd_len);
		if (!cmd_buf) {
			virtio_queue_set_used_elem(vq, head, 0);
			continue;
		}

		cmd_len = gpu_iovec_read(dev, q->iov, iov_cnt, cmd_buf,
					 cmd_len);
		if (cmd_len != cmd_alloc_len) {
			my_free(dev, (uint64_t)(uintptr_t)cmd_buf,
				(int)cmd_alloc_len);
			virtio_queue_set_used_elem(vq, head, 0);
			continue;
		}

		memset(resp_buf, 0, sizeof(resp_buf));
		ret = submit(dev, cmd_buf, (int)cmd_len, resp_buf,
			     (int)resp_cap, &resp_len);
		if (ret || resp_len < 0 || (uint32_t)resp_len > resp_cap)
			resp_len = 0;

		if (resp_len)
			gpu_iovec_write(dev, q->iov, iov_cnt, resp_buf,
					(uint32_t)resp_len);
		virtio_queue_set_used_elem(vq, head, (uint32_t)resp_len);
		my_free(dev, (uint64_t)(uintptr_t)cmd_buf,
			(int)cmd_alloc_len);
	}

	if (virtio_queue_should_signal(vq) && dev->vn && dev->vn->notify)
		dev->vn->notify(dev, qnum);
}

static int virtio_gpu_notify_vq(struct virtio_device *dev, uint32_t vq)
{
	struct virtio_gpu_dev *gdev = dev->emu_data;

	if (!virtio_gpu_queue_size(vq))
		return -1;

	gdev->pending_queues |= 1U << vq;
	return 0;
}

static void virtio_gpu_req_process(void *data)
{
	struct virtio_gpu_dev *gdev = data;
	uint32_t pending;
	uint32_t qnum;

	if (!gdev)
		return;

	pending = gdev->pending_queues;
	gdev->pending_queues = 0;

	for (qnum = 0; qnum < VIRTIO_GPU_NUM_QUEUES; qnum++) {
		if (pending & (1U << qnum))
			virtio_gpu_process_queue(gdev, qnum);
	}
}

static void virtio_gpu_status_changed(struct virtio_device *dev,
				      uint32_t new_status)
{
}

static int virtio_gpu_read_config(struct virtio_device *dev,
				  uint32_t offset, void *dst, uint32_t dst_len)
{
	struct virtio_gpu_dev *gdev = dev->emu_data;
	uint8_t *src = (uint8_t *)&gdev->config;
	uint32_t i, src_len = sizeof(gdev->config);

	for (i = 0; (i < dst_len) && ((offset + i) < src_len); i++)
		((uint8_t *)dst)[i] = src[offset + i];

	return 0;
}

static int virtio_gpu_write_config(struct virtio_device *dev,
				   uint32_t offset, void *src, uint32_t src_len)
{
	struct virtio_gpu_dev *gdev = dev->emu_data;
	uint8_t *dst = (uint8_t *)&gdev->config;
	uint32_t i, dst_len = sizeof(gdev->config);

	for (i = 0; (i < src_len) && ((offset + i) < dst_len); i++)
		dst[offset + i] = ((uint8_t *)src)[i];

	return 0;
}

static int virtio_gpu_reset(struct virtio_device *dev)
{
	return 0;
}

static int virtio_gpu_connect(struct virtio_device *dev,
			      struct virtio_emulator *emu)
{
	struct virtio_gpu_dev *gdev;
	struct virtio_mmio_dev *mdev = container_of(dev, struct virtio_mmio_dev, dev);

	gdev = (struct virtio_gpu_dev *)my_zalloc(dev, sizeof(*gdev));
	if (!gdev)
		return -1;

	gdev->vdev = dev;
	gdev->width = 1280;
	gdev->height = 800;
	gdev->max_outputs = 1;
	gdev->config.num_scanouts = gdev->max_outputs;
	dev->emu_data = gdev;
	mdev->cb.process_req = virtio_gpu_req_process;
	mdev->cb.data = gdev;

	return 0;
}

static void virtio_gpu_disconnect(struct virtio_device *dev)
{
	struct virtio_gpu_dev *gdev = dev->emu_data;

	if (!gdev)
		return;
	my_free(dev, (uint64_t)gdev, sizeof(*gdev));
	dev->emu_data = NULL;
}

static struct virtio_device_id virtio_gpu_emu_id[] = {
	{ .type = VMM_VIRTIO_ID_GPU },
	{ },
};

static struct virtio_emulator virtio_gpu = {
	.name = "virtio_gpu",
	.id_table = virtio_gpu_emu_id,

	.get_host_features      = virtio_gpu_get_host_features,
	.set_guest_features     = virtio_gpu_set_guest_features,
	.init_vq                = virtio_gpu_init_vq,
	.init_vq_addr           = virtio_gpu_init_vq_addr,
	.get_pfn_vq             = virtio_gpu_get_pfn_vq,
	.get_size_vq            = virtio_gpu_get_size_vq,
	.set_size_vq            = virtio_gpu_set_size_vq,
	.notify_vq              = virtio_gpu_notify_vq,
	.status_changed         = virtio_gpu_status_changed,

	.read_config = virtio_gpu_read_config,
	.write_config = virtio_gpu_write_config,
	.reset = virtio_gpu_reset,
	.connect = virtio_gpu_connect,
	.disconnect = virtio_gpu_disconnect,
};

struct virtio_emulator *virtio_gpu_emulator_create(void)
{
	return &virtio_gpu;
}
