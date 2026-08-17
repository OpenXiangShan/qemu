#include "io_system.h"
#include "io_system_internal_api.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define TEST_MEM_BASE 0x80000000ULL
#define TEST_IOMMU_BASE 0x311f0000ULL
#define TEST_IOMMU_CAPABILITIES_OFFSET 0x0
#define TEST_IOMMU_DDTP_OFFSET 0x10
#define TEST_IOMMU_DDTP_BARE 0x1ULL

typedef struct TestMemory {
    uint8_t data[128];
} TestMemory;

static int test_guest_memory_read(void *opaque, uint64_t gpa, void *dst,
                                  uint32_t len)
{
    TestMemory *mem = opaque;

    if (gpa < TEST_MEM_BASE ||
        gpa + len > TEST_MEM_BASE + sizeof(mem->data)) {
        return -1;
    }

    memcpy(dst, mem->data + (size_t)(gpa - TEST_MEM_BASE), len);
    return (int)len;
}

static int test_guest_memory_write(void *opaque, uint64_t gpa,
                                   const void *src, uint32_t len)
{
    TestMemory *mem = opaque;

    if (gpa < TEST_MEM_BASE ||
        gpa + len > TEST_MEM_BASE + sizeof(mem->data)) {
        return -1;
    }

    memcpy(mem->data + (size_t)(gpa - TEST_MEM_BASE), src, len);
    return (int)len;
}

int main(void)
{
    TestMemory mem = { 0 };
    IoSystemConfig config = {
        .backend_kind = IO_SYSTEM_BACKEND_CMODEL,
        .iommu_kind = IO_SYSTEM_IOMMU_CMODEL,
        .iommu_placement = IO_SYSTEM_IOMMU_PLACEMENT_AUTO,
        .trace_capacity = 8,
    };
    IoSystemHostOps host_ops = {
        .opaque = &mem,
        .guest_memory_read = test_guest_memory_read,
        .guest_memory_write = test_guest_memory_write,
    };
    IoSystemDmaAttrs attrs = {
        .requester_id = 0x1234,
    };
    const uint8_t src[8] = {
        0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
    };
    uint8_t dst[sizeof(src)] = { 0 };
    uint32_t caps = 0;
    uint64_t ddtp_bare = TEST_IOMMU_DDTP_BARE;
    IoSystem *system;

    system = io_system_create(&config, &host_ops);
    assert(system);

    assert(io_system_q2io_read(system,
                               TEST_IOMMU_BASE +
                               TEST_IOMMU_CAPABILITIES_OFFSET,
                               &caps, sizeof(caps)) == IO_SYSTEM_OK);
    assert(caps != 0);

    assert(io_system_q2io_write(system,
                                TEST_IOMMU_BASE + TEST_IOMMU_DDTP_OFFSET,
                                &ddtp_bare,
                                sizeof(ddtp_bare)) == IO_SYSTEM_OK);
    assert(io_system_dma_write(system, &attrs, TEST_MEM_BASE + 4,
                               src, sizeof(src)) == IO_SYSTEM_OK);
    assert(!memcmp(mem.data + 4, src, sizeof(src)));
    assert(io_system_dma_read(system, &attrs, TEST_MEM_BASE + 4,
                              dst, sizeof(dst)) == IO_SYSTEM_OK);
    assert(!memcmp(dst, src, sizeof(src)));

    io_system_destroy(system);
    return 0;
}
