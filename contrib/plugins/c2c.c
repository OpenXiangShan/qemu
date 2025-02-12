#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <glib.h>
#include <qemu-plugin.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <glib.h>

#define FILENAME_MXLEN 256
QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

typedef struct MonitorInfo {
    GMutex lock;
    int interval_length;
    // Paddr to block include
    int c2c_cnt;
    int current_interval_id;
    GHashTable * commit_table;
    char output_dir[FILENAME_MXLEN];
    FILE* output_file;
    int num_cpus;
    int *inst_cnt;
    int cores_stop;
    /*int pause_cnt;*/
    /*GCond *condvars; // condvars[0] owned by store 0*/
    /*GCond stop_req;*/
    /*GCond stop_resp;*/
    bool dummy;
} MonitorInfo_t;

typedef struct CommInfo {
    int last_writer;
    int communication_cnt;
    uint64_t usage_vec;
    /*int interval_id;*/
    GMutex lock;
} CommInfo_t;

// Paddr to block include
static MonitorInfo_t mon_info; 

void sum_cnts(gpointer key, gpointer value, gpointer user_data) {
    int *sum_ptr = (int *)user_data;
    *sum_ptr += ((CommInfo_t *) value)->communication_cnt;
    ((CommInfo_t *) value)->communication_cnt = 0;
}

void on_inst_exec(unsigned int cpu_index, void *userdata) {
    if (mon_info.dummy) {
        mon_info.inst_cnt[cpu_index]++;
        if (cpu_index == 0 && mon_info.inst_cnt[0] % mon_info.interval_length == 0) {
            printf("CPU Inst cnt:");
            for (int i = 0; i < mon_info.num_cpus; i++) {
                printf(" %d", mon_info.inst_cnt[i]);
            }
            printf("\n\n");
        }
        return;
    }

    assert(mon_info.inst_cnt[0] <= 2000000);
    g_atomic_int_add(&mon_info.inst_cnt[cpu_index], 1);
    if (cpu_index != 0) {
        while (mon_info.cores_stop) {
            /*g_mutex_lock(&mon_info.lock);
            mon_info.pause_cnt++;
            g_cond_signal(&mon_info.stop_req);
            while (mon_info.cores_stop) {
                g_cond_wait(&mon_info.condvars[0], &mon_info.lock);
            }
            mon_info.pause_cnt--;
            g_mutex_unlock(&mon_info.lock);*/
        }
        return;
    }

    if (0 && mon_info.inst_cnt[0] >= 0 && mon_info.inst_cnt[0] <= 200) {
       // print pc
        uint64_t pc = (uint64_t) userdata;
        printf("%lx\n", pc);
    }
    assert(mon_info.cores_stop == 0);
    if (mon_info.inst_cnt[cpu_index] == mon_info.interval_length) {
        g_mutex_lock(&mon_info.lock);
        mon_info.current_interval_id++;
        // send wait signal
        mon_info.cores_stop = 1;
        
        // fence
        __sync_synchronize();
        // check for halted cpu first

        /*while (mon_info.pause_cnt < (mon_info.num_cpus - 1)) {
            g_cond_wait(&mon_info.condvars[i], &mon_info.lock);
        }
        assert(mon_info.pause_cnt == mon_info.num_cpus - 1);*/
        // Collect al communication_cnt
        int sum = 0;
        g_hash_table_foreach(mon_info.commit_table, sum_cnts, &sum);
        mon_info.c2c_cnt = sum;
        printf("Previous interval count is %d\n", mon_info.c2c_cnt);
        printf("Starting interval No. %d \n", mon_info.current_interval_id);
        // Collect data
        mon_info.c2c_cnt = 0;
        // TODO this is not accurate
        printf("CPU Inst cnt:");
        for (int i = 0; i < mon_info.num_cpus; i++) {
            printf(" %d", mon_info.inst_cnt[i]);
        }
        printf("\n\n");
        memset(mon_info.inst_cnt, 0, sizeof(int) * mon_info.num_cpus);
        mon_info.cores_stop = 0;
        /*for (int i = 1; i < mon_info.num_cpus; i++) {
            g_cond_signal(&mon_info.condvars[0]);
        }*/
        g_mutex_unlock(&mon_info.lock);
    }
    
}

static void on_ldst(unsigned int vcpu_index, qemu_plugin_meminfo_t info, uint64_t vaddr, void *userdata) {
    if (mon_info.dummy) {
        return;
    }
    struct qemu_plugin_hwaddr* paddr_handle = qemu_plugin_get_hwaddr(info, vaddr);
    uint64_t paddr = paddr_handle ? qemu_plugin_hwaddr_phys_addr(paddr_handle) : vaddr;
    if (paddr_handle && qemu_plugin_hwaddr_is_io(paddr_handle)) {
        return;
    }

    uint64_t block_addr = paddr & 0xffffffffffffffc0;
    bool is_store = qemu_plugin_mem_is_store(info);
    CommInfo_t *comm_info;
    g_mutex_lock(&mon_info.lock);
    comm_info = (CommInfo_t *)g_hash_table_lookup(mon_info.commit_table, (gconstpointer) block_addr);
    if (is_store) {
        if (comm_info) {
            /*g_mutex_lock(&comm_info->lock);*/
            /*if (comm_info->usage_vec != (1 << vcpu_index))*/
                /*printf("W: cpu %d: paddr %lx vaddr %lx\n", vcpu_index, block_addr, vaddr);*/
            comm_info->last_writer = vcpu_index;
            comm_info->usage_vec = 1 << vcpu_index;
            /*g_mutex_unlock(&comm_info->lock);*/
        } else {
            comm_info = g_new0(CommInfo_t, 1);
            /*g_mutex_lock(&comm_info->lock);*/
            comm_info->last_writer = vcpu_index;
            comm_info->communication_cnt = 0;
            comm_info->usage_vec = 1 << vcpu_index;
            /*g_mutex_unlock(&comm_info->lock);*/
            g_hash_table_insert(mon_info.commit_table, (gpointer)block_addr, (gpointer) comm_info);
        }       
    } else {
        if (comm_info) {
            /*g_mutex_lock(&comm_info->lock);*/
            /*if (comm_info->last_writer != vcpu_index)*/
                /*printf("Increased communication_cnt");*/
            if (!(comm_info->usage_vec & (1 << vcpu_index))) {
                comm_info->usage_vec ^= ( 1 << vcpu_index);
                /*printf("R: cpu %d from %d: paddr %lx vaddr %lx\n", vcpu_index, comm_info->last_writer, block_addr, vaddr);*/
                comm_info->communication_cnt ++; //+= (comm_info->last_writer != vcpu_index ? 1 : 0);
             }
           /*g_mutex_unlock(&comm_info->lock);*/
        }
    }
    
    g_mutex_unlock(&mon_info.lock);
    // put entry back to hash table 
    return;
}

static void on_translate(qemu_plugin_id_t id, struct qemu_plugin_tb *tb) {
    size_t insns = qemu_plugin_tb_n_insns(tb);
    for (size_t i = 0; i < insns; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);

        qemu_plugin_register_vcpu_insn_exec_cb(insn, on_inst_exec, QEMU_PLUGIN_CB_NO_REGS,
                                               (void *) qemu_plugin_insn_vaddr(insn));
        qemu_plugin_register_vcpu_mem_cb(insn, on_ldst, QEMU_PLUGIN_CB_NO_REGS, QEMU_PLUGIN_MEM_RW, NULL);
    }
}

static void on_plugin_exit(qemu_plugin_id_t id, void *userdata) {
    g_mutex_lock(&mon_info.lock);
    if (mon_info.dummy) {
        printf("CPU Inst cnt:");
        for (int i = 0; i < mon_info.num_cpus; i++) {
            printf(" %d", mon_info.inst_cnt[i]);
        }
        printf("\n\n");
    }
    // Close output_dir
    /*gz_close(mon_info.output_dir);*/
    fclose(mon_info.output_file);
    g_hash_table_destroy(mon_info.commit_table);
    g_free(mon_info.inst_cnt);
    /*for (int i = 1; i < mon_info.num_cpus; i++){
        g_cond_clear(&mon_info.condvars[i]);
    }
    g_free(mon_info.condvars);*/

    g_mutex_unlock(&mon_info.lock);
}
    // usage "-plugin ${qemu}/lib/libprofiling.so,workload=${workload_name},intervals=${intervals},target=$out"
// usage "-plugin build/contrib/plugins/libc2c.so,interval=,output_dir="
QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info, int argc,
                                           char **argv) {
    if (!info->system_emulation) {
        return -1;
    }
    
    // init
    g_mutex_lock(&mon_info.lock);
    mon_info.interval_length = 10000000; // Default 1M
    mon_info.dummy = false;
    for (int i = 0; i < argc; i++) {
        char *opt = argv[i];
        g_auto(GStrv) tokens = g_strsplit(opt, "=", 2);

        if (g_strcmp0(tokens[0], "interval") == 0) {
            mon_info.interval_length = atoi(tokens[1]);
        } else if (g_strcmp0(tokens[0], "output_dir") == 0) {
            strncpy(mon_info.output_dir, tokens[1], FILENAME_MXLEN);
        } else if (g_strcmp0(tokens[0], "dummy") == 0) {
            mon_info.dummy = true;
        } else {
            printf("unknown arument %s %s\n", tokens[0], tokens[1]);
            return -1;
        }
    }
    mon_info.commit_table = g_hash_table_new_full(NULL, g_direct_equal, NULL, g_free);
    printf("Communication monitor: interval length is %d instructions\n", mon_info.interval_length);

    // Init output file
    printf("Output dir is %s\n", mon_info.output_dir);
    assert(g_mkdir_with_parents(mon_info.output_dir, 0755) == 0);
    char gz_path[FILENAME_MXLEN] = {0};
    snprintf(gz_path, FILENAME_MXLEN, "%s/%s", mon_info.output_dir, "c2c.txt");

    printf("Core to core data transfer info file is %s \n", gz_path);

    mon_info.output_file = fopen(gz_path, "w");

    assert(mon_info.output_file);
    
    mon_info.num_cpus = info->system.smp_vcpus;
    mon_info.inst_cnt = g_malloc(sizeof(int) * mon_info.num_cpus);
    memset(mon_info.inst_cnt, 0, sizeof(int) * mon_info.num_cpus);

    // init conds
/*    mon_info.condvars = g_malloc(sizeof(GCond) * mon_info.num_cpus);
    for (int i = 1; i < mon_info.num_cpus; i++){
        g_cond_init(&mon_info.condvars[i]);
    }*/

    mon_info.cores_stop = 0;
    g_mutex_unlock(&mon_info.lock);

    qemu_plugin_register_vcpu_tb_trans_cb(id, on_translate);
    qemu_plugin_register_atexit_cb(id, on_plugin_exit, NULL);
    return 0;
}
