#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <inttypes.h>
#include <qemu-plugin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <zlib.h>

#define FILENAME_MXLEN 256
#define MAX_CPUS 8

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

typedef struct {
    uint64_t first;
    uint64_t second;
} UInt64Pair;

guint hash_pair(gconstpointer key) {
    const UInt64Pair *p = (const UInt64Pair *)key;
    uint64_t h1 = p->first;
    uint64_t h2 = p->second;
    return h1 ^ (h2 << 1);
}

gboolean compare_pair(gconstpointer a, gconstpointer b) {
    const UInt64Pair *p1 = (const UInt64Pair *)(a);
    const UInt64Pair *p2 = (const UInt64Pair *)(b);
    return (p1->first == p2->first) && (p1->second == p2->second);
}

typedef struct Args {
    char workload_path[FILENAME_MXLEN];
    char target_path[FILENAME_MXLEN];
    uint64_t intervals;
} Args_t;

typedef struct BasicBlockExecCount {
    uint64_t start_addr;
    uint64_t end_addr;
    uint64_t exec_insns_count;
    uint64_t trans_count;
    uint64_t insns;
    uint64_t id;
} BasicBlockExecCount_t;

typedef struct QemuInfo {
    const char *target_name;
    uint64_t smp_vcpus;
    uint64_t max_vcpus;
    bool system_emulation;
} QemuInfo_t;

typedef struct ProfilingInfo {
    GMutex lock;
    Args_t args;
    gzFile bbv_file;
    GHashTable *bbv;
    bool start_profiling;
    uint64_t unique_trans_id;
    uint64_t profiling_insns;
    uint64_t bbv_exec_insns;
    uint64_t exec_count_all;
} ProfilingInfo_t;

/* use ScoreBoard to track the current execution state */
typedef struct {
    /* address of end of block */
    uint64_t end_block;
    /* next pc after end of block */
    uint64_t pc_after_block;
    /* address of last executed PC */
    uint64_t last_pc;
    /* address of instruction next to the last executed PC */
    uint64_t last_pc_next_insn_addr;
    /* address of start of block */
    uint64_t begin_block;
    /* instruction count of current block */
    uint64_t current_tb_insn_cnt;
    /* total instructions of current block */
    uint64_t current_tb_total_cnt;
    /* Middle Exit Detection
    Flaw: When using ScoreBoard, the middle exit detection has a flaw where the
    exit cannot be detected if the excution of last instruction in the TB
    (Translation Block) aborts due to a fault/exception.
    */
    uint64_t middle_exit_flag;
} VCPUScoreBoard;

/* descriptors for accessing the above scoreboard */
static qemu_plugin_u64 end_block;
static qemu_plugin_u64 pc_after_block;
static qemu_plugin_u64 last_pc;
static qemu_plugin_u64 last_pc_next_insn_addr;
static qemu_plugin_u64 begin_block;
static qemu_plugin_u64 current_tb_insn_cnt;
static qemu_plugin_u64 current_tb_total_cnt;
static qemu_plugin_u64 middle_exit_flag;

/* current execution state for accurate instruction count */
struct qemu_plugin_scoreboard *state;

static QemuInfo_t qemu_info;
static ProfilingInfo_t profiling_info;

/* MMIO split TB indicator */
static uint64_t MMIO_split_flag = 0;
/* original TB info before MMIO split */
static uint64_t MMIO_split_tb_pc;
static uint64_t MMIO_split_tb_endpc;

static void profiling_init(const char *target_dirname,
                           const char *workload_filename) {
    assert(g_mkdir_with_parents(target_dirname, 0775) == 0);

    char gz_path[FILENAME_MXLEN] = {0};

    snprintf(gz_path, FILENAME_MXLEN, "%s/%s", target_dirname,
             "simpoint_bbv.gz");

    printf("SimPoint bbv path %s \n", gz_path);

    g_mutex_lock(&profiling_info.lock);

    profiling_info.bbv_file = gzopen(gz_path, "w");

    g_mutex_unlock(&profiling_info.lock);
    assert(profiling_info.bbv_file);
}

static BasicBlockExecCount_t *fetch_bbcnt(uint64_t start_addr,
                                          uint64_t end_addr, bool with_lock) {
    UInt64Pair hash_key;
    hash_key.first = start_addr;
    hash_key.second = end_addr;
    if (with_lock)
        g_mutex_lock(&profiling_info.lock);
    BasicBlockExecCount_t *result =
        (BasicBlockExecCount_t *)g_hash_table_lookup(profiling_info.bbv,
                                                     (gconstpointer)&hash_key);
    if (with_lock)
        g_mutex_unlock(&profiling_info.lock);
    return result;
}

static gint __attribute__((unused))
cmp_exec_count(gconstpointer a, gconstpointer b) {
    BasicBlockExecCount_t *ea = (BasicBlockExecCount_t *)a;
    BasicBlockExecCount_t *eb = (BasicBlockExecCount_t *)b;
    if (ea->exec_insns_count > eb->exec_insns_count) {
        return -1;
    } else if ((ea->exec_insns_count) == (eb->exec_insns_count)) {
        return 0;
    } else {
        return 1;
    }
}

static gint cmp_id(gconstpointer a, gconstpointer b) {
    BasicBlockExecCount_t *ea = (BasicBlockExecCount_t *)a;
    BasicBlockExecCount_t *eb = (BasicBlockExecCount_t *)b;
    if (ea->id < eb->id) {
        return -1;
    } else if (ea->id == eb->id) {
        return 0;
    } else {
        return 1;
    }
}

void bbv_output(gpointer data, gpointer user_data) {
    g_assert(data);
    BasicBlockExecCount_t *ec = (BasicBlockExecCount_t *)data;
    g_assert((int64_t)(ec->exec_insns_count) >= 0);
    if (ec->exec_insns_count != 0) {
        assert(profiling_info.bbv_file);
        gzprintf(profiling_info.bbv_file, ":%ld:%ld ", ec->id,
                 ec->exec_insns_count);
        ec->exec_insns_count = 0;
    }
}

void clean_exec_count(gpointer key, gpointer value, gpointer user_data) {
    g_assert(value);
    BasicBlockExecCount_t *ec = (BasicBlockExecCount_t *)value;
    ec->exec_insns_count = 0;
}

void vcpu_tb_exec(unsigned int cpu_index, void *userdata) {
    if (cpu_index != 0) {
        return;
    }

    uint64_t ebpc;
    uint64_t lpc;

    BasicBlockExecCount_t *cnt = (BasicBlockExecCount_t *)userdata;
    g_assert(cnt);
    BasicBlockExecCount_t *mmio_original_cnt;

    // Lazy Load: Load ebpc and lpc only when MMIO_split_flag is true, used to
    // determine whether the original MMIO TB has finished execution
    if (MMIO_split_flag) {
        ebpc = qemu_plugin_u64_get(end_block, cpu_index);
        lpc = qemu_plugin_u64_get(last_pc, cpu_index);

        // determine whether the original MMIO TB has finished execution
        if ((lpc == ebpc) && (ebpc == MMIO_split_tb_endpc)) {
            MMIO_split_flag = 0;
        }
    }

    // Check MMIO split flag. If the original MMIO TB has not finished
    // execution, the current pre-counted TB is still included in the original
    // TB
    if (MMIO_split_flag) {
        // Fetch original MMIO TB bbcnt
        g_assert(cnt->start_addr <= MMIO_split_tb_endpc);
        g_assert(
            cnt->end_addr ==
            MMIO_split_tb_endpc); // The end address of the sub-TB generated by
                                  // MMIO splitting should match the original TB
                                  // end address (unless the number of
                                  // instructions >= 512)
        mmio_original_cnt =
            fetch_bbcnt(MMIO_split_tb_pc, MMIO_split_tb_endpc, true);
        g_assert(mmio_original_cnt);
    }

    // Before pre-counting, first check if the instruction count interval is
    // reached, and output the counting result
    if (profiling_info.start_profiling == true) {

        if (profiling_info.bbv_exec_insns >= profiling_info.args.intervals) {
            assert(profiling_info.bbv_file);
            gzprintf(profiling_info.bbv_file, "T");

            g_mutex_lock(&profiling_info.lock);
            GList *sorted_list = g_list_sort(
                g_hash_table_get_values(profiling_info.bbv), cmp_id);
            g_mutex_unlock(&profiling_info.lock);

            // output to bbv
            g_list_foreach(sorted_list, bbv_output, NULL);
            // free sort list
            g_list_free(sorted_list);

            gzprintf(profiling_info.bbv_file, "\n");

            // clean after output
            profiling_info.bbv_exec_insns = 0;
        }
    }

    // pre-counting
    profiling_info.exec_count_all += cnt->insns;

    if (profiling_info.start_profiling == true) {
        // add bbv and profiling inst
        profiling_info.bbv_exec_insns += cnt->insns;
        profiling_info.profiling_insns += cnt->insns;

        // when MMIO_split_flag is valid, save the counting information to the
        // original TB
        if (!MMIO_split_flag) {
            cnt->exec_insns_count += cnt->insns;
        } else {
            mmio_original_cnt->exec_insns_count += cnt->insns;
        }
    }
}

typedef struct InstructionCount {
    GMutex lock;
    uint64_t all_exec_insns;
    uint64_t last_instuction;
    uint64_t vset_counter;
    gzFile log_file;

} InstructionCount_t;
InstructionCount_t vset_counter;

#ifdef VSET_COUNT
static void __attribute__((unused))
check_vset(unsigned int vcpu_index, void *userdata) {
    assert(vset_counter.log_file != NULL);

    char buf[256];
    vset_counter.vset_counter += 1;
    g_mutex_lock(&vset_counter.lock);
    sprintf(buf, "Execute width %ld\n",
            vset_counter.all_exec_insns - vset_counter.last_instuction);
    vset_counter.last_instuction = profiling_info.all_exec_insns;
    g_mutex_unlock(&vset_counter.lock);

    fwrite(buf, strlen(buf), 1, vset_counter.log_file);
}
#endif

static void __attribute__((unused))
instruction_check(unsigned int vcpu_index, void *userdata) {
    uint64_t data = (uint64_t)userdata;

    if (((data & 0x80007057) == 0x80007057 ||
         (data & 0xc0007057) == 0xc0007057 || (data & 0x7057) == 0x7057) &&
        profiling_info.start_profiling) {
        //    printf("instructions %x data&ins %x\n",data,data&0x80007057);
        assert(vset_counter.log_file != NULL);

        char buf[256];
        vset_counter.vset_counter += 1;
        g_mutex_lock(&vset_counter.lock);
        //    sprintf(buf,"Execute width %ld all %ld last
        //    %ld\n",(vset_counter.all_exec_insns -
        //    vset_counter.last_instuction),vset_counter.all_exec_insns,vset_counter.last_instuction);
        sprintf(buf, "%lx:%ld ", data,
                (profiling_info.exec_count_all - vset_counter.last_instuction));
        vset_counter.last_instuction = profiling_info.exec_count_all;
        if (vset_counter.last_instuction % 10000000 == 0) {
            sprintf(buf, "\n");
        }
        g_mutex_unlock(&vset_counter.lock);

        gzprintf(vset_counter.log_file, buf);
    }
}

static void nemu_trap_check(unsigned int vcpu_index, void *userdata) {
    // data is inst value
    static int profiling_exit = 0;
    if (profiling_exit == 1) {
        return;
    }
    if (vcpu_index != 0) {
        return;
    }
    uint64_t data = (uint64_t)userdata;
    static int nemu_trap_count = 0;

    g_mutex_lock(&profiling_info.lock);
    printf("From plugin, all exec insn %ld\n", profiling_info.exec_count_all);
    if (profiling_info.start_profiling == true) {
        // prepare exit
        printf("PLUGIN: After profiling GET NEMU_TRAP\n");
        printf("SimPoint profiling exit, total guest instructions = %ld\n",
               profiling_info.profiling_insns);
        profiling_exit = 1;
        g_mutex_unlock(&profiling_info.lock);
        return;
    }
    // disable timer
    nemu_trap_count += 1;
    // start profiling
    if (nemu_trap_count ==
        2) { // The first TB that starts profiling triggers a nemu_trap in the
             // middle, and the number of instructions after the nemu_trap is
             // not included in profiling, so it needs to be corrected
        profiling_info.start_profiling = true;
        g_hash_table_foreach(profiling_info.bbv, clean_exec_count, NULL);
        printf("PLUGIN: worklaod loaded........................\n");
        // Correct profiling instruction count
        uint64_t tb_insn_cnt =
            qemu_plugin_u64_get(current_tb_insn_cnt, vcpu_index);
        uint64_t tb_bpc = qemu_plugin_u64_get(begin_block, vcpu_index);
        uint64_t tb_ebpc = qemu_plugin_u64_get(end_block, vcpu_index);
        uint64_t mmio_flag = MMIO_split_flag;
        BasicBlockExecCount_t *cnt = fetch_bbcnt(tb_bpc, tb_ebpc, false);
        g_assert(cnt);
        profiling_info.bbv_exec_insns += (cnt->insns - tb_insn_cnt);
        profiling_info.profiling_insns += (cnt->insns - tb_insn_cnt);
        // Same counting logic as in vcpu_tb_exec, need to check MMIO status,
        // and save the counting information to the original TB
        if (!mmio_flag) {
            cnt->exec_insns_count += (cnt->insns - tb_insn_cnt);
        } else {
            uint64_t mmio_bpc = MMIO_split_tb_pc;
            uint64_t mmio_ebpc = MMIO_split_tb_endpc;
            BasicBlockExecCount_t *mmio_cnt =
                fetch_bbcnt(mmio_bpc, mmio_ebpc, false);
            g_assert(mmio_cnt);
            mmio_cnt->exec_insns_count += (cnt->insns - tb_insn_cnt);
        }
    }
    g_mutex_unlock(&profiling_info.lock);
    return;
}

/*
 * Called when we detect a middle exit (middle_exit_flag = 1).
 * This could be due to a fault or a MMIO instruction in the middle of TB.
 */
static void vcpu_tb_middle_exit_exec(unsigned int cpu_index, void *udata) {
    if (cpu_index != 0)
        return;

    uint64_t ebpc = qemu_plugin_u64_get(end_block, cpu_index);
    uint64_t current_pc = (uint64_t)udata;
    uint64_t tb_insn_cnt = qemu_plugin_u64_get(current_tb_insn_cnt, cpu_index);
    uint64_t tb_total_cnt =
        qemu_plugin_u64_get(current_tb_total_cnt, cpu_index);
    uint64_t bpc = qemu_plugin_u64_get(begin_block, cpu_index);
    uint64_t lpc_next = qemu_plugin_u64_get(last_pc_next_insn_addr, cpu_index);

    // fix insns count
    uint64_t unexecuted_insns = tb_total_cnt - tb_insn_cnt + 1;
    g_assert(unexecuted_insns >= 0);

    // determine whether it is a fault or a MMIO insn
    if (current_pc == lpc_next) // MMIO insn
    {
        unexecuted_insns -=
            1; // One MMIO instruction (single-instruction TB with mem_only
               // cflag) will not be counted, fix here
        if (!MMIO_split_flag) {
            MMIO_split_flag = 1;
            MMIO_split_tb_pc = bpc;
            MMIO_split_tb_endpc = ebpc;
        }
    }

    profiling_info.exec_count_all -= unexecuted_insns;

    if (profiling_info.start_profiling == true) {
        // fix bbv and profiling insns count
        profiling_info.bbv_exec_insns -= unexecuted_insns;
        profiling_info.profiling_insns -= unexecuted_insns;

        // when MMIO_split_flag is valid, save the counting information to the
        // original TB
        if (!MMIO_split_flag) {
            BasicBlockExecCount_t *cnt = fetch_bbcnt(bpc, ebpc, true);
            g_assert(cnt);
            cnt->exec_insns_count -= unexecuted_insns;
        } else {
            BasicBlockExecCount_t *mmio_original_cnt =
                fetch_bbcnt(MMIO_split_tb_pc, MMIO_split_tb_endpc, true);
            g_assert(mmio_original_cnt);
            mmio_original_cnt->exec_insns_count -= unexecuted_insns;
        }
    }
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb) {
    uint64_t pc = qemu_plugin_tb_vaddr(tb);
    size_t insns = qemu_plugin_tb_n_insns(tb);
    struct qemu_plugin_insn *first_insn = qemu_plugin_tb_get_insn(tb, 0);
    struct qemu_plugin_insn *last_insn = qemu_plugin_tb_get_insn(tb, insns - 1);
    uint64_t end_pc = qemu_plugin_insn_vaddr(last_insn);
    BasicBlockExecCount_t *cnt;

    UInt64Pair *hash_key = g_new(UInt64Pair, 1);
    hash_key->first = pc;
    hash_key->second = end_pc;

    // find exists bbv_info
    cnt = fetch_bbcnt(pc, end_pc, true);

    if (cnt) {
        // if exists add trans_count
        cnt->trans_count++;
    } else {
        // else create new bbv info
        cnt = g_new0(BasicBlockExecCount_t, 1);
        cnt->start_addr = pc;
        cnt->end_addr = end_pc;
        cnt->id = (++(profiling_info.unique_trans_id));
        cnt->insns = insns;
        cnt->trans_count = 1;
        cnt->exec_insns_count = 0;
        g_mutex_lock(&profiling_info.lock);
        g_hash_table_insert(profiling_info.bbv, (gpointer)hash_key,
                            (gpointer)cnt);
        g_mutex_unlock(&profiling_info.lock);
    }

    /*
     * check if we are executing linearly after the last block.
     * handle early block exits and normal branches in the callback.
     */
    qemu_plugin_register_vcpu_tb_exec_cond_cb(
        tb, vcpu_tb_middle_exit_exec, QEMU_PLUGIN_CB_NO_REGS,
        QEMU_PLUGIN_COND_NE, middle_exit_flag, 0, (void *)pc);
    qemu_plugin_register_vcpu_tb_exec_cb(tb, vcpu_tb_exec,
                                         QEMU_PLUGIN_CB_NO_REGS, (void *)cnt);

    /*
     * Now we can set start/end for this block so the next block can
     * check where we are at. Do this on the first instruction and not
     * the TB so we don't get mixed up with above.
     */
    qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
        first_insn, QEMU_PLUGIN_INLINE_STORE_U64, end_block,
        qemu_plugin_insn_vaddr(last_insn));
    qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
        first_insn, QEMU_PLUGIN_INLINE_STORE_U64, pc_after_block,
        qemu_plugin_insn_vaddr(last_insn) + qemu_plugin_insn_size(last_insn));
    qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
        first_insn, QEMU_PLUGIN_INLINE_STORE_U64, begin_block, pc);
    qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
        first_insn, QEMU_PLUGIN_INLINE_STORE_U64, current_tb_total_cnt, insns);
    qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
        first_insn, QEMU_PLUGIN_INLINE_STORE_U64, current_tb_insn_cnt, 0);
    qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
        first_insn, QEMU_PLUGIN_INLINE_STORE_U64, middle_exit_flag, 1);
    qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
        last_insn, QEMU_PLUGIN_INLINE_STORE_U64, middle_exit_flag, 0);

    uint32_t data;
    // register callback when translation inst is nemu_trap + Record Last PC
    for (size_t i = 0; i < insns; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        uint64_t ipc = qemu_plugin_insn_vaddr(insn);

        /* Store the PC of what we are about to execute */
        qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
            insn, QEMU_PLUGIN_INLINE_STORE_U64, last_pc, ipc);
        qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
            insn, QEMU_PLUGIN_INLINE_STORE_U64, last_pc_next_insn_addr,
            ipc + qemu_plugin_insn_size(insn));
        qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
            insn, QEMU_PLUGIN_INLINE_ADD_U64, current_tb_insn_cnt, 1);

        uint32_t size = qemu_plugin_insn_data(insn, &data, sizeof(uint32_t));
        assert(size == sizeof(uint32_t) || size == sizeof(uint16_t));
        if (data == 0x6b) {
            qemu_plugin_register_vcpu_insn_exec_cb(insn, nemu_trap_check,
                                                   QEMU_PLUGIN_CB_NO_REGS,
                                                   GUINT_TO_POINTER(data));
        }
    }
}

//
static void profiling_exit(qemu_plugin_id_t id, void *userdata) {
    g_mutex_lock(&profiling_info.lock);
    // close bbv file
    gzclose(profiling_info.bbv_file);
    // remove hashtable
    g_hash_table_destroy(profiling_info.bbv);
    // g_hash_table_foreach_remove(profiling_info.bbv,hash_table_remove,NULL);
    g_mutex_unlock(&profiling_info.lock);

#ifdef VSET_COUNT
    gzclose(vset_counter.log_file);
#endif
    //  printf("simpoint profiling exit all insns %ld\n",
    //         profiling_info.all_exec_insns);

    //  g_mutex_lock(&profiling_info.lock);
    //  gzclose(profiling_info.bbv_file);
    //
    //  GList *values;
    //  values=g_hash_table_get_values(profiling_info.bbv);
    //  if (values) {
    //    g_list_free(values);
    //  }
    //
    //  g_hash_table_destory(profiling_info.bbv);
    //// g_hash_table_foreach_remove(profiling_info.bbv,hash_table_remove,NULL);
    //  printf("%s\n all insns %ld","simpoint profiling
    //  exit",profiling_info.all_exec_insns);
    //  g_mutex_unlock(&profiling_info.lock);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info, int argc,
                                           char **argv) {
    // init qemu info
    qemu_info.target_name = info->target_name;
    qemu_info.max_vcpus = info->system.max_vcpus;
    qemu_info.smp_vcpus = info->system.smp_vcpus;
    qemu_info.system_emulation = info->system_emulation;

    // exit when in used mode
    if (!qemu_info.system_emulation) {
        return -1;
    }

    // init profiling info
    g_mutex_lock(&profiling_info.lock);

    for (int i = 0; i < argc; i++) {
        char *opt = argv[i];
        g_auto(GStrv) tokens = g_strsplit(opt, "=", 2);

        if (g_strcmp0(tokens[0], "workload") == 0) {

            strncpy(profiling_info.args.workload_path, tokens[1],
                    FILENAME_MXLEN);

        } else if (g_strcmp0(tokens[0], "intervals") == 0) {

            profiling_info.args.intervals = atoi(tokens[1]);

        } else if (g_strcmp0(tokens[0], "target") == 0) {

            strncpy(profiling_info.args.target_path, tokens[1], FILENAME_MXLEN);

        } else {

            printf("unknown argument %s %s\n", tokens[0], tokens[1]);
            return -1;
        }
    }

    profiling_info.start_profiling = false;
    profiling_info.unique_trans_id = 0;
    profiling_info.profiling_insns = 0;
    profiling_info.exec_count_all = 0;
    profiling_info.bbv_exec_insns = 0;

    // using for contain all bbl
    profiling_info.bbv =
        g_hash_table_new_full(hash_pair, compare_pair, g_free, g_free);

    printf("qemu profiling: workload %s intervals %ld target path %s\n",
           profiling_info.args.workload_path, profiling_info.args.intervals,
           profiling_info.args.target_path);

    g_mutex_unlock(&profiling_info.lock);

    // create simpoint_bbv.gz
    profiling_init(profiling_info.args.target_path,
                   profiling_info.args.workload_path);

    /* init execution state descriptors */
    state = qemu_plugin_scoreboard_new(sizeof(VCPUScoreBoard));

    /* scoreboard declarations */
    end_block =
        qemu_plugin_scoreboard_u64_in_struct(state, VCPUScoreBoard, end_block);
    pc_after_block = qemu_plugin_scoreboard_u64_in_struct(state, VCPUScoreBoard,
                                                          pc_after_block);
    last_pc =
        qemu_plugin_scoreboard_u64_in_struct(state, VCPUScoreBoard, last_pc);
    begin_block = qemu_plugin_scoreboard_u64_in_struct(state, VCPUScoreBoard,
                                                       begin_block);
    current_tb_insn_cnt = qemu_plugin_scoreboard_u64_in_struct(
        state, VCPUScoreBoard, current_tb_insn_cnt);
    current_tb_total_cnt = qemu_plugin_scoreboard_u64_in_struct(
        state, VCPUScoreBoard, current_tb_total_cnt);
    last_pc_next_insn_addr = qemu_plugin_scoreboard_u64_in_struct(
        state, VCPUScoreBoard, last_pc_next_insn_addr);
    middle_exit_flag = qemu_plugin_scoreboard_u64_in_struct(
        state, VCPUScoreBoard, middle_exit_flag);

#ifdef VSET_COUNT
    // vsetcounter init
    vset_counter.last_instuction = 0;
    vset_counter.vset_counter = 0;
    char vset_count_path[128];
    strcat(vset_count_path, profiling_info.args.workload_path);
    strcat(vset_count_path, "_vset_counter.log");

    vset_counter.log_file = gzopen(vset_count_path, "w");
#endif

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);

    // exit manual
    qemu_plugin_register_atexit_cb(id, profiling_exit, NULL);
    return 0;
}
