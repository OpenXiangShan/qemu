/*
 * QEMU RISC-V VirtIO Board
 *
 * Copyright (c) 2024 Beijing Institute of Open Source Chip (BOSC).
 *
 * RISC-V machine with 16550a UART and VirtIO MMIO
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "qemu/guest-random.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "hw/char/serial-mm.h"
#include "target/riscv/cpu.h"
#include "hw/core/sysbus-fdt.h"
#include "target/riscv/pmu.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/bosc_nh.h"
#include "hw/riscv/boot.h"
#include "hw/riscv/numa.h"
#include "kvm/kvm_riscv.h"
#include "hw/firmware/smbios.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/intc/riscv_aplic.h"
#include "hw/intc/sifive_plic.h"
#include "hw/misc/sifive_test.h"
#include "hw/platform-bus.h"
#include "chardev/char.h"
#include "sysemu/device_tree.h"
#include "sysemu/sysemu.h"
#include "sysemu/tcg.h"
#include "sysemu/kvm.h"
#include "sysemu/tpm.h"
#include "sysemu/qtest.h"
#include "hw/pci/pci.h"
#include "hw/pci-host/gpex.h"
#include "hw/display/ramfb.h"
#include "hw/acpi/aml-build.h"
#include "qapi/qapi-visit-common.h"
#include "hw/virtio/virtio-iommu.h"

/* KVM AIA only supports APLIC MSI. APLIC Wired is always emulated by QEMU. */
static bool virt_use_kvm_aia(RISCVVirtState *s)
{
    return kvm_irqchip_in_kernel() && s->aia_type == VIRT_AIA_TYPE_APLIC_IMSIC;
}

static bool virt_aclint_allowed(void)
{
    return tcg_enabled() || qtest_enabled();
}

static const MemMapEntry virt_memmap[] = {
    [VIRT_DEBUG] =        {        0x0,         0x100 },
    [VIRT_MROM] =         {     0x1000,        0xf000 },
    [VIRT_TEST] =         {   0x100000,        0x1000 },
    [VIRT_RTC] =          {   0x101000,        0x1000 },
    [VIRT_CLINT] =        {  0x38000000,       0x10000 },
    [VIRT_ACLINT_SSWI] =  {  0x2F00000,        0x4000 },
    [VIRT_PCIE_PIO] =     {  0x3000000,       0x10000 },
    [VIRT_PLATFORM_BUS] = {  0x4000000,     0x2000000 },
    [VIRT_PLIC] =         {  0x3c000000, VIRT_PLIC_SIZE(VIRT_CPUS_MAX * 2) },
    [VIRT_APLIC_M] =      {  0x31100000, APLIC_SIZE(VIRT_CPUS_MAX) },
    [VIRT_APLIC_S] =      {  0x31120000, APLIC_SIZE(VIRT_CPUS_MAX) },
    [VIRT_UART0] =        { 0x310b0000,         0x100 },
    [VIRT_VIRTIO] =       { 0x10001000,        0x1000 },
    [VIRT_FW_CFG] =       { 0x20100000,          0x18 },
    [VIRT_FLASH] =        { 0x10000000,     0x4000000 },
    [VIRT_IMSIC_M] =      { 0x3a800000, VIRT_IMSIC_MAX_SIZE },
    [VIRT_IMSIC_S] =      { 0x3b000000, VIRT_IMSIC_MAX_SIZE },
    [VIRT_PCIE_ECAM] =    { 0x32000000,    0x1000000 },
    [VIRT_PCIE_MMIO] =    { 0x60000000,    0x20000000 },
    [VIRT_DRAM] =         { 0x80000000,           0x0 },
    [VIRT_UART1] =        { 0x310b0000,         0x100 },
};

/* PCIe high mmio is fixed for RV32 */
#define VIRT32_HIGH_PCIE_MMIO_BASE  0x300000000ULL
#define VIRT32_HIGH_PCIE_MMIO_SIZE  (4 * GiB)

/* PCIe high mmio for RV64, size is fixed but base depends on top of RAM */
#define VIRT64_HIGH_PCIE_MMIO_SIZE  (16 * GiB)

static MemMapEntry virt_high_pcie_memmap;

#define VIRT_FLASH_SECTOR_SIZE (256 * KiB)

static PFlashCFI01 *virt_flash_create1(RISCVVirtState *s,
                                       const char *name,
                                       const char *alias_prop_name)
{
    /*
     * Create a single flash device.  We use the same parameters as
     * the flash devices on the ARM virt board.
     */
    DeviceState *dev = qdev_new(TYPE_PFLASH_CFI01);

    qdev_prop_set_uint64(dev, "sector-length", VIRT_FLASH_SECTOR_SIZE);
    qdev_prop_set_uint8(dev, "width", 4);
    qdev_prop_set_uint8(dev, "device-width", 2);
    qdev_prop_set_bit(dev, "big-endian", false);
    qdev_prop_set_uint16(dev, "id0", 0x89);
    qdev_prop_set_uint16(dev, "id1", 0x18);
    qdev_prop_set_uint16(dev, "id2", 0x00);
    qdev_prop_set_uint16(dev, "id3", 0x00);
    qdev_prop_set_string(dev, "name", name);

    object_property_add_child(OBJECT(s), name, OBJECT(dev));
    object_property_add_alias(OBJECT(s), alias_prop_name,
                              OBJECT(dev), "drive");

    return PFLASH_CFI01(dev);
}

static void virt_flash_create(RISCVVirtState *s)
{
    s->flash[0] = virt_flash_create1(s, "virt.flash0", "pflash0");
    s->flash[1] = virt_flash_create1(s, "virt.flash1", "pflash1");
}

static void virt_flash_map1(PFlashCFI01 *flash,
                            hwaddr base, hwaddr size,
                            MemoryRegion *sysmem)
{
    DeviceState *dev = DEVICE(flash);

    assert(QEMU_IS_ALIGNED(size, VIRT_FLASH_SECTOR_SIZE));
    assert(size / VIRT_FLASH_SECTOR_SIZE <= UINT32_MAX);
    qdev_prop_set_uint32(dev, "num-blocks", size / VIRT_FLASH_SECTOR_SIZE);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    memory_region_add_subregion(sysmem, base,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(dev),
                                                       0));
}

static void virt_flash_map(RISCVVirtState *s,
                           MemoryRegion *sysmem)
{
    hwaddr flashsize = virt_memmap[VIRT_FLASH].size / 2;
    hwaddr flashbase = virt_memmap[VIRT_FLASH].base;

    virt_flash_map1(s->flash[0], flashbase, flashsize,
                    sysmem);
    virt_flash_map1(s->flash[1], flashbase + flashsize, flashsize,
                    sysmem);
}

static uint32_t imsic_num_bits(uint32_t count)
{
    uint32_t ret = 0;

    while (BIT(ret) < count) {
        ret++;
    }

    return ret;
}


static void create_fdt(RISCVVirtState *s, const MemMapEntry *memmap)
{
    MachineState *ms = MACHINE(s);

//return;
    ms->fdt = create_device_tree(&s->fdt_size);
    if (!ms->fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }
return;

}

static inline DeviceState *gpex_pcie_init(MemoryRegion *sys_mem,
                                          DeviceState *irqchip,
                                          RISCVVirtState *s)
{
    DeviceState *dev;
    MemoryRegion *ecam_alias, *ecam_reg;
    MemoryRegion *mmio_alias, *high_mmio_alias, *mmio_reg;
    hwaddr ecam_base = s->memmap[VIRT_PCIE_ECAM].base;
    hwaddr ecam_size = s->memmap[VIRT_PCIE_ECAM].size;
    hwaddr mmio_base = s->memmap[VIRT_PCIE_MMIO].base;
    hwaddr mmio_size = s->memmap[VIRT_PCIE_MMIO].size;
    hwaddr high_mmio_base = virt_high_pcie_memmap.base;
    hwaddr high_mmio_size = virt_high_pcie_memmap.size;
    hwaddr pio_base = s->memmap[VIRT_PCIE_PIO].base;
    hwaddr pio_size = s->memmap[VIRT_PCIE_PIO].size;
    qemu_irq irq;
    int i;

    dev = qdev_new(TYPE_GPEX_HOST);

    /* Set GPEX object properties for the virt machine */
    object_property_set_uint(OBJECT(GPEX_HOST(dev)), PCI_HOST_ECAM_BASE,
                            ecam_base, NULL);
    object_property_set_int(OBJECT(GPEX_HOST(dev)), PCI_HOST_ECAM_SIZE,
                            ecam_size, NULL);
    object_property_set_uint(OBJECT(GPEX_HOST(dev)),
                             PCI_HOST_BELOW_4G_MMIO_BASE,
                             mmio_base, NULL);
    object_property_set_int(OBJECT(GPEX_HOST(dev)), PCI_HOST_BELOW_4G_MMIO_SIZE,
                            mmio_size, NULL);
    object_property_set_uint(OBJECT(GPEX_HOST(dev)),
                             PCI_HOST_ABOVE_4G_MMIO_BASE,
                             high_mmio_base, NULL);
    object_property_set_int(OBJECT(GPEX_HOST(dev)), PCI_HOST_ABOVE_4G_MMIO_SIZE,
                            high_mmio_size, NULL);
    object_property_set_uint(OBJECT(GPEX_HOST(dev)), PCI_HOST_PIO_BASE,
                            pio_base, NULL);
    object_property_set_int(OBJECT(GPEX_HOST(dev)), PCI_HOST_PIO_SIZE,
                            pio_size, NULL);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    ecam_alias = g_new0(MemoryRegion, 1);
    ecam_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_init_alias(ecam_alias, OBJECT(dev), "pcie-ecam",
                             ecam_reg, 0, ecam_size);
    memory_region_add_subregion(get_system_memory(), ecam_base, ecam_alias);

    mmio_alias = g_new0(MemoryRegion, 1);
    mmio_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 1);
    memory_region_init_alias(mmio_alias, OBJECT(dev), "pcie-mmio",
                             mmio_reg, mmio_base, mmio_size);
    memory_region_add_subregion(get_system_memory(), mmio_base, mmio_alias);

    /* Map high MMIO space */
    high_mmio_alias = g_new0(MemoryRegion, 1);
    memory_region_init_alias(high_mmio_alias, OBJECT(dev), "pcie-mmio-high",
                             mmio_reg, high_mmio_base, high_mmio_size);
    memory_region_add_subregion(get_system_memory(), high_mmio_base,
                                high_mmio_alias);

    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 2, pio_base);

    for (i = 0; i < GPEX_NUM_IRQS; i++) {
        irq = qdev_get_gpio_in(irqchip, PCIE_IRQ + i);

        sysbus_connect_irq(SYS_BUS_DEVICE(dev), i, irq);
        gpex_set_irq_num(GPEX_HOST(dev), i, PCIE_IRQ + i);
    }

    GPEX_HOST(dev)->gpex_cfg.bus = PCI_HOST_BRIDGE(GPEX_HOST(dev))->bus;
    return dev;
}

static FWCfgState *create_fw_cfg(const MachineState *ms)
{
    hwaddr base = virt_memmap[VIRT_FW_CFG].base;
    FWCfgState *fw_cfg;

    fw_cfg = fw_cfg_init_mem_wide(base + 8, base, 8, base + 16,
                                  &address_space_memory);
    fw_cfg_add_i16(fw_cfg, FW_CFG_NB_CPUS, (uint16_t)ms->smp.cpus);

    return fw_cfg;
}

static DeviceState *virt_create_plic(const MemMapEntry *memmap, int socket,
                                     int base_hartid, int hart_count)
{
    DeviceState *ret;
    g_autofree char *plic_hart_config = NULL;

    /* Per-socket PLIC hart topology configuration string */
    plic_hart_config = riscv_plic_hart_config_string(hart_count);

    /* Per-socket PLIC */
    ret = sifive_plic_create(
            memmap[VIRT_PLIC].base + socket * memmap[VIRT_PLIC].size,
            plic_hart_config, hart_count, base_hartid,
            VIRT_IRQCHIP_NUM_SOURCES,
            ((1U << VIRT_IRQCHIP_NUM_PRIO_BITS) - 1),
            VIRT_PLIC_PRIORITY_BASE,
            VIRT_PLIC_PENDING_BASE,
            VIRT_PLIC_ENABLE_BASE,
            VIRT_PLIC_ENABLE_STRIDE,
            VIRT_PLIC_CONTEXT_BASE,
            VIRT_PLIC_CONTEXT_STRIDE,
            memmap[VIRT_PLIC].size);

    return ret;
}

static DeviceState *virt_create_aia(RISCVVirtAIAType aia_type, int aia_guests,
                                    const MemMapEntry *memmap, int socket,
                                    int base_hartid, int hart_count)
{
    int i;
    hwaddr addr;
    uint32_t guest_bits;
    DeviceState *aplic_s = NULL;
    DeviceState *aplic_m = NULL;
    bool msimode = aia_type == VIRT_AIA_TYPE_APLIC_IMSIC;

    if (msimode) {
        if (!kvm_enabled()) {
            /* Per-socket M-level IMSICs */
            addr = memmap[VIRT_IMSIC_M].base +
                   socket * VIRT_IMSIC_GROUP_MAX_SIZE;
            for (i = 0; i < hart_count; i++) {
                riscv_imsic_create(addr + i * IMSIC_HART_SIZE(0),
                                   base_hartid + i, true, 1,
                                   VIRT_IRQCHIP_NUM_MSIS);
            }
        }

        /* Per-socket S-level IMSICs */
        guest_bits = imsic_num_bits(aia_guests + 1);
        addr = memmap[VIRT_IMSIC_S].base + socket * VIRT_IMSIC_GROUP_MAX_SIZE;
        for (i = 0; i < hart_count; i++) {
            riscv_imsic_create(addr + i * IMSIC_HART_SIZE(guest_bits),
                               base_hartid + i, false, 1 + aia_guests,
                               VIRT_IRQCHIP_NUM_MSIS);
        }
    }

    if (!kvm_enabled()) {
        /* Per-socket M-level APLIC */
        aplic_m = riscv_aplic_create(memmap[VIRT_APLIC_M].base +
                                     socket * memmap[VIRT_APLIC_M].size,
                                     memmap[VIRT_APLIC_M].size,
                                     (msimode) ? 0 : base_hartid,
                                     (msimode) ? 0 : hart_count,
                                     VIRT_IRQCHIP_NUM_SOURCES,
                                     VIRT_IRQCHIP_NUM_PRIO_BITS,
                                     msimode, true, NULL);
    }

    /* Per-socket S-level APLIC */
    aplic_s = riscv_aplic_create(memmap[VIRT_APLIC_S].base +
                                 socket * memmap[VIRT_APLIC_S].size,
                                 memmap[VIRT_APLIC_S].size,
                                 (msimode) ? 0 : base_hartid,
                                 (msimode) ? 0 : hart_count,
                                 VIRT_IRQCHIP_NUM_SOURCES,
                                 VIRT_IRQCHIP_NUM_PRIO_BITS,
                                 msimode, false, aplic_m);

    return kvm_enabled() ? aplic_s : aplic_m;
}

static void create_platform_bus(RISCVVirtState *s, DeviceState *irqchip)
{
    DeviceState *dev;
    SysBusDevice *sysbus;
    const MemMapEntry *memmap = virt_memmap;
    int i;
    MemoryRegion *sysmem = get_system_memory();

    dev = qdev_new(TYPE_PLATFORM_BUS_DEVICE);
    dev->id = g_strdup(TYPE_PLATFORM_BUS_DEVICE);
    qdev_prop_set_uint32(dev, "num_irqs", VIRT_PLATFORM_BUS_NUM_IRQS);
    qdev_prop_set_uint32(dev, "mmio_size", memmap[VIRT_PLATFORM_BUS].size);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    s->platform_bus_dev = dev;

    sysbus = SYS_BUS_DEVICE(dev);
    for (i = 0; i < VIRT_PLATFORM_BUS_NUM_IRQS; i++) {
        int irq = VIRT_PLATFORM_BUS_IRQ + i;
        sysbus_connect_irq(sysbus, i, qdev_get_gpio_in(irqchip, irq));
    }

    memory_region_add_subregion(sysmem,
                                memmap[VIRT_PLATFORM_BUS].base,
                                sysbus_mmio_get_region(sysbus, 0));
}

static void virt_build_smbios(RISCVVirtState *s)
{
    MachineClass *mc = MACHINE_GET_CLASS(s);
    MachineState *ms = MACHINE(s);
    uint8_t *smbios_tables, *smbios_anchor;
    size_t smbios_tables_len, smbios_anchor_len;
    struct smbios_phys_mem_area mem_array;
    const char *product = "QEMU Virtual Machine";

    if (kvm_enabled()) {
        product = "KVM Virtual Machine";
    }

    smbios_set_defaults("QEMU", product, mc->name);

    if (riscv_is_32bit(&s->soc[0])) {
        smbios_set_default_processor_family(0x200);
    } else {
        smbios_set_default_processor_family(0x201);
    }

    /* build the array of physical mem area from base_memmap */
    mem_array.address = s->memmap[VIRT_DRAM].base;
    mem_array.length = ms->ram_size;

    smbios_get_tables(ms, SMBIOS_ENTRY_POINT_TYPE_64,
                      &mem_array, 1,
                      &smbios_tables, &smbios_tables_len,
                      &smbios_anchor, &smbios_anchor_len,
                      &error_fatal);

    if (smbios_anchor) {
        fw_cfg_add_file(s->fw_cfg, "etc/smbios/smbios-tables",
                        smbios_tables, smbios_tables_len);
        fw_cfg_add_file(s->fw_cfg, "etc/smbios/smbios-anchor",
                        smbios_anchor, smbios_anchor_len);
    }
}

static void virt_machine_done(Notifier *notifier, void *data)
{
    RISCVVirtState *s = container_of(notifier, RISCVVirtState,
                                     machine_done);
    const MemMapEntry *memmap = virt_memmap;
    MachineState *machine = MACHINE(s);
    target_ulong start_addr = memmap[VIRT_DRAM].base;
    target_ulong firmware_end_addr, kernel_start_addr;
    const char *firmware_name = riscv_default_firmware_name(&s->soc[0]);
    uint64_t fdt_load_addr;
    uint64_t kernel_entry = 0;
    BlockBackend *pflash_blk0;


    /*
     * Only direct boot kernel is currently supported for KVM VM,
     * so the "-bios" parameter is not supported when KVM is enabled.
     */
    if (kvm_enabled()) {
        if (machine->firmware) {
            if (strcmp(machine->firmware, "none")) {
                error_report("Machine mode firmware is not supported in "
                             "combination with KVM.");
                exit(1);
            }
        } else {
            machine->firmware = g_strdup("none");
        }
    }

    firmware_end_addr = riscv_find_and_load_firmware(machine, firmware_name,
                                                     &start_addr, NULL);

    pflash_blk0 = pflash_cfi01_get_blk(s->flash[0]);
    if (pflash_blk0) {
        if (machine->firmware && !strcmp(machine->firmware, "none") &&
            !kvm_enabled()) {
            /*
             * Pflash was supplied but bios is none and not KVM guest,
             * let's overwrite the address we jump to after reset to
             * the base of the flash.
             */
            start_addr = virt_memmap[VIRT_FLASH].base;
        } else {
            /*
             * Pflash was supplied but either KVM guest or bios is not none.
             * In this case, base of the flash would contain S-mode payload.
             */
            riscv_setup_firmware_boot(machine);
            kernel_entry = virt_memmap[VIRT_FLASH].base;
        }
    }

    if (machine->kernel_filename && !kernel_entry) {
        kernel_start_addr = riscv_calc_kernel_start_addr(&s->soc[0],
                                                         firmware_end_addr);

        kernel_entry = riscv_load_kernel(machine, &s->soc[0],
                                         kernel_start_addr, true, NULL);
    }

    fdt_load_addr = riscv_compute_fdt_addr(memmap[VIRT_DRAM].base,
                                           memmap[VIRT_DRAM].size,
                                           machine);
    riscv_load_fdt(fdt_load_addr, machine->fdt);

    /* load the reset vector */
    riscv_setup_rom_reset_vec(machine, &s->soc[0], start_addr,
                              virt_memmap[VIRT_MROM].base,
                              virt_memmap[VIRT_MROM].size, kernel_entry,
                              fdt_load_addr);

    /*
     * Only direct boot kernel is currently supported for KVM VM,
     * So here setup kernel start address and fdt address.
     * TODO:Support firmware loading and integrate to TCG start
     */
    if (kvm_enabled()) {
        riscv_setup_direct_kernel(kernel_entry, fdt_load_addr);
    }

    virt_build_smbios(s);

//    if (virt_is_acpi_enabled(s)) {
      if(s->acpi != ON_OFF_AUTO_OFF){
           virt_acpi_setup(s);
    }
}

static void virt_machine_init(MachineState *machine)
{
    const MemMapEntry *memmap = virt_memmap;
    RISCVVirtState *s = RISCV_VIRT_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *mask_rom = g_new(MemoryRegion, 1);
    DeviceState *mmio_irqchip, *virtio_irqchip, *pcie_irqchip;
    int i, base_hartid, hart_count;
    int socket_count = riscv_socket_count(machine);

    /* Check socket count limit */
    if (VIRT_SOCKETS_MAX < socket_count) {
        error_report("number of sockets/nodes should be less than %d",
            VIRT_SOCKETS_MAX);
        exit(1);
    }

    if (!virt_aclint_allowed() && s->have_aclint) {
        error_report("'aclint' is only available with TCG acceleration");
        exit(1);
    }

    /* Initialize sockets */
    mmio_irqchip = virtio_irqchip = pcie_irqchip = NULL;
    for (i = 0; i < socket_count; i++) {
        g_autofree char *soc_name = g_strdup_printf("soc%d", i);

        if (!riscv_socket_check_hartids(machine, i)) {
            error_report("discontinuous hartids in socket%d", i);
            exit(1);
        }

        base_hartid = riscv_socket_first_hartid(machine, i);
        if (base_hartid < 0) {
            error_report("can't find hartid base for socket%d", i);
            exit(1);
        }

        hart_count = riscv_socket_hart_count(machine, i);
        if (hart_count < 0) {
            error_report("can't find hart count for socket%d", i);
            exit(1);
        }

        object_initialize_child(OBJECT(machine), soc_name, &s->soc[i],
                                TYPE_RISCV_HART_ARRAY);
        object_property_set_str(OBJECT(&s->soc[i]), "cpu-type",
                                machine->cpu_type, &error_abort);
        object_property_set_int(OBJECT(&s->soc[i]), "hartid-base",
                                base_hartid, &error_abort);
        object_property_set_int(OBJECT(&s->soc[i]), "num-harts",
                                hart_count, &error_abort);
        sysbus_realize(SYS_BUS_DEVICE(&s->soc[i]), &error_fatal);

        if (virt_aclint_allowed() && s->have_aclint) {
            if (s->aia_type == VIRT_AIA_TYPE_APLIC_IMSIC) {
                /* Per-socket ACLINT MTIMER */
                riscv_aclint_mtimer_create(memmap[VIRT_CLINT].base +
                            i * RISCV_ACLINT_DEFAULT_MTIMER_SIZE,
                        RISCV_ACLINT_DEFAULT_MTIMER_SIZE,
                        base_hartid, hart_count,
                        RISCV_ACLINT_DEFAULT_MTIMECMP,
                        RISCV_ACLINT_DEFAULT_MTIME,
                        RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ, true);
            } else {
                /* Per-socket ACLINT MSWI, MTIMER, and SSWI */
                riscv_aclint_swi_create(memmap[VIRT_CLINT].base +
                            i * memmap[VIRT_CLINT].size,
                        base_hartid, hart_count, false);
                riscv_aclint_mtimer_create(memmap[VIRT_CLINT].base +
                            i * memmap[VIRT_CLINT].size +
                            RISCV_ACLINT_SWI_SIZE,
                        RISCV_ACLINT_DEFAULT_MTIMER_SIZE,
                        base_hartid, hart_count,
                        RISCV_ACLINT_DEFAULT_MTIMECMP,
                        RISCV_ACLINT_DEFAULT_MTIME,
                        RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ, true);
                riscv_aclint_swi_create(memmap[VIRT_ACLINT_SSWI].base +
                            i * memmap[VIRT_ACLINT_SSWI].size,
                        base_hartid, hart_count, true);
            }
        } else if (tcg_enabled()) {
            /* Per-socket SiFive CLINT */
            riscv_aclint_swi_create(
                    memmap[VIRT_CLINT].base + i * memmap[VIRT_CLINT].size,
                    base_hartid, hart_count, false);
            riscv_aclint_mtimer_create(memmap[VIRT_CLINT].base +
                        i * memmap[VIRT_CLINT].size + RISCV_ACLINT_SWI_SIZE,
                    RISCV_ACLINT_DEFAULT_MTIMER_SIZE, base_hartid, hart_count,
                    RISCV_ACLINT_DEFAULT_MTIMECMP, RISCV_ACLINT_DEFAULT_MTIME,
                    RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ, true);
        }

        /* Per-socket interrupt controller */
        if (s->aia_type == VIRT_AIA_TYPE_NONE) {
            s->irqchip[i] = virt_create_plic(memmap, i,
                                             base_hartid, hart_count);
        } else {
            s->irqchip[i] = virt_create_aia(s->aia_type, s->aia_guests,
                                            memmap, i, base_hartid,
                                            hart_count);
        }

        /* Try to use different IRQCHIP instance based device type */
        if (i == 0) {
            mmio_irqchip = s->irqchip[i];
            virtio_irqchip = s->irqchip[i];
            pcie_irqchip = s->irqchip[i];
        }
        if (i == 1) {
            virtio_irqchip = s->irqchip[i];
            pcie_irqchip = s->irqchip[i];
        }
        if (i == 2) {
            pcie_irqchip = s->irqchip[i];
        }
    }

    if (kvm_enabled() && virt_use_kvm_aia(s)) {
        kvm_riscv_aia_create(machine, IMSIC_MMIO_GROUP_MIN_SHIFT,
                             VIRT_IRQCHIP_NUM_SOURCES, VIRT_IRQCHIP_NUM_MSIS,
                             memmap[VIRT_APLIC_S].base,
                             memmap[VIRT_IMSIC_S].base,
                             s->aia_guests);
    }

    if (riscv_is_32bit(&s->soc[0])) {
#if HOST_LONG_BITS == 64
        /* limit RAM size in a 32-bit system */
        if (machine->ram_size > 10 * GiB) {
            machine->ram_size = 10 * GiB;
            error_report("Limiting RAM size to 10 GiB");
        }
#endif
        virt_high_pcie_memmap.base = VIRT32_HIGH_PCIE_MMIO_BASE;
        virt_high_pcie_memmap.size = VIRT32_HIGH_PCIE_MMIO_SIZE;
    } else {
        virt_high_pcie_memmap.size = VIRT64_HIGH_PCIE_MMIO_SIZE;
        virt_high_pcie_memmap.base = memmap[VIRT_DRAM].base + machine->ram_size;
        virt_high_pcie_memmap.base =
            ROUND_UP(virt_high_pcie_memmap.base, virt_high_pcie_memmap.size);
    }

    s->memmap = virt_memmap;

    /* register system main memory (actual RAM) */
    memory_region_add_subregion(system_memory, memmap[VIRT_DRAM].base,
        machine->ram);

    /* boot rom */
    memory_region_init_rom(mask_rom, NULL, "riscv_virt_board.mrom",
                           memmap[VIRT_MROM].size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[VIRT_MROM].base,
                                mask_rom);

    /*
     * Init fw_cfg. Must be done before riscv_load_fdt, otherwise the
     * device tree cannot be altered and we get FDT_ERR_NOSPACE.
     */
    s->fw_cfg = create_fw_cfg(machine);
    rom_set_fw(s->fw_cfg);

    /* SiFive Test MMIO device */
    sifive_test_create(memmap[VIRT_TEST].base);

    /* VirtIO MMIO devices */
    for (i = 0; i < VIRTIO_COUNT; i++) {
        sysbus_create_simple("virtio-mmio",
            memmap[VIRT_VIRTIO].base + i * memmap[VIRT_VIRTIO].size,
            qdev_get_gpio_in(virtio_irqchip, VIRTIO_IRQ + i));
    }

    gpex_pcie_init(system_memory, pcie_irqchip, s);

    create_platform_bus(s, mmio_irqchip);

    if (s->aia_type == VIRT_AIA_TYPE_NONE) {
        serial_mm_init(system_memory, memmap[VIRT_UART0].base,
            2, qdev_get_gpio_in(mmio_irqchip, UART0_IRQ), 399193,
            serial_hd(0), DEVICE_LITTLE_ENDIAN);
    } else {
        serial_mm_init(system_memory, memmap[VIRT_UART1].base,
            2, qdev_get_gpio_in(mmio_irqchip, UART1_IRQ), 399193,
            serial_hd(0), DEVICE_LITTLE_ENDIAN);
    }

    sysbus_create_simple("goldfish_rtc", memmap[VIRT_RTC].base,
        qdev_get_gpio_in(mmio_irqchip, RTC_IRQ));

    for (i = 0; i < ARRAY_SIZE(s->flash); i++) {
        /* Map legacy -drive if=pflash to machine properties */
        pflash_cfi01_legacy_drive(s->flash[i],
                                  drive_get(IF_PFLASH, 0, i));
    }
    virt_flash_map(s, system_memory);

    /* load/create device tree */
    if (machine->dtb) {
        machine->fdt = load_device_tree(machine->dtb, &s->fdt_size);
        if (!machine->fdt) {
            error_report("load_device_tree() failed");
            exit(1);
        }
    } else {
        create_fdt(s, memmap);
    }

    s->machine_done.notify = virt_machine_done;
    qemu_add_machine_init_done_notifier(&s->machine_done);
}

static void virt_machine_instance_init(Object *obj)
{
    RISCVVirtState *s = RISCV_VIRT_MACHINE(obj);

    virt_flash_create(s);

    s->oem_id = g_strndup(ACPI_BUILD_APPNAME6, 6);
    s->oem_table_id = g_strndup(ACPI_BUILD_APPNAME8, 8);
    s->acpi = ON_OFF_AUTO_AUTO;
}

static char *virt_get_aia_guests(Object *obj, Error **errp)
{
    RISCVVirtState *s = RISCV_VIRT_MACHINE(obj);
    char val[32];

    sprintf(val, "%d", s->aia_guests);
    return g_strdup(val);
}

static void virt_set_aia_guests(Object *obj, const char *val, Error **errp)
{
    RISCVVirtState *s = RISCV_VIRT_MACHINE(obj);

    s->aia_guests = atoi(val);
    if (s->aia_guests < 0 || s->aia_guests > VIRT_IRQCHIP_MAX_GUESTS) {
        error_setg(errp, "Invalid number of AIA IMSIC guests");
        error_append_hint(errp, "Valid values be between 0 and %d.\n",
                          VIRT_IRQCHIP_MAX_GUESTS);
    }
}

static char *virt_get_aia(Object *obj, Error **errp)
{
    RISCVVirtState *s = RISCV_VIRT_MACHINE(obj);
    const char *val;

    switch (s->aia_type) {
    case VIRT_AIA_TYPE_APLIC:
        val = "aplic";
        break;
    case VIRT_AIA_TYPE_APLIC_IMSIC:
        val = "aplic-imsic";
        break;
    default:
        val = "none";
        break;
    };

    return g_strdup(val);
}

static void virt_set_aia(Object *obj, const char *val, Error **errp)
{
    RISCVVirtState *s = RISCV_VIRT_MACHINE(obj);

    if (!strcmp(val, "none")) {
        s->aia_type = VIRT_AIA_TYPE_NONE;
    } else if (!strcmp(val, "aplic")) {
        s->aia_type = VIRT_AIA_TYPE_APLIC;
    } else if (!strcmp(val, "aplic-imsic")) {
        s->aia_type = VIRT_AIA_TYPE_APLIC_IMSIC;
    } else {
        error_setg(errp, "Invalid AIA interrupt controller type");
        error_append_hint(errp, "Valid values are none, aplic, and "
                          "aplic-imsic.\n");
    }
}

static bool virt_get_aclint(Object *obj, Error **errp)
{
    RISCVVirtState *s = RISCV_VIRT_MACHINE(obj);

    return s->have_aclint;
}

static void virt_set_aclint(Object *obj, bool value, Error **errp)
{
    RISCVVirtState *s = RISCV_VIRT_MACHINE(obj);

    s->have_aclint = value;
}

//static bool virt_is_acpi_enabled(RISCVVirtState *s)
//{
//    return s->acpi != ON_OFF_AUTO_OFF;
//}

static void virt_get_acpi(Object *obj, Visitor *v, const char *name,
                          void *opaque, Error **errp)
{
    RISCVVirtState *s = RISCV_VIRT_MACHINE(obj);
    OnOffAuto acpi = s->acpi;

    visit_type_OnOffAuto(v, name, &acpi, errp);
}

static void virt_set_acpi(Object *obj, Visitor *v, const char *name,
                          void *opaque, Error **errp)
{
    RISCVVirtState *s = RISCV_VIRT_MACHINE(obj);

    visit_type_OnOffAuto(v, name, &s->acpi, errp);
}

static HotplugHandler *virt_machine_get_hotplug_handler(MachineState *machine,
                                                        DeviceState *dev)
{
    MachineClass *mc = MACHINE_GET_CLASS(machine);

    if (device_is_dynamic_sysbus(mc, dev) ||
        object_dynamic_cast(OBJECT(dev), TYPE_VIRTIO_IOMMU_PCI)) {
        return HOTPLUG_HANDLER(machine);
    }
    return NULL;
}

static void virt_machine_device_plug_cb(HotplugHandler *hotplug_dev,
                                        DeviceState *dev, Error **errp)
{
    RISCVVirtState *s = RISCV_VIRT_MACHINE(hotplug_dev);

    if (s->platform_bus_dev) {
        MachineClass *mc = MACHINE_GET_CLASS(s);

        if (device_is_dynamic_sysbus(mc, dev)) {
            platform_bus_link_device(PLATFORM_BUS_DEVICE(s->platform_bus_dev),
                                     SYS_BUS_DEVICE(dev));
        }
    }

}

static void virt_machine_class_init(ObjectClass *oc, void *data)
{
    char str[128];
    MachineClass *mc = MACHINE_CLASS(oc);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(oc);

    mc->desc = "RISC-V VirtIO board";
    mc->init = virt_machine_init;
    mc->max_cpus = VIRT_CPUS_MAX;
    mc->default_cpu_type = TYPE_RISCV_CPU_BOSC_NanHu;
    mc->pci_allow_0_address = true;
    mc->possible_cpu_arch_ids = riscv_numa_possible_cpu_arch_ids;
    mc->cpu_index_to_instance_props = riscv_numa_cpu_index_to_props;
    mc->get_default_cpu_node_id = riscv_numa_get_default_cpu_node_id;
    mc->numa_mem_supported = true;
    /* platform instead of architectural choice */
    mc->cpu_cluster_has_numa_boundary = true;
    mc->default_ram_id = "riscv_virt_board.ram";
    assert(!mc->get_hotplug_handler);
    mc->get_hotplug_handler = virt_machine_get_hotplug_handler;

    hc->plug = virt_machine_device_plug_cb;

    machine_class_allow_dynamic_sysbus_dev(mc, TYPE_RAMFB_DEVICE);
#ifdef CONFIG_TPM
    machine_class_allow_dynamic_sysbus_dev(mc, TYPE_TPM_TIS_SYSBUS);
#endif


    object_class_property_add_bool(oc, "aclint", virt_get_aclint,
                                   virt_set_aclint);
    object_class_property_set_description(oc, "aclint",
                                          "(TCG only) Set on/off to "
                                          "enable/disable emulating "
                                          "ACLINT devices");

    object_class_property_add_str(oc, "aia", virt_get_aia,
                                  virt_set_aia);
    object_class_property_set_description(oc, "aia",
                                          "Set type of AIA interrupt "
                                          "controller. Valid values are "
                                          "none, aplic, and aplic-imsic.");

    object_class_property_add_str(oc, "aia-guests",
                                  virt_get_aia_guests,
                                  virt_set_aia_guests);
    sprintf(str, "Set number of guest MMIO pages for AIA IMSIC. Valid value "
                 "should be between 0 and %d.", VIRT_IRQCHIP_MAX_GUESTS);
    object_class_property_set_description(oc, "aia-guests", str);
    object_class_property_add(oc, "acpi", "OnOffAuto",
                              virt_get_acpi, virt_set_acpi,
                              NULL, NULL);
    object_class_property_set_description(oc, "acpi",
                                          "Enable ACPI");
}

static const TypeInfo virt_machine_typeinfo = {
    .name       = MACHINE_TYPE_NAME("bosc-nh"),
    .parent     = TYPE_MACHINE,
    .class_init = virt_machine_class_init,
    .instance_init = virt_machine_instance_init,
    .instance_size = sizeof(RISCVVirtState),
    .interfaces = (InterfaceInfo[]) {
         { TYPE_HOTPLUG_HANDLER },
         { }
    },
};

static void virt_machine_init_register_types(void)
{
    type_register_static(&virt_machine_typeinfo);
}

type_init(virt_machine_init_register_types)
