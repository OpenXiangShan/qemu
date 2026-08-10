#ifndef CONFIG_USER_ONLY
#include "qemu/osdep.h"
#include "checkpoint/checkpoint.h"
#include "cpu_bits.h"
#include "exec/cpu-common.h"
#include "hw/core/cpu.h"
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
#include "hw/core/boards.h"
#include "checkpoint/checkpoint.h"
#include "checkpoint/checkpoint.pb.h"
#include "checkpoint/serializer_utils.h"
#include "system/cpu-timers.h"
#include "system/physmem.h"
#include "system/runstate.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static uint64_t global_mtime = 0;
gint simpoint_checkpoint_exit = 0;

#define relative_kernel_exec_insns(ns, cpu_idx) (ns->sync_info.workload_insns[cpu_idx] - ns->sync_info.kernel_insns[cpu_idx])

static void try_sync(NEMUState* ns, uint64_t icount, int cpu_idx,
                             bool exit_sync_period, bool *sync_end);
static bool __attribute_maybe_unused__
serialize(uint64_t memory_addr, int cpu_idx, int cpus, uint64_t inst_count);

__attribute_maybe_unused__ static inline void multicore_try_take_cpt(NEMUState* ns, uint64_t icount, int cpu_idx,
                             bool exit_sync_period);


static inline void send_fifo(NEMUState *ns){
    ns->q2d_buf.cpt_ready = true;
    if (qemu_write_full(ns->q2d_fifo, &ns->q2d_buf,
                        sizeof(Qemu2Detail)) != sizeof(Qemu2Detail)) {
        error_report("failed to send checkpoint state to detail model");
    }
}

static inline void read_fifo(NEMUState *ns){
    ns->sync_control_info.info_vaild_periods -= 1;
    if (ns->sync_control_info.info_vaild_periods <= 0) {
        if (read(ns->d2q_fifo, &ns->sync_control_info.u_arch_info,
                 sizeof(Detail2Qemu)) != sizeof(Detail2Qemu)) {
            error_report("failed to read checkpoint state from detail model");
        }
    }
}

inline uint64_t simpoint_get_next_instructions(NEMUState *ns)
{
    GList *first_insns_item = g_list_first(ns->simpoint_info.cpt_instructions);
    if (first_insns_item == NULL) {
        set_simpoint_checkpoint_exit();
        return UINT64_MAX;
    }

    return GPOINTER_TO_UINT(first_insns_item->data) *
           ns->nemu_args.cpt_interval;
}

MODE_DEF_HELPER(simpoint,
    uint64_t, get_cpt_limit_instructions, (NEMUState *ns), {
        return simpoint_get_next_instructions(ns);
    },
    uint64_t, get_sync_limit_instructions, (NEMUState *ns, int cpu_idx), {
        if (ns->nemu_args.sync_interval != 0) {
            return ns->sync_info.uniform_sync_limit;
        }
        return simpoint_get_next_instructions(ns);
    },
    void, try_take_cpt, (NEMUState* ns, uint64_t icount, int cpu_idx, bool exit_sync_period), {
        return multicore_try_take_cpt(ns, icount, cpu_idx, exit_sync_period);
    },
    void, after_take_cpt, (NEMUState *ns, int cpu_idx), {
        GList* first_insns_item = g_list_first(ns->simpoint_info.cpt_instructions);
        if (first_insns_item == NULL) {
            set_simpoint_checkpoint_exit();
        }
    },
    void, update_cpt_limit_instructions, (NEMUState *ns, uint64_t icount), {
        info_report("Taking checkpoint @ instruction count %lu", icount);
        guint cpt_insns_list_length=g_list_length(ns->simpoint_info.cpt_instructions);
        if (cpt_insns_list_length!=0) {
            ns->simpoint_info.cpt_instructions=g_list_remove(ns->simpoint_info.cpt_instructions,g_list_first(ns->simpoint_info.cpt_instructions)->data);
            ns->path_manager.checkpoint_path_list=g_list_remove(ns->path_manager.checkpoint_path_list,g_list_first(ns->path_manager.checkpoint_path_list)->data);
        }
    },
    void, try_set_mie, (void *env, NEMUState *ns), {
        assert(0);
    },
    void, update_sync_limit_instructions, (NEMUState *ns), {
        if (ns->nemu_args.sync_interval != 0) {
            ns->sync_info.uniform_sync_limit += ns->nemu_args.sync_interval;
        }
    }
)

MODE_DEF_HELPER(uniform,
    uint64_t, get_cpt_limit_instructions, (NEMUState *ns), {
        return ns->checkpoint_info.next_uniform_point;
    },
    uint64_t, get_sync_limit_instructions, (NEMUState *ns, int cpu_idx), {
        return ns->sync_info.uniform_sync_limit;
    },
    void, try_take_cpt, (NEMUState* ns, uint64_t icount, int cpu_idx, bool exit_sync_period), {
        return multicore_try_take_cpt(ns, icount, cpu_idx, exit_sync_period);
    },
    void, after_take_cpt, (NEMUState *ns, int cpu_idx), {
    },
    void, update_cpt_limit_instructions, (NEMUState *ns, uint64_t icount), {
        ns->checkpoint_info.next_uniform_point += ns->nemu_args.cpt_interval;
    },
    void, try_set_mie, (void *env, NEMUState *ns), {
    },
    void, update_sync_limit_instructions, (NEMUState *ns), {
        ns->sync_info.uniform_sync_limit += ns->nemu_args.sync_interval;
    }
)

MODE_DEF_HELPER(sync_uni,
    uint64_t, get_cpt_limit_instructions, (NEMUState *ns), {
        return ns->checkpoint_info.next_uniform_point;
    },
    uint64_t, get_sync_limit_instructions, (NEMUState *ns, int cpu_idx), {
        return (uint64_t)((double)ns->nemu_args.sync_interval /
                  ns->sync_control_info.u_arch_info.CPI[cpu_idx]);

    },
    void, try_take_cpt, (NEMUState* ns, uint64_t icount, int cpu_idx, bool exit_sync_period), {
        return multicore_try_take_cpt(ns, icount, cpu_idx, exit_sync_period);
    },
    void, after_take_cpt, (NEMUState *ns, int cpu_idx), {
        // send fifo message
        ns->q2d_buf.cpt_id = ns->cpt_func.get_cpt_limit_instructions(ns);
        ns->q2d_buf.total_inst_count = relative_kernel_exec_insns(ns, cpu_idx);
        send_fifo(ns);
        // when period <= 0, next cpi will update
        read_fifo(ns);
    },
    void, update_cpt_limit_instructions, (NEMUState *ns, uint64_t icount), {
        ns->checkpoint_info.next_uniform_point += ns->nemu_args.cpt_interval;
    },
    void, try_set_mie, (void *env, NEMUState *ns), {
    },
    void, update_sync_limit_instructions, (NEMUState *ns), {
    }
)

MODE_DEF_HELPER(no_cpt,
    uint64_t, get_cpt_limit_instructions, (NEMUState *ns), {
        assert(0);
    },
    uint64_t, get_sync_limit_instructions, (NEMUState *ns, int cpu_idx), {
        assert(0);
    },
    void, try_take_cpt, (NEMUState* ns, uint64_t icount, int cpu_idx, bool exit_sync_period), {
    },
    void, after_take_cpt, (NEMUState *ns, int cpu_idx), {
    },
    void, update_cpt_limit_instructions, (NEMUState *ns, uint64_t icount), {
    },
    void, try_set_mie, (void *env, NEMUState *ns), {
    },
    void, update_sync_limit_instructions, (NEMUState *ns), {
    }
)

static void single_try_set_mie(void *env, NEMUState *ns)
{
    ((CPURISCVState *)env)->mie =
        (((CPURISCVState *)env)->mie & (~(1 << 7)));
    ((CPURISCVState *)env)->mie =
        (((CPURISCVState *)env)->mie & (~(1 << 5)));
    info_report("Notify: disable timmer interrupr");
}

#define NO_PRINT

void set_simpoint_checkpoint_exit(void){
    g_atomic_int_set(&simpoint_checkpoint_exit, 1);
}

__attribute__((unused)) static void set_global_mtime(void)
{
    physical_memory_read(CLINT_MMIO + CLINT_MTIME, &global_mtime, 8);
}

static bool __attribute_maybe_unused__
serialize(uint64_t memory_addr, int cpu_idx, int cpus, uint64_t inst_count)
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
        info_report("buffer %d serialize success, start addr %lx", i,
                    serialize_reg_base_addr + (i * 1024 * 1024));
    }

    if (!using_gcpt_mmio) {
        physical_memory_write(memory_addr, hardware_status_buffer,
                              cpt_header.cpt_offset + 1024 * 1024 * cpus);
    }
    return serialize_pmem(inst_count, false, hardware_status_buffer,
                          cpt_header.cpt_offset + 1024 * 1024 * cpus);
}

static inline int sync_multicore(NEMUState *ns, uint64_t icount, int cpu_idx, int early_exit){
    if (g_atomic_int_get(&ns->sync_info.online_cpus) != ns->sync_info.cpus) {
        return EXIT;
    }

    if (early_exit || ns->cs_vec[cpu_idx]->halted == 1) {
        return WAIT;
    }

    if ((icount - ns->sync_info.kernel_insns[cpu_idx]) >= ns->cpt_func.get_sync_limit_instructions(ns, cpu_idx)) {
        return WAIT;
    }

    return RUNNING;
}


static void try_sync(NEMUState* ns, uint64_t icount, int cpu_idx,
                             bool exit_sync_period, bool *sync_end)
{
    if (sync_multicore(ns, icount, cpu_idx, exit_sync_period) == WAIT) {
        /* A generation prevents arrivals leaking into the next barrier. */
        int my_gen = g_atomic_int_get(&ns->sync_info.barrier_gen);
        int arrived = g_atomic_int_add(&ns->sync_info.barrier_arrive, 1) + 1;
        bool is_leader = (arrived == ns->sync_info.cpus);
        g_assert(arrived <= ns->sync_info.cpus);
        if (arrived == 1) {
            /* The first arrival freezes global time. */
            cpu_disable_ticks();
            set_global_mtime();
        }
        if (is_leader) {
            *sync_end = true;
        } else {
            while (g_atomic_int_get(&ns->sync_info.barrier_gen) == my_gen) {
                cpu_relax();
            }
        }
    }
}

void check_exit(uint64_t icount) {
    if (simpoint_checkpoint_exit) {
        // exit;
        printf("icount instructions = %ld\n", icount);
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_HOST_QMP_QUIT);
    }
}

__attribute_maybe_unused__ static inline void multicore_try_take_cpt(NEMUState* ns, uint64_t icount, int cpu_idx,
                             bool exit_sync_period){
    bool sync_end = false;
    static uint64_t wait_times;

    try_sync(ns, icount, cpu_idx, exit_sync_period, &sync_end);

    if (sync_end) {
        wait_times++;
        ns->cpt_func.update_sync_limit_instructions(ns);

        if ((icount - ns->sync_info.kernel_insns[cpu_idx]) >= ns->cpt_func.get_cpt_limit_instructions(ns)) {

            info_report("cpu %d get cpt limit wait times %" PRIu64, cpu_idx,
                        wait_times);
            wait_times = 0;

            if (serialize(0x80300000, cpu_idx, ns->sync_info.cpus,
                          icount)) {
                /* Consume the SimPoint only after its checkpoint is written. */
                ns->cpt_func.update_cpt_limit_instructions(ns, icount);
                ns->cpt_func.after_take_cpt(ns, cpu_idx);
            }
        }

        /* Reset arrivals before releasing peers into the next generation. */
        g_atomic_int_set(&ns->sync_info.barrier_arrive, 0);
        g_atomic_int_add(&ns->sync_info.barrier_gen, 1);

        cpu_enable_ticks();
    }

    check_exit(icount);
}

static void sync_init(NEMUState *ns, gint cpus){
    ns->sync_info.cpus = cpus;
    // store online cpu info, when cpu exec before workload, online[cpu_idx] = 1
    ns->sync_info.online = g_malloc0(cpus * sizeof(gint));

    // store online cpu nums
    if(ns->nemu_args.checkpoint != NULL){
        // if checkpoint mode, default online 2 cpud
        ns->sync_info.online_cpus = cpus;
        for (int i = 0; i < cpus; i++) {
            ns->sync_info.online[i] = 1;
        }
    }else{
        // if not, online cpus will add by before_worklod
        ns->sync_info.online_cpus = 0;
    }

    // set uniform next sync limit
    ns->sync_info.uniform_sync_limit = ns->nemu_args.sync_interval;

    // store how many insts exec
    ns->sync_info.workload_insns = g_malloc0(cpus * sizeof(int64_t));
    // store before 'before workload' exec how many insts
    ns->sync_info.kernel_insns = g_malloc0(cpus * sizeof(int64_t));

    // if exec halt or nemu_trap, will get early exit
    ns->sync_info.early_exit = g_malloc0(cpus * sizeof(bool));
    // check checkpoint status
    ns->sync_info.checkpoint_end = g_malloc0(cpus * sizeof(bool));
    // check sync status
    ns->sync_info.waiting = g_malloc0(cpus * sizeof(gint));

    ns->sync_info.barrier_arrive = 0;
    ns->sync_info.barrier_gen = 0;
}

NEMUState *local_nemu_state;
void multicore_checkpoint_init(MachineState *machine)
{
    MachineState *ms = machine;
    NEMUState *ns = NEMU_MACHINE(ms);
    int64_t cpus = ms->smp.cpus;
    local_nemu_state = ns;

    sync_init(ns, cpus);

    ns->cs_vec = g_malloc0(sizeof(CPUState*)*cpus);
    for (int i = 0; i < cpus; i++) {
        ns->cs_vec[i] = qemu_get_cpu(i);
    }

    if (ns->nemu_args.checkpoint_mode == SyncUniformCheckpoint) {
        const char *detail_to_qemu_fifo_name = "./detail_to_qemu.fifo";
        ns->d2q_fifo = open(detail_to_qemu_fifo_name, O_RDONLY);

        const char *qemu_to_detail_fifo_name = "./qemu_to_detail.fifo";
        ns->q2d_fifo = open(qemu_to_detail_fifo_name, O_WRONLY);

        ns->sync_control_info.info_vaild_periods = 1;
        for (int i = 0; i < 8; i++) {
            ns->sync_control_info.u_arch_info.CPI[i] = 1;
        }
    }

    switch (ns->nemu_args.checkpoint_mode) {
        case UniformCheckpointing:
            ns->cpt_func=uniform_func;
            break;
        case SyncUniformCheckpoint:
            ns->cpt_func=sync_uni_func;
            break;
        case SimpointCheckpointing:
            ns->cpt_func=simpoint_func;
            break;
        default:
            ns->cpt_func=no_cpt_func;
            break;
    }

    if (cpus == 1) {
        ns->cpt_func.try_take_cpt = single_core_try_take_cpt;
        ns->cpt_func.try_set_mie = single_try_set_mie;
    }else{
        ns->cpt_func.try_set_mie = single_try_set_mie;
    }
}

inline void try_take_cpt(NEMUState *ns, uint64_t inst_count, int cpu_idx, bool exit_sync_period)
{
    return ns->cpt_func.try_take_cpt(ns, inst_count, cpu_idx, exit_sync_period);
}
#endif
