#ifndef IO_VIRTIO_IOMMU_H
#define IO_VIRTIO_IOMMU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct IoVirtioIommu IoVirtioIommu;

typedef enum IoVirtioIommuMode {
    IO_VIRTIO_IOMMU_MODE_S1 = 0,
    IO_VIRTIO_IOMMU_MODE_S2 = 1,
    IO_VIRTIO_IOMMU_MODE_S1_S2 = 2,
} IoVirtioIommuMode;

typedef enum IoVirtioIommuStage {
    IO_VIRTIO_IOMMU_STAGE_S1 = 0,
    IO_VIRTIO_IOMMU_STAGE_S2 = 1,
} IoVirtioIommuStage;

enum {
    IO_VIRTIO_IOMMU_ACCESS_READ = 1U << 0,
    IO_VIRTIO_IOMMU_ACCESS_WRITE = 1U << 1,
    IO_VIRTIO_IOMMU_ACCESS_EXEC = 1U << 2,
};

enum {
    IO_VIRTIO_IOMMU_MAP_READ = 1U << 0,
    IO_VIRTIO_IOMMU_MAP_WRITE = 1U << 1,
    IO_VIRTIO_IOMMU_MAP_MMIO = 1U << 2,
};

typedef struct IoVirtioIommuConfig {
    IoVirtioIommuMode mode;
    uint32_t max_endpoints;
    uint64_t shadow_base;
    bool trace;
} IoVirtioIommuConfig;

typedef struct IoVirtioIommuFault {
    uint32_t cause;
    uint32_t endpoint;
    uint32_t process_id;
    uint32_t access;
    uint64_t address;
    uint64_t address2;
} IoVirtioIommuFault;

IoVirtioIommu *io_virtio_iommu_create(const IoVirtioIommuConfig *config);
void io_virtio_iommu_destroy(IoVirtioIommu *iommu);
int io_virtio_iommu_reset(IoVirtioIommu *iommu);

int io_virtio_iommu_attach(IoVirtioIommu *iommu, uint32_t domain,
                           uint32_t endpoint, uint32_t flags,
                           IoVirtioIommuMode mode);
int io_virtio_iommu_detach(IoVirtioIommu *iommu, uint32_t domain,
                           uint32_t endpoint);
int io_virtio_iommu_map(IoVirtioIommu *iommu, uint32_t domain,
                        uint64_t virt_start, uint64_t virt_end,
                        uint64_t phys_start, uint32_t flags,
                        IoVirtioIommuMode mode);
int io_virtio_iommu_unmap(IoVirtioIommu *iommu, uint32_t domain,
                          uint64_t virt_start, uint64_t virt_end,
                          IoVirtioIommuMode mode);

int io_virtio_iommu_map_stage(IoVirtioIommu *iommu, uint32_t domain,
                              uint64_t virt_start, uint64_t virt_end,
                              uint64_t phys_start, uint32_t flags,
                              IoVirtioIommuStage stage);
int io_virtio_iommu_unmap_stage(IoVirtioIommu *iommu, uint32_t domain,
                                uint64_t virt_start, uint64_t virt_end,
                                IoVirtioIommuStage stage);

int io_virtio_iommu_translate(IoVirtioIommu *iommu, uint32_t endpoint,
                              uint64_t address, size_t length,
                              uint32_t access, uint64_t *translated,
                              uint64_t *address_mask);

/* Return the currently attached backend domain for an endpoint.  This is
 * used by the host-only inner-guest relay; standard virtio requests continue
 * to use the logical domain ID carried by the request. */
int io_virtio_iommu_endpoint_domain(IoVirtioIommu *iommu, uint32_t endpoint,
                                    uint32_t *domain);

/* Host-only inner-guest relay operations.  namespace identifies the KVM VM;
 * inner_domain remains private to that VM and is never programmed into the
 * native DC.  The endpoint's already-attached outer domain supplies the S2
 * half of the combined walk. */
int io_virtio_iommu_relay_attach(IoVirtioIommu *iommu, uint64_t namespace_id,
                                 uint32_t inner_domain, uint32_t endpoint);
int io_virtio_iommu_relay_detach(IoVirtioIommu *iommu, uint64_t namespace_id,
                                 uint32_t inner_domain, uint32_t endpoint);
int io_virtio_iommu_relay_map(IoVirtioIommu *iommu, uint64_t namespace_id,
                              uint32_t inner_domain, uint32_t endpoint,
                              uint64_t virt_start, uint64_t virt_end,
                              uint64_t phys_start, uint32_t flags);
int io_virtio_iommu_relay_unmap(IoVirtioIommu *iommu, uint64_t namespace_id,
                                uint32_t inner_domain, uint32_t endpoint,
                                uint64_t virt_start, uint64_t virt_end);
int io_virtio_iommu_pop_fault(IoVirtioIommu *iommu,
                              IoVirtioIommuFault *fault);

#ifdef __cplusplus
}
#endif

#endif
