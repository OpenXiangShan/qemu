/*
 * QEMU model of the BOSC Kunminghu multi-die SoC.
 *
 * Copyright (c) 2026 Beijing Institute of Open Source Chip (BOSC)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include CONFIG_DEVICES
#include "qemu/cutils.h"
#include "qemu/datadir.h"
#include "qemu/error-report.h"
#include "qemu/guest-random.h"
#include "qemu/host-utils.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qapi/qapi-visit-common.h"
#include "hw/boards.h"
#include "hw/dma/dw-axi-dmac.h"
#include "hw/loader.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/char/serial-mm.h"
#include "hw/core/cpu.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/intc/riscv_aplic.h"
#include "hw/intc/riscv_imsic.h"
#include "hw/misc/kmh_bosc_aplic_test.h"
#include "hw/misc/bosc-axi4-traffic.h"
#include "hw/misc/bosc-did-mapper.h"
#include "hw/misc/unimp.h"
#include "hw/pci-host/designware.h"
#include "hw/pci/pci.h"
#include "hw/pci/pcie_host.h"
#include "hw/or-irq.h"
#include "hw/riscv/boot.h"
#include "hw/riscv/kmh_bosc_soc.h"
#include "hw/riscv/kmh_bosc_irq.h"
#include "hw/riscv/kmh_bosc_checkpoint.h"
#include "../../third_party/kmh-portable-restore/native-abi.h"
#include "../../third_party/kmh-portable-restore/native-resources.inc"
#ifdef CONFIG_ACPI
#include "hw/riscv/kmh_bosc_acpi.h"
#endif
#include "hw/riscv/iommu.h"
#include "hw/riscv/riscv-iommu.h"
#include "hw/riscv/riscv_hart.h"
#include "system/address-spaces.h"
#include "system/device_tree.h"
#include "system/memory.h"
#include "system/reset.h"
#include "system/runstate.h"
#include "system/kvm.h"
#include "system/system.h"
#include "target/riscv/cpu.h"

#include <libfdt.h>

#define KMH_BOSC_MACHINE_TYPE             MACHINE_TYPE_NAME("kmh-bosc-soc")
#define TYPE_KMH_BOSC_PCIE_DMA            "kmh-bosc-pcie-dma"
#define TYPE_KMH_BOSC_PCIE_APP            "kmh-bosc-pcie-app"

#define KMH_BOSC_DIES                     4
#define KMH_BOSC_PCIE_PER_DIE             3
#define KMH_BOSC_APP_HARTS_PER_DIE        16
#define KMH_BOSC_TOTAL_APP_HARTS          (KMH_BOSC_DIES * KMH_BOSC_APP_HARTS_PER_DIE)
#define KMH_BOSC_APP_HART_MASK            ((1U << KMH_BOSC_APP_HARTS_PER_DIE) - 1U)
#define KMH_BOSC_MCU_HARTID_BASE          KMH_BOSC_TOTAL_APP_HARTS

#define KMH_BOSC_DIE_SHIFT                44
#define KMH_BOSC_DIE_MASK_BITS            0x0f

#define KMH_BOSC_DDR_NODE_BASE            0x0000000080000000ULL
/* Four fixed 32 GiB slots; -m supplies an equal-sized prefix of each slot. */
#define KMH_BOSC_DDR_NODE_STRIDE          (32 * GiB)
#define KMH_BOSC_DDR_MAX_SIZE             \
    (KMH_BOSC_DIES * KMH_BOSC_DDR_NODE_STRIDE)
#define KMH_BOSC_MCU_PAYLOAD_ADDR         KMH_BOSC_DDR_NODE_BASE
#define KMH_BOSC_MCU_SYNC_FLAG_ADDR       0x0000000088000000ULL
#define KMH_BOSC_IOMMU_PER_DIE            KMH_BOSC_PCIE_PER_DIE
#define KMH_BOSC_IOMMU_IRQS_PER_INST      4

#define KMH_BOSC_APLIC_NUM_SOURCES        KMH_BOSC_APLIC_TEST_NUM_IRQS
#define KMH_BOSC_IMSIC_NUM_IDS            255
#define KMH_BOSC_IMSIC_GUEST_INDEX_BITS   3
#define KMH_BOSC_IRQ_TYPE_LEVEL_HIGH      0x4
#define KMH_BOSC_PCIE_MEMMAP_COUNT         6

#define KMH_BOSC_FDT_PCI_ADDR_CELLS       3
#define KMH_BOSC_FDT_PCI_INT_CELLS        1
#define KMH_BOSC_FDT_APLIC_INT_CELLS      2
#define KMH_BOSC_FDT_MAX_INT_MAP_WIDTH    \
    (KMH_BOSC_FDT_PCI_ADDR_CELLS + KMH_BOSC_FDT_PCI_INT_CELLS + 1 + \
     KMH_BOSC_FDT_APLIC_INT_CELLS)

#define KMH_BOSC_PCIE_ECAM_SIZE           PCIE_MMCFG_SIZE_MAX
#define KMH_BOSC_PCIE_MEM_BUS_BASE        0x0000000100000000ULL
#define KMH_BOSC_PCIE_MEM32_BUS_BASE      0x0000000050000000ULL
#define KMH_BOSC_PCIE_ROOT_BUS_STRIDE     0x0000000004000000ULL
#define KMH_BOSC_PCIE_WINDOW_SIZE         0x0000000002000000ULL
#define KMH_BOSC_PCIE_PREFETCH_OFFSET     0x0000000020000000ULL
#define KMH_BOSC_UART_BAUD                115200
#define KMH_BOSC_UART_CLOCK               (16 * KMH_BOSC_UART_BAUD)
#define KMH_BOSC_IOMMU_MMIO_SIZE          0x1000
#define KMH_BOSC_AXI4_TRAFFIC_COUNT       KMH_BOSC_IOMMU_PER_DIE

#define KMH_BOSC_AUTO_TEST_TRIGGER_ADDR_DEFAULT \
                                             0x0000000090000000ULL
#define KMH_BOSC_AUTO_TEST_TRIGGER_SIZE    0x0000000000200000ULL
#define KMH_BOSC_AUTO_TEST_ROOTFS_ADDR_DEFAULT \
                                             0x00000003c0000000ULL
#define KMH_BOSC_AUTO_TEST_ROOTFS_SIZE     0x0000000020000000ULL
#define KMH_BOSC_AUTO_TEST_WORKLOAD_ADDR_DEFAULT \
                                             0x00000003e0000000ULL
#define KMH_BOSC_AUTO_TEST_WORKLOAD_SIZE   0x0000000080000000ULL
#define KMH_BOSC_ACPI_HANDOFF_ADDR_DEFAULT 0x0000000090200000ULL
#define KMH_BOSC_ACPI_HANDOFF_SIZE_DEFAULT 0x0000000000020000ULL
#define KMH_BOSC_AUTO_TEST_BOOTARGS        \
    "console=ttyS0,115200 earlycon loglevel=8 " \
    "drm.debug=0x2 amdgpu.cik=1 amdgpu.si=1 amdgpu.dpm=0 " \
    "pcie_aspm=off pcie_port_pm=off amdgpu.vm_update_mode=3 " \
    "root=/dev/sda1 rw task=0x0000000000000000 " \
    "guest_task=0x0000000000000000"

enum {
    KMH_BOSC_MCU_BOOTROM,
    KMH_BOSC_SRAM0,
    KMH_BOSC_SRAM1,
    KMH_BOSC_UART0,
    KMH_BOSC_WATCH_DOG,
    KMH_BOSC_TRACE,
    KMH_BOSC_SYS_CRG,
    KMH_BOSC_SYS_CTRL,
    KMH_BOSC_QSPI_FLASH_CTRL,
    KMH_BOSC_DMAC,
    KMH_BOSC_QSPI_FLASH_DATA,
    KMH_BOSC_CLINT,
    KMH_BOSC_DEBUG,
    KMH_BOSC_IMSIC_M,
    KMH_BOSC_IMSIC_S,
    KMH_BOSC_SYSCNT,
    KMH_BOSC_APLIC_M,
    KMH_BOSC_APLIC_S,
    KMH_BOSC_DDR0_CFG,
    KMH_BOSC_DDR1_CFG,
    KMH_BOSC_PCIE_PHY,
    KMH_BOSC_IOMMU_CFG1,
    KMH_BOSC_IOMMU,
    KMH_BOSC_IOMMU1,
    KMH_BOSC_IOMMU2,
};

static const MemMapEntry kmh_bosc_memmap[] = {
    [KMH_BOSC_MCU_BOOTROM] =    { 0x0000000001000000ULL, 1 * MiB },
    [KMH_BOSC_SRAM0] =          { 0x0000000001100000ULL, 15 * MiB },
    [KMH_BOSC_SRAM1] =          { 0x0000000002000000ULL, 16 * MiB },
    [KMH_BOSC_UART0] =          { 0x0000000004000000ULL, 0x10000 },
    [KMH_BOSC_WATCH_DOG] =      { 0x0000000004060000ULL, 0x1000 },
    [KMH_BOSC_TRACE] =          { 0x0000000004070000ULL, 0x1000 },
    [KMH_BOSC_SYS_CRG] =        { 0x0000000004080000ULL, 0x10000 },
    [KMH_BOSC_SYS_CTRL] =       { 0x0000000004090000ULL, 0x10000 },
    [KMH_BOSC_QSPI_FLASH_CTRL] = { 0x00000000040b0000ULL, 0x10000 },
    [KMH_BOSC_DMAC] =           { 0x000000000b000000ULL,
                                  DW_AXI_DMAC_MMIO_SIZE },
    [KMH_BOSC_QSPI_FLASH_DATA] = { 0x0000000010000000ULL, 128 * MiB },
    [KMH_BOSC_CLINT] =          { 0x0000000019000000ULL, 0x10000 },
    [KMH_BOSC_DEBUG] =          { 0x000000001b000000ULL, 1 * MiB },
    [KMH_BOSC_IMSIC_M] =        { 0x000000001c000000ULL,
                                  KMH_BOSC_APP_HARTS_PER_DIE *
                                  IMSIC_HART_SIZE(0) },
    [KMH_BOSC_IMSIC_S] =        { 0x000000001d000000ULL,
                                  KMH_BOSC_APP_HARTS_PER_DIE *
                                  IMSIC_HART_SIZE(KMH_BOSC_IMSIC_GUEST_INDEX_BITS) },
    [KMH_BOSC_SYSCNT] =         { 0x000000001e000000ULL, 0x20000 },
    [KMH_BOSC_APLIC_M] =        { 0x000000001e020000ULL, 0x4000 },
    [KMH_BOSC_APLIC_S] =        { 0x000000001e024000ULL, 0x4000 },
    [KMH_BOSC_DDR0_CFG] =       { 0x0000000028000000ULL, 64 * MiB },
    [KMH_BOSC_DDR1_CFG] =       { 0x000000002c000000ULL, 64 * MiB },
    [KMH_BOSC_PCIE_PHY] =       { 0x0000004f01800000ULL, 2 * MiB },
    [KMH_BOSC_IOMMU_CFG1] =     { 0x0000004f80000000ULL, 2 * MiB },
    [KMH_BOSC_IOMMU] =          { 0x0000004f80200000ULL,
                                  KMH_BOSC_IOMMU_MMIO_SIZE },
    [KMH_BOSC_IOMMU1] =         { 0x0000004f80210000ULL, 0x10000 },
    [KMH_BOSC_IOMMU2] =         { 0x0000004f80220000ULL, 0x10000 },
};

static const hwaddr kmh_bosc_iommu_reg_base[KMH_BOSC_IOMMU_PER_DIE] = {
    0x0000004f80200000ULL,
    0x0000004f80210000ULL,
    0x0000004f80220000ULL,
};

static const hwaddr kmh_bosc_axi4_traffic_base[KMH_BOSC_AXI4_TRAFFIC_COUNT] = {
    0x0000004f80201000ULL,
    0x0000004f80211000ULL,
    0x0000004f80221000ULL,
};

typedef struct KmhBoscPciePortMemMap {
    MemMapEntry iopmp;
    MemMapEntry dbi;
    MemMapEntry app;
    MemMapEntry mctp;
    MemMapEntry ecam;
    MemMapEntry mem;
} KmhBoscPciePortMemMap;

static const KmhBoscPciePortMemMap kmh_bosc_pcie_memmap[] = {
    [0] = {
        .iopmp = { 0x000000055e000000ULL, 0x10000 },
        .dbi =   { 0x0000004f00000000ULL, 4 * MiB },
        .app =   { 0x0000004f00400000ULL, 1 * MiB },
        .mctp =  { 0x0000004f00500000ULL, 0x10000 },
        .ecam =  { 0x0000004800000000ULL, KMH_BOSC_PCIE_ECAM_SIZE },
        .mem =   { 0x0000048000000000ULL, 384 * GiB },
    },
    [1] = {
        .iopmp = { 0x000000055e010000ULL, 0x10000 },
        .dbi =   { 0x0000004f00800000ULL, 4 * MiB },
        .app =   { 0x0000004f00c00000ULL, 1 * MiB },
        .mctp =  { 0x0000004f00d00000ULL, 0x10000 },
        .ecam =  { 0x0000004900000000ULL, KMH_BOSC_PCIE_ECAM_SIZE },
        .mem =   { 0x000004e000000000ULL, 64 * GiB },
    },
    [2] = {
        .iopmp = { 0x000000055e020000ULL, 0x10000 },
        .dbi =   { 0x0000004f01000000ULL, 4 * MiB },
        .app =   { 0x0000004f01400000ULL, 1 * MiB },
        .mctp =  { 0x0000004f01500000ULL, 0x10000 },
        .ecam =  { 0x0000004a00000000ULL, KMH_BOSC_PCIE_ECAM_SIZE },
        .mem =   { 0x000004f000000000ULL, 64 * GiB },
    },
};

/* SoC APP interrupt status: bank n is latched at bit 8 + n. */
#define KMH_BOSC_EP_INT_STA_REG0 0x80000
#define KMH_BOSC_MSI_CTRL_INTSTA 0x0000ff00U
#define KMH_BOSC_EP_INT_W1C      0x0000ffffU

typedef struct KmhBoscPcieApp {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    qemu_irq msi_irq;
    qemu_irq intx_irqs[PCI_NUM_PINS];
    uint32_t msi_levels;
    uint32_t int_status;
} KmhBoscPcieApp;

OBJECT_DECLARE_SIMPLE_TYPE(KmhBoscPcieApp, KMH_BOSC_PCIE_APP)

static void kmh_bosc_pcie_app_update_irq(KmhBoscPcieApp *s)
{
    qemu_set_irq(s->msi_irq, s->msi_levels ||
                 (s->int_status & KMH_BOSC_MSI_CTRL_INTSTA));
}

static void kmh_bosc_pcie_app_msi(void *opaque, int bank, int level)
{
    KmhBoscPcieApp *s = opaque;

    if (level) {
        s->msi_levels |= 1U << bank;
        s->int_status |= 1U << (8 + bank);
    } else {
        s->msi_levels &= ~(1U << bank);
    }
    kmh_bosc_pcie_app_update_irq(s);
}

static void kmh_bosc_pcie_app_intx(void *opaque, int pin, int level)
{
    KmhBoscPcieApp *s = opaque;
    bool old = s->int_status & (1U << (16 + pin));

    if (level) {
        s->int_status |= 1U << (16 + pin);
    } else {
        s->int_status &= ~(1U << (16 + pin));
    }
    if (old != !!level) {
        s->int_status |= 1U << (pin + (level ? 0 : 4));
    }
    /* Preserve the existing level-sensitive INTx wiring. */
    qemu_set_irq(s->intx_irqs[pin], level);
}

static uint64_t kmh_bosc_pcie_app_read(void *opaque, hwaddr addr,
                                       unsigned size)
{
    KmhBoscPcieApp *s = opaque;

    return addr == KMH_BOSC_EP_INT_STA_REG0 ? s->int_status : 0;
}

static void kmh_bosc_pcie_app_write(void *opaque, hwaddr addr,
                                    uint64_t value, unsigned size)
{
    KmhBoscPcieApp *s = opaque;

    if (addr == KMH_BOSC_EP_INT_STA_REG0) {
        s->int_status &= ~(value & KMH_BOSC_EP_INT_W1C);
        /* An active DWC bank must not be lost if APP is cleared first. */
        s->int_status |= s->msi_levels << 8;
        kmh_bosc_pcie_app_update_irq(s);
    }
}

static const MemoryRegionOps kmh_bosc_pcie_app_ops = {
    .read = kmh_bosc_pcie_app_read,
    .write = kmh_bosc_pcie_app_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void kmh_bosc_pcie_app_reset(DeviceState *dev)
{
    KmhBoscPcieApp *s = KMH_BOSC_PCIE_APP(dev);

    s->msi_levels = 0;
    s->int_status = 0;
    kmh_bosc_pcie_app_update_irq(s);
    for (int pin = 0; pin < PCI_NUM_PINS; pin++) {
        qemu_set_irq(s->intx_irqs[pin], 0);
    }
}

static int kmh_bosc_pcie_app_post_load(void *opaque, int version_id)
{
    KmhBoscPcieApp *s = opaque;

    kmh_bosc_pcie_app_update_irq(s);
    for (int pin = 0; pin < PCI_NUM_PINS; pin++) {
        qemu_set_irq(s->intx_irqs[pin],
                     !!(s->int_status & (1U << (16 + pin))));
    }
    return 0;
}

static const VMStateDescription vmstate_kmh_bosc_pcie_app = {
    .name = TYPE_KMH_BOSC_PCIE_APP,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = kmh_bosc_pcie_app_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(msi_levels, KmhBoscPcieApp),
        VMSTATE_UINT32(int_status, KmhBoscPcieApp),
        VMSTATE_END_OF_LIST()
    },
};

static void kmh_bosc_pcie_app_init(Object *obj)
{
    KmhBoscPcieApp *s = KMH_BOSC_PCIE_APP(obj);

    memory_region_init_io(&s->mmio, obj, &kmh_bosc_pcie_app_ops, s,
                          TYPE_KMH_BOSC_PCIE_APP, 1 * MiB);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->msi_irq);
    qdev_init_gpio_in_named(DEVICE(obj), kmh_bosc_pcie_app_msi,
                            "msi-bank", DESIGNWARE_PCIE_NUM_MSI_BANKS);
    qdev_init_gpio_in_named(DEVICE(obj), kmh_bosc_pcie_app_intx,
                            "intx", PCI_NUM_PINS);
    qdev_init_gpio_out_named(DEVICE(obj), s->intx_irqs,
                             "intx-out", PCI_NUM_PINS);
}

static void kmh_bosc_pcie_app_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &vmstate_kmh_bosc_pcie_app;
    device_class_set_legacy_reset(dc, kmh_bosc_pcie_app_reset);
}

typedef struct KmhBoscState KmhBoscState;

typedef struct KmhBoscPcieDma KmhBoscPcieDma;

typedef struct KmhBoscPcieRequester {
    IOMMUMemoryRegion mr;
    AddressSpace as;
    MemoryRegion msi_monitor;
    AddressSpace msi_as;
    KmhBoscPcieDma *port;
    AddressSpace *downstream;
    PCIBus *bus;
    int devfn;
    struct KmhBoscPcieRequester *next;
} KmhBoscPcieRequester;

struct KmhBoscPcieDma {
    DesignwarePCIEHost *host;
    BoscDidMapperState *mapper;
    AddressSpace *die_as;
    AddressSpace msi_as;
    const PCIIOMMUOps *downstream_ops;
    void *downstream_opaque;
    int die;
    int port;
    KmhBoscPcieRequester *requesters;
};

typedef struct KmhBoscBootCtlState {
    MemoryRegion mr;
    KmhBoscState *machine;
    int die;
} KmhBoscBootCtlState;

typedef struct KmhBoscSysCtrlState {
    MemoryRegion mr;
    KmhBoscState *machine;
    int die;
    uint32_t hartid_grp[4];
    uint32_t rst_vec_low[KMH_BOSC_APP_HARTS_PER_DIE];
    uint32_t rst_vec_high[KMH_BOSC_APP_HARTS_PER_DIE];
    uint32_t cpu_iso_en;
    uint32_t cpu_pwrdwn_req_n;
    uint32_t cpu_pwrdwn_ack_n;
    uint32_t mcu_handshake[4];
    uint32_t mcu_die_num;
    uint32_t high_addr_cfg;
    uint32_t mcu_rsvd[4];
    uint32_t qspi_sd_sel;
} KmhBoscSysCtrlState;

struct KmhBoscState {
    MachineState parent_obj;

    RISCVHartArrayState app_cpus[KMH_BOSC_DIES];
    RISCVHartArrayState mcu_cpus[KMH_BOSC_DIES];
    DeviceState *irqchip[KMH_BOSC_DIES];
    OrIRQState pcie_ep_irq[KMH_BOSC_DIES][KMH_BOSC_PCIE_PER_DIE];
    OrIRQState iommu_irq[KMH_BOSC_DIES];

    DesignwarePCIEHost pcie[KMH_BOSC_DIES][KMH_BOSC_PCIE_PER_DIE];
    KmhBoscPcieApp pcie_app[KMH_BOSC_DIES][KMH_BOSC_PCIE_PER_DIE];
    KmhBoscPcieDma pcie_dma[KMH_BOSC_DIES][KMH_BOSC_PCIE_PER_DIE];
    MemoryRegion pcie_outbound[KMH_BOSC_DIES][KMH_BOSC_PCIE_PER_DIE];
    MemoryRegion pcie_ecam_alias[KMH_BOSC_DIES][KMH_BOSC_PCIE_PER_DIE];
    MemoryRegion pcie_mem_alias[KMH_BOSC_DIES][KMH_BOSC_PCIE_PER_DIE];

    MemoryRegion ddr_alias[KMH_BOSC_DIES];
    MemoryRegion mcu_bootrom[KMH_BOSC_DIES];
    MemoryRegion sram0[KMH_BOSC_DIES];
    MemoryRegion sram1[KMH_BOSC_DIES];
    MemoryRegion qspi_flash_data[KMH_BOSC_DIES];
    MemoryRegion mcu_local_mem[KMH_BOSC_DIES];
    MemoryRegion mcu_local_system_alias[KMH_BOSC_DIES];
    MemoryRegion mcu_local_bootrom_alias[KMH_BOSC_DIES];
    MemoryRegion mcu_local_sram0_alias[KMH_BOSC_DIES];
    MemoryRegion mcu_local_sram1_alias[KMH_BOSC_DIES];
    MemoryRegion mcu_local_qspi_flash_data_alias[KMH_BOSC_DIES];
    MemoryRegion mcu_local_bootctl_alias[KMH_BOSC_DIES];
    MemoryRegion mcu_local_sysctrl_alias[KMH_BOSC_DIES];
    MemoryRegion mcu_local_dmac_alias[KMH_BOSC_DIES];
    MemoryRegion die_mem[KMH_BOSC_DIES];
    MemoryRegion die_system_alias[KMH_BOSC_DIES];
    MemoryRegion die_local_alias[KMH_BOSC_DIES][ARRAY_SIZE(kmh_bosc_memmap) +
        KMH_BOSC_PCIE_PER_DIE * KMH_BOSC_PCIE_MEMMAP_COUNT];
    MemoryRegion clint_alias[KMH_BOSC_DIES];
    MemoryRegion traffic_window[KMH_BOSC_DIES][KMH_BOSC_IOMMU_PER_DIE];
    MemoryRegion traffic_local_alias[KMH_BOSC_DIES][KMH_BOSC_IOMMU_PER_DIE];
    RISCVIOMMUState *iommu[KMH_BOSC_DIES][KMH_BOSC_IOMMU_PER_DIE];
    AddressSpace die_as[KMH_BOSC_DIES];
    KmhBoscBootCtlState bootctl[KMH_BOSC_DIES];
    KmhBoscSysCtrlState sysctrl[KMH_BOSC_DIES];
    DWAxiDMACState dmac[KMH_BOSC_DIES];

    KmhBoscCheckpoint checkpoint;

    char *boot_source;
    char *mcu_bios;
    bool single_soc;
    bool per_die_core_mask;
    uint32_t die_mask;
    uint32_t core_mask[KMH_BOSC_DIES];
    bool dw_pcie;
    bool generated_acpi;
    OnOffAuto iommu_sys;
    bool iommu_bypass;
    OnOffAuto generated_dtb;
    bool autotest_dtb;
    uint64_t acpi_handoff_addr;
    uint64_t acpi_handoff_size;
    uint64_t autotest_trigger_addr;
    uint64_t autotest_rootfs_addr;
    uint64_t autotest_workload_addr;
    int fdt_size;
    hwaddr mcu_fdt_addr;
};

DECLARE_INSTANCE_CHECKER(KmhBoscState, KMH_BOSC_MACHINE, KMH_BOSC_MACHINE_TYPE)

static int kmh_bosc_first_selected_die(const KmhBoscState *s);

static int kmh_bosc_die_count(const KmhBoscState *s)
{
    return s->single_soc ? 1 : KMH_BOSC_DIES;
}

static inline hwaddr kmh_bosc_die_addr(const KmhBoscState *s, int die,
                                      hwaddr offset)
{
    /* Only the four-die SoC encodes hardware IDs 1..4. */
    return s->single_soc ? offset :
           (((hwaddr)(die + 1)) << KMH_BOSC_DIE_SHIFT) | offset;
}

static inline hwaddr kmh_bosc_die_memmap_base(const KmhBoscState *s,
                                             int die, int region)
{
    return kmh_bosc_die_addr(s, die, kmh_bosc_memmap[region].base);
}

static RISCVCPU *kmh_bosc_app_cpu(KmhBoscState *s, int hartid)
{
    return &s->app_cpus[hartid / KMH_BOSC_APP_HARTS_PER_DIE].harts[
        hartid % KMH_BOSC_APP_HARTS_PER_DIE];
}

static inline hwaddr kmh_bosc_ddr_base(int die)
{
    return KMH_BOSC_DDR_NODE_BASE + ((hwaddr)die * KMH_BOSC_DDR_NODE_STRIDE);
}

static uint64_t kmh_bosc_die_ram_size(const MachineState *machine)
{
    return machine->ram_size /
           kmh_bosc_die_count(KMH_BOSC_MACHINE(machine));
}

static inline int kmh_bosc_pcie_domain(int die, int port)
{
    return die * KMH_BOSC_PCIE_PER_DIE + port;
}

static hwaddr kmh_bosc_pcie_mem32_bus_base(int segment)
{
    return KMH_BOSC_PCIE_MEM32_BUS_BASE +
           (hwaddr)segment * KMH_BOSC_PCIE_ROOT_BUS_STRIDE;
}

static bool kmh_bosc_boot_from_mcu(const KmhBoscState *s)
{
    return !strcmp(s->boot_source, "mcu");
}

static bool kmh_bosc_should_generate_dtb(const KmhBoscState *s)
{
    return s->generated_dtb == ON_OFF_AUTO_ON;
}

static bool kmh_bosc_is_iommu_sys_enabled(const KmhBoscState *s)
{
    return s->iommu_sys == ON_OFF_AUTO_ON;
}

static bool kmh_bosc_has_fw_payload(const MachineState *machine)
{
    return machine->firmware && g_str_has_suffix(machine->firmware,
                                                 "fw_payload.bin");
}

static bool kmh_bosc_firmware_is_none(const MachineState *machine)
{
    return machine->firmware && !strcmp(machine->firmware, "none");
}

static bool kmh_bosc_ddr_direct_kernel(const MachineState *machine)
{
    return kmh_bosc_firmware_is_none(machine) && machine->kernel_filename;
}

static bool kmh_bosc_die_selected(const KmhBoscState *s, int die)
{
    return s->die_mask & (1U << die);
}

static int kmh_bosc_selected_die_count(const KmhBoscState *s)
{
    return ctpop32(s->die_mask & KMH_BOSC_DIE_MASK_BITS);
}

static int kmh_bosc_selected_hart_count_from_masks(
    uint32_t die_mask, const uint32_t core_mask[KMH_BOSC_DIES])
{
    int die;
    int count = 0;

    for (die = 0; die < KMH_BOSC_DIES; die++) {
        if (!(die_mask & (1U << die))) {
            continue;
        }

        count += ctpop32(core_mask[die] & KMH_BOSC_APP_HART_MASK);
    }

    return count;
}

static bool kmh_bosc_app_hart_mask_selected(const KmhBoscState *s, int die,
                                            int hart)
{
    return s->core_mask[die] & (1U << hart);
}

static bool kmh_bosc_app_hart_selected(const KmhBoscState *s, int die, int hart)
{
    return kmh_bosc_die_selected(s, die) &&
           kmh_bosc_app_hart_mask_selected(s, die, hart);
}

static int kmh_bosc_selected_hart_count(const KmhBoscState *s)
{
    return kmh_bosc_selected_hart_count_from_masks(s->die_mask, s->core_mask);
}

static int kmh_bosc_numa_node_id(const KmhBoscState *s, int die)
{
    return ctpop32(s->die_mask & ((1U << die) - 1));
}

static int kmh_bosc_first_selected_hartid(const KmhBoscState *s)
{
    int die, hart;

    for (die = 0; die < kmh_bosc_die_count(s); die++) {
        for (hart = 0; hart < KMH_BOSC_APP_HARTS_PER_DIE; hart++) {
            if (kmh_bosc_app_hart_selected(s, die, hart)) {
                return die * KMH_BOSC_APP_HARTS_PER_DIE + hart;
            }
        }
    }

    return -1;
}

static int kmh_bosc_first_selected_die(const KmhBoscState *s)
{
    int die, hart;

    for (die = 0; die < kmh_bosc_die_count(s); die++) {
        for (hart = 0; hart < KMH_BOSC_APP_HARTS_PER_DIE; hart++) {
            if (kmh_bosc_app_hart_selected(s, die, hart)) {
                return die;
            }
        }
    }

    return -1;
}

static void kmh_bosc_power_off_unselected_app_harts(KmhBoscState *s)
{
    int die, hart;

    for (die = 0; die < kmh_bosc_die_count(s); die++) {
        for (hart = 0; hart < KMH_BOSC_APP_HARTS_PER_DIE; hart++) {
            CPUState *cs;
            int hartid;

            if (kmh_bosc_app_hart_selected(s, die, hart)) {
                continue;
            }

            hartid = die * KMH_BOSC_APP_HARTS_PER_DIE + hart;
            cs = CPU(kmh_bosc_app_cpu(s, hartid));
            cs->start_powered_off = true;
            cs->halted = true;
        }
    }
}

#define KMH_BOSC_SYSCTRL_HARTID_GRP_BASE      0x0000
#define KMH_BOSC_SYSCTRL_RST_VEC_BASE         0x0010
#define KMH_BOSC_SYSCTRL_CPU_ISO_EN           0x0114
#define KMH_BOSC_SYSCTRL_CPU_PWRDWN_REQ_N     0x0118
#define KMH_BOSC_SYSCTRL_CPU_PWRDWN_ACK_N     0x011c
#define KMH_BOSC_SYSCTRL_UNCORE_SELF_ID       0x0128
#define KMH_BOSC_SYSCTRL_MCU_HANDSHAKE_BASE   0x012c
#define KMH_BOSC_SYSCTRL_MCU_IO_DIEID         0x013c
#define KMH_BOSC_SYSCTRL_MCU_DIE_NUM          0x0140
#define KMH_BOSC_SYSCTRL_HIGH_ADDR_CFG        0x0144
#define KMH_BOSC_SYSCTRL_MCU_RSVD_BASE        0x0168
#define KMH_BOSC_SYSCTRL_QSPI_SD_SEL          0x021c

#define KMH_BOSC_SYSCTRL_RST_VEC_STRIDE       0x8
#define KMH_BOSC_SYSCTRL_REG_MASK_16          0x0000ffffU
#define KMH_BOSC_SYSCTRL_RST_VEC_MASK         0x00ffffffU
#define KMH_BOSC_SYSCTRL_DEFAULT_RST_VEC_HIGH 0x2
#define KMH_BOSC_SYSCTRL_HIGH_ADDR_CFG_MASK   0x0001ffffU

static void kmh_bosc_sysctrl_reset(void *opaque)
{
    KmhBoscSysCtrlState *sysctrl = opaque;
    int hart;

    memset(sysctrl->hartid_grp, 0, sizeof(sysctrl->hartid_grp));
    memset(sysctrl->rst_vec_low, 0, sizeof(sysctrl->rst_vec_low));
    memset(sysctrl->rst_vec_high, 0, sizeof(sysctrl->rst_vec_high));
    memset(sysctrl->mcu_handshake, 0, sizeof(sysctrl->mcu_handshake));
    memset(sysctrl->mcu_rsvd, 0, sizeof(sysctrl->mcu_rsvd));

    for (hart = 0; hart < KMH_BOSC_APP_HARTS_PER_DIE; hart++) {
        sysctrl->rst_vec_high[hart] =
            KMH_BOSC_SYSCTRL_DEFAULT_RST_VEC_HIGH;
    }

    sysctrl->cpu_iso_en = 0;
    sysctrl->cpu_pwrdwn_req_n = 1;
    sysctrl->cpu_pwrdwn_ack_n = 0;
    sysctrl->mcu_die_num = 0;
    sysctrl->high_addr_cfg = 0;
    sysctrl->qspi_sd_sel = 0;
}

static uint64_t kmh_bosc_sysctrl_get_reset_vec(const KmhBoscState *s,
                                               int die, int hart)
{
    const KmhBoscSysCtrlState *sysctrl = &s->sysctrl[die];
    uint64_t high = sysctrl->rst_vec_high[hart] &
                    KMH_BOSC_SYSCTRL_RST_VEC_MASK;
    uint64_t low = sysctrl->rst_vec_low[hart] &
                   KMH_BOSC_SYSCTRL_RST_VEC_MASK;

    return (high << 24) | low;
}

static uint64_t kmh_bosc_sysctrl_read(void *opaque, hwaddr addr,
                                      unsigned size)
{
    KmhBoscSysCtrlState *sysctrl = opaque;

    if (addr & 0x3) {
        return 0;
    }

    if (addr < sizeof(sysctrl->hartid_grp)) {
        return sysctrl->hartid_grp[addr / sizeof(uint32_t)];
    }

    if (addr >= KMH_BOSC_SYSCTRL_RST_VEC_BASE &&
        addr < KMH_BOSC_SYSCTRL_RST_VEC_BASE +
               KMH_BOSC_APP_HARTS_PER_DIE * KMH_BOSC_SYSCTRL_RST_VEC_STRIDE) {
        unsigned index = (addr - KMH_BOSC_SYSCTRL_RST_VEC_BASE) /
                         KMH_BOSC_SYSCTRL_RST_VEC_STRIDE;
        bool high = (addr - KMH_BOSC_SYSCTRL_RST_VEC_BASE) & 0x4;

        return high ? sysctrl->rst_vec_high[index] :
                      sysctrl->rst_vec_low[index];
    }

    switch (addr) {
    case KMH_BOSC_SYSCTRL_CPU_ISO_EN:
        return sysctrl->cpu_iso_en;
    case KMH_BOSC_SYSCTRL_CPU_PWRDWN_REQ_N:
        return sysctrl->cpu_pwrdwn_req_n;
    case KMH_BOSC_SYSCTRL_CPU_PWRDWN_ACK_N:
        return sysctrl->cpu_pwrdwn_ack_n;
    case KMH_BOSC_SYSCTRL_UNCORE_SELF_ID:
        return sysctrl->die + 1;
    case KMH_BOSC_SYSCTRL_MCU_IO_DIEID:
        return sysctrl->die;
    case KMH_BOSC_SYSCTRL_MCU_DIE_NUM:
        return sysctrl->mcu_die_num;
    case KMH_BOSC_SYSCTRL_HIGH_ADDR_CFG:
        return sysctrl->high_addr_cfg;
    case KMH_BOSC_SYSCTRL_QSPI_SD_SEL:
        return sysctrl->qspi_sd_sel;
    default:
        break;
    }

    if (addr >= KMH_BOSC_SYSCTRL_MCU_HANDSHAKE_BASE &&
        addr < KMH_BOSC_SYSCTRL_MCU_HANDSHAKE_BASE +
               sizeof(sysctrl->mcu_handshake)) {
        return sysctrl->mcu_handshake[
            (addr - KMH_BOSC_SYSCTRL_MCU_HANDSHAKE_BASE) / sizeof(uint32_t)];
    }

    if (addr >= KMH_BOSC_SYSCTRL_MCU_RSVD_BASE &&
        addr < KMH_BOSC_SYSCTRL_MCU_RSVD_BASE + sizeof(sysctrl->mcu_rsvd)) {
        return sysctrl->mcu_rsvd[
            (addr - KMH_BOSC_SYSCTRL_MCU_RSVD_BASE) / sizeof(uint32_t)];
    }

    return 0;
}

static void kmh_bosc_sysctrl_write(void *opaque, hwaddr addr,
                                   uint64_t value, unsigned size)
{
    KmhBoscSysCtrlState *sysctrl = opaque;
    uint32_t val = value;

    if (addr & 0x3) {
        return;
    }

    if (addr < sizeof(sysctrl->hartid_grp)) {
        sysctrl->hartid_grp[addr / sizeof(uint32_t)] = val;
        return;
    }

    if (addr >= KMH_BOSC_SYSCTRL_RST_VEC_BASE &&
        addr < KMH_BOSC_SYSCTRL_RST_VEC_BASE +
               KMH_BOSC_APP_HARTS_PER_DIE * KMH_BOSC_SYSCTRL_RST_VEC_STRIDE) {
        unsigned index = (addr - KMH_BOSC_SYSCTRL_RST_VEC_BASE) /
                         KMH_BOSC_SYSCTRL_RST_VEC_STRIDE;

        if ((addr - KMH_BOSC_SYSCTRL_RST_VEC_BASE) & 0x4) {
            sysctrl->rst_vec_high[index] =
                val & KMH_BOSC_SYSCTRL_RST_VEC_MASK;
        } else {
            sysctrl->rst_vec_low[index] =
                val & KMH_BOSC_SYSCTRL_RST_VEC_MASK;
        }
        return;
    }

    switch (addr) {
    case KMH_BOSC_SYSCTRL_CPU_ISO_EN:
        sysctrl->cpu_iso_en = val & KMH_BOSC_SYSCTRL_REG_MASK_16;
        return;
    case KMH_BOSC_SYSCTRL_CPU_PWRDWN_REQ_N:
        sysctrl->cpu_pwrdwn_req_n = val & KMH_BOSC_SYSCTRL_REG_MASK_16;
        return;
    case KMH_BOSC_SYSCTRL_CPU_PWRDWN_ACK_N:
        sysctrl->cpu_pwrdwn_ack_n = val & KMH_BOSC_SYSCTRL_REG_MASK_16;
        return;
    case KMH_BOSC_SYSCTRL_MCU_DIE_NUM:
        sysctrl->mcu_die_num = val;
        return;
    case KMH_BOSC_SYSCTRL_HIGH_ADDR_CFG:
        sysctrl->high_addr_cfg =
            val & KMH_BOSC_SYSCTRL_HIGH_ADDR_CFG_MASK;
        return;
    case KMH_BOSC_SYSCTRL_QSPI_SD_SEL:
        sysctrl->qspi_sd_sel = val & 0x1;
        return;
    default:
        break;
    }

    if (addr >= KMH_BOSC_SYSCTRL_MCU_HANDSHAKE_BASE &&
        addr < KMH_BOSC_SYSCTRL_MCU_HANDSHAKE_BASE +
               sizeof(sysctrl->mcu_handshake)) {
        sysctrl->mcu_handshake[
            (addr - KMH_BOSC_SYSCTRL_MCU_HANDSHAKE_BASE) /
            sizeof(uint32_t)] = val;
        return;
    }

    if (addr >= KMH_BOSC_SYSCTRL_MCU_RSVD_BASE &&
        addr < KMH_BOSC_SYSCTRL_MCU_RSVD_BASE + sizeof(sysctrl->mcu_rsvd)) {
        sysctrl->mcu_rsvd[(addr - KMH_BOSC_SYSCTRL_MCU_RSVD_BASE) /
                          sizeof(uint32_t)] = val;
    }
}

static const MemoryRegionOps kmh_bosc_sysctrl_ops = {
    .read = kmh_bosc_sysctrl_read,
    .write = kmh_bosc_sysctrl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void kmh_bosc_prepare_release_app_hart(KmhBoscState *s, int hartid,
                                              hwaddr resetvec)
{
    RISCVCPU *cpu;
    CPUState *cs;

    if (hartid < 0 || hartid >= KMH_BOSC_TOTAL_APP_HARTS) {
        return;
    }

    cpu = kmh_bosc_app_cpu(s, hartid);
    cs = CPU(cpu);
    cpu->env.resetvec = resetvec;
    cpu_reset(cs);
    cpu->env.gpr[10] = hartid;
    if (s->mcu_fdt_addr) {
        cpu->env.gpr[11] = s->mcu_fdt_addr;
    }
    cs->halted = false;
}

static void kmh_bosc_resume_app_hart(KmhBoscState *s, int hartid)
{
    RISCVCPU *cpu;
    CPUState *cs;

    if (hartid < 0 || hartid >= KMH_BOSC_TOTAL_APP_HARTS) {
        return;
    }

    cpu = kmh_bosc_app_cpu(s, hartid);
    cs = CPU(cpu);
    cpu_resume(cs);
}

static void kmh_bosc_set_app_boot_args(KmhBoscState *s, hwaddr fdt_addr)
{
    int die, hart;

    for (die = 0; die < kmh_bosc_die_count(s); die++) {
        for (hart = 0; hart < KMH_BOSC_APP_HARTS_PER_DIE; hart++) {
            int hartid;
            RISCVCPU *cpu;

            if (!kmh_bosc_app_hart_selected(s, die, hart)) {
                continue;
            }

            hartid = die * KMH_BOSC_APP_HARTS_PER_DIE + hart;
            cpu = kmh_bosc_app_cpu(s, hartid);
            cpu->env.gpr[10] = hartid;
            cpu->env.gpr[11] = fdt_addr;
        }
    }
}

static void kmh_bosc_set_app_resetvec(KmhBoscState *s, hwaddr resetvec)
{
    int die, hart;

    for (die = 0; die < kmh_bosc_die_count(s); die++) {
        for (hart = 0; hart < KMH_BOSC_APP_HARTS_PER_DIE; hart++) {
            int hartid;
            RISCVCPU *cpu;

            if (!kmh_bosc_app_hart_selected(s, die, hart)) {
                continue;
            }

            hartid = die * KMH_BOSC_APP_HARTS_PER_DIE + hart;
            cpu = kmh_bosc_app_cpu(s, hartid);
            cpu->env.resetvec = resetvec;
            cpu->env.pc = resetvec;
        }
    }
}

static void kmh_bosc_set_app_hartid_arg(KmhBoscState *s)
{
    int die, hart;

    for (die = 0; die < kmh_bosc_die_count(s); die++) {
        for (hart = 0; hart < KMH_BOSC_APP_HARTS_PER_DIE; hart++) {
            int hartid;
            RISCVCPU *cpu;

            if (!kmh_bosc_app_hart_selected(s, die, hart)) {
                continue;
            }

            hartid = die * KMH_BOSC_APP_HARTS_PER_DIE + hart;
            cpu = kmh_bosc_app_cpu(s, hartid);
            cpu->env.gpr[10] = hartid;
        }
    }
}

static void kmh_bosc_release_die_harts(KmhBoscState *s, int die)
{
    int hart;

    if (!kmh_bosc_die_selected(s, die)) {
        return;
    }

    for (hart = 0; hart < KMH_BOSC_APP_HARTS_PER_DIE; hart++) {
        int hartid;
        hwaddr resetvec;

        if (!kmh_bosc_app_hart_selected(s, die, hart)) {
            continue;
        }

        hartid = die * KMH_BOSC_APP_HARTS_PER_DIE + hart;
        resetvec = kmh_bosc_sysctrl_get_reset_vec(s, die, hart);

        kmh_bosc_prepare_release_app_hart(s, hartid, resetvec);
    }

    for (hart = 0; hart < KMH_BOSC_APP_HARTS_PER_DIE; hart++) {
        if (!kmh_bosc_app_hart_selected(s, die, hart)) {
            continue;
        }

        kmh_bosc_resume_app_hart(s, die * KMH_BOSC_APP_HARTS_PER_DIE + hart);
    }
}

static uint64_t kmh_bosc_bootctl_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void kmh_bosc_bootctl_write(void *opaque, hwaddr addr,
                                   uint64_t value, unsigned size)
{
    KmhBoscBootCtlState *bootctl = opaque;

    if (!addr && value) {
        KmhBoscState *s = bootctl->machine;

        if (kmh_bosc_boot_from_mcu(s)) {
            kmh_bosc_release_die_harts(s, bootctl->die);
            return;
        }

        kmh_bosc_release_die_harts(s, bootctl->die);
    }
}

static const MemoryRegionOps kmh_bosc_bootctl_ops = {
    .read = kmh_bosc_bootctl_read,
    .write = kmh_bosc_bootctl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static uint64_t kmh_bosc_compute_fdt_addr(hwaddr node_base, hwaddr node_size,
                                          MachineState *machine,
                                          RISCVBootInfo *info)
{
    hwaddr dram_end, temp;
    uint64_t dtb_start, dtb_start_limit;
    int fdtsize;

    g_assert(fdt_pack(machine->fdt) == 0);

    fdtsize = fdt_totalsize(machine->fdt);
    if (fdtsize <= 0) {
        error_report("invalid device-tree");
        exit(1);
    }

    if (info->kernel_size) {
        dtb_start_limit = info->image_high_addr;
    } else {
        dtb_start_limit = 0;
    }

    dram_end = node_base + node_size;
    temp = info->is_32bit ? MIN(dram_end, 3072 * MiB) : dram_end;
    dtb_start = QEMU_ALIGN_DOWN(temp - fdtsize, 2 * MiB);

    if (dtb_start_limit && dtb_start < dtb_start_limit) {
        error_report("not enough memory in the boot DDR node to place DTB");
        exit(1);
    }

    return dtb_start;
}

static DeviceState *kmh_bosc_create_aia(KmhBoscState *s, int die,
                                       int hartid_base)
{
    const MemMapEntry *memmap = kmh_bosc_memmap;
    DeviceState *aplic_m;
    DeviceState *aplic_s;
    hwaddr imsic_m_base = kmh_bosc_die_memmap_base(s, die, KMH_BOSC_IMSIC_M);
    hwaddr imsic_s_base = kmh_bosc_die_memmap_base(s, die, KMH_BOSC_IMSIC_S);
    hwaddr aplic_m_base = kmh_bosc_die_memmap_base(s, die, KMH_BOSC_APLIC_M);
    hwaddr aplic_s_base = kmh_bosc_die_memmap_base(s, die, KMH_BOSC_APLIC_S);
    int i;

    for (i = 0; i < KMH_BOSC_APP_HARTS_PER_DIE; i++) {
        s->checkpoint.imsic[s->checkpoint.num_imsic++] =
            riscv_imsic_create(imsic_m_base + i * IMSIC_HART_SIZE(0),
                           hartid_base + i, true, 1,
                           KMH_BOSC_IMSIC_NUM_IDS);
        s->checkpoint.imsic[s->checkpoint.num_imsic++] =
            riscv_imsic_create(imsic_s_base +
                           i * IMSIC_HART_SIZE(KMH_BOSC_IMSIC_GUEST_INDEX_BITS),
                           hartid_base + i, false,
                           1U << KMH_BOSC_IMSIC_GUEST_INDEX_BITS,
                           KMH_BOSC_IMSIC_NUM_IDS);
    }

    aplic_m = riscv_aplic_create(aplic_m_base, memmap[KMH_BOSC_APLIC_M].size,
                                 0, 0, KMH_BOSC_APLIC_NUM_SOURCES,
                                 1, true, true, NULL);
    aplic_s = riscv_aplic_create(aplic_s_base, memmap[KMH_BOSC_APLIC_S].size,
                       0, 0, KMH_BOSC_APLIC_NUM_SOURCES,
                       1, true, false, aplic_m);
    /* MSI die ID 0 is relative to the APLIC, not to the issuing CPU. */
    riscv_aplic_set_msi_address_space(RISCV_APLIC(aplic_m), &s->die_as[die]);
    riscv_aplic_set_msi_address_space(RISCV_APLIC(aplic_s), &s->die_as[die]);

    s->checkpoint.aplic[s->checkpoint.num_aplic++] = aplic_m;
    s->checkpoint.aplic[s->checkpoint.num_aplic++] = aplic_s;
    return aplic_m;
}

static void kmh_bosc_add_distance_map(void *fdt, const KmhBoscState *s)
{
    const int selected_dies = kmh_bosc_selected_die_count(s);
    g_autofree uint32_t *matrix = g_new0(uint32_t,
                                         selected_dies * selected_dies * 3);
    int die, other, idx = 0;

    qemu_fdt_add_subnode(fdt, "/distance-map");
    qemu_fdt_setprop_string(fdt, "/distance-map", "compatible",
                            "numa-distance-map-v1");

    for (die = 0; die < kmh_bosc_die_count(s); die++) {
        int die_id;

        if (!kmh_bosc_die_selected(s, die)) {
            continue;
        }

        die_id = kmh_bosc_numa_node_id(s, die);
        for (other = 0; other < kmh_bosc_die_count(s); other++) {
            int other_id;

            if (!kmh_bosc_die_selected(s, other)) {
                continue;
            }

            other_id = kmh_bosc_numa_node_id(s, other);
            matrix[idx++] = cpu_to_be32(die_id);
            matrix[idx++] = cpu_to_be32(other_id);
            matrix[idx++] = cpu_to_be32(die == other ? 10 : 20);
        }
    }

    qemu_fdt_setprop(fdt, "/distance-map", "distance-matrix",
                     matrix, selected_dies * selected_dies * 3 *
                     sizeof(*matrix));
}

static void kmh_bosc_add_pcie_irq_map(void *fdt, const char *node_path,
                                      uint32_t intc_phandle, int ep_irq)
{
    uint32_t full_irq_map[PCI_NUM_PINS * PCI_NUM_PINS *
                          KMH_BOSC_FDT_MAX_INT_MAP_WIDTH] = { 0 };
    uint32_t *irq_map = full_irq_map;
    int irq_map_stride = 0;
    int dev, pin;

    for (dev = 0; dev < PCI_NUM_PINS; dev++) {
        int devfn = dev * 0x8;

        for (pin = 0; pin < PCI_NUM_PINS; pin++) {
            /* All legacy endpoint pins share the hardware ep_int wire. */
            int irq_nr = ep_irq;
            int i = 0;

            irq_map[i++] = cpu_to_be32(devfn << 8);
            irq_map[i++] = 0;
            irq_map[i++] = 0;
            irq_map[i++] = cpu_to_be32(pin + 1);
            irq_map[i++] = cpu_to_be32(intc_phandle);
            irq_map[i++] = cpu_to_be32(irq_nr);
            irq_map[i++] = cpu_to_be32(KMH_BOSC_IRQ_TYPE_LEVEL_HIGH);

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

static void kmh_bosc_fdt_ensure_reserved_memory(void *fdt)
{
    qemu_fdt_add_path(fdt, "/reserved-memory");
    qemu_fdt_setprop_cell(fdt, "/reserved-memory", "#address-cells", 2);
    qemu_fdt_setprop_cell(fdt, "/reserved-memory", "#size-cells", 2);
    qemu_fdt_setprop(fdt, "/reserved-memory", "ranges", NULL, 0);
}

static void kmh_bosc_fdt_add_apei_workspaces(void *fdt)
{
    static const struct {
        const char *name;
        hwaddr base;
        hwaddr size;
    } workspaces[] = {
        /* Include the complete status clear [0x87001200, 0x87002200). */
        { "einj",      0x87000000, 0x3000 },
        { "erst",      0x87003000, 0x1000 },
        { "error-log", 0x87004000, 0x2000 },
    };

    kmh_bosc_fdt_ensure_reserved_memory(fdt);
    for (size_t i = 0; i < ARRAY_SIZE(workspaces); i++) {
        g_autofree char *path = g_strdup_printf(
            "/reserved-memory/%s@%" HWADDR_PRIx,
            workspaces[i].name, workspaces[i].base);

        qemu_fdt_add_subnode(fdt, path);
        qemu_fdt_setprop_sized_cells(fdt, path, "reg",
                                     2, workspaces[i].base,
                                     2, workspaces[i].size);
        qemu_fdt_setprop(fdt, path, "no-map", NULL, 0);
    }
}

static void kmh_bosc_fdt_add_acpi_handoff(KmhBoscState *s)
{
    void *fdt = MACHINE(s)->fdt;
    g_autofree char *node_path = g_strdup_printf(
        "/reserved-memory/acpi-handoff@%" HWADDR_PRIx,
        s->acpi_handoff_addr);

    kmh_bosc_fdt_ensure_reserved_memory(fdt);
    qemu_fdt_add_subnode(fdt, node_path);
    qemu_fdt_setprop_string(fdt, node_path, "compatible",
                            "bosc,kmh-acpi-handoff");
    qemu_fdt_setprop_sized_cells(fdt, node_path, "reg",
                                 2, s->acpi_handoff_addr,
                                 2, s->acpi_handoff_size);
    qemu_fdt_setprop_string(fdt, node_path, "status", "okay");
    qemu_fdt_setprop(fdt, node_path, "no-map", NULL, 0);
}

static void kmh_bosc_add_auto_test_fdt_nodes(KmhBoscState *s)
{
    void *fdt = MACHINE(s)->fdt;
    g_autofree char *trigger_path = NULL;
    g_autofree char *rootfs_reserved_path = NULL;
    g_autofree char *workload_reserved_path = NULL;
    g_autofree char *rootfs_pmem_path = NULL;
    g_autofree char *workload_pmem_path = NULL;
    static const char *const pmem_compat[] = {
        "pmem-region",
        "nvdimm",
    };

    if (!s->autotest_dtb) {
        return;
    }

    kmh_bosc_fdt_ensure_reserved_memory(fdt);

    trigger_path = g_strdup_printf(
        "/reserved-memory/my_reserved_buffer@%" PRIx64,
        s->autotest_trigger_addr);
    rootfs_reserved_path = g_strdup_printf(
        "/reserved-memory/region@%" PRIx64, s->autotest_rootfs_addr);
    workload_reserved_path = g_strdup_printf(
        "/reserved-memory/region@%" PRIx64, s->autotest_workload_addr);
    rootfs_pmem_path = g_strdup_printf("/pmem@%" PRIx64,
                                       s->autotest_rootfs_addr);
    workload_pmem_path = g_strdup_printf("/pmem@%" PRIx64,
                                         s->autotest_workload_addr);

    qemu_fdt_add_subnode(fdt, trigger_path);
    qemu_fdt_setprop_string(fdt, trigger_path,
                            "compatible", "my,reserved-mem");
    qemu_fdt_setprop_sized_cells(fdt, trigger_path, "reg",
                                 2, s->autotest_trigger_addr,
                                 2, KMH_BOSC_AUTO_TEST_TRIGGER_SIZE);
    qemu_fdt_setprop_string(fdt, trigger_path, "status", "okay");

    qemu_fdt_add_subnode(fdt, rootfs_reserved_path);
    qemu_fdt_setprop_sized_cells(fdt, rootfs_reserved_path, "reg",
                                 2, s->autotest_rootfs_addr,
                                 2, KMH_BOSC_AUTO_TEST_ROOTFS_SIZE);
    qemu_fdt_setprop(fdt, rootfs_reserved_path, "no-map", NULL, 0);

    qemu_fdt_add_subnode(fdt, workload_reserved_path);
    qemu_fdt_setprop_sized_cells(fdt, workload_reserved_path, "reg",
                                 2, s->autotest_workload_addr,
                                 2, KMH_BOSC_AUTO_TEST_WORKLOAD_SIZE);
    qemu_fdt_setprop(fdt, workload_reserved_path, "no-map", NULL, 0);

    qemu_fdt_add_subnode(fdt, workload_pmem_path);
    qemu_fdt_setprop_string_array(fdt, workload_pmem_path, "compatible",
                                  (char **)&pmem_compat,
                                  ARRAY_SIZE(pmem_compat));
    qemu_fdt_setprop_sized_cells(fdt, workload_pmem_path, "reg",
                                 2, s->autotest_workload_addr,
                                 2, KMH_BOSC_AUTO_TEST_WORKLOAD_SIZE);
    qemu_fdt_setprop_string(fdt, workload_pmem_path, "status", "okay");

    qemu_fdt_add_subnode(fdt, rootfs_pmem_path);
    qemu_fdt_setprop_string_array(fdt, rootfs_pmem_path, "compatible",
                                  (char **)&pmem_compat,
                                  ARRAY_SIZE(pmem_compat));
    qemu_fdt_setprop_sized_cells(fdt, rootfs_pmem_path, "reg",
                                 2, s->autotest_rootfs_addr,
                                 2, KMH_BOSC_AUTO_TEST_ROOTFS_SIZE);
    qemu_fdt_setprop_string(fdt, rootfs_pmem_path, "status", "okay");

    qemu_fdt_setprop_string(fdt, "/chosen", "bootargs",
                            KMH_BOSC_AUTO_TEST_BOOTARGS);
}

typedef struct KmhBoscMemRange {
    hwaddr start;
    hwaddr end;
} KmhBoscMemRange;

static hwaddr kmh_bosc_range_end(hwaddr start, uint64_t size)
{
    hwaddr end = start + size;

    return end < start ? HWADDR_MAX : end;
}

static bool kmh_bosc_range_in_selected_ddr(KmhBoscState *s, hwaddr start,
                                           uint64_t size)
{
    MachineState *machine = MACHINE(s);
    hwaddr end;

    if (!size) {
        return false;
    }

    end = start + size;
    if (end <= start) {
        return false;
    }

    for (int die = 0; die < kmh_bosc_die_count(s); die++) {
        hwaddr ddr_start;
        hwaddr ddr_end;

        if (!kmh_bosc_die_selected(s, die)) {
            continue;
        }

        ddr_start = kmh_bosc_ddr_base(die);
        ddr_end = ddr_start + kmh_bosc_die_ram_size(machine);
        if (start >= ddr_start && end <= ddr_end) {
            return true;
        }
    }

    return false;
}

static void kmh_bosc_fdt_add_memory(KmhBoscState *s, int die, uint64_t node_size)
{
    MachineState *machine = MACHINE(s);
    void *fdt = machine->fdt;
    hwaddr node_start = kmh_bosc_ddr_base(die);
    hwaddr node_end = kmh_bosc_range_end(node_start, node_size);
    g_autofree char *mem_name = g_strdup_printf("/memory@%" HWADDR_PRIx,
                                                node_start);
    uint64_t reg[12];
    unsigned int cells = 0;

    qemu_fdt_add_subnode(fdt, mem_name);
    qemu_fdt_setprop_string(fdt, mem_name, "device_type", "memory");
    qemu_fdt_setprop_cell(fdt, mem_name, "numa-node-id",
                          kmh_bosc_numa_node_id(s, die));

    if (s->autotest_dtb) {
        KmhBoscMemRange holes[2] = {
            {
                .start = s->autotest_rootfs_addr,
                .end = kmh_bosc_range_end(s->autotest_rootfs_addr,
                                          KMH_BOSC_AUTO_TEST_ROOTFS_SIZE),
            }, {
                .start = s->autotest_workload_addr,
                .end = kmh_bosc_range_end(s->autotest_workload_addr,
                                          KMH_BOSC_AUTO_TEST_WORKLOAD_SIZE),
            }
        };
        KmhBoscMemRange merged[2];
        int nr_merged = 0;
        hwaddr cursor = node_start;
        int i;

        if (holes[1].start < holes[0].start) {
            KmhBoscMemRange tmp = holes[0];

            holes[0] = holes[1];
            holes[1] = tmp;
        }

        for (i = 0; i < ARRAY_SIZE(holes); i++) {
            hwaddr start = MAX(holes[i].start, node_start);
            hwaddr end = MIN(holes[i].end, node_end);

            if (end <= start) {
                continue;
            }
            if (nr_merged && start <= merged[nr_merged - 1].end) {
                merged[nr_merged - 1].end = MAX(merged[nr_merged - 1].end, end);
            } else {
                merged[nr_merged++] = (KmhBoscMemRange) {
                    .start = start,
                    .end = end,
                };
            }
        }

        for (i = 0; i < nr_merged; i++) {
            if (cursor < merged[i].start) {
                reg[cells++] = 2;
                reg[cells++] = cursor;
                reg[cells++] = 2;
                reg[cells++] = merged[i].start - cursor;
            }
            cursor = MAX(cursor, merged[i].end);
        }
        if (cursor < node_end) {
            reg[cells++] = 2;
            reg[cells++] = cursor;
            reg[cells++] = 2;
            reg[cells++] = node_end - cursor;
        }

        if (cells) {
            qemu_fdt_setprop_sized_cells_from_array(fdt, mem_name, "reg",
                                                    cells / 2, reg);
            return;
        }
    }

    qemu_fdt_setprop_sized_cells(fdt, mem_name, "reg",
                                 2, node_start, 2, node_size);
}

static void kmh_bosc_create_fdt(KmhBoscState *s)
{
    MachineState *machine = MACHINE(s);
    void *fdt = machine->fdt;
    const MemMapEntry *memmap = kmh_bosc_memmap;
    static const char *const imsic_compat[] = {
        "bosc,kmh-v2-imsics", "riscv,imsics",
    };
    static const char *const aplic_compat[] = {
        "bosc,kmh-v2-aplic", "riscv,aplic",
    };
    static const char *const soc_compat[] = {
        "bosc,kmh-bosc-soc",
        "simple-bus",
    };
    uint32_t aplic_s_phandles[KMH_BOSC_DIES] = { 0 };
    uint32_t intc_phandles[KMH_BOSC_TOTAL_APP_HARTS] = { 0 };
    g_autofree uint32_t *imsic_cells = NULL;
    g_autofree uint32_t *imsic_hart_indexes = NULL;
    g_autofree uint32_t *imsic_m_regs = NULL;
    g_autofree uint32_t *imsic_s_regs = NULL;
    g_autofree uint32_t *aclint_mswi_cells = NULL;
    g_autofree uint32_t *aclint_mtimer_cells = NULL;
    uint32_t imsic_m_phandle, imsic_s_phandle;
    uint32_t iommu_phandles[KMH_BOSC_DIES][KMH_BOSC_IOMMU_PER_DIE] = { { 0 } };
    const uint64_t node_size = kmh_bosc_die_ram_size(machine);
    const int boot_hartid = kmh_bosc_first_selected_hartid(s);
    const int selected_dies = kmh_bosc_selected_die_count(s);
    const int imsic_targets = selected_dies * KMH_BOSC_APP_HARTS_PER_DIE;
    /*
     * The shared ACLINT register index is the physical hart ID. Unlike the
     * separate IMSIC regsets, its parent list cannot compact disabled dies.
     */
    const int aclint_targets = kmh_bosc_die_count(s) *
                              KMH_BOSC_APP_HARTS_PER_DIE;
    uint32_t boot_cpu_phandle = 0;
    int die, hart, idx;
    int reg_idx = 0;

    imsic_cells = g_new0(uint32_t, imsic_targets * 2);
    imsic_hart_indexes = g_new0(uint32_t, imsic_targets);
    imsic_m_regs = g_new0(uint32_t, selected_dies * 4);
    imsic_s_regs = g_new0(uint32_t, selected_dies * 4);
    aclint_mswi_cells = g_new0(uint32_t, aclint_targets * 2);
    aclint_mtimer_cells = g_new0(uint32_t, aclint_targets * 2);

    qemu_fdt_setprop_string(fdt, "/", "model", s->single_soc ?
                            "BOSC KMH single-die SoC" : "BOSC KMH multi-die SoC");
    qemu_fdt_setprop_string(fdt, "/", "compatible", "bosc,kmh-bosc-soc");
    qemu_fdt_setprop_cell(fdt, "/", "#address-cells", 2);
    qemu_fdt_setprop_cell(fdt, "/", "#size-cells", 2);

    qemu_fdt_add_subnode(fdt, "/chosen");
    if (boot_hartid >= 0) {
        qemu_fdt_setprop_cell(fdt, "/chosen", "boot-hartid", boot_hartid);
    }
    qemu_fdt_add_subnode(fdt, "/aliases");
    qemu_fdt_add_subnode(fdt, "/cpus");
    qemu_fdt_add_subnode(fdt, "/soc");
    qemu_fdt_setprop_cell(fdt, "/cpus", "#address-cells", 1);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#size-cells", 0);
    qemu_fdt_setprop_cell(fdt, "/cpus", "timebase-frequency",
                          KMH_BOSC_CLINT_TIMEBASE_FREQ);
    qemu_fdt_setprop_cell(fdt, "/soc", "#address-cells", 2);
    qemu_fdt_setprop_cell(fdt, "/soc", "#size-cells", 2);
    qemu_fdt_setprop_string_array(fdt, "/soc", "compatible",
                                  (char **)&soc_compat,
                                  ARRAY_SIZE(soc_compat));
    qemu_fdt_setprop(fdt, "/soc", "ranges", NULL, 0);

    imsic_m_phandle = qemu_fdt_alloc_phandle(fdt);
    imsic_s_phandle = qemu_fdt_alloc_phandle(fdt);

    /*
     * qemu_fdt_add_subnode() prepends a child under its parent. Populate
     * CPU nodes in reverse order so Linux still observes hart IDs in
     * ascending DT order.
     */
    for (die = kmh_bosc_die_count(s) - 1; die >= 0; die--) {
        for (hart = KMH_BOSC_APP_HARTS_PER_DIE - 1; hart >= 0; hart--) {
            RISCVCPU *cpu;
            g_autofree char *cpu_name = NULL;
            g_autofree char *intc_name = NULL;
            g_autofree char *mmu_type = NULL;
            uint32_t cpu_phandle = qemu_fdt_alloc_phandle(fdt);
            uint32_t intc_phandle = qemu_fdt_alloc_phandle(fdt);

            idx = die * KMH_BOSC_APP_HARTS_PER_DIE + hart;
            cpu = kmh_bosc_app_cpu(s, idx);

            cpu_name = g_strdup_printf("/cpus/cpu@%u", idx);
            qemu_fdt_add_subnode(fdt, cpu_name);
            qemu_fdt_setprop_cell(fdt, cpu_name, "reg", idx);
            qemu_fdt_setprop_cell(fdt, cpu_name, "phandle", cpu_phandle);
            qemu_fdt_setprop_string(fdt, cpu_name, "compatible", "riscv");
            qemu_fdt_setprop_string(fdt, cpu_name, "device_type", "cpu");
            if (kmh_bosc_app_hart_selected(s, die, hart)) {
                qemu_fdt_setprop_cell(fdt, cpu_name, "numa-node-id",
                                      kmh_bosc_numa_node_id(s, die));
            }
            if (!kmh_bosc_app_hart_selected(s, die, hart)) {
                qemu_fdt_setprop_string(fdt, cpu_name, "status", "disabled");
            } else {
                qemu_fdt_setprop_string(fdt, cpu_name, "status", "okay");
            }

            riscv_isa_write_fdt(cpu, fdt, cpu_name);
            if (cpu->cfg.max_satp_mode != -1) {
                mmu_type = g_strdup_printf("riscv,%s",
                                           satp_mode_str(cpu->cfg.max_satp_mode,
                                                         false));
                qemu_fdt_setprop_string(fdt, cpu_name, "mmu-type", mmu_type);
            }

            intc_name = g_strdup_printf("%s/interrupt-controller", cpu_name);
            qemu_fdt_add_subnode(fdt, intc_name);
            qemu_fdt_setprop_cell(fdt, intc_name, "phandle", intc_phandle);
            qemu_fdt_setprop_string(fdt, intc_name, "compatible",
                                    "riscv,cpu-intc");
            qemu_fdt_setprop(fdt, intc_name, "interrupt-controller", NULL, 0);
            qemu_fdt_setprop_cell(fdt, intc_name, "#interrupt-cells", 1);
            intc_phandles[idx] = intc_phandle;

            if (idx == boot_hartid) {
                boot_cpu_phandle = cpu_phandle;
            }
        }
    }

    idx = 0;
    for (die = 0; die < kmh_bosc_die_count(s); die++) {
        for (hart = 0; hart < KMH_BOSC_APP_HARTS_PER_DIE; hart++) {
            int hartid = die * KMH_BOSC_APP_HARTS_PER_DIE + hart;
            uint32_t intc_phandle = intc_phandles[hartid];

            aclint_mswi_cells[idx * 2] = cpu_to_be32(intc_phandle);
            aclint_mswi_cells[idx * 2 + 1] = cpu_to_be32(IRQ_M_SOFT);
            aclint_mtimer_cells[idx * 2] = cpu_to_be32(intc_phandle);
            aclint_mtimer_cells[idx * 2 + 1] = cpu_to_be32(IRQ_M_TIMER);
            idx++;
        }
    }
    g_assert(idx == aclint_targets);

    if (boot_cpu_phandle) {
        qemu_fdt_add_subnode(fdt, "/chosen/opensbi-config");
        qemu_fdt_setprop_string(fdt, "/chosen/opensbi-config", "compatible",
                                "opensbi,config");
        qemu_fdt_setprop_cell(fdt, "/chosen/opensbi-config",
                              "cold-boot-harts", boot_cpu_phandle);
    }

    for (die = kmh_bosc_die_count(s) - 1; die >= 0; die--) {
        g_autofree char *aplic_m_name = NULL;
        g_autofree char *aplic_s_name = NULL;
        uint32_t aplic_m_phandle;
        uint32_t aplic_s_phandle;

        if (kmh_bosc_die_selected(s, die)) {
            kmh_bosc_fdt_add_memory(s, die, node_size);
        }

        if (!kmh_bosc_die_selected(s, die)) {
            continue;
        }

        aplic_s_phandle = qemu_fdt_alloc_phandle(fdt);
        aplic_m_phandle = qemu_fdt_alloc_phandle(fdt);
        aplic_s_phandles[die] = aplic_s_phandle;

        aplic_s_name = g_strdup_printf("/soc/aplic-s@%" HWADDR_PRIx,
                                       kmh_bosc_die_memmap_base(s, die,
                                                                KMH_BOSC_APLIC_S));
        qemu_fdt_add_subnode(fdt, aplic_s_name);
        qemu_fdt_setprop_string_array(fdt, aplic_s_name, "compatible",
                                      (char **)aplic_compat,
                                      ARRAY_SIZE(aplic_compat));
        qemu_fdt_setprop_cell(fdt, aplic_s_name, "#address-cells", 0);
        qemu_fdt_setprop_cell(fdt, aplic_s_name, "msi-parent", imsic_s_phandle);
        qemu_fdt_setprop_sized_cells(fdt, aplic_s_name, "reg",
                                     2, kmh_bosc_die_memmap_base(s, die,
                                                                 KMH_BOSC_APLIC_S),
                                     2, memmap[KMH_BOSC_APLIC_S].size);
        qemu_fdt_setprop_cell(fdt, aplic_s_name, "phandle", aplic_s_phandle);
        qemu_fdt_setprop_cell(fdt, aplic_s_name, "riscv,num-sources",
                              KMH_BOSC_APLIC_NUM_SOURCES);
        qemu_fdt_setprop_cell(fdt, aplic_s_name, "#interrupt-cells", 2);
        qemu_fdt_setprop(fdt, aplic_s_name, "interrupt-controller", NULL, 0);

        aplic_m_name = g_strdup_printf("/soc/aplic-m@%" HWADDR_PRIx,
                                       kmh_bosc_die_memmap_base(s, die,
                                                                KMH_BOSC_APLIC_M));
        qemu_fdt_add_subnode(fdt, aplic_m_name);
        qemu_fdt_setprop_string_array(fdt, aplic_m_name, "compatible",
                                      (char **)aplic_compat,
                                      ARRAY_SIZE(aplic_compat));
        qemu_fdt_setprop_cell(fdt, aplic_m_name, "#address-cells", 0);
        qemu_fdt_setprop_cell(fdt, aplic_m_name, "msi-parent", imsic_m_phandle);
        qemu_fdt_setprop_sized_cells(fdt, aplic_m_name, "reg",
                                     2, kmh_bosc_die_memmap_base(s, die,
                                                                 KMH_BOSC_APLIC_M),
                                     2, memmap[KMH_BOSC_APLIC_M].size);
        qemu_fdt_setprop_cell(fdt, aplic_m_name, "phandle", aplic_m_phandle);
        qemu_fdt_setprop_cell(fdt, aplic_m_name, "riscv,num-sources",
                              KMH_BOSC_APLIC_NUM_SOURCES);
        qemu_fdt_setprop_cell(fdt, aplic_m_name, "#interrupt-cells", 2);
        qemu_fdt_setprop(fdt, aplic_m_name, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop_cell(fdt, aplic_m_name, "riscv,children",
                              aplic_s_phandle);
        qemu_fdt_setprop_cells(fdt, aplic_m_name, "riscv,delegation",
                               aplic_s_phandle, 1,
                               KMH_BOSC_APLIC_NUM_SOURCES);
        qemu_fdt_setprop_cells(fdt, aplic_m_name, "riscv,delegate",
                               aplic_s_phandle, 1,
                               KMH_BOSC_APLIC_NUM_SOURCES);
    }

    {
        g_autofree char *mswi_name = g_strdup_printf("/soc/mswi@%" HWADDR_PRIx,
                                                     memmap[KMH_BOSC_CLINT].base);
        g_autofree char *mtimer_name = g_strdup_printf("/soc/mtimer@%" HWADDR_PRIx,
                                                       memmap[KMH_BOSC_CLINT].base +
                                                       RISCV_ACLINT_SWI_SIZE);

        qemu_fdt_add_subnode(fdt, mswi_name);
        qemu_fdt_setprop_string(fdt, mswi_name, "compatible",
                                "riscv,aclint-mswi");
        qemu_fdt_setprop_sized_cells(fdt, mswi_name, "reg",
                                     2, memmap[KMH_BOSC_CLINT].base,
                                     2, RISCV_ACLINT_SWI_SIZE);
        qemu_fdt_setprop(fdt, mswi_name, "interrupts-extended",
                         aclint_mswi_cells, aclint_targets * 2 *
                         sizeof(*aclint_mswi_cells));
        qemu_fdt_setprop(fdt, mswi_name, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop_cell(fdt, mswi_name, "#interrupt-cells", 0);

        qemu_fdt_add_subnode(fdt, mtimer_name);
        qemu_fdt_setprop_string(fdt, mtimer_name, "compatible",
                                "riscv,aclint-mtimer");
        qemu_fdt_setprop_sized_cells(
            fdt, mtimer_name, "reg",
            2, memmap[KMH_BOSC_CLINT].base + RISCV_ACLINT_SWI_SIZE +
               RISCV_ACLINT_DEFAULT_MTIME,
            2, RISCV_ACLINT_DEFAULT_MTIMER_SIZE - RISCV_ACLINT_DEFAULT_MTIME,
            2, memmap[KMH_BOSC_CLINT].base + RISCV_ACLINT_SWI_SIZE +
               RISCV_ACLINT_DEFAULT_MTIMECMP,
            2, RISCV_ACLINT_DEFAULT_MTIME);
        qemu_fdt_setprop(fdt, mtimer_name, "interrupts-extended",
                         aclint_mtimer_cells, aclint_targets * 2 *
                         sizeof(*aclint_mtimer_cells));
    }

    idx = 0;
    for (die = 0; die < kmh_bosc_die_count(s); die++) {
        if (!kmh_bosc_die_selected(s, die)) {
            continue;
        }

        for (hart = 0; hart < KMH_BOSC_APP_HARTS_PER_DIE; hart++) {
            int hartid = die * KMH_BOSC_APP_HARTS_PER_DIE + hart;

            imsic_cells[idx * 2] = cpu_to_be32(intc_phandles[hartid]);
            imsic_hart_indexes[idx] = cpu_to_be32(hartid);
            idx++;
        }
    }
    g_assert(idx == imsic_targets);

    for (idx = 0; idx < imsic_targets; idx++) {
        imsic_cells[idx * 2 + 1] = cpu_to_be32(IRQ_M_EXT);
    }
    for (die = 0; die < kmh_bosc_die_count(s); die++) {
        hwaddr base = kmh_bosc_die_memmap_base(s, die, KMH_BOSC_IMSIC_M);

        if (!kmh_bosc_die_selected(s, die)) {
            continue;
        }

        imsic_m_regs[reg_idx * 4 + 0] = cpu_to_be32(base >> 32);
        imsic_m_regs[reg_idx * 4 + 1] = cpu_to_be32(base);
        imsic_m_regs[reg_idx * 4 + 2] = 0;
        imsic_m_regs[reg_idx * 4 + 3] =
            cpu_to_be32(memmap[KMH_BOSC_IMSIC_M].size);
        reg_idx++;
    }
    qemu_fdt_add_subnode(fdt, "/soc/imsics-m");
    qemu_fdt_setprop_string_array(fdt, "/soc/imsics-m", "compatible",
                                  (char **)imsic_compat,
                                  ARRAY_SIZE(imsic_compat));
    qemu_fdt_setprop_cell(fdt, "/soc/imsics-m", "#msi-cells", 0);
    qemu_fdt_setprop_cell(fdt, "/soc/imsics-m", "phandle", imsic_m_phandle);
    qemu_fdt_setprop_cell(fdt, "/soc/imsics-m", "#interrupt-cells", 0);
    qemu_fdt_setprop_cell(fdt, "/soc/imsics-m", "riscv,num-ids",
                          KMH_BOSC_IMSIC_NUM_IDS);
    qemu_fdt_setprop(fdt, "/soc/imsics-m", "interrupt-controller", NULL, 0);
    qemu_fdt_setprop(fdt, "/soc/imsics-m", "msi-controller", NULL, 0);
    qemu_fdt_setprop(fdt, "/soc/imsics-m", "interrupts-extended",
                     imsic_cells, imsic_targets * 2 * sizeof(*imsic_cells));
    qemu_fdt_setprop(fdt, "/soc/imsics-m", "riscv,hart-indexes",
                     imsic_hart_indexes,
                     imsic_targets * sizeof(*imsic_hart_indexes));
    qemu_fdt_setprop(fdt, "/soc/imsics-m", "reg",
                     imsic_m_regs, selected_dies * 4 * sizeof(*imsic_m_regs));
    qemu_fdt_setprop_cell(fdt, "/soc/imsics-m", "riscv,hart-index-bits", 4);
    qemu_fdt_setprop_cell(fdt, "/soc/imsics-m", "riscv,group-index-bits",
                          s->single_soc ? 0 : 3);
    qemu_fdt_setprop_cell(fdt, "/soc/imsics-m", "riscv,group-index-shift",
                          KMH_BOSC_DIE_SHIFT);

    for (idx = 0; idx < imsic_targets; idx++) {
        imsic_cells[idx * 2 + 1] = cpu_to_be32(IRQ_S_EXT);
    }
    reg_idx = 0;
    for (die = 0; die < kmh_bosc_die_count(s); die++) {
        hwaddr base = kmh_bosc_die_memmap_base(s, die, KMH_BOSC_IMSIC_S);

        if (!kmh_bosc_die_selected(s, die)) {
            continue;
        }

        imsic_s_regs[reg_idx * 4 + 0] = cpu_to_be32(base >> 32);
        imsic_s_regs[reg_idx * 4 + 1] = cpu_to_be32(base);
        imsic_s_regs[reg_idx * 4 + 2] = 0;
        imsic_s_regs[reg_idx * 4 + 3] =
            cpu_to_be32(memmap[KMH_BOSC_IMSIC_S].size);
        reg_idx++;
    }
    qemu_fdt_add_subnode(fdt, "/soc/imsics-s");
    qemu_fdt_setprop_string_array(fdt, "/soc/imsics-s", "compatible",
                                  (char **)imsic_compat,
                                  ARRAY_SIZE(imsic_compat));
    qemu_fdt_setprop_cell(fdt, "/soc/imsics-s", "#msi-cells", 0);
    qemu_fdt_setprop_cell(fdt, "/soc/imsics-s", "phandle", imsic_s_phandle);
    qemu_fdt_setprop_cell(fdt, "/soc/imsics-s", "#interrupt-cells", 0);
    qemu_fdt_setprop_cell(fdt, "/soc/imsics-s", "riscv,num-ids",
                          KMH_BOSC_IMSIC_NUM_IDS);
    qemu_fdt_setprop_cell(fdt, "/soc/imsics-s", "riscv,guest-index-bits",
                          KMH_BOSC_IMSIC_GUEST_INDEX_BITS);
    qemu_fdt_setprop(fdt, "/soc/imsics-s", "interrupt-controller", NULL, 0);
    qemu_fdt_setprop(fdt, "/soc/imsics-s", "msi-controller", NULL, 0);
    qemu_fdt_setprop(fdt, "/soc/imsics-s", "interrupts-extended",
                     imsic_cells, imsic_targets * 2 * sizeof(*imsic_cells));
    qemu_fdt_setprop(fdt, "/soc/imsics-s", "riscv,hart-indexes",
                     imsic_hart_indexes,
                     imsic_targets * sizeof(*imsic_hart_indexes));
    qemu_fdt_setprop(fdt, "/soc/imsics-s", "reg",
                     imsic_s_regs, selected_dies * 4 * sizeof(*imsic_s_regs));
    qemu_fdt_setprop_cell(fdt, "/soc/imsics-s", "riscv,hart-index-bits", 4);
    qemu_fdt_setprop_cell(fdt, "/soc/imsics-s", "riscv,group-index-bits",
                          s->single_soc ? 0 : 3);
    qemu_fdt_setprop_cell(fdt, "/soc/imsics-s", "riscv,group-index-shift",
                          KMH_BOSC_DIE_SHIFT);

    {
        int uart_die = kmh_bosc_first_selected_die(s);
        hwaddr uart_base = kmh_bosc_die_memmap_base(s, uart_die,
                                                    KMH_BOSC_UART0);
        g_autofree char *uart_name = g_strdup_printf("/soc/serial@%" HWADDR_PRIx,
                                                     uart_base);

        qemu_fdt_add_subnode(fdt, uart_name);
        qemu_fdt_setprop_string(fdt, uart_name, "compatible", "ns16550a");
        qemu_fdt_setprop_sized_cells(fdt, uart_name, "reg",
                                     2, uart_base,
                                     2, memmap[KMH_BOSC_UART0].size);
        qemu_fdt_setprop_cell(fdt, uart_name, "reg-shift", 2);
        qemu_fdt_setprop_cell(fdt, uart_name, "clock-frequency",
                              KMH_BOSC_UART_CLOCK);
        qemu_fdt_setprop_cell(fdt, uart_name, "current-speed",
                              KMH_BOSC_UART_BAUD);
        qemu_fdt_setprop_cell(fdt, uart_name, "numa-node-id",
                              kmh_bosc_numa_node_id(s, uart_die));
        qemu_fdt_setprop_cell(fdt, uart_name, "interrupt-parent",
                              aplic_s_phandles[uart_die]);
        qemu_fdt_setprop_cells(fdt, uart_name, "interrupts",
                               KMH_BOSC_UART0_IRQ, KMH_BOSC_IRQ_TYPE_LEVEL_HIGH);
        qemu_fdt_setprop_string(fdt, "/chosen", "stdout-path", uart_name);
        qemu_fdt_setprop_string(fdt, "/aliases", "serial0", uart_name);
    }

    if (kmh_bosc_is_iommu_sys_enabled(s)) {
        int die_i, iommu_idx;

        for (die_i = 0; die_i < kmh_bosc_die_count(s); die_i++) {
            if (!kmh_bosc_die_selected(s, die_i)) {
                continue;
            }
            for (iommu_idx = 0; iommu_idx < KMH_BOSC_IOMMU_PER_DIE; iommu_idx++) {
                hwaddr iommu_base =
                    kmh_bosc_die_addr(s, die_i, kmh_bosc_iommu_reg_base[iommu_idx]);
                hwaddr mapper_base =
                    kmh_bosc_die_addr(s, die_i, kmh_bosc_axi4_traffic_base[iommu_idx]);
                g_autofree char *iommu_name =
                    g_strdup_printf("/soc/iommu@%" HWADDR_PRIx, iommu_base);
                g_autofree char *mapper_name =
                    g_strdup_printf("/soc/did-mapper@%" HWADDR_PRIx, mapper_base);
                g_autofree char *traffic_name =
                    g_strdup_printf("/soc/axi4-traffic@%" HWADDR_PRIx,
                                    mapper_base);

                iommu_phandles[die_i][iommu_idx] = qemu_fdt_alloc_phandle(fdt);
                qemu_fdt_add_subnode(fdt, iommu_name);
                qemu_fdt_setprop_cell(fdt, iommu_name, "numa-node-id",
                                      kmh_bosc_numa_node_id(s, die_i));
                qemu_fdt_setprop_string(fdt, iommu_name, "compatible",
                                        "riscv,iommu");
                qemu_fdt_setprop_cell(fdt, iommu_name, "#iommu-cells", 1);
                qemu_fdt_setprop_cell(fdt, iommu_name, "phandle",
                                      iommu_phandles[die_i][iommu_idx]);
                qemu_fdt_setprop_sized_cells(fdt, iommu_name, "reg",
                                             2, iommu_base,
                                             2, KMH_BOSC_IOMMU_MMIO_SIZE);
                qemu_fdt_setprop_cell(fdt, iommu_name, "interrupt-parent",
                                      aplic_s_phandles[die_i]);
                qemu_fdt_setprop_cells(fdt, iommu_name, "interrupts",
                                       KMH_BOSC_IOMMU_IRQ,
                                       KMH_BOSC_IRQ_TYPE_LEVEL_HIGH);
                qemu_fdt_setprop_cell(fdt, iommu_name, "msi-parent",
                                      imsic_s_phandle);
                qemu_fdt_setprop_string(fdt, iommu_name, "status", "okay");

                qemu_fdt_add_subnode(fdt, mapper_name);
                qemu_fdt_setprop_cell(fdt, mapper_name, "numa-node-id",
                                      kmh_bosc_numa_node_id(s, die_i));
                qemu_fdt_setprop_string(fdt, mapper_name, "compatible",
                                        "bosc,did-mapper");
                qemu_fdt_setprop_sized_cells(fdt, mapper_name, "reg",
                                             2, mapper_base,
                                             2, BOSC_DID_MAPPER_MMIO_SIZE);
                qemu_fdt_setprop_string(fdt, mapper_name, "status", "okay");

                qemu_fdt_add_subnode(fdt, traffic_name);
                qemu_fdt_setprop_cell(fdt, traffic_name, "numa-node-id",
                                      kmh_bosc_numa_node_id(s, die_i));
                qemu_fdt_setprop_string(fdt, traffic_name, "compatible",
                                        "bosc,axi4-traffic");
                qemu_fdt_setprop_sized_cells(fdt, traffic_name, "reg",
                                             2, mapper_base,
                                             2, BOSC_AXI4_TRAFFIC_MMIO_SIZE);
                qemu_fdt_setprop_string(fdt, traffic_name, "status", "okay");
            }
        }
    }

    for (die = 0; die < kmh_bosc_die_count(s); die++) {
        hwaddr dmac_base;
        g_autofree char *dmac_name = NULL;
        int port;

        if (!kmh_bosc_die_selected(s, die)) {
            continue;
        }

        dmac_base = kmh_bosc_die_memmap_base(s, die, KMH_BOSC_DMAC);
        dmac_name = g_strdup_printf("/soc/dma-controller@%" HWADDR_PRIx,
                                    dmac_base);
        qemu_fdt_add_subnode(fdt, dmac_name);
        qemu_fdt_setprop_cell(fdt, dmac_name, "numa-node-id",
                              kmh_bosc_numa_node_id(s, die));
        qemu_fdt_setprop_string(fdt, dmac_name, "compatible",
                                "snps,axi-dma-1.01a");
        qemu_fdt_setprop(fdt, dmac_name, "dma-controller", NULL, 0);
        qemu_fdt_setprop_cell(fdt, dmac_name, "#dma-cells", 1);
        qemu_fdt_setprop_cells(fdt, dmac_name, "dma-channels",
                               DW_AXI_DMAC_NR_CHANS);
        qemu_fdt_setprop_cell(fdt, dmac_name, "snps,dma-masters", 1);
        qemu_fdt_setprop_cells(fdt, dmac_name, "snps,data-width", 8);
        qemu_fdt_setprop_sized_cells(fdt, dmac_name, "reg",
                                     2, dmac_base,
                                     2, memmap[KMH_BOSC_DMAC].size);
        qemu_fdt_setprop_cell(fdt, dmac_name, "interrupt-parent",
                              aplic_s_phandles[die]);
        qemu_fdt_setprop_cells(fdt, dmac_name, "interrupts",
                               KMH_BOSC_DMAC_IRQ,
                               KMH_BOSC_IRQ_TYPE_LEVEL_HIGH);
        qemu_fdt_setprop_string(fdt, dmac_name, "status", "okay");

        if (s->dw_pcie) {
            for (port = 0; port < KMH_BOSC_PCIE_PER_DIE; port++) {
                const KmhBoscPciePortMemMap *pcie = &kmh_bosc_pcie_memmap[port];
                const hwaddr dbi_base = kmh_bosc_die_addr(s, die, pcie->dbi.base);
                const hwaddr ecam_base = kmh_bosc_die_addr(s, die, pcie->ecam.base);
                const hwaddr app_base =
                    kmh_bosc_die_addr(s, die, pcie->app.base);
                const hwaddr mem_base = kmh_bosc_die_addr(s, die, pcie->mem.base);
                const int segment = kmh_bosc_pcie_domain(die, port);
                const hwaddr mem32_bus_base =
                    kmh_bosc_pcie_mem32_bus_base(segment);
                const hwaddr mem64_cpu_base =
                    mem_base + KMH_BOSC_PCIE_PREFETCH_OFFSET;
                const int ep_irq = kmh_bosc_pcie_ep_irq(port);
                g_autofree char *pcie_name =
                    g_strdup_printf("/soc/pci@%" HWADDR_PRIx, dbi_base);

                qemu_fdt_add_subnode(fdt, pcie_name);
                qemu_fdt_setprop_cell(fdt, pcie_name, "numa-node-id",
                                      kmh_bosc_numa_node_id(s, die));
                qemu_fdt_setprop_string(fdt, pcie_name, "compatible",
                                        "bosc,kmh-bosc-soc-pcie");
                qemu_fdt_setprop_cell(fdt, pcie_name, "#address-cells", 3);
                qemu_fdt_setprop_cell(fdt, pcie_name, "#size-cells", 2);
                qemu_fdt_setprop_cell(fdt, pcie_name, "#interrupt-cells", 1);
                qemu_fdt_setprop_string(fdt, pcie_name, "device_type", "pci");
                qemu_fdt_setprop_cells(fdt, pcie_name, "bus-range", 0, 0xff);
                qemu_fdt_setprop_cell(fdt, pcie_name, "linux,pci-domain",
                                      segment);
                qemu_fdt_setprop_sized_cells(fdt, pcie_name, "reg",
                                             2, dbi_base,
                                             2, pcie->dbi.size,
                                             2, ecam_base,
                                             2, pcie->ecam.size,
                                             2, app_base,
                                             2, pcie->app.size);
                {
                    static const char *const reg_names[] = {
                        "dbi", "config", "app"
                    };
                    qemu_fdt_setprop_string_array(fdt, pcie_name, "reg-names",
                                                  (char **)&reg_names,
                                                  ARRAY_SIZE(reg_names));
                }
                qemu_fdt_setprop_sized_cells(fdt, pcie_name, "ranges",
                                             1, FDT_PCI_RANGE_MMIO,
                                             2, mem32_bus_base,
                                             2, mem_base,
                                             2, KMH_BOSC_PCIE_WINDOW_SIZE,
                                             1, FDT_PCI_RANGE_MMIO_64BIT |
                                                FDT_PCI_RANGE_PREFETCHABLE,
                                             2, KMH_BOSC_PCIE_MEM_BUS_BASE,
                                             2, mem64_cpu_base,
                                             2, KMH_BOSC_PCIE_WINDOW_SIZE);
                qemu_fdt_setprop_sized_cells(fdt, pcie_name, "dma-ranges",
                                             1, FDT_PCI_RANGE_MMIO,
                                             2, 0x80000000ULL,
                                             2, 0x80000000ULL,
                                             2, 0x80000000ULL);
                qemu_fdt_setprop_cell(fdt, pcie_name, "interrupt-parent",
                                      aplic_s_phandles[die]);
                /* Omit msi-parent to select the DWC integrated receiver. */
                qemu_fdt_setprop(fdt, pcie_name, "snps,imsi-rx-forward",
                                  NULL, 0);
                qemu_fdt_setprop_cells(fdt, pcie_name, "interrupts",
                                       ep_irq,
                                       KMH_BOSC_IRQ_TYPE_LEVEL_HIGH,
                                       kmh_bosc_pcie_sii_irq(port),
                                       KMH_BOSC_IRQ_TYPE_LEVEL_HIGH);
                if (kmh_bosc_is_iommu_sys_enabled(s) &&
                    iommu_phandles[die][port]) {
                    /* Independent per-root IOMMU: RID maps directly. */
                    qemu_fdt_setprop_cells(fdt, pcie_name, "iommu-map",
                                           0, iommu_phandles[die][port],
                                           0, 0x10000);
                }
                {
                    static const char *const interrupt_names[] = { "msi", "hp" };
                    qemu_fdt_setprop_string_array(fdt, pcie_name,
                                                  "interrupt-names",
                                                  (char **)&interrupt_names,
                                                  ARRAY_SIZE(interrupt_names));
                }
                qemu_fdt_setprop_cell(fdt, pcie_name, "num-lanes", 1);
                qemu_fdt_setprop_cell(fdt, pcie_name, "max-link-speed", 1);
                qemu_fdt_setprop_string(fdt, pcie_name, "status", "okay");
                kmh_bosc_add_pcie_irq_map(fdt, pcie_name,
                                          aplic_s_phandles[die], ep_irq);
            }
        }
    }

    kmh_bosc_add_distance_map(fdt, s);
    kmh_bosc_fdt_add_apei_workspaces(fdt);
    if (s->generated_acpi) {
        kmh_bosc_fdt_add_acpi_handoff(s);
    }
    kmh_bosc_add_auto_test_fdt_nodes(s);
}

static void kmh_bosc_realize_hart_arrays(KmhBoscState *s)
{
    MachineState *machine = MACHINE(s);
    const MemMapEntry *memmap = kmh_bosc_memmap;
    bool app_powered_off = kmh_bosc_boot_from_mcu(s);
    int boot_die = kmh_bosc_first_selected_die(s);
    hwaddr app_resetvec = boot_die >= 0 ? kmh_bosc_ddr_base(boot_die) : 0;
    int die;

    for (die = 0; die < kmh_bosc_die_count(s); die++) {
        g_autofree char *name = g_strdup_printf("app-cpus%d", die);
        DeviceState *cpus;

        object_initialize_child(OBJECT(machine), name, &s->app_cpus[die],
                                TYPE_RISCV_HART_ARRAY);
        cpus = DEVICE(&s->app_cpus[die]);
        qdev_prop_set_uint32(cpus, "num-harts", KMH_BOSC_APP_HARTS_PER_DIE);
        qdev_prop_set_uint32(cpus, "hartid-base",
                             die * KMH_BOSC_APP_HARTS_PER_DIE);
        qdev_prop_set_string(cpus, "cpu-type", TYPE_RISCV_CPU_XIANGSHAN_KMH);
        qdev_prop_set_uint64(cpus, "resetvec", app_resetvec);
        qdev_prop_set_bit(cpus, "start-powered-off", app_powered_off);
        object_property_set_link(OBJECT(cpus), "memory",
                                 OBJECT(&s->die_mem[die]), &error_abort);
        sysbus_realize(SYS_BUS_DEVICE(cpus), &error_fatal);
    }

    for (die = 0; die < kmh_bosc_die_count(s); die++) {
        g_autofree char *mcu_name = g_strdup_printf("mcu-cpu%d", die);
        bool mcu_powered_off = !kmh_bosc_boot_from_mcu(s) ||
                               !kmh_bosc_die_selected(s, die);

        object_initialize_child(OBJECT(machine), mcu_name, &s->mcu_cpus[die],
                                TYPE_RISCV_HART_ARRAY);
        qdev_prop_set_uint32(DEVICE(&s->mcu_cpus[die]), "num-harts", 1);
        qdev_prop_set_uint32(DEVICE(&s->mcu_cpus[die]), "hartid-base",
                             KMH_BOSC_MCU_HARTID_BASE + die);
        qdev_prop_set_string(DEVICE(&s->mcu_cpus[die]), "cpu-type",
                             TYPE_RISCV_CPU_BASE32);
        qdev_prop_set_uint64(DEVICE(&s->mcu_cpus[die]), "resetvec",
                             memmap[KMH_BOSC_MCU_BOOTROM].base);
        object_property_set_link(OBJECT(&s->mcu_cpus[die]), "memory",
                                 OBJECT(&s->mcu_local_mem[die]),
                                 &error_abort);
        qdev_prop_set_bit(DEVICE(&s->mcu_cpus[die]), "start-powered-off",
                          mcu_powered_off);
        sysbus_realize(SYS_BUS_DEVICE(&s->mcu_cpus[die]), &error_fatal);
    }

    kmh_bosc_power_off_unselected_app_harts(s);
}

static void kmh_bosc_init_mcu_local_mem(KmhBoscState *s)
{
    const MemMapEntry *memmap = kmh_bosc_memmap;
    int die;

    for (die = 0; die < kmh_bosc_die_count(s); die++) {
        MemoryRegion *dmac_mr =
            sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->dmac[die]), 0);
        g_autofree char *name = NULL;

        name = g_strdup_printf("kmh-bosc.mcu-local-mem.%d", die);
        memory_region_init(&s->mcu_local_mem[die], OBJECT(s), name, UINT64_MAX);

        name = g_strdup_printf("kmh-bosc.mcu-local-system.%d", die);
        memory_region_init_alias(&s->mcu_local_system_alias[die], OBJECT(s),
                                 name, &s->die_mem[die], 0, UINT64_MAX);
        memory_region_add_subregion_overlap(&s->mcu_local_mem[die], 0,
                                            &s->mcu_local_system_alias[die],
                                            -1);

        name = g_strdup_printf("kmh-bosc.mcu-local-bootrom.%d", die);
        memory_region_init_alias(&s->mcu_local_bootrom_alias[die], OBJECT(s),
                                 name, &s->mcu_bootrom[die], 0,
                                 memmap[KMH_BOSC_MCU_BOOTROM].size);
        memory_region_add_subregion_overlap(&s->mcu_local_mem[die],
                                            memmap[KMH_BOSC_MCU_BOOTROM].base,
                                            &s->mcu_local_bootrom_alias[die],
                                            1);

        name = g_strdup_printf("kmh-bosc.mcu-local-sram0.%d", die);
        memory_region_init_alias(&s->mcu_local_sram0_alias[die], OBJECT(s),
                                 name, &s->sram0[die], 0,
                                 memmap[KMH_BOSC_SRAM0].size);
        memory_region_add_subregion_overlap(&s->mcu_local_mem[die],
                                            memmap[KMH_BOSC_SRAM0].base,
                                            &s->mcu_local_sram0_alias[die],
                                            1);

        name = g_strdup_printf("kmh-bosc.mcu-local-sram1.%d", die);
        memory_region_init_alias(&s->mcu_local_sram1_alias[die], OBJECT(s),
                                 name, &s->sram1[die], 0,
                                 memmap[KMH_BOSC_SRAM1].size);
        memory_region_add_subregion_overlap(&s->mcu_local_mem[die],
                                            memmap[KMH_BOSC_SRAM1].base,
                                            &s->mcu_local_sram1_alias[die],
                                            1);

        name = g_strdup_printf("kmh-bosc.mcu-local-qspi-flash-data.%d", die);
        memory_region_init_alias(&s->mcu_local_qspi_flash_data_alias[die],
                                 OBJECT(s), name, &s->qspi_flash_data[die], 0,
                                 memmap[KMH_BOSC_QSPI_FLASH_DATA].size);
        memory_region_add_subregion_overlap(&s->mcu_local_mem[die],
                                            memmap[KMH_BOSC_QSPI_FLASH_DATA].base,
                                            &s->mcu_local_qspi_flash_data_alias[die],
                                            1);

        name = g_strdup_printf("kmh-bosc.mcu-local-bootctl.%d", die);
        memory_region_init_alias(&s->mcu_local_bootctl_alias[die], OBJECT(s),
                                 name, &s->bootctl[die].mr, 0,
                                 memmap[KMH_BOSC_WATCH_DOG].size);
        memory_region_add_subregion_overlap(&s->mcu_local_mem[die],
                                            memmap[KMH_BOSC_WATCH_DOG].base,
                                            &s->mcu_local_bootctl_alias[die],
                                            1);

        name = g_strdup_printf("kmh-bosc.mcu-local-sysctrl.%d", die);
        memory_region_init_alias(&s->mcu_local_sysctrl_alias[die], OBJECT(s),
                                 name, &s->sysctrl[die].mr, 0,
                                 memmap[KMH_BOSC_SYS_CTRL].size);
        memory_region_add_subregion_overlap(&s->mcu_local_mem[die],
                                            memmap[KMH_BOSC_SYS_CTRL].base,
                                            &s->mcu_local_sysctrl_alias[die],
                                            1);

        name = g_strdup_printf("kmh-bosc.mcu-local-dmac.%d", die);
        memory_region_init_alias(&s->mcu_local_dmac_alias[die], OBJECT(s),
                                 name, dmac_mr, 0, DW_AXI_DMAC_MMIO_SIZE);
        memory_region_add_subregion_overlap(&s->mcu_local_mem[die],
                                            memmap[KMH_BOSC_DMAC].base,
                                            &s->mcu_local_dmac_alias[die],
                                            1);
    }
}

static void kmh_bosc_map_local_memory(KmhBoscState *s)
{
    MemoryRegion *system_memory = get_system_memory();
    const MemMapEntry *memmap = kmh_bosc_memmap;
    int die;

    for (die = 0; die < kmh_bosc_die_count(s); die++) {
        g_autofree char *name = NULL;
        hwaddr base = kmh_bosc_die_memmap_base(s, die, KMH_BOSC_MCU_BOOTROM);

        name = g_strdup_printf("kmh-bosc.mcu-bootrom%d", die);
        memory_region_init_rom(&s->mcu_bootrom[die], NULL, name,
                               memmap[KMH_BOSC_MCU_BOOTROM].size, &error_fatal);
        memory_region_add_subregion(system_memory, base, &s->mcu_bootrom[die]);

        name = g_strdup_printf("kmh-bosc.sram0.%d", die);
        memory_region_init_ram(&s->sram0[die], NULL, name,
                               memmap[KMH_BOSC_SRAM0].size, &error_fatal);
        memory_region_add_subregion(system_memory,
                                    kmh_bosc_die_memmap_base(s, die, KMH_BOSC_SRAM0),
                                    &s->sram0[die]);

        name = g_strdup_printf("kmh-bosc.sram1.%d", die);
        memory_region_init_ram(&s->sram1[die], NULL, name,
                               memmap[KMH_BOSC_SRAM1].size, &error_fatal);
        memory_region_add_subregion(system_memory,
                                    kmh_bosc_die_memmap_base(s, die, KMH_BOSC_SRAM1),
                                    &s->sram1[die]);

        name = g_strdup_printf("kmh-bosc.qspi-flash-data.%d", die);
        memory_region_init_rom(&s->qspi_flash_data[die], NULL, name,
                               memmap[KMH_BOSC_QSPI_FLASH_DATA].size,
                               &error_fatal);
        memory_region_add_subregion(system_memory,
                                    kmh_bosc_die_memmap_base(s, die,
                                                             KMH_BOSC_QSPI_FLASH_DATA),
                                    &s->qspi_flash_data[die]);

        s->bootctl[die].machine = s;
        s->bootctl[die].die = die;
        name = g_strdup_printf("kmh-bosc.bootctl.%d", die);
        memory_region_init_io(&s->bootctl[die].mr, NULL,
                              &kmh_bosc_bootctl_ops, &s->bootctl[die],
                              name, memmap[KMH_BOSC_WATCH_DOG].size);
        memory_region_add_subregion(system_memory,
                                    kmh_bosc_die_memmap_base(
                                        s, die, KMH_BOSC_WATCH_DOG),
                                    &s->bootctl[die].mr);

        s->sysctrl[die].machine = s;
        s->sysctrl[die].die = die;
        name = g_strdup_printf("kmh-bosc.sysctrl.%d", die);
        memory_region_init_io(&s->sysctrl[die].mr, NULL,
                              &kmh_bosc_sysctrl_ops, &s->sysctrl[die],
                              name, memmap[KMH_BOSC_SYS_CTRL].size);
        memory_region_add_subregion(system_memory,
                                    kmh_bosc_die_memmap_base(s, die,
                                                             KMH_BOSC_SYS_CTRL),
                                    &s->sysctrl[die].mr);
        kmh_bosc_sysctrl_reset(&s->sysctrl[die]);
        qemu_register_reset(kmh_bosc_sysctrl_reset, &s->sysctrl[die]);

        create_unimplemented_device("kmh-bosc-sys-crg",
                                    kmh_bosc_die_memmap_base(s, die,
                                                             KMH_BOSC_SYS_CRG),
                                    memmap[KMH_BOSC_SYS_CRG].size);
        create_unimplemented_device("kmh-bosc-trace",
                                    kmh_bosc_die_memmap_base(s, die,
                                                             KMH_BOSC_TRACE),
                                    memmap[KMH_BOSC_TRACE].size);
        create_unimplemented_device("kmh-bosc-qspi-flash-ctrl",
                                    kmh_bosc_die_memmap_base(s, die,
                                                             KMH_BOSC_QSPI_FLASH_CTRL),
                                    memmap[KMH_BOSC_QSPI_FLASH_CTRL].size);

        name = g_strdup_printf("kmh-bosc.dmac.%d", die);
        object_initialize_child(OBJECT(s), name, &s->dmac[die],
                                TYPE_DW_AXI_DMAC);
        object_property_set_link(OBJECT(&s->dmac[die]), "dma-memory",
                                 OBJECT(&s->die_mem[die]), &error_abort);
        sysbus_realize(SYS_BUS_DEVICE(&s->dmac[die]), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->dmac[die]), 0,
                        kmh_bosc_die_memmap_base(s, die, KMH_BOSC_DMAC));

        create_unimplemented_device("kmh-bosc-debug",
                                    kmh_bosc_die_memmap_base(s, die, KMH_BOSC_DEBUG),
                                    memmap[KMH_BOSC_DEBUG].size);
        create_unimplemented_device("kmh-bosc-syscnt",
                                    kmh_bosc_die_memmap_base(s, die, KMH_BOSC_SYSCNT),
                                    memmap[KMH_BOSC_SYSCNT].size);
        create_unimplemented_device("kmh-bosc-ddr0-cfg",
                                    kmh_bosc_die_memmap_base(s, die, KMH_BOSC_DDR0_CFG),
                                    memmap[KMH_BOSC_DDR0_CFG].size);
        create_unimplemented_device("kmh-bosc-ddr1-cfg",
                                    kmh_bosc_die_memmap_base(s, die, KMH_BOSC_DDR1_CFG),
                                    memmap[KMH_BOSC_DDR1_CFG].size);
    }
    kmh_bosc_init_mcu_local_mem(s);
}

static void kmh_bosc_map_ddr(KmhBoscState *s)
{
    MachineState *machine = MACHINE(s);
    MemoryRegion *system_memory = get_system_memory();
    uint64_t die_ram_size = kmh_bosc_die_ram_size(machine);
    int die;
    int node = 0;

    for (die = 0; die < kmh_bosc_die_count(s); die++) {
        if (!kmh_bosc_die_selected(s, die)) {
            continue;
        }

        g_autofree char *name = g_strdup_printf("kmh-bosc.ddr%d", die);

        memory_region_init_alias(&s->ddr_alias[die], NULL, name,
                                 machine->ram, node * die_ram_size,
                                 die_ram_size);
        memory_region_add_subregion(system_memory, kmh_bosc_ddr_base(die),
                                    &s->ddr_alias[die]);
        node++;
    }
}

/*
 * Each requester gets a stable address space: die-id 0 aliases its own
 * canonical peripheral windows, while nonzero IDs and DDR remain global.
 * Use real aliases (also for RAM/ROM), never current_cpu-dependent MMIO.
 */
static void kmh_bosc_init_die_address_spaces(KmhBoscState *s)
{
    MemoryRegion *system_memory = get_system_memory();

    for (int die = 0; die < kmh_bosc_die_count(s); die++) {
        int index = 0;
        g_autofree char *name = g_strdup_printf("kmh-bosc.die%d", die);

        memory_region_init(&s->die_mem[die], OBJECT(s), name, UINT64_MAX);
        memory_region_init_alias(&s->die_system_alias[die], OBJECT(s), name,
                                 system_memory, 0, UINT64_MAX);
        memory_region_add_subregion_overlap(&s->die_mem[die], 0,
                                            &s->die_system_alias[die], -1);
        if (s->single_soc) {
            address_space_init(&s->die_as[die], &s->die_mem[die], name);
            continue;
        }
        for (int region = 0; region < ARRAY_SIZE(kmh_bosc_memmap); region++) {
            const MemMapEntry *entry = &kmh_bosc_memmap[region];
            MemoryRegion *alias = &s->die_local_alias[die][index++];

            memory_region_init_alias(alias, OBJECT(s), name, system_memory,
                                     kmh_bosc_die_addr(s, die, entry->base),
                                     entry->size);
            memory_region_add_subregion_overlap(&s->die_mem[die], entry->base,
                                                alias, 1);
        }
        for (int port = 0; port < KMH_BOSC_PCIE_PER_DIE; port++) {
            const KmhBoscPciePortMemMap *pcie = &kmh_bosc_pcie_memmap[port];
            const MemMapEntry *entries[] = {
                &pcie->iopmp, &pcie->dbi, &pcie->app, &pcie->mctp,
                &pcie->ecam, &pcie->mem,
            };

            for (int i = 0; i < ARRAY_SIZE(entries); i++) {
                const MemMapEntry *entry = entries[i];
                MemoryRegion *alias = &s->die_local_alias[die][index++];

                memory_region_init_alias(alias, OBJECT(s), name, system_memory,
                                         kmh_bosc_die_addr(s, die, entry->base),
                                         entry->size);
                memory_region_add_subregion_overlap(&s->die_mem[die],
                                                    entry->base, alias, 1);
            }
        }
        address_space_init(&s->die_as[die], &s->die_mem[die], name);
    }
}

static void kmh_bosc_init_per_die_interrupts(KmhBoscState *s)
{
    hwaddr clint_base = kmh_bosc_memmap[KMH_BOSC_CLINT].base;
    int die;
    int app_harts = kmh_bosc_die_count(s) * KMH_BOSC_APP_HARTS_PER_DIE;

    for (die = 0; die < kmh_bosc_die_count(s); die++) {
        int hartid_base = die * KMH_BOSC_APP_HARTS_PER_DIE;

        s->irqchip[die] = kmh_bosc_create_aia(s, die, hartid_base);
        kmh_bosc_aplic_test_create(
            s->irqchip[die],
            kmh_bosc_die_addr(s, die, KMH_BOSC_APLIC_TEST_MMIO_BASE));
    }

    riscv_aclint_swi_create(clint_base, 0, app_harts, false);
    s->checkpoint.mtimer = riscv_aclint_mtimer_create(
                               clint_base + RISCV_ACLINT_SWI_SIZE,
                               RISCV_ACLINT_DEFAULT_MTIMER_SIZE,
                               0, app_harts,
                               RISCV_ACLINT_DEFAULT_MTIMECMP,
                               RISCV_ACLINT_DEFAULT_MTIME,
                               KMH_BOSC_CLINT_TIMEBASE_FREQ, true);
    if (s->single_soc) {
        return;
    }
    /* Multi-die ACLINT is a shared 64-hart block, also reachable by die ID. */
    for (die = 0; die < kmh_bosc_die_count(s); die++) {
        memory_region_init_alias(&s->clint_alias[die], OBJECT(s),
                                 "kmh-bosc.clint", get_system_memory(),
                                 clint_base,
                                 kmh_bosc_memmap[KMH_BOSC_CLINT].size);
        memory_region_add_subregion(get_system_memory(),
                                    kmh_bosc_die_addr(s, die, clint_base),
                                    &s->clint_alias[die]);
    }
}

static void kmh_bosc_init_dmac_irqs(KmhBoscState *s)
{
    int die;

    for (die = 0; die < kmh_bosc_die_count(s); die++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->dmac[die]), 0,
                           qdev_get_gpio_in(s->irqchip[die],
                                            KMH_BOSC_DMAC_IRQ));
    }
}

static void kmh_bosc_init_uart(KmhBoscState *s)
{
    for (int die = 0; die < kmh_bosc_die_count(s); die++) {
        s->checkpoint.uart[s->checkpoint.num_uart++] =
            serial_mm_init(get_system_memory(),
                       kmh_bosc_die_memmap_base(s, die, KMH_BOSC_UART0), 2,
                       qdev_get_gpio_in(s->irqchip[die], KMH_BOSC_UART0_IRQ),
                       KMH_BOSC_UART_BAUD, serial_hd(die),
                       DEVICE_LITTLE_ENDIAN);
    }
}

static AddressSpace *kmh_bosc_pcie_dma_target(KmhBoscPcieRequester *r)
{
    /* Bypass skips both identity overrides and IOMMU translation. */
    if (bosc_did_mapper_get_mux(r->port->mapper) &
        BOSC_DID_MAPPER_MUX_BYPASS) {
        return r->port->die_as;
    }
    return r->downstream;
}

static MemTxResult kmh_bosc_pcie_msi_write(void *opaque, hwaddr addr,
                                         uint64_t data, unsigned size,
                                         MemTxAttrs attrs)
{
    KmhBoscPcieRequester *r = opaque;
    MemTxResult receiver, downstream;

    /* Observe the full PCI address before the 44-bit AXI master. */
    address_space_stl_le(&r->port->msi_as, addr, data, attrs, &receiver);
    /* Keep this endpoint's requester identity for the forwarded write. */
    address_space_stl_le(kmh_bosc_pcie_dma_target(r),
                         addr & MAKE_64BIT_MASK(0, 44), data, attrs,
                         &downstream);
    return receiver | downstream;
}

static const MemoryRegionOps kmh_bosc_pcie_msi_ops = {
    .write_with_attrs = kmh_bosc_pcie_msi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

/* iMSI-RX monitors writes before AXI truncation and forwards them to DMA. */
static IOMMUTLBEntry kmh_bosc_pcie_dma_translate(IOMMUMemoryRegion *mr,
                                               hwaddr addr,
                                               IOMMUAccessFlags flag,
                                               int iommu_idx)
{
    KmhBoscPcieRequester *r = container_of(mr, KmhBoscPcieRequester, mr);
    DesignwarePCIEMSI *msi = &r->port->host->root.msi;
    hwaddr mask = 0xfff;
    AddressSpace *target = kmh_bosc_pcie_dma_target(r);
    hwaddr translated = addr & MAKE_64BIT_MASK(0, 44);

    if ((flag & IOMMU_WO) && msi->enabled &&
        (addr & ~mask) == (msi->base & ~mask)) {
        /* Do not map DMA across the four-byte receiver in the same page. */
        mask = 3;
        if ((addr & ~mask) == msi->base) {
            target = &r->msi_as;
            translated = addr;
        }
    }

    return (IOMMUTLBEntry) {
        .target_as = target,
        .iova = addr & ~mask,
        .translated_addr = translated & ~mask,
        .addr_mask = mask,
        .perm = flag,
    };
}

static uint64_t kmh_bosc_pcie_dma_min_page_size(IOMMUMemoryRegion *mr)
{
    return 4;
}

static void kmh_bosc_pcie_dma_class_init(ObjectClass *klass, const void *data)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_CLASS(klass);

    imrc->translate = kmh_bosc_pcie_dma_translate;
    imrc->get_min_page_size = kmh_bosc_pcie_dma_min_page_size;
}

static AddressSpace *kmh_bosc_pcie_dma_as(PCIBus *bus, void *opaque, int devfn)
{
    KmhBoscPcieDma *port = opaque;
    KmhBoscPcieRequester *requester;
    g_autofree char *name = NULL;

    for (requester = port->requesters; requester;
         requester = requester->next) {
        if (requester->bus == bus && requester->devfn == devfn) {
            return &requester->as;
        }
    }

    requester = g_new0(KmhBoscPcieRequester, 1);
    name = g_strdup_printf("kmh-bosc-pcie-d%d-p%d-dma-%02x",
                           port->die, port->port, devfn);
    memory_region_init_iommu(&requester->mr, sizeof(requester->mr),
                             TYPE_KMH_BOSC_PCIE_DMA, OBJECT(port->host),
                             name, UINT64_MAX);
    requester->port = port;
    requester->bus = bus;
    requester->devfn = devfn;
    requester->downstream = port->downstream_ops ?
        port->downstream_ops->get_address_space(bus, port->downstream_opaque,
                                                devfn) : port->die_as;
    address_space_init(&requester->as, MEMORY_REGION(&requester->mr), name);
    memory_region_init_io(&requester->msi_monitor, OBJECT(port->host),
                           &kmh_bosc_pcie_msi_ops, requester,
                           "kmh-bosc-pcie-msi-monitor", UINT64_MAX);
    address_space_init(&requester->msi_as, &requester->msi_monitor, name);
    requester->next = port->requesters;
    port->requesters = requester;
    return &requester->as;
}

static const PCIIOMMUOps kmh_bosc_pcie_dma_ops = {
    .get_address_space = kmh_bosc_pcie_dma_as,
};

static void kmh_bosc_init_irq_or(KmhBoscState *s, OrIRQState *or_irq,
                                  const char *name, unsigned inputs,
                                  unsigned die, unsigned source)
{
    object_initialize_child(OBJECT(s), name, or_irq, TYPE_OR_IRQ);
    qdev_prop_set_uint16(DEVICE(or_irq), "num-lines", inputs);
    qdev_realize(DEVICE(or_irq), NULL, &error_fatal);
    qdev_connect_gpio_out(DEVICE(or_irq), 0,
                         qdev_get_gpio_in(s->irqchip[die], source));
}

static void kmh_bosc_init_pcie(KmhBoscState *s)
{
    const MemMapEntry *memmap = kmh_bosc_memmap;
    int die, port;

    for (die = 0; die < kmh_bosc_die_count(s); die++) {
        if (!kmh_bosc_die_selected(s, die)) {
            continue;
        }

        for (port = 0; port < KMH_BOSC_PCIE_PER_DIE; port++) {
            const KmhBoscPciePortMemMap *pcie_map = &kmh_bosc_pcie_memmap[port];
            const hwaddr dbi_base = kmh_bosc_die_addr(s, die, pcie_map->dbi.base);
            const hwaddr app_base = kmh_bosc_die_addr(s, die, pcie_map->app.base);
            const hwaddr ecam_base = kmh_bosc_die_addr(s, die, pcie_map->ecam.base);
            const hwaddr mctp_base = kmh_bosc_die_addr(s, die, pcie_map->mctp.base);
            const hwaddr iopmp_base = kmh_bosc_die_addr(s, die, pcie_map->iopmp.base);
            DesignwarePCIEHost *host = &s->pcie[die][port];
            KmhBoscPcieApp *app = &s->pcie_app[die][port];
            OrIRQState *ep_irq = &s->pcie_ep_irq[die][port];
            MemoryRegion *outbound = &s->pcie_outbound[die][port];
            MemoryRegion *ecam_alias = &s->pcie_ecam_alias[die][port];
            MemoryRegion *mem_alias = &s->pcie_mem_alias[die][port];
            g_autofree char *child_name = g_strdup_printf("pcie-d%d-p%d", die, port);
            g_autofree char *irq_name =
                g_strdup_printf("pcie-d%d-p%d-ep-irq", die, port);
            g_autofree char *root_bus_name =
                g_strdup_printf("pcie-d%d-p%d-root", die, port);
            g_autofree char *sec_bus_name =
                g_strdup_printf("pcie-d%d-p%d", die, port);
            g_autofree char *root_bus_path =
                g_strdup_printf("%04x:00", kmh_bosc_pcie_domain(die, port));
            g_autofree char *dbi_rest_name =
                g_strdup_printf("kmh-bosc-pcie-d%d-p%d-dbi-rest", die, port);
            g_autofree char *app_name =
                g_strdup_printf("kmh-bosc-pcie-d%d-p%d-app", die, port);
            g_autofree char *mctp_name =
                g_strdup_printf("kmh-bosc-pcie-d%d-p%d-mctp", die, port);
            g_autofree char *iopmp_name =
                g_strdup_printf("kmh-bosc-pcie-d%d-p%d-iopmp", die, port);
            SysBusDevice *pcie;

            /*
             * The fabric selects the RC using the global die ID, then
             * removes that ID before the RC's outbound iATU comparison.
             * Keep each RC's local viewports in a separate container so
             * identical BASE/LIMIT values on different dies cannot alias.
             */
            memory_region_init(outbound, OBJECT(s), child_name,
                               1ULL << KMH_BOSC_DIE_SHIFT);
            object_initialize_child(OBJECT(MACHINE(s)), child_name, host,
                                    TYPE_DESIGNWARE_PCIE_HOST);
            object_property_set_link(OBJECT(host), "outbound-memory",
                                     OBJECT(outbound), &error_fatal);
            qdev_prop_set_string(DEVICE(host), "root-bus-name", root_bus_name);
            qdev_prop_set_string(DEVICE(host), "sec-bus-name", sec_bus_name);
            qdev_prop_set_string(DEVICE(host), "root-bus-path", root_bus_path);
            qdev_prop_set_uint32(DEVICE(host), "num-msi-vectors", 256);

            pcie = SYS_BUS_DEVICE(host);
            sysbus_realize(pcie, &error_fatal);
            sysbus_mmio_map(pcie, 0, dbi_base);
            kmh_bosc_init_irq_or(s, ep_irq, irq_name, PCI_NUM_PINS + 1,
                                 die, kmh_bosc_pcie_ep_irq(port));
            object_initialize_child(OBJECT(MACHINE(s)), app_name, app,
                                    TYPE_KMH_BOSC_PCIE_APP);
            sysbus_realize(SYS_BUS_DEVICE(app), &error_fatal);
            sysbus_mmio_map(SYS_BUS_DEVICE(app), 0, app_base);
            sysbus_connect_irq(SYS_BUS_DEVICE(app), 0,
                                qdev_get_gpio_in(DEVICE(ep_irq), PCI_NUM_PINS));
            for (unsigned pin = 0; pin < PCI_NUM_PINS; pin++) {
                sysbus_connect_irq(pcie, pin,
                                   qdev_get_gpio_in_named(DEVICE(app),
                                                          "intx", pin));
                qdev_connect_gpio_out_named(DEVICE(app), "intx-out", pin,
                                            qdev_get_gpio_in(DEVICE(ep_irq),
                                                             pin));
            }
            for (unsigned bank = 0;
                 bank < DESIGNWARE_PCIE_NUM_MSI_BANKS; bank++) {
                qdev_connect_gpio_out_named(DEVICE(host), "msi-bank", bank,
                                            qdev_get_gpio_in_named(DEVICE(app),
                                                                   "msi-bank",
                                                                   bank));
            }
            /*
             * DWC installs an inbound-iATU address space as PCI's default
             * IOMMU. Install the board DMA filter after the optional system
             * IOMMU has registered its per-requester address spaces.
             */
            PCI_HOST_BRIDGE(host)->bus->iommu_ops = NULL;
            PCI_HOST_BRIDGE(host)->bus->iommu_opaque = NULL;
            /* Root configuration is local; downstream CFG needs an iATU. */
            pcie_host_mmcfg_init(PCIE_HOST_BRIDGE(host),
                                 PCIE_MMCFG_SIZE_MIN);
            memory_region_add_subregion(outbound, pcie_map->ecam.base,
                                         &PCIE_HOST_BRIDGE(host)->mmio);
            memory_region_init_alias(ecam_alias, OBJECT(s), child_name,
                                     outbound, pcie_map->ecam.base,
                                     pcie_map->ecam.size);
            memory_region_add_subregion(get_system_memory(), ecam_base,
                                         ecam_alias);
            memory_region_init_alias(mem_alias, OBJECT(s), child_name,
                                     outbound, pcie_map->mem.base,
                                     pcie_map->mem.size);
            memory_region_add_subregion(get_system_memory(),
                                         kmh_bosc_die_addr(s, die,
                                                            pcie_map->mem.base),
                                         mem_alias);

            create_unimplemented_device(dbi_rest_name, dbi_base + 0x1000,
                                        pcie_map->dbi.size - 0x1000);
            create_unimplemented_device(mctp_name, mctp_base,
                                        pcie_map->mctp.size);
            create_unimplemented_device(iopmp_name, iopmp_base,
                                        pcie_map->iopmp.size);
        }

        {
            g_autofree char *phy_name =
                g_strdup_printf("kmh-bosc-pcie-d%d-phy", die);
            create_unimplemented_device(phy_name,
                                        kmh_bosc_die_memmap_base(s, die,
                                                                 KMH_BOSC_PCIE_PHY),
                                        memmap[KMH_BOSC_PCIE_PHY].size);
        }
    }
}


static void kmh_bosc_init_iommu(KmhBoscState *s)
{
    const MemMapEntry *memmap = kmh_bosc_memmap;
    int die, idx;

    for (die = 0; die < kmh_bosc_die_count(s); die++) {
        if (!kmh_bosc_die_selected(s, die)) {
            continue;
        }

        g_autofree char *irq_name = g_strdup_printf("iommu-d%d-irq", die);
        OrIRQState *irq_or = &s->iommu_irq[die];

        kmh_bosc_init_irq_or(s, irq_or, irq_name,
                             KMH_BOSC_IOMMU_PER_DIE *
                             KMH_BOSC_IOMMU_IRQS_PER_INST,
                             die, KMH_BOSC_IOMMU_IRQ);
        for (idx = 0; idx < KMH_BOSC_IOMMU_PER_DIE; idx++) {
            DeviceState *iommu_sys = qdev_new(TYPE_RISCV_IOMMU_SYS);
            Object *iommu_obj;
            RISCVIOMMUState *iommu;
            DeviceState *mapper_dev = qdev_new(TYPE_BOSC_DID_MAPPER);
            BoscDidMapperState *mapper;
            DeviceState *traffic_dev = qdev_new(TYPE_BOSC_AXI4_TRAFFIC);
            hwaddr iommu_base =
                kmh_bosc_die_addr(s, die, kmh_bosc_iommu_reg_base[idx]);
            hwaddr traffic_base =
                kmh_bosc_die_addr(s, die, kmh_bosc_axi4_traffic_base[idx]);
            MemoryRegion *window = &s->traffic_window[die][idx];

            object_property_set_uint(OBJECT(iommu_sys), "addr", iommu_base,
                                     &error_fatal);
            object_property_set_uint(OBJECT(iommu_sys), "base-irq",
                                     idx * KMH_BOSC_IOMMU_IRQS_PER_INST,
                                     &error_fatal);
            object_property_set_link(OBJECT(iommu_sys), "irqchip",
                                     OBJECT(irq_or),
                                     &error_fatal);

            iommu_obj = object_resolve_path_component(OBJECT(iommu_sys),
                                                      "iommu");
            if (!iommu_obj) {
                error_report("failed to resolve IOMMU die %d idx %d", die, idx);
                exit(1);
            }
            iommu = RISCV_IOMMU(iommu_obj);
            /*
             * Per-die / per-root IOMMUs are independent: requester ID is the
             * PCI RID. DID overrides come from the paired did-mapper.
             */
            sysbus_realize_and_unref(SYS_BUS_DEVICE(iommu_sys), &error_fatal);

            object_property_set_link(OBJECT(mapper_dev), "iommu", OBJECT(iommu),
                                     &error_fatal);
            qdev_prop_set_bit(mapper_dev, "bypass-on-reset", s->iommu_bypass);
            sysbus_realize(SYS_BUS_DEVICE(mapper_dev), &error_fatal);
            mapper = BOSC_DID_MAPPER(mapper_dev);
            s->pcie_dma[die][idx].mapper = mapper;
            iommu->did_mapper_apply = bosc_did_mapper_apply;
            iommu->did_mapper_address_mask = bosc_did_mapper_address_mask;
            iommu->did_mapper_opaque = mapper;

            object_property_set_link(OBJECT(traffic_dev), "iommu",
                                     OBJECT(iommu), &error_fatal);
            object_property_set_link(OBJECT(traffic_dev), "did-mapper",
                                     OBJECT(mapper), &error_fatal);
            sysbus_realize(SYS_BUS_DEVICE(traffic_dev), &error_fatal);

            memory_region_init(window, OBJECT(s),
                               "kmh-bosc.traffic-window",
                               BOSC_AXI4_TRAFFIC_MMIO_SIZE);
            memory_region_add_subregion(
                window, 0,
                sysbus_mmio_get_region(SYS_BUS_DEVICE(mapper_dev), 0));
            memory_region_add_subregion(
                window, BOSC_DID_MAPPER_MMIO_SIZE,
                sysbus_mmio_get_region(SYS_BUS_DEVICE(traffic_dev), 0));
            memory_region_add_subregion(get_system_memory(), traffic_base,
                                        window);

            if (!s->single_soc) {
                MemoryRegion *alias = &s->traffic_local_alias[die][idx];

                memory_region_init_alias(alias, OBJECT(s),
                                         "kmh-bosc.traffic-local",
                                         get_system_memory(), traffic_base,
                                         BOSC_AXI4_TRAFFIC_MMIO_SIZE);
                memory_region_add_subregion(&s->die_mem[die],
                                            kmh_bosc_axi4_traffic_base[idx],
                                            alias);
            }

            object_unref(OBJECT(traffic_dev));
            object_unref(OBJECT(mapper_dev));
            s->iommu[die][idx] = iommu;
        }

        if (s->dw_pcie) {
            for (idx = 0; idx < KMH_BOSC_PCIE_PER_DIE; idx++) {
                PCIBus *bus = PCI_HOST_BRIDGE(&s->pcie[die][idx])->bus;

                if (!bus || bus->iommu_ops || bus->iommu_opaque) {
                    error_report("cannot attach IOMMU to PCIe die %d port %d",
                                 die, idx);
                    exit(1);
                }
                riscv_iommu_pci_setup_iommu(s->iommu[die][idx], bus,
                                           &error_fatal);
            }
        }
    }

    create_unimplemented_device("kmh-bosc-iommu-cfg1",
                                kmh_bosc_die_memmap_base(
                                    s, 0, KMH_BOSC_IOMMU_CFG1),
                                memmap[KMH_BOSC_IOMMU_CFG1].size);
}

static void kmh_bosc_init_pcie_dma(KmhBoscState *s)
{
    for (int die = 0; die < kmh_bosc_die_count(s); die++) {
        if (!kmh_bosc_die_selected(s, die)) {
            continue;
        }
        for (int port = 0; port < KMH_BOSC_PCIE_PER_DIE; port++) {
            KmhBoscPcieDma *dma = &s->pcie_dma[die][port];
            DesignwarePCIEHost *host = &s->pcie[die][port];
            PCIBus *bus = PCI_HOST_BRIDGE(host)->bus;

            dma->host = host;
            dma->die = die;
            dma->port = port;
            dma->die_as = &s->die_as[die];
            dma->downstream_ops = bus->iommu_ops;
            dma->downstream_opaque = bus->iommu_opaque;
            /* The receiver's region retains its programmed MSI address. */
            address_space_init(&dma->msi_as, &host->root.msi.iomem,
                               host->sec_bus_name);
            pci_setup_iommu(bus, &kmh_bosc_pcie_dma_ops, dma);
        }
    }
}

static void kmh_bosc_load_mcu_bios(KmhBoscState *s)
{
    const MemMapEntry *memmap = kmh_bosc_memmap;
    g_autofree char *mcu_bios = NULL;
    int die;

    if (!s->mcu_bios || !*s->mcu_bios) {
        if (kmh_bosc_boot_from_mcu(s)) {
            error_report("boot-source=mcu requires -M kmh-bosc-soc,mcu-bios=<raw-bin>");
            exit(1);
        }
        return;
    }

    mcu_bios = riscv_find_firmware(s->mcu_bios, NULL);
    if (!mcu_bios) {
        error_report("could not find MCU boot image '%s'", s->mcu_bios);
        exit(1);
    }

    for (die = 0; die < kmh_bosc_die_count(s); die++) {
        hwaddr bootrom = kmh_bosc_die_memmap_base(s, die, KMH_BOSC_MCU_BOOTROM);
        ssize_t size = load_image_targphys_as(mcu_bios, bootrom,
                                              memmap[KMH_BOSC_MCU_BOOTROM].size,
                                              NULL);

        if (size <= 0) {
            error_report("could not load MCU boot image '%s' as raw binary",
                         s->mcu_bios);
            exit(1);
        }
    }
}

static void kmh_bosc_load_mcu_payload(KmhBoscState *s)
{
    MachineState *machine = MACHINE(s);
    const MemMapEntry *memmap = kmh_bosc_memmap;
    g_autofree char *firmware = NULL;
    int die;

    if (!kmh_bosc_has_fw_payload(machine)) {
        error_report("boot-source=mcu requires -bios fw_payload.bin");
        exit(1);
    }

    if (machine->kernel_filename) {
        error_report("boot-source=mcu does not support -kernel");
        exit(1);
    }

    firmware = riscv_find_firmware(machine->firmware, NULL);
    if (!firmware) {
        error_report("could not find MCU payload '%s'", machine->firmware);
        exit(1);
    }

    for (die = 0; die < kmh_bosc_die_count(s); die++) {
        hwaddr flash_base = kmh_bosc_die_memmap_base(s, die,
                                                     KMH_BOSC_QSPI_FLASH_DATA);
        ssize_t size = load_image_targphys_as(firmware, flash_base,
                                              memmap[KMH_BOSC_QSPI_FLASH_DATA].size,
                                              NULL);

        if (size <= 0) {
            error_report("could not load MCU payload '%s' as raw binary",
                         machine->firmware);
            exit(1);
        }
    }
}

static void kmh_bosc_load_mcu_fdt(KmhBoscState *s)
{
    MachineState *machine = MACHINE(s);
    RISCVBootInfo boot_info;
    int boot_die = kmh_bosc_first_selected_die(s);
    uint64_t boot_window_size = kmh_bosc_die_ram_size(machine);
    uint64_t fdt_addr;

    if (boot_die < 0) {
        error_report("boot-source=mcu requires a selected boot die");
        exit(1);
    }

    riscv_boot_info_init(&boot_info, &s->app_cpus[boot_die]);
    fdt_addr = kmh_bosc_compute_fdt_addr(kmh_bosc_ddr_base(boot_die),
                                         boot_window_size,
                                         machine, &boot_info);
    if (fdt_addr < KMH_BOSC_MCU_SYNC_FLAG_ADDR + sizeof(uint32_t)) {
        error_report("not enough memory in the boot DDR node to place MCU FDT");
        exit(1);
    }

    if (machine->kernel_cmdline && *machine->kernel_cmdline) {
        qemu_fdt_setprop_string(machine->fdt, "/chosen", "bootargs",
                                machine->kernel_cmdline);
    }
    riscv_load_fdt(fdt_addr, machine->fdt);
    s->mcu_fdt_addr = fdt_addr;
}

static void kmh_bosc_init_mcu_die_num(KmhBoscState *s)
{
    int die;
    uint32_t die_num = kmh_bosc_selected_die_count(s);

    for (die = 0; die < kmh_bosc_die_count(s); die++) {
        if (kmh_bosc_die_selected(s, die)) {
            s->sysctrl[die].mcu_die_num = die_num;
        }
    }
}

static void kmh_bosc_validate(MachineState *machine)
{
    KmhBoscState *s = KMH_BOSC_MACHINE(machine);
    uint64_t die_ram_size = kmh_bosc_die_ram_size(machine);
    int dies = kmh_bosc_die_count(s);
    int app_harts = dies * KMH_BOSC_APP_HARTS_PER_DIE;

    if (s->single_soc) {
        /* die-mask selects population only in multi mode. */
        s->die_mask = 1;
        for (int die = 1; die < KMH_BOSC_DIES; die++) {
            if (s->per_die_core_mask && s->core_mask[die]) {
                error_report("soc-mode=single only supports die0 core-mask");
                exit(1);
            }
            s->core_mask[die] = 0;
        }
    }

    if (kvm_enabled()) {
        error_report("kmh-bosc-soc currently requires TCG");
        exit(1);
    }

    if (machine->smp.cpus != app_harts) {
        error_report("kmh-bosc-soc soc-mode=%s requires -smp %d",
                     s->single_soc ? "single" : "multi", app_harts);
        exit(1);
    }

    if (machine->smp.max_cpus < app_harts + dies) {
        error_report("kmh-bosc-soc requires maxcpus >= %d", app_harts + dies);
        exit(1);
    }

    if (!s->die_mask || (s->die_mask & ~KMH_BOSC_DIE_MASK_BITS)) {
        error_report("die-mask must be a non-zero 4-bit mask");
        exit(1);
    }

    if (!kmh_bosc_selected_hart_count(s)) {
        error_report("core-mask must enable at least one app hart across the selected dies");
        exit(1);
    }

    if (machine->ram_size > dies * KMH_BOSC_DDR_NODE_STRIDE) {
        error_report("RAM size exceeds %d GiB DDR aperture", dies * 32);
        exit(1);
    }

    if (machine->ram_size % dies) {
        error_report("RAM size must be divisible by the die count");
        exit(1);
    }

    if (machine->initrd_filename) {
        error_report("initrd is not supported yet on kmh-bosc-soc");
        exit(1);
    }

    if (machine->dtb && (s->generated_dtb == ON_OFF_AUTO_ON ||
                         s->autotest_dtb)) {
        error_report("-dtb cannot be combined with generated-dtb=on or "
                     "autotest-dtb=on");
        exit(1);
    }

    if (s->autotest_dtb && s->generated_dtb != ON_OFF_AUTO_ON) {
        error_report("autotest-dtb=on requires generated-dtb=on");
        exit(1);
    }

    if (s->autotest_dtb && kmh_bosc_boot_from_mcu(s)) {
        error_report("autotest-dtb is only supported with boot-source=ddr");
        exit(1);
    }

#ifndef CONFIG_ACPI
    if (s->generated_acpi) {
        error_report("generated-acpi=on requires CONFIG_ACPI");
        exit(1);
    }
#endif

    if (s->generated_acpi &&
        (!QEMU_IS_ALIGNED(s->acpi_handoff_addr, 16) ||
         !QEMU_IS_ALIGNED(s->acpi_handoff_size, 16) ||
         !kmh_bosc_range_in_selected_ddr(s, s->acpi_handoff_addr,
                                         s->acpi_handoff_size))) {
        error_report("acpi-handoff-addr/size must describe a non-empty "
                     "16-byte aligned range inside selected DDR");
        exit(1);
    }

    if (kmh_bosc_boot_from_mcu(s)) {
        if (!s->mcu_bios || !*s->mcu_bios) {
            error_report("boot-source=mcu requires -M kmh-bosc-soc,mcu-bios=<raw-bin>");
            exit(1);
        }

        if (!kmh_bosc_has_fw_payload(machine)) {
            error_report("boot-source=mcu requires -bios fw_payload.bin");
            exit(1);
        }

        if (machine->kernel_filename) {
            error_report("boot-source=mcu does not support -kernel");
            exit(1);
        }

        if (!kmh_bosc_die_selected(s, 0)) {
            error_report("boot-source=mcu requires die-mask to include die0");
            exit(1);
        }

        if (die_ram_size < (128 * MiB + sizeof(uint32_t))) {
            error_report("boot-source=mcu requires enough per-die DDR for a 128 MiB payload plus sync flag");
            exit(1);
        }
    } else if (!s->checkpoint.checkpoint_load) {
        if (kmh_bosc_firmware_is_none(machine)) {
            if (!machine->kernel_filename) {
                error_report("boot-source=ddr with -bios none requires -kernel <elf>");
                exit(1);
            }

            if (!kmh_bosc_should_generate_dtb(s) && !machine->dtb) {
                error_report("boot-source=ddr with -bios none -kernel requires "
                             "generated-dtb=on or -dtb <file>");
                exit(1);
            }
        } else if (machine->kernel_filename) {
            error_report("boot-source=ddr direct -kernel requires -bios none");
            exit(1);
        }
    }
}

static void kmh_bosc_load_app_boot(KmhBoscState *s)
{
    MachineState *machine = MACHINE(s);
    RISCVBootInfo boot_info;
    int boot_die = kmh_bosc_first_selected_die(s);
    uint64_t start_addr = kmh_bosc_ddr_base(boot_die);
    uint64_t boot_window_size = kmh_bosc_die_ram_size(machine);
    uint64_t firmware_end_addr;
    uint64_t kernel_start_addr = 0;
    uint64_t fdt_addr;
    bool direct_kernel = kmh_bosc_ddr_direct_kernel(machine);

    riscv_boot_info_init(&boot_info, &s->app_cpus[boot_die]);
    firmware_end_addr = start_addr;
    if (!direct_kernel) {
        firmware_end_addr = riscv_find_and_load_firmware(machine,
                                riscv_default_firmware_name(
                                    &s->app_cpus[boot_die]),
                                &start_addr, NULL);
        if (firmware_end_addr > (kmh_bosc_ddr_base(boot_die) + boot_window_size)) {
            error_report("firmware does not fit in the boot DDR node");
            exit(1);
        }
    }

    if (machine->kernel_filename) {
        kernel_start_addr = riscv_calc_kernel_start_addr(&boot_info,
                                                         firmware_end_addr);
        if (kernel_start_addr >= (kmh_bosc_ddr_base(boot_die) + boot_window_size)) {
            error_report("kernel load address exceeds the boot DDR node");
            exit(1);
        }

        riscv_load_kernel(machine, &boot_info, kernel_start_addr, false, NULL);
        if (boot_info.image_high_addr > (kmh_bosc_ddr_base(boot_die) + boot_window_size)) {
            error_report("kernel image does not fit in the boot DDR node");
            exit(1);
        }

        if (direct_kernel) {
            kmh_bosc_set_app_resetvec(s, boot_info.image_low_addr);
        }
    }

    kmh_bosc_set_app_hartid_arg(s);
    if (machine->fdt && (s->checkpoint.checkpoint_path ||
                         s->checkpoint.checkpoint_interval)) {
        const uint64_t regions[][2] = {
            { KMH_NATIVE_WORK, KMH_NATIVE_WORK_SIZE },
            { s->checkpoint.cpu_state_base, KMH_BOSC_CPT_CPU_SIZE },
            { KMH_BOSC_CPT_DEVICE_BASE, KMH_BOSC_CPT_DEVICE_SIZE },
        };
        kmh_bosc_fdt_ensure_reserved_memory(machine->fdt);
        for (unsigned int i = 0; i < ARRAY_SIZE(regions); i++) {
            g_autofree char *name = g_strdup_printf(
                "/reserved-memory/kmh-checkpoint@%" PRIx64, regions[i][0]);
            qemu_fdt_add_path(machine->fdt, name);
            qemu_fdt_setprop_sized_cells(machine->fdt, name, "reg",
                                         2, regions[i][0], 2, regions[i][1]);
            qemu_fdt_setprop(machine->fdt, name, "no-map", NULL, 0);
        }
    }
    if (machine->fdt) {
        fdt_addr = kmh_bosc_compute_fdt_addr(kmh_bosc_ddr_base(boot_die),
                                             boot_window_size,
                                             machine, &boot_info);
        riscv_load_fdt(fdt_addr, machine->fdt);
        s->mcu_fdt_addr = fdt_addr;
        kmh_bosc_set_app_boot_args(s, fdt_addr);
    }
}

static char *kmh_bosc_get_soc_mode(Object *obj, Error **errp)
{
    KmhBoscState *s = KMH_BOSC_MACHINE(obj);

    return g_strdup(s->single_soc ? "single" : "multi");
}

static void kmh_bosc_set_soc_mode(Object *obj, const char *value, Error **errp)
{
    KmhBoscState *s = KMH_BOSC_MACHINE(obj);

    if (!value || (strcmp(value, "single") && strcmp(value, "multi"))) {
        error_setg(errp, "soc-mode must be single or multi");
        return;
    }
    s->single_soc = !strcmp(value, "single");
}

static char *kmh_bosc_get_boot_source(Object *obj, Error **errp)
{
    KmhBoscState *s = KMH_BOSC_MACHINE(obj);

    return g_strdup(s->boot_source);
}

static void kmh_bosc_set_boot_source(Object *obj, const char *value,
                                     Error **errp)
{
    KmhBoscState *s = KMH_BOSC_MACHINE(obj);

    if (strcmp(value, "ddr") && strcmp(value, "mcu")) {
        error_setg(errp, "invalid boot-source '%s'", value);
        error_append_hint(errp, "Valid values are ddr and mcu.\n");
        return;
    }

    g_free(s->boot_source);
    s->boot_source = g_strdup(value);
}

static char *kmh_bosc_get_mcu_bios(Object *obj, Error **errp)
{
    KmhBoscState *s = KMH_BOSC_MACHINE(obj);

    return g_strdup(s->mcu_bios ? s->mcu_bios : "");
}

static void kmh_bosc_set_mcu_bios(Object *obj, const char *value,
                                  Error **errp)
{
    KmhBoscState *s = KMH_BOSC_MACHINE(obj);

    g_free(s->mcu_bios);
    s->mcu_bios = (value && *value) ? g_strdup(value) : NULL;
}

static char *kmh_bosc_get_core_mask(Object *obj, Error **errp)
{
    KmhBoscState *s = KMH_BOSC_MACHINE(obj);

    return g_strdup_printf("0x%x:0x%x:0x%x:0x%x",
                           s->core_mask[0], s->core_mask[1],
                           s->core_mask[2], s->core_mask[3]);
}

static void kmh_bosc_set_core_mask(Object *obj, const char *value, Error **errp)
{
    KmhBoscState *s = KMH_BOSC_MACHINE(obj);
    uint32_t masks[KMH_BOSC_DIES];
    char **parts;
    int nr_parts = 0;
    int die;

    if (!value || !*value) {
        error_setg(errp, "core-mask must not be empty");
        return;
    }

    parts = g_strsplit(value, ":", -1);
    while (parts[nr_parts]) {
        nr_parts++;
    }

    if (nr_parts != 1 && nr_parts != KMH_BOSC_DIES) {
        error_setg(errp,
                   "core-mask must contain one mask or %d colon-separated masks",
                   KMH_BOSC_DIES);
        g_strfreev(parts);
        return;
    }

    for (die = 0; die < KMH_BOSC_DIES; die++) {
        const char *token = parts[nr_parts == 1 ? 0 : die];
        const char *endptr = NULL;
        uint64_t mask;

        if (!token || !*token || qemu_strtou64(token, &endptr, 0, &mask) < 0 ||
            (endptr && *endptr)) {
            error_setg(errp, "invalid core-mask entry '%s'",
                       token ? token : "");
            g_strfreev(parts);
            return;
        }
        if (mask & ~KMH_BOSC_APP_HART_MASK) {
            error_setg(errp,
                       "core-mask entry '%s' exceeds %d app harts per die",
                       token, KMH_BOSC_APP_HARTS_PER_DIE);
            g_strfreev(parts);
            return;
        }

        masks[die] = mask;
    }

    g_strfreev(parts);

    /* Validate against the final soc-mode and die-mask in realize. */
    s->per_die_core_mask = nr_parts != 1;

    for (die = 0; die < KMH_BOSC_DIES; die++) {
        s->core_mask[die] = masks[die];
    }
}

static bool kmh_bosc_get_autotest_dtb(Object *obj, Error **errp)
{
    KmhBoscState *s = KMH_BOSC_MACHINE(obj);

    return s->autotest_dtb;
}

static void kmh_bosc_set_autotest_dtb(Object *obj, bool value, Error **errp)
{
    KmhBoscState *s = KMH_BOSC_MACHINE(obj);

    s->autotest_dtb = value;
}

static void kmh_bosc_get_generated_dtb(Object *obj, Visitor *v,
                                       const char *name, void *opaque,
                                       Error **errp)
{
    KmhBoscState *s = KMH_BOSC_MACHINE(obj);
    OnOffAuto generated_dtb = s->generated_dtb;

    visit_type_OnOffAuto(v, name, &generated_dtb, errp);
}

static void kmh_bosc_set_generated_dtb(Object *obj, Visitor *v,
                                       const char *name, void *opaque,
                                       Error **errp)
{
    KmhBoscState *s = KMH_BOSC_MACHINE(obj);

    visit_type_OnOffAuto(v, name, &s->generated_dtb, errp);
}

static void kmh_bosc_get_iommu_sys(Object *obj, Visitor *v,
                                   const char *name, void *opaque,
                                   Error **errp)
{
    KmhBoscState *s = KMH_BOSC_MACHINE(obj);
    OnOffAuto iommu_sys = s->iommu_sys;

    visit_type_OnOffAuto(v, name, &iommu_sys, errp);
}

static void kmh_bosc_set_iommu_sys(Object *obj, Visitor *v,
                                   const char *name, void *opaque,
                                   Error **errp)
{
    KmhBoscState *s = KMH_BOSC_MACHINE(obj);

    visit_type_OnOffAuto(v, name, &s->iommu_sys, errp);
}

static bool kmh_bosc_get_iommu_bypass(Object *obj, Error **errp)
{
    return KMH_BOSC_MACHINE(obj)->iommu_bypass;
}

static void kmh_bosc_set_iommu_bypass(Object *obj, bool value, Error **errp)
{
    KMH_BOSC_MACHINE(obj)->iommu_bypass = value;
}

static bool kmh_bosc_get_generated_acpi(Object *obj, Error **errp)
{
    KmhBoscState *s = KMH_BOSC_MACHINE(obj);

    return s->generated_acpi;
}

static void kmh_bosc_set_generated_acpi(Object *obj, bool value, Error **errp)
{
    KmhBoscState *s = KMH_BOSC_MACHINE(obj);

    s->generated_acpi = value;
}

static bool kmh_bosc_get_dw_pcie(Object *obj, Error **errp)
{
    KmhBoscState *s = KMH_BOSC_MACHINE(obj);

    return s->dw_pcie;
}

static void kmh_bosc_set_dw_pcie(Object *obj, bool value, Error **errp)
{
    KmhBoscState *s = KMH_BOSC_MACHINE(obj);

    s->dw_pcie = value;
}

static bool kmh_bosc_checkpoint_enabled(KmhBoscState *s)
{
    KmhBoscCheckpoint *c = &s->checkpoint;

    return c->checkpoint_path || c->checkpoint_load || c->checkpoint_interval;
}

static void kmh_bosc_checkpoint_validate(KmhBoscState *s)
{
    KmhBoscCheckpoint *c = &s->checkpoint;

    if (!kmh_bosc_checkpoint_enabled(s)) {
        return;
    }
#ifndef CONFIG_ZSTD
    error_report("kmh-bosc-soc checkpoint requires --enable-zstd");
    exit(1);
#endif
    if (kmh_bosc_boot_from_mcu(s) || !kmh_bosc_selected_hart_count(s)) {
        error_report("kmh-bosc-soc checkpoint requires DDR boot and at least one selected application hart");
        exit(1);
    }
    if (s->dw_pcie || kmh_bosc_is_iommu_sys_enabled(s) || s->generated_acpi) {
        error_report("kmh-bosc-soc checkpoint does not yet support PCIe, "
                     "system IOMMU or ACPI state");
        exit(1);
    }
    if (kmh_bosc_die_ram_size(MACHINE(s)) <
        KMH_BOSC_CPT_DEVICE_BASE + KMH_BOSC_CPT_DEVICE_SIZE -
        KMH_BOSC_DDR_NODE_BASE) {
        error_report("kmh-bosc-soc checkpoint requires at least 292 MiB "
                     "in the die0 DDR slot");
        exit(1);
    }
    if (c->checkpoint_interval && !c->checkpoint_path) {
        error_report("checkpoint-interval requires checkpoint-path");
        exit(1);
    }
    c->single_soc = s->single_soc;
    c->die_mask = s->die_mask;
    c->active_hart_mask = 0;
    c->active_hart_count = 0;
    c->trigger_hartid = UINT_MAX;
    for (unsigned int die = 0; die < KMH_BOSC_DIES; die++) {
        c->core_mask[die] = s->core_mask[die];
        if (!(s->die_mask & (1U << die))) {
            continue;
        }
        for (unsigned int hart = 0;
             hart < KMH_BOSC_APP_HARTS_PER_DIE; hart++) {
            unsigned int hartid = die * KMH_BOSC_APP_HARTS_PER_DIE + hart;
            if (s->core_mask[die] & (1U << hart)) {
                c->active_hart_mask |= UINT64_C(1) << hartid;
                c->active_hart_count++;
                if (c->trigger_hartid == UINT_MAX) {
                    c->trigger_hartid = hartid;
                }
            }
        }
    }
    if (!c->checkpoint_load && c->save_version == KMH_NATIVE_VERSION &&
        !(s->die_mask & 1)) {
        error_report("native v3 reset entry requires die0 DDR");
        exit(1);
    }
    if (!c->checkpoint_load && c->save_version == KMH_NATIVE_VERSION &&
        kmh_bosc_die_ram_size(MACHINE(s)) < 512 * MiB) {
        error_report("native checkpoint requires at least 512 MiB per DDR slot");
        exit(1);
    }
    c->format_version = c->save_version;
    c->cpu_state_base = kmh_bosc_checkpoint_cpu_state_base(c);
    c->checkpoint_next = c->checkpoint_interval;
}

static bool kmh_bosc_machine_checkpoint_save(MachineState *machine,
                                             const char *path, Error **errp)
{
    KmhBoscState *s = KMH_BOSC_MACHINE(machine);

    if (!kmh_bosc_checkpoint_enabled(s)) {
        error_setg(errp, "start kmh-bosc-soc with checkpoint-path to reserve "
                   "checkpoint memory and enable portable saves");
        return false;
    }
    return kmh_bosc_checkpoint_save(&s->checkpoint, path, errp);
}

static char *kmh_bosc_get_checkpoint_path(Object *obj, Error **errp)
{
    return g_strdup(KMH_BOSC_MACHINE(obj)->checkpoint.checkpoint_path);
}

static void kmh_bosc_set_checkpoint_path(Object *obj, const char *value,
                                         Error **errp)
{
    KmhBoscCheckpoint *c = &KMH_BOSC_MACHINE(obj)->checkpoint;

    g_free(c->checkpoint_path);
    c->checkpoint_path = g_strdup(value);
}

static char *kmh_bosc_get_checkpoint_load(Object *obj, Error **errp)
{
    return g_strdup(KMH_BOSC_MACHINE(obj)->checkpoint.checkpoint_load);
}

static void kmh_bosc_set_checkpoint_load(Object *obj, const char *value,
                                         Error **errp)
{
    KmhBoscCheckpoint *c = &KMH_BOSC_MACHINE(obj)->checkpoint;

    g_free(c->checkpoint_load);
    c->checkpoint_load = g_strdup(value);
}

static char *kmh_bosc_get_checkpoint_stub(Object *obj, Error **errp)
{
    return g_strdup(KMH_BOSC_MACHINE(obj)->checkpoint.checkpoint_stub);
}

static void kmh_bosc_set_checkpoint_stub(Object *obj, const char *value,
                                         Error **errp)
{
    KmhBoscCheckpoint *c = &KMH_BOSC_MACHINE(obj)->checkpoint;

    g_free(c->checkpoint_stub);
    c->checkpoint_stub = g_strdup(value);
}

static bool kmh_bosc_get_checkpoint_exit(Object *obj, Error **errp)
{
    return KMH_BOSC_MACHINE(obj)->checkpoint.checkpoint_exit;
}

static void kmh_bosc_set_checkpoint_exit(Object *obj, bool value, Error **errp)
{
    KMH_BOSC_MACHINE(obj)->checkpoint.checkpoint_exit = value;
}

static bool kmh_bosc_get_checkpoint_marker(Object *obj, Error **errp)
{
    return KMH_BOSC_MACHINE(obj)->checkpoint.checkpoint_marker;
}

static void kmh_bosc_set_checkpoint_marker(Object *obj, bool value,
                                           Error **errp)
{
    KMH_BOSC_MACHINE(obj)->checkpoint.checkpoint_marker = value;
}

static char *kmh_bosc_get_checkpoint_format(Object *obj, Error **errp)
{
    return g_strdup_printf("v%u", KMH_BOSC_MACHINE(obj)->checkpoint.save_version);
}

static void kmh_bosc_set_checkpoint_format(Object *obj, const char *value,
                                          Error **errp)
{
    KmhBoscCheckpoint *c = &KMH_BOSC_MACHINE(obj)->checkpoint;

    if (!strcmp(value, "v2")) {
        c->save_version = 2;
    } else if (!strcmp(value, "v3")) {
        c->save_version = KMH_NATIVE_VERSION;
    } else {
        error_setg(errp, "checkpoint-format must be v2 or v3");
    }
}

static void kmh_bosc_checkpoint_bios(KmhBoscState *s)
{
    MachineState *ms = MACHINE(s);
    uint8_t magic[4], entry[KMH_NATIVE_ENTRY_SIZE];
    FILE *fp;
    struct stat st;
    g_autofree char *path = NULL;

    if (!ms->firmware || !strcmp(ms->firmware, "none") ||
        !strcmp(ms->firmware, "default")) {
        return;
    }
    path = riscv_find_firmware(ms->firmware, NULL);
    fp = path ? fopen(path, "rb") : NULL;
    if (!fp) {
        return; /* The normal BIOS loader diagnoses file errors. */
    }
    bool zstd = fread(magic, 1, sizeof(magic), fp) == sizeof(magic) &&
                magic[0] == 0x28 && magic[1] == 0xb5 &&
                magic[2] == 0x2f && magic[3] == 0xfd;
    bool raw = fstat(fileno(fp), &st) == 0 &&
               (uint64_t)st.st_size == ms->ram_size &&
               fseek(fp, 0, SEEK_SET) == 0 &&
               fread(entry, 1, sizeof(entry), fp) == sizeof(entry) &&
               !memcmp(entry, kmh_native_entry, sizeof(entry));
    fclose(fp);
    if (!zstd && !raw) {
        return;
    }
    if (s->checkpoint.checkpoint_load || s->checkpoint.checkpoint_stub ||
        ms->kernel_filename || ms->initrd_filename || ms->dtb) {
        error_report("-bios checkpoint.zst is self-contained; remove "
                     "checkpoint-load/stub, kernel, initrd and dtb");
        exit(1);
    }
    s->checkpoint.checkpoint_load = g_strdup(path);
    s->checkpoint.bios_checkpoint = true;
    s->checkpoint.raw_checkpoint = raw;
}

static void kmh_bosc_machine_init(MachineState *machine)
{
    KmhBoscState *s = KMH_BOSC_MACHINE(machine);

    kmh_bosc_checkpoint_bios(s);
    kmh_bosc_validate(machine);
    kmh_bosc_checkpoint_validate(s);

    kmh_bosc_map_ddr(s);
    kmh_bosc_init_die_address_spaces(s);
    kmh_bosc_map_local_memory(s);
    kmh_bosc_realize_hart_arrays(s);
    kmh_bosc_init_per_die_interrupts(s);
    if (kmh_bosc_checkpoint_enabled(s)) {
        s->checkpoint.cpu = kmh_bosc_app_cpu(s, s->checkpoint.trigger_hartid);
        for (unsigned int hartid = 0; hartid < KMH_BOSC_CPT_CPU_RECORDS;
             hartid++) {
            if ((s->checkpoint.active_hart_mask & (UINT64_C(1) << hartid)) &&
                kmh_bosc_app_cpu(s, hartid)->cfg.vlenb != 16) {
                error_report("BOSC checkpoint requires VLEN=128 on every selected hart");
                exit(1);
            }
        }
        kmh_bosc_checkpoint_init(&s->checkpoint);
    }
    kmh_bosc_init_dmac_irqs(s);
    kmh_bosc_init_uart(s);
    if (s->dw_pcie) {
        kmh_bosc_init_pcie(s);
    }
    if (kmh_bosc_is_iommu_sys_enabled(s)) {
        kmh_bosc_init_iommu(s);
    }
    if (s->dw_pcie) {
        kmh_bosc_init_pcie_dma(s);
    }
    if (s->checkpoint.checkpoint_load) {
        kmh_bosc_checkpoint_load(&s->checkpoint,
                                 s->checkpoint.checkpoint_load, &error_fatal);
        return;
    }
    kmh_bosc_load_mcu_bios(s);
    if (kmh_bosc_boot_from_mcu(s)) {
        kmh_bosc_load_mcu_payload(s);
    }

    if (kmh_bosc_should_generate_dtb(s)) {
        machine->fdt = create_device_tree(&s->fdt_size);
        if (!machine->fdt) {
            error_report("create_device_tree() failed");
            exit(1);
        }
        kmh_bosc_create_fdt(s);
    } else if (machine->dtb) {
        machine->fdt = load_device_tree(machine->dtb, &s->fdt_size);
        if (!machine->fdt) {
            error_report("load_device_tree() failed");
            exit(1);
        }
    }

#ifdef CONFIG_ACPI
    if (s->generated_acpi) {
        kmh_bosc_acpi_setup(machine, s->app_cpus, s->die_mask, s->core_mask,
                            s->acpi_handoff_addr, s->acpi_handoff_size,
                            s->dw_pcie, s->single_soc);
    }
#endif

    if (kmh_bosc_boot_from_mcu(s)) {
        if (machine->fdt) {
            kmh_bosc_load_mcu_fdt(s);
        }
        kmh_bosc_init_mcu_die_num(s);
    } else {
        kmh_bosc_load_app_boot(s);
    }
}

static void kmh_bosc_machine_instance_init(Object *obj)
{
    MachineState *machine = MACHINE(obj);
    KmhBoscState *s = KMH_BOSC_MACHINE(obj);

    s->checkpoint.machine = machine;
    s->checkpoint.save_version = KMH_NATIVE_VERSION;
    s->checkpoint.checkpoint_exit = true;
    s->checkpoint.checkpoint_marker = true;
    machine->smp.max_cpus = KMH_BOSC_TOTAL_APP_HARTS + KMH_BOSC_DIES;
    s->boot_source = g_strdup("ddr");
    s->mcu_bios = NULL;
    s->die_mask = KMH_BOSC_DIE_MASK_BITS;
    for (int die = 0; die < KMH_BOSC_DIES; die++) {
        s->core_mask[die] = KMH_BOSC_APP_HART_MASK;
    }
    s->dw_pcie = false;
    s->generated_acpi = false;
    s->iommu_sys = ON_OFF_AUTO_AUTO;
    s->iommu_bypass = true;
    s->generated_dtb = ON_OFF_AUTO_AUTO;
    s->autotest_dtb = false;
    s->acpi_handoff_addr = KMH_BOSC_ACPI_HANDOFF_ADDR_DEFAULT;
    s->acpi_handoff_size = KMH_BOSC_ACPI_HANDOFF_SIZE_DEFAULT;
    s->autotest_trigger_addr = KMH_BOSC_AUTO_TEST_TRIGGER_ADDR_DEFAULT;
    s->autotest_rootfs_addr = KMH_BOSC_AUTO_TEST_ROOTFS_ADDR_DEFAULT;
    s->autotest_workload_addr = KMH_BOSC_AUTO_TEST_WORKLOAD_ADDR_DEFAULT;
    s->mcu_fdt_addr = 0;

    object_property_add_str(obj, "checkpoint-format",
                            kmh_bosc_get_checkpoint_format,
                            kmh_bosc_set_checkpoint_format);
    object_property_add_str(obj, "checkpoint-path",
                            kmh_bosc_get_checkpoint_path,
                            kmh_bosc_set_checkpoint_path);
    object_property_add_str(obj, "checkpoint-load",
                            kmh_bosc_get_checkpoint_load,
                            kmh_bosc_set_checkpoint_load);
    object_property_add_str(obj, "checkpoint-stub",
                            kmh_bosc_get_checkpoint_stub,
                            kmh_bosc_set_checkpoint_stub);
    object_property_add_bool(obj, "checkpoint-exit",
                             kmh_bosc_get_checkpoint_exit,
                             kmh_bosc_set_checkpoint_exit);
    object_property_add_bool(obj, "checkpoint-marker",
                             kmh_bosc_get_checkpoint_marker,
                             kmh_bosc_set_checkpoint_marker);
    object_property_add_uint64_ptr(obj, "checkpoint-interval",
                                   &s->checkpoint.checkpoint_interval,
                                   OBJ_PROP_FLAG_READWRITE);


    object_property_add_str(obj, "soc-mode",
                            kmh_bosc_get_soc_mode, kmh_bosc_set_soc_mode);
    object_property_set_description(obj, "soc-mode",
                                    "SoC shape: single or multi (default)");

    object_property_add_str(obj, "boot-source",
                            kmh_bosc_get_boot_source,
                            kmh_bosc_set_boot_source);
    object_property_set_description(obj, "boot-source",
                                    "Boot application harts from ddr or mcu");

    object_property_add_str(obj, "mcu-bios",
                            kmh_bosc_get_mcu_bios,
                            kmh_bosc_set_mcu_bios);
    object_property_set_description(obj, "mcu-bios",
                                    "Raw RV32 MCU boot image copied into each selected die bootrom");

    object_property_add_uint32_ptr(obj, "die-mask", &s->die_mask,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_set_description(obj, "die-mask",
                                    "Multi-mode die release mask (low 4 bits); ignored in single mode");

    object_property_add_str(obj, "core-mask",
                            kmh_bosc_get_core_mask,
                            kmh_bosc_set_core_mask);
    object_property_set_description(
        obj, "core-mask",
        "Per-die app hart mask, for example 0x1 or 0x1:0x0:0x0:0x0");

    object_property_add(obj, "generated-dtb", "OnOffAuto",
                        kmh_bosc_get_generated_dtb,
                        kmh_bosc_set_generated_dtb, NULL, NULL);
    object_property_set_description(obj, "generated-dtb",
                                    "Use QEMU-generated device tree");

    object_property_add(obj, "iommu-sys", "OnOffAuto",
                        kmh_bosc_get_iommu_sys,
                        kmh_bosc_set_iommu_sys, NULL, NULL);
    object_property_set_description(obj, "iommu-sys",
                                    "Enable IOMMU platform device");

    object_property_add_bool(obj, "iommu-bypass",
                             kmh_bosc_get_iommu_bypass,
                             kmh_bosc_set_iommu_bypass);
    object_property_set_description(obj, "iommu-bypass",
                                    "Reset each IOMMU wrapper in bypass mode (default on)");

    object_property_add_bool(obj, "generated-acpi",
                             kmh_bosc_get_generated_acpi,
                             kmh_bosc_set_generated_acpi);
    object_property_set_description(obj, "generated-acpi",
                                    "Use QEMU-generated ACPI tables");

    object_property_add_bool(obj, "dw-pcie",
                             kmh_bosc_get_dw_pcie,
                             kmh_bosc_set_dw_pcie);
    object_property_set_description(obj, "dw-pcie",
                                    "Enable DWC PCIe host controllers");

    object_property_add_bool(obj, "autotest-dtb",
                             kmh_bosc_get_autotest_dtb,
                             kmh_bosc_set_autotest_dtb);
    object_property_set_description(obj, "autotest-dtb",
                                    "Add automated-test reserved-memory, pmem, and bootargs nodes to the generated FDT");

    object_property_add_uint64_ptr(obj, "acpi-handoff-addr",
                                   &s->acpi_handoff_addr,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_set_description(obj, "acpi-handoff-addr",
                                    "DDR base address for generated ACPI handoff");

    object_property_add_uint64_ptr(obj, "acpi-handoff-size",
                                   &s->acpi_handoff_size,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_set_description(obj, "acpi-handoff-size",
                                    "DDR size for generated ACPI handoff");

    object_property_add_uint64_ptr(obj, "autotest-trigger-addr",
                                   &s->autotest_trigger_addr,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_set_description(obj, "autotest-trigger-addr",
                                    "Automated-test trigger reserved-memory address");

    object_property_add_uint64_ptr(obj, "autotest-rootfs-addr",
                                   &s->autotest_rootfs_addr,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_set_description(obj, "autotest-rootfs-addr",
                                    "Automated-test rootfs pmem address");

    object_property_add_uint64_ptr(obj, "autotest-workload-addr",
                                   &s->autotest_workload_addr,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_set_description(obj, "autotest-workload-addr",
                                    "Automated-test workload pmem address");
}

static void kmh_bosc_machine_instance_finalize(Object *obj)
{
    KmhBoscState *s = KMH_BOSC_MACHINE(obj);

    g_free(s->boot_source);
    g_free(s->mcu_bios);
    g_free(s->checkpoint.checkpoint_path);
    g_free(s->checkpoint.checkpoint_load);
    g_free(s->checkpoint.checkpoint_stub);
    g_free(s->checkpoint.restore_path);
    g_free(s->checkpoint.save_path);
}

static void kmh_bosc_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    static const char * const valid_cpu_types[] = {
        TYPE_RISCV_CPU_XIANGSHAN_KMH,
        NULL,
    };

    mc->desc = "BOSC Kunminghu single/four-die RISC-V SoC";
    mc->init = kmh_bosc_machine_init;
    mc->checkpoint_save = kmh_bosc_machine_checkpoint_save;
    mc->default_cpu_type = TYPE_RISCV_CPU_XIANGSHAN_KMH;
    mc->default_cpus = KMH_BOSC_TOTAL_APP_HARTS;
    mc->min_cpus = KMH_BOSC_APP_HARTS_PER_DIE;
    mc->max_cpus = KMH_BOSC_TOTAL_APP_HARTS + KMH_BOSC_DIES;
    mc->default_ram_size = 4 * GiB;
    mc->default_ram_id = "kmh-bosc-soc.ram";
    mc->valid_cpu_types = valid_cpu_types;
    mc->pci_allow_0_address = true;
}

static const TypeInfo kmh_bosc_machine_typeinfo = {
    .name = KMH_BOSC_MACHINE_TYPE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(KmhBoscState),
    .instance_init = kmh_bosc_machine_instance_init,
    .instance_finalize = kmh_bosc_machine_instance_finalize,
    .class_init = kmh_bosc_machine_class_init,
};

static void kmh_bosc_machine_register_types(void)
{
    static const TypeInfo dma_type = {
        .name = TYPE_KMH_BOSC_PCIE_DMA,
        .parent = TYPE_IOMMU_MEMORY_REGION,
        .instance_size = sizeof(IOMMUMemoryRegion),
        .class_init = kmh_bosc_pcie_dma_class_init,
    };

    static const TypeInfo app_type = {
        .name = TYPE_KMH_BOSC_PCIE_APP,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(KmhBoscPcieApp),
        .instance_init = kmh_bosc_pcie_app_init,
        .class_init = kmh_bosc_pcie_app_class_init,
    };

    type_register_static(&app_type);
    type_register_static(&dma_type);
    type_register_static(&kmh_bosc_machine_typeinfo);
}

type_init(kmh_bosc_machine_register_types)
