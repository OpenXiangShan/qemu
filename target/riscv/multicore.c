#include "checkpoint/checkpoint.h"
#include "cpu_bits.h"
#include "exec/cpu-common.h"
#include "hw/core/cpu.h"
#include "hw/qdev-core.h"
#include "hw/riscv/nemu.h"
#include "qemu/error-report.h"
#include "cpu.h"
#include "qemu/typedefs.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/cdefs.h>
#include <zlib.h>
#include <zstd.h>
#include "hw/boards.h"
#include "checkpoint/checkpoint.h"
#include "checkpoint/checkpoint.pb.h"
#include "checkpoint/serializer_utils.h"
#include "sysemu/cpu-timers.h"
#include "sysemu/runstate.h"

GMutex sync_lock;
GCond sync_signal;

#define NO_PRINT

static uint64_t global_mtime = 0;

__attribute__((unused)) static void set_global_mtime(void)
{ // maybe unused
    cpu_physical_memory_read(CLINT_MMIO + CLINT_MTIME, &global_mtime, 8);
}

static bool try_take_single_core_checkpoint = false;
void multicore_checkpoint_init(MachineState *machine)
{
    MachineState *ms = machine;
    NEMUState *ns = NEMU_MACHINE(ms);
    int64_t cpus = ms->smp.cpus;

    g_mutex_init(&sync_lock);

    g_mutex_lock(&sync_lock);

    ns->sync_info.cpus = cpus;
    if (cpus == 1) {
        try_take_single_core_checkpoint = true;
    }

    ns->sync_info.workload_exit_percpu = g_malloc0(cpus * sizeof(uint8_t));
    ns->sync_info.workload_loaded_percpu = g_malloc0(cpus * sizeof(uint8_t));
    ns->sync_info.workload_insns = g_malloc0(cpus * sizeof(uint64_t));
    ns->sync_info.early_exit = g_malloc0(cpus * sizeof(bool));
    ns->sync_info.checkpoint_end = g_malloc0(cpus * sizeof(bool));
    g_mutex_unlock(&sync_lock);
}

static uint64_t workload_insns(int cpu_index)
{
    CPUState *cs = qemu_get_cpu(cpu_index);
    CPURISCVState *env = cpu_env(cs);
    return env->profiling_insns - env->last_seen_insns;
}

static uint64_t cpt_limit_instructions(NEMUState *ns)
{
    if (ns->checkpoint_info.checkpoint_mode == UniformCheckpointing) {
        return ns->checkpoint_info.next_uniform_point;
    } else if (ns->checkpoint_info.checkpoint_mode == SimpointCheckpointing) {
        return get_next_instructions(ns);
    }{
        return 0;
    }
}

static uint64_t sync_limit_instructions(NEMUState *ns)
{
    if (ns->checkpoint_info.checkpoint_mode == UniformCheckpointing) {
        return ns->sync_info.sync_interval;
    }else {
        return cpt_limit_instructions(ns);
    }
}

__attribute_maybe_unused__ static int get_env_cpu_mode(uint64_t cpu_idx)
{
    CPUState *cs = qemu_get_cpu(cpu_idx);
    CPURISCVState *env = cpu_env(cs);
    return env->priv;
}

static bool sync_and_check_take_checkpoint(NEMUState *ns,
                                           uint64_t workload_exec_insns,
                                           uint64_t cpu_idx,
                                           bool *should_take_cpt,
                                           bool early_exit)
{
#ifndef NO_PRINT
    __attribute__((unused)) int r = rand() % 100000;
    if (r < 10) {
        fprintf(stderr, "%s: cpu %ld trying to obtain lock\n", __func__,
                cpu_idx);
    }
#endif
    g_mutex_lock(&sync_lock);
    // single cpu check
    if (ns->sync_info.workload_loaded_percpu[cpu_idx] != 0x1) {
#ifndef NO_PRINT
        if (r < 10) {
            fprintf(stderr, "%s: cpu %ld not seen before workload\n", __func__,
                    cpu_idx);
        }
#endif
        goto failed;
    }

    if (ns->sync_info.workload_exit_percpu[cpu_idx] == 0x1) {
#ifndef NO_PRINT
        if (r < 10) {
            fprintf(stderr, "%s: cpu %ld has exited\n", __func__, cpu_idx);
        }
#endif
        goto failed;
    }

    // all cpu check, do not wait for before workload
    for (int i = 0; i < ns->sync_info.cpus; i++) {
        if (ns->sync_info.workload_loaded_percpu[i] != 0x1) {
#ifndef NO_PRINT
            if (r < 10) {
                fprintf(stderr,
                        "%s: cpu %ld: other core has not executed "
                        "before_workload\n",
                        __func__, cpu_idx);
            }
#endif
            goto failed;
        }
    }

    // set halt flag manually
    if (early_exit) {
        CPUState *cs = qemu_get_cpu(cpu_idx);
        cs->halted = 1;
        cs->exception_index = EXCP_HLT;
    }

    int wait_cpus = 0; // executed enough instructions or halt
    int online_cpus = 0; // not exited
    int halt_cpus = 0; //  not exited but halt

    for (int i = 0; i < ns->sync_info.cpus; i++) {
        // idx i hart more than limit instructions and workload not exit, this
        // hart could wait
        // count online cpu
        if (ns->sync_info.workload_exit_percpu[i] != 0x1) {
            online_cpus += 1;
        }

        // count halt
        CPUState *cs = qemu_get_cpu(i);
        bool halt = cs->halted == 1;
        if (ns->sync_info.workload_exit_percpu[i] != 0x1 && halt) {
            halt_cpus += 1;
        }

        // halt or workload insns >= sync limit -> goto wait
        if (halt || workload_insns(i) >= sync_limit_instructions(ns)) {
            if (ns->sync_info.workload_exit_percpu[i] != 0x1) {
                wait_cpus += 1;
            }
        }
    }

    // if this cpu get early wait, could go on
    if (early_exit) {
        ns->sync_info.early_exit[cpu_idx] = true;
    // if not get early wait but get limit instructions, could go on
    } else if (workload_exec_insns >= sync_limit_instructions(ns)) {
        ns->sync_info.workload_insns[cpu_idx] = workload_exec_insns;
    // else must goto failed
    } else {
#ifndef NO_PRINT
        if (r < 10 || (wait_cpus == 1)) {
            CPUState *cs = qemu_get_cpu(cpu_idx);
            CPURISCVState *env = cpu_env(cs);
            fprintf(
                stderr,
                "%s: cpu %ld: has not reached limit insns: %lu at pc: %#lx\n",
                __func__, cpu_idx, workload_insns(cpu_idx), env->pc);
        }
#endif
        goto failed;
    }

    if (wait_cpus < online_cpus) {
        goto wait;
    }

    uint64_t limit_instructions = cpt_limit_instructions(ns);
    if (limit_instructions == 0) {
        *should_take_cpt = 0;
    } else if(workload_exec_insns >= limit_instructions){
        *should_take_cpt = 1;
    } else {
        *should_take_cpt = 0;
    }

    g_mutex_unlock(&sync_lock);
    // all hart get sync node
    return true;

wait:
//#ifndef NO_PRINT
    fprintf(stderr,
            "cpu %ld get wait with insns: %lu, sync point: %lu, early exit "
            "this period: %i, online: %i, wait cpus: %i, halt cpus: %i\n",
            cpu_idx, workload_insns(cpu_idx), sync_limit_instructions(ns),
            ns->sync_info.early_exit[cpu_idx], online_cpus, wait_cpus, halt_cpus);
//#endif


    if (wait_cpus == 1) {
        cpu_disable_ticks();
        set_global_mtime();
    }

    // wait for checkpoint thread set flag true
    while (!ns->sync_info.checkpoint_end[cpu_idx]) {
        g_cond_wait(&sync_signal, &sync_lock);
    }

//#ifndef NO_PRINT
    fprintf(stderr, "cpu: %ld get the sync end, core0: %lu, core1: %lu\n",
            cpu_idx, workload_insns(0), workload_insns(1));
//#endif

    // reset status
    ns->sync_info.checkpoint_end[cpu_idx] = false;
    g_mutex_unlock(&sync_lock);

    return false;

failed:
    g_mutex_unlock(&sync_lock);
    return false;
}

__attribute_maybe_unused__ static void
serialize(uint64_t memory_addr, int cpu_index, int cpus, uint64_t inst_count)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    NEMUState *ns = NEMU_MACHINE(ms);
    checkpoint_header cpt_header = default_cpt_header;
    single_core_rvgc_rvv_rvh_memlayout cpt_percpu_layout =
        single_core_rvgcvh_default_memlayout;
    uint64_t serialize_reg_base_addr;
    bool using_gcpt_mmio = false;
    char *hardware_status_buffer = (char *)ns->gcpt_memory;

#ifdef USING_PROTOBUF


    serialize_reg_base_addr =
        (uint64_t)cpt_header.cpt_offset + (uint64_t)hardware_status_buffer;

    cpt_header_encode(hardware_status_buffer, &cpt_header, &cpt_percpu_layout);
#else
    serialize_reg_base_addr = (uint64_t)hardware_status_buffer;

#endif

    for (int i = 0; i < cpus; i++) {
        serializeRegs(i, (char *)(serialize_reg_base_addr + i * (1024 * 1024)),
                      &cpt_percpu_layout, cpt_header.cpu_num, global_mtime);
        info_report("buffer %d serialize success, start addr %lx\n", i,
                    serialize_reg_base_addr + (i * 1024 * 1024));
    }

    if (!using_gcpt_mmio) {
        cpu_physical_memory_write(memory_addr, hardware_status_buffer,
                                  cpt_header.cpt_offset + 1024 * 1024 * cpus);
    }
    serialize_pmem(inst_count, false, hardware_status_buffer,
                   cpt_header.cpt_offset + 1024 * 1024 * cpus);
}

static bool all_cpu_exit(NEMUState *ns, uint64_t cpu_idx)
{
#ifndef NO_PRINT
    fprintf(stderr, "%s: cpu %ld trying to obtain lock\n", __func__, cpu_idx);
#endif
    g_mutex_lock(&sync_lock);
    int load_worload_num = 0;
    int exit_worload_num = 0;
    for (int i = 0; i < ns->sync_info.cpus; i++) {
        if (ns->sync_info.workload_loaded_percpu[i] == 0x1) {
            load_worload_num++;
            // only loaded workload could set exit
            if (ns->sync_info.workload_exit_percpu[i] == 0x1) {
                exit_worload_num++;
            }
        }
    }

    g_mutex_unlock(&sync_lock);

    if (load_worload_num == exit_worload_num) {
        return true;
    } else {
        return false;
    }
}

static void update_sync_interval(NEMUState *ns){
    switch (ns->checkpoint_info.checkpoint_mode) {
        case UniformCheckpointing:
            ns->sync_info.next_sync_point += ns->sync_info.sync_interval;
            break;
        case SimpointCheckpointing:
            break;
        default:
            break;
    }
}

static void update_last_seen_insns(NEMUState *ns){
    switch (ns->checkpoint_info.checkpoint_mode) {
        case UniformCheckpointing:
            for (int i = 0; i < ns->sync_info.cpus; i++) {
                CPUState *cs = qemu_get_cpu(i);
                CPURISCVState *env = cpu_env(cs);
                env->last_seen_insns = env->profiling_insns;
            }
            break;
        case SimpointCheckpointing:
            break;
        default:
            break;
    }
}

bool multi_core_try_take_cpt(NEMUState* ns, uint64_t icount, uint64_t cpu_idx,
                             bool exit_sync_period)
{
    if (ns->checkpoint_info.checkpoint_mode == NoCheckpoint) {
        return false;
    }

    bool should_take_cpt = false;
    
    if (sync_and_check_take_checkpoint(ns, workload_insns(cpu_idx), cpu_idx,
                                       &should_take_cpt, exit_sync_period)) {
#ifndef NO_PRINT
        fprintf(stderr, "%s: cpu %ld finished sync lastly, exit period: %i\n",
                __func__, cpu_idx, exit_sync_period);
#endif
        g_mutex_lock(&sync_lock);

        // start checkpoint
        if (should_take_cpt) {
            serialize(0x80300000, cpu_idx, ns->sync_info.cpus,
                      workload_insns(cpu_idx));
        }

        // checkpoint end, set all flags
        for (int i = 0; i < ns->sync_info.cpus; i++) {
            ns->sync_info.checkpoint_end[i] = true;
        }

//#ifndef NO_PRINT
        fprintf(stderr, "cpu: %ld get the broadcast, core0: %lu, core1: %lu\n",
                cpu_idx, workload_insns(0), workload_insns(1));
//#endif
        for (int i = 0; i < ns->sync_info.cpus; i++) {
#ifndef NO_PRINT
            fprintf(stderr, "cpu %d, insns %ld\n", i, workload_insns(i));
#endif
        }

        if (should_take_cpt) {
            if (ns->checkpoint_info.checkpoint_mode == UniformCheckpointing) {
                // update checkpoint limit instructions
                update_cpt_limit_instructions(ns, icount);
                // update sync interval
                update_sync_interval(ns);
                // reset status
                memset(ns->sync_info.early_exit, 0, ns->sync_info.cpus * sizeof(bool));

                update_last_seen_insns(ns);
            }
        }

        // reset self flag
        ns->sync_info.checkpoint_end[cpu_idx] = false;

        cpu_enable_ticks();

        g_cond_broadcast(&sync_signal);

        g_mutex_unlock(&sync_lock);
    }

    if (ns->checkpoint_info.workload_exit && all_cpu_exit(ns, cpu_idx)) {
        // exit;
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_HOST_QMP_QUIT);
    }

    return false;
}

void try_set_mie(void *env)
{
    if (try_take_single_core_checkpoint) {
        ((CPURISCVState *)env)->mie =
            (((CPURISCVState *)env)->mie & (~(1 << 7)));
        ((CPURISCVState *)env)->mie =
            (((CPURISCVState *)env)->mie & (~(1 << 5)));
    }
}

bool try_take_cpt(uint64_t inst_count, uint64_t cpu_idx, bool exit_sync_period)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    NEMUState *ns = NEMU_MACHINE(ms);
 
    if (try_take_single_core_checkpoint) {
        return single_core_try_take_cpt(ns, inst_count);
    } else {
        return multi_core_try_take_cpt(ns, inst_count, cpu_idx, exit_sync_period);
    }
}
