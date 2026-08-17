#include "io_cmodel_internal.h"

#include <stdbool.h>
#include <string.h>

#define IO_CMODEL_MMIO_NAME_SIZE 64

static uint64_t io_cmodel_mmio_range_end(uint64_t base, uint64_t size)
{
    uint64_t end = base + size;

    return end < base ? UINT64_MAX : end;
}

static bool io_cmodel_mmio_range_contains(uint64_t base,
                                          uint64_t size,
                                          uint64_t addr,
                                          size_t access_size)
{
    uint64_t end = io_cmodel_mmio_range_end(base, size);
    uint64_t access_end = io_cmodel_mmio_range_end(addr, access_size);

    return access_size && addr >= base && access_end <= end &&
           access_end > addr;
}

void io_cmodel_mmio_window_table_init(IoCModelMmioWindowTable *table)
{
    if (!table) {
        return;
    }

    memset(table, 0, sizeof(*table));
}

IoSystemStatus io_cmodel_mmio_window_table_register(
    IoCModelMmioWindowTable *table,
    const char *name,
    uint64_t base,
    uint64_t size,
    IoSystemMmioRead read,
    IoSystemMmioWrite write,
    void *opaque)
{
    IoCModelMmioWindow *window;

    if (!table || !size || (!read && !write) ||
        table->window_count >= IO_CMODEL_MAX_MMIO_WINDOWS) {
        return IO_SYSTEM_ERR_INVALID;
    }

    window = &table->windows[table->window_count++];
    memset(window, 0, sizeof(*window));
    if (name && *name) {
        strncpy(window->name, name, IO_CMODEL_MMIO_NAME_SIZE - 1);
    }
    window->base = base;
    window->size = size;
    window->read = read;
    window->write = write;
    window->opaque = opaque;

    return IO_SYSTEM_OK;
}

const IoCModelMmioWindow *io_cmodel_mmio_window_table_find(
    const IoCModelMmioWindowTable *table,
    uint64_t addr,
    size_t size)
{
    if (!table) {
        return NULL;
    }

    for (size_t i = 0; i < table->window_count; i++) {
        const IoCModelMmioWindow *window = &table->windows[i];

        if (io_cmodel_mmio_range_contains(window->base, window->size,
                                          addr, size)) {
            return window;
        }
    }

    return NULL;
}

IoSystemStatus io_cmodel_mmio_read_dispatch(IoSystem *system,
                                            IoCModelMmioWindowTable *table,
                                            uint64_t addr,
                                            void *data,
                                            size_t size)
{
    const IoCModelMmioWindow *window;
    IoSystemStatus status;

    if (!system || !table || !data || !size || size > IO_AXI_MAX_BEAT_BYTES) {
        return IO_SYSTEM_ERR_INVALID;
    }

    window = io_cmodel_mmio_window_table_find(table, addr, size);
    if (!window || !window->read) {
        memset(data, 0xff, size);
        io_system_trace(system, "q2io", "read", addr, data, size,
                        IO_AXI_RESPONSE_DECERR);
        return IO_SYSTEM_ERR_UNMAPPED;
    }

    status = window->read(window->opaque, addr, data, size);
    io_system_trace(system, "q2io", "read", addr, data, size,
                    status == IO_SYSTEM_OK ? IO_AXI_RESPONSE_OKAY :
                    IO_AXI_RESPONSE_SLVERR);
    return status;
}

IoSystemStatus io_cmodel_mmio_write_dispatch(IoSystem *system,
                                             IoCModelMmioWindowTable *table,
                                             uint64_t addr,
                                             const void *data,
                                             size_t size)
{
    const IoCModelMmioWindow *window;
    IoSystemStatus status;

    if (!system || !table || !data || !size || size > IO_AXI_MAX_BEAT_BYTES) {
        return IO_SYSTEM_ERR_INVALID;
    }

    window = io_cmodel_mmio_window_table_find(table, addr, size);
    if (!window || !window->write) {
        io_system_trace(system, "q2io", "write", addr, data, size,
                        IO_AXI_RESPONSE_DECERR);
        return IO_SYSTEM_ERR_UNMAPPED;
    }

    status = window->write(window->opaque, addr, data, size);
    io_system_trace(system, "q2io", "write", addr, data, size,
                    status == IO_SYSTEM_OK ? IO_AXI_RESPONSE_OKAY :
                    IO_AXI_RESPONSE_SLVERR);
    return status;
}
