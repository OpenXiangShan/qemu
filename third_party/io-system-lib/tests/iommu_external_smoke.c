#include "io_system.h"
#include "io_system_internal_api.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef IO_IOMMU_STUB_API_SO
#define IO_IOMMU_STUB_API_SO ""
#endif

#define TEST_MEM_BASE 0x80000000ULL
#define TEST_IOMMU_BASE 0x311f0000ULL

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
        .iommu_kind = IO_SYSTEM_IOMMU_RTL,
        .iommu_placement = IO_SYSTEM_IOMMU_PLACEMENT_EXTERNAL,
        .trace_capacity = 8,
    };
    IoSystemHostOps host_ops = {
        .opaque = &mem,
        .guest_memory_read = test_guest_memory_read,
        .guest_memory_write = test_guest_memory_write,
    };
    IoSystemDmaAttrs attrs = {
        .requester_id = 0x1234,
        .pasid = 0x56,
        .has_pasid = true,
        .translated = true,
    };
    IoSystemDmaAttrs bad_attrs = {
        .requester_id = 0x1,
    };
    const uint32_t mmio_value = 0x5aa55aa5;
    uint32_t mmio_readback = 0;
    const uint8_t src[8] = {
        0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88,
    };
    uint8_t dst[sizeof(src)] = { 0 };
    IoSystem *system;

    assert(setenv("IO_SYSTEM_IOMMU_RTL_API_SO", IO_IOMMU_STUB_API_SO, 1) == 0);
    system = io_system_create(&config, &host_ops);
    assert(system);

    assert(io_system_q2io_write(system, TEST_IOMMU_BASE + 4, &mmio_value,
                                sizeof(mmio_value)) == IO_SYSTEM_OK);
    assert(io_system_q2io_read(system, TEST_IOMMU_BASE + 4, &mmio_readback,
                               sizeof(mmio_readback)) == IO_SYSTEM_OK);
    assert(mmio_readback == mmio_value);
    assert(io_system_trace_count(system) == 2);

    assert(io_system_dma_write(system, &attrs, TEST_MEM_BASE + 4,
                               src, sizeof(src)) == IO_SYSTEM_OK);
    assert(!memcmp(mem.data + 4, src, sizeof(src)));
    assert(io_system_dma_read(system, &attrs, TEST_MEM_BASE + 4,
                              dst, sizeof(dst)) == IO_SYSTEM_OK);
    assert(!memcmp(dst, src, sizeof(src)));
    assert(io_system_dma_read(system, &bad_attrs, TEST_MEM_BASE + 4,
                              dst, sizeof(dst)) == IO_SYSTEM_ERR_IO);

    io_system_destroy(system);
    return 0;
}
