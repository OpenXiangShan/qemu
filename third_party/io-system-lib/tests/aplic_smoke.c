#include "io_aplic.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct TestMemory {
    uint64_t last_addr;
    uint32_t last_value;
    uint32_t last_len;
    int writes;
} TestMemory;

static int test_guest_memory_read(void *opaque, uint64_t gpa, void *dst,
                                  uint32_t len)
{
    (void)opaque;
    (void)gpa;

    memset(dst, 0, len);
    return (int)len;
}

static int test_guest_memory_write(void *opaque, uint64_t gpa, const void *src,
                                   uint32_t len)
{
    TestMemory *mem = opaque;

    mem->last_addr = gpa;
    mem->last_len = len;
    mem->writes++;
    memcpy(&mem->last_value, src, len < sizeof(mem->last_value) ?
           len : sizeof(mem->last_value));
    return (int)len;
}

static int test_guest_memory_atomic(void *opaque, uint64_t gpa, void *value,
                                    uint32_t len, uint32_t op)
{
    (void)opaque;
    (void)gpa;
    (void)value;
    (void)len;
    (void)op;

    return IO_SYSTEM_ERR_UNSUPPORTED;
}

static uint64_t test_clock(void *opaque)
{
    (void)opaque;
    return 0;
}

static void test_log(void *opaque, int level, const char *fmt, va_list ap)
{
    (void)opaque;
    (void)level;
    (void)fmt;
    (void)ap;
}

int main(void)
{
    IoSystem *system;
    IoAplic *aplic;
    IoAplicConfig config = {
        .name = "test-aplic",
        .base = 0x31100000ULL,
        .size = 0x4000,
        .num_sources = 96,
        .num_harts = 1,
        .iprio_bits = 8,
        .msimode = true,
        .mmode = true,
    };
    IoSystemHostOps host_ops = {
        .opaque = &(TestMemory){0},
        .guest_memory_read = test_guest_memory_read,
        .guest_memory_write = test_guest_memory_write,
        .guest_memory_atomic = test_guest_memory_atomic,
        .clock = test_clock,
        .log = test_log,
    };
    TestMemory *mem = host_ops.opaque;
    uint32_t value = 0;
    const IoAxiBeatTrace *trace = NULL;

    system = io_system_create(NULL, &host_ops);
    assert(system);

    aplic = io_aplic_create(system, &config, NULL);
    assert(aplic);

    assert(io_system_q2io_read(system, config.base + IO_APLIC_DOMAINCFG,
                               &value, sizeof(value)) == IO_SYSTEM_OK);
    assert(value == (IO_APLIC_DOMAINCFG_RDONLY | IO_APLIC_DOMAINCFG_DM));

    value = IO_APLIC_DOMAINCFG_IE;
    assert(io_system_q2io_write(system, config.base + IO_APLIC_DOMAINCFG,
                                &value, sizeof(value)) == IO_SYSTEM_OK);

    value = IO_APLIC_SOURCECFG_SM_EDGE_RISE;
    assert(io_system_q2io_write(system,
                                config.base + IO_APLIC_SOURCECFG_BASE,
                                &value, sizeof(value)) == IO_SYSTEM_OK);

    value = 0x55;
    assert(io_system_q2io_write(system, config.base + IO_APLIC_TARGET_BASE,
                                &value, sizeof(value)) == IO_SYSTEM_OK);

    value = 1;
    assert(io_system_q2io_write(system, config.base + IO_APLIC_MMSICFGADDR,
                                &value, sizeof(value)) == IO_SYSTEM_OK);
    value = 0;
    assert(io_system_q2io_write(system, config.base + IO_APLIC_MMSICFGADDRH,
                                &value, sizeof(value)) == IO_SYSTEM_OK);

    value = 1;
    assert(io_system_q2io_write(system, config.base + IO_APLIC_SETIPNUM,
                                &value, sizeof(value)) == IO_SYSTEM_OK);
    assert(io_system_q2io_write(system, config.base + IO_APLIC_SETIENUM,
                                &value, sizeof(value)) == IO_SYSTEM_OK);

    assert(mem->writes == 1);
    assert(mem->last_addr == 0x1000);
    assert(mem->last_len == sizeof(uint32_t));
    assert(mem->last_value == 0x55);

    assert(io_system_trace_count(system) == 9);
    for (size_t i = 0; i < io_system_trace_count(system); i++) {
        const IoAxiBeatTrace *candidate = io_system_trace_at(system, i);

        if (candidate && strcmp(candidate->port, "io2q") == 0 &&
            strcmp(candidate->channel, "memory-write") == 0) {
            trace = candidate;
            break;
        }
    }
    assert(trace);
    assert(trace->address == 0x1000);
    assert(trace->data[0] == 0x55);

    io_aplic_destroy(aplic);
    io_system_destroy(system);
    return 0;
}
