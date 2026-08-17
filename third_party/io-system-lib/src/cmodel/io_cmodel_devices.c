#include "io_cmodel_internal.h"

#include "io_aplic.h"
#include "io_dwc_dmac.h"
#include "io_dwc_pcie.h"
#include "io_my_virtio_blk.h"

#include <stdlib.h>

#ifndef IO_SYSTEM_QEMU_BACKEND
#define IO_SYSTEM_QEMU_BACKEND 0
#endif

#define IO_CMODEL_DEFAULT_APLIC_NUM_SOURCES 96u
#define IO_CMODEL_DEFAULT_APLIC_NUM_HARTS   1u
#define IO_CMODEL_DEFAULT_APLIC_IPRIO_BITS  8u

struct IoSystemCModelDevices {
    IoAplic *aplic_m;
    IoAplic *aplic_s;
    IoDwcDmac *dmac;
    IoMyVirtioBlk *my_virtio_blk;
#if IO_SYSTEM_QEMU_BACKEND
    IoDwcPcie *pcie;
#endif
};

static uint32_t io_cmodel_config_or_default(uint32_t value,
                                            uint32_t default_value)
{
    return value ? value : default_value;
}

static const IoManifestEntry *io_cmodel_find_manifest(IoSystem *system,
                                                      IoManifestDevice device)
{
    return io_manifest_find(system ? system->manifest : NULL, device);
}

static IoSystemStatus io_cmodel_create_aplics(IoSystem *system,
                                              IoSystemCModelDevices *devices)
{
    const IoManifestEntry *aplic_m;
    const IoManifestEntry *aplic_s;
    const IoSystemConfig *config;
    uint32_t num_sources;
    uint32_t num_harts;
    uint32_t iprio_bits;

    if (!system || !devices) {
        return IO_SYSTEM_ERR_INVALID;
    }

    config = &system->config;
    aplic_m = io_cmodel_find_manifest(system, IO_MANIFEST_DEVICE_APLIC_M);
    aplic_s = io_cmodel_find_manifest(system, IO_MANIFEST_DEVICE_APLIC_S);
    if (!aplic_m || !aplic_s) {
        return IO_SYSTEM_ERR_INVALID;
    }

    num_sources = io_cmodel_config_or_default(
        config->aplic_num_sources, IO_CMODEL_DEFAULT_APLIC_NUM_SOURCES);
    num_harts = io_cmodel_config_or_default(
        config->aplic_num_harts, IO_CMODEL_DEFAULT_APLIC_NUM_HARTS);
    iprio_bits = io_cmodel_config_or_default(
        config->aplic_iprio_bits, IO_CMODEL_DEFAULT_APLIC_IPRIO_BITS);

    devices->aplic_m = io_aplic_create(system, &(IoAplicConfig) {
        .name = aplic_m->name,
        .base = aplic_m->base,
        .size = aplic_m->size,
        .num_sources = num_sources,
        .num_harts = num_harts,
        .iprio_bits = iprio_bits,
        .msimode = true,
        .mmode = true,
    }, NULL);
    if (!devices->aplic_m) {
        return IO_SYSTEM_ERR_IO;
    }

    devices->aplic_s = io_aplic_create(system, &(IoAplicConfig) {
        .name = aplic_s->name,
        .base = aplic_s->base,
        .size = aplic_s->size,
        .num_sources = num_sources,
        .num_harts = num_harts,
        .iprio_bits = iprio_bits,
        .msimode = true,
        .mmode = false,
    }, devices->aplic_m);
    if (!devices->aplic_s) {
        return IO_SYSTEM_ERR_IO;
    }

    return IO_SYSTEM_OK;
}

static IoSystemStatus io_cmodel_create_dmac(IoSystem *system,
                                            IoSystemCModelDevices *devices)
{
    const IoManifestEntry *dmac;

    if (!system || !devices || !devices->aplic_s) {
        return IO_SYSTEM_ERR_INVALID;
    }

    dmac = io_cmodel_find_manifest(system, IO_MANIFEST_DEVICE_DWC_DMAC);
    if (!dmac) {
        return IO_SYSTEM_ERR_INVALID;
    }

    devices->dmac = io_dwc_dmac_create(system, &(IoDwcDmacConfig) {
        .name = dmac->name,
        .base = dmac->base,
        .size = dmac->size,
        .irq = dmac->irq,
        .requester_id = system->config.dmac_requester_id,
    }, devices->aplic_s);
    return devices->dmac ? IO_SYSTEM_OK : IO_SYSTEM_ERR_IO;
}

static IoSystemStatus io_cmodel_create_my_virtio_blk(
    IoSystem *system, IoSystemCModelDevices *devices)
{
    const IoManifestEntry *blk;

    if (!system || !devices || !devices->aplic_s) {
        return IO_SYSTEM_ERR_INVALID;
    }

    blk = io_cmodel_find_manifest(system, IO_MANIFEST_DEVICE_MY_VIRTIO_BLK);
    if (!blk) {
        return IO_SYSTEM_ERR_INVALID;
    }

    devices->my_virtio_blk =
        io_my_virtio_blk_create(system, &(IoMyVirtioBlkConfig) {
            .name = blk->name,
            .base = blk->base,
            .size = blk->size,
            .irq = blk->irq,
            .requester_id = system->config.my_virtio_blk_requester_id,
            .use_iommu = system->config.my_virtio_blk_use_iommu,
            .image_path = system->config.my_virtio_blk_image_path,
        }, devices->aplic_s);
    return devices->my_virtio_blk ? IO_SYSTEM_OK : IO_SYSTEM_ERR_IO;
}

static IoSystemStatus io_cmodel_create_pcie(IoSystem *system,
                                            IoSystemCModelDevices *devices)
{
#if IO_SYSTEM_QEMU_BACKEND
    const IoManifestEntry *dbi;
    const IoManifestEntry *bar;
    const IoManifestEntry *ecam;

    if (!system || !devices || !devices->aplic_s) {
        return IO_SYSTEM_ERR_INVALID;
    }

    dbi = io_cmodel_find_manifest(system, IO_MANIFEST_DEVICE_DWC_DBI);
    bar = io_cmodel_find_manifest(system, IO_MANIFEST_DEVICE_PCIE_BAR);
    ecam = io_cmodel_find_manifest(system, IO_MANIFEST_DEVICE_PCIE_ECAM);
    if (!dbi || !bar || !ecam) {
        return IO_SYSTEM_ERR_INVALID;
    }

    devices->pcie = io_dwc_pcie_create(system, &(IoDwcPcieConfig) {
        .name = "qti-pcie",
        .dbi_base = dbi->base,
        .dbi_size = dbi->size,
        .ecam_base = ecam->base,
        .ecam_size = ecam->size,
        .bar_base = bar->base,
        .bar_size = bar->size,
        .msi_irq = system->config.pcie_msi_irq,
        .inta_irq = system->config.pcie_inta_irq,
        .intb_irq = system->config.pcie_intb_irq,
        .intc_irq = system->config.pcie_intc_irq,
        .intd_irq = system->config.pcie_intd_irq,
        .root_bus_name = system->config.pcie_root_bus_name,
        .secondary_bus_name = system->config.pcie_secondary_bus_name,
        .root_bus_path = system->config.pcie_root_bus_path,
    }, devices->aplic_s);
    return devices->pcie ? IO_SYSTEM_OK : IO_SYSTEM_ERR_IO;
#else
    (void)system;
    (void)devices;
    return IO_SYSTEM_ERR_UNSUPPORTED;
#endif
}

IoSystemStatus io_system_cmodel_devices_create(IoSystem *system)
{
    IoSystemCModelDevices *devices;
    bool need_aplic;
    IoSystemStatus status = IO_SYSTEM_OK;

    if (!system) {
        return IO_SYSTEM_ERR_INVALID;
    }
    if (system->config.backend_kind != IO_SYSTEM_BACKEND_CMODEL) {
        if (system->config.aplic_enabled || system->config.dmac_enabled ||
            system->config.pcie_enabled) {
            return IO_SYSTEM_ERR_UNSUPPORTED;
        }
        return IO_SYSTEM_OK;
    }

    need_aplic = system->config.aplic_enabled ||
                 system->config.dmac_enabled ||
                 system->config.pcie_enabled ||
                 system->config.my_virtio_blk_enabled;
    if (!need_aplic && !system->config.dmac_enabled &&
        !system->config.pcie_enabled &&
        !system->config.my_virtio_blk_enabled) {
        return IO_SYSTEM_OK;
    }

    devices = calloc(1, sizeof(*devices));
    if (!devices) {
        return IO_SYSTEM_ERR_NOMEM;
    }
    system->cmodel_devices = devices;

    if (need_aplic) {
        status = io_cmodel_create_aplics(system, devices);
        if (status != IO_SYSTEM_OK) {
            goto fail;
        }
    }
    if (system->config.dmac_enabled) {
        status = io_cmodel_create_dmac(system, devices);
        if (status != IO_SYSTEM_OK) {
            goto fail;
        }
    }
    if (system->config.my_virtio_blk_enabled) {
        status = io_cmodel_create_my_virtio_blk(system, devices);
        if (status != IO_SYSTEM_OK) {
            goto fail;
        }
    }
    if (system->config.pcie_enabled) {
        status = io_cmodel_create_pcie(system, devices);
        if (status != IO_SYSTEM_OK) {
            goto fail;
        }
    }

    return IO_SYSTEM_OK;

fail:
    io_system_cmodel_devices_destroy(system);
    return status;
}

void io_system_cmodel_devices_destroy(IoSystem *system)
{
    IoSystemCModelDevices *devices;

    if (!system || !system->cmodel_devices) {
        return;
    }

    devices = system->cmodel_devices;
#if IO_SYSTEM_QEMU_BACKEND
    io_dwc_pcie_destroy(devices->pcie);
#endif
    io_my_virtio_blk_destroy(devices->my_virtio_blk);
    io_dwc_dmac_destroy(devices->dmac);
    io_aplic_destroy(devices->aplic_s);
    io_aplic_destroy(devices->aplic_m);
    free(devices);
    system->cmodel_devices = NULL;
}

IoSystemStatus io_system_cmodel_devices_reset(IoSystem *system)
{
    IoSystemCModelDevices *devices;
    IoSystemStatus status;

    if (!system || !system->cmodel_devices) {
        return IO_SYSTEM_OK;
    }

    devices = system->cmodel_devices;
    if (devices->aplic_m) {
        status = io_aplic_reset(devices->aplic_m);
        if (status != IO_SYSTEM_OK) {
            return status;
        }
    }
    if (devices->aplic_s) {
        status = io_aplic_reset(devices->aplic_s);
        if (status != IO_SYSTEM_OK) {
            return status;
        }
    }
    if (devices->dmac) {
        status = io_dwc_dmac_reset(devices->dmac);
        if (status != IO_SYSTEM_OK) {
            return status;
        }
    }
    if (devices->my_virtio_blk) {
        status = io_my_virtio_blk_reset(devices->my_virtio_blk);
        if (status != IO_SYSTEM_OK) {
            return status;
        }
    }
#if IO_SYSTEM_QEMU_BACKEND
    if (devices->pcie) {
        status = io_dwc_pcie_reset(devices->pcie);
        if (status != IO_SYSTEM_OK) {
            return status;
        }
    }
#endif
    return IO_SYSTEM_OK;
}
