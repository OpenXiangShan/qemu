/*
 * QEMU RISC-V board that fronts the external io-system C-model.
 *
 * Copyright (c) 2026 Beijing Institute of Open Source Chip (BOSC)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include CONFIG_DEVICES
#include "qapi/error.h"
#include "qapi/qapi-visit-common.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/units.h"
#include "system/address-spaces.h"
#include "system/device_tree.h"
#include "system/kvm.h"
#include "system/system.h"
#include "hw/boards.h"
#include "hw/char/serial-mm.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/intc/riscv_imsic.h"
#include "hw/loader.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "hw/riscv/boot.h"
#include "hw/riscv/qemu_to_iosystem.h"
#include "hw/riscv/riscv_hart.h"
#include "io_dwc_dmac.h"
#include "target/riscv/cpu.h"
#include "target/riscv/cpu_bits.h"

#include <libfdt.h>

#define QTI_BIOS_BIN "opensbi-riscv64-xiangshan-kmh-fw_dynamic.bin"
#define QTI_FW_JUMP_FDT_ADDR 0x82200000ULL
#define QTI_UART0_CLOCK 50000000
#define QTI_CLINT_TIMEBASE_FREQ 1000000
#define QTI_IMSIC_NUM_IDS 255
#define QTI_IMSIC_NUM_GUESTS 5
#define QTI_IMSIC_GUEST_BITS 3
#define QTI_APLIC_NUM_SOURCES 96
#define QTI_DMAC_CLOCK 50000000
#define QTI_TRACE_CAPACITY_WITH_FILE 65536
#define QTI_RTL_SYSTEM_MMIO_BASE 0x30000000ULL
#define QTI_RTL_SYSTEM_MMIO_SIZE 0x47ff0000ULL
#define QTI_RTL_SYSTEM_DMAC_COMPAT "bosc,io-system-rtl-mem2mem-dmac"
#define QTI_UART0_IRQ 10
#define QTI_PCIE_MSI_IRQ 24
#define QTI_PCIE_INTA_IRQ 25
#define QTI_PCIE_INTB_IRQ 26
#define QTI_PCIE_INTC_IRQ 27
#define QTI_PCIE_INTD_IRQ 28
#define QTI_PCIE_HP_IRQ 29
#define QTI_PCIE_MEM32_BUS_BASE 0x40000000ULL
#define QTI_PCIE_MEM64_BUS_BASE 0x100000000ULL
#define QTI_PCIE_WINDOW_SIZE 0x02000000ULL
#define QTI_FDT_MAX_INT_MAP_WIDTH 7
#define FDT_IRQ_TYPE_EDGE_RISING 4
#define FDT_IRQ_TYPE_LEVEL_HIGH 4

enum {
    QTI_ROM,
    QTI_FLASH,
    QTI_UART0,
    QTI_CLINT,
    QTI_SRAM,
    QTI_IMSIC_M,
    QTI_IMSIC_S,
    QTI_DRAM,
};

static const MemMapEntry qti_memmap[] = {
    [QTI_ROM]     = { 0x1000,     0x40000 },
    [QTI_FLASH]   = { 0x10000000, 0x4000000 },
    [QTI_UART0]   = { 0x310b0000, 0x10000 },
    [QTI_CLINT]   = { 0x38000000, 0x10000 },
    [QTI_SRAM]    = { 0x37f00000, 0x100000 },
    [QTI_IMSIC_M] = { 0x3a800000, 0x10000 },
    [QTI_IMSIC_S] = { 0x3b000000, 0x80000 },
    [QTI_DRAM]    = { 0x80000000, 0x0 },
};

static const IoManifestEntry *qti_manifest_entry(IoManifestDevice device)
{
    const IoManifestEntry *entry = io_manifest_find(io_manifest_default(),
                                                    device);

    if (!entry) {
        error_report("io-system manifest is missing device %d", device);
        exit(1);
    }

    return entry;
}

static IoSystemBackendKind qti_parse_backend_kind(const char *name)
{
    if (!name || !*name || !g_strcmp0(name, "cmodel")) {
        return IO_SYSTEM_BACKEND_CMODEL;
    }
    if (!g_strcmp0(name, "rtl-template")) {
        return IO_SYSTEM_BACKEND_RTL_TEMPLATE;
    }
    if (!g_strcmp0(name, "rtl-system")) {
        return IO_SYSTEM_BACKEND_RTL_SYSTEM;
    }

    error_report("invalid io-system-backend '%s' (expected cmodel, rtl-template or rtl-system)",
                 name);
    exit(1);
}

static const char *qti_backend_kind_name(IoSystemBackendKind kind)
{
    switch (kind) {
    case IO_SYSTEM_BACKEND_CMODEL:
        return "cmodel";
    case IO_SYSTEM_BACKEND_RTL_TEMPLATE:
        return "rtl-template";
    case IO_SYSTEM_BACKEND_RTL_SYSTEM:
        return "rtl-system";
    default:
        return "unknown";
    }
}

static bool qti_backend_uses_cmodel_devices(IoSystemBackendKind kind)
{
    return kind == IO_SYSTEM_BACKEND_CMODEL;
}

static bool qti_backend_uses_rtl_system(IoSystemBackendKind kind)
{
    return kind == IO_SYSTEM_BACKEND_RTL_SYSTEM;
}

static const char *qti_axi_response_name(IoAxiResponse response)
{
    switch (response) {
    case IO_AXI_RESPONSE_OKAY:
        return "OKAY";
    case IO_AXI_RESPONSE_EXOKAY:
        return "EXOKAY";
    case IO_AXI_RESPONSE_SLVERR:
        return "SLVERR";
    case IO_AXI_RESPONSE_DECERR:
        return "DECERR";
    default:
        return "UNKNOWN";
    }
}

static void qti_trace_open(QemuToIoSystemState *s)
{
    if (!s->io_system_trace_file || !*s->io_system_trace_file ||
        s->io_system_trace_fp) {
        return;
    }

    s->io_system_trace_fp = fopen(s->io_system_trace_file, "w");
    if (!s->io_system_trace_fp) {
        error_report("failed to open io-system trace file '%s': %s",
                     s->io_system_trace_file, g_strerror(errno));
        exit(1);
    }
    setvbuf(s->io_system_trace_fp, NULL, _IOLBF, 0);
}

static void qti_trace_dump_one(FILE *fp, const IoAxiBeatTrace *trace)
{
    uint8_t beat_size = MIN(trace->beat_size, IO_AXI_MAX_BEAT_BYTES);

    fprintf(fp,
            "port=%s channel=%s transaction_id=%u address=0x%016" PRIx64
            " beat_index=%u beat_size=%u response=%s data=0x",
            trace->port ? trace->port : "",
            trace->channel ? trace->channel : "",
            trace->transaction_id, trace->address, trace->beat_index,
            trace->beat_size, qti_axi_response_name(trace->response));
    for (uint8_t i = 0; i < beat_size; i++) {
        fprintf(fp, "%02x", trace->data[i]);
    }
    fprintf(fp, " strobe=0x");
    for (uint8_t i = 0; i < beat_size; i++) {
        fprintf(fp, "%02x", trace->strobe[i]);
    }
    fputc('\n', fp);
}

static void qti_trace_dump_new(QemuToIoSystemState *s)
{
    size_t trace_count;

    qti_trace_open(s);
    if (!s->io_system_trace_fp || !s->io_system) {
        return;
    }

    trace_count = io_system_trace_count(s->io_system);
    while (s->io_system_trace_emitted < trace_count) {
        const IoAxiBeatTrace *trace =
            io_system_trace_at(s->io_system, s->io_system_trace_emitted);

        if (trace) {
            qti_trace_dump_one(s->io_system_trace_fp, trace);
        }
        s->io_system_trace_emitted++;
    }
    fflush(s->io_system_trace_fp);
}

static uint64_t qti_io_bridge_read(void *opaque, hwaddr offset, unsigned size)
{
    QemuToIoSystemBridgeWindow *window = opaque;
    QemuToIoSystemState *s = window->machine;
    uint8_t buf[8] = { 0 };
    const char *name = window->entry ? window->entry->name : window->name;
    uint64_t addr = window->base + offset;
    IoSystemStatus status;

    status = io_system_q2io_read(s->io_system, addr, buf, size);
    if (status != IO_SYSTEM_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: %s read failed addr=0x%" PRIx64
                      " size=%u status=%d\n",
                      __func__, name ? name : "io-system", addr, size,
                      status);
        memset(buf, 0xff, size);
    }

    qti_trace_dump_new(s);
    return ldn_le_p(buf, size);
}

static void qti_io_bridge_write(void *opaque, hwaddr offset,
                                uint64_t value, unsigned size)
{
    QemuToIoSystemBridgeWindow *window = opaque;
    QemuToIoSystemState *s = window->machine;
    uint8_t buf[8];
    const char *name = window->entry ? window->entry->name : window->name;
    uint64_t addr = window->base + offset;
    IoSystemStatus status;

    stn_le_p(buf, size, value);
    status = io_system_q2io_write(s->io_system, addr, buf, size);
    if (status != IO_SYSTEM_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: %s write failed addr=0x%" PRIx64
                      " size=%u value=0x%" PRIx64 " status=%d\n",
                      __func__, name ? name : "io-system", addr, size,
                      value, status);
    }
    qti_trace_dump_new(s);
}

static const MemoryRegionOps qti_io_bridge_ops = {
    .read = qti_io_bridge_read,
    .write = qti_io_bridge_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static void qti_create_io_bridge_window(QemuToIoSystemState *s,
                                        IoManifestDevice device,
                                        MemoryRegion *system_memory)
{
    const IoManifestEntry *entry = qti_manifest_entry(device);
    QemuToIoSystemBridgeWindow *window = &s->io_windows[device];
    g_autofree char *name = g_strdup_printf("qemu-to-iosystem.%s",
                                            entry->name);

    window->entry = entry;
    window->machine = s;
    window->name = entry->name;
    window->base = entry->base;
    memory_region_init_io(&window->mr, OBJECT(s), &qti_io_bridge_ops,
                          window, name, entry->size);
    memory_region_add_subregion(system_memory, entry->base, &window->mr);
}

static void qti_create_rtl_system_bridge_window(QemuToIoSystemState *s,
                                                MemoryRegion *system_memory)
{
    QemuToIoSystemBridgeWindow *window = &s->rtl_system_window;

    window->entry = NULL;
    window->machine = s;
    window->name = "rtl-system";
    window->base = QTI_RTL_SYSTEM_MMIO_BASE;
    memory_region_init_io(&window->mr, OBJECT(s), &qti_io_bridge_ops,
                          window, "qemu-to-iosystem.rtl-system",
                          QTI_RTL_SYSTEM_MMIO_SIZE);
    /*
     * This is the whole RTL io-system Q2IO aperture, not a per-device
     * demux.  It ends at the manifest PCIe ECAM end address.  QEMU-owned
     * devices inside the same address range (UART, CLINT, IMSIC, SRAM) remain
     * visible because they are registered at the default higher priority.
     */
    memory_region_add_subregion_overlap(system_memory,
                                        QTI_RTL_SYSTEM_MMIO_BASE,
                                        &window->mr, -1);
}

static void qti_create_io_bridge_windows(QemuToIoSystemState *s,
                                         MemoryRegion *system_memory)
{
    const IoManifest *manifest = io_manifest_default();

    for (size_t i = 0; i < manifest->count; i++) {
        const IoManifestEntry *entry = &manifest->entries[i];

        if (entry->device >= IO_MANIFEST_DEVICE__COUNT) {
            error_report("io-system manifest has invalid device id %d",
                         entry->device);
            exit(1);
        }

        qti_create_io_bridge_window(s, entry->device, system_memory);
    }
}

static void qti_create_pcie(QemuToIoSystemState *s)
{
    const IoManifestEntry *dbi = qti_manifest_entry(IO_MANIFEST_DEVICE_DWC_DBI);
    const IoManifestEntry *bar = qti_manifest_entry(IO_MANIFEST_DEVICE_PCIE_BAR);
    const IoManifestEntry *ecam = qti_manifest_entry(IO_MANIFEST_DEVICE_PCIE_ECAM);

    s->pcie = io_dwc_pcie_create(s->io_system, &(IoDwcPcieConfig) {
        .name = "qti-pcie",
        .dbi_base = dbi->base,
        .dbi_size = dbi->size,
        .ecam_base = ecam->base,
        .ecam_size = ecam->size,
        .bar_base = bar->base,
        .bar_size = bar->size,
        .msi_irq = QTI_PCIE_MSI_IRQ,
        .inta_irq = QTI_PCIE_INTA_IRQ,
        .intb_irq = QTI_PCIE_INTB_IRQ,
        .intc_irq = QTI_PCIE_INTC_IRQ,
        .intd_irq = QTI_PCIE_INTD_IRQ,
        .root_bus_name = "qti-pcie-root",
        .secondary_bus_name = "qti-pcie",
        .root_bus_path = "0000:00",
    }, s->aplic_s);

    if (!s->pcie) {
        error_report("failed to create io-system DWC PCIe");
        exit(1);
    }
}

static void qti_create_dmac(QemuToIoSystemState *s)
{
    const IoManifestEntry *dmac = qti_manifest_entry(
        IO_MANIFEST_DEVICE_DWC_DMAC);

    s->dmac = io_dwc_dmac_create(s->io_system, &(IoDwcDmacConfig) {
        .name = dmac->name,
        .base = dmac->base,
        .size = dmac->size,
        .irq = dmac->irq,
        .requester_id = 0x8,
    }, s->aplic_s);
    if (!s->dmac) {
        error_report("failed to create io-system DWC DMAC");
        exit(1);
    }
}

static int qti_guest_memory_read(void *opaque, uint64_t gpa, void *dst,
                                 uint32_t len)
{
    (void)opaque;

    return address_space_rw(&address_space_memory, gpa,
                            MEMTXATTRS_UNSPECIFIED, dst, len, false) ==
           MEMTX_OK ? (int)len : -1;
}

static int qti_guest_memory_write(void *opaque, uint64_t gpa, const void *src,
                                  uint32_t len)
{
    (void)opaque;

    return address_space_rw(&address_space_memory, gpa,
                            MEMTXATTRS_UNSPECIFIED, (void *)src, len, true) ==
           MEMTX_OK ? (int)len : -1;
}

static int qti_guest_memory_atomic(void *opaque, uint64_t gpa, void *value,
                                   uint32_t len, uint32_t op)
{
    (void)opaque;
    (void)gpa;
    (void)value;
    (void)len;
    (void)op;

    return IO_SYSTEM_ERR_UNSUPPORTED;
}

static uint64_t qti_clock(void *opaque)
{
    (void)opaque;

    return qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}

static void qti_log(void *opaque, int level, const char *fmt, va_list ap)
    G_GNUC_PRINTF(3, 0);

static bool qti_info_logs_enabled(void)
{
    static int initialized;
    static bool enabled;

    if (!initialized) {
        const char *env = getenv("QTI_IO_SYSTEM_INFO_LOG");

        enabled = env && env[0] && env[0] != '0';
        initialized = 1;
    }
    return enabled;
}

static void qti_log(void *opaque, int level, const char *fmt, va_list ap)
{
    (void)opaque;

    if (level <= 0) {
        error_vreport(fmt, ap);
    } else if (level == 1) {
        warn_vreport(fmt, ap);
    } else if (qti_info_logs_enabled()) {
        info_vreport(fmt, ap);
    }
}

static void qti_create_io_system(QemuToIoSystemState *s)
{
    MachineState *machine = MACHINE(s);
    IoSystemConfig config = {
        .name = "qemu_to_iosystem",
        .manifest = io_manifest_default(),
        .backend_kind = s->io_system_backend_kind,
        .backend_library_path = s->io_system_backend_lib,
        .backend_vcs_libdir = s->io_system_vcs_libdir,
        .my_virtio_blk_enabled = s->my_virtio_blk,
        .my_virtio_blk_image_path = s->my_virtio_blk_image,
        .io2q_max_beat_bytes = IO_AXI_MAX_BEAT_BYTES,
        .io2q_initial_outstanding = 1,
        .trace_capacity = s->io_system_trace_file &&
                          *s->io_system_trace_file ?
                          QTI_TRACE_CAPACITY_WITH_FILE : 0,
    };
    IoSystemHostOps host_ops = {
        .opaque = s,
        .guest_memory_read = qti_guest_memory_read,
        .guest_memory_write = qti_guest_memory_write,
        .guest_memory_atomic = qti_guest_memory_atomic,
        .clock = qti_clock,
        .log = qti_log,
    };

    s->io_system = io_system_create(&config, &host_ops);
    if (!s->io_system) {
        error_report("failed to create io-system %s context",
                     qti_backend_kind_name(s->io_system_backend_kind));
        exit(1);
    }

    if (qti_backend_uses_cmodel_devices(s->io_system_backend_kind)) {
        const IoManifestEntry *aplic_m = qti_manifest_entry(
            IO_MANIFEST_DEVICE_APLIC_M);
        const IoManifestEntry *aplic_s = qti_manifest_entry(
            IO_MANIFEST_DEVICE_APLIC_S);

        s->aplic_m = io_aplic_create(s->io_system, &(IoAplicConfig) {
            .name = aplic_m->name,
            .base = aplic_m->base,
            .size = aplic_m->size,
            .num_sources = QTI_APLIC_NUM_SOURCES,
            .num_harts = machine->smp.cpus,
            .iprio_bits = 8,
            .msimode = true,
            .mmode = true,
        }, NULL);
        if (!s->aplic_m) {
            error_report("failed to create io-system machine APLIC");
            exit(1);
        }

        s->aplic_s = io_aplic_create(s->io_system, &(IoAplicConfig) {
            .name = aplic_s->name,
            .base = aplic_s->base,
            .size = aplic_s->size,
            .num_sources = QTI_APLIC_NUM_SOURCES,
            .num_harts = machine->smp.cpus,
            .iprio_bits = 8,
            .msimode = true,
            .mmode = false,
        }, s->aplic_m);
        if (!s->aplic_s) {
            error_report("failed to create io-system supervisor APLIC");
            exit(1);
        }

        qti_create_dmac(s);
    }
}

static void qti_create_my_virtio_blk(QemuToIoSystemState *s)
{
    const IoManifestEntry *blk = qti_manifest_entry(
        IO_MANIFEST_DEVICE_MY_VIRTIO_BLK);

    s->my_virtio_blk_dev = io_my_virtio_blk_create(s->io_system,
                                                   &(IoMyVirtioBlkConfig) {
        .name = blk->name,
        .base = blk->base,
        .size = blk->size,
        .irq = blk->irq,
        .image_path = s->my_virtio_blk_image,
    }, s->aplic_s);
    if (!s->my_virtio_blk_dev) {
        error_report("failed to create io-system my-virtio-blk");
        exit(1);
    }
}

static void qti_create_imsics(uint32_t num_harts)
{
    for (uint32_t i = 0; i < num_harts; i++) {
        riscv_imsic_create(qti_memmap[QTI_IMSIC_M].base +
                           i * IMSIC_HART_SIZE(0),
                           i, true, 1, QTI_IMSIC_NUM_IDS);
        riscv_imsic_create(qti_memmap[QTI_IMSIC_S].base +
                           i * IMSIC_HART_SIZE(QTI_IMSIC_GUEST_BITS),
                           i, false, 1 + QTI_IMSIC_NUM_GUESTS,
                           QTI_IMSIC_NUM_IDS);
    }
}

static bool qti_should_generate_dtb(QemuToIoSystemState *s)
{
    MachineState *machine = MACHINE(s);

    return s->generated_dtb == ON_OFF_AUTO_ON ||
           (s->generated_dtb == ON_OFF_AUTO_AUTO && !machine->dtb);
}

static void qti_fdt_add_memory(QemuToIoSystemState *s)
{
    MachineState *ms = MACHINE(s);
    void *fdt = ms->fdt;
    g_autofree char *name = g_strdup_printf("/memory@%"HWADDR_PRIx,
        qti_memmap[QTI_DRAM].base);

    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "device_type", "memory");
    qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                 2, qti_memmap[QTI_DRAM].base,
                                 2, ms->ram_size);
}

static void qti_fdt_add_virtio_blk(QemuToIoSystemState *s,
                                   uint32_t aplic_s_phandle)
{
    MachineState *ms = MACHINE(s);
    void *fdt = ms->fdt;
    const IoManifestEntry *blk = qti_manifest_entry(
        IO_MANIFEST_DEVICE_MY_VIRTIO_BLK);
    g_autofree char *name = g_strdup_printf("/soc/virtio@%"PRIx64,
                                            blk->base);

    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "virtio,mmio");
    qemu_fdt_setprop_sized_cells(fdt, name, "reg", 2, blk->base, 2, blk->size);
    qemu_fdt_setprop_cell(fdt, name, "interrupt-parent", aplic_s_phandle);
    qemu_fdt_setprop_cells(fdt, name, "interrupts",
                           blk->irq, FDT_IRQ_TYPE_EDGE_RISING);
    qemu_fdt_setprop_string(fdt, name, "status", "okay");
}

static void qti_fdt_add_pcie_irq_map(void *fdt, const char *node_path,
                                     uint32_t aplic_s_phandle)
{
    uint32_t full_irq_map[PCI_NUM_PINS * PCI_NUM_PINS *
                          QTI_FDT_MAX_INT_MAP_WIDTH] = { 0 };
    uint32_t *irq_map = full_irq_map;
    int irq_map_stride = 0;

    for (int dev = 0; dev < PCI_NUM_PINS; dev++) {
        int devfn = dev * 0x8;

        for (int pin = 0; pin < PCI_NUM_PINS; pin++) {
            int irq_nr = QTI_PCIE_INTA_IRQ + ((pin + dev) % PCI_NUM_PINS);
            int i = 0;

            irq_map[i++] = cpu_to_be32(devfn << 8);
            irq_map[i++] = 0;
            irq_map[i++] = 0;
            irq_map[i++] = cpu_to_be32(pin + 1);
            irq_map[i++] = cpu_to_be32(aplic_s_phandle);
            irq_map[i++] = cpu_to_be32(irq_nr);
            irq_map[i++] = cpu_to_be32(FDT_IRQ_TYPE_LEVEL_HIGH);

            if (!irq_map_stride) {
                irq_map_stride = i;
            }
            irq_map += irq_map_stride;
        }
    }

    qemu_fdt_setprop(fdt, node_path, "interrupt-map", full_irq_map,
                     sizeof(full_irq_map));
    qemu_fdt_setprop_cells(fdt, node_path, "interrupt-map-mask",
                           0x1800, 0, 0, 0x7);
}

static void qti_fdt_add_dmac(QemuToIoSystemState *s,
                             uint32_t aplic_s_phandle,
                             uint32_t *phandle)
{
    MachineState *ms = MACHINE(s);
    void *fdt = ms->fdt;
    const IoManifestEntry *dmac = qti_manifest_entry(
        IO_MANIFEST_DEVICE_DWC_DMAC);
    uint32_t dmac_clk_phandle;
    g_autofree char *clk_name = g_strdup("/dmac-clk");
    g_autofree char *name = g_strdup_printf("/soc/dma-controller@%"PRIx64,
                                            dmac->base);
    static const char *const clock_names[] = {
        "core-clk",
        "cfgr-clk",
    };

    dmac_clk_phandle = (*phandle)++;

    qemu_fdt_add_subnode(fdt, clk_name);
    qemu_fdt_setprop_string(fdt, clk_name, "compatible", "fixed-clock");
    qemu_fdt_setprop_cell(fdt, clk_name, "#clock-cells", 0);
    qemu_fdt_setprop_cell(fdt, clk_name, "clock-frequency", QTI_DMAC_CLOCK);
    qemu_fdt_setprop_cell(fdt, clk_name, "phandle", dmac_clk_phandle);
    qemu_fdt_setprop_string(fdt, clk_name, "status", "okay");

    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "snps,axi-dma-1.01a");
    qemu_fdt_setprop(fdt, name, "dma-controller", NULL, 0);
    qemu_fdt_setprop_cell(fdt, name, "#dma-cells", 1);
    qemu_fdt_setprop_cell(fdt, name, "dma-channels", IO_DWC_DMAC_NR_CHANS);
    qemu_fdt_setprop_cell(fdt, name, "snps,dma-masters", 1);
    qemu_fdt_setprop_cell(fdt, name, "snps,data-width", 3);
    qemu_fdt_setprop_cells(fdt, name, "snps,block-size", 4096, 4096);
    qemu_fdt_setprop_cells(fdt, name, "snps,priority", 0, 1);
    qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                 2, dmac->base,
                                 2, dmac->size);
    qemu_fdt_setprop_cell(fdt, name, "interrupt-parent", aplic_s_phandle);
    qemu_fdt_setprop_cells(fdt, name, "interrupts",
                           dmac->irq, FDT_IRQ_TYPE_LEVEL_HIGH);
    qemu_fdt_setprop_cells(fdt, name, "clocks",
                           dmac_clk_phandle, dmac_clk_phandle);
    qemu_fdt_setprop_string_array(fdt, name, "clock-names",
                                  (char **)&clock_names,
                                  ARRAY_SIZE(clock_names));
    qemu_fdt_setprop_string(fdt, name, "status", "okay");
}

static void qti_fdt_add_rtl_system_dmac(QemuToIoSystemState *s)
{
    MachineState *ms = MACHINE(s);
    void *fdt = ms->fdt;
    const IoManifestEntry *dmac = qti_manifest_entry(
        IO_MANIFEST_DEVICE_DWC_DMAC);
    g_autofree char *name = g_strdup_printf("/soc/io-system-dmac@%"PRIx64,
                                            dmac->base);

    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible",
                            QTI_RTL_SYSTEM_DMAC_COMPAT);
    qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                 2, dmac->base,
                                 2, dmac->size);
    qemu_fdt_setprop_string(fdt, name, "status", "okay");
}

static void qti_fdt_add_pcie(QemuToIoSystemState *s,
                             uint32_t aplic_s_phandle,
                             uint32_t imsic_s_phandle)
{
    MachineState *ms = MACHINE(s);
    void *fdt = ms->fdt;
    const IoManifestEntry *dbi = qti_manifest_entry(
        IO_MANIFEST_DEVICE_DWC_DBI);
    const IoManifestEntry *bar = qti_manifest_entry(
        IO_MANIFEST_DEVICE_PCIE_BAR);
    const IoManifestEntry *ecam = qti_manifest_entry(
        IO_MANIFEST_DEVICE_PCIE_ECAM);
    const uint64_t mem32_cpu_base = bar->base;
    const uint64_t mem64_cpu_base = bar->base + QTI_PCIE_WINDOW_SIZE;
    g_autofree char *name = g_strdup_printf("/soc/pci@%"PRIx64, dbi->base);
    static const char *const reg_names[] = { "dbi", "config" };
    static const char *const interrupt_names[] = { "msi", "hp" };

    if (bar->size < QTI_PCIE_WINDOW_SIZE * 2) {
        error_report("pcie-bar manifest window is too small for PCIe ranges");
        exit(1);
    }

    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "snps,dw-pcie");
    qemu_fdt_setprop_cell(fdt, name, "#address-cells", 3);
    qemu_fdt_setprop_cell(fdt, name, "#size-cells", 2);
    qemu_fdt_setprop_cell(fdt, name, "#interrupt-cells", 1);
    qemu_fdt_setprop_string(fdt, name, "device_type", "pci");
    qemu_fdt_setprop_cells(fdt, name, "bus-range", 0, 0xff);
    qemu_fdt_setprop_cell(fdt, name, "linux,pci-domain", 0);
    qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                 2, dbi->base, 2, dbi->size,
                                 2, ecam->base, 2, ecam->size);
    qemu_fdt_setprop_string_array(fdt, name, "reg-names",
                                  (char **)&reg_names,
                                  ARRAY_SIZE(reg_names));
    qemu_fdt_setprop_cell(fdt, name, "num-ib-windows", 1);
    qemu_fdt_setprop_sized_cells(fdt, name, "ranges",
                                 1, FDT_PCI_RANGE_MMIO,
                                 2, QTI_PCIE_MEM32_BUS_BASE,
                                 2, mem32_cpu_base,
                                 2, QTI_PCIE_WINDOW_SIZE,
                                 1, FDT_PCI_RANGE_MMIO_64BIT,
                                 2, QTI_PCIE_MEM64_BUS_BASE,
                                 2, mem64_cpu_base,
                                 2, QTI_PCIE_WINDOW_SIZE);
    qemu_fdt_setprop_cell(fdt, name, "interrupt-parent",
                          aplic_s_phandle);
    qemu_fdt_setprop_cell(fdt, name, "msi-parent", imsic_s_phandle);
    qemu_fdt_setprop_cells(fdt, name, "interrupts",
                           QTI_PCIE_MSI_IRQ, FDT_IRQ_TYPE_LEVEL_HIGH,
                           QTI_PCIE_HP_IRQ, FDT_IRQ_TYPE_LEVEL_HIGH);
    qemu_fdt_setprop_string_array(fdt, name, "interrupt-names",
                                  (char **)&interrupt_names,
                                  ARRAY_SIZE(interrupt_names));
    qemu_fdt_setprop_cell(fdt, name, "num-lanes", 1);
    qemu_fdt_setprop_string(fdt, name, "status", "okay");
    qti_fdt_add_pcie_irq_map(fdt, name, aplic_s_phandle);
}

static void qti_create_fdt(QemuToIoSystemState *s)
{
    MachineState *ms = MACHINE(s);
    bool has_io_system_aplic =
        qti_backend_uses_cmodel_devices(s->io_system_backend_kind) ||
        (qti_backend_uses_rtl_system(s->io_system_backend_kind) &&
         s->my_virtio_blk);
    uint32_t phandle = 1;
    uint32_t imsic_m_phandle;
    uint32_t imsic_s_phandle;
    uint32_t aplic_m_phandle = 0;
    uint32_t aplic_s_phandle = 0;
    g_autofree uint32_t *intc_phandles = g_new0(uint32_t, ms->smp.cpus);
    g_autofree uint32_t *clint_cells = g_new0(uint32_t, ms->smp.cpus * 4);
    g_autofree uint32_t *imsic_m_cells = g_new0(uint32_t, ms->smp.cpus * 2);
    g_autofree uint32_t *imsic_s_cells = g_new0(uint32_t, ms->smp.cpus * 2);
    void *fdt;
    static const char * const cpu_compat[2] = {
        "bosc,kmh-v2", "riscv"
    };
    static const char * const soc_compat[2] = {
        "bosc,kmh-v2-soc", "simple-bus"
    };

    fdt = ms->fdt = create_device_tree(&s->fdt_size);
    if (!fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    qemu_fdt_setprop_string(fdt, "/", "model",
                            "QEMU to io-system Xiangshan machine");
    qemu_fdt_setprop_string(fdt, "/", "compatible",
                            "bosc,qemu-to-iosystem");
    qemu_fdt_setprop_cell(fdt, "/", "#address-cells", 2);
    qemu_fdt_setprop_cell(fdt, "/", "#size-cells", 2);

    qemu_fdt_add_subnode(fdt, "/aliases");
    qemu_fdt_setprop_string(fdt, "/aliases", "serial0",
                            "/soc/serial@310b0000");

    qemu_fdt_add_subnode(fdt, "/chosen");
    qemu_fdt_setprop_string(fdt, "/chosen", "stdout-path",
                            "/soc/serial@310b0000:115200n8");
    qemu_fdt_setprop_string(fdt, "/chosen", "bootargs",
                            ms->kernel_cmdline && *ms->kernel_cmdline ?
                            ms->kernel_cmdline :
                            "console=ttyS0,115200 earlycon=sbi loglevel=8");

    qemu_fdt_add_subnode(fdt, "/cpus");
    qemu_fdt_setprop_cell(fdt, "/cpus", "#address-cells", 1);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#size-cells", 0);
    qemu_fdt_setprop_cell(fdt, "/cpus", "timebase-frequency",
                          QTI_CLINT_TIMEBASE_FREQ);

    for (int cpu = ms->smp.cpus - 1; cpu >= 0; cpu--) {
        RISCVCPU *cpu_ptr = &s->cpus.harts[cpu];
        g_autofree char *cpu_name = g_strdup_printf("/cpus/cpu@%x", cpu);
        g_autofree char *intc_name = g_strdup_printf(
            "/cpus/cpu@%x/interrupt-controller", cpu);
        uint32_t cpu_phandle = phandle++;

        qemu_fdt_add_subnode(fdt, cpu_name);
        qemu_fdt_setprop_string_array(fdt, cpu_name, "compatible",
                                      (char **)&cpu_compat,
                                      ARRAY_SIZE(cpu_compat));
        qemu_fdt_setprop_string(fdt, cpu_name, "device_type", "cpu");
        qemu_fdt_setprop_string(fdt, cpu_name, "status", "okay");
        qemu_fdt_setprop_cell(fdt, cpu_name, "reg", cpu);
        qemu_fdt_setprop_string(fdt, cpu_name, "mmu-type", "riscv,sv48");
        qemu_fdt_setprop_cell(fdt, cpu_name, "d-cache-block-size", 64);
        qemu_fdt_setprop_cell(fdt, cpu_name, "i-cache-block-size", 64);
        qemu_fdt_setprop_cell(fdt, cpu_name, "riscv,cbom-block-size", 64);
        qemu_fdt_setprop_cell(fdt, cpu_name, "riscv,cboz-block-size", 64);
        riscv_isa_write_fdt(cpu_ptr, fdt, cpu_name);
        qemu_fdt_setprop_cell(fdt, cpu_name, "phandle", cpu_phandle);

        intc_phandles[cpu] = phandle++;
        qemu_fdt_add_subnode(fdt, intc_name);
        qemu_fdt_setprop_string(fdt, intc_name, "compatible",
                                "riscv,cpu-intc");
        qemu_fdt_setprop(fdt, intc_name, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop_cell(fdt, intc_name, "#interrupt-cells", 1);
        qemu_fdt_setprop_cell(fdt, intc_name, "phandle",
                              intc_phandles[cpu]);
    }

    qti_fdt_add_memory(s);

    qemu_fdt_add_subnode(fdt, "/soc");
    qemu_fdt_setprop(fdt, "/soc", "ranges", NULL, 0);
    qemu_fdt_setprop_string_array(fdt, "/soc", "compatible",
                                  (char **)&soc_compat,
                                  ARRAY_SIZE(soc_compat));
    qemu_fdt_setprop_cell(fdt, "/soc", "#address-cells", 2);
    qemu_fdt_setprop_cell(fdt, "/soc", "#size-cells", 2);

    for (int cpu = 0; cpu < ms->smp.cpus; cpu++) {
        clint_cells[cpu * 4 + 0] = cpu_to_be32(intc_phandles[cpu]);
        clint_cells[cpu * 4 + 1] = cpu_to_be32(IRQ_M_SOFT);
        clint_cells[cpu * 4 + 2] = cpu_to_be32(intc_phandles[cpu]);
        clint_cells[cpu * 4 + 3] = cpu_to_be32(IRQ_M_TIMER);
        imsic_m_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
        imsic_m_cells[cpu * 2 + 1] = cpu_to_be32(IRQ_M_EXT);
        imsic_s_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
        imsic_s_cells[cpu * 2 + 1] = cpu_to_be32(IRQ_S_EXT);
    }

    {
        g_autofree char *name = g_strdup_printf("/soc/clint@%"HWADDR_PRIx,
            qti_memmap[QTI_CLINT].base);
        static const char * const compat[2] = {
            "sifive,clint0", "riscv,clint0"
        };

        qemu_fdt_add_subnode(fdt, name);
        qemu_fdt_setprop_string_array(fdt, name, "compatible",
                                      (char **)&compat, ARRAY_SIZE(compat));
        qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                     2, qti_memmap[QTI_CLINT].base,
                                     2, qti_memmap[QTI_CLINT].size);
        qemu_fdt_setprop(fdt, name, "interrupts-extended", clint_cells,
                         ms->smp.cpus * sizeof(uint32_t) * 4);
    }

    imsic_m_phandle = phandle++;
    imsic_s_phandle = phandle++;
    {
        g_autofree char *name = g_strdup_printf("/soc/imsics@%"HWADDR_PRIx,
            qti_memmap[QTI_IMSIC_M].base);

        qemu_fdt_add_subnode(fdt, name);
        qemu_fdt_setprop_string(fdt, name, "compatible", "riscv,imsics");
        qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                     2, qti_memmap[QTI_IMSIC_M].base,
                                     2, qti_memmap[QTI_IMSIC_M].size);
        qemu_fdt_setprop(fdt, name, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop(fdt, name, "msi-controller", NULL, 0);
        qemu_fdt_setprop_cell(fdt, name, "#interrupt-cells", 0);
        qemu_fdt_setprop_cell(fdt, name, "riscv,num-ids",
                              QTI_IMSIC_NUM_IDS);
        qemu_fdt_setprop(fdt, name, "interrupts-extended", imsic_m_cells,
                         ms->smp.cpus * sizeof(uint32_t) * 2);
        qemu_fdt_setprop_cell(fdt, name, "phandle", imsic_m_phandle);
    }
    {
        g_autofree char *name = g_strdup_printf("/soc/imsics@%"HWADDR_PRIx,
            qti_memmap[QTI_IMSIC_S].base);

        qemu_fdt_add_subnode(fdt, name);
        qemu_fdt_setprop_string(fdt, name, "compatible", "riscv,imsics");
        qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                     2, qti_memmap[QTI_IMSIC_S].base,
                                     2, qti_memmap[QTI_IMSIC_S].size);
        qemu_fdt_setprop(fdt, name, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop(fdt, name, "msi-controller", NULL, 0);
        qemu_fdt_setprop_cell(fdt, name, "#interrupt-cells", 0);
        qemu_fdt_setprop_cell(fdt, name, "riscv,num-ids",
                              QTI_IMSIC_NUM_IDS);
        qemu_fdt_setprop_cell(fdt, name, "riscv,guest-index-bits",
                              QTI_IMSIC_GUEST_BITS);
        qemu_fdt_setprop(fdt, name, "interrupts-extended", imsic_s_cells,
                         ms->smp.cpus * sizeof(uint32_t) * 2);
        qemu_fdt_setprop_cell(fdt, name, "phandle", imsic_s_phandle);
    }

    if (has_io_system_aplic) {
        aplic_s_phandle = phandle++;
        aplic_m_phandle = phandle++;
        {
            const IoManifestEntry *aplic_s = qti_manifest_entry(
                IO_MANIFEST_DEVICE_APLIC_S);
            g_autofree char *name = g_strdup_printf("/soc/aplic@%"PRIx64,
                                                    aplic_s->base);

            qemu_fdt_add_subnode(fdt, name);
            qemu_fdt_setprop_string(fdt, name, "compatible", "riscv,aplic");
            qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                         2, aplic_s->base, 2, aplic_s->size);
            qemu_fdt_setprop(fdt, name, "interrupt-controller", NULL, 0);
            qemu_fdt_setprop_cell(fdt, name, "#interrupt-cells", 2);
            qemu_fdt_setprop_cell(fdt, name, "riscv,num-sources",
                                  QTI_APLIC_NUM_SOURCES);
            qemu_fdt_setprop_cell(fdt, name, "msi-parent", imsic_s_phandle);
            qemu_fdt_setprop_cell(fdt, name, "phandle", aplic_s_phandle);
        }
        {
            const IoManifestEntry *aplic_m = qti_manifest_entry(
                IO_MANIFEST_DEVICE_APLIC_M);
            g_autofree char *name = g_strdup_printf("/soc/aplic@%"PRIx64,
                                                    aplic_m->base);

            qemu_fdt_add_subnode(fdt, name);
            qemu_fdt_setprop_string(fdt, name, "compatible", "riscv,aplic");
            qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                         2, aplic_m->base, 2, aplic_m->size);
            qemu_fdt_setprop(fdt, name, "interrupt-controller", NULL, 0);
            qemu_fdt_setprop_cell(fdt, name, "#interrupt-cells", 2);
            qemu_fdt_setprop_cell(fdt, name, "riscv,num-sources",
                                  QTI_APLIC_NUM_SOURCES);
            qemu_fdt_setprop_cell(fdt, name, "msi-parent", imsic_m_phandle);
            qemu_fdt_setprop_cell(fdt, name, "riscv,children", aplic_s_phandle);
            qemu_fdt_setprop_cells(fdt, name, "riscv,delegate",
                                   aplic_s_phandle, 1, QTI_APLIC_NUM_SOURCES);
            qemu_fdt_setprop_cell(fdt, name, "phandle", aplic_m_phandle);
        }
    }

    if (qti_backend_uses_cmodel_devices(s->io_system_backend_kind)) {
        qti_fdt_add_dmac(s, aplic_s_phandle, &phandle);
    } else {
        qti_fdt_add_rtl_system_dmac(s);
    }

    {
        g_autofree char *name = g_strdup_printf("/soc/serial@%"HWADDR_PRIx,
            qti_memmap[QTI_UART0].base);

        qemu_fdt_add_subnode(fdt, name);
        qemu_fdt_setprop_string(fdt, name, "compatible", "ns16550a");
        qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                     2, qti_memmap[QTI_UART0].base,
                                     2, qti_memmap[QTI_UART0].size);
        qemu_fdt_setprop_cell(fdt, name, "reg-shift", 2);
        qemu_fdt_setprop_cell(fdt, name, "reg-io-width", 4);
        qemu_fdt_setprop_cell(fdt, name, "clock-frequency", QTI_UART0_CLOCK);
        qemu_fdt_setprop_cell(fdt, name, "current-speed", 115200);
        qemu_fdt_setprop_string(fdt, name, "status", "okay");
    }

    if (s->my_virtio_blk && has_io_system_aplic) {
        qti_fdt_add_virtio_blk(s, aplic_s_phandle);
    }

    if (s->dw_pcie &&
        qti_backend_uses_cmodel_devices(s->io_system_backend_kind)) {
        qti_fdt_add_pcie(s, aplic_s_phandle, imsic_s_phandle);
    }
}

static void qti_machine_init(MachineState *machine)
{
    QemuToIoSystemState *s = QEMU_TO_IOSYSTEM_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    hwaddr start_addr = qti_memmap[QTI_DRAM].base;
    target_ulong firmware_end_addr;
    target_ulong kernel_start_addr;
    uint64_t fdt_load_addr = 0;
    uint64_t kernel_entry = 0;
    RISCVBootInfo boot_info;
    bool generate_dtb;
    int fdt_size;

    if (kvm_enabled()) {
        error_report("qemu_to_iosystem currently supports TCG only");
        exit(1);
    }

    if (!s->io_system) {
        qti_create_io_system(s);
    }

    qdev_prop_set_uint32(DEVICE(&s->cpus), "num-harts", machine->smp.cpus);
    qdev_prop_set_uint32(DEVICE(&s->cpus), "hartid-base", 0);
    qdev_prop_set_string(DEVICE(&s->cpus), "cpu-type", machine->cpu_type);
    sysbus_realize(SYS_BUS_DEVICE(&s->cpus), &error_fatal);

    qti_create_imsics(machine->smp.cpus);

    if (s->dw_pcie &&
        !qti_backend_uses_cmodel_devices(s->io_system_backend_kind)) {
        error_report("io-system backend %s does not support dw-pcie in qemu_to_iosystem",
                     qti_backend_kind_name(s->io_system_backend_kind));
        exit(1);
    }

    if (s->my_virtio_blk &&
        !qti_backend_uses_cmodel_devices(s->io_system_backend_kind) &&
        !qti_backend_uses_rtl_system(s->io_system_backend_kind)) {
        error_report("io-system backend %s does not support my-virtio-blk in qemu_to_iosystem",
                     qti_backend_kind_name(s->io_system_backend_kind));
        exit(1);
    }

    if (s->my_virtio_blk &&
        qti_backend_uses_cmodel_devices(s->io_system_backend_kind)) {
        qti_create_my_virtio_blk(s);
    }
    if (s->dw_pcie) {
        qti_create_pcie(s);
    }

    serial_mm_init(system_memory, qti_memmap[QTI_UART0].base, 2, NULL,
                   115200, serial_hd(0), DEVICE_LITTLE_ENDIAN);

    riscv_aclint_swi_create(qti_memmap[QTI_CLINT].base,
                            0, machine->smp.cpus, false);
    riscv_aclint_mtimer_create(qti_memmap[QTI_CLINT].base +
                               RISCV_ACLINT_SWI_SIZE,
                               RISCV_ACLINT_DEFAULT_MTIMER_SIZE,
                               0, machine->smp.cpus,
                               RISCV_ACLINT_DEFAULT_MTIMECMP,
                               RISCV_ACLINT_DEFAULT_MTIME,
                               QTI_CLINT_TIMEBASE_FREQ, true);

    memory_region_init_rom(&s->rom, NULL, "qemu-to-iosystem.rom",
                           qti_memmap[QTI_ROM].size, &error_fatal);
    memory_region_add_subregion(system_memory, qti_memmap[QTI_ROM].base,
                                &s->rom);

    memory_region_init_ram(&s->sram, NULL,
                           "qemu-to-iosystem.sram",
                           qti_memmap[QTI_SRAM].size, &error_fatal);
    memory_region_add_subregion(system_memory, qti_memmap[QTI_SRAM].base,
                                &s->sram);

    memory_region_init_rom(&s->flash, NULL,
                           "qemu-to-iosystem.flash",
                           qti_memmap[QTI_FLASH].size, &error_fatal);
    memory_region_add_subregion(system_memory, qti_memmap[QTI_FLASH].base,
                                &s->flash);

    memory_region_add_subregion(system_memory, qti_memmap[QTI_DRAM].base,
                                machine->ram);

    if (qti_backend_uses_cmodel_devices(s->io_system_backend_kind)) {
        qti_create_io_bridge_windows(s, system_memory);
    } else {
        qti_create_rtl_system_bridge_window(s, system_memory);
    }

    generate_dtb = qti_should_generate_dtb(s);
    if (machine->dtb && s->generated_dtb == ON_OFF_AUTO_ON) {
        error_report("-dtb cannot be combined with generated-dtb=on");
        exit(1);
    }
    if (generate_dtb) {
        qti_create_fdt(s);
    } else if (machine->dtb) {
        machine->fdt = load_device_tree(machine->dtb, &fdt_size);
        if (!machine->fdt) {
            error_report("load_device_tree() failed");
            exit(1);
        }
        s->fdt_size = fdt_size;
    }

    firmware_end_addr = riscv_find_and_load_firmware(machine, QTI_BIOS_BIN,
                                                     &start_addr, NULL);

    riscv_boot_info_init(&boot_info, &s->cpus);
    if (machine->kernel_filename) {
        kernel_start_addr = riscv_calc_kernel_start_addr(&boot_info,
                                                         firmware_end_addr);
        riscv_load_kernel(machine, &boot_info, kernel_start_addr, false, NULL);
        kernel_entry = boot_info.image_low_addr;

        if (machine->firmware && !strcmp(machine->firmware, "none")) {
            start_addr = kernel_entry;
        }
    }

    if (machine->fdt) {
        if (s->fw_jump_fdt_addr) {
            fdt_load_addr = s->fw_jump_fdt_addr;
        } else if (machine->kernel_filename) {
            fdt_load_addr = riscv_compute_fdt_addr(qti_memmap[QTI_DRAM].base,
                                                   qti_memmap[QTI_DRAM].size,
                                                   machine, &boot_info);
        } else {
            fdt_load_addr = s->fw_jump_fdt_addr;
        }
        riscv_load_fdt(fdt_load_addr, machine->fdt);
    }

    riscv_setup_rom_reset_vec(machine, &s->cpus, start_addr,
                              qti_memmap[QTI_ROM].base,
                              qti_memmap[QTI_ROM].size,
                              kernel_entry, fdt_load_addr);
}

static void qti_get_generated_dtb(Object *obj, Visitor *v,
                                  const char *name, void *opaque,
                                  Error **errp)
{
    QemuToIoSystemState *s = QEMU_TO_IOSYSTEM_MACHINE(obj);
    OnOffAuto generated_dtb = s->generated_dtb;

    visit_type_OnOffAuto(v, name, &generated_dtb, errp);
}

static void qti_set_generated_dtb(Object *obj, Visitor *v,
                                  const char *name, void *opaque,
                                  Error **errp)
{
    QemuToIoSystemState *s = QEMU_TO_IOSYSTEM_MACHINE(obj);

    visit_type_OnOffAuto(v, name, &s->generated_dtb, errp);
}

static bool qti_get_my_virtio_blk(Object *obj, Error **errp)
{
    QemuToIoSystemState *s = QEMU_TO_IOSYSTEM_MACHINE(obj);

    return s->my_virtio_blk;
}

static void qti_set_my_virtio_blk(Object *obj, bool value, Error **errp)
{
    QemuToIoSystemState *s = QEMU_TO_IOSYSTEM_MACHINE(obj);

    s->my_virtio_blk = value;
}

static bool qti_get_dw_pcie(Object *obj, Error **errp)
{
    QemuToIoSystemState *s = QEMU_TO_IOSYSTEM_MACHINE(obj);

    return s->dw_pcie;
}

static void qti_set_dw_pcie(Object *obj, bool value, Error **errp)
{
    QemuToIoSystemState *s = QEMU_TO_IOSYSTEM_MACHINE(obj);

    s->dw_pcie = value;
}

static char *qti_get_my_virtio_blk_image(Object *obj, Error **errp)
{
    QemuToIoSystemState *s = QEMU_TO_IOSYSTEM_MACHINE(obj);

    return g_strdup(s->my_virtio_blk_image ? s->my_virtio_blk_image : "");
}

static void qti_set_my_virtio_blk_image(Object *obj, const char *value,
                                        Error **errp)
{
    QemuToIoSystemState *s = QEMU_TO_IOSYSTEM_MACHINE(obj);

    g_free(s->my_virtio_blk_image);
    s->my_virtio_blk_image = g_strdup(value && *value ? value : "disk.img");
}

static char *qti_get_io_system_trace_file(Object *obj, Error **errp)
{
    QemuToIoSystemState *s = QEMU_TO_IOSYSTEM_MACHINE(obj);

    return g_strdup(s->io_system_trace_file ? s->io_system_trace_file : "");
}

static void qti_set_io_system_trace_file(Object *obj, const char *value,
                                         Error **errp)
{
    QemuToIoSystemState *s = QEMU_TO_IOSYSTEM_MACHINE(obj);

    g_free(s->io_system_trace_file);
    s->io_system_trace_file = g_strdup(value && *value ? value : "");
}

static char *qti_get_io_system_backend(Object *obj, Error **errp)
{
    QemuToIoSystemState *s = QEMU_TO_IOSYSTEM_MACHINE(obj);

    return g_strdup(s->io_system_backend ? s->io_system_backend : "cmodel");
}

static void qti_set_io_system_backend(Object *obj, const char *value,
                                      Error **errp)
{
    QemuToIoSystemState *s = QEMU_TO_IOSYSTEM_MACHINE(obj);

    g_free(s->io_system_backend);
    s->io_system_backend = g_strdup(value && *value ? value : "cmodel");
    s->io_system_backend_kind = qti_parse_backend_kind(s->io_system_backend);
}

static char *qti_get_io_system_backend_lib(Object *obj, Error **errp)
{
    QemuToIoSystemState *s = QEMU_TO_IOSYSTEM_MACHINE(obj);

    return g_strdup(s->io_system_backend_lib ? s->io_system_backend_lib : "");
}

static void qti_set_io_system_backend_lib(Object *obj, const char *value,
                                          Error **errp)
{
    QemuToIoSystemState *s = QEMU_TO_IOSYSTEM_MACHINE(obj);

    g_free(s->io_system_backend_lib);
    s->io_system_backend_lib = g_strdup(value && *value ? value : "");
}

static char *qti_get_io_system_vcs_libdir(Object *obj, Error **errp)
{
    QemuToIoSystemState *s = QEMU_TO_IOSYSTEM_MACHINE(obj);

    return g_strdup(s->io_system_vcs_libdir ? s->io_system_vcs_libdir : "");
}

static void qti_set_io_system_vcs_libdir(Object *obj, const char *value,
                                         Error **errp)
{
    QemuToIoSystemState *s = QEMU_TO_IOSYSTEM_MACHINE(obj);

    g_free(s->io_system_vcs_libdir);
    s->io_system_vcs_libdir = g_strdup(value && *value ? value : "");
}

static void qti_get_uint64(Object *obj, Visitor *v, const char *name,
                           void *opaque, Error **errp)
{
    QemuToIoSystemState *s = QEMU_TO_IOSYSTEM_MACHINE(obj);
    uint64_t value = *(uint64_t *)((char *)s + (uintptr_t)opaque);

    visit_type_uint64(v, name, &value, errp);
}

static void qti_set_uint64(Object *obj, Visitor *v, const char *name,
                           void *opaque, Error **errp)
{
    QemuToIoSystemState *s = QEMU_TO_IOSYSTEM_MACHINE(obj);

    visit_type_uint64(v, name,
                      (uint64_t *)((char *)s + (uintptr_t)opaque), errp);
}

#define QTI_UINT64_PROP(_name, _field, _desc) \
    do { \
        object_class_property_add(klass, _name, "uint64", \
                                  qti_get_uint64, qti_set_uint64, NULL, \
                                  (void *)offsetof(QemuToIoSystemState, \
                                                   _field)); \
        object_class_property_set_description(klass, _name, _desc); \
    } while (0)

static void qti_machine_instance_init(Object *obj)
{
    QemuToIoSystemState *s = QEMU_TO_IOSYSTEM_MACHINE(obj);

    object_initialize_child(obj, "cpus", &s->cpus, TYPE_RISCV_HART_ARRAY);
    s->generated_dtb = ON_OFF_AUTO_AUTO;
    s->my_virtio_blk = false;
    s->dw_pcie = false;
    s->io_system_backend_kind = IO_SYSTEM_BACKEND_CMODEL;
    s->io_system_backend = g_strdup("cmodel");
    s->io_system_backend_lib = g_strdup("");
    s->io_system_vcs_libdir = g_strdup("");
    s->my_virtio_blk_image = g_strdup("disk.img");
    s->io_system_trace_file = g_strdup("");
    s->fw_jump_fdt_addr = QTI_FW_JUMP_FDT_ADDR;
}

static void qti_machine_instance_finalize(Object *obj)
{
    QemuToIoSystemState *s = QEMU_TO_IOSYSTEM_MACHINE(obj);

    qti_trace_dump_new(s);
    if (s->io_system_trace_fp) {
        fclose(s->io_system_trace_fp);
    }
    if (s->dmac) {
        io_dwc_dmac_destroy(s->dmac);
    }
    if (s->my_virtio_blk_dev) {
        io_my_virtio_blk_destroy(s->my_virtio_blk_dev);
    }
    if (s->aplic_s) {
        io_aplic_destroy(s->aplic_s);
    }
    if (s->aplic_m) {
        io_aplic_destroy(s->aplic_m);
    }
    io_system_destroy(s->io_system);
    g_free(s->io_system_backend);
    g_free(s->io_system_backend_lib);
    g_free(s->io_system_vcs_libdir);
    g_free(s->io_system_trace_file);
    g_free(s->my_virtio_blk_image);
}

static void qti_machine_class_init(ObjectClass *klass, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(klass);
    static const char *const valid_cpu_types[] = {
        TYPE_RISCV_CPU_XIANGSHAN_KMH,
        NULL
    };

    mc->desc = "RISC-V Xiangshan machine connected to io-system C-model";
    mc->init = qti_machine_init;
    mc->max_cpus = QEMU_TO_IOSYSTEM_MAX_CPUS;
    mc->default_cpu_type = TYPE_RISCV_CPU_XIANGSHAN_KMH;
    mc->valid_cpu_types = valid_cpu_types;
    mc->default_ram_size = 1 * GiB;
    mc->default_ram_id = "qemu-to-iosystem.ram";

    object_class_property_add(klass, "generated-dtb", "OnOffAuto",
                              qti_get_generated_dtb,
                              qti_set_generated_dtb, NULL, NULL);
    object_class_property_set_description(klass, "generated-dtb",
                                          "Use QEMU-generated device tree");

    object_class_property_add_str(klass, "io-system-backend",
                                  qti_get_io_system_backend,
                                  qti_set_io_system_backend);
    object_class_property_set_description(klass, "io-system-backend",
                                          "Select io-system backend: cmodel, rtl-template, or rtl-system");

    object_class_property_add_str(klass, "io-system-backend-lib",
                                  qti_get_io_system_backend_lib,
                                  qti_set_io_system_backend_lib);
    object_class_property_set_description(klass, "io-system-backend-lib",
                                          "Path to the io-system RTL backend API shared library");

    object_class_property_add_str(klass, "io-system-vcs-libdir",
                                  qti_get_io_system_vcs_libdir,
                                  qti_set_io_system_vcs_libdir);
    object_class_property_set_description(klass, "io-system-vcs-libdir",
                                          "Directory containing VCS runtime libraries for the RTL backend");

    object_class_property_add_bool(klass, "my-virtio-blk",
                                   qti_get_my_virtio_blk,
                                   qti_set_my_virtio_blk);
    object_class_property_set_description(klass, "my-virtio-blk",
                                          "Enable my-virtio block device");

    object_class_property_add_bool(klass, "dw-pcie",
                                   qti_get_dw_pcie,
                                   qti_set_dw_pcie);
    object_class_property_set_description(klass, "dw-pcie",
                                          "Enable QEMU DesignWare PCIe host controller");

    object_class_property_add_str(klass, "my-virtio-blk-image",
                                  qti_get_my_virtio_blk_image,
                                  qti_set_my_virtio_blk_image);
    object_class_property_set_description(klass, "my-virtio-blk-image",
                                          "Disk image path for my-virtio block device");

    object_class_property_add_str(klass, "io-system-trace-file",
                                  qti_get_io_system_trace_file,
                                  qti_set_io_system_trace_file);
    object_class_property_set_description(klass, "io-system-trace-file",
                                          "Write io-system AXI beat trace to this file");

    QTI_UINT64_PROP("fw-jump-fdt-addr", fw_jump_fdt_addr,
                    "Generated DTB load address for fw_jump boot");
}

#undef QTI_UINT64_PROP

static const TypeInfo qti_machine_info = {
    .name = TYPE_QEMU_TO_IOSYSTEM_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(QemuToIoSystemState),
    .class_init = qti_machine_class_init,
    .instance_init = qti_machine_instance_init,
    .instance_finalize = qti_machine_instance_finalize,
};

static void qti_machine_register_types(void)
{
    type_register_static(&qti_machine_info);
}
type_init(qti_machine_register_types)
