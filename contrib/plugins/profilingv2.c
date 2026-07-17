#include <qemu-plugin.h>
#include <assert.h>
#include <curses.h>
#include <stdbool.h>
#include <stdint.h>
#include <qemu-plugin.h>
#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

#define NO_LOG

typedef struct {
    uint64_t first;
    uint64_t second;
} UInt64Pair;
typedef UInt64Pair HashKey;

typedef struct ProfilingPath {
    GString *workload_path;
    GString *target_path;
} ProfilingPath_t;

typedef struct ProfilingInfo {
    ProfilingPath_t path;
    uint64_t intervals;
} ProfilingInfo_t;

typedef struct ProfilingControl {
    bool start_profiling;
    uint64_t unique_trans_id;
    uint64_t unique_exec_id;
    uint64_t execute_instr_counts;
    uint64_t profiling_instr_counts;
    uint64_t per_bb_vector_instr_count;
    GHashTable *bbv;
    gzFile bbv_file;
} ProfilingControl_t;

/* use ScoreBoard to track the current execution state */
typedef struct {
    /* address of end of block */
    uint64_t current_block_end_pc;
    /* next pc after end of block */
    uint64_t ordered_next_block_begin_pc;
    /* address of last executed PC */
    uint64_t last_pc;
    /* address of instruction next to the last executed PC */
    uint64_t last_pc_next_insn_addr;
    /* address of start of block */
    uint64_t current_block_begin_pc;
    /* instruction count of current block */
    uint64_t current_tb_execd_insn_cnt;
    /* total instructions of current block */
    uint64_t current_tb_total_cnt;
    /* Middle Exit Detection
    Flaw: When using ScoreBoard, the middle exit detection has a flaw where the
    exit cannot be detected if the excution of last instruction in the TB
    (Translation Block) aborts due to a fault/exception.
    */
    uint64_t middle_exit_flag;
} VCPUScoreBoard;

typedef struct BasicBlockExecCount {
    uint64_t begin_addr;
    uint64_t end_addr;
    uint64_t instrs;
    uint64_t trans_count;
    uint64_t exec_times;
    uint64_t per_vector_exec_instr_count;
    uint64_t trans_id;
    uint64_t exec_id;
} BasicBlockExecCount_t;

static const qemu_info_t* qemu_info;
static ProfilingInfo_t profiling_info;
static ProfilingControl_t profiling_control;
static struct qemu_plugin_scoreboard *score_board;

/* descriptors for accessing the above scoreboard */
static qemu_plugin_u64 current_block_end_pc;
static qemu_plugin_u64 ordered_next_block_begin_pc;
static qemu_plugin_u64 last_pc;
static qemu_plugin_u64 last_pc_next_insn_addr;
static qemu_plugin_u64 current_block_begin_pc;
static qemu_plugin_u64 current_tb_execd_insn_cnt;
static qemu_plugin_u64 current_tb_total_cnt;
static qemu_plugin_u64 middle_exit_flag;

static guint hash_pair(gconstpointer key) {
    const UInt64Pair *p = (const UInt64Pair *)key;
    uint64_t h1 = p->first;
    uint64_t h2 = p->second;
    return h1 ^ (h2 << 1);
}

static gboolean compare_pair(gconstpointer a, gconstpointer b) {
    const UInt64Pair *p1 = (const UInt64Pair *)(a);
    const UInt64Pair *p2 = (const UInt64Pair *)(b);
    return (p1->first == p2->first) && (p1->second == p2->second);
}

static void init_bbv_file(const GString *target_dirname,
                           const GString *workload_filename) {
    assert(g_mkdir_with_parents(target_dirname->str, 0775) == 0);

    GString *gz_path = g_string_new(target_dirname->str);
    g_string_append_printf(gz_path, "/%s", "simpoint_bbv.gz");

    printf("SimPoint bbv path %s \n", gz_path->str);

    profiling_control.bbv_file = gzopen(gz_path->str, "w");

    g_string_free(gz_path, true);
    assert(profiling_control.bbv_file);
}

static void init_profiling_control(const GString *target_dirname,
                                     const GString *workload_filename) {
    profiling_control.start_profiling = false;
    profiling_control.unique_trans_id = 0;
    profiling_control.unique_exec_id = 0;
    profiling_control.execute_instr_counts = 0;
    profiling_control.profiling_instr_counts = 0;
    profiling_control.bbv = g_hash_table_new_full(hash_pair, compare_pair, g_free, g_free);
    init_bbv_file(target_dirname, workload_filename);
}

static void init_score_board() {
    score_board = qemu_plugin_scoreboard_new(sizeof(VCPUScoreBoard));

    current_block_end_pc = qemu_plugin_scoreboard_u64_in_struct(score_board, VCPUScoreBoard, current_block_end_pc);
    ordered_next_block_begin_pc = qemu_plugin_scoreboard_u64_in_struct(score_board, VCPUScoreBoard, ordered_next_block_begin_pc);
    last_pc = qemu_plugin_scoreboard_u64_in_struct(score_board, VCPUScoreBoard, last_pc);
    current_block_begin_pc = qemu_plugin_scoreboard_u64_in_struct(score_board, VCPUScoreBoard, current_block_begin_pc);
    current_tb_execd_insn_cnt = qemu_plugin_scoreboard_u64_in_struct(score_board, VCPUScoreBoard, current_tb_execd_insn_cnt);
    current_tb_total_cnt = qemu_plugin_scoreboard_u64_in_struct(score_board, VCPUScoreBoard, current_tb_total_cnt);
    last_pc_next_insn_addr = qemu_plugin_scoreboard_u64_in_struct(score_board, VCPUScoreBoard, last_pc_next_insn_addr);
    middle_exit_flag = qemu_plugin_scoreboard_u64_in_struct(score_board, VCPUScoreBoard, middle_exit_flag);
}

static BasicBlockExecCount_t *fetch_bbcnt(UInt64Pair *hash_key) {
    BasicBlockExecCount_t *result =
        (BasicBlockExecCount_t *)g_hash_table_lookup(profiling_control.bbv,
                                                     (gconstpointer)hash_key);
    return result;
}

static inline void score_board_record_inject_before_tb_exec(struct qemu_plugin_insn* tb_begin_instr,
                                                     struct qemu_plugin_insn* tb_end_instr,
                                                     uint64_t tb_begin_pc,
                                                     uint64_t tb_end_pc,
                                                     uint64_t tb_instrs) {
    /*
     * Now we can set start/end for this block so the next block can
     * check where we are at. Do this on the first instruction and not
     * the TB so we don't get mixed up with above.
     */
    qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
        tb_begin_instr, QEMU_PLUGIN_INLINE_STORE_U64, current_block_end_pc,
        tb_end_pc);
    qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
        tb_begin_instr, QEMU_PLUGIN_INLINE_STORE_U64, ordered_next_block_begin_pc,
        tb_end_pc + qemu_plugin_insn_size(tb_end_instr));
    qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
        tb_begin_instr, QEMU_PLUGIN_INLINE_STORE_U64, current_block_begin_pc,
        tb_begin_pc);
    qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
        tb_begin_instr, QEMU_PLUGIN_INLINE_STORE_U64, current_tb_total_cnt,
        tb_instrs);
    qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
        tb_begin_instr, QEMU_PLUGIN_INLINE_STORE_U64, current_tb_execd_insn_cnt,
        0);
    qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
        tb_begin_instr, QEMU_PLUGIN_INLINE_STORE_U64, middle_exit_flag,
        1);
    qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
        tb_end_instr, QEMU_PLUGIN_INLINE_STORE_U64, middle_exit_flag,
        0);
}

static void clean_exec_count(gpointer key, gpointer value, gpointer user_data) {
    g_assert(value);
    BasicBlockExecCount_t *ec = (BasicBlockExecCount_t *)value;
    ec->per_vector_exec_instr_count = 0;
}

static void nemu_trap_check(unsigned int vcpu_index, void *userdata) {
    static int profiling_exit = 0;

    if (profiling_exit == 1) {
        return;
    }

    if (vcpu_index != 0) {
        return;
    }

    static int nemu_trap_count = 0;

    printf("From plugin, all exec instrs %ld, all profiling instrs %ld\n", profiling_control.execute_instr_counts, profiling_control.profiling_instr_counts);
    if (profiling_control.start_profiling == true) {
        // prepare exit
        printf("PLUGIN: After profiling GET NEMU_TRAP\n");
        printf("SimPoint profiling exit, total guest instructions = %ld, total profiling instrs = %ld\n",
               profiling_control.execute_instr_counts, profiling_control.profiling_instr_counts);
        profiling_exit = 1;
        return;
    }
    // disable timer
    nemu_trap_count += 1;
    // start profiling
    if (nemu_trap_count == 2) {
        // The first TB that starts profiling triggers a nemu_trap in the
        // middle, and the number of instructions after the nemu_trap is
        // not included in profiling, so it needs to be corrected
        profiling_control.start_profiling = true;
        g_hash_table_foreach(profiling_control.bbv, clean_exec_count, NULL);
        profiling_control.profiling_instr_counts = 0;
        profiling_control.per_bb_vector_instr_count = 0;
        printf("PLUGIN: worklaod loaded........................\n");
    }
    return;
}

static inline void try_inject_nemu_trap_check(struct qemu_plugin_tb *tb, uint64_t tb_instrs) {
    uint32_t data;
    for (size_t i = 0; i < tb_instrs; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);

        uint32_t size = qemu_plugin_insn_data(insn, &data, sizeof(uint32_t));
        assert(size == sizeof(uint32_t) || size == sizeof(uint16_t));
        if (data == 0x6b) {
            qemu_plugin_register_vcpu_insn_exec_cb(insn, nemu_trap_check,
                                                   QEMU_PLUGIN_CB_NO_REGS,
                                                   GUINT_TO_POINTER(data));
        }
    }
}

static inline void score_board_record_inject_before_insn_exec(struct qemu_plugin_tb *tb, uint64_t tb_instrs) {
    // register callback when translation inst is nemu_trap + Record Last PC
    for (size_t i = 0; i < tb_instrs; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        uint64_t instruction_pc = qemu_plugin_insn_vaddr(insn);

        /* Store the PC of what we are about to execute */
        qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
            insn, QEMU_PLUGIN_INLINE_STORE_U64, last_pc,
            instruction_pc);
        qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
            insn, QEMU_PLUGIN_INLINE_STORE_U64, last_pc_next_insn_addr,
            instruction_pc + qemu_plugin_insn_size(insn));
        qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
            insn, QEMU_PLUGIN_INLINE_ADD_U64, current_tb_execd_insn_cnt,
            1);
    }
}

static void bbv_output(gpointer data, gpointer user_data) {
    g_assert(data);
    BasicBlockExecCount_t *ec = (BasicBlockExecCount_t *)data;
    g_assert((int64_t)(ec->per_vector_exec_instr_count) >= 0);
    if (ec->per_vector_exec_instr_count != 0) {
        assert(profiling_control.bbv_file);
        gzprintf(profiling_control.bbv_file, ":%ld:%ld ", ec->exec_id,
                 ec->per_vector_exec_instr_count);
        ec->per_vector_exec_instr_count = 0;
    }
}

static inline void try_output_to_bb_file() {
    if (profiling_control.per_bb_vector_instr_count >= profiling_info.intervals) {
        assert(profiling_control.bbv_file);
        gzprintf(profiling_control.bbv_file, "T");
        GList *bbv_list = g_hash_table_get_values(profiling_control.bbv);

        g_list_foreach(bbv_list, bbv_output, NULL);
        g_list_free(bbv_list);
        gzprintf(profiling_control.bbv_file, "\n");
        profiling_control.per_bb_vector_instr_count = 0;
    }
}

// first normal exec, whole block could be executed
static void vcpu_tb_exec(unsigned int cpu_index, void *userdata) {
    if (cpu_index != 0) {
        return;
    }

    BasicBlockExecCount_t *bb_cnt = userdata;
    if (bb_cnt->exec_times == 0) {
        bb_cnt->exec_id = profiling_control.unique_exec_id++;
    }

    // record tb
    bb_cnt->exec_times ++;
    bb_cnt->per_vector_exec_instr_count += bb_cnt->instrs;

    // record global
    profiling_control.per_bb_vector_instr_count += bb_cnt->instrs;
    profiling_control.execute_instr_counts += bb_cnt->instrs;
    profiling_control.profiling_instr_counts += bb_cnt->instrs;

#ifndef NO_LOG
    fprintf(stderr, "a bb exec, instrs up: %ld\n", bb_cnt->instrs);
    fflush(stderr);
#endif

    if (profiling_control.start_profiling) {
        try_output_to_bb_file();
    }
}

static inline void rollback_instr_counter(uint64_t unexecuted_insns, uint64_t first_pc, uint64_t second_pc) {
    UInt64Pair hash_key = {.first = first_pc, .second = second_pc};
    BasicBlockExecCount_t *original_bb_cnt = fetch_bbcnt(&hash_key);

    if (original_bb_cnt == NULL) {
        fprintf(stderr,
                "profilingv2: missing BBV entry for rollback: "
                "begin=0x%lx end=0x%lx unexecuted=%lu\n",
                first_pc, second_pc, unexecuted_insns);
        return;
    }

    // global
    profiling_control.profiling_instr_counts -= unexecuted_insns;
    profiling_control.per_bb_vector_instr_count -= unexecuted_insns;
    profiling_control.execute_instr_counts -= unexecuted_insns;
#ifndef NO_LOG
    fprintf(stderr, "get mmio instr, sub instrs: %ld\n", unexecuted_insns);
    fflush(stderr);
#endif

    original_bb_cnt->per_vector_exec_instr_count -= unexecuted_insns;
}

static void vcpu_tb_middle_exit_exec(unsigned int cpu_index, void *udata) {
    if (cpu_index != 0)
        return;

    uint64_t second_pc    = qemu_plugin_u64_get(current_block_end_pc, cpu_index);
    uint64_t current_block_first_pc = (uint64_t)udata;
    uint64_t tb_execd_cnt = qemu_plugin_u64_get(current_tb_execd_insn_cnt, cpu_index);
    uint64_t tb_total_cnt = qemu_plugin_u64_get(current_tb_total_cnt, cpu_index);
    uint64_t first_pc     = qemu_plugin_u64_get(current_block_begin_pc, cpu_index);
    uint64_t lnpc         = qemu_plugin_u64_get(last_pc_next_insn_addr, cpu_index);

    // fix insns count
    uint64_t unexecuted_insns = tb_total_cnt - tb_execd_cnt; // all instrs - execed instrs + mmio instr
    g_assert(unexecuted_insns >= 0);

    if (unexecuted_insns > 0) {
        // mmio or except
        if(current_block_first_pc == lnpc) {
            rollback_instr_counter(unexecuted_insns, first_pc, second_pc);
#ifndef NO_LOG
            fprintf(stderr, "first pc %lx, second pc %lx, current block first pc %lx, lnpc %lx\n", first_pc, second_pc, current_block_first_pc, lnpc);
            fflush(stderr);
#endif
        // exception
        } else {
            unexecuted_insns += 1; // exception instr will execute when ret out of trap_handler
            rollback_instr_counter(unexecuted_insns, first_pc, second_pc);
#ifndef NO_LOG
            fprintf(stderr, "first pc %lx, second pc %lx, current block first pc %lx, lnpc %lx\n", first_pc, second_pc, current_block_first_pc, lnpc);
            fflush(stderr);
#endif
        }
    }
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb) {
    uint64_t tb_begin_pc = qemu_plugin_tb_vaddr(tb);
    uint64_t tb_instrs = qemu_plugin_tb_n_insns(tb);
    struct qemu_plugin_insn *tb_begin_instr = qemu_plugin_tb_get_insn(tb, 0);
    struct qemu_plugin_insn *tb_end_instr = qemu_plugin_tb_get_insn(tb, tb_instrs - 1);
    uint64_t tb_end_pc = qemu_plugin_insn_vaddr(tb_end_instr);

    HashKey *hash_key = g_new(UInt64Pair, 1);
    hash_key->first = tb_begin_pc;
    hash_key->second = tb_end_pc;
    BasicBlockExecCount_t *bb_cnt;
    bb_cnt = fetch_bbcnt(hash_key);

    if (bb_cnt) {
        bb_cnt->trans_count++;
    } else {
        bb_cnt = g_new0(BasicBlockExecCount_t, 1);
        bb_cnt->begin_addr = tb_begin_pc;
        bb_cnt->end_addr = tb_end_pc;
        bb_cnt->instrs = tb_instrs;
        bb_cnt->trans_count = 1;
        bb_cnt->exec_times = 0;
        bb_cnt->trans_id = (profiling_control.unique_trans_id++);
        bb_cnt->exec_id = 0;
        bb_cnt->per_vector_exec_instr_count = 0;
        g_hash_table_insert(profiling_control.bbv, hash_key, bb_cnt);
    }
#ifndef NO_LOG
    fprintf(stderr, "trans new bb, first pc: %lx second pc: %lx\n", tb_begin_pc, tb_end_pc);
#endif

    // mmio tb will not update tb info
    score_board_record_inject_before_tb_exec(tb_begin_instr, tb_end_instr, tb_begin_pc, tb_end_pc, tb_instrs);

    // mmio tb will not inject middle check
    // after mmio block, will inject this
    qemu_plugin_register_vcpu_tb_exec_cond_cb(
        tb, vcpu_tb_middle_exit_exec, QEMU_PLUGIN_CB_NO_REGS,
        QEMU_PLUGIN_COND_NE, middle_exit_flag, 0, (void *)tb_begin_pc);
    qemu_plugin_register_vcpu_tb_exec_cb(tb, vcpu_tb_exec,
                                         QEMU_PLUGIN_CB_NO_REGS, (void *)bb_cnt);

    score_board_record_inject_before_insn_exec(tb, tb_instrs);

    try_inject_nemu_trap_check(tb, tb_instrs);
}


static void profiling_exit(qemu_plugin_id_t id, void *userdata) {
    fprintf(stderr, "SimPoint profiling exit, total guest instructions = %ld, total profiling instrs = %ld\n",
           profiling_control.execute_instr_counts, profiling_control.profiling_instr_counts);
    fflush(stderr);

    gzclose(profiling_control.bbv_file);
    g_hash_table_destroy(profiling_control.bbv);

}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info, int argc,
                                           char **argv) {
    qemu_info = info;

    if (!qemu_info->system_emulation) {
        return -1;
    }

    for (int i = 0; i < argc; i++) {
        char *opt = argv[i];
        g_auto(GStrv) tokens = g_strsplit(opt, "=", 2);


        if (g_strcmp0(tokens[0], "workload") == 0) {

            profiling_info.path.workload_path = g_string_new(tokens[1]);

        } else if (g_strcmp0(tokens[0], "intervals") == 0) {

            profiling_info.intervals = atoi(tokens[1]);

        } else if (g_strcmp0(tokens[0], "target") == 0) {

            profiling_info.path.target_path = g_string_new(tokens[1]);

        } else {

            fprintf(stdout, "unknown argument %s %s\n", tokens[0], tokens[1]);
            return -1;
        }
    }

    init_profiling_control(profiling_info.path.target_path, profiling_info.path.workload_path);

    init_score_board();

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);

    qemu_plugin_register_atexit_cb(id, profiling_exit, NULL);

    return 0;
}
