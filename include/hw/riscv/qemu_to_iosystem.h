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
#include "hw/riscv/riscv_hart.h"
#include "io_dwc_dmac.h"
#include "io_dwc_pcie.h"
#include "io_aplic.h"
#include "io_my_virtio_blk.h"
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
    IoAplic *aplic_m;
    IoAplic *aplic_s;
    IoDwcDmac *dmac;
    IoDwcPcie *pcie;
    IoMyVirtioBlk *my_virtio_blk_dev;
    QemuToIoSystemBridgeWindow io_windows[IO_MANIFEST_DEVICE__COUNT];
    QemuToIoSystemBridgeWindow rtl_system_window;

    OnOffAuto generated_dtb;
    bool my_virtio_blk;
    bool dw_pcie;
    IoSystemBackendKind io_system_backend_kind;
    char *io_system_backend;
    char *io_system_backend_lib;
    char *io_system_vcs_libdir;
    char *my_virtio_blk_image;
    char *io_system_trace_file;
    FILE *io_system_trace_fp;
    size_t io_system_trace_emitted;
    uint64_t fw_jump_fdt_addr;
    int fdt_size;
} QemuToIoSystemState;

#endif
