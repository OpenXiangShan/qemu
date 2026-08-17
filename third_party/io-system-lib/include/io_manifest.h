#ifndef IO_MANIFEST_H
#define IO_MANIFEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IO_MANIFEST_NO_IRQ UINT32_MAX

typedef enum IoManifestDevice {
    IO_MANIFEST_DEVICE_MY_VIRTIO_CONSOLE = 0,
    IO_MANIFEST_DEVICE_MY_VIRTIO_NET,
    IO_MANIFEST_DEVICE_MY_VIRTIO_BLK,
    IO_MANIFEST_DEVICE_APLIC_M,
    IO_MANIFEST_DEVICE_APLIC_S,
    IO_MANIFEST_DEVICE_IOMMU,
    IO_MANIFEST_DEVICE_DWC_DMAC,
    IO_MANIFEST_DEVICE_DWC_DBI,
    IO_MANIFEST_DEVICE_PCIE_BAR,
    IO_MANIFEST_DEVICE_PCIE_ECAM,
    IO_MANIFEST_DEVICE__COUNT,
} IoManifestDevice;

typedef struct IoManifestEntry {
    IoManifestDevice device;
    const char *name;
    uint64_t base;
    uint64_t size;
    uint32_t irq;
    bool implemented;
} IoManifestEntry;

typedef struct IoManifest {
    const IoManifestEntry *entries;
    size_t count;
} IoManifest;

const IoManifest *io_manifest_default(void);
const IoManifestEntry *io_manifest_find(const IoManifest *manifest,
                                        IoManifestDevice device);
const IoManifestEntry *io_manifest_find_by_addr(const IoManifest *manifest,
                                                uint64_t addr,
                                                uint64_t size);
const char *io_manifest_device_name(IoManifestDevice device);

#ifdef __cplusplus
}
#endif

#endif
