#include "checkpoint/checkpoint.h"
#include "checkpoint/serializer_utils.h"
#include "cpu_bits.h"
#include "hw/boards.h"
#include "hw/riscv/nemu.h"
#include "qemu/error-report.h"
#include "qemu/osdep.h"
#include "cpu.h"
#include "qemu/typedefs.h"
#include <zlib.h>
#include <zstd.h>

static uint64_t get_next_instructions(void) {
    GList* first_insns_item=g_list_first(simpoint_info.cpt_instructions);
    uint64_t insns=0;
    if (first_insns_item==NULL) {
        return insns;
    } else {
        if (first_insns_item->data==0) {
            simpoint_info.cpt_instructions=g_list_remove(simpoint_info.cpt_instructions,g_list_first(simpoint_info.cpt_instructions)->data);
            path_manager.checkpoint_path_list=g_list_remove(path_manager.checkpoint_path_list,g_list_first(path_manager.checkpoint_path_list)->data);
            return 0;
        }
        return GPOINTER_TO_UINT(first_insns_item->data) * checkpoint.cpt_interval;
    }
}

static int get_env_cpu_mode(void){
    CPUState *cs = qemu_get_cpu(0);
    CPURISCVState *env = cpu_env(cs);
    return env->priv;
}

static uint64_t get_kernel_insns(void){
    CPUState *cs = qemu_get_cpu(0);
    CPURISCVState *env = cpu_env(cs);
    return env->last_seen_insns;
}

static bool instrsCouldTakeCpt(uint64_t icount) {
    uint64_t limit_instructions = get_next_instructions();
    if (limit_instructions==0) {
//        error_printf("simpoint file or weight file error, get limit_instructions 0\n");
        return false;
    }
    limit_instructions += 100000;

    switch (checkpoint.checkpoint_mode) {
    case SimpointCheckpointing:
        if (limit_instructions==0) {
            break;
        } else {
            if (icount >= limit_instructions) {
                info_report("Should take cpt now: %lu limit_instructions: %lu", icount,limit_instructions);
                return true;
            } else if (icount % checkpoint.cpt_interval == 0) {
                info_report("Next cpt @ %lu, now: %lu",
                            limit_instructions, icount);
                break;
            } else {
                break;
            }
        }
    case UniformCheckpointing:
        if (icount >= checkpoint.next_uniform_point) {
            info_report("Should take cpt now: %lu", icount);
            return true;
        }
        break;
    case NoCheckpoint:
        break;
    default:
        break;
    }
    return false;
}

static void notify_taken(uint64_t icount) {
    info_report("Taking checkpoint @ instruction count %lu", icount);
    guint cpt_insns_list_length=g_list_length(simpoint_info.cpt_instructions);
    switch (checkpoint.checkpoint_mode) {
        case SimpointCheckpointing:

            if (cpt_insns_list_length!=0) {
                simpoint_info.cpt_instructions=g_list_remove(simpoint_info.cpt_instructions,g_list_first(simpoint_info.cpt_instructions)->data);
                path_manager.checkpoint_path_list=g_list_remove(path_manager.checkpoint_path_list,g_list_first(path_manager.checkpoint_path_list)->data);
            }

            info_report("left checkpoint numbers: %d",g_list_length(simpoint_info.cpt_instructions));
            break;
        case UniformCheckpointing:
            checkpoint.next_uniform_point += checkpoint.cpt_interval;
            break;
        default:
            break;
    }
}

static void serialize(uint64_t icount) {
    MachineState *ms = MACHINE(qdev_get_machine());
    NEMUState *ns = NEMU_MACHINE(ms);
    serializeRegs(0, ns->memory, &single_core_rvgcvh_default_memlayout, 1, 0);
    serialize_pmem(icount, false, NULL, 0, NULL);
}

static bool could_take_checkpoint(uint64_t icount){
    if (checkpoint.checkpoint_mode==NoCheckpoint) {
        return false;
    }
    if (!checkpoint.workload_loaded) {
        return false;
    }
    if (get_env_cpu_mode()==PRV_M) {
        return false;
    }
    if (!instrsCouldTakeCpt(icount)) {
        return false;
    }
    return true;
}

bool single_core_try_take_cpt(uint64_t icount) {
    uint64_t workload_exec_insns=icount-get_kernel_insns();
    if (could_take_checkpoint(workload_exec_insns)) {
        serialize(workload_exec_insns);
        notify_taken(workload_exec_insns);
        return true;
    }
    return false;
}
