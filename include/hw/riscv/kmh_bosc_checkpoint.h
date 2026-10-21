/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_RISCV_KMH_BOSC_CHECKPOINT_H
#define HW_RISCV_KMH_BOSC_CHECKPOINT_H

#include "hw/boards.h"
#include "hw/char/serial-mm.h"
#include "target/riscv/cpu.h"
#include "qemu/notify.h"

#define KMH_BOSC_CPT_CPU_BASE     UINT64_C(0x80300000)
#define KMH_BOSC_CPT_STUB_BASE    UINT64_C(0x80300000)
#define KMH_BOSC_CPT_CPU_RECORD_SIZE UINT64_C(0x00100000)
#define KMH_BOSC_CPT_CPU_RECORDS  64U
#define KMH_BOSC_CPT_CPU_SIZE     (KMH_BOSC_CPT_CPU_RECORD_SIZE * \
                                   KMH_BOSC_CPT_CPU_RECORDS)
#define KMH_BOSC_CPT_DEVICE_BASE  UINT64_C(0x92000000)
#define KMH_BOSC_CPT_DEVICE_SIZE  UINT64_C(0x00400000)

typedef struct KmhBoscCheckpoint {
    MachineState *machine;
    RISCVCPU *cpu;
    DeviceState *mtimer;
    DeviceState *aplic[8];
    DeviceState *imsic[128];
    SerialMM *uart[4];
    unsigned int num_uart;
    unsigned int num_aplic;
    unsigned int num_imsic;
    bool single_soc;
    uint32_t die_mask;
    uint32_t core_mask[4];
    uint64_t cpu_state_base;
    uint64_t active_hart_mask;
    unsigned int active_hart_count;
    unsigned int trigger_hartid;
    uint32_t format_version;
    uint32_t save_version;
    uint64_t native_pending_mask;
    bool bios_checkpoint;
    bool raw_checkpoint;

    char *checkpoint_path;
    char *checkpoint_load;
    char *checkpoint_stub;
    uint64_t checkpoint_interval;
    uint64_t checkpoint_next;
    bool checkpoint_exit;
    bool checkpoint_marker;
    Notifier machine_done;
    char *restore_path;
    char *save_path;
    bool restore_pending;
    bool restored;
    uint64_t resume_insns;
    bool save_pending;
    bool save_bh_scheduled;
    bool saving;
    unsigned int restoring_harts;
} KmhBoscCheckpoint;

bool kmh_bosc_checkpoint_save(KmhBoscCheckpoint *s, const char *path,
                              Error **errp);
bool kmh_bosc_checkpoint_load(KmhBoscCheckpoint *s, const char *path,
                              Error **errp);
void kmh_bosc_checkpoint_init(KmhBoscCheckpoint *s);
uint64_t kmh_bosc_checkpoint_cpu_state_base(const KmhBoscCheckpoint *s);

#endif
