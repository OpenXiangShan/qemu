#ifndef IO_CMODEL_INTERNAL_H
#define IO_CMODEL_INTERNAL_H

#include "io_system_internal.h"

#define IO_CMODEL_MAX_MMIO_WINDOWS 32

typedef struct IoCModelMmioWindow {
    char name[64];
    uint64_t base;
    uint64_t size;
    IoSystemMmioRead read;
    IoSystemMmioWrite write;
    void *opaque;
} IoCModelMmioWindow;

typedef struct IoCModelMmioWindowTable {
    IoCModelMmioWindow windows[IO_CMODEL_MAX_MMIO_WINDOWS];
    size_t window_count;
} IoCModelMmioWindowTable;

typedef struct IoSystemCModelBackend {
    IoSystemBackend base;
    IoCModelMmioWindowTable windows;
    IoSystemStatus (*service_cb)(void *opaque, uint32_t budget);
    bool (*needs_service_cb)(void *opaque);
    void *service_opaque;
} IoSystemCModelBackend;

typedef IoSystemStatus (*IoCModelService)(void *opaque, uint32_t budget);
typedef bool (*IoCModelNeedsService)(void *opaque);

IoSystemStatus io_cmodel_register_mmio_window(IoSystem *system,
                                              const char *name,
                                              uint64_t base,
                                              uint64_t size,
                                              IoSystemMmioRead read,
                                              IoSystemMmioWrite write,
                                              void *opaque);
IoSystemStatus io_cmodel_register_service(IoSystem *system,
                                          IoCModelService service,
                                          IoCModelNeedsService needs_service,
                                          void *opaque);

void io_cmodel_mmio_window_table_init(IoCModelMmioWindowTable *table);
IoSystemStatus io_cmodel_mmio_window_table_register(
    IoCModelMmioWindowTable *table,
    const char *name,
    uint64_t base,
    uint64_t size,
    IoSystemMmioRead read,
    IoSystemMmioWrite write,
    void *opaque);
const IoCModelMmioWindow *io_cmodel_mmio_window_table_find(
    const IoCModelMmioWindowTable *table,
    uint64_t addr,
    size_t size);

IoSystemStatus io_cmodel_mmio_read_dispatch(IoSystem *system,
                                            IoCModelMmioWindowTable *table,
                                            uint64_t addr,
                                            void *data,
                                            size_t size);
IoSystemStatus io_cmodel_mmio_write_dispatch(IoSystem *system,
                                             IoCModelMmioWindowTable *table,
                                             uint64_t addr,
                                             const void *data,
                                             size_t size);

#endif
