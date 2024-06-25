#include "checkpoint/checkpoint.h"
#include "checkpoint/directed_tbs.h"
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
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

GMutex sync_lock;
sync_info_t sync_info;
SyncControlInfo sync_control_info;
Qemu2Detail q2d_buf;
GCond sync_signal;

int d2q_fifo;
int q2d_fifo;

#define NO_PRINT

static uint64_t global_mtime = 0;

__attribute__((unused)) static void set_global_mtime(void)
{ // maybe unused
    cpu_physical_memory_read(CLINT_MMIO + CLINT_MTIME, &global_mtime, 8);
}

static bool try_take_single_core_checkpoint = false;
void multicore_checkpoint_init(void)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    int64_t cpus = ms->smp.cpus;

    g_mutex_init(&sync_lock);

    g_mutex_lock(&sync_lock);

    sync_info.cpus = cpus;
    if (cpus == 1) {
        try_take_single_core_checkpoint = true;
    }

    sync_info.workload_exit_percpu = g_malloc0(cpus * sizeof(uint8_t));
    sync_info.workload_loaded_percpu = g_malloc0(cpus * sizeof(uint8_t));
    sync_info.workload_insns = g_malloc0(cpus * sizeof(uint64_t));
    sync_info.early_exit = g_malloc0(cpus * sizeof(bool));
    sync_info.checkpoint_end = g_malloc0(cpus * sizeof(bool));

    const char *detail_to_qemu_fifo_name = "./detail_to_qemu.fifo";
    d2q_fifo = open(detail_to_qemu_fifo_name, O_RDONLY);

    const char *qemu_to_detail_fifo_name = "./qemu_to_detail.fifo";
    q2d_fifo = open(qemu_to_detail_fifo_name, O_WRONLY);

    sync_control_info.info_vaild_periods = 1;

    g_mutex_unlock(&sync_lock);
}

static uint64_t workload_insns(int cpu_index)
{
    CPUState *cs = qemu_get_cpu(cpu_index);
    CPURISCVState *env = cpu_env(cs);
    return env->profiling_insns - env->last_seen_insns;
}

// prepare merge single core checkpoint
static uint64_t get_next_instructions(void)
{
    return 0;
}

static uint64_t cpt_limit_instructions(void)
{
    if (checkpoint.checkpoint_mode == UniformCheckpointing) {
        return checkpoint.next_uniform_point;
    } else {
        return get_next_instructions();
    }
}

static uint64_t sync_limit_instructions(uint64_t cpu_idx)
{
    return (uint64_t)((double)sync_info.sync_interval /
                      sync_control_info.u_arch_info.CPI[cpu_idx]);
}

static void update_uniform_limit_inst(bool taken_cpt)
{
    if (taken_cpt) {
        checkpoint.next_uniform_point += checkpoint.cpt_interval;
        q2d_buf.cpt_ready = true;
        // q2d_buf.cpt_id = 0;
        // q2d_buf.total_inst_count = checkpoint.next_uniform_point;

        write(q2d_fifo, &q2d_buf, sizeof(Qemu2Detail));
    }

    sync_info.next_sync_point += sync_info.sync_interval;
    memset(sync_info.early_exit, 0, sync_info.cpus * sizeof(bool));

    sync_control_info.info_vaild_periods -= 1;
    if (sync_control_info.info_vaild_periods <= 0) {
        // For test, TODO remove it in real sim
        write(q2d_fifo, &q2d_buf, sizeof(Qemu2Detail));
#ifndef NO_PRINT
        fprintf(stderr, "Obtaining new sync control info\n");
#endif
        read(d2q_fifo, &sync_control_info.u_arch_info,
             sizeof(Detail2Qemu));
        // For test, TODO remove it in real sim
        sync_control_info.info_vaild_periods = 3;
        // For real
        // sync_control_info.info_vaild_periods = checkpoint.cpt_interval /
        //                                       sync_info.sync_interval;
#ifndef NO_PRINT
        fprintf(stderr,
                "Obtained new sync control info, valid thru %i periods\n",
                sync_control_info.info_vaild_periods);
#endif
    }
}

__attribute__ ((__unused__)) static int get_env_cpu_mode(uint64_t cpu_idx)
{
    CPUState *cs = qemu_get_cpu(cpu_idx);
    CPURISCVState *env = cpu_env(cs);
    return env->priv;
}

static bool sync_and_check_take_checkpoint(uint64_t workload_exec_insns,
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
    if (sync_info.workload_loaded_percpu[cpu_idx] != 0x1) {
#ifndef NO_PRINT
        if (r < 10) {
            fprintf(stderr, "%s: cpu %ld not seen before workload\n", __func__,
                    cpu_idx);
        }
#endif
        goto failed;
    }

    if (sync_info.workload_exit_percpu[cpu_idx] == 0x1) {
#ifndef NO_PRINT
        if (r < 10) {
            fprintf(stderr, "%s: cpu %ld has exited\n", __func__, cpu_idx);
        }
#endif
        goto failed;
    }

    // all cpu check, do not wait for before workload
    for (int i = 0; i < sync_info.cpus; i++) {
        if (sync_info.workload_loaded_percpu[i] != 0x1) {
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

    if (early_exit) {
        CPUState *cs = qemu_get_cpu(cpu_idx);
        cs->halted = 1;
        cs->exception_index = EXCP_HLT;
    }

    int wait_cpus = 0; // executed enough instructions or halt
    int online_cpus = 0; // not exited
    __attribute__ ((__unused__)) int halt_cpus = 0; //  not exited but halt

    for (int i = 0; i < sync_info.cpus; i++) {
        // idx i hart less than limit instructions and workload not exit, this
        // hart could wait
        if (sync_info.workload_exit_percpu[i] != 0x1) {
            online_cpus += 1;
        }
        CPUState *cs = qemu_get_cpu(i);
        bool halt = cs->halted == 1;
        bool this_cpu_exit_sync_period =
            i == cpu_idx ? early_exit : sync_info.early_exit[i];
        if (sync_info.workload_exit_percpu[i] != 0x1 && halt) {
            halt_cpus += 1;
        }
        if (this_cpu_exit_sync_period || halt ||
            workload_insns(i) >= sync_limit_instructions(i)) {
            if (sync_info.workload_exit_percpu[i] != 0x1) {
                wait_cpus += 1;
            }
        }
    }

    // when set limit instructions, hart must goto wait
    if (early_exit) {
        sync_info.early_exit[cpu_idx] = true;
    } else if (workload_exec_insns >= sync_limit_instructions(cpu_idx)) {
        sync_info.workload_insns[cpu_idx] = workload_exec_insns;
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

    *should_take_cpt = workload_exec_insns >= cpt_limit_instructions();

    g_mutex_unlock(&sync_lock);
    // all hart get sync node
    return true;

wait:
#ifndef NO_PRINT
    fprintf(stderr,
            "cpu %ld get wait with insns: %lu, sync point: %lu, early exit "
            "this period: %i, online: %i, wait cpus: %i, halt cpus: %i\n",
            cpu_idx, workload_insns(cpu_idx), sync_limit_instructions(cpu_idx),
            sync_info.early_exit[cpu_idx], online_cpus, wait_cpus, halt_cpus);
#endif


    if (wait_cpus == 1) {
        cpu_disable_ticks();
        set_global_mtime();
    }

    // wait for checkpoint thread set flag true
    while (!sync_info.checkpoint_end[cpu_idx]) {
        g_cond_wait(&sync_signal, &sync_lock);
    }

#ifndef NO_PRINT
    fprintf(stderr, "cpu: %ld get the sync end, core0: %lu, core1: %lu\n",
            cpu_idx, workload_insns(0), workload_insns(1));
#endif

    // reset status
    sync_info.checkpoint_end[cpu_idx] = false;
    g_mutex_unlock(&sync_lock);

    return false;

failed:
    g_mutex_unlock(&sync_lock);
    return false;
}

__attribute__ ((__unused__)) static void
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
                   cpt_header.cpt_offset + 1024 * 1024 * cpus, &q2d_buf);
}

static bool all_cpu_exit(uint64_t cpu_idx)
{
#ifndef NO_PRINT
    fprintf(stderr, "%s: cpu %ld trying to obtain lock\n", __func__, cpu_idx);
#endif
    g_mutex_lock(&sync_lock);
    int load_worload_num = 0;
    int exit_worload_num = 0;
    for (int i = 0; i < sync_info.cpus; i++) {
        if (sync_info.workload_loaded_percpu[i] == 0x1) {
            load_worload_num++;
            // only loaded workload could set exit
            if (sync_info.workload_exit_percpu[i] == 0x1) {
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

bool multi_core_try_take_cpt(uint64_t icount, uint64_t cpu_idx,
                             bool exit_sync_period)
{
    if (checkpoint.checkpoint_mode == NoCheckpoint) {
        return false;
    }

    bool should_take_cpt = false;
    if (sync_and_check_take_checkpoint(workload_insns(cpu_idx), cpu_idx,
                                       &should_take_cpt, exit_sync_period)) {
#ifndef NO_PRINT
        fprintf(stderr, "%s: cpu %ld finished sync lastly, exit period: %i\n",
                __func__, cpu_idx, exit_sync_period);
#endif
        g_mutex_lock(&sync_lock);

        // start checkpoint
        if (should_take_cpt) {
            serialize(0x80300000, cpu_idx, sync_info.cpus,
                      workload_insns(cpu_idx));
        }

        // checkpoint end, set all flags
        for (int i = 0; i < sync_info.cpus; i++) {
            sync_info.checkpoint_end[i] = true;
        }

#ifndef NO_PRINT
        fprintf(stderr, "cpu: %ld get the broadcast, core0: %lu, core1: %lu\n",
                cpu_idx, workload_insns(0), workload_insns(1));
#endif
        for (int i = 0; i < sync_info.cpus; i++) {
#ifndef NO_PRINT
            fprintf(stderr, "cpu %d, insns %ld\n", i, workload_insns(i));
#endif
        }

        if (checkpoint.checkpoint_mode == UniformCheckpointing) {
            update_uniform_limit_inst(should_take_cpt);

            for (int i = 0; i < sync_info.cpus; i++) {
                CPUState *cs = qemu_get_cpu(i);
                CPURISCVState *env = cpu_env(cs);
                env->last_seen_insns = env->profiling_insns;
            }
        }

        // reset self flag
        sync_info.checkpoint_end[cpu_idx] = false;

        cpu_enable_ticks();

        g_cond_broadcast(&sync_signal);

        g_mutex_unlock(&sync_lock);
    }

    if (checkpoint.workload_exit && all_cpu_exit(cpu_idx)) {
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
    if (try_take_single_core_checkpoint) {
        return single_core_try_take_cpt(inst_count);
    } else {
        return multi_core_try_take_cpt(inst_count, cpu_idx, exit_sync_period);
    }
}
