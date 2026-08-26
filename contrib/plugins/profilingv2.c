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

#define NEMU_TRAP_INSN 0x0000006b
#define DISABLE_TIME_INTR 0x100
#define NOTIFY_PROFILER 0x101

typedef enum ProfilingState {
    PROFILING_WAITING,
    PROFILING_STARTING,
    PROFILING_ACTIVE,
} ProfilingState;

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
    ProfilingState state;
    bool marker_error_reported;
    uint64_t unique_trans_id;
    uint64_t unique_exec_id;
    uint64_t profiling_instr_counts;
    uint64_t per_bb_vector_instr_count;
    GMutex bbv_lock;
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
    /* BBV entry for the TB described by the fields above. */
    uint64_t current_bbv_entry;
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

static qemu_plugin_id_t plugin_id;
static ProfilingInfo_t profiling_info;
static ProfilingControl_t profiling_control;
static struct qemu_plugin_scoreboard *score_board;
static struct qemu_plugin_register **a0_registers;
static size_t a0_register_count;

/* descriptors for accessing the above scoreboard */
static qemu_plugin_u64 current_block_end_pc;
static qemu_plugin_u64 ordered_next_block_begin_pc;
static qemu_plugin_u64 last_pc;
static qemu_plugin_u64 last_pc_next_insn_addr;
static qemu_plugin_u64 current_block_begin_pc;
static qemu_plugin_u64 current_tb_execd_insn_cnt;
static qemu_plugin_u64 current_tb_total_cnt;
static qemu_plugin_u64 middle_exit_flag;
static qemu_plugin_u64 current_bbv_entry;

static void vcpu_tb_trans(struct qemu_plugin_tb *tb, void *userdata);
static void profiling_exit(void *userdata);
static void try_output_to_bb_file(void);
static void output_bbv_vector(void);
static void rollback_instr_counter(uint64_t unexecuted_insns,
                                   BasicBlockExecCount_t *original_bb_cnt,
                                   uint64_t first_pc, uint64_t second_pc);

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

static void init_profiling_control(void)
{
    profiling_control.state = PROFILING_WAITING;
    profiling_control.marker_error_reported = false;
    profiling_control.unique_trans_id = 0;
    /* SimPoint dimensions are one-based; dimension zero is invalid. */
    profiling_control.unique_exec_id = 1;
    profiling_control.profiling_instr_counts = 0;
    g_mutex_init(&profiling_control.bbv_lock);
    profiling_control.bbv =
        g_hash_table_new_full(hash_pair, compare_pair, g_free, g_free);
}

static void init_score_board(void) {
    score_board = qemu_plugin_scoreboard_new(sizeof(VCPUScoreBoard));

    current_block_end_pc = qemu_plugin_scoreboard_u64_in_struct(score_board, VCPUScoreBoard, current_block_end_pc);
    ordered_next_block_begin_pc = qemu_plugin_scoreboard_u64_in_struct(score_board, VCPUScoreBoard, ordered_next_block_begin_pc);
    last_pc = qemu_plugin_scoreboard_u64_in_struct(score_board, VCPUScoreBoard, last_pc);
    current_block_begin_pc = qemu_plugin_scoreboard_u64_in_struct(score_board, VCPUScoreBoard, current_block_begin_pc);
    current_tb_execd_insn_cnt = qemu_plugin_scoreboard_u64_in_struct(score_board, VCPUScoreBoard, current_tb_execd_insn_cnt);
    current_tb_total_cnt = qemu_plugin_scoreboard_u64_in_struct(score_board, VCPUScoreBoard, current_tb_total_cnt);
    last_pc_next_insn_addr = qemu_plugin_scoreboard_u64_in_struct(score_board, VCPUScoreBoard, last_pc_next_insn_addr);
    middle_exit_flag = qemu_plugin_scoreboard_u64_in_struct(score_board, VCPUScoreBoard, middle_exit_flag);
    current_bbv_entry = qemu_plugin_scoreboard_u64_in_struct(
        score_board, VCPUScoreBoard, current_bbv_entry);
}

static BasicBlockExecCount_t *fetch_bbcnt(UInt64Pair *hash_key) {
    BasicBlockExecCount_t *result =
        (BasicBlockExecCount_t *)g_hash_table_lookup(profiling_control.bbv,
                                                     (gconstpointer)hash_key);
    return result;
}

static inline void score_board_record_inject_before_tb_exec(
    struct qemu_plugin_insn *tb_begin_instr,
    struct qemu_plugin_insn *tb_end_instr, uint64_t tb_begin_pc,
    uint64_t tb_end_pc, uint64_t tb_instrs, BasicBlockExecCount_t *bb_cnt)
{
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
        tb_begin_instr, QEMU_PLUGIN_INLINE_STORE_U64, current_bbv_entry,
        (uint64_t)(uintptr_t)bb_cnt);
    qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
        tb_end_instr, QEMU_PLUGIN_INLINE_STORE_U64, middle_exit_flag,
        0);
}

static void enable_profiling(void *userdata)
{
    if (profiling_control.state != PROFILING_STARTING) {
        return;
    }

    init_bbv_file(profiling_info.path.target_path,
                  profiling_info.path.workload_path);
    init_score_board();
    profiling_control.state = PROFILING_ACTIVE;

    qemu_plugin_register_vcpu_tb_trans_cb(plugin_id, vcpu_tb_trans, NULL);
    qemu_plugin_register_atexit_cb(plugin_id, profiling_exit, NULL);

    printf("PLUGIN: workload loaded, profiling enabled\n");
}

static void vcpu_init(unsigned int vcpu_index, void *userdata)
{
    g_autoptr(GArray) registers = qemu_plugin_get_registers();

    if (vcpu_index >= a0_register_count) {
        fprintf(stderr,
                "profilingv2: vCPU index %u exceeds configured maximum\n",
                vcpu_index);
        return;
    }

    for (size_t i = 0; i < registers->len; i++) {
        qemu_plugin_reg_descriptor *reg = &g_array_index(
            registers, qemu_plugin_reg_descriptor, i);

        if (g_strcmp0(reg->name, "a0") == 0) {
            a0_registers[vcpu_index] = reg->handle;
            return;
        }
    }

    fprintf(stderr, "profilingv2: RISC-V register a0 is unavailable on "
            "vCPU %u\n", vcpu_index);
}

static bool read_marker_code(unsigned int vcpu_index, uint64_t *marker_code)
{
    g_autoptr(GByteArray) value = g_byte_array_new();
    struct qemu_plugin_register *reg;

    if (vcpu_index >= a0_register_count || !a0_registers[vcpu_index]) {
        return false;
    }

    reg = a0_registers[vcpu_index];
    if (!qemu_plugin_read_register(reg, value) ||
        value->len == 0 || value->len > sizeof(*marker_code)) {
        return false;
    }

    *marker_code = 0;
    for (size_t i = 0; i < value->len; i++) {
        *marker_code |= (uint64_t)value->data[i] << (i * 8);
    }
    return true;
}

static void nemu_trap_check(unsigned int vcpu_index, void *userdata)
{
    uint64_t marker_code;

    if (vcpu_index != 0) {
        return;
    }

    if (!read_marker_code(vcpu_index, &marker_code)) {
        if (!profiling_control.marker_error_reported) {
            fprintf(stderr, "profilingv2: cannot read RISC-V a0 marker code\n");
            profiling_control.marker_error_reported = true;
        }
        return;
    }

    switch (marker_code) {
    case DISABLE_TIME_INTR:
        return;
    case NOTIFY_PROFILER:
        if (profiling_control.state != PROFILING_WAITING) {
            fprintf(stderr, "profilingv2: ignoring profile BEGIN in state %d\n",
                    profiling_control.state);
            return;
        }
        profiling_control.state = PROFILING_STARTING;
        qemu_plugin_reset(plugin_id, enable_profiling, NULL);
        return;
    default:
        return;
    }
}

static inline void try_inject_nemu_trap_check(struct qemu_plugin_tb *tb, uint64_t tb_instrs) {
    for (size_t i = 0; i < tb_instrs; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        uint32_t data = 0;

        uint32_t size = qemu_plugin_insn_data(insn, &data, sizeof(uint32_t));
        assert(size == sizeof(uint32_t) || size == sizeof(uint16_t));
        if (size == sizeof(uint32_t) && data == NEMU_TRAP_INSN) {
            qemu_plugin_register_vcpu_insn_exec_cb(insn, nemu_trap_check,
                                                   QEMU_PLUGIN_CB_R_REGS,
                                                   GUINT_TO_POINTER(data));
        }
    }
}

static void waiting_vcpu_tb_trans(struct qemu_plugin_tb *tb, void *userdata)
{
    try_inject_nemu_trap_check(tb, qemu_plugin_tb_n_insns(tb));
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

static gint compare_exec_id(gconstpointer a, gconstpointer b)
{
    const BasicBlockExecCount_t *bb_a = a;
    const BasicBlockExecCount_t *bb_b = b;

    return (bb_a->exec_id > bb_b->exec_id) -
           (bb_a->exec_id < bb_b->exec_id);
}

static void output_bbv_vector(void)
{
    assert(profiling_control.bbv_file);
    gzprintf(profiling_control.bbv_file, "T");
    g_mutex_lock(&profiling_control.bbv_lock);
    GList *bbv_list = g_list_sort(
        g_hash_table_get_values(profiling_control.bbv), compare_exec_id);

    g_list_foreach(bbv_list, bbv_output, NULL);
    g_list_free(bbv_list);
    g_mutex_unlock(&profiling_control.bbv_lock);
    gzprintf(profiling_control.bbv_file, "\n");
    profiling_control.per_bb_vector_instr_count = 0;
}

static void try_output_to_bb_file(void)
{
    if (profiling_control.per_bb_vector_instr_count >=
        profiling_info.intervals) {
        output_bbv_vector();
    }
}

// first normal exec, whole block could be executed
static void vcpu_tb_exec(unsigned int cpu_index, void *userdata) {
    if (cpu_index != 0) {
        return;
    }

    BasicBlockExecCount_t *bb_cnt = userdata;
    /* Correct the preceding TB before a vector can be emitted and reset. */
    try_output_to_bb_file();

    if (bb_cnt->exec_times == 0) {
        bb_cnt->exec_id = profiling_control.unique_exec_id++;
    }

    // record tb
    bb_cnt->exec_times ++;
    bb_cnt->per_vector_exec_instr_count += bb_cnt->instrs;

    // record global
    profiling_control.per_bb_vector_instr_count += bb_cnt->instrs;
    profiling_control.profiling_instr_counts += bb_cnt->instrs;

#ifndef NO_LOG
    fprintf(stderr, "a bb exec, instrs up: %ld\n", bb_cnt->instrs);
    fflush(stderr);
#endif

}

static void rollback_instr_counter(
    uint64_t unexecuted_insns, BasicBlockExecCount_t *original_bb_cnt,
    uint64_t first_pc, uint64_t second_pc) {
    if (original_bb_cnt == NULL) {
        fprintf(stderr,
                "profilingv2: missing BBV entry for rollback: "
                "begin=0x%lx end=0x%lx unexecuted=%lu\n",
                first_pc, second_pc, unexecuted_insns);
    }

    // global
    g_assert(unexecuted_insns <= profiling_control.profiling_instr_counts);
    g_assert(unexecuted_insns <=
             profiling_control.per_bb_vector_instr_count);
    profiling_control.profiling_instr_counts -= unexecuted_insns;
    profiling_control.per_bb_vector_instr_count -= unexecuted_insns;
#ifndef NO_LOG
    fprintf(stderr, "get mmio instr, sub instrs: %ld\n", unexecuted_insns);
    fflush(stderr);
#endif

    if (original_bb_cnt != NULL) {
        g_assert(unexecuted_insns <=
                 original_bb_cnt->per_vector_exec_instr_count);
        original_bb_cnt->per_vector_exec_instr_count -= unexecuted_insns;
    }
}

static void vcpu_tb_middle_exit_exec(unsigned int cpu_index, void *udata) {
    if (cpu_index != 0) {
        return;
    }

    uint64_t second_pc    = qemu_plugin_u64_get(current_block_end_pc, cpu_index);
    uint64_t current_block_first_pc = (uint64_t)udata;
    uint64_t tb_execd_cnt = qemu_plugin_u64_get(current_tb_execd_insn_cnt, cpu_index);
    uint64_t tb_total_cnt = qemu_plugin_u64_get(current_tb_total_cnt, cpu_index);
    uint64_t first_pc     = qemu_plugin_u64_get(current_block_begin_pc, cpu_index);
    uint64_t lnpc         = qemu_plugin_u64_get(last_pc_next_insn_addr, cpu_index);
    BasicBlockExecCount_t *original_bb_cnt =
        (BasicBlockExecCount_t *)(uintptr_t)qemu_plugin_u64_get(
            current_bbv_entry, cpu_index);

    // fix insns count
    g_assert(tb_execd_cnt <= tb_total_cnt);
    uint64_t unexecuted_insns = tb_total_cnt - tb_execd_cnt;

    if (unexecuted_insns > 0) {
        // mmio or except
        if(current_block_first_pc == lnpc) {
            rollback_instr_counter(unexecuted_insns, original_bb_cnt, first_pc,
                                   second_pc);
#ifndef NO_LOG
            fprintf(stderr, "first pc %lx, second pc %lx, current block first pc %lx, lnpc %lx\n", first_pc, second_pc, current_block_first_pc, lnpc);
            fflush(stderr);
#endif
        // exception
        } else {
            unexecuted_insns += 1; // exception instr will execute when ret out of trap_handler
            rollback_instr_counter(unexecuted_insns, original_bb_cnt, first_pc,
                                   second_pc);
#ifndef NO_LOG
            fprintf(stderr, "first pc %lx, second pc %lx, current block first pc %lx, lnpc %lx\n", first_pc, second_pc, current_block_first_pc, lnpc);
            fflush(stderr);
#endif
        }
    }
}

static void vcpu_tb_trans(struct qemu_plugin_tb *tb, void *userdata) {
    uint64_t tb_begin_pc = qemu_plugin_tb_vaddr(tb);
    uint64_t tb_instrs = qemu_plugin_tb_n_insns(tb);
    struct qemu_plugin_insn *tb_begin_instr = qemu_plugin_tb_get_insn(tb, 0);
    struct qemu_plugin_insn *tb_end_instr = qemu_plugin_tb_get_insn(tb, tb_instrs - 1);
    uint64_t tb_end_pc = qemu_plugin_insn_vaddr(tb_end_instr);

    HashKey *hash_key = g_new(UInt64Pair, 1);
    hash_key->first = tb_begin_pc;
    hash_key->second = tb_end_pc;
    BasicBlockExecCount_t *bb_cnt;
    g_mutex_lock(&profiling_control.bbv_lock);
    bb_cnt = fetch_bbcnt(hash_key);

    if (bb_cnt) {
        bb_cnt->trans_count++;
        g_free(hash_key);
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
    g_mutex_unlock(&profiling_control.bbv_lock);
#ifndef NO_LOG
    fprintf(stderr, "trans new bb, first pc: %lx second pc: %lx\n", tb_begin_pc, tb_end_pc);
#endif

    // mmio tb will not update tb info
    score_board_record_inject_before_tb_exec(tb_begin_instr, tb_end_instr,
                                             tb_begin_pc, tb_end_pc,
                                             tb_instrs, bb_cnt);

    // mmio tb will not inject middle check
    // after mmio block, will inject this
    qemu_plugin_register_vcpu_tb_exec_cond_cb(
        tb, vcpu_tb_middle_exit_exec, QEMU_PLUGIN_CB_NO_REGS,
        QEMU_PLUGIN_COND_NE, middle_exit_flag, 0, (void *)tb_begin_pc);
    qemu_plugin_register_vcpu_tb_exec_cb(tb, vcpu_tb_exec,
                                         QEMU_PLUGIN_CB_NO_REGS, (void *)bb_cnt);

    score_board_record_inject_before_insn_exec(tb, tb_instrs);
}


static void profiling_exit(void *userdata) {
    if (profiling_control.state == PROFILING_ACTIVE) {
        if (profiling_control.per_bb_vector_instr_count > 0) {
            output_bbv_vector();
        }

        fprintf(stderr,
                "SimPoint profiling exit, total guest instructions = %ld, "
                "total profiling instrs = %ld\n",
                profiling_control.profiling_instr_counts,
                profiling_control.profiling_instr_counts);
    } else {
        fprintf(stderr,
                "profilingv2: profiling never started; no BBV produced\n");
    }
    fflush(stderr);

    if (profiling_control.bbv_file) {
        gzclose(profiling_control.bbv_file);
        profiling_control.bbv_file = NULL;
    }
    g_mutex_lock(&profiling_control.bbv_lock);
    if (profiling_control.bbv) {
        g_hash_table_destroy(profiling_control.bbv);
        profiling_control.bbv = NULL;
    }
    g_mutex_unlock(&profiling_control.bbv_lock);
    g_mutex_clear(&profiling_control.bbv_lock);

    if (score_board) {
        qemu_plugin_scoreboard_free(score_board);
        score_board = NULL;
    }

    g_free(a0_registers);
    a0_registers = NULL;
    a0_register_count = 0;
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info, int argc,
                                           char **argv) {
    plugin_id = id;

    if (!info->system_emulation ||
        !g_str_has_prefix(info->target_name, "riscv")) {
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

    init_profiling_control();

    a0_register_count = info->system.max_vcpus;
    a0_registers = g_new0(struct qemu_plugin_register *, a0_register_count);

    qemu_plugin_register_vcpu_init_cb(id, vcpu_init, NULL);
    qemu_plugin_register_vcpu_tb_trans_cb(id, waiting_vcpu_tb_trans, NULL);

    qemu_plugin_register_atexit_cb(id, profiling_exit, NULL);

    return 0;
}
