#include "io_system.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define TEST_RTL_REG_ID          0x00
#define TEST_RTL_REG_CONTROL     0x08
#define TEST_RTL_REG_STATUS      0x0c
#define TEST_RTL_REG_OUT_ADDR    0x10
#define TEST_RTL_REG_OUT_LEN     0x18
#define TEST_RTL_REG_OUT_DATA    0x40

#define TEST_RTL_ID              0x544c5452u
#define TEST_RTL_CONTROL_KICK    (1u << 0)
#define TEST_RTL_STATUS_PENDING  (1u << 0)
#define TEST_RTL_STATUS_ISSUED   (1u << 1)
#define TEST_RTL_STATUS_DONE     (1u << 2)
#define TEST_RTL_STATUS_ERROR    (1u << 3)

static uint64_t load_le(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    uint64_t value = 0;

    for (size_t i = 0; i < size && i < sizeof(value); i++) {
        value |= (uint64_t)bytes[i] << (8 * i);
    }

    return value;
}

static void store_le(void *data, uint64_t value, size_t size)
{
    uint8_t *bytes = data;

    for (size_t i = 0; i < size; i++) {
        bytes[i] = (uint8_t)(value >> (8 * i));
    }
}

static int guest_write(void *opaque, uint64_t gpa, const void *src,
                       uint32_t len)
{
    uint8_t *mem = opaque;

    if (gpa < 0x1000 || gpa + len > 0x1100) {
        return -1;
    }
    memcpy(mem + gpa - 0x1000, src, len);
    return (int)len;
}

static uint32_t q2io_read32(IoSystem *system, uint64_t addr)
{
    uint8_t data[4];

    assert(io_system_q2io_read(system, addr, data, sizeof(data)) ==
           IO_SYSTEM_OK);
    return (uint32_t)load_le(data, sizeof(data));
}

static void q2io_write32(IoSystem *system, uint64_t addr, uint32_t value)
{
    uint8_t data[4];

    store_le(data, value, sizeof(data));
    assert(io_system_q2io_write(system, addr, data, sizeof(data)) ==
           IO_SYSTEM_OK);
}

static void q2io_write64(IoSystem *system, uint64_t addr, uint64_t value)
{
    uint8_t data[8];

    store_le(data, value, sizeof(data));
    assert(io_system_q2io_write(system, addr, data, sizeof(data)) ==
           IO_SYSTEM_OK);
}

int main(void)
{
    uint8_t guest_mem[0x100] = { 0 };
    IoSystemHostOps host_ops = {
        .opaque = guest_mem,
        .guest_memory_write = guest_write,
    };
    IoSystemConfig config = {
        .name = "rtl-template-smoke",
        .backend_kind = IO_SYSTEM_BACKEND_RTL_TEMPLATE,
        .io2q_async_enabled = true,
        .io2q_outstanding_depth = 4,
        .trace_capacity = 64,
    };
    IoSystem *system = io_system_create(&config, &host_ops);
    const uint64_t out_addr = 0x1000;
    const uint8_t payload[8] = {
        0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
    };
    uint32_t status;

    assert(system);
    assert(q2io_read32(system, TEST_RTL_REG_ID) == TEST_RTL_ID);

    q2io_write64(system, TEST_RTL_REG_OUT_ADDR, out_addr);
    q2io_write32(system, TEST_RTL_REG_OUT_LEN, sizeof(payload));
    assert(io_system_q2io_write(system, TEST_RTL_REG_OUT_DATA, payload,
                                sizeof(payload)) == IO_SYSTEM_OK);
    q2io_write32(system, TEST_RTL_REG_CONTROL, TEST_RTL_CONTROL_KICK);

    status = q2io_read32(system, TEST_RTL_REG_STATUS);
    assert(status & TEST_RTL_STATUS_PENDING);
    assert(!(status & TEST_RTL_STATUS_ERROR));

    assert(io_system_needs_service(system));
    assert(io_system_service(system, 0) == IO_SYSTEM_OK);

    status = q2io_read32(system, TEST_RTL_REG_STATUS);
    assert(!(status & TEST_RTL_STATUS_PENDING));
    assert(status & TEST_RTL_STATUS_DONE);
    assert(!(status & TEST_RTL_STATUS_ERROR));
    assert(!memcmp(guest_mem, payload, sizeof(payload)));
    assert(io_system_trace_count(system) >= 8);

    io_system_destroy(system);
    return 0;
}
