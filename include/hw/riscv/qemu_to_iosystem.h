/*
 * QEMU RISC-V board that fronts the external io-system C-model.
 *
 * Copyright (c) 2026 Beijing Institute of Open Source Chip (BOSC)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RISCV_QEMU_TO_IOSYSTEM_H
#define HW_RISCV_QEMU_TO_IOSYSTEM_H

#include "hw/boards.h"
#include "hw/irq.h"
#include "qemu/main-loop.h"
#include "qemu/notify.h"
#include "qemu/thread.h"
#include "hw/riscv/riscv_hart.h"
#include "io_system.h"

#define TYPE_QEMU_TO_IOSYSTEM_MACHINE MACHINE_TYPE_NAME("qemu_to_iosystem")
OBJECT_DECLARE_SIMPLE_TYPE(QemuToIoSystemState,
                           QEMU_TO_IOSYSTEM_MACHINE)

#define QEMU_TO_IOSYSTEM_MAX_CPUS 16

typedef struct QemuToIoSystemBridgeWindow {
    MemoryRegion mr;
    const IoManifestEntry *entry;
    QemuToIoSystemState *machine;
    const char *name;
    uint64_t base;
} QemuToIoSystemBridgeWindow;

typedef struct QemuToIoSystemState {
    MachineState parent_obj;

    RISCVHartArrayState cpus;

    MemoryRegion rom;
    MemoryRegion sram;
    MemoryRegion flash;

    IoSystem *io_system;
    Notifier io_system_exit_notifier;
    bool io_system_exit_notifier_registered;
    QemuToIoSystemBridgeWindow io_windows[IO_MANIFEST_DEVICE__COUNT];
    QemuToIoSystemBridgeWindow rtl_system_window;

    OnOffAuto generated_dtb;
    bool my_virtio_blk;
    bool dw_pcie;
    bool io_system_pcie_iommu_map;
    IoSystemBackendKind io_system_backend_kind;
    IoSystemIommuKind io_system_iommu_kind;
    IoSystemIommuPlacement io_system_iommu_placement;
    char *io_system_backend;
    char *io_system_iommu;
    char *io_system_iommu_placement_str;
    char *io_system_backend_lib;
    char *io_system_vcs_libdir;
    char *io_system_iommu_refmodel_dir;
    char *io_system_iommu_rtl_ip_dir;
    char *io_system_iommu_picker_out;
    char *io_system_iommu_vcs_libdir;
    char *my_virtio_blk_image;
    char *io_system_trace_file;
    FILE *io_system_trace_fp;
    QEMUBH *io_system_posted_msi_bh;
    QemuMutex io_system_posted_msi_lock;
    struct QtiPostedMsi *io_system_posted_msi_head;
    struct QtiPostedMsi *io_system_posted_msi_tail;
    size_t io_system_trace_emitted;
    uint64_t io2q_outstanding;
    bool io2q_async;
    IoSystemServiceMode service_mode;
    char *io_system_service_mode;
    QEMUBH *io_system_service_bh;
    CPUState *io_system_service_cpu;
    uint32_t io_system_service_work_pending;
    uint64_t fw_jump_fdt_addr;
    int fdt_size;
} QemuToIoSystemState;

#endif
