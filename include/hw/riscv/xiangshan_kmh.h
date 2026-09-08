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
#define XIANGSHAN_KMH_PCIE_NUM 2
#define XIANGSHAN_KMH_MY_VIRTIO_MAX_SLOTS 16
#define XIANGSHAN_KMH_MY_VIRTIO_BASE 0x310A0000ULL
#define XIANGSHAN_KMH_MY_VIRTIO_SLOT_SIZE 0x1000ULL
#define XIANGSHAN_KMH_MY_VIRTIO_IRQ_BASE 15
#define XIANGSHAN_KMH_MY_VIRTIO_ENDPOINT_BASE 0x10
#define XIANGSHAN_KMH_VFIO_PLATFORM_BASE \
    (XIANGSHAN_KMH_MY_VIRTIO_BASE + XIANGSHAN_KMH_MY_VIRTIO_SLOT_SIZE)
#define XIANGSHAN_KMH_VFIO_PLATFORM_SIZE XIANGSHAN_KMH_MY_VIRTIO_SLOT_SIZE
#define XIANGSHAN_KMH_VFIO_PLATFORM_IRQ  (XIANGSHAN_KMH_MY_VIRTIO_IRQ_BASE + 1)
#define XIANGSHAN_KMH_MY_VIRTIO_IOMMU_RELAY_BASE 0x311e0000ULL
#define XIANGSHAN_KMH_MY_VIRTIO_IOMMU_RELAY_SIZE 0x1000ULL

typedef enum XiangshanKmhMyVirtioDevice {
    XIANGSHAN_KMH_MY_VIRTIO_DEVICE_BLK,
    XIANGSHAN_KMH_MY_VIRTIO_DEVICE_NET,
    XIANGSHAN_KMH_MY_VIRTIO_DEVICE_CONSOLE,
    XIANGSHAN_KMH_MY_VIRTIO_DEVICE_GPU,
    XIANGSHAN_KMH_MY_VIRTIO_DEVICE_KEYBOARD,
    XIANGSHAN_KMH_MY_VIRTIO_DEVICE_MOUSE,
    XIANGSHAN_KMH_MY_VIRTIO_DEVICE_TABLET,
    XIANGSHAN_KMH_MY_VIRTIO_DEVICE_GENIRQ,
    XIANGSHAN_KMH_MY_VIRTIO_DEVICE_IOMMU,
    XIANGSHAN_KMH_MY_VIRTIO_DEVICE_COUNT,
} XiangshanKmhMyVirtioDevice;

typedef enum XiangshanKmhMyVirtioIommuMode {
    XIANGSHAN_KMH_MY_VIRTIO_IOMMU_S1,
    XIANGSHAN_KMH_MY_VIRTIO_IOMMU_S2,
    XIANGSHAN_KMH_MY_VIRTIO_IOMMU_S1_S2,
} XiangshanKmhMyVirtioIommuMode;

typedef struct XiangshanKmhMyVirtioSlot {
    XiangshanKmhMyVirtioDevice device;
    hwaddr base;
    uint32_t irq;
} XiangshanKmhMyVirtioSlot;

typedef struct XiangshanKmhSoCState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    RISCVHartArrayState cpus;
    DeviceState *irqchip;
#ifdef CONFIG_KVM_M_MODE
    bool kvm_m_mode;
#endif
    MemoryRegion rom;
    MemoryRegion sram;
    MemoryRegion flash;
    DesignwarePCIEHost pcie[XIANGSHAN_KMH_PCIE_NUM];
    bool dw_pcie;
    bool my_virtio_blk;
    bool my_virtio_net;
    bool my_virtio_console;
    bool my_virtio_gpu;
    bool my_virtio_keyboard;
    bool my_virtio_mouse;
    bool my_virtio_tablet;
    bool my_virtio_genirq;
    uint64_t my_virtio_genirq_relay_delay_ms;
    bool my_virtio_iommu;
    bool my_virtio_iommu_relay;
    bool my_virtio_packed;
    uint32_t my_virtio_iommu_device_mask;
    XiangshanKmhMyVirtioIommuMode my_virtio_iommu_mode;
    uint32_t my_virtio_slot_count;
    XiangshanKmhMyVirtioSlot
        my_virtio_slots[XIANGSHAN_KMH_MY_VIRTIO_MAX_SLOTS];
    void *my_virtio_ui;
    void *my_virtio_iommu_handle;
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
    char *my_virtio_keyboard_backend;
    char *my_virtio_keyboard_evdev_path;
    char *my_virtio_mouse_backend;
    char *my_virtio_mouse_evdev_path;
    char *my_virtio_tablet_backend;
    char *my_virtio_tablet_evdev_path;
    char *my_virtio_vnc_listen;
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
    bool generated_acpi;
    bool autotest_dtb;
    bool dw_pcie;
    bool my_virtio_blk;
    bool my_virtio_net;
    bool my_virtio_console;
    bool my_virtio_gpu;
    bool my_virtio_keyboard;
    bool my_virtio_mouse;
    bool my_virtio_tablet;
    bool my_virtio_genirq;
    uint64_t my_virtio_genirq_relay_delay_ms;
    bool my_virtio_iommu;
    bool my_virtio_iommu_relay;
    bool my_virtio_vfio_platform;
    bool my_virtio_packed;
    uint32_t my_virtio_iommu_device_mask;
    XiangshanKmhMyVirtioIommuMode my_virtio_iommu_mode;
    uint32_t my_virtio_slot_count;
    XiangshanKmhMyVirtioSlot
        my_virtio_slots[XIANGSHAN_KMH_MY_VIRTIO_MAX_SLOTS];
#ifdef CONFIG_KVM_M_MODE
    bool kvm_m_mode;
#endif
#ifdef CONFIG_KVM_NESTED
    bool kvm_nested;
#endif
    void *my_virtio_ui;
    char *my_virtio_devices;
    char *my_virtio_iommu_devices;
    char *my_virtio_iommu_mode_str;
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
    char *my_virtio_keyboard_backend;
    char *my_virtio_keyboard_evdev_path;
    char *my_virtio_mouse_backend;
    char *my_virtio_mouse_evdev_path;
    char *my_virtio_tablet_backend;
    char *my_virtio_tablet_evdev_path;
    char *my_virtio_vnc_listen;
    uint64_t acpi_handoff_addr;
    uint64_t acpi_handoff_size;
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
    XIANGSHAN_KMH_MY_VIRTIO,
    XIANGSHAN_KMH_UART0,
    XIANGSHAN_KMH_CLINT,
    XIANGSHAN_KMH_APLIC_M,
    XIANGSHAN_KMH_APLIC_S,
    XIANGSHAN_KMH_IOMMU_SYS,
    XIANGSHAN_KMH_PCIE0_DBI,
    XIANGSHAN_KMH_PCIE1_DBI,
    XIANGSHAN_KMH_SRAM,
    XIANGSHAN_KMH_IMSIC_M,
    XIANGSHAN_KMH_IMSIC_S,
    XIANGSHAN_KMH_UART1,
    XIANGSHAN_KMH_PCIE0_BAR,
    XIANGSHAN_KMH_PCIE1_BAR,
    XIANGSHAN_KMH_DRAM,
};

enum {
    XIANGSHAN_KMH_UART0_IRQ = 10,
    XIANGSHAN_KMH_UART1_IRQ = 11,
    XIANGSHAN_KMH_RC_MSI0_IRQ = 12,
    XIANGSHAN_KMH_RC_HP0_IRQ = 13,
    XIANGSHAN_KMH_RC_MSI1_IRQ = 31,
    XIANGSHAN_KMH_RC_HP1_IRQ = 32,
    XIANGSHAN_KMH_IOMMU_SYS_IRQ = 0x24,
};

/* Indicating Timebase-freq (1MHZ) */
#define XIANGSHAN_KMH_CLINT_TIMEBASE_FREQ 1000000

#define XIANGSHAN_KMH_IMSIC_NUM_IDS 255
/* Three guest-index bits with five implemented guest interrupt files. */
#define XIANGSHAN_KMH_IMSIC_NUM_GUESTS 5
#define XIANGSHAN_KMH_IMSIC_GUEST_BITS 3

#define XIANGSHAN_KMH_APLIC_NUM_SOURCES 96

void xiangshan_kmh_acpi_setup(XiangshanKmhState *s);

#endif
