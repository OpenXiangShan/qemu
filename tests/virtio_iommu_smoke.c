#include <assert.h>
#include <endian.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"
#include "virtio_config.h"
#include "virtio_iommu.h"
#include "virtio_ids.h"
#include "virtio_mmio.h"
#include "virtio_wrapper.h"

#define MMIO_BASE 0x10000000ULL
#define MMIO_SIZE 0x200U
#define TEST_MEM_SIZE 0x40000U
#define REQUEST_DESC 0x1000U
#define REQUEST_AVAIL 0x2000U
#define REQUEST_USED 0x3000U
#define REQUEST_DATA 0x4000U
#define REQUEST_TAIL 0x4800U
#define EVENT_DESC 0x5000U
#define EVENT_AVAIL 0x6000U
#define EVENT_USED 0x7000U
#define EVENT_DATA 0x8000U
#define QUEUE_SIZE 8U

struct test_memory {
	uint8_t data[TEST_MEM_SIZE];
	unsigned int irq_count;
	unsigned int msi_count;
	uint64_t msi_addr;
	uint32_t msi_data;
	bool forbid_dma;
	bool fail_event_write;
	unsigned int dma_accesses;
	uint32_t work;
	unsigned int notifications;
	virtio_handle_t enqueue_on_dma;
};

static uint64_t test_alloc(int size)
{
	return (uint64_t)(uintptr_t)calloc(1, (size_t)size);
}

static void test_free(uint64_t address, int size)
{
	(void)size;
	free((void *)(uintptr_t)address);
}

static int test_read(uint64_t gpa, void *dst, uint32_t len, void *opaque)
{
	struct test_memory *memory = opaque;

	assert(!memory->forbid_dma);
	memory->dma_accesses++;
	if (memory->enqueue_on_dma) {
		virtio_handle_t iommu = memory->enqueue_on_dma;

		memory->enqueue_on_dma = NULL;
		assert(virtio_iommu_report_fault(iommu, 0x11, 0xdeaddead,
						VIRTIO_IOMMU_ACCESS_WRITE) == 0);
		assert(virtio_iommu_set_deferred(iommu, NULL, NULL) == -EBUSY);
		assert(virtio_iommu_drain_faults(iommu, 1) == -EBUSY);
	}
	if (gpa > TEST_MEM_SIZE || len > TEST_MEM_SIZE - gpa)
		return -1;
	memcpy(dst, memory->data + gpa, len);
	return (int)len;
}

static int test_write(uint64_t gpa, void *src, uint32_t len, void *opaque)
{
	struct test_memory *memory = opaque;

	assert(!memory->forbid_dma);
	memory->dma_accesses++;
	if (gpa == EVENT_DATA && memory->fail_event_write)
		return -1;
	if (gpa > TEST_MEM_SIZE || len > TEST_MEM_SIZE - gpa)
		return -1;
	memcpy(memory->data + gpa, src, len);
	return (int)len;
}

static int test_map(uint64_t gpa, uint64_t size, uint64_t *hpa,
		    uint64_t *hsize, void *opaque)
{
	struct test_memory *memory = opaque;

	if (gpa > TEST_MEM_SIZE || size > TEST_MEM_SIZE - gpa)
		return -1;
	*hpa = (uint64_t)(uintptr_t)(memory->data + gpa);
	*hsize = size;
	return 0;
}

static int test_set_irq(void *opaque)
{
	struct test_memory *memory = opaque;

	memory->irq_count++;
	return 0;
}

static int test_send_msi(uint64_t addr, uint32_t data, void *opaque)
{
	struct test_memory *memory = opaque;

	memory->msi_count++;
	memory->msi_addr = addr;
	memory->msi_data = data;
	return 0;
}

static void mmio_write(virtio_handle_t handle, uint32_t offset,
		       uint32_t value)
{
	int doorbell = 0;

	assert(virtio_mmio_write(handle, MMIO_BASE + offset, value, 4,
				&doorbell) == 0);
}

static uint32_t mmio_read(virtio_handle_t handle, uint32_t offset)
{
	uint32_t value = 0;

	assert(virtio_mmio_read(handle, MMIO_BASE + offset, &value, 4) == 0);
	return value;
}

static void setup_queue(virtio_handle_t handle, uint32_t queue,
			uint64_t desc, uint64_t avail, uint64_t used)
{
	mmio_write(handle, VMM_VIRTIO_MMIO_QUEUE_SEL, queue);
	assert(mmio_read(handle, VMM_VIRTIO_MMIO_QUEUE_NUM_MAX) >= QUEUE_SIZE);
	mmio_write(handle, VMM_VIRTIO_MMIO_QUEUE_NUM, QUEUE_SIZE);
	mmio_write(handle, VMM_VIRTIO_MMIO_QUEUE_DESC_LOW, (uint32_t)desc);
	mmio_write(handle, VMM_VIRTIO_MMIO_QUEUE_DESC_HIGH, desc >> 32);
	mmio_write(handle, VMM_VIRTIO_MMIO_QUEUE_AVAIL_LOW, (uint32_t)avail);
	mmio_write(handle, VMM_VIRTIO_MMIO_QUEUE_AVAIL_HIGH, avail >> 32);
	mmio_write(handle, VMM_VIRTIO_MMIO_QUEUE_USED_LOW, (uint32_t)used);
	mmio_write(handle, VMM_VIRTIO_MMIO_QUEUE_USED_HIGH, used >> 32);
	mmio_write(handle, VMM_VIRTIO_MMIO_QUEUE_READY, 1);
}

static struct virtio_iommu_req_tail *queue_request(
				      virtio_handle_t handle,
				      struct test_memory *memory, const void *request,
				      uint32_t request_len, uint16_t avail_index)
{
	struct vring_desc *desc = (void *)(memory->data + REQUEST_DESC);
	struct vring_avail *avail = (void *)(memory->data + REQUEST_AVAIL);
	struct virtio_iommu_req_tail *tail =
		(void *)(memory->data + REQUEST_TAIL);
	int doorbell = 0;

	memcpy(memory->data + REQUEST_DATA, request, request_len);
	memset(tail, 0xff, sizeof(*tail));
	desc[0].addr = REQUEST_DATA;
	desc[0].len = request_len;
	desc[0].flags = VMM_VRING_DESC_F_NEXT;
	desc[0].next = 1;
	desc[1].addr = REQUEST_TAIL;
	desc[1].len = sizeof(*tail);
	desc[1].flags = VMM_VRING_DESC_F_WRITE;
	desc[1].next = 0;
	avail->ring[(avail_index - 1) % QUEUE_SIZE] = 0;
	avail->idx = avail_index;
	assert(virtio_mmio_write(handle,
				MMIO_BASE + VMM_VIRTIO_MMIO_QUEUE_NOTIFY,
				VIRTIO_IOMMU_REQUEST_VQ, 4, &doorbell) == 0);
	assert(doorbell == 1);
	return tail;
}

static uint8_t submit_request(virtio_handle_t handle,
				      struct test_memory *memory, const void *request,
				      uint32_t request_len, uint16_t avail_index)
{
	struct virtio_iommu_req_tail *tail;
	struct vring_used *used = (void *)(memory->data + REQUEST_USED);

	tail = queue_request(handle, memory, request, request_len, avail_index);
	virtio_process_req(handle);
	assert(used->idx == avail_index);
	assert(used->ring[(avail_index - 1) % QUEUE_SIZE].id == 0);
	assert(used->ring[(avail_index - 1) % QUEUE_SIZE].len == sizeof(*tail));
	return tail->status;
}

static void add_event_buffer(struct test_memory *memory, uint16_t index)
{
	struct vring_desc *desc = (void *)(memory->data + EVENT_DESC);
	struct vring_avail *avail = (void *)(memory->data + EVENT_AVAIL);

	desc[0].addr = EVENT_DATA;
	desc[0].len = sizeof(struct virtio_iommu_fault);
	desc[0].flags = VMM_VRING_DESC_F_WRITE;
	desc[0].next = 0;
	avail->ring[(index - 1) % QUEUE_SIZE] = 0;
	avail->idx = index;
}

static void test_work_notify(void *opaque, uint32_t work)
{
	struct test_memory *memory = opaque;

	memory->work |= work;
	memory->notifications++;
}

static void test_deferred(enum virtio_iommu_stage_mode mode)
{
	struct test_memory memory = { 0 };
	struct libvirtio_ops ops = {
		.mm_alloc = test_alloc, .mm_free = test_free, .map = test_map,
		.guest_mem_read = test_read, .guest_mem_write = test_write,
		.set_irq = test_set_irq,
	};
	struct virtio_mmio_options options = { .iommu_mode = mode };
	struct virtio_iommu_req_attach attach = {
		.head.type = VIRTIO_IOMMU_T_ATTACH,
		.domain = htole32(7), .endpoint = htole32(0x10),
	};
	struct virtio_iommu_req_map map = {
		.head.type = VIRTIO_IOMMU_T_MAP, .domain = htole32(7),
		.virt_start = htole64(0x20000), .virt_end = htole64(0x20fff),
		.phys_start = htole64(0x10000), .flags = htole32(VIRTIO_IOMMU_MAP_F_READ),
	};
	struct virtio_iommu_req_unmap unmap = {
		.head.type = VIRTIO_IOMMU_T_UNMAP, .domain = htole32(7),
		.virt_start = htole64(0x20000), .virt_end = htole64(0x20fff),
	};
	struct virtio_iommu_fault_stats stats;
	struct virtio_iommu_fault *fault = (void *)(memory.data + EVENT_DATA);
	struct vring_used *used = (void *)(memory.data + EVENT_USED);
	uint64_t hpa, mask;
	uint64_t addr = mode == VIRTIO_IOMMU_STAGE_S1_S2 ? 0x40055 : 0x20055;
	unsigned int notifications;
	uint16_t seq = 0;
	virtio_handle_t iommu = virtio_mmio_create_ex(VIRTIO_EMU_NAME_IOMMU,
		MMIO_BASE, MMIO_SIZE, &ops, &memory, &options);

	assert(iommu);
	assert(!virtio_iommu_set_deferred(iommu, test_work_notify, &memory));
	setup_queue(iommu, 0, REQUEST_DESC, REQUEST_AVAIL, REQUEST_USED);
	setup_queue(iommu, 1, EVENT_DESC, EVENT_AVAIL, EVENT_USED);
	assert(submit_request(iommu, &memory, &attach,
		sizeof(attach) - sizeof(attach.tail), 1) == VIRTIO_IOMMU_S_OK);
	attach.endpoint = htole32(0x11);
	assert(submit_request(iommu, &memory, &attach,
		sizeof(attach) - sizeof(attach.tail), 2) == VIRTIO_IOMMU_S_OK);
	assert(queue_request(iommu, &memory, &map,
		sizeof(map) - sizeof(map.tail), 3)->status == 0xff);
	assert(memory.work & VIRTIO_IOMMU_WORK_REQUEST);
	assert(virtio_iommu_set_deferred(iommu, NULL, NULL) == -EBUSY);
	/* A pending MAP must not be fetched from the endpoint coroutine. */
	memory.forbid_dma = true;
	assert(virtio_iommu_translate(iommu, 0x10, 0x20055, 1,
		VIRTIO_IOMMU_ACCESS_READ, &hpa, &mask) != 0);
	memory.forbid_dma = false;
	virtio_process_req(iommu);
	assert(((struct vring_used *)(memory.data + REQUEST_USED))->idx == 3);
	if (mode == VIRTIO_IOMMU_STAGE_S1_S2) {
		assert(!virtio_iommu_relay_attach(iommu, 1, 9, 0x10));
		assert(!virtio_iommu_relay_map(iommu, 1, 9, 0x10,
			0x40000, 0x40fff, 0x20000, VIRTIO_IOMMU_MAP_F_READ));
	}
	memory.forbid_dma = true;
	assert(!virtio_iommu_translate(iommu, 0x10, addr, 1,
		VIRTIO_IOMMU_ACCESS_READ, &hpa, &mask));
	assert(hpa == 0x10055);
	assert(virtio_iommu_translate(iommu, 0x10, addr, 1,
		VIRTIO_IOMMU_ACCESS_WRITE, &hpa, &mask) != 0);
	assert(!virtio_iommu_report_fault(iommu, 0x10, addr, VIRTIO_IOMMU_ACCESS_WRITE));
	assert(memory.work & VIRTIO_IOMMU_WORK_EVENT);
	assert(used->idx == 0);
	memory.forbid_dma = false;
	notifications = memory.notifications;
	assert(virtio_iommu_drain_faults(iommu, 8) == 0);
	assert(notifications == memory.notifications); /* No-buffer: no busy loop. */
	assert(!virtio_iommu_get_fault_stats(iommu, &stats) && stats.pending == 1);
	add_event_buffer(&memory, ++seq);
	memory.work = 0;
	mmio_write(iommu, VMM_VIRTIO_MMIO_QUEUE_NOTIFY, 1);
	assert(memory.work == VIRTIO_IOMMU_WORK_EVENT);
	/* Simulate another endpoint arriving while the IOMMU is in guest DMA. */
	memory.enqueue_on_dma = iommu;
	assert(virtio_iommu_drain_faults(iommu, 1) == 1);
	assert(used->idx == seq && le64toh(fault->address) == addr);
	assert(le32toh(fault->endpoint) == 0x10);
	assert(!virtio_iommu_get_fault_stats(iommu, &stats) && stats.pending == 1);
	add_event_buffer(&memory, ++seq);
	assert(virtio_iommu_drain_faults(iommu, 1) == 1);
	assert(le32toh(fault->endpoint) == 0x11 && le64toh(fault->address) == 0xdeaddead);

	/* Bad buffers are returned with zero length; the fault is not lost. */
	assert(!virtio_iommu_report_fault(iommu, 0x10, 0xbad, 1));
	add_event_buffer(&memory, ++seq);
	((struct vring_desc *)(memory.data + EVENT_DESC))->flags = 0;
	assert(virtio_iommu_drain_faults(iommu, 1) == 0 && used->idx == seq);
	assert(used->ring[(seq - 1) % QUEUE_SIZE].len == 0);
	add_event_buffer(&memory, ++seq);
	((struct vring_desc *)(memory.data + EVENT_DESC))->len = 1;
	assert(virtio_iommu_drain_faults(iommu, 1) == 0 && used->idx == seq);
	add_event_buffer(&memory, ++seq);
	memory.fail_event_write = true;
	assert(virtio_iommu_drain_faults(iommu, 1) == 0 && used->idx == seq);
	memory.fail_event_write = false;
	add_event_buffer(&memory, ++seq);
	assert(virtio_iommu_drain_faults(iommu, 1) == 1);
	assert(le64toh(fault->address) == 0xbad);

	/* Bounded FIFO, drop-newest overflow, then wrap the producer/consumer. */
	memory.forbid_dma = true;
	for (unsigned int i = 0; i < VIRTIO_IOMMU_FAULT_BACKLOG; i++)
		assert(!virtio_iommu_report_fault(iommu, 0x10 + (i & 1), i, 1));
	assert(virtio_iommu_report_fault(iommu, 0x10, 999, 1) == -ENOSPC);
	assert(!virtio_iommu_get_fault_stats(iommu, &stats));
	assert(stats.pending == VIRTIO_IOMMU_FAULT_BACKLOG && stats.dropped == 1);
	assert(virtio_iommu_set_deferred(iommu, NULL, NULL) == -EBUSY);
	memory.forbid_dma = false;
	for (unsigned int i = 0; i < VIRTIO_IOMMU_FAULT_BACKLOG; i++) {
		add_event_buffer(&memory, ++seq);
		assert(virtio_iommu_drain_faults(iommu, 1) == 1);
		assert(le64toh(fault->address) == i);
		assert(le32toh(fault->endpoint) == 0x10 + (i & 1));
	}
	assert(submit_request(iommu, &memory, &unmap,
		sizeof(unmap) - sizeof(unmap.tail), 4) == VIRTIO_IOMMU_S_OK);
	memory.forbid_dma = true;
	assert(virtio_iommu_translate(iommu, 0x10, addr, 1, 1, &hpa, &mask) != 0);
	/* In two-stage mode the above can produce a native S2 fault. A later
	 * precheck fault must never inherit its address or endpoint from FQ.
	 */
	assert(!virtio_iommu_report_fault(iommu, 0x11, 0xcafe, 2));
	memory.forbid_dma = false;
	add_event_buffer(&memory, ++seq);
	assert(virtio_iommu_drain_faults(iommu, 1) == 1);
	assert(le64toh(fault->address) == 0xcafe && le32toh(fault->endpoint) == 0x11);
	assert(!virtio_iommu_report_fault(iommu, 0x10, 0xdead, 1));
	mmio_write(iommu, VMM_VIRTIO_MMIO_STATUS, 0);
	assert(!virtio_iommu_get_fault_stats(iommu, &stats));
	assert(!stats.pending && !stats.dropped);
	assert(!virtio_iommu_report_fault(iommu, 0x10, 0xbeef, 1));
	assert(!virtio_iommu_get_fault_stats(iommu, &stats) && stats.pending == 1);
	mmio_write(iommu, VMM_VIRTIO_MMIO_STATUS, 0);
	assert(!virtio_iommu_set_deferred(iommu, NULL, NULL));
	assert(virtio_iommu_drain_faults(iommu, 1) == -EINVAL);
	virtio_mmio_destroy(iommu);
	printf("VIRTIO_IOMMU_DEFERRED_PASS mode=%u dma=%u notifications=%u\n",
	       mode, memory.dma_accesses, memory.notifications);
}

static int test_legacy(void)
{
	struct test_memory memory = { 0 };
	struct libvirtio_ops ops = {
		.mm_alloc = test_alloc,
		.mm_free = test_free,
		.map = test_map,
		.guest_mem_read = test_read,
		.guest_mem_write = test_write,
		.set_irq = test_set_irq,
		.genirq_ops.send_msi = test_send_msi,
	};
	struct virtio_mmio_options options = {
		.iommu_mode = VIRTIO_IOMMU_STAGE_S1,
	};
	struct virtio_mmio_options endpoint_options = { 0 };
	struct virtio_iommu_req_attach attach = { 0 };
	struct virtio_iommu_req_map map = { 0 };
	struct virtio_iommu_req_unmap unmap = { 0 };
	struct virtio_iommu_req_detach detach = { 0 };
	struct virtio_iommu_req_probe probe = { 0 };
	struct virtio_iommu_req_head unknown = { .type = 0xff };
	struct virtio_iommu_fault *fault = (void *)(memory.data + EVENT_DATA);
	struct vring_used *event_used = (void *)(memory.data + EVENT_USED);
	struct virtio_mmio_dev endpoint = { 0 };
	uint8_t expected[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	uint8_t write_data[8] = { 8, 7, 6, 5, 4, 3, 2, 1 };
	uint8_t received[8] = { 0 };
	uint64_t hpa;
	uint64_t hsize;
	virtio_handle_t iommu;
	virtio_handle_t endpoint_handle;

	iommu = virtio_mmio_create_ex(VIRTIO_EMU_NAME_IOMMU, MMIO_BASE,
				      MMIO_SIZE, &ops, &memory, &options);
	assert(iommu);
	assert(mmio_read(iommu, VMM_VIRTIO_MMIO_VERSION) == 2);
	assert(mmio_read(iommu, VMM_VIRTIO_MMIO_DEVICE_ID) ==
	       VMM_VIRTIO_ID_IOMMU);
	assert(mmio_read(iommu, VMM_VIRTIO_MMIO_CONFIG +
		       offsetof(struct virtio_iommu_config, input_range.end)) ==
	       UINT32_MAX);
	assert(mmio_read(iommu, VMM_VIRTIO_MMIO_CONFIG +
		       offsetof(struct virtio_iommu_config, input_range.end) + 4) ==
	       0x3f);
	mmio_write(iommu, VMM_VIRTIO_MMIO_HOST_FEATURES_SEL, 1);
	assert(mmio_read(iommu, VMM_VIRTIO_MMIO_HOST_FEATURES) & 1);
	setup_queue(iommu, VIRTIO_IOMMU_REQUEST_VQ, REQUEST_DESC,
		    REQUEST_AVAIL, REQUEST_USED);
	setup_queue(iommu, VIRTIO_IOMMU_EVENT_VQ, EVENT_DESC,
		    EVENT_AVAIL, EVENT_USED);
	endpoint_options.iommu = iommu;
	endpoint_options.endpoint_id = 0x10;
	endpoint_handle = virtio_mmio_create_ex(VIRTIO_EMU_NAME_BLK,
						 MMIO_BASE + 0x1000, MMIO_SIZE,
						 &ops, &memory, &endpoint_options);
	assert(endpoint_handle);
	assert(virtio_device_get_host_features(
		       &((struct virtio_mmio_dev *)endpoint_handle)->dev) &
	       (1ULL << VMM_VIRTIO_F_IOMMU_PLATFORM));

	attach.head.type = VIRTIO_IOMMU_T_ATTACH;
	attach.domain = htole32(7);
	attach.endpoint = htole32(0x10);
	assert(submit_request(iommu, &memory, &attach,
			      sizeof(attach) - sizeof(attach.tail), 1) ==
	       VIRTIO_IOMMU_S_OK);
	assert(virtio_iommu_relay_attach(iommu, 1, 1, 0x10) == -EINVAL);

	map.head.type = VIRTIO_IOMMU_T_MAP;
	map.domain = htole32(7);
	map.virt_start = htole64(0x20000);
	map.virt_end = htole64(0x21fff);
	map.phys_start = htole64(0x10000);
	map.flags = htole32(VIRTIO_IOMMU_MAP_F_READ |
				    VIRTIO_IOMMU_MAP_F_WRITE);
	endpoint.ops = &ops;
	endpoint.priv = &memory;
	endpoint.iommu = iommu;
	endpoint.endpoint_id = 0x10;
	assert(queue_request(iommu, &memory, &map,
			     sizeof(map) - sizeof(map.tail), 2)->status == 0xff);
	memcpy(memory.data + 0x10ffc, expected, 4);
	memcpy(memory.data + 0x11000, expected + 4, 4);
	assert(libvirtio_gphys_read(&endpoint.dev, 0x20ffc, received,
				    sizeof(received)) == sizeof(received));
	assert(((struct vring_used *)(memory.data + REQUEST_USED))->idx == 2);
	assert(!memcmp(received, expected, sizeof(expected)));
	assert(libvirtio_gphys_write(&endpoint.dev, 0x20ffc, write_data,
				     sizeof(write_data)) == sizeof(write_data));
	assert(!memcmp(memory.data + 0x10ffc, write_data, 4));
	assert(!memcmp(memory.data + 0x11000, write_data + 4, 4));
	assert(libvirtio_genirq_send_msi(&endpoint.dev, 0x21000, 37) == 0);
	assert(memory.msi_count == 1 && memory.msi_addr == 0x11000);
	assert(memory.msi_data == 37);
	assert(libvirtio_genirq_send_msi(&endpoint.dev, 0x21001, 37) == -1);
	assert(memory.msi_count == 1);

	add_event_buffer(&memory, 1);
	assert(virtio_iommu_report_fault(iommu, 0x10, 0x2345,
				 VIRTIO_IOMMU_ACCESS_WRITE) == 0);
	assert(event_used->idx == 1 && event_used->ring[0].len == sizeof(*fault));
	assert(fault->reason == VIRTIO_IOMMU_FAULT_R_MAPPING);
	assert(le32toh(fault->endpoint) == 0x10);
	assert(le64toh(fault->address) == 0x2345);
	assert(le32toh(fault->flags) ==
	       (VIRTIO_IOMMU_FAULT_F_ADDRESS | VIRTIO_IOMMU_ACCESS_WRITE));

	assert(libvirtio_gphys_map(&endpoint.dev, 0, 1, &hpa, &hsize) == -1);

	unmap.head.type = VIRTIO_IOMMU_T_UNMAP;
	unmap.domain = htole32(7);
	unmap.virt_start = htole64(0x20000);
	unmap.virt_end = htole64(0x21fff);
	assert(submit_request(iommu, &memory, &unmap,
			      sizeof(unmap) - sizeof(unmap.tail), 3) ==
	       VIRTIO_IOMMU_S_OK);
	add_event_buffer(&memory, 2);
	assert(libvirtio_gphys_read(&endpoint.dev, 0x20000, received, 1) == 0);
	assert(event_used->idx == 2);
	assert(le32toh(fault->endpoint) == 0x10);
	assert(le64toh(fault->address) == 0x20000);
	assert(le32toh(fault->flags) ==
	       (VIRTIO_IOMMU_FAULT_F_ADDRESS | VIRTIO_IOMMU_ACCESS_READ));
	add_event_buffer(&memory, 3);
	assert(libvirtio_genirq_send_msi(&endpoint.dev, 0x21000, 37) == -1);
	assert(memory.msi_count == 1 && event_used->idx == 3);
	assert(le64toh(fault->address) == 0x21000);
	assert(le32toh(fault->flags) ==
	       (VIRTIO_IOMMU_FAULT_F_ADDRESS | VIRTIO_IOMMU_ACCESS_WRITE));

	probe.head.type = VIRTIO_IOMMU_T_PROBE;
	probe.endpoint = htole32(0x10);
	assert(submit_request(iommu, &memory, &probe, sizeof(probe), 4) ==
	       VIRTIO_IOMMU_S_OK);
	assert(submit_request(iommu, &memory, &unknown, sizeof(unknown), 5) ==
	       VIRTIO_IOMMU_S_UNSUPP);
	{
		uint8_t oversized[sizeof(attach) + 1] = { 0 };

		memcpy(oversized, &attach, sizeof(attach) - sizeof(attach.tail));
		assert(submit_request(iommu, &memory, oversized,
			      sizeof(attach) - sizeof(attach.tail) + 1, 6) ==
		       VIRTIO_IOMMU_S_INVAL);
	}

	detach.head.type = VIRTIO_IOMMU_T_DETACH;
	detach.domain = htole32(7);
	detach.endpoint = htole32(0x10);
	assert(submit_request(iommu, &memory, &detach,
			      sizeof(detach) - sizeof(detach.tail), 7) ==
	       VIRTIO_IOMMU_S_OK);

	endpoint.iommu = NULL;
	assert(libvirtio_genirq_send_msi(&endpoint.dev, 0x3b000000, 41) == 0);
	assert(memory.msi_count == 2 && memory.msi_addr == 0x3b000000);
	assert(memory.msi_data == 41);
	memory.data[0x123] = 0xa5;
	assert(libvirtio_gphys_read(&endpoint.dev, 0x123, received, 1) == 1);
	assert(received[0] == 0xa5);
	virtio_mmio_destroy(endpoint_handle);
	virtio_mmio_destroy(iommu);

	memset(&memory, 0, sizeof(memory));
	memset(&endpoint, 0, sizeof(endpoint));
	memset(&attach, 0, sizeof(attach));
	memset(&map, 0, sizeof(map));
	memset(&unmap, 0, sizeof(unmap));
	memset(&detach, 0, sizeof(detach));
	options.iommu_mode = VIRTIO_IOMMU_STAGE_S1_S2;
	iommu = virtio_mmio_create_ex(VIRTIO_EMU_NAME_IOMMU, MMIO_BASE,
				      MMIO_SIZE, &ops, &memory, &options);
	assert(iommu);
	setup_queue(iommu, VIRTIO_IOMMU_REQUEST_VQ, REQUEST_DESC,
		    REQUEST_AVAIL, REQUEST_USED);
	setup_queue(iommu, VIRTIO_IOMMU_EVENT_VQ, EVENT_DESC,
		    EVENT_AVAIL, EVENT_USED);
	endpoint_options.iommu = iommu;
	endpoint_handle = virtio_mmio_create_ex(VIRTIO_EMU_NAME_BLK,
						 MMIO_BASE + 0x1000, MMIO_SIZE,
						 &ops, &memory, &endpoint_options);
	assert(endpoint_handle);
	endpoint.ops = &ops;
	endpoint.priv = &memory;
	endpoint.iommu = iommu;
	endpoint.endpoint_id = 0x10;

	attach.head.type = VIRTIO_IOMMU_T_ATTACH;
	attach.domain = htole32(9);
	attach.endpoint = htole32(0x10);
	assert(submit_request(iommu, &memory, &attach,
			      sizeof(attach) - sizeof(attach.tail), 1) ==
	       VIRTIO_IOMMU_S_OK);
	map.head.type = VIRTIO_IOMMU_T_MAP;
	map.domain = htole32(9);
	map.virt_start = htole64(0x30000);
	map.virt_end = htole64(0x30fff);
	map.phys_start = htole64(0x10000);
	map.flags = htole32(VIRTIO_IOMMU_MAP_F_READ |
			    VIRTIO_IOMMU_MAP_F_WRITE);
	assert(submit_request(iommu, &memory, &map,
			      sizeof(map) - sizeof(map.tail), 2) ==
	       VIRTIO_IOMMU_S_OK);
	memory.data[0x10040] = 0x3c;
	assert(libvirtio_gphys_read(&endpoint.dev, 0x30040, received, 1) == 1);
	assert(received[0] == 0x3c);
	assert(virtio_iommu_relay_attach(iommu, 0x1234, 5, 0x10) == 0);
	assert(virtio_iommu_relay_attach(iommu, 0x5678, 5, 0x10) ==
	       -EBUSY);
	assert(virtio_iommu_relay_attach(iommu, 0x1234, 6, 0x10) ==
	       -EBUSY);
	assert(virtio_iommu_relay_map(iommu, 0x5678, 5, 0x10,
				      0x20000, 0x20fff, 0x30000,
				      VIRTIO_IOMMU_MAP_F_READ) == -ENOENT);
	assert(virtio_iommu_relay_map(iommu, 0x1234, 5, 0x10,
				      0x20000, 0x20fff, 0x30000,
				      VIRTIO_IOMMU_MAP_F_READ |
				      VIRTIO_IOMMU_MAP_F_WRITE) == 0);
	memory.data[0x10040] = 0x5a;
	assert(libvirtio_gphys_read(&endpoint.dev, 0x20040, received, 1) == 1);
	assert(received[0] == 0x5a);
	assert(libvirtio_genirq_send_msi(&endpoint.dev, 0x20000, 43) == 0);
	assert(memory.msi_count == 1 && memory.msi_addr == 0x10000);
	assert(memory.msi_data == 43);
	assert(virtio_iommu_relay_unmap(iommu, 0x1234, 5, 0x10,
					0x20000, 0x20fff) == 0);
	assert(virtio_iommu_relay_unmap(iommu, 0x5678, 5, 0x10,
					0x20000, 0x20fff) == -ENOENT);
	assert(virtio_iommu_relay_detach(iommu, 0x5678, 5, 0x10) ==
	       -ENOENT);
	assert(virtio_iommu_relay_detach(iommu, 0x1234, 5, 0x10) == 0);
	memory.data[0x10040] = 0xc3;
	assert(libvirtio_gphys_read(&endpoint.dev, 0x30040, received, 1) == 1);
	assert(received[0] == 0xc3);
	assert(virtio_iommu_relay_attach(iommu, 0x5678, 5, 0x10) == 0);
	assert(virtio_iommu_relay_detach(iommu, 0x5678, 5, 0x10) == 0);
	assert(virtio_iommu_relay_attach(iommu, 0x1234, 5, 0x10) == 0);
	mmio_write(iommu, VMM_VIRTIO_MMIO_STATUS, 0);
	assert(virtio_iommu_relay_detach(iommu, 0x1234, 5, 0x10) == -ENOENT);
	virtio_mmio_destroy(endpoint_handle);
	virtio_mmio_destroy(iommu);
	assert(virtio_iommu_relay_attach(NULL, 0, 0, 0) == -EINVAL);
	options.iommu_mode = (enum virtio_iommu_stage_mode)99;
	assert(!virtio_mmio_create_ex(VIRTIO_EMU_NAME_IOMMU, MMIO_BASE,
				       MMIO_SIZE, &ops, &memory, &options));
	return 0;
}

int main(int argc, char **argv)
{
	/* The legacy suite also creates blk devices. Keep deferred IOMMU
	 * lifetime checks independently runnable under LeakSanitizer.
	 */
	if (argc == 1)
		assert(test_legacy() == 0);
	else
		assert(argc == 2 && !strcmp(argv[1], "--deferred-only"));
	for (unsigned int mode = VIRTIO_IOMMU_STAGE_S1;
	     mode <= VIRTIO_IOMMU_STAGE_S1_S2; mode++)
		test_deferred(mode);
	return 0;
}
