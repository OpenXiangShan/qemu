#ifndef DIRECTED_TBS_H
#define DIRECTED_TBS_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_HARTS 8

typedef struct Qemu2Detail {
    bool cpt_ready;
    uint32_t cpt_id;
    uint64_t total_inst_count;
    char checkpoint_path[256];
} Qemu2Detail;

typedef struct Detail2Qemu {
    double CPI[MAX_HARTS];
    bool has_wfi[MAX_HARTS];
} Detail2Qemu;

typedef struct SyncControlInfo {
    Detail2Qemu u_arch_info;
    int info_vaild_periods;
} SyncControlInfo;

#endif
