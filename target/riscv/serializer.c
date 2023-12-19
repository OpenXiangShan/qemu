#include "checkpoint/checkpoint.h"
#include "cpu_bits.h"
#include "qemu/error-report.h"
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "tcg/tcg-op.h"
#include "disas/disas.h"
#include "exec/cpu_ldst.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"

#include "exec/translator.h"
#include "exec/log.h"
#include "semihosting/semihost.h"
#include "instmap.h"
#include <zlib.h>
#include "internals.h"
#include "hw/intc/riscv_aclint.h"
#include "qapi/qapi-commands-machine.h"

static void serializeRegs(void) {
    CPUState *cs = qemu_get_cpu(0);
    RISCVCPU *cpu = RISCV_CPU(&cs->parent_obj);


    for(int i = 0 ; i < 32; i++) {
        cpu_physical_memory_write(INT_REG_CPT_ADDR + i*8, &cs->env_ptr->gpr[i], 8);
        printf("gpr %04d value %016lx ",i,cs->env_ptr->gpr[i]);
        if ((i+1)%4==0) {
            printf("\n");
        }
    }
    info_report("Writting int registers to checkpoint memory");

    // F extertion
    for(int i = 0 ; i < 32; i++) {
        cpu_physical_memory_write(FLOAT_REG_CPT_ADDR + i*8, &cs->env_ptr->fpr[i], 8);
    }
    info_report("Writting float registers to checkpoint memory");


    // V extertion
//    if(cs->env_ptr->virt_enabled) {
        for(int i = 0; i < 32 * cpu->cfg.vlen / 64; i++) {
            cpu_physical_memory_write(VECTOR_REG_CPT_ADDR + i*8, &cs->env_ptr->vreg[i], 8);
            if ((i+1)%(2)==0) {
                info_report("[%lx]: 0x%016lx_%016lx",(uint64_t)VECTOR_REG_CPT_ADDR+(i-1)*8,cs->env_ptr->vreg[i-1],cs->env_ptr->vreg[i]);
            }
        }
        info_report("Writting 32 * %d vector registers to checkpoint memory",cpu->cfg.vlen /64);
//    }

    // CSR registers
    for(int i = 0; i < CSR_TABLE_SIZE; i++) {
        if(csr_ops[i].read != NULL) {
            target_ulong val;
            csr_ops[i].read(cs->env_ptr, i, &val);
            cpu_physical_memory_write(CSR_REG_CPT_ADDR + i * 8, &val, 8);
            if (val!=0) {
                info_report("csr id %x name %s value %lx",i,csr_ops[i].name,val);
            }
        }
    }
    info_report("Writting csr registers to checkpoint memory");
    uint64_t tmp_satp=0;
    cpu_physical_memory_read(CSR_REG_CPT_ADDR + 0x180 * 8, &tmp_satp, 8);
    info_report("Satp from env %lx, Satp from memory %lx",cs->env_ptr->satp, tmp_satp);

    uint64_t flag_val;
    flag_val = CPT_MAGIC_BUMBER;
    cpu_physical_memory_write(BOOT_FLAG_ADDR, &flag_val, 8);

    uint64_t tmp_mip=cs->env_ptr->mip;
    tmp_mip=set_field(tmp_mip, MIP_MTIP, 0);
    tmp_mip=set_field(tmp_mip, MIP_STIP, 0);
    cpu_physical_memory_write(CSR_REG_CPT_ADDR + 0x344 * 8, &tmp_mip, 8);
    info_report("Writting mip registers to checkpoint memory: %lx",tmp_mip);

    uint64_t tmp_mstatus=cs->env_ptr->mstatus;
    tmp_mstatus=set_field(tmp_mstatus, MSTATUS_MPIE, get_field(tmp_mstatus, MSTATUS_MIE));
    tmp_mstatus=set_field(tmp_mstatus, MSTATUS_MIE, 0);
    tmp_mstatus=set_field(tmp_mstatus, MSTATUS_MPP, cs->env_ptr->priv);
    cpu_physical_memory_write(CSR_REG_CPT_ADDR + 0x300 * 8, &tmp_mstatus, 8);
    info_report("Writting mstatus registers to checkpoint memory: %lx mpp %lx",tmp_mstatus,cs->env_ptr->priv);

    uint64_t tmp_mepc=cs->env_ptr->pc;
    cpu_physical_memory_write(CSR_REG_CPT_ADDR + 0x341 * 8, &tmp_mepc, 8);
    info_report("Writting mepc registers to checkpoint memory: %lx",tmp_mepc);

    cpu_physical_memory_write(PC_CPT_ADDR, &cs->env_ptr->pc, 8);
    cpu_physical_memory_write(MODE_CPT_ADDR, &cs->env_ptr->priv, 8);

    uint64_t tmp_mtime_cmp;
    cpu_physical_memory_read(CLINT_MMIO+CLINT_MTIMECMP, &tmp_mtime_cmp, 8);
    cpu_physical_memory_write(MTIME_CPT_ADDR, &tmp_mtime_cmp, 8);
    info_report("Writting mtime_cmp registers to checkpoint memory: %lx %x",tmp_mtime_cmp,CLINT_MMIO+CLINT_MTIMECMP);

    uint64_t tmp_mtime;
    cpu_physical_memory_read(CLINT_MMIO+CLINT_MTIME, &tmp_mtime, 8);
    cpu_physical_memory_write(MTIME_CMP_CPT_ADDR, &tmp_mtime, 8);
    info_report("Writting mtime registers to checkpoint memory: %lx %x",tmp_mtime,CLINT_MMIO+CLINT_MTIME);

    uint64_t tmp_vstart;
    csr_ops[0x008].read(cs->env_ptr, 0x008, &tmp_vstart);
    info_report("vstart registers check: env %lx csr read %lx",cs->env_ptr->vstart,tmp_vstart);
    uint64_t tmp_vxsat;
    csr_ops[0x009].read(cs->env_ptr, 0x009, &tmp_vxsat);
    info_report("vxsat registers check: env %lx csr read %lx",cs->env_ptr->vxsat,tmp_vxsat);
    uint64_t tmp_vxrm;
    csr_ops[0x00a].read(cs->env_ptr, 0x00a, &tmp_vxrm);
    info_report("vxrm registers check: csr read %lx",tmp_vxrm);
    uint64_t tmp_vcsr;
    csr_ops[0x00f].read(cs->env_ptr, 0x00f, &tmp_vcsr);
    info_report("vcsr registers check: csr read %lx",tmp_vcsr);
    uint64_t tmp_vl;
    csr_ops[0xc20].read(cs->env_ptr, 0xc20, &tmp_vl);
    info_report("vl registers check: env %lx csr read %lx",cs->env_ptr->vl,tmp_vl);
    uint64_t tmp_vtype;
    csr_ops[0xc21].read(cs->env_ptr, 0xc21, &tmp_vtype);
    info_report("vtype registers check: env %lx csr read %lx",cs->env_ptr->vtype,tmp_vtype);
    uint64_t tmp_vlenb;
    csr_ops[0xc22].read(cs->env_ptr, 0xc22, &tmp_vlenb);
    info_report("vlenb registers check: csr read %lx",tmp_vlenb);

}


static void serialize_pmem(uint64_t inst_count)
{
#define MEM_READ_BUF_SIZE 409600
#define FILEPATH_BUF_SIZE 256
    MemoryInfo * guest_pmem_info = qmp_query_memory_size_summary(NULL);
    uint64_t guest_pmem_size = guest_pmem_info->base_memory;

    uint8_t serialize_mem_buf[MEM_READ_BUF_SIZE];
    char filepath[FILEPATH_BUF_SIZE];

    uint64_t guest_pmem_start_addr = 0x80000000;
    gzFile compressed_mem=NULL;


    //prepare path
    if (checkpoint.checkpoint_mode == SimpointCheckpointing) {
        strcpy(filepath,((GString*)(g_list_first(path_manager.checkpoint_path_list)->data))->str);
        g_mkdir_with_parents(g_path_get_dirname(filepath),0775);
        compressed_mem=gzopen(filepath,"wb");
    }

    // else {
    //     strcpy(filepath, path_manager.output_path->str);
    //     assert(g_mkdir_with_parents(path_manager.output_path,0775)==0);
    //     strcat(filepath, "_");
    //     strcat(filepath, itoa(inst_count, str_num, 10));
    //     strcat(filepath, "_.gz");
    //     compressed_mem=gzopen(filepath);
    // }

    if (!compressed_mem) {
        error_printf("filename %s can't open", filepath);
        return;
    }

    for (int i = 0; i<guest_pmem_size/MEM_READ_BUF_SIZE; i++ ) {
        cpu_physical_memory_read(guest_pmem_start_addr + i * MEM_READ_BUF_SIZE, serialize_mem_buf, MEM_READ_BUF_SIZE);
        if (gzwrite(compressed_mem, serialize_mem_buf, (uint32_t) MEM_READ_BUF_SIZE) != MEM_READ_BUF_SIZE) {
            error_printf("qmp_gzpmemsave write error");
            goto exit;
        }
    }

exit:
    gzclose(compressed_mem);
    info_report("success write into checkpoint file: %s",filepath);
    uint64_t mtime;
    cpu_physical_memory_read(MTIME_CMP_CPT_ADDR, &mtime, 8);
    cpu_physical_memory_write(CLINT_MMIO+CLINT_MTIME, &mtime, 8);
}

static uint64_t get_next_instructions(void) {
    GList* first_insns_item=g_list_first(simpoint_info.cpt_instructions);
    uint64_t insns=0;
    if (first_insns_item==NULL) {
        return insns;
    } else {
        if (first_insns_item->data==0) {
            simpoint_info.cpt_instructions=g_list_remove(simpoint_info.cpt_instructions,g_list_first(simpoint_info.cpt_instructions)->data);
            path_manager.checkpoint_path_list=g_list_remove(path_manager.checkpoint_path_list,g_list_first(path_manager.checkpoint_path_list)->data);
            return 0;
        }
        return GPOINTER_TO_UINT(first_insns_item->data) * checkpoint.cpt_interval;
    }
}

static int get_env_cpu_mode(void){
    CPUState *cs = qemu_get_cpu(0);
    return cs->env_ptr->priv;
}

static uint64_t get_kernel_insns(void){
    CPUState *cs = qemu_get_cpu(0);
    return cs->env_ptr->kernel_insns;
}

static bool instrsCouldTakeCpt(uint64_t icount) {
    uint64_t limit_instructions = get_next_instructions();
    if (limit_instructions==0) {
//        error_printf("simpoint file or weight file error, get limit_instructions 0\n");
        return false;
    }
    limit_instructions += 100000;

    switch (checkpoint.checkpoint_mode) {
    case SimpointCheckpointing:
        if (limit_instructions==0) {
            break;
        } else {
            if (icount >= limit_instructions) {
                info_report("Should take cpt now: %lu limit_instructions: %lu", icount,limit_instructions);
                return true;
            } else if (icount % checkpoint.cpt_interval == 0) {
                info_report("Next cpt @ %lu, now: %lu",
                            limit_instructions, icount);
                break;
            } else {
                break;
            }
        }
    case UniformCheckpointing:
        if (icount >= checkpoint.next_uniform_point) {
            info_report("Should take cpt now: %lu", icount);
            return true;
        }
        break;
    case NoCheckpoint:
        break;
    default:
        break;
    }
    return false;
}

static void notify_taken(uint64_t icount) {
    info_report("Taking checkpoint @ instruction count %lu", icount);
    guint cpt_insns_list_length=g_list_length(simpoint_info.cpt_instructions);
    switch (checkpoint.checkpoint_mode) {
        case SimpointCheckpointing:

            if (cpt_insns_list_length!=0) {
                simpoint_info.cpt_instructions=g_list_remove(simpoint_info.cpt_instructions,g_list_first(simpoint_info.cpt_instructions)->data);
                path_manager.checkpoint_path_list=g_list_remove(path_manager.checkpoint_path_list,g_list_first(path_manager.checkpoint_path_list)->data);
            }

            info_report("left checkpoint numbers: %d",g_list_length(simpoint_info.cpt_instructions));
            break;
        case UniformCheckpointing:
            checkpoint.next_uniform_point += checkpoint.cpt_interval;
            break;
        default:
            break;
    }
}

static void serialize(uint64_t icount) {
    serializeRegs();
    serialize_pmem(icount);
}

static bool could_take_checkpoint(uint64_t icount){
    if (checkpoint.checkpoint_mode==NoCheckpoint) {
        return false;
    }
    if (!checkpoint.workload_loaded) {
        return false;
    }
    if (get_env_cpu_mode()==PRV_M) {
        return false;
    }
    if (!instrsCouldTakeCpt(icount)) {
        return false;
    }
    return true;
}

bool try_take_cpt(uint64_t icount) {
    uint64_t workload_exec_insns=icount-get_kernel_insns();
    if (could_take_checkpoint(workload_exec_insns)) {
        serialize(workload_exec_insns);
        notify_taken(workload_exec_insns);
        return true;
    }
    return false;
}
