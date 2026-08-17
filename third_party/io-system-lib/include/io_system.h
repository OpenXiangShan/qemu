#ifndef IO_SYSTEM_H
#define IO_SYSTEM_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "io_axi.h"
#include "io_manifest.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#define IO_SYSTEM_PRINTF_ATTR(fmt_idx, first_arg) \
    __attribute__((format(gnu_printf, fmt_idx, first_arg)))
#else
#define IO_SYSTEM_PRINTF_ATTR(fmt_idx, first_arg)
#endif

typedef struct IoSystem IoSystem;

typedef enum IoSystemStatus {
    IO_SYSTEM_OK = 0,
    IO_SYSTEM_ERR_INVALID = -1,
    IO_SYSTEM_ERR_NOMEM = -2,
    IO_SYSTEM_ERR_UNMAPPED = -3,
    IO_SYSTEM_ERR_UNSUPPORTED = -4,
    IO_SYSTEM_ERR_IO = -5,
    IO_SYSTEM_ERR_NO_TRANSACTION = -6,
    IO_SYSTEM_ERR_BUSY = -7,
} IoSystemStatus;

typedef int (*IoSystemGuestMemoryRead)(void *opaque, uint64_t gpa,
                                       void *dst, uint32_t len);
typedef int (*IoSystemGuestMemoryWrite)(void *opaque, uint64_t gpa,
                                        const void *src, uint32_t len);
typedef int (*IoSystemGuestMemoryAtomic)(void *opaque, uint64_t gpa,
                                         void *value, uint32_t len,
                                         uint32_t op);
typedef void (*IoSystemBackendEvent)(void *opaque, uint32_t event_id);
typedef uint64_t (*IoSystemClock)(void *opaque);
typedef void (*IoSystemLog)(void *opaque, int level, const char *fmt,
                            va_list ap) IO_SYSTEM_PRINTF_ATTR(3, 0);

typedef struct IoSystemHostOps {
    void *opaque;
    IoSystemGuestMemoryRead guest_memory_read;
    IoSystemGuestMemoryWrite guest_memory_write;
    IoSystemGuestMemoryAtomic guest_memory_atomic;
    IoSystemBackendEvent backend_event;
    IoSystemClock clock;
    IoSystemLog log;
} IoSystemHostOps;

typedef enum IoSystemBackendKind {
    IO_SYSTEM_BACKEND_CMODEL = 0,
    IO_SYSTEM_BACKEND_RTL_TEMPLATE = 1,
    IO_SYSTEM_BACKEND_RTL_SYSTEM = 2,
} IoSystemBackendKind;

typedef enum IoSystemServiceMode {
    IO_SYSTEM_SERVICE_INLINE = 0,
    IO_SYSTEM_SERVICE_BH = 1,
} IoSystemServiceMode;

typedef enum IoSystemIommuKind {
    IO_SYSTEM_IOMMU_NONE = 0,
    IO_SYSTEM_IOMMU_CMODEL = 1,
    IO_SYSTEM_IOMMU_RTL = 2,
} IoSystemIommuKind;

typedef enum IoSystemIommuPlacement {
    IO_SYSTEM_IOMMU_PLACEMENT_AUTO = 0,
    IO_SYSTEM_IOMMU_PLACEMENT_EXTERNAL = 1,
    IO_SYSTEM_IOMMU_PLACEMENT_EMBEDDED = 2,
} IoSystemIommuPlacement;

typedef struct IoSystemConfig {
    const char *name;
    const IoManifest *manifest;
    IoSystemBackendKind backend_kind;
    const char *backend_library_path;
    const char *backend_vcs_libdir;
    IoSystemIommuKind iommu_kind;
    IoSystemIommuPlacement iommu_placement;
    const char *iommu_refmodel_dir;
    const char *iommu_rtl_ip_dir;
    const char *iommu_picker_out;
    const char *iommu_vcs_libdir;
    bool aplic_enabled;
    uint32_t aplic_num_sources;
    uint32_t aplic_num_harts;
    uint32_t aplic_iprio_bits;
    bool dmac_enabled;
    uint32_t dmac_requester_id;
    bool pcie_enabled;
    uint32_t pcie_msi_irq;
    uint32_t pcie_inta_irq;
    uint32_t pcie_intb_irq;
    uint32_t pcie_intc_irq;
    uint32_t pcie_intd_irq;
    const char *pcie_root_bus_name;
    const char *pcie_secondary_bus_name;
    const char *pcie_root_bus_path;
    bool my_virtio_blk_enabled;
    uint32_t my_virtio_blk_requester_id;
    bool my_virtio_blk_use_iommu;
    const char *my_virtio_blk_image_path;
    uint32_t io2q_max_beat_bytes;
    bool io2q_async_enabled;
    uint32_t io2q_outstanding_depth;
    IoSystemServiceMode service_mode;
    /* Kept as an ABI source-compatibility alias for older callers. */
    uint32_t io2q_initial_outstanding;
    uint32_t trace_capacity;
} IoSystemConfig;

IoSystem *io_system_create(const IoSystemConfig *config,
                           const IoSystemHostOps *host_ops);
void io_system_destroy(IoSystem *system);
IoSystemStatus io_system_reset(IoSystem *system);

IoSystemStatus io_system_q2io_read(IoSystem *system, uint64_t addr,
                                   void *data, size_t size);
IoSystemStatus io_system_q2io_write(IoSystem *system, uint64_t addr,
                                    const void *data, size_t size);

IoSystemStatus io_system_service(IoSystem *system, uint32_t budget);
bool io_system_needs_service(IoSystem *system);

size_t io_system_trace_count(const IoSystem *system);
const IoAxiBeatTrace *io_system_trace_at(const IoSystem *system,
                                         size_t index);

#ifdef __cplusplus
}
#endif

#undef IO_SYSTEM_PRINTF_ATTR

#endif
