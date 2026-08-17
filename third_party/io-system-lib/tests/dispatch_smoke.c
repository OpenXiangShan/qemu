#include "io_cmodel_internal.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct TestWindow {
    uint32_t value;
} TestWindow;

static IoSystemStatus test_read(void *opaque, uint64_t addr,
                                void *data, size_t size)
{
    TestWindow *window = opaque;

    (void)addr;
    assert(size == sizeof(window->value));
    memcpy(data, &window->value, size);
    return IO_SYSTEM_OK;
}

static IoSystemStatus test_write(void *opaque, uint64_t addr,
                                 const void *data, size_t size)
{
    TestWindow *window = opaque;

    (void)addr;
    assert(size == sizeof(window->value));
    memcpy(&window->value, data, size);
    return IO_SYSTEM_OK;
}

int main(void)
{
    IoSystem *system = io_system_create(NULL, NULL);
    TestWindow window = { .value = 0x12345678 };
    uint32_t value = 0;

    assert(system);
    assert(io_cmodel_register_mmio_window(system, "test", 0x1000, 0x100,
                                          test_read, test_write,
                                          &window) == IO_SYSTEM_OK);
    assert(io_system_q2io_read(system, 0x1000, &value,
                               sizeof(value)) == IO_SYSTEM_OK);
    assert(value == 0x12345678);

    value = 0xa5a55a5a;
    assert(io_system_q2io_write(system, 0x1004, &value,
                                sizeof(value)) == IO_SYSTEM_OK);
    assert(window.value == 0xa5a55a5a);

    value = 0;
    assert(io_system_q2io_read(system, 0x2000, &value,
                               sizeof(value)) == IO_SYSTEM_ERR_UNMAPPED);
    assert(value == 0xffffffffU);
    assert(io_system_trace_count(system) == 3);

    io_system_destroy(system);
    return 0;
}
