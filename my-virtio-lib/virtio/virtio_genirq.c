#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "virtio_wrapper.h"
#include "virtio.h"
#include "virtio_mmio.h"
#include "virtio_genirq.h"
#include "virtio_ids.h"
#include "virtio_ring.h"
#include "utils.h"

struct virtio_genirq_dev {
	struct virtio_device *vdev;
	struct virtio_queue vqs[VIRTIO_GENIRQ_NUM_QUEUES];
	struct virtio_iovec iov[VIRTIO_GENIRQ_QUEUE_SIZE];
	struct virtio_iovec read_iov[VIRTIO_GENIRQ_QUEUE_SIZE];
	struct virtio_iovec write_iov[VIRTIO_GENIRQ_QUEUE_SIZE];
	struct virtio_genirq_config config;
	uint64_t features;
	uint64_t total_sends;
	uint64_t total_errors;
	uint32_t pending_queues;
};

static void virtio_genirq_refresh_config(struct virtio_genirq_dev *gdev)
{
	gdev->config.version = VIRTIO_GENIRQ_VERSION;
	gdev->config.max_targets = VIRTIO_GENIRQ_MAX_TARGETS;
	gdev->config.max_ops = VIRTIO_GENIRQ_MAX_OPS;
	gdev->config.max_repeat = VIRTIO_GENIRQ_MAX_REPEAT;
	gdev->config.total_sends = gdev->total_sends;
	gdev->config.total_errors = gdev->total_errors;
}

static int virtio_genirq_send_one(struct virtio_device *dev, uint64_t addr,
					 uint32_t data)
{
	struct virtio_genirq_dev *gdev = dev->emu_data;
	int ret;

	ret = my_genirq_send_msi(dev, addr, data);
	if (ret) {
		gdev->total_errors++;
		return ret;
	}

	gdev->total_sends++;
	return 0;
}

static int virtio_genirq_send_target(struct virtio_device *dev,
					    const struct virtio_genirq_target *target,
					    uint32_t repeat, uint32_t *sent)
{
	uint32_t i;

	if (!repeat)
		repeat = 1;
	if (repeat > VIRTIO_GENIRQ_MAX_REPEAT)
		return -1;

	for (i = 0; i < repeat; i++) {
		if (virtio_genirq_send_one(dev, target->addr, target->data))
			return -1;
		if (sent)
			(*sent)++;
	}

	return 0;
}

static uint32_t virtio_genirq_lcg_next(uint32_t *seed)
{
	*seed = *seed * 1103515245U + 12345U;
	return *seed;
}

static uint32_t virtio_genirq_run_ops(struct virtio_device *dev,
					     const struct virtio_genirq_target *targets,
					     uint32_t target_count,
					     const struct virtio_genirq_op *ops,
					     uint32_t op_count,
					     uint32_t *error_op)
{
	uint32_t i, j, sent = 0;

	for (i = 0; i < op_count; i++) {
		const struct virtio_genirq_op *op = &ops[i];

		switch (op->type) {
		case VIRTIO_GENIRQ_OP_SEND:
			if (op->target >= target_count)
				goto invalid;
			if (virtio_genirq_send_target(dev, &targets[op->target],
							    op->count, &sent))
				goto ioerr;
			break;
		case VIRTIO_GENIRQ_OP_SEND_RANGE:
		{
			uint32_t stride = op->arg1 ? (uint32_t)op->arg1 : 1;
			uint32_t repeat = op->arg0 ? (uint32_t)op->arg0 : 1;

			for (j = 0; j < op->count; j++) {
				uint64_t idx64 = op->target + (uint64_t)j * stride;

				if (idx64 >= target_count)
					goto invalid;
				if (virtio_genirq_send_target(dev, &targets[idx64],
								    repeat, &sent))
					goto ioerr;
			}
			break;
		}
		case VIRTIO_GENIRQ_OP_DELAY_NS:
			break;
		case VIRTIO_GENIRQ_OP_RANDOM:
		{
			uint32_t seed = op->arg0 ? (uint32_t)op->arg0 : 1;
			uint32_t count = op->count ? op->count : 1;

			for (j = 0; j < count; j++) {
				uint32_t idx = virtio_genirq_lcg_next(&seed) % target_count;

				if (virtio_genirq_send_target(dev, &targets[idx], 1,
								    &sent))
					goto ioerr;
			}
			break;
		}
		default:
			*error_op = i;
			return VIRTIO_GENIRQ_STATUS_UNSUPPORTED;
		}
	}

	return VIRTIO_GENIRQ_STATUS_OK;

invalid:
	*error_op = i;
	return VIRTIO_GENIRQ_STATUS_INVALID;
ioerr:
	*error_op = i;
	return VIRTIO_GENIRQ_STATUS_IOERR;
}

static uint32_t virtio_genirq_handle_request(struct virtio_device *dev,
						    uint8_t *req_buf,
						    uint32_t req_len,
						    struct virtio_genirq_resp *resp)
{
	struct virtio_genirq_dev *gdev = dev->emu_data;
	struct virtio_genirq_req_hdr hdr;
	struct virtio_genirq_target *targets;
	struct virtio_genirq_op *ops;
	uint32_t min_len, status, error_op = 0;

	memset(resp, 0, sizeof(*resp));
	resp->status = VIRTIO_GENIRQ_STATUS_INVALID;
	resp->error_op = UINT_MAX;

	if (req_len < sizeof(hdr))
		return resp->status;

	memcpy(&hdr, req_buf, sizeof(hdr));
	resp->seq = hdr.seq;

	if (hdr.magic != VIRTIO_GENIRQ_REQ_MAGIC ||
	    hdr.version != VIRTIO_GENIRQ_VERSION)
		return resp->status;

	if (hdr.target_count > VIRTIO_GENIRQ_MAX_TARGETS ||
	    hdr.op_count > VIRTIO_GENIRQ_MAX_OPS)
		return resp->status;

	min_len = sizeof(hdr) + hdr.target_count * sizeof(*targets) +
		  hdr.op_count * sizeof(*ops);
	if (req_len < min_len)
		return resp->status;

	targets = (void *)(req_buf + sizeof(hdr));
	ops = (void *)(req_buf + sizeof(hdr) + hdr.target_count * sizeof(*targets));

	switch (hdr.opcode) {
	case VIRTIO_GENIRQ_CMD_RUN:
		if (!hdr.target_count)
			break;
		if (!hdr.op_count) {
			struct virtio_genirq_op op = {
				.type = VIRTIO_GENIRQ_OP_SEND_RANGE,
				.target = 0,
				.count = hdr.target_count,
				.arg0 = 1,
				.arg1 = 1,
			};

			status = virtio_genirq_run_ops(dev, targets, hdr.target_count,
							    &op, 1, &error_op);
		} else {
			status = virtio_genirq_run_ops(dev, targets, hdr.target_count,
							    ops, hdr.op_count, &error_op);
		}
		resp->status = status;
		resp->error_op = error_op;
		break;
	case VIRTIO_GENIRQ_CMD_GET_STATS:
		resp->status = VIRTIO_GENIRQ_STATUS_OK;
		break;
	case VIRTIO_GENIRQ_CMD_RESET_STATS:
		gdev->total_sends = 0;
		gdev->total_errors = 0;
		resp->status = VIRTIO_GENIRQ_STATUS_OK;
		break;
	default:
		resp->status = VIRTIO_GENIRQ_STATUS_UNSUPPORTED;
		break;
	}

	resp->sends = gdev->total_sends;
	resp->errors = gdev->total_errors;
	virtio_genirq_refresh_config(gdev);

	return resp->status;
}

static uint64_t virtio_genirq_get_host_features(struct virtio_device *dev)
{
	return 1UL << VMM_VIRTIO_RING_F_EVENT_IDX;
}

static void virtio_genirq_set_guest_features(struct virtio_device *dev,
						     uint32_t select,
						     uint32_t features)
{
	struct virtio_genirq_dev *gdev = dev->emu_data;

	if (select > 1)
		return;

	gdev->features &= ~((uint64_t)UINT_MAX << (select * 32));
	gdev->features |= ((uint64_t)features << (select * 32));
}

static int virtio_genirq_init_vq(struct virtio_device *dev, uint32_t vq,
					uint32_t page_size, uint32_t align,
					uint32_t pfn)
{
	struct virtio_genirq_dev *gdev = dev->emu_data;

	if (vq != VIRTIO_GENIRQ_CMD_QUEUE)
		return -1;

	return virtio_queue_setup(dev, &gdev->vqs[vq], pfn, page_size,
				  VIRTIO_GENIRQ_QUEUE_SIZE, align);
}

static int virtio_genirq_init_vq_addr(struct virtio_device *dev, uint32_t vq,
					     uint64_t desc_addr, uint64_t avail_addr,
					     uint64_t used_addr, uint32_t size)
{
	struct virtio_genirq_dev *gdev = dev->emu_data;

	if (vq != VIRTIO_GENIRQ_CMD_QUEUE)
		return -1;

	return virtio_queue_setup_addr(dev, &gdev->vqs[vq], desc_addr,
					avail_addr, used_addr, size);
}

static void virtio_genirq_reset_vq(struct virtio_device *dev, uint32_t vq)
{
	struct virtio_genirq_dev *gdev = dev->emu_data;

	if (!gdev || vq >= VIRTIO_GENIRQ_NUM_QUEUES)
		return;

	my_virtio_queue_reset(&gdev->vqs[vq]);
}

static int virtio_genirq_get_pfn_vq(struct virtio_device *dev, uint32_t vq)
{
	struct virtio_genirq_dev *gdev = dev->emu_data;

	if (vq != VIRTIO_GENIRQ_CMD_QUEUE)
		return -1;

	return virtio_queue_guest_pfn(&gdev->vqs[vq]);
}

static int virtio_genirq_get_size_vq(struct virtio_device *dev, uint32_t vq)
{
	return vq == VIRTIO_GENIRQ_CMD_QUEUE ? VIRTIO_GENIRQ_QUEUE_SIZE : 0;
}

static int virtio_genirq_set_size_vq(struct virtio_device *dev, uint32_t vq,
					    int size)
{
	return size;
}

static int virtio_genirq_notify_vq(struct virtio_device *dev, uint32_t vq)
{
	struct virtio_genirq_dev *gdev = dev->emu_data;

	if (vq != VIRTIO_GENIRQ_CMD_QUEUE)
		return -1;

	gdev->pending_queues |= 1U << vq;
	return 0;
}

static void virtio_genirq_complete(struct virtio_device *dev,
					  struct virtio_queue *vq, uint16_t head,
					  struct virtio_iovec *write_iov,
					  uint32_t write_iov_cnt,
					  struct virtio_genirq_resp *resp)
{
	uint32_t len = 0;

	if (write_iov_cnt)
		len = virtio_buf_to_iovec_write(dev, write_iov, write_iov_cnt,
						  resp, sizeof(*resp));

	virtio_queue_set_used_elem(vq, head, len);
	if (virtio_queue_should_signal(vq) && dev->vn && dev->vn->notify)
		dev->vn->notify(dev, VIRTIO_GENIRQ_CMD_QUEUE);
}

static void virtio_genirq_req_process(void *data)
{
	struct virtio_genirq_dev *gdev = data;
	struct virtio_device *dev;
	struct virtio_queue *vq;

	if (!gdev)
		return;

	dev = gdev->vdev;
	vq = &gdev->vqs[VIRTIO_GENIRQ_CMD_QUEUE];
	gdev->pending_queues = 0;

	while (virtio_queue_available(vq)) {
		struct virtio_genirq_resp resp;
		uint16_t head = 0;
		uint32_t i, iov_cnt = 0, total_len = 0;
		uint32_t read_iov_cnt = 0, write_iov_cnt = 0, req_len = 0;
		uint8_t *req_buf;
		int rc;

		rc = virtio_queue_get_iovec(vq, gdev->iov, &iov_cnt,
					       &total_len, &head);
		if (rc) {
			my_print(dev, "%s: failed to get iovec error=%d\n",
				 __FUNCTION__, rc);
			continue;
		}

		for (i = 0; i < iov_cnt; i++) {
			if (gdev->iov[i].flags) {
				gdev->write_iov[write_iov_cnt++] = gdev->iov[i];
			} else {
				gdev->read_iov[read_iov_cnt++] = gdev->iov[i];
				req_len += gdev->iov[i].len;
			}
		}

		req_buf = (void *)(uintptr_t)my_zalloc(dev, req_len ? req_len : 1);
		if (!req_buf) {
			memset(&resp, 0, sizeof(resp));
			resp.status = VIRTIO_GENIRQ_STATUS_IOERR;
			virtio_genirq_complete(dev, vq, head, gdev->write_iov,
					       write_iov_cnt, &resp);
			continue;
		}

		if (read_iov_cnt)
			virtio_iovec_to_buf_read(dev, gdev->read_iov, read_iov_cnt,
						 req_buf, req_len);
		virtio_genirq_handle_request(dev, req_buf, req_len, &resp);
		virtio_genirq_complete(dev, vq, head, gdev->write_iov,
					       write_iov_cnt, &resp);
		my_free(dev, (uint64_t)(uintptr_t)req_buf, req_len ? req_len : 1);
	}
}

static void virtio_genirq_status_changed(struct virtio_device *dev,
						 uint32_t new_status)
{
}

static int virtio_genirq_raw_send(struct virtio_device *dev)
{
	struct virtio_genirq_dev *gdev = dev->emu_data;
	uint32_t i, count = gdev->config.raw_count;
	uint64_t addr = gdev->config.raw_addr;
	uint32_t data = gdev->config.raw_data;

	if (!count)
		count = 1;
	if (count > VIRTIO_GENIRQ_MAX_REPEAT) {
		gdev->config.raw_status = VIRTIO_GENIRQ_STATUS_INVALID;
		return -1;
	}

	for (i = 0; i < count; i++) {
		uint32_t value = data;

		if (gdev->config.raw_flags & VIRTIO_GENIRQ_RAW_F_DATA_INC)
			value += i;
		if (virtio_genirq_send_one(dev, addr, value)) {
			gdev->config.raw_status = VIRTIO_GENIRQ_STATUS_IOERR;
			return -1;
		}
		addr += gdev->config.raw_stride;
	}

	gdev->config.raw_status = VIRTIO_GENIRQ_STATUS_OK;
	virtio_genirq_refresh_config(gdev);
	return 0;
}

static int virtio_genirq_read_config(struct virtio_device *dev,
					    uint32_t offset, void *dst,
					    uint32_t dst_len)
{
	struct virtio_genirq_dev *gdev = dev->emu_data;
	uint8_t *src;
	uint32_t i;

	virtio_genirq_refresh_config(gdev);
	src = (uint8_t *)&gdev->config;
	for (i = 0; (i < dst_len) && ((offset + i) < sizeof(gdev->config)); i++)
		((uint8_t *)dst)[i] = src[offset + i];

	return 0;
}

static int virtio_genirq_write_config(struct virtio_device *dev,
					     uint32_t offset, void *src,
					     uint32_t src_len)
{
	struct virtio_genirq_dev *gdev = dev->emu_data;
	uint32_t val;

	if (src_len != 4)
		return -1;

	val = *(uint32_t *)src;
	switch (offset) {
	case offsetof(struct virtio_genirq_config, raw_flags):
		gdev->config.raw_flags = val;
		break;
	case offsetof(struct virtio_genirq_config, raw_addr):
		gdev->config.raw_addr &= 0xffffffff00000000ULL;
		gdev->config.raw_addr |= val;
		break;
	case offsetof(struct virtio_genirq_config, raw_addr) + 4:
		gdev->config.raw_addr &= 0xffffffffULL;
		gdev->config.raw_addr |= (uint64_t)val << 32;
		break;
	case offsetof(struct virtio_genirq_config, raw_data):
		gdev->config.raw_data = val;
		break;
	case offsetof(struct virtio_genirq_config, raw_count):
		gdev->config.raw_count = val;
		break;
	case offsetof(struct virtio_genirq_config, raw_stride):
		gdev->config.raw_stride = val;
		break;
	case offsetof(struct virtio_genirq_config, raw_kick):
		gdev->config.raw_kick = val;
		if (val == VIRTIO_GENIRQ_RAW_KICK_SEND)
			return virtio_genirq_raw_send(dev);
		break;
	default:
		return -1;
	}

	return 0;
}

static int virtio_genirq_reset(struct virtio_device *dev)
{
	struct virtio_genirq_dev *gdev = dev->emu_data;

	if (!gdev)
		return 0;

	virtio_genirq_reset_vq(dev, VIRTIO_GENIRQ_CMD_QUEUE);
	gdev->features = 0;
	gdev->pending_queues = 0;
	gdev->config.raw_status = 0;
	gdev->config.raw_flags = 0;
	gdev->config.raw_addr = 0;
	gdev->config.raw_data = 0;
	gdev->config.raw_count = 0;
	gdev->config.raw_stride = 0;
	gdev->config.raw_kick = 0;
	virtio_genirq_refresh_config(gdev);

	return 0;
}

static int virtio_genirq_connect(struct virtio_device *dev,
					struct virtio_emulator *emu)
{
	struct virtio_genirq_dev *gdev;
	struct virtio_mmio_dev *mdev = container_of(dev, struct virtio_mmio_dev, dev);

	gdev = (struct virtio_genirq_dev *)my_zalloc(dev, sizeof(*gdev));
	if (!gdev) {
		my_print(dev, "%s alloc virtio_genirq_dev failed\n", __FUNCTION__);
		return -1;
	}

	gdev->vdev = dev;
	dev->emu_data = gdev;
	virtio_genirq_reset(dev);

	mdev->cb.process_req = virtio_genirq_req_process;
	mdev->cb.data = gdev;

	return 0;
}

static void virtio_genirq_disconnect(struct virtio_device *dev)
{
}

static struct virtio_device_id virtio_genirq_emu_id[] = {
	{ .type = VMM_VIRTIO_ID_GENIRQ },
	{ },
};

static struct virtio_emulator virtio_genirq = {
	.name = "virtio_genirq",
	.id_table = virtio_genirq_emu_id,
	.get_host_features = virtio_genirq_get_host_features,
	.set_guest_features = virtio_genirq_set_guest_features,
	.init_vq = virtio_genirq_init_vq,
	.init_vq_addr = virtio_genirq_init_vq_addr,
	.reset_vq = virtio_genirq_reset_vq,
	.get_pfn_vq = virtio_genirq_get_pfn_vq,
	.get_size_vq = virtio_genirq_get_size_vq,
	.set_size_vq = virtio_genirq_set_size_vq,
	.notify_vq = virtio_genirq_notify_vq,
	.status_changed = virtio_genirq_status_changed,
	.read_config = virtio_genirq_read_config,
	.write_config = virtio_genirq_write_config,
	.reset = virtio_genirq_reset,
	.connect = virtio_genirq_connect,
	.disconnect = virtio_genirq_disconnect,
};

struct virtio_emulator *virtio_genirq_emulator_create(void)
{
	return &virtio_genirq;
}
