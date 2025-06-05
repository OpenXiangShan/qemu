#include <errno.h>
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <zlib.h>
#include <glib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <qemu-plugin.h>
#include <fcntl.h>

#define FILENAME_MXLEN 256
#define MAX_CPUS 8

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

typedef struct Args {
  char worklaod_path[FILENAME_MXLEN];
  char target_path[FILENAME_MXLEN];
  uint64_t intervals;
} Args_t;

typedef struct BasicBlockExecCount {
  GMutex lock;
  uint64_t start_addr;
  uint64_t end_addr;
  uint64_t exec_count;
  uint64_t trans_count;
  uint64_t insns;
  uint64_t id;
} BasicBlockExecCount_t;

typedef struct QemuInfo {
  const char * target_name;
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

static QemuInfo_t qemu_info;
static ProfilingInfo_t profiling_info;

static void profiling_init(const char *target_dirname,
                           const char *workload_filename) {
  assert(g_mkdir_with_parents(target_dirname, 0775) == 0);

  char gz_path[FILENAME_MXLEN] = {0};

  snprintf(gz_path, FILENAME_MXLEN, "%s/%s", target_dirname, "simpoint_bbv.gz");

  printf("SimPoint bbv path %s \n", gz_path);

  g_mutex_lock(&profiling_info.lock);

  profiling_info.bbv_file = gzopen(gz_path, "w");

  g_mutex_unlock(&profiling_info.lock);
  assert(profiling_info.bbv_file);
}

static gint __attribute__((unused))
cmp_exec_count(gconstpointer a, gconstpointer b) {
  BasicBlockExecCount_t *ea = (BasicBlockExecCount_t *)a;
  BasicBlockExecCount_t *eb = (BasicBlockExecCount_t *)b;
  if (ea->exec_count * ea->insns > eb->exec_count * eb->insns) {
    return -1;
  } else if ((ea->exec_count * ea->insns) == (eb->exec_count * eb->insns)) {
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
  if (ec->exec_count != 0) {
    assert(profiling_info.bbv_file);
    g_mutex_lock(&ec->lock);
    gzprintf(profiling_info.bbv_file, ":%ld:%ld ", ec->id,
             ec->exec_count * ec->insns);
    //    gzprintf(profiling_info.bbv_file, ":%ld:%ld:%lx:%lx
    //    \n",ec->id,ec->exec_count*ec->insns,ec->start_addr,ec->end_addr);
    ec->exec_count = 0;
    g_mutex_unlock(&ec->lock);
  }
}

void clean_exec_count(gpointer key, gpointer value, gpointer user_data) {
  g_assert(value);
  BasicBlockExecCount_t *ec = (BasicBlockExecCount_t *)value;
  g_mutex_lock(&ec->lock);
  ec->exec_count = 0;
  g_mutex_unlock(&ec->lock);
}

void try_profiling(unsigned int cpu_index, void *userdata) {
  if (cpu_index != 0) {
    return;
  }
  // hash key
  uint64_t hash = (uint64_t)userdata;
  BasicBlockExecCount_t *cnt;

  g_mutex_lock(&profiling_info.lock);
  cnt = g_hash_table_lookup(profiling_info.bbv, (gconstpointer)hash);
  g_assert(cnt);
  g_mutex_unlock(&profiling_info.lock);

  g_mutex_lock(&profiling_info.lock);
  // add all inst
  profiling_info.exec_count_all += cnt->insns;

  if (profiling_info.start_profiling == true) {
    // add bbv and profiling inst
    profiling_info.bbv_exec_insns += cnt->insns;
    profiling_info.profiling_insns += cnt->insns;

    g_mutex_lock(&cnt->lock);
    cnt->exec_count++;
    g_mutex_unlock(&cnt->lock);

    if (profiling_info.bbv_exec_insns >= profiling_info.args.intervals) {
      assert(profiling_info.bbv_file);
      gzprintf(profiling_info.bbv_file, "T");

      GList *sorted_list =
          g_list_sort(g_hash_table_get_values(profiling_info.bbv), cmp_id);

      // output to bbv
      g_list_foreach(sorted_list, bbv_output, NULL);
      // free sort list
      g_list_free(sorted_list);

      gzprintf(profiling_info.bbv_file, "\n");

      // clean after output
      profiling_info.bbv_exec_insns = 0;
    }
  }

  g_mutex_unlock(&profiling_info.lock);
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

  if (((data & 0x80007057) == 0x80007057 || (data & 0xc0007057) == 0xc0007057 ||
       (data & 0x7057) == 0x7057) &&
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
  if (vcpu_index!=0) {
    return;
  }
  uint64_t data = (uint64_t)userdata;
  static int nemu_trap_count = 0;

  g_mutex_lock(&profiling_info.lock);
  printf("From plugin, all exec insn %ld\n", profiling_info.exec_count_all);
  if (profiling_info.start_profiling == true) {
    // prepare exit
    printf("PLUGIN: After profiling GET NEMU_TRAP\n");
    printf("SimPoint profiling exit, total guest instructions = %ld\n", profiling_info.profiling_insns);
    profiling_exit = 1;
    g_mutex_unlock(&profiling_info.lock);
    return;
  }
  // disable timer
  nemu_trap_count += 1;
  // start profiling
  if (nemu_trap_count == 2) {
    profiling_info.start_profiling = true;
    g_hash_table_foreach(profiling_info.bbv, clean_exec_count, NULL);
    printf("PLUGIN: worklaod loaded........................\n");
  }
  g_mutex_unlock(&profiling_info.lock);
  return;
}

// translation block translation call back
static void prepare_bbl(qemu_plugin_id_t id, struct qemu_plugin_tb *tb) {
  uint64_t pc = qemu_plugin_tb_vaddr(tb);
  size_t insns = qemu_plugin_tb_n_insns(tb);
  struct qemu_plugin_insn *tb_end_insn = qemu_plugin_tb_get_insn(tb, insns - 1);
  uint64_t end_pc = qemu_plugin_insn_vaddr(tb_end_insn);
  BasicBlockExecCount_t *cnt;

  char index_buf[128] = {0};
  // create hash key
  sprintf(index_buf, "%ld%ld", pc, insns);
  uint64_t hash_key = atol(index_buf);

  uint32_t *data = malloc(sizeof(uint32_t));
  // register callback when translation inst is nemu_trap
  for (size_t i = 0; i < insns; i++) {
    struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);

    uint32_t size = qemu_plugin_insn_data(insn, data, sizeof(uint32_t));
    assert(size == sizeof(uint32_t) || size == sizeof(uint16_t));
    if (*data == 0x6b) {
      qemu_plugin_register_vcpu_insn_exec_cb(insn, nemu_trap_check,
                                             QEMU_PLUGIN_CB_NO_REGS, (void*)(long)*data);
    } 
    // else
    // if((((*data)&0x80007057)==0x80007057||((*data)&0xc0007057)==0xc0007057||((*data)&0x7057)==0x7057)){
    //   qemu_plugin_register_vcpu_insn_exec_cb(insn,instruction_check,QEMU_PLUGIN_CB_NO_REGS,*data);
    //}
  }
  free(data);

  g_mutex_lock(&profiling_info.lock);
  // find exists bbv_info
  cnt = (BasicBlockExecCount_t *)g_hash_table_lookup(profiling_info.bbv,
                                                     (gconstpointer)hash_key);

  if (cnt) {
    // if exists add trans_count
    g_mutex_lock(&cnt->lock);
    cnt->trans_count++;
    g_mutex_unlock(&cnt->lock);
  } else {
    // else create new bbv info
    cnt = g_new0(BasicBlockExecCount_t, 1);
    g_mutex_lock(&cnt->lock);
    cnt->start_addr = pc;
    cnt->end_addr = end_pc;
    cnt->id = (++(profiling_info.unique_trans_id));
    cnt->insns = insns;
    cnt->trans_count = 1;
    cnt->exec_count = 0;
    g_mutex_unlock(&cnt->lock);
    g_hash_table_insert(profiling_info.bbv, (gpointer)hash_key, (gpointer)cnt);
  }

  g_mutex_unlock(&profiling_info.lock);

  qemu_plugin_register_vcpu_tb_exec_cb(
      tb, try_profiling, QEMU_PLUGIN_CB_NO_REGS, (void *)hash_key);
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
  ////  g_hash_table_foreach_remove(profiling_info.bbv,hash_table_remove,NULL);
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

      strncpy(profiling_info.args.worklaod_path, tokens[1], FILENAME_MXLEN);

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
      g_hash_table_new_full(NULL, g_direct_equal, NULL, g_free);

  printf("qemu profiling: workload %s intervals %ld target path %s\n",
         profiling_info.args.worklaod_path, profiling_info.args.intervals,
         profiling_info.args.target_path);

  g_mutex_unlock(&profiling_info.lock);

  // create simpoint_bbv.gz
  profiling_init(profiling_info.args.target_path,
                 profiling_info.args.worklaod_path);

#ifdef VSET_COUNT
  // vsetcounter init
  vset_counter.last_instuction = 0;
  vset_counter.vset_counter = 0;
  char vset_count_path[128];
  strcat(vset_count_path, profiling_info.args.worklaod_path);
  strcat(vset_count_path, "_vset_counter.log");

  vset_counter.log_file = gzopen(vset_count_path, "w");
#endif

  qemu_plugin_register_vcpu_tb_trans_cb(id, prepare_bbl);

  // exit manual
  qemu_plugin_register_atexit_cb(id, profiling_exit, NULL);
  return 0;
}
