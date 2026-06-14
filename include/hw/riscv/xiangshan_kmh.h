/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * QEMU RISC-V Board Compatible with the Xiangshan Kunminghu
 * FPGA prototype platform
 *
 * Copyright (c) 2025 Beijing Institute of Open Source Chip (BOSC)
 *
 */

#ifndef HW_XIANGSHAN_KMH_H
#define HW_XIANGSHAN_KMH_H

#include "hw/boards.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/pci-host/designware.h"
#include "qemu/units.h"


#define XIANGSHAN_KMH_MAX_CPUS 16

typedef struct XiangshanKmhSoCState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    RISCVHartArrayState cpus;
    DeviceState *irqchip;
    MemoryRegion rom;
    MemoryRegion sram;
    MemoryRegion flash;
    DesignwarePCIEHost pcie0;
    bool dw_pcie;
    bool my_virtio_blk;
    bool my_virtio_net;
    bool my_virtio_console;
    char *my_virtio_blk_image;
    char *my_virtio_net_hostfwd;
    char *my_virtio_net_network;
    char *my_virtio_net_netmask;
    char *my_virtio_net_host_ip;
    char *my_virtio_net_dhcp_start;
    char *my_virtio_net_dns_ip;
    char *my_virtio_console_backend;
    char *my_virtio_console_input_path;
    char *my_virtio_console_output_path;
} XiangshanKmhSoCState;

#define TYPE_XIANGSHAN_KMH_SOC "xiangshan.kunminghu.soc"
DECLARE_INSTANCE_CHECKER(XiangshanKmhSoCState, XIANGSHAN_KMH_SOC,
                         TYPE_XIANGSHAN_KMH_SOC)

typedef struct XiangshanKmhState {
    /*< private >*/
    MachineState parent_obj;

    /*< public >*/
    XiangshanKmhSoCState soc;
    OnOffAuto iommu_sys;
    OnOffAuto generated_dtb;
    bool autotest_dtb;
    bool dw_pcie;
    bool my_virtio_blk;
    bool my_virtio_net;
    bool my_virtio_console;
    char *my_virtio_blk_image;
    char *my_virtio_net_hostfwd;
    char *my_virtio_net_network;
    char *my_virtio_net_netmask;
    char *my_virtio_net_host_ip;
    char *my_virtio_net_dhcp_start;
    char *my_virtio_net_dns_ip;
    char *my_virtio_console_backend;
    char *my_virtio_console_input_path;
    char *my_virtio_console_output_path;
    uint64_t fw_jump_fdt_addr;
    uint64_t autotest_image_addr;
    uint64_t autotest_rootfs_addr;
    uint64_t autotest_workload_addr;
    uint64_t autotest_trigger_addr;
    uint64_t autotest_rootfs_size;
    uint64_t autotest_workload_size;
    uint64_t autotest_trigger_size;
    int fdt_size;
} XiangshanKmhState;

#define TYPE_XIANGSHAN_KMH_MACHINE MACHINE_TYPE_NAME("xiangshan-kunminghu")
DECLARE_INSTANCE_CHECKER(XiangshanKmhState, XIANGSHAN_KMH_MACHINE,
                         TYPE_XIANGSHAN_KMH_MACHINE)

enum {
    XIANGSHAN_KMH_ROM,
    XIANGSHAN_KMH_FLASH,
    XIANGSHAN_KMH_MY_VIRTIO_CONSOLE,
    XIANGSHAN_KMH_MY_VIRTIO_NET,
    XIANGSHAN_KMH_MY_VIRTIO_BLK,
    XIANGSHAN_KMH_UART0,
    XIANGSHAN_KMH_CLINT,
    XIANGSHAN_KMH_APLIC_M,
    XIANGSHAN_KMH_APLIC_S,
    XIANGSHAN_KMH_IOMMU_SYS,
    XIANGSHAN_KMH_PCIE0_DBI,
    XIANGSHAN_KMH_SRAM,
    XIANGSHAN_KMH_IMSIC_M,
    XIANGSHAN_KMH_IMSIC_S,
    XIANGSHAN_KMH_UART1,
    XIANGSHAN_KMH_PCIE0_BAR,
    XIANGSHAN_KMH_DRAM,
};

enum {
    XIANGSHAN_KMH_UART0_IRQ = 10,
    XIANGSHAN_KMH_UART1_IRQ = 11,
    XIANGSHAN_KMH_RC_MSI0_IRQ = 12,
    XIANGSHAN_KMH_RC_HP_IRQ = 13,
    XIANGSHAN_KMH_MY_VIRTIO_BLK_IRQ = 15,
    XIANGSHAN_KMH_MY_VIRTIO_NET_IRQ = 16,
    XIANGSHAN_KMH_MY_VIRTIO_CONSOLE_IRQ = 17,
    XIANGSHAN_KMH_IOMMU_SYS_IRQ = 0x24,
};

/* Indicating Timebase-freq (1MHZ) */
#define XIANGSHAN_KMH_CLINT_TIMEBASE_FREQ 1000000

#define XIANGSHAN_KMH_IMSIC_NUM_IDS 255
#define XIANGSHAN_KMH_IMSIC_NUM_GUESTS 7
#define XIANGSHAN_KMH_IMSIC_GUEST_BITS 3

#define XIANGSHAN_KMH_APLIC_NUM_SOURCES 96

#endif
