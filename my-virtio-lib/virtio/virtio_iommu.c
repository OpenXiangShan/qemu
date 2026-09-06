#include <errno.h>
#include <endian.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "io_virtio_iommu.h"
#include "virtio.h"
#include "virtio_ids.h"
#include "virtio_iommu.h"
#include "virtio_mmio.h"
#include "utils.h"

struct virtio_iommu_dev {
	struct virtio_device *vdev;
	struct virtio_queue vqs[VIRTIO_IOMMU_NUM_VQS];
	struct virtio_iovec iov[VIRTIO_QUEUE_MAX_DESC];
	struct virtio_iovec read_iov[VIRTIO_QUEUE_MAX_DESC];
	struct virtio_iovec write_iov[VIRTIO_QUEUE_MAX_DESC];
	struct virtio_iommu_config config;
	IoVirtioIommu *refmodel;
	enum virtio_iommu_stage_mode mode;
	uint64_t features;
	bool pending_request;
};

static bool virtio_iommu_trace_enabled(void)
{
	return getenv("MY_VIRTIO_IOMMU_TRACE") != NULL;
}

static IoVirtioIommuMode iommu_refmodel_mode(
					enum virtio_iommu_stage_mode mode)
{
	switch (mode) {
	case VIRTIO_IOMMU_STAGE_S2:
		return IO_VIRTIO_IOMMU_MODE_S2;
	case VIRTIO_IOMMU_STAGE_S1_S2:
		return IO_VIRTIO_IOMMU_MODE_S1_S2;
	case VIRTIO_IOMMU_STAGE_S1:
	default:
		return IO_VIRTIO_IOMMU_MODE_S1;
	}
}

static int iommu_status_from_errno(int ret)
{
	if (ret >= VIRTIO_IOMMU_S_OK && ret <= VIRTIO_IOMMU_S_NOMEM)
		return ret;
	if (ret == -ENOMEM)
		return VIRTIO_IOMMU_S_NOMEM;
	if (ret == -EINVAL || ret == -EOVERFLOW)
		return VIRTIO_IOMMU_S_INVAL;
	if (ret == -EEXIST)
		return VIRTIO_IOMMU_S_INVAL;
	if (ret == -ERANGE)
		return VIRTIO_IOMMU_S_RANGE;
	if (ret == -ENOENT)
		return VIRTIO_IOMMU_S_NOENT;
	if (ret == -EOPNOTSUPP || ret == -ENOSYS)
		return VIRTIO_IOMMU_S_UNSUPP;
	if (ret == -EFAULT)
		return VIRTIO_IOMMU_S_FAULT;
	return VIRTIO_IOMMU_S_IOERR;
}

static uint32_t iommu_feature_bits(void)
{
	return (1U << VIRTIO_IOMMU_F_INPUT_RANGE) |
	       (1U << VIRTIO_IOMMU_F_DOMAIN_RANGE) |
	       (1U << VIRTIO_IOMMU_F_MAP_UNMAP) |
	       (1U << VIRTIO_IOMMU_F_PROBE) |
	       (1U << VIRTIO_IOMMU_F_MMIO);
}

static uint64_t virtio_iommu_get_host_features(struct virtio_device *dev)
{
	(void)dev;
	return iommu_feature_bits();
}

static void virtio_iommu_set_guest_features(struct virtio_device *dev,
						uint32_t select, uint32_t features)
{
	struct virtio_iommu_dev *idev = dev ? dev->emu_data : NULL;

	if (!idev || select > 1)
		return;
	idev->features &= ~((uint64_t)UINT32_MAX << (select * 32));
	idev->features |= (uint64_t)features << (select * 32);
}

static int virtio_iommu_init_vq(struct virtio_device *dev, uint32_t vq,
					uint32_t page_size, uint32_t align,
					uint32_t pfn)
{
	struct virtio_iommu_dev *idev = dev ? dev->emu_data : NULL;

	if (!idev || vq >= VIRTIO_IOMMU_NUM_VQS)
		return -1;
	return virtio_queue_setup(dev, &idev->vqs[vq], pfn, page_size,
				  VIRTIO_IOMMU_QUEUE_SIZE, align);
}

static int virtio_iommu_init_vq_addr(struct virtio_device *dev, uint32_t vq,
					     uint64_t desc_addr, uint64_t avail_addr,
					     uint64_t used_addr, uint32_t size)
{
	struct virtio_iommu_dev *idev = dev ? dev->emu_data : NULL;

	if (!idev || vq >= VIRTIO_IOMMU_NUM_VQS || !size ||
	    size > VIRTIO_QUEUE_MAX_DESC)
		return -1;
	return virtio_queue_setup_addr(dev, &idev->vqs[vq], desc_addr,
				       avail_addr, used_addr, size);
}

static void virtio_iommu_reset_vq(struct virtio_device *dev, uint32_t vq)
{
	struct virtio_iommu_dev *idev = dev ? dev->emu_data : NULL;

	if (!idev || vq >= VIRTIO_IOMMU_NUM_VQS)
		return;
	my_virtio_queue_reset(&idev->vqs[vq]);
}

static int virtio_iommu_get_pfn_vq(struct virtio_device *dev, uint32_t vq)
{
	struct virtio_iommu_dev *idev = dev ? dev->emu_data : NULL;

	if (!idev || vq >= VIRTIO_IOMMU_NUM_VQS)
		return -1;
	return virtio_queue_guest_pfn(&idev->vqs[vq]);
}

static int virtio_iommu_get_size_vq(struct virtio_device *dev, uint32_t vq)
{
	(void)dev;
	return vq < VIRTIO_IOMMU_NUM_VQS ? VIRTIO_IOMMU_QUEUE_SIZE : 0;
}

static int virtio_iommu_set_size_vq(struct virtio_device *dev, uint32_t vq,
					    int size)
{
	(void)dev;
	if (vq >= VIRTIO_IOMMU_NUM_VQS || size <= 0 ||
	    size > VIRTIO_QUEUE_MAX_DESC)
		return -1;
	return size;
}

static int virtio_iommu_notify_vq(struct virtio_device *dev, uint32_t vq)
{
	struct virtio_iommu_dev *idev = dev ? dev->emu_data : NULL;

	if (!idev || vq >= VIRTIO_IOMMU_NUM_VQS)
		return -1;
	if (vq == VIRTIO_IOMMU_REQUEST_VQ)
		idev->pending_request = true;
	return 0;
}

static int virtio_iommu_read_config(struct virtio_device *dev,
					    uint32_t offset, void *dst,
					    uint32_t dst_len)
{
	struct virtio_iommu_dev *idev = dev ? dev->emu_data : NULL;
	const uint8_t *src;

	if (!idev || !dst || offset >= sizeof(idev->config))
		return -1;
	src = (const uint8_t *)&idev->config;
	if (dst_len > sizeof(idev->config) - offset)
		dst_len = sizeof(idev->config) - offset;
	memcpy(dst, src + offset, dst_len);
	return 0;
}

static int virtio_iommu_write_config(struct virtio_device *dev,
					     uint32_t offset, void *src,
					     uint32_t src_len)
{
	(void)dev;
	(void)offset;
	(void)src;
	(void)src_len;
	return -1;
}

static int virtio_iommu_handle_request(struct virtio_iommu_dev *idev,
					       const uint8_t *buf, uint32_t len)
{
	uint32_t domain, endpoint, flags;
	uint64_t virt_start, virt_end, phys_start;
	int ret;

	if (!idev->refmodel || len < sizeof(struct virtio_iommu_req_head))
		return VIRTIO_IOMMU_S_UNSUPP;

	switch (buf[0]) {
	case VIRTIO_IOMMU_T_ATTACH:
		if (len != sizeof(struct virtio_iommu_req_attach) -
			   sizeof(struct virtio_iommu_req_tail))
			return VIRTIO_IOMMU_S_INVAL;
		memcpy(&domain, buf + 4, sizeof(domain));
		memcpy(&endpoint, buf + 8, sizeof(endpoint));
		memcpy(&flags, buf + 12, sizeof(flags));
		domain = le32toh(domain);
		endpoint = le32toh(endpoint);
		flags = le32toh(flags);
		if (flags & ~VIRTIO_IOMMU_ATTACH_F_BYPASS)
			return VIRTIO_IOMMU_S_INVAL;
		ret = io_virtio_iommu_attach(idev->refmodel, domain, endpoint,
					     flags,
					     iommu_refmodel_mode(idev->mode));
		if (virtio_iommu_trace_enabled())
			my_print(idev->vdev, "viommu attach domain=%u endpoint=%u flags=%#x mode=%u ret=%d\n",
				 domain, endpoint, flags, idev->mode, ret);
		return iommu_status_from_errno(ret);
	case VIRTIO_IOMMU_T_DETACH:
		if (len != sizeof(struct virtio_iommu_req_detach) -
			   sizeof(struct virtio_iommu_req_tail))
			return VIRTIO_IOMMU_S_INVAL;
		memcpy(&domain, buf + 4, sizeof(domain));
		memcpy(&endpoint, buf + 8, sizeof(endpoint));
		domain = le32toh(domain);
		endpoint = le32toh(endpoint);
		ret = io_virtio_iommu_detach(idev->refmodel, domain, endpoint);
		if (virtio_iommu_trace_enabled())
			my_print(idev->vdev, "viommu detach domain=%u endpoint=%u ret=%d\n",
				 domain, endpoint, ret);
		return iommu_status_from_errno(ret);
	case VIRTIO_IOMMU_T_MAP:
		if (len != sizeof(struct virtio_iommu_req_map) -
			   sizeof(struct virtio_iommu_req_tail))
			return VIRTIO_IOMMU_S_INVAL;
		memcpy(&domain, buf + 4, sizeof(domain));
		memcpy(&virt_start, buf + 8, sizeof(virt_start));
		memcpy(&virt_end, buf + 16, sizeof(virt_end));
		memcpy(&phys_start, buf + 24, sizeof(phys_start));
		memcpy(&flags, buf + 32, sizeof(flags));
		domain = le32toh(domain);
		virt_start = le64toh(virt_start);
		virt_end = le64toh(virt_end);
		phys_start = le64toh(phys_start);
		flags = le32toh(flags);
		if (virt_end < virt_start)
			return VIRTIO_IOMMU_S_RANGE;
		if ((flags & ~VIRTIO_IOMMU_MAP_F_MASK) ||
		    !(flags & (VIRTIO_IOMMU_MAP_F_READ |
			       VIRTIO_IOMMU_MAP_F_WRITE)))
			return VIRTIO_IOMMU_S_INVAL;
		ret = io_virtio_iommu_map(idev->refmodel, domain, virt_start,
					  virt_end, phys_start, flags,
					  iommu_refmodel_mode(idev->mode));
		if (virtio_iommu_trace_enabled())
			my_print(idev->vdev, "viommu map domain=%u iova=%#llx-%#llx phys=%#llx flags=%#x mode=%u ret=%d\n",
				 domain, (unsigned long long)virt_start,
				 (unsigned long long)virt_end,
				 (unsigned long long)phys_start, flags, idev->mode, ret);
		return iommu_status_from_errno(ret);
	case VIRTIO_IOMMU_T_UNMAP:
		if (len != sizeof(struct virtio_iommu_req_unmap) -
			   sizeof(struct virtio_iommu_req_tail))
			return VIRTIO_IOMMU_S_INVAL;
		memcpy(&domain, buf + 4, sizeof(domain));
		memcpy(&virt_start, buf + 8, sizeof(virt_start));
		memcpy(&virt_end, buf + 16, sizeof(virt_end));
		domain = le32toh(domain);
		virt_start = le64toh(virt_start);
		virt_end = le64toh(virt_end);
		if (virt_end < virt_start)
			return VIRTIO_IOMMU_S_RANGE;
		ret = io_virtio_iommu_unmap(idev->refmodel, domain, virt_start,
					    virt_end,
					    iommu_refmodel_mode(idev->mode));
		if (virtio_iommu_trace_enabled())
			my_print(idev->vdev, "viommu unmap domain=%u iova=%#llx-%#llx mode=%u ret=%d\n",
				 domain, (unsigned long long)virt_start,
				 (unsigned long long)virt_end, idev->mode, ret);
		return iommu_status_from_errno(ret);
	case VIRTIO_IOMMU_T_PROBE:
		if (len != sizeof(struct virtio_iommu_req_probe))
			return VIRTIO_IOMMU_S_INVAL;
		return VIRTIO_IOMMU_S_OK;
	default:
		return VIRTIO_IOMMU_S_UNSUPP;
	}
}

static void virtio_iommu_complete(struct virtio_iommu_dev *idev,
					  struct virtio_queue *vq, uint16_t head,
					  struct virtio_iovec *write_iov,
					  uint32_t write_iov_cnt, int status)
{
	struct virtio_iommu_req_tail tail;
	uint32_t written = 0;

	memset(&tail, 0, sizeof(tail));
	tail.status = (uint8_t)status;
	if (write_iov_cnt)
		written = virtio_buf_to_iovec_write(idev->vdev, write_iov,
						write_iov_cnt, &tail, sizeof(tail));
	virtio_queue_set_used_elem(vq, head, written);
	if (virtio_queue_should_signal(vq) && idev->vdev->vn &&
	    idev->vdev->vn->notify)
		idev->vdev->vn->notify(idev->vdev, VIRTIO_IOMMU_REQUEST_VQ);
}

static void virtio_iommu_process_request(void *data)
{
	struct virtio_iommu_dev *idev = data;
	struct virtio_queue *vq;

	if (!idev)
		return;
	vq = &idev->vqs[VIRTIO_IOMMU_REQUEST_VQ];
	idev->pending_request = false;
	while (virtio_queue_available(vq)) {
		uint32_t iov_cnt = 0, total_len = 0, req_len = 0;
		uint32_t read_iov_cnt = 0, write_iov_cnt = 0;
		uint32_t write_len = 0;
		uint16_t head = 0;
		uint8_t *req_buf;
		bool write_seen = false;
		bool bad_order = false;
		int status;

		if (virtio_queue_get_iovec(vq, idev->iov, &iov_cnt, &total_len,
					   &head))
			break;
		for (uint32_t i = 0; i < iov_cnt; i++) {
			if (idev->iov[i].flags) {
				write_seen = true;
				idev->write_iov[write_iov_cnt++] = idev->iov[i];
				write_len += idev->iov[i].len;
			} else {
				if (write_seen)
					bad_order = true;
				idev->read_iov[read_iov_cnt++] = idev->iov[i];
				req_len += idev->iov[i].len;
			}
		}
		if (bad_order || write_len < sizeof(struct virtio_iommu_req_tail) ||
		    !req_len || req_len > 4096) {
			virtio_iommu_complete(idev, vq, head, idev->write_iov,
					      write_iov_cnt, VIRTIO_IOMMU_S_INVAL);
			continue;
		}
		req_buf = (uint8_t *)(uintptr_t)my_zalloc(idev->vdev, req_len);
		if (!req_buf) {
			virtio_iommu_complete(idev, vq, head, idev->write_iov,
					      write_iov_cnt, VIRTIO_IOMMU_S_NOMEM);
			continue;
		}
		if (virtio_iovec_to_buf_read(idev->vdev, idev->read_iov,
					     read_iov_cnt, req_buf, req_len) != req_len)
			status = VIRTIO_IOMMU_S_IOERR;
		else
			status = virtio_iommu_handle_request(idev, req_buf, req_len);
		virtio_iommu_complete(idev, vq, head, idev->write_iov,
				      write_iov_cnt, status);
		my_free(idev->vdev, (uint64_t)(uintptr_t)req_buf, req_len);
	}
}

static int virtio_iommu_reset(struct virtio_device *dev)
{
	struct virtio_iommu_dev *idev = dev ? dev->emu_data : NULL;

	if (!idev)
		return 0;
	virtio_iommu_reset_vq(dev, VIRTIO_IOMMU_REQUEST_VQ);
	virtio_iommu_reset_vq(dev, VIRTIO_IOMMU_EVENT_VQ);
	idev->features = 0;
	idev->pending_request = false;
	return idev->refmodel ? io_virtio_iommu_reset(idev->refmodel) : 0;
}

static int virtio_iommu_connect(struct virtio_device *dev,
					struct virtio_emulator *emu)
{
	struct virtio_mmio_dev *mdev = container_of(dev, struct virtio_mmio_dev, dev);
	struct virtio_iommu_dev *idev;
	IoVirtioIommuConfig refmodel_config;

	if (mdev->iommu_mode < VIRTIO_IOMMU_STAGE_S1 ||
	    mdev->iommu_mode > VIRTIO_IOMMU_STAGE_S1_S2)
		return -1;
	idev = (struct virtio_iommu_dev *)(uintptr_t)my_zalloc(dev, sizeof(*idev));
	if (!idev)
		return -1;
	idev->vdev = dev;
	idev->mode = mdev->iommu_mode;
	memset(&refmodel_config, 0, sizeof(refmodel_config));
	refmodel_config.mode = iommu_refmodel_mode(idev->mode);
	refmodel_config.max_endpoints = 64;
	idev->refmodel = io_virtio_iommu_create(&refmodel_config);
	if (!idev->refmodel) {
		my_free(dev, (uint64_t)(uintptr_t)idev, sizeof(*idev));
		return -1;
	}
	idev->config.page_size_mask = htole64(1ULL << 12);
	idev->config.input_range.start = htole64(0);
	idev->config.input_range.end = htole64(
		idev->mode == VIRTIO_IOMMU_STAGE_S2 ?
		(1ULL << 41) - 1 : (1ULL << 38) - 1);
	idev->config.domain_range.start = htole32(1);
	idev->config.domain_range.end = htole32(
		idev->mode == VIRTIO_IOMMU_STAGE_S1 ?
		(1U << 20) - 1 : (1U << 16) - 1);
	idev->config.probe_size = htole32(VIRTIO_IOMMU_PROBE_SIZE);
	idev->config.bypass = 0;
	dev->emu_data = idev;
	mdev->cb.process_req = virtio_iommu_process_request;
	mdev->cb.data = idev;
	(void)emu;
	return 0;
}

static void virtio_iommu_disconnect(struct virtio_device *dev)
{
	struct virtio_iommu_dev *idev = dev ? dev->emu_data : NULL;

	if (idev) {
		io_virtio_iommu_destroy(idev->refmodel);
		my_free(dev, (uint64_t)(uintptr_t)idev, sizeof(*idev));
		dev->emu_data = NULL;
	}
}

static struct virtio_device_id virtio_iommu_emu_id[] = {
	{ .type = VMM_VIRTIO_ID_IOMMU },
	{ },
};

static struct virtio_emulator virtio_iommu = {
	.name = VIRTIO_EMU_NAME_IOMMU,
	.id_table = virtio_iommu_emu_id,
	.get_host_features = virtio_iommu_get_host_features,
	.set_guest_features = virtio_iommu_set_guest_features,
	.init_vq = virtio_iommu_init_vq,
	.init_vq_addr = virtio_iommu_init_vq_addr,
	.reset_vq = virtio_iommu_reset_vq,
	.get_pfn_vq = virtio_iommu_get_pfn_vq,
	.get_size_vq = virtio_iommu_get_size_vq,
	.set_size_vq = virtio_iommu_set_size_vq,
	.notify_vq = virtio_iommu_notify_vq,
	.read_config = virtio_iommu_read_config,
	.write_config = virtio_iommu_write_config,
	.reset = virtio_iommu_reset,
	.connect = virtio_iommu_connect,
	.disconnect = virtio_iommu_disconnect,
};

struct virtio_emulator *virtio_iommu_emulator_create(void)
{
	return &virtio_iommu;
}

static struct virtio_iommu_dev *virtio_iommu_from_handle(
						virtio_handle_t handle)
{
	struct virtio_mmio_dev *mdev = handle;

	if (!mdev || !mdev->dev.emu || !mdev->dev.emu->id_table ||
	    mdev->dev.emu->id_table[0].type != VMM_VIRTIO_ID_IOMMU)
		return NULL;
	return mdev->dev.emu_data;
}

int virtio_iommu_translate(virtio_handle_t handle, uint32_t endpoint,
				   uint64_t addr, uint32_t len, uint32_t access,
				   uint64_t *translated, uint64_t *addr_mask)
{
	struct virtio_mmio_dev *mdev = handle;
	struct virtio_iommu_dev *idev = virtio_iommu_from_handle(handle);

	if (!idev || !idev->refmodel || !translated || !addr_mask)
		return -1;
	if (idev->pending_request && mdev->cb.process_req)
		mdev->cb.process_req(mdev->cb.data);
	return io_virtio_iommu_translate(idev->refmodel, endpoint, addr, len,
					 access, translated, addr_mask);
}

int virtio_iommu_report_fault(virtio_handle_t handle, uint32_t endpoint,
				      uint64_t addr, uint32_t access)
{
	struct virtio_mmio_dev *mdev = handle;
	struct virtio_iommu_dev *idev = virtio_iommu_from_handle(handle);
	struct virtio_queue *vq;
	struct virtio_iovec iov[VIRTIO_QUEUE_MAX_DESC];
	uint32_t iov_cnt, total_len;
	uint16_t head;
	struct virtio_iommu_fault fault;
	IoVirtioIommuFault backend_fault;

	if (!idev || !idev->refmodel)
		return -1;
	vq = &idev->vqs[VIRTIO_IOMMU_EVENT_VQ];
	if (!virtio_queue_available(vq))
		return -1;
	if (virtio_queue_get_iovec(vq, iov, &iov_cnt, &total_len, &head))
		return -1;
	if (total_len < sizeof(fault)) {
		virtio_queue_set_used_elem(vq, head, 0);
		return -1;
	}
	for (uint32_t i = 0; i < iov_cnt; i++) {
		if (!iov[i].flags) {
			virtio_queue_set_used_elem(vq, head, 0);
			return -1;
		}
	}
	memset(&fault, 0, sizeof(fault));
	if (!io_virtio_iommu_pop_fault(idev->refmodel, &backend_fault)) {
		endpoint = backend_fault.endpoint;
		addr = backend_fault.address;
		access = backend_fault.access;
	}
	fault.reason = VIRTIO_IOMMU_FAULT_R_MAPPING;
	fault.flags = htole32(VIRTIO_IOMMU_FAULT_F_ADDRESS | access);
	fault.endpoint = htole32(endpoint);
	fault.address = htole64(addr);
	if (virtio_buf_to_iovec_write(idev->vdev, iov, iov_cnt,
					     &fault, sizeof(fault)) !=
	    sizeof(fault)) {
		virtio_queue_set_used_elem(vq, head, 0);
		return -1;
	}
	virtio_queue_set_used_elem(vq, head, sizeof(fault));
	if (virtio_queue_should_signal(vq) && mdev->dev.vn &&
	    mdev->dev.vn->notify)
		mdev->dev.vn->notify(&mdev->dev, VIRTIO_IOMMU_EVENT_VQ);
	return 0;
}

int virtio_iommu_relay_attach(virtio_handle_t handle, uint64_t namespace_id,
			      uint32_t domain, uint32_t endpoint)
{
	struct virtio_iommu_dev *idev = virtio_iommu_from_handle(handle);

	if (!idev || !idev->refmodel)
		return -EINVAL;
	return io_virtio_iommu_relay_attach(idev->refmodel, namespace_id,
					    domain, endpoint);
}

int virtio_iommu_relay_detach(virtio_handle_t handle, uint64_t namespace_id,
			      uint32_t domain, uint32_t endpoint)
{
	struct virtio_iommu_dev *idev = virtio_iommu_from_handle(handle);

	if (!idev || !idev->refmodel)
		return -EINVAL;
	return io_virtio_iommu_relay_detach(idev->refmodel, namespace_id,
					    domain, endpoint);
}

int virtio_iommu_relay_map(virtio_handle_t handle, uint64_t namespace_id,
			   uint32_t domain, uint32_t endpoint,
			   uint64_t virt_start, uint64_t virt_end,
			   uint64_t phys_start, uint32_t flags)
{
	struct virtio_iommu_dev *idev = virtio_iommu_from_handle(handle);

	if (!idev || !idev->refmodel)
		return -EINVAL;
	return io_virtio_iommu_relay_map(idev->refmodel, namespace_id, domain,
					 endpoint, virt_start, virt_end,
					 phys_start, flags);
}

int virtio_iommu_relay_unmap(virtio_handle_t handle, uint64_t namespace_id,
			     uint32_t domain, uint32_t endpoint,
			     uint64_t virt_start, uint64_t virt_end)
{
	struct virtio_iommu_dev *idev = virtio_iommu_from_handle(handle);

	if (!idev || !idev->refmodel)
		return -EINVAL;
	return io_virtio_iommu_relay_unmap(idev->refmodel, namespace_id, domain,
					   endpoint, virt_start, virt_end);
}
