#include "io_manifest.h"

#include <assert.h>

int main(void)
{
    const IoManifest *manifest = io_manifest_default();
    const IoManifestEntry *blk;
    const IoManifestEntry *aplic_s;
    const IoManifestEntry *iommu_ext;
    const IoManifestEntry *dmac;

    assert(manifest);
    assert(manifest->count == IO_MANIFEST_DEVICE__COUNT);

    blk = io_manifest_find(manifest, IO_MANIFEST_DEVICE_MY_VIRTIO_BLK);
    assert(blk);
    assert(blk->base == 0x310a0000ULL);
    assert(blk->size == 0x1000);
    assert(blk->irq == 15);
    assert(blk->implemented);

    aplic_s = io_manifest_find_by_addr(manifest, 0x31120000ULL, 4);
    assert(aplic_s);
    assert(aplic_s->device == IO_MANIFEST_DEVICE_APLIC_S);

    iommu_ext = io_manifest_find_by_addr(manifest, 0x311ff000ULL, 4);
    assert(iommu_ext);
    assert(iommu_ext->device == IO_MANIFEST_DEVICE_IOMMU);

    dmac = io_manifest_find(manifest, IO_MANIFEST_DEVICE_DWC_DMAC);
    assert(dmac);
    assert(dmac->base == 0x30040000ULL);
    assert(dmac->size == 0x1000);
    assert(dmac->irq == 18);
    assert(dmac->implemented);

    return 0;
}
