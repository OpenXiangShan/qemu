#include "checkpoint/checkpoint.h"
#include "checkpoint/directed_tbs.h"
#include "hw/boards.h"
#include "hw/riscv/nemu.h"
#include "qemu/error-report.h"
#include "checkpoint/serializer_utils.h"
#include "checkpoint/checkpoint.pb.h"
#include "checkpoint/pb_encode.h"
#include <assert.h>
#include <zstd.h>

#define USE_ZSTD_COMPRESS
void serialize_pmem(uint64_t inst_count, int using_gcpt_mmio, char* hardware_status_buffer, int buffer_size, Qemu2Detail *q2d_buf)
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

    char filepath[FILEPATH_BUF_SIZE];

    //prepare path
    if (checkpoint.checkpoint_mode == SimpointCheckpointing) {
        strcpy(filepath,((GString*)(g_list_first(path_manager.checkpoint_path_list)->data))->str);
    }else if(checkpoint.checkpoint_mode==UniformCheckpointing){
        sprintf(filepath, "%s/%ld/_%ld_.gz", path_manager.uniform_path->str, inst_count, inst_count);
    }

    printf("prepare for generate checkpoint path %s pmem_size %ld\n",filepath,guest_pmem_size);
    assert(g_mkdir_with_parents(g_path_get_dirname(filepath), 0775)==0);
    if (q2d_buf != NULL)
        memcpy(q2d_buf->checkpoint_path, filepath, sizeof(FILEPATH_BUF_SIZE));

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

    size_t const compress_size = ZSTD_compress(compress_buffer, compress_buffer_size, pmem_addr, guest_pmem_size, 1);
    assert(compress_size<=compress_buffer_size&&compress_size!=0);

    FILE *compress_file=fopen(filepath,"wb");
    size_t fw_size = fwrite(compress_buffer,1,compress_size,compress_file);

    if (fw_size != (size_t)compress_size) {
        fprintf(stderr, "fwrite: %s : %s \n", filepath, strerror(errno));
    }
    if (fclose(compress_file)) {
        fprintf(stderr, "fclose: %s : %s \n", filepath, strerror(errno));
    }

    free(compress_buffer);

#endif

#ifdef USE_ZLIB_COMPRESS
    //zlib compress
    gzFile compressed_mem=NULL;
    compressed_mem=gzopen(filepath,"wb");

    if (!compressed_mem) {
        error_printf("filename %s can't open", filepath);
        return;
    }

    uint64_t write_size;
    uint64_t seg_size=1*1024*1024*1024;
    for (int i = 0;i<guest_pmem_size/seg_size;i++) {
        write_size=gzwrite(compressed_mem, pmem_addr+(i*seg_size),seg_size);
        printf("wirte in index %d\n",i);
        if (write_size != seg_size) {
            error_printf("qmp_gzpmemsave write error size %ld index %d\n",write_size,i);
            goto exit;
        }
    }
    info_report("success write into checkpoint file: %s",filepath);
exit:
    gzclose(compressed_mem);
#endif
    //    useless for now
    //    uint64_t mtime;
    //    cpu_physical_memory_read(MTIME_CMP_CPT_ADDR, &mtime, 8);
    //    cpu_physical_memory_write(CLINT_MMIO+CLINT_MTIME, &mtime, 8);
}



__attribute__ ((__unused__)) void serializeRegs(int cpu_index, char *buffer, single_core_rvgc_rvv_rvh_memlayout *cpt_percpu_layout, uint64_t all_cpu_num, uint64_t arg_mtime)  {
    CPUState *cs = qemu_get_cpu(cpu_index);
    RISCVCPU *cpu = RISCV_CPU(&cs->parent_obj);
    CPURISCVState *env = cpu_env(cs);
    uint64_t buffer_offset=0;
    assert(cpt_percpu_layout);

    buffer_offset = cpt_percpu_layout->int_reg_cpt_addr;
    for(int i = 0 ; i < 32; i++) {
        memcpy(buffer + buffer_offset + i * 8, &env->gpr[i], 8);
        printf("gpr %04d value %016lx ", i, env->gpr[i]);
        if ((i + 1) % 4 == 0) {
            printf("\n");
        }
    }
    info_report("Writting int registers to checkpoint memory");

//    F extertion
    buffer_offset = cpt_percpu_layout->float_reg_cpt_addr;
    for (int i = 0; i < 32; i++) {
        memcpy(buffer + buffer_offset + i * 8, &env->fpr[i], 8);
    }
    info_report("Writting float registers to checkpoint memory");
//    V extertion
//    if(env->virt_enabled) {
    buffer_offset = cpt_percpu_layout->vector_reg_cpt_addr;
    for (int i = 0; i < 32 * cpu->cfg.vlen / 64; i++) {
        memcpy(buffer + buffer_offset + i * 8, &env->vreg[i], 8);
        if ((i + 1) % (2) == 0) {
            info_report("[%lx]: 0x%016lx_%016lx",
                        (uint64_t)VECTOR_REG_CPT_ADDR + (i - 1) * 8, env->vreg[i - 1],
                        env->vreg[i]);
        }
    }
    info_report("Writting 32 * %d vector registers to checkpoint memory\n",
            cpu->cfg.vlen / 64);
//    }

//    CSR registers
    buffer_offset = cpt_percpu_layout->csr_reg_cpt_addr;
    for (int i = 0; i < CSR_TABLE_SIZE; i++) {
        if (csr_ops[i].read != NULL) {
            target_ulong val;
            csr_ops[i].read(env, i, &val);
            memcpy(buffer + buffer_offset + i * 8, &val, 8);
            if (val != 0) {
                info_report("csr id %x name %s value %lx", i, csr_ops[i].name, val);
            }
        }
    }
    info_report("Writting csr registers to checkpoint memory");

//    magic number write
#ifndef USING_PROTOBUF
    uint64_t flag_val = CPT_MAGIC_BUMBER;
    buffer_offset = 0xECDB0;
    memcpy(buffer + buffer_offset, &flag_val, 8);
#endif

    uint64_t tmp_mstatus = env->mstatus;
    // set mpie = mie
    tmp_mstatus =
        set_field(tmp_mstatus, MSTATUS_MPIE, get_field(tmp_mstatus, MSTATUS_MIE));

    // clear mie
    tmp_mstatus=set_field(tmp_mstatus, MSTATUS_MIE, 0);
    // set v flag for h-ext checkpoint
    tmp_mstatus=set_field(tmp_mstatus, MSTATUS_MPP, env->priv);
    buffer_offset = cpt_percpu_layout->csr_reg_cpt_addr + 0x300 * 8;
    memcpy(buffer + buffer_offset, &tmp_mstatus, 8);
    info_report("Writting mstatus registers to checkpoint memory: %lx mpp %lx",
                tmp_mstatus, env->priv);

    uint64_t tmp_mideleg = env->mideleg;
    buffer_offset = cpt_percpu_layout->csr_reg_cpt_addr + 0x303 * 8;
    memcpy(buffer + buffer_offset, &tmp_mideleg, 8);
    info_report("Writting mideleg registers to screen: %lx", tmp_mideleg);
    uint64_t tmp_mie = env->mie;

    // if (all_cpu_num == 1) {
    //     tmp_mstatus = set_field(tmp_mstatus, MSTATUS_MPIE, 0);
    // }

    // one cpu donot need time interrupt
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

    uint64_t tmp_mepc=env->pc;
    buffer_offset = cpt_percpu_layout->csr_reg_cpt_addr + 0x341 * 8;
    memcpy(buffer+buffer_offset,&tmp_mepc,8);
    info_report("Writting mepc registers to checkpoint memory: %lx",tmp_mepc);

    buffer_offset=cpt_percpu_layout->pc_cpt_addr;
    memcpy(buffer+buffer_offset,&env->pc,8);

    buffer_offset=cpt_percpu_layout->mode_cpt_addr;
    memcpy(buffer+buffer_offset,&env->priv,8);

    uint64_t tmp_mtime_cmp;
    cpu_physical_memory_read(CLINT_MMIO+CLINT_MTIMECMP+(cpu_index*8), &tmp_mtime_cmp, 8);
    buffer_offset=cpt_percpu_layout->mtime_cmp_cpt_addr+(cpu_index*8);
    memcpy(buffer+buffer_offset,&tmp_mtime_cmp,8);
    info_report("Writting mtime_cmp registers to checkpoint memory: %lx %x",tmp_mtime_cmp,CLINT_MMIO+CLINT_MTIMECMP+(cpu_index*8));

    uint64_t tmp_mtime;
    if (arg_mtime == 0) {
        cpu_physical_memory_read(CLINT_MMIO+CLINT_MTIME, &tmp_mtime, 8);
    }else {
        tmp_mtime = arg_mtime;
    }
    cpu_physical_memory_read(CLINT_MMIO+CLINT_MTIME, &tmp_mtime, 8);
    info_report("Read time value %lx", tmp_mtime);

    buffer_offset=cpt_percpu_layout->mtime_cpt_addr;
    memcpy(buffer + buffer_offset, &tmp_mtime, 8);
    info_report("Writting mtime registers to checkpoint memory: %lx %x", tmp_mtime,
                CLINT_MMIO + CLINT_MTIME);

    // write all_cpus
    buffer_offset = cpt_percpu_layout->csr_reserve;
    memcpy(buffer+buffer_offset, &all_cpu_num, 8);
    info_report("Writting all_cpus %ld to checkpoint memory: %lx", all_cpu_num, cpt_percpu_layout->csr_reserve);

    uint64_t tmp_vstart;
    csr_ops[0x008].read(env, 0x008, &tmp_vstart);
    info_report("vstart registers check: env %lx csr read %lx",env->vstart,tmp_vstart);
    uint64_t tmp_vxsat;
    csr_ops[0x009].read(env, 0x009, &tmp_vxsat);
    info_report("vxsat registers check: env %lx csr read %lx",env->vxsat,tmp_vxsat);
    uint64_t tmp_vxrm;
    csr_ops[0x00a].read(env, 0x00a, &tmp_vxrm);
    info_report("vxrm registers check: csr read %lx",tmp_vxrm);
    uint64_t tmp_vcsr;
    csr_ops[0x00f].read(env, 0x00f, &tmp_vcsr);
    info_report("vcsr registers check: csr read %lx",tmp_vcsr);
    uint64_t tmp_vl;
    csr_ops[0xc20].read(env, 0xc20, &tmp_vl);
    info_report("vl registers check: env %lx csr read %lx",env->vl,tmp_vl);
    uint64_t tmp_vtype;
    csr_ops[0xc21].read(env, 0xc21, &tmp_vtype);
    info_report("vtype registers check: env %lx csr read %lx",env->vtype,tmp_vtype);
    uint64_t tmp_vlenb;
    csr_ops[0xc22].read(env, 0xc22, &tmp_vlenb);
    info_report("vlenb registers check: csr read %lx",tmp_vlenb);

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
