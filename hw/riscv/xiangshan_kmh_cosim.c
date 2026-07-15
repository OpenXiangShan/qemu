/*
 * QEMU RISC-V Board Compatible with the Xiangshan Kunminghu
 * FPGA prototype platform
 *
 * Copyright (c) 2025 Beijing Institute of Open Source Chip (BOSC)
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Provides a board compatible with the Xiangshan Kunminghu
 * FPGA prototype platform:
 *
 * 0) UART (16550A)
 * 1) CLINT (Core-Local Interruptor)
 * 2) IMSIC (Incoming MSI Controller)
 * 3) APLIC (Advanced Platform-Level Interrupt Controller)
 *
 * More information can be found in our Github repository:
 * https://github.com/OpenXiangShan/XiangShan
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
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "system/address-spaces.h"
#include "hw/boards.h"
#include "hw/char/serial-mm.h"
#include "hw/char/xilinx_uartlite.h"
#include "hw/sysbus.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/intc/riscv_aplic.h"
#include "hw/intc/riscv_imsic.h"
#include "hw/dma/dw-axi-dmac.h"
#include "hw/misc/devproxy-dma-bridge.h"
#include "hw/misc/devproxy-irq-controller.h"
#include "hw/misc/devproxy-irq-proxy.h"
#include "hw/misc/devproxy-mmio-proxy.h"
#include "hw/misc/devproxy-msi-controller.h"
#include "hw/misc/devproxy-pcie-ecam-proxy.h"
#include "system/devproxy-dma.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_host.h"
#include "hw/pci-host/gpex.h"
#include "hw/qdev-properties.h"
#include "hw/riscv/boot.h"
#include "hw/riscv/xiangshan_kmh_cosim.h"
#include "hw/riscv/riscv_hart.h"
#include "system/system.h"
#include "hw/misc/unimp.h"
#include "qapi/qapi-visit-common.h"

#define XIANGSHAN_KMH_DEVPROXY_MODE_FRONTEND "frontend"
#define XIANGSHAN_KMH_DEVPROXY_MODE_BACKEND "backend"

static const MemMapEntry xiangshan_kmh_cosim_memmap[] = {
    [XIANGSHAN_KMH_ROM]                =      {     0x1000,       0x40000 },
    [XIANGSHAN_KMH_FLASH]              =      { 0x10000000,     0x4000000 },
    [XIANGSHAN_KMH_DMAC]               =      { 0x30040000,       0x1000 },
    [XIANGSHAN_KMH_UART0]              =      { 0x310B0000,       0x10000 },
    [XIANGSHAN_KMH_PCIE_ECAM]          =      { 0x50000000,      0x100000 },
    [XIANGSHAN_KMH_PCIE_MMIO]          =      { 0x60000000,    0x20000000 },
    [XIANGSHAN_KMH_CLINT]              =      { 0x38000000,       0x10000 },
    [XIANGSHAN_KMH_APLIC_M]            =      { 0x31100000,        0x4000 },
    [XIANGSHAN_KMH_APLIC_S]            =      { 0x31120000,        0x4000 },
    [XIANGSHAN_KMH_SRAM]               =      { 0x37f00000,      0x100000 },
    [XIANGSHAN_KMH_IMSIC_M]            =      { 0x3A800000,       0x10000 },
    [XIANGSHAN_KMH_IMSIC_S]            =      { 0x3B000000,       0x80000 },
    [XIANGSHAN_KMH_UART1]              =      { 0x40600000,        0x1000 },
    [XIANGSHAN_KMH_DRAM]               =      { 0x80000000,           0x0 },
};

static bool xiangshan_kmh_cosim_mode_is(const XiangshanKmhCosimState *s,
                                        const char *mode)
{
    return s->devproxy_mode && !strcmp(s->devproxy_mode, mode);
}

static bool xiangshan_kmh_cosim_is_devproxy_backend(
    const XiangshanKmhCosimState *s)
{
    return xiangshan_kmh_cosim_mode_is(s, XIANGSHAN_KMH_DEVPROXY_MODE_BACKEND);
}

static void xiangshan_kmh_cosim_pcie_proxy_init(XiangshanKmhCosimSoCState *s)
{
    const MemMapEntry *memmap = xiangshan_kmh_cosim_memmap;
    MachineState *ms = MACHINE(qdev_get_machine());
    XiangshanKmhCosimState *kmh = XIANGSHAN_KMH_COSIM_MACHINE(ms);
    DeviceState *dev = qdev_new(TYPE_DEVPROXY_PCIE_ECAM_PROXY);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    qdev_prop_set_string(dev, "socket", kmh->devproxy_mmio_socket);
    qdev_prop_set_string(dev, "irq-socket", kmh->devproxy_irq_socket);
    qdev_prop_set_string(dev, "msi-socket", kmh->devproxy_msi_socket);
    qdev_prop_set_uint64(dev, "ecam-base",
                         memmap[XIANGSHAN_KMH_PCIE_ECAM].base);
    qdev_prop_set_uint64(dev, "ecam-size",
                         memmap[XIANGSHAN_KMH_PCIE_ECAM].size);
    qdev_prop_set_uint64(dev, "mmio-base",
                         memmap[XIANGSHAN_KMH_PCIE_MMIO].base);
    qdev_prop_set_uint64(dev, "mmio-size",
                         memmap[XIANGSHAN_KMH_PCIE_MMIO].size);
    qdev_prop_set_uint64(dev, "msi-base",
                         memmap[XIANGSHAN_KMH_IMSIC_S].base);
    qdev_prop_set_uint64(dev, "msi-size",
                         memmap[XIANGSHAN_KMH_IMSIC_S].size);
    sysbus_realize_and_unref(sbd, &error_fatal);
    sysbus_mmio_map(sbd, 0, memmap[XIANGSHAN_KMH_PCIE_ECAM].base);
    sysbus_mmio_map(sbd, 1, memmap[XIANGSHAN_KMH_PCIE_MMIO].base);
    sysbus_connect_irq(sbd, 0, qdev_get_gpio_in(DEVICE(s->irqchip),
                                                XIANGSHAN_KMH_PCIE_IRQ));
}

static void xiangshan_kmh_cosim_create_devproxy_dma_bridge(
    XiangshanKmhCosimState *kmh)
{
    DeviceState *dev = qdev_new(TYPE_DEVPROXY_DMA_BRIDGE);

    qdev_prop_set_string(dev, "socket", kmh->devproxy_dma_socket);
    qdev_prop_set_string(dev, "dma-shm", kmh->devproxy_dma_shm);
    qdev_realize_and_unref(dev, NULL, &error_fatal);
}

static void xiangshan_kmh_cosim_create_devproxy_mmio_proxy(hwaddr base,
                                                     hwaddr size,
                                                     const char *socket,
                                                     uint64_t phys_addr)
{
    DeviceState *dev = qdev_new(TYPE_DEVPROXY_MMIO_PROXY);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    qdev_prop_set_string(dev, "socket", socket);
    qdev_prop_set_uint64(dev, "phys-addr", phys_addr);
    qdev_prop_set_uint64(dev, "mmio-size", size);
    sysbus_realize_and_unref(sbd, &error_fatal);
    sysbus_mmio_map(sbd, 0, base);
}

static void xiangshan_kmh_cosim_create_devproxy_irq_proxy(const char *socket,
                                                    qemu_irq irq)
{
    DeviceState *dev = qdev_new(TYPE_DEVPROXY_IRQ_PROXY);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    qdev_prop_set_string(dev, "socket", socket);
    sysbus_realize_and_unref(sbd, &error_fatal);
    sysbus_connect_irq(sbd, 0, irq);
}

static void xiangshan_kmh_cosim_create_devproxy_dmac_frontend(
    XiangshanKmhCosimSoCState *s)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    XiangshanKmhCosimState *kmh = XIANGSHAN_KMH_COSIM_MACHINE(ms);
    const MemMapEntry *memmap = xiangshan_kmh_cosim_memmap;

    xiangshan_kmh_cosim_create_devproxy_mmio_proxy(
        memmap[XIANGSHAN_KMH_DMAC].base,
        memmap[XIANGSHAN_KMH_DMAC].size,
        kmh->devproxy_mmio_socket,
        memmap[XIANGSHAN_KMH_DMAC].base);
    xiangshan_kmh_cosim_create_devproxy_irq_proxy(
        kmh->devproxy_dmac_irq_socket,
        qdev_get_gpio_in(DEVICE(s->irqchip), XIANGSHAN_KMH_DMAC_IRQ));
}

static void xiangshan_kmh_cosim_dw_pcie_init(XiangshanKmhCosimSoCState *s)
{
    DesignwarePCIEHost *pcie0 = &s->pcie0;
    qemu_irq irq;

    /*
     * PCIE RC0
     */
    sysbus_realize(SYS_BUS_DEVICE(pcie0), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(pcie0), 0, 0x32000000);

    /* MSI */
    irq = qdev_get_gpio_in(DEVICE(s->irqchip),
                           XIANGSHAN_KMH_RC_MSI0_IRQ);
    sysbus_connect_irq(SYS_BUS_DEVICE(pcie0), 0, irq);
    /* Hot plug */
    irq = qdev_get_gpio_in(DEVICE(s->irqchip), XIANGSHAN_KMH_RC_HP_IRQ);
    sysbus_connect_irq(SYS_BUS_DEVICE(pcie0), 0, irq);
    pcie0->pci.irqs[3] = qdev_get_gpio_in(DEVICE(s->irqchip),
                                          XIANGSHAN_KMH_RC_MSI0_IRQ);
    pcie0->pci.address_space.root = get_system_memory();
}

static void xiangshan_kmh_cosim_fill_pcie_memmap(void)
{
    create_unimplemented_device("pcie0-cfg1", 0x40000000, 512 * MiB);
    /* 0x40_0000_0000 through 0xe0_0000_0000 */
    create_unimplemented_device("pcie0-phy1", 0x4000000000, 640 * GiB);
}

static PCIBus *xiangshan_kmh_cosim_create_devproxy_backend_pcie_host(
    MemoryRegion *sys_mem, DeviceState *irq_controller)
{
    DeviceState *dev;
    MemoryRegion *ecam_alias, *ecam_reg;
    MemoryRegion *mmio_alias, *mmio_reg;
    hwaddr ecam_base = xiangshan_kmh_cosim_memmap[XIANGSHAN_KMH_PCIE_ECAM].base;
    hwaddr ecam_size = xiangshan_kmh_cosim_memmap[XIANGSHAN_KMH_PCIE_ECAM].size;
    hwaddr mmio_base = xiangshan_kmh_cosim_memmap[XIANGSHAN_KMH_PCIE_MMIO].base;
    hwaddr mmio_size = xiangshan_kmh_cosim_memmap[XIANGSHAN_KMH_PCIE_MMIO].size;

    dev = qdev_new(TYPE_GPEX_HOST);

    object_property_set_uint(OBJECT(dev), PCI_HOST_ECAM_BASE, ecam_base, NULL);
    object_property_set_int(OBJECT(dev), PCI_HOST_ECAM_SIZE, ecam_size, NULL);
    object_property_set_uint(OBJECT(dev), PCI_HOST_BELOW_4G_MMIO_BASE,
                             mmio_base, NULL);
    object_property_set_int(OBJECT(dev), PCI_HOST_BELOW_4G_MMIO_SIZE,
                            mmio_size, NULL);
    object_property_set_uint(OBJECT(dev), PCI_HOST_ABOVE_4G_MMIO_BASE, 0, NULL);
    object_property_set_int(OBJECT(dev), PCI_HOST_ABOVE_4G_MMIO_SIZE, 0, NULL);
    object_property_set_uint(OBJECT(dev), PCI_HOST_PIO_BASE, 0, NULL);
    object_property_set_int(OBJECT(dev), PCI_HOST_PIO_SIZE, 0, NULL);

    sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);

    ecam_alias = g_new0(MemoryRegion, 1);
    ecam_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_init_alias(ecam_alias, OBJECT(dev), "pcie-ecam",
                             ecam_reg, 0, ecam_size);
    memory_region_add_subregion(sys_mem, ecam_base, ecam_alias);

    mmio_alias = g_new0(MemoryRegion, 1);
    mmio_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 1);
    memory_region_init_alias(mmio_alias, OBJECT(dev), "pcie-mmio",
                             mmio_reg, mmio_base, mmio_size);
    memory_region_add_subregion(sys_mem, mmio_base, mmio_alias);

    if (irq_controller) {
        int i;

        for (i = 0; i < PCI_NUM_PINS; i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(dev), i,
                               qdev_get_gpio_in(irq_controller, i));
        }
    }

    GPEX_HOST(dev)->gpex_cfg.bus = PCI_HOST_BRIDGE(dev)->bus;
    object_unref(OBJECT(dev));
    return PCI_HOST_BRIDGE(dev)->bus;
}

static DeviceState *xiangshan_kmh_cosim_create_devproxy_irq_controller(
    XiangshanKmhCosimState *s)
{
    DeviceState *dev = qdev_new(TYPE_DEVPROXY_IRQ_CONTROLLER);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    qdev_prop_set_string(dev, "socket", s->devproxy_irq_socket);
    sysbus_realize_and_unref(sbd, &error_fatal);
    sysbus_mmio_map(sbd, 0,
                    xiangshan_kmh_cosim_memmap[XIANGSHAN_KMH_APLIC_S].base);
    return dev;
}

static DeviceState *xiangshan_kmh_cosim_create_devproxy_dmac_irq_controller(
    XiangshanKmhCosimState *s)
{
    DeviceState *dev = qdev_new(TYPE_DEVPROXY_IRQ_CONTROLLER);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    qdev_prop_set_string(dev, "socket", s->devproxy_dmac_irq_socket);
    qdev_prop_set_uint32(dev, "num-lines", 1);
    sysbus_realize_and_unref(sbd, &error_fatal);
    return dev;
}

static DeviceState *xiangshan_kmh_cosim_create_devproxy_msi_controller(
    XiangshanKmhCosimState *s)
{
    DeviceState *dev = qdev_new(TYPE_DEVPROXY_MSI_CONTROLLER);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    qdev_prop_set_string(dev, "socket", s->devproxy_msi_socket);
    qdev_prop_set_uint64(dev, "phys-addr",
                         xiangshan_kmh_cosim_memmap[
                             XIANGSHAN_KMH_IMSIC_S].base);
    qdev_prop_set_uint64(dev, "mmio-size",
                         xiangshan_kmh_cosim_memmap[
                             XIANGSHAN_KMH_IMSIC_S].size);
    sysbus_realize_and_unref(sbd, &error_fatal);
    sysbus_mmio_map(sbd, 0,
                    xiangshan_kmh_cosim_memmap[XIANGSHAN_KMH_IMSIC_S].base);
    return dev;
}

static DeviceState *xiangshan_kmh_cosim_create_aia(uint32_t num_harts)
{
    int i;
    const MemMapEntry *memmap = xiangshan_kmh_cosim_memmap;
    hwaddr addr = 0;
    DeviceState *aplic_m = NULL;

    /* M-level IMSICs */
    addr = memmap[XIANGSHAN_KMH_IMSIC_M].base;
    for (i = 0; i < num_harts; i++) {
        riscv_imsic_create(addr + i * IMSIC_HART_SIZE(0), i, true,
                           1, XIANGSHAN_KMH_IMSIC_NUM_IDS);
    }

    /* S-level IMSICs */
    addr = memmap[XIANGSHAN_KMH_IMSIC_S].base;
    for (i = 0; i < num_harts; i++) {
        riscv_imsic_create(addr +
                           i * IMSIC_HART_SIZE(XIANGSHAN_KMH_IMSIC_GUEST_BITS),
                           i, false, 1 + XIANGSHAN_KMH_IMSIC_GUEST_BITS,
                           XIANGSHAN_KMH_IMSIC_NUM_IDS);
    }

    /* M-level APLIC */
    aplic_m = riscv_aplic_create(memmap[XIANGSHAN_KMH_APLIC_M].base,
                                 memmap[XIANGSHAN_KMH_APLIC_M].size,
                                 0, 0, XIANGSHAN_KMH_APLIC_NUM_SOURCES,
                                 1, true, true, NULL);

    /* S-level APLIC */
    riscv_aplic_create(memmap[XIANGSHAN_KMH_APLIC_S].base,
                       memmap[XIANGSHAN_KMH_APLIC_S].size,
                       0, 0, XIANGSHAN_KMH_APLIC_NUM_SOURCES,
                       1, true, false, aplic_m);

    return aplic_m;
}

static XilinxUARTLite *uartlite_init(hwaddr base, qemu_irq irq, Chardev *chr)
{
    XilinxUARTLite *uartlite = XILINX_UARTLITE(qdev_new(TYPE_XILINX_UARTLITE));

    qdev_prop_set_chr(DEVICE(uartlite), "chardev", chr);
    qdev_prop_set_enum(DEVICE(uartlite), "endianness", ENDIAN_MODE_LITTLE);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(uartlite), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(uartlite), 0, base);
    sysbus_connect_irq(SYS_BUS_DEVICE(uartlite), 0, irq);

    return uartlite;
}

static void xiangshan_kmh_cosim_register_devproxy_platform_dma_requester(
    uint32_t requester_id)
{
    if (!devproxy_dma_register_platform_requester(requester_id,
                                                  &error_fatal)) {
        exit(1);
    }
}

static void xiangshan_kmh_cosim_dw_axi_dmac_init(hwaddr base, qemu_irq irq)
{
    DeviceState *dev = qdev_new(TYPE_DW_AXI_DMAC);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    MachineState *ms = MACHINE(qdev_get_machine());
    XiangshanKmhCosimState *kmh = XIANGSHAN_KMH_COSIM_MACHINE(ms);

    qdev_prop_set_uint32(dev, "requester-id", kmh->devproxy_dmac_requester_id);
    sysbus_realize_and_unref(sbd, &error_fatal);
    sysbus_mmio_map(sbd, 0, base);
    sysbus_connect_irq(sbd, 0, irq);
}

static void xiangshan_kmh_cosim_soc_realize(DeviceState *dev, Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    XiangshanKmhCosimState *kmh = XIANGSHAN_KMH_COSIM_MACHINE(ms);
    XiangshanKmhCosimSoCState *s = XIANGSHAN_KMH_COSIM_SOC(dev);
    const MemMapEntry *memmap = xiangshan_kmh_cosim_memmap;
    MemoryRegion *system_memory = get_system_memory();
    uint32_t num_harts = ms->smp.cpus;

    qdev_prop_set_uint32(DEVICE(&s->cpus), "num-harts", num_harts);
    qdev_prop_set_uint32(DEVICE(&s->cpus), "hartid-base", 0);
    qdev_prop_set_string(DEVICE(&s->cpus), "cpu-type",
                         TYPE_RISCV_CPU_XIANGSHAN_KMH);
    sysbus_realize(SYS_BUS_DEVICE(&s->cpus), &error_fatal);

    if (xiangshan_kmh_cosim_is_devproxy_backend(kmh)) {
        return;
    }

    /* AIA */
    s->irqchip = xiangshan_kmh_cosim_create_aia(num_harts);

    /* UART */
    serial_mm_init(system_memory, memmap[XIANGSHAN_KMH_UART0].base, 2,
                   qdev_get_gpio_in(s->irqchip, XIANGSHAN_KMH_UART0_IRQ),
                   115200, serial_hd(0), DEVICE_LITTLE_ENDIAN);

    /* UART1: Xilinx UART Lite */
    uartlite_init(xiangshan_kmh_cosim_memmap[XIANGSHAN_KMH_UART1].base,
                  qdev_get_gpio_in(DEVICE(s->irqchip), XIANGSHAN_KMH_UART1_IRQ),
                  serial_hd(1));

    xiangshan_kmh_cosim_create_devproxy_dmac_frontend(s);

    /* CLINT */
    riscv_aclint_swi_create(memmap[XIANGSHAN_KMH_CLINT].base,
                            0, num_harts, false);
    riscv_aclint_mtimer_create(memmap[XIANGSHAN_KMH_CLINT].base +
                               RISCV_ACLINT_SWI_SIZE,
                               RISCV_ACLINT_DEFAULT_MTIMER_SIZE,
                               0, num_harts, RISCV_ACLINT_DEFAULT_MTIMECMP,
                               RISCV_ACLINT_DEFAULT_MTIME,
                               XIANGSHAN_KMH_CLINT_TIMEBASE_FREQ, true);

    /* ROM */
    memory_region_init_rom(&s->rom, OBJECT(dev), "xiangshan.kunminghu.rom",
                           memmap[XIANGSHAN_KMH_ROM].size, &error_fatal);
    memory_region_add_subregion(system_memory,
                                memmap[XIANGSHAN_KMH_ROM].base, &s->rom);

    /* SRAM */
    memory_region_init_ram(&s->sram, OBJECT(dev), "riscv.bosc.kmh.sram",
                           memmap[XIANGSHAN_KMH_SRAM].size, &error_fatal);
    memory_region_add_subregion(system_memory,
                           memmap[XIANGSHAN_KMH_SRAM].base, &s->sram);

    /* FLASH */
    memory_region_init_rom(&s->flash, NULL, "riscv.bosc.kmh.flash0",
                           memmap[XIANGSHAN_KMH_FLASH].size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[XIANGSHAN_KMH_FLASH].base,
                                &s->flash);

    xiangshan_kmh_cosim_dw_pcie_init(s);
    xiangshan_kmh_cosim_pcie_proxy_init(s);

    /*
     * PCIe PHY
     */
    xiangshan_kmh_cosim_fill_pcie_memmap();

}

static void xiangshan_kmh_cosim_soc_class_init(ObjectClass *klass,
                                               const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = xiangshan_kmh_cosim_soc_realize;
    dc->user_creatable = false;
}

static void xiangshan_kmh_cosim_soc_instance_init(Object *obj)
{
    XiangshanKmhCosimSoCState *s = XIANGSHAN_KMH_COSIM_SOC(obj);
    XiangshanKmhCosimState *kmh =
        XIANGSHAN_KMH_COSIM_MACHINE(qdev_get_machine());

    object_initialize_child(obj, "cpus", &s->cpus, TYPE_RISCV_HART_ARRAY);
    if (!xiangshan_kmh_cosim_is_devproxy_backend(kmh)) {
        object_initialize_child(obj, "pcie0", &s->pcie0,
                                TYPE_DESIGNWARE_PCIE_HOST);
    }
}

static const TypeInfo xiangshan_kmh_cosim_soc_info = {
    .name = TYPE_XIANGSHAN_KMH_COSIM_SOC,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(XiangshanKmhCosimSoCState),
    .instance_init = xiangshan_kmh_cosim_soc_instance_init,
    .class_init = xiangshan_kmh_cosim_soc_class_init,
};

static void xiangshan_kmh_cosim_soc_register_types(void)
{
    type_register_static(&xiangshan_kmh_cosim_soc_info);
}
type_init(xiangshan_kmh_cosim_soc_register_types)

static void xiangshan_kmh_cosim_machine_init_devproxy_backend(
    MachineState *machine)
{
    XiangshanKmhCosimState *s = XIANGSHAN_KMH_COSIM_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    DeviceState *irq_controller;
    DeviceState *dmac_irq_controller;

    object_initialize_child(OBJECT(machine), "soc", &s->soc,
                            TYPE_XIANGSHAN_KMH_COSIM_SOC);
    qdev_realize(DEVICE(&s->soc), NULL, &error_fatal);

    irq_controller = xiangshan_kmh_cosim_create_devproxy_irq_controller(s);
    dmac_irq_controller =
        xiangshan_kmh_cosim_create_devproxy_dmac_irq_controller(s);
    xiangshan_kmh_cosim_create_devproxy_msi_controller(s);
    xiangshan_kmh_cosim_register_devproxy_platform_dma_requester(
        s->devproxy_dmac_requester_id);
    xiangshan_kmh_cosim_dw_axi_dmac_init(
        xiangshan_kmh_cosim_memmap[XIANGSHAN_KMH_DMAC].base,
        qdev_get_gpio_in(dmac_irq_controller, 0));
    xiangshan_kmh_cosim_create_devproxy_backend_pcie_host(system_memory,
                                                          irq_controller);
}

static void xiangshan_kmh_cosim_machine_init(MachineState *machine)
{
    XiangshanKmhCosimState *s = XIANGSHAN_KMH_COSIM_MACHINE(machine);
    const MemMapEntry *memmap = xiangshan_kmh_cosim_memmap;
    MemoryRegion *system_memory = get_system_memory();
    hwaddr start_addr = memmap[XIANGSHAN_KMH_DRAM].base;
    RISCVBootInfo boot_info;

    if (xiangshan_kmh_cosim_is_devproxy_backend(s)) {
        xiangshan_kmh_cosim_machine_init_devproxy_backend(machine);
        return;
    }

    /* Initialize SoC */
    object_initialize_child(OBJECT(machine), "soc", &s->soc,
                            TYPE_XIANGSHAN_KMH_COSIM_SOC);
    qdev_realize(DEVICE(&s->soc), NULL, &error_fatal);

    /* Register RAM */
    memory_region_add_subregion(system_memory,
                                memmap[XIANGSHAN_KMH_DRAM].base,
                                machine->ram);

    xiangshan_kmh_cosim_create_devproxy_dma_bridge(s);

    riscv_boot_info_init(&boot_info, &s->soc.cpus);
    if (machine->firmware && !strcmp(machine->firmware, "none") &&
        machine->kernel_filename) {
        riscv_load_kernel(machine, &boot_info, start_addr, false, NULL);
        start_addr = boot_info.image_low_addr;
    } else if (machine->firmware && strcmp(machine->firmware, "none")) {
        riscv_load_firmware(machine->firmware, &start_addr, NULL);
    }

    /* ROM reset vector */
    riscv_setup_rom_reset_vec(machine, &s->soc.cpus,
                              start_addr,
                              memmap[XIANGSHAN_KMH_ROM].base,
                              memmap[XIANGSHAN_KMH_ROM].size, 0, 0);

    /* Note: dtb has been integrated into firmware(OpenSBI) when compiling */
}

static void xiangshan_kmh_cosim_machine_instance_init(Object *obj)
{
    XiangshanKmhCosimState *s = XIANGSHAN_KMH_COSIM_MACHINE(obj);
    const char *runtime_dir = g_get_user_runtime_dir();

    s->devproxy_mode = g_strdup(XIANGSHAN_KMH_DEVPROXY_MODE_FRONTEND);
    s->devproxy_mmio_socket = g_build_filename(
        runtime_dir, "qemu-devproxy-mmio.sock", NULL);
    s->devproxy_dma_socket = g_build_filename(
        runtime_dir, "qemu-devproxy-dma.sock", NULL);
    s->devproxy_dma_shm = g_build_filename(
        runtime_dir, "qemu-devproxy-dma-bounce", NULL);
    s->devproxy_irq_socket = g_build_filename(
        runtime_dir, "qemu-devproxy-irq.sock", NULL);
    s->devproxy_dmac_irq_socket = g_build_filename(
        runtime_dir, "qemu-devproxy-dmac-irq.sock", NULL);
    s->devproxy_msi_socket = g_build_filename(
        runtime_dir, "qemu-devproxy-msi.sock", NULL);
    s->devproxy_dmac_requester_id = 0x8;
}

static void xiangshan_kmh_cosim_machine_instance_finalize(Object *obj)
{
    XiangshanKmhCosimState *s = XIANGSHAN_KMH_COSIM_MACHINE(obj);

    g_free(s->devproxy_mode);
    g_free(s->devproxy_mmio_socket);
    g_free(s->devproxy_dma_socket);
    g_free(s->devproxy_dma_shm);
    g_free(s->devproxy_irq_socket);
    g_free(s->devproxy_dmac_irq_socket);
    g_free(s->devproxy_msi_socket);
}

static char *xiangshan_kmh_cosim_get_devproxy_mode(Object *obj, Error **errp)
{
    XiangshanKmhCosimState *s = XIANGSHAN_KMH_COSIM_MACHINE(obj);

    return g_strdup(s->devproxy_mode);
}

static void xiangshan_kmh_cosim_set_devproxy_mode(Object *obj,
                                                  const char *value,
                                                  Error **errp)
{
    XiangshanKmhCosimState *s = XIANGSHAN_KMH_COSIM_MACHINE(obj);

    if (strcmp(value, XIANGSHAN_KMH_DEVPROXY_MODE_FRONTEND) &&
        strcmp(value, XIANGSHAN_KMH_DEVPROXY_MODE_BACKEND)) {
        error_setg(errp,
                   "unsupported devproxy-mode '%s', expected '%s' or '%s'",
                   value, XIANGSHAN_KMH_DEVPROXY_MODE_FRONTEND,
                   XIANGSHAN_KMH_DEVPROXY_MODE_BACKEND);
        return;
    }

    g_free(s->devproxy_mode);
    s->devproxy_mode = g_strdup(value);
}

static char *xiangshan_kmh_cosim_get_devproxy_mmio_socket(Object *obj,
                                                      Error **errp)
{
    XiangshanKmhCosimState *s = XIANGSHAN_KMH_COSIM_MACHINE(obj);

    return g_strdup(s->devproxy_mmio_socket);
}

static void xiangshan_kmh_cosim_set_devproxy_mmio_socket(Object *obj,
                                                     const char *value,
                                                     Error **errp)
{
    XiangshanKmhCosimState *s = XIANGSHAN_KMH_COSIM_MACHINE(obj);

    g_free(s->devproxy_mmio_socket);
    s->devproxy_mmio_socket = g_strdup(value);
}

static char *xiangshan_kmh_cosim_get_devproxy_dma_socket(Object *obj,
                                                   Error **errp)
{
    XiangshanKmhCosimState *s = XIANGSHAN_KMH_COSIM_MACHINE(obj);

    return g_strdup(s->devproxy_dma_socket);
}

static void xiangshan_kmh_cosim_set_devproxy_dma_socket(Object *obj,
                                                  const char *value,
                                                  Error **errp)
{
    XiangshanKmhCosimState *s = XIANGSHAN_KMH_COSIM_MACHINE(obj);

    g_free(s->devproxy_dma_socket);
    s->devproxy_dma_socket = g_strdup(value);
}

static char *xiangshan_kmh_cosim_get_devproxy_dma_shm(Object *obj,
                                                Error **errp)
{
    XiangshanKmhCosimState *s = XIANGSHAN_KMH_COSIM_MACHINE(obj);

    return g_strdup(s->devproxy_dma_shm);
}

static void xiangshan_kmh_cosim_set_devproxy_dma_shm(Object *obj,
                                               const char *value,
                                               Error **errp)
{
    XiangshanKmhCosimState *s = XIANGSHAN_KMH_COSIM_MACHINE(obj);

    g_free(s->devproxy_dma_shm);
    s->devproxy_dma_shm = g_strdup(value);
}

static char *xiangshan_kmh_cosim_get_devproxy_irq_socket(Object *obj,
                                                   Error **errp)
{
    XiangshanKmhCosimState *s = XIANGSHAN_KMH_COSIM_MACHINE(obj);

    return g_strdup(s->devproxy_irq_socket);
}

static void xiangshan_kmh_cosim_set_devproxy_irq_socket(Object *obj,
                                                  const char *value,
                                                  Error **errp)
{
    XiangshanKmhCosimState *s = XIANGSHAN_KMH_COSIM_MACHINE(obj);

    g_free(s->devproxy_irq_socket);
    s->devproxy_irq_socket = g_strdup(value);
}

static char *xiangshan_kmh_cosim_get_devproxy_msi_socket(Object *obj,
                                                   Error **errp)
{
    XiangshanKmhCosimState *s = XIANGSHAN_KMH_COSIM_MACHINE(obj);

    return g_strdup(s->devproxy_msi_socket);
}

static char *xiangshan_kmh_cosim_get_devproxy_dmac_irq_socket(Object *obj,
                                                        Error **errp)
{
    XiangshanKmhCosimState *s = XIANGSHAN_KMH_COSIM_MACHINE(obj);

    return g_strdup(s->devproxy_dmac_irq_socket);
}

static void xiangshan_kmh_cosim_set_devproxy_msi_socket(Object *obj,
                                                  const char *value,
                                                  Error **errp)
{
    XiangshanKmhCosimState *s = XIANGSHAN_KMH_COSIM_MACHINE(obj);

    g_free(s->devproxy_msi_socket);
    s->devproxy_msi_socket = g_strdup(value);
}

static void xiangshan_kmh_cosim_set_devproxy_dmac_irq_socket(Object *obj,
                                                       const char *value,
                                                       Error **errp)
{
    XiangshanKmhCosimState *s = XIANGSHAN_KMH_COSIM_MACHINE(obj);

    g_free(s->devproxy_dmac_irq_socket);
    s->devproxy_dmac_irq_socket = g_strdup(value);
}

static void xiangshan_kmh_cosim_get_devproxy_dmac_requester_id(Object *obj,
                                                         Visitor *v,
                                                         const char *name,
                                                         void *opaque,
                                                         Error **errp)
{
    XiangshanKmhCosimState *s = XIANGSHAN_KMH_COSIM_MACHINE(obj);
    uint32_t value = s->devproxy_dmac_requester_id;

    visit_type_uint32(v, name, &value, errp);
}

static void xiangshan_kmh_cosim_set_devproxy_dmac_requester_id(Object *obj,
                                                         Visitor *v,
                                                         const char *name,
                                                         void *opaque,
                                                         Error **errp)
{
    XiangshanKmhCosimState *s = XIANGSHAN_KMH_COSIM_MACHINE(obj);
    uint32_t value = s->devproxy_dmac_requester_id;

    if (!visit_type_uint32(v, name, &value, errp)) {
        return;
    }
    if (value > UINT16_MAX) {
        error_setg(errp, "devproxy-dmac-requester-id must fit in 16 bits");
        return;
    }

    s->devproxy_dmac_requester_id = value;
}

static void xiangshan_kmh_cosim_machine_class_init(ObjectClass *klass,
                                                   const void *data)
{
    MachineClass *mc = MACHINE_CLASS(klass);
    static const char *const valid_cpu_types[] = {
        TYPE_RISCV_CPU_XIANGSHAN_KMH,
        NULL
    };

    mc->desc = "RISC-V Board compatible with the Xiangshan " \
               "Kunminghu FPGA prototype platform";
    mc->init = xiangshan_kmh_cosim_machine_init;
    mc->max_cpus = XIANGSHAN_KMH_MAX_CPUS;
    mc->default_cpu_type = TYPE_RISCV_CPU_XIANGSHAN_KMH;
    mc->valid_cpu_types = valid_cpu_types;
    mc->default_ram_id = "xiangshan.kunminghu.ram";

    object_class_property_add_str(klass, "devproxy-mode",
                                  xiangshan_kmh_cosim_get_devproxy_mode,
                                  xiangshan_kmh_cosim_set_devproxy_mode);
    object_class_property_set_description(
        klass, "devproxy-mode",
        "Select devproxy integration mode: 'frontend' for the TCG-side "
        "frontend or 'backend' for the device-side backend");

    object_class_property_add_str(klass, "devproxy-mmio-socket",
                                  xiangshan_kmh_cosim_get_devproxy_mmio_socket,
                                  xiangshan_kmh_cosim_set_devproxy_mmio_socket);
    object_class_property_set_description(klass, "devproxy-mmio-socket",
                                          "Unix socket path used by the"
                                          " PCIe and platform MMIO proxy");

    object_class_property_add_str(klass, "devproxy-dma-socket",
                                  xiangshan_kmh_cosim_get_devproxy_dma_socket,
                                  xiangshan_kmh_cosim_set_devproxy_dma_socket);
    object_class_property_set_description(klass, "devproxy-dma-socket",
                                          "Unix socket path used by the"
                                          " DMA proxy channel");

    object_class_property_add_str(klass, "devproxy-dma-shm",
                                  xiangshan_kmh_cosim_get_devproxy_dma_shm,
                                  xiangshan_kmh_cosim_set_devproxy_dma_shm);
    object_class_property_set_description(
        klass, "devproxy-dma-shm",
        "POSIX shared memory object name or file path used by the"
        " shared-bounce DMA proxy channel");

    object_class_property_add_str(klass, "devproxy-irq-socket",
                                  xiangshan_kmh_cosim_get_devproxy_irq_socket,
                                  xiangshan_kmh_cosim_set_devproxy_irq_socket);
    object_class_property_set_description(klass, "devproxy-irq-socket",
                                          "Unix socket path used by the"
                                          " line interrupt proxy channel");

    object_class_property_add_str(klass, "devproxy-msi-socket",
                                  xiangshan_kmh_cosim_get_devproxy_msi_socket,
                                  xiangshan_kmh_cosim_set_devproxy_msi_socket);
    object_class_property_set_description(klass, "devproxy-msi-socket",
                                          "Unix socket path used by the"
                                          " MSI doorbell proxy channel");

    object_class_property_add_str(
        klass, "devproxy-dmac-irq-socket",
        xiangshan_kmh_cosim_get_devproxy_dmac_irq_socket,
        xiangshan_kmh_cosim_set_devproxy_dmac_irq_socket);
    object_class_property_set_description(klass, "devproxy-dmac-irq-socket",
                                          "Unix socket path used by the"
                                          " platform DMAC interrupt proxy"
                                          " channel");

    object_class_property_add(
        klass, "devproxy-dmac-requester-id", "uint32",
        xiangshan_kmh_cosim_get_devproxy_dmac_requester_id,
        xiangshan_kmh_cosim_set_devproxy_dmac_requester_id,
        NULL, NULL);
    object_class_property_set_description(klass, "devproxy-dmac-requester-id",
                                          "16-bit requester/device id used when the"
                                          " backend DMAC issues remote DMA");
}

static const TypeInfo xiangshan_kmh_cosim_machine_info = {
    .name = TYPE_XIANGSHAN_KMH_COSIM_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(XiangshanKmhCosimState),
    .class_init = xiangshan_kmh_cosim_machine_class_init,
    .instance_init = xiangshan_kmh_cosim_machine_instance_init,
    .instance_finalize = xiangshan_kmh_cosim_machine_instance_finalize,
};

static void xiangshan_kmh_cosim_machine_register_types(void)
{
    type_register_static(&xiangshan_kmh_cosim_machine_info);
}
type_init(xiangshan_kmh_cosim_machine_register_types)
