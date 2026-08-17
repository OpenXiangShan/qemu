#include "io_system.h"
#include "io_system_internal_api.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define TEST_MEM_BASE 0x80000000ULL

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
    IoSystem *system = io_system_create(NULL, &host_ops);
    const uint8_t src[8] = {
        0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
    };
    uint8_t dst[sizeof(src)] = { 0 };

    assert(system);
    assert(io_system_dma_write(system, &attrs, TEST_MEM_BASE + 4,
                               src, sizeof(src)) == IO_SYSTEM_OK);
    assert(!memcmp(mem.data + 4, src, sizeof(src)));
    assert(io_system_dma_read(system, &attrs, TEST_MEM_BASE + 4,
                              dst, sizeof(dst)) == IO_SYSTEM_OK);
    assert(!memcmp(dst, src, sizeof(src)));

    io_system_destroy(system);
    return 0;
}
