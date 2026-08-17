#include "io_manifest.h"

static const IoManifestEntry io_default_entries[] = {
    {
        .device = IO_MANIFEST_DEVICE_MY_VIRTIO_CONSOLE,
        .name = "my-virtio-console",
        .base = 0x31080000ULL,
        .size = 0x1000,
        .irq = 17,
        .implemented = false,
    }, {
        .device = IO_MANIFEST_DEVICE_MY_VIRTIO_NET,
        .name = "my-virtio-net",
        .base = 0x31090000ULL,
        .size = 0x1000,
        .irq = 16,
        .implemented = false,
    }, {
        .device = IO_MANIFEST_DEVICE_MY_VIRTIO_BLK,
        .name = "my-virtio-blk",
        .base = 0x310a0000ULL,
        .size = 0x1000,
        .irq = 15,
        .implemented = true,
    }, {
        .device = IO_MANIFEST_DEVICE_APLIC_M,
        .name = "aplic-m",
        .base = 0x31100000ULL,
        .size = 0x4000,
        .irq = IO_MANIFEST_NO_IRQ,
        .implemented = true,
    }, {
        .device = IO_MANIFEST_DEVICE_APLIC_S,
        .name = "aplic-s",
        .base = 0x31120000ULL,
        .size = 0x4000,
        .irq = IO_MANIFEST_NO_IRQ,
        .implemented = true,
    }, {
        .device = IO_MANIFEST_DEVICE_IOMMU,
        .name = "iommu",
        .base = 0x311f0000ULL,
        .size = 0x10000,
        .irq = 0x24,
        .implemented = false,
    }, {
        .device = IO_MANIFEST_DEVICE_DWC_DMAC,
        .name = "dw-axi-dmac",
        .base = 0x30040000ULL,
        .size = 0x1000,
        .irq = 18,
        .implemented = true,
    }, {
        .device = IO_MANIFEST_DEVICE_DWC_DBI,
        .name = "dwc-dbi",
        .base = 0x32000000ULL,
        .size = 0x1000000,
        .irq = IO_MANIFEST_NO_IRQ,
        .implemented = false,
    }, {
        .device = IO_MANIFEST_DEVICE_PCIE_BAR,
        .name = "pcie-bar",
        .base = 0x60000000ULL,
        .size = 0x7ff0000,
        .irq = IO_MANIFEST_NO_IRQ,
        .implemented = false,
    }, {
        .device = IO_MANIFEST_DEVICE_PCIE_ECAM,
        .name = "pcie-ecam",
        .base = 0x67ff0000ULL,
        .size = 0x10000000,
        .irq = IO_MANIFEST_NO_IRQ,
        .implemented = false,
    },
};

static const IoManifest io_default_manifest = {
    .entries = io_default_entries,
    .count = sizeof(io_default_entries) / sizeof(io_default_entries[0]),
};

const IoManifest *io_manifest_default(void)
{
    return &io_default_manifest;
}

const IoManifestEntry *io_manifest_find(const IoManifest *manifest,
                                        IoManifestDevice device)
{
    if (!manifest) {
        manifest = io_manifest_default();
    }

    for (size_t i = 0; i < manifest->count; i++) {
        if (manifest->entries[i].device == device) {
            return &manifest->entries[i];
        }
    }

    return NULL;
}

const IoManifestEntry *io_manifest_find_by_addr(const IoManifest *manifest,
                                                uint64_t addr,
                                                uint64_t size)
{
    if (!manifest || !size) {
        manifest = io_manifest_default();
    }

    for (size_t i = 0; i < manifest->count; i++) {
        const IoManifestEntry *entry = &manifest->entries[i];
        uint64_t end = entry->base + entry->size;
        uint64_t access_end = addr + size;

        if (end < entry->base) {
            end = UINT64_MAX;
        }
        if (access_end < addr) {
            access_end = UINT64_MAX;
        }
        if (addr >= entry->base && access_end <= end && access_end > addr) {
            return entry;
        }
    }

    return NULL;
}

const char *io_manifest_device_name(IoManifestDevice device)
{
    const IoManifestEntry *entry = io_manifest_find(io_manifest_default(),
                                                   device);

    return entry ? entry->name : "unknown";
}
