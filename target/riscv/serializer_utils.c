#ifndef CONFIG_USER_ONLY
#include "checkpoint/checkpoint.h"
#include "hw/core/boards.h"
#include "hw/riscv/nemu.h"
#include "qemu/error-report.h"
#include "checkpoint/serializer_utils.h"
#include "checkpoint/checkpoint.pb.h"
#include "checkpoint/pb_encode.h"
#include "system/physmem.h"
#include "target/riscv/tcg/csr.h"
#include <assert.h>
#include <stdint.h>
#include <zstd.h>

#define USE_ZSTD_COMPRESS



bool serialize_pmem(uint64_t inst_count, int using_gcpt_mmio,
                    char *hardware_status_buffer, int buffer_size)
{

    MachineState *ms = MACHINE(qdev_get_machine());
    NEMUState *ns=NEMU_MACHINE(ms);
    uint64_t guest_pmem_size=ms->ram_size;
    uint64_t gcpt_mmio_pmem_size = 0;
    char* pmem_addr=ns->memory;
    assert(pmem_addr);

    // no using mmio, copy hardware status to phymem
    if (!using_gcpt_mmio) {
        gcpt_mmio_pmem_size = guest_pmem_size;
    }else {
        assert(hardware_status_buffer);
        gcpt_mmio_pmem_size += guest_pmem_size;
        gcpt_mmio_pmem_size += buffer_size;
    }

#define FILEPATH_BUF_SIZE 1024
    char filepath[FILEPATH_BUF_SIZE];

    //prepare path
    if (ns->nemu_args.checkpoint_mode == SimpointCheckpointing) {
        GList *path_item = g_list_first(ns->path_manager.checkpoint_path_list);
        if (path_item == NULL ||
            g_strlcpy(filepath, ((GString *)path_item->data)->str,
                      sizeof(filepath)) >= sizeof(filepath)) {
            error_report("invalid or oversized SimPoint checkpoint path");
            return false;
        }
        info_report("prepare for generate checkpoint path %s inst_count %ld pmem_size %ld", filepath, inst_count, guest_pmem_size);
    }else if(ns->nemu_args.checkpoint_mode==UniformCheckpointing || ns->nemu_args.checkpoint_mode == SyncUniformCheckpoint){
        if (snprintf(filepath, sizeof(filepath), "%s/%ld/_%ld_.gz",
                     ns->path_manager.uniform_path->str, inst_count,
                     inst_count) >= sizeof(filepath)) {
            error_report("uniform checkpoint path is too long");
            return false;
        }
        info_report("prepare for generate checkpoint path %s base_path %s inst_count %ld pmem_size %ld", filepath, ns->path_manager.uniform_path->str, inst_count, guest_pmem_size);
    } else {
        error_report("unsupported checkpoint mode");
        return false;
    }
    g_autofree char *checkpoint_dir = g_path_get_dirname(filepath);
    if (g_mkdir_with_parents(checkpoint_dir, 0775) != 0) {
        error_report("failed to create checkpoint directory %s: %s",
                     checkpoint_dir, strerror(errno));
        return false;
    }

#ifdef USE_ZSTD_COMPRESS
    //zstd compress
    size_t const compress_buffer_size = ZSTD_compressBound(gcpt_mmio_pmem_size);
    void* const compress_buffer = malloc(compress_buffer_size);
    assert(compress_buffer);

    // compress gcpt device memory
    size_t gcpt_compress_size = 0;
    if (using_gcpt_mmio) {
        gcpt_compress_size = ZSTD_compress(compress_buffer, compress_buffer_size, hardware_status_buffer, buffer_size, 1);
        assert(gcpt_compress_size <= compress_buffer_size && gcpt_compress_size != 0);
        fprintf(stdout, "compress gcpt success, compress size %ld", gcpt_compress_size);
    }

    size_t const compress_size = ZSTD_compress(
        compress_buffer, compress_buffer_size, pmem_addr, guest_pmem_size, 1);
    if (ZSTD_isError(compress_size)) {
        error_report("failed to compress checkpoint: %s",
                     ZSTD_getErrorName(compress_size));
        free(compress_buffer);
        return false;
    }

    FILE *compress_file=fopen(filepath,"wb");
    if (compress_file == NULL) {
        error_report("failed to open checkpoint %s: %s", filepath,
                     strerror(errno));
        free(compress_buffer);
        return false;
    }
    size_t fw_size = fwrite(compress_buffer,1,compress_size,compress_file);
    bool write_ok = fw_size == compress_size;

    if (!write_ok) {
        fprintf(stderr, "fwrite: %s: %s\n", filepath, strerror(errno));
    }
    if (fclose(compress_file)) {
        fprintf(stderr, "fclose: %s: %s\n", filepath, strerror(errno));
        write_ok = false;
    }

    free(compress_buffer);
    if (!write_ok) {
        return false;
    }
    info_report("serialize pmem finish\n");

#endif

#ifdef USE_ZLIB_COMPRESS
    //zlib compress
    gzFile compressed_mem=NULL;
    bool zlib_write_ok = true;
    compressed_mem=gzopen(filepath,"wb");

    if (!compressed_mem) {
        error_printf("filename %s can't open", filepath);
        return false;
    }

    uint64_t write_size;
    uint64_t seg_size=1*1024*1024*1024;
    for (int i = 0;i<guest_pmem_size/seg_size;i++) {
        write_size=gzwrite(compressed_mem, pmem_addr+(i*seg_size),seg_size);
        printf("wirte in index %d\n",i);
        if (write_size != seg_size) {
            error_printf("qmp_gzpmemsave write error size %ld index %d\n",write_size,i);
            zlib_write_ok = false;
            goto exit;
        }
    }
    info_report("success write into checkpoint file: %s",filepath);
exit:
    if (gzclose(compressed_mem) != Z_OK) {
        zlib_write_ok = false;
    }
    if (!zlib_write_ok) {
        return false;
    }
#endif
    //    useless for now
    //    uint64_t mtime;
    //    cpu_physical_memory_read(MTIME_CMP_CPT_ADDR, &mtime, 8);
    //    cpu_physical_memory_write(CLINT_MMIO+CLINT_MTIME, &mtime, 8);
    return true;
}



__attribute_maybe_unused__ void serializeRegs(int cpu_index, char *buffer, single_core_rvgc_rvv_rvh_memlayout *cpt_percpu_layout, uint64_t all_cpu_num, uint64_t arg_mtime)  {
    // init vars
    CPUState *cs = qemu_get_cpu(cpu_index);
    RISCVCPU *cpu = RISCV_CPU(&cs->parent_obj);
    CPURISCVState *env = cpu_env(cs);
    uint64_t buffer_offset=0;
    assert(cpt_percpu_layout);

    // store int regs
    buffer_offset = cpt_percpu_layout->int_reg_cpt_addr;
    for(int i = 0 ; i < 32; i++) {
        memcpy(buffer + buffer_offset + i * 8, &env->gpr[i], 8);
        info_report("gpr %04d value %016lx ", i, env->gpr[i]);
    }
    info_report("Writting int registers to checkpoint memory");

    // store fp regs
    buffer_offset = cpt_percpu_layout->float_reg_cpt_addr;
    for (int i = 0; i < 32; i++) {
        memcpy(buffer + buffer_offset + i * 8, &env->fpr[i], 8);
    }
    info_report("Writting float registers to checkpoint memory");

    // store vector regs
    buffer_offset = cpt_percpu_layout->vector_reg_cpt_addr;
    for (int i = 0; i < 32 * cpu->cfg.vlenb / 64; i++) {
        memcpy(buffer + buffer_offset + i * 8, &env->vreg[i], 8);
        if ((i + 1) % (2) == 0) {
            info_report("[%lx]: 0x%016lx_%016lx",
                        (uint64_t)VECTOR_REG_CPT_ADDR + (i - 1) * 8, env->vreg[i - 1],
                        env->vreg[i]);
        }
    }
    info_report("Writting 32 * %d vector registers to checkpoint memory",
            cpu->cfg.vlenb / 64);

    // store csr regs
    buffer_offset = cpt_percpu_layout->csr_reg_cpt_addr;
    for (int i = 0; i < CSR_TABLE_SIZE; i++) {
        if (csr_ops[i].read != NULL) {
            target_ulong val;
            csr_ops[i].read(env, i, &val);
            uint64_t checkpoint_val = val;
            memcpy(buffer + buffer_offset + i * 8, &checkpoint_val,
                   sizeof(checkpoint_val));
            // mstatus and mepc will set later
            if (val != 0 && i!= 0x300 && i != 0x341) {
                info_report("csr id %x name %s value " TARGET_FMT_lx,
                            i, csr_ops[i].name, val);
            }
        }
    }
    info_report("Writting csr registers to checkpoint memory");

    // if unuse protobuf we must set magic number at addr (0xECDB0 + (char*)buffer)
#ifndef USING_PROTOBUF
    uint64_t flag_val = CPT_MAGIC_BUMBER;
    buffer_offset = 0xECDB0;
    memcpy(buffer + buffer_offset, &flag_val, 8);
#endif

    uint64_t priv = env->priv;
    if (priv==PRV_M) {
        info_report("Generate checkpoint from M mode !!!!!!!!!!!!!!!!!!!!!!!!!");
    }
    uint64_t tmp_mstatus = env->mstatus;

    // if the hart not at M mode, we should preprocess some regs
    if (priv != PRV_M) {
        // set mpie = mie
        tmp_mstatus =
            set_field(tmp_mstatus, MSTATUS_MPIE, get_field(tmp_mstatus, MSTATUS_MIE));

        // clear mie
        tmp_mstatus=set_field(tmp_mstatus, MSTATUS_MIE, 0);

        // set priv in mpp
        tmp_mstatus=set_field(tmp_mstatus, MSTATUS_MPP, env->priv);

        // set v flag for h-ext checkpoint
        tmp_mstatus=set_field(tmp_mstatus, MSTATUS_MPV, env->virt_enabled);
    }

    // overwrite mstatus
    buffer_offset = cpt_percpu_layout->csr_reg_cpt_addr + 0x300 * 8;
    memcpy(buffer + buffer_offset, &tmp_mstatus, 8);
    info_report("Writting mstatus registers to checkpoint memory: %lx mpp %x",
                tmp_mstatus, env->priv);

    uint64_t tmp_mideleg = env->mideleg;
    buffer_offset = cpt_percpu_layout->csr_reg_cpt_addr + 0x303 * 8;
    memcpy(buffer + buffer_offset, &tmp_mideleg, 8);
    info_report("Writting mideleg registers to screen: %lx", tmp_mideleg);
    uint64_t tmp_mie = env->mie;

    // if workload only use one cpu, that donot need enable time interrupt
    if (all_cpu_num == 1) {
        tmp_mie=set_field(tmp_mie, MIE_STIE, 0); //restore disable stie
        tmp_mie=set_field(tmp_mie, MIE_UTIE, 0); //restore disable utie
//        tmp_mie=set_field(tmp_mie, MIE_MTIE, 0); //restore disable mtie
    }
    buffer_offset = cpt_percpu_layout->csr_reg_cpt_addr + 0x304 * 8;
    memcpy(buffer+buffer_offset, &tmp_mie, 8);
    info_report("Writting mie registers to screen: %lx",tmp_mie);

    // mip
    uint64_t tmp_mip=env->mip;
    buffer_offset = cpt_percpu_layout->csr_reg_cpt_addr + 0x344 * 8;
    memcpy(buffer + buffer_offset,&tmp_mip,8);
    info_report("Writting mip registers to checkpoint memory: %lx",tmp_mip);

    uint64_t tmp_hideleg=env->hideleg;
    buffer_offset = cpt_percpu_layout->csr_reg_cpt_addr + 0x603 * 8;
    memcpy(buffer+buffer_offset, &tmp_hideleg, 8);
    info_report("Writting hideleg registers to screen: %lx",tmp_hideleg);

//    uint64_t tmp_hie=env->hie 604;
//    uint64_t tmp_hip=env->hip 644;
//    uint64_t tmp_vsip=env->vsip 244;
    uint64_t tmp_hvip=env->hvip;
    buffer_offset = cpt_percpu_layout->csr_reg_cpt_addr + 0x645 * 8;
    memcpy(buffer+buffer_offset, &tmp_hvip, 8);
    info_report("Writting hvip registers to screen: %lx",tmp_hvip);

    uint64_t tmp_vsie=env->vsie;
    buffer_offset = cpt_percpu_layout->csr_reg_cpt_addr + 0x204 * 8;
    memcpy(buffer+buffer_offset, &tmp_vsie, 8);
    info_report("Writting vsie registers to screen: %lx",tmp_vsie);

    uint64_t tmp_satp=0;
    buffer_offset = cpt_percpu_layout->csr_reg_cpt_addr + 0x180 * 8;
    tmp_satp=*(uint64_t*)(buffer+buffer_offset);
    info_report("Satp from env %lx, Satp from memory %lx",env->satp, tmp_satp);


    uint64_t tmp_mepc = env->mepc;
    if (priv != PRV_M) {
        tmp_mepc = env->pc;
    }
    buffer_offset = cpt_percpu_layout->csr_reg_cpt_addr + 0x341 * 8;
    memcpy(buffer+buffer_offset,&tmp_mepc,8);
    info_report("Writting mepc registers to checkpoint memory: %lx", tmp_mepc);

    buffer_offset = cpt_percpu_layout->pc_cpt_addr;
    memcpy(buffer + buffer_offset, &env->pc, 8);
    info_report("Writting pc registers to checkpoint memory: %lx", env->pc);

    buffer_offset = cpt_percpu_layout->mode_cpt_addr;
    memcpy(buffer + buffer_offset, &env->priv, 8);
    info_report("Writting priv mode to checkpoint memory: %x", env->priv);

    uint64_t tmp_mtime_cmp;
    physical_memory_read(CLINT_MMIO + CLINT_MTIMECMP + cpu_index * 8,
                         &tmp_mtime_cmp, 8);
    buffer_offset=cpt_percpu_layout->mtime_cmp_cpt_addr+(cpu_index*8);
    memcpy(buffer+buffer_offset,&tmp_mtime_cmp,8);
    info_report("Writting mtime_cmp registers to checkpoint memory: %lx %x",tmp_mtime_cmp,CLINT_MMIO+CLINT_MTIMECMP+(cpu_index*8));

    // multicore will use global time
    uint64_t tmp_mtime;
    if (arg_mtime == 0) {
        physical_memory_read(CLINT_MMIO + CLINT_MTIME, &tmp_mtime, 8);
    }else {
        tmp_mtime = arg_mtime;
    }
    physical_memory_read(CLINT_MMIO + CLINT_MTIME, &tmp_mtime, 8);
    info_report("Read time value %lx", tmp_mtime);

    buffer_offset=cpt_percpu_layout->mtime_cpt_addr;
    memcpy(buffer + buffer_offset, &tmp_mtime, 8);
    info_report("Writting mtime registers to checkpoint memory: %lx %x", tmp_mtime,
                CLINT_MMIO + CLINT_MTIME);

    // write all_cpu nums in reverse space
    buffer_offset = cpt_percpu_layout->csr_reserve;
    memcpy(buffer+buffer_offset, &all_cpu_num, 8);
    info_report("Writting all_cpus %ld to checkpoint memory: %lx", all_cpu_num, cpt_percpu_layout->csr_reserve);

    target_ulong tmp_vstart;
    csr_ops[0x008].read(env, 0x008, &tmp_vstart);
    info_report("vstart registers check: env %x csr read " TARGET_FMT_lx,
                env->vstart, tmp_vstart);
    target_ulong tmp_vxsat;
    csr_ops[0x009].read(env, 0x009, &tmp_vxsat);
    info_report("vxsat registers check: env %x csr read " TARGET_FMT_lx,
                env->vxsat, tmp_vxsat);
    target_ulong tmp_vxrm;
    csr_ops[0x00a].read(env, 0x00a, &tmp_vxrm);
    info_report("vxrm registers check: csr read " TARGET_FMT_lx, tmp_vxrm);
    target_ulong tmp_vcsr;
    csr_ops[0x00f].read(env, 0x00f, &tmp_vcsr);
    info_report("vcsr registers check: csr read " TARGET_FMT_lx, tmp_vcsr);
    target_ulong tmp_vl;
    csr_ops[0xc20].read(env, 0xc20, &tmp_vl);
    info_report("vl registers check: env %x csr read " TARGET_FMT_lx,
                env->vl, tmp_vl);
    target_ulong tmp_vtype;
    csr_ops[0xc21].read(env, 0xc21, &tmp_vtype);
    info_report("vtype registers check: env %" PRIx64
                " csr read " TARGET_FMT_lx, env->vtype, tmp_vtype);
    target_ulong tmp_vlenb;
    csr_ops[0xc22].read(env, 0xc22, &tmp_vlenb);
    info_report("vlenb registers check: csr read " TARGET_FMT_lx, tmp_vlenb);

}

int cpt_header_encode(void *gcpt_mmio, checkpoint_header *cpt_header, single_core_rvgc_rvv_rvh_memlayout *cpt_memlayout) {
  int status;

  if (cpt_header == NULL) {
    cpt_header = &default_cpt_header;
  }
  if (cpt_memlayout == NULL) {
    cpt_memlayout = &default_cpt_percpu_layout;
  }

  pb_ostream_t stream =
    pb_ostream_from_buffer((void *)gcpt_mmio, sizeof(checkpoint_header) + sizeof(single_core_rvgc_rvv_rvh_memlayout));
  status = pb_encode_ex(&stream, checkpoint_header_fields, cpt_header, PB_ENCODE_NULLTERMINATED);
  if (!status) {
    printf("LOG: header encode error %s\n", stream.errmsg);
    return 0;
  }

  status = pb_encode_ex(&stream, single_core_rvgc_rvv_rvh_memlayout_fields, cpt_memlayout, PB_ENCODE_NULLTERMINATED);
  if (!status) {
    printf("LOG: body encode error %s\n", stream.errmsg);
    return 0;
  }

  return cpt_header->cpt_offset;
}
#endif
