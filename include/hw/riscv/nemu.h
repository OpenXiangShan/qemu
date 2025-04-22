#ifndef HW_RISCV_NEMU_H
#define HW_RISCV_NEMU_H

#include "hw/boards.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/sysbus.h"
#include "stdint.h"
#include "checkpoint/directed_tbs.h"

#define NEMU_CPUS_MAX 128
#define NEMU_CPUS_MIN 1

#define TYPE_NEMU_MACHINE MACHINE_TYPE_NAME("nemu")
typedef struct NEMUState NEMUState;
DECLARE_INSTANCE_CHECKER(NEMUState, NEMU_MACHINE,
                         TYPE_NEMU_MACHINE)

typedef struct NEMUConfig{} NEMUConfig;

typedef uint64_t (*get_cpt_limit_instructions_func)(NEMUState *ns);
typedef uint64_t (*get_sync_limit_instructions_func)(NEMUState *ns, int cpu_idx);
typedef void (*try_take_cpt_func)(NEMUState* ns, uint64_t icount, int cpu_idx, bool exit_sync_period);
typedef void (*after_take_cpt_func)(NEMUState *ns, int cpu_idx);
typedef void (*try_set_mie_func)(void *env, NEMUState *ns);
typedef void (*update_cpt_limit_instructions_func)(NEMUState *ns, uint64_t icount);
typedef void (*update_last_seen_instructions_func)(NEMUState *ns, int cpu_idx, uint64_t profiling_insns);
typedef void (*update_sync_limit_instructions_func)(NEMUState *ns);

typedef struct {
    update_last_seen_instructions_func update_last_seen_instructions;
    update_cpt_limit_instructions_func update_cpt_limit_instructions;
    update_sync_limit_instructions_func update_sync_limit_instructions;
    get_cpt_limit_instructions_func get_cpt_limit_instructions;
    get_sync_limit_instructions_func get_sync_limit_instructions;
    try_take_cpt_func try_take_cpt;
    after_take_cpt_func after_take_cpt;
    try_set_mie_func try_set_mie;
} CheckpointFunc;

#define DEF_FUNC(type, name, args, body) \
    static type name args body

#define MODE_DEF_HELPER(mode, \
    get_cpt_limit_insns_ret, get_cpt_limit_insns_name, get_cpt_limit_insns_args, get_cpt_limit_insns_body, \
    get_sync_limit_insns_ret, get_sync_limit_insns_name, get_sync_limit_insns_args, get_sync_limit_insns_body, \
    take_checkpoint_ret, take_checkpoint_name, take_checkpoint_args, take_checkpoint_body, \
    after_take_checkpoint_ret, after_take_checkpoint_name, after_take_checkpoint_args, after_take_checkpoint_body, \
    update_cpt_limit_insns_ret, update_cpt_limit_insns_name, update_cpt_limit_insns_args, update_cpt_limit_insns_body, \
    set_env_mie_ret, set_env_mie_name, set_env_mie_args, set_env_mie_body, \
    update_sync_limit_instructions_ret, update_sync_limit_instructions_name, update_sync_limit_instructions_args, update_sync_limit_instructions_body) \
DEF_FUNC(get_cpt_limit_insns_ret, get_cpt_limit_insns_name##_##mode, get_cpt_limit_insns_args, get_cpt_limit_insns_body) \
DEF_FUNC(get_sync_limit_insns_ret, get_sync_limit_insns_name##_##mode, get_sync_limit_insns_args, get_sync_limit_insns_body) \
DEF_FUNC(take_checkpoint_ret, take_checkpoint_name##_##mode, take_checkpoint_args, take_checkpoint_body) \
DEF_FUNC(after_take_checkpoint_ret, after_take_checkpoint_name##_##mode, after_take_checkpoint_args, after_take_checkpoint_body) \
DEF_FUNC(update_cpt_limit_insns_ret, update_cpt_limit_insns_name##_##mode, update_cpt_limit_insns_args, update_cpt_limit_insns_body) \
DEF_FUNC(set_env_mie_ret, set_env_mie_name##_##mode, set_env_mie_args, set_env_mie_body) \
DEF_FUNC(update_sync_limit_instructions_ret, update_sync_limit_instructions_name##_##mode, update_sync_limit_instructions_args, update_sync_limit_instructions_body) \
static CheckpointFunc mode##_func = { \
    .get_cpt_limit_instructions = get_cpt_limit_insns_name##_##mode, \
    .get_sync_limit_instructions = get_sync_limit_insns_name##_##mode, \
    .try_take_cpt = take_checkpoint_name##_##mode, \
    .after_take_cpt = after_take_checkpoint_name##_##mode, \
    .try_set_mie = set_env_mie_name##_##mode, \
    .update_cpt_limit_instructions = update_cpt_limit_insns_name##_##mode, \
    .update_sync_limit_instructions = update_sync_limit_instructions_name##_##mode \
};

typedef struct Checkpoint{
    uint64_t next_uniform_point;
}Checkpoint_t;

typedef struct PathManager{
    GString *simpoint_path;
    GString *uniform_path;
    GList *checkpoint_path_list;
}PathManager_t;

typedef struct SimpointInfo{
    GList *cpt_instructions;
    GList *weights;
}SimpointInfo_t;

typedef struct sync_info{
    gint cpus;
    gint *online;
    gint online_cpus;

    uint64_t uniform_sync_limit;
    int64_t *workload_insns;
    int64_t *kernel_insns;

    bool *early_exit;  // such as wfi
    gint *checkpoint_end;
    gint *waiting;
}SyncInfo_t;

enum CheckpointMode{
    NoCheckpoint=0,
    SimpointCheckpointing,
    UniformCheckpointing,
    SyncUniformCheckpoint,
};

typedef enum{
    RUNNING,
    WAIT,
    EXIT,
    HALT
}CheckpointState;

typedef struct{
    char *checkpoint;
    char *gcpt_restore;
    char *simpoint_path;

    GString *config_name;
    GString *base_dir;
    GString *workload_name;

    uint64_t sync_interval;
    uint64_t cpt_interval;
    uint64_t warmup_interval;
    uint64_t checkpoint_mode;
    bool skip_boot;

    uint64_t limit_instrucitons;
}NEMUArgs_t;

struct NEMUState{
    /*< private >*/
    MachineState parent;

    /*< public >*/
    RISCVHartArrayState soc[NEMU_CPUS_MAX];

    Checkpoint_t checkpoint_info;
    PathManager_t path_manager;
    SimpointInfo_t simpoint_info;
    SyncInfo_t sync_info;
    CheckpointFunc cpt_func;

    struct CPUState **cs_vec;
    SyncControlInfo sync_control_info;
    Qemu2Detail q2d_buf;
    NEMUArgs_t nemu_args;

    int d2q_fifo;
    int q2d_fifo;

    NEMUConfig cfg;
    char* memory;
    char* gcpt_memory;

    DeviceState *irqchip[NEMU_CPUS_MAX];
};


#endif
