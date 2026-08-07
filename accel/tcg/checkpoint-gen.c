#include "qemu/osdep.h"
#include "cpu.h"
#include "target/riscv/cpu.h"
#include "tcg/tcg.h"
#include "tcg/tcg-temp-internal.h"
#include "tcg/tcg-op.h"
#include "exec/translator.h"
#include "checkpoint/checkpoint.h"
#include "exec/helper-proto-common.h"
#include <stdio.h>

#define HELPER_H "accel/tcg/checkpoint-helper.h"
#include "exec/helper-info.c.inc"
#undef HELPER_H

static void checkpoint_gen_empty_check_cb(void)
{
    TCGv_i32 cpu_index = tcg_temp_ebb_new_i32();
    TCGv_i64 udata = tcg_temp_ebb_new_i64();

    tcg_gen_ld_i64(udata, tcg_env, offsetof(CPURISCVState, profiling_insns));
    tcg_gen_ld_i32(cpu_index, tcg_env,
                   -offsetof(ArchCPU, env) + offsetof(CPUState, cpu_index));

    gen_helper_checkpoint_sync_check(cpu_index,udata);

    tcg_temp_free_i32(cpu_index);
    tcg_temp_free_i64(udata);
}

void checkpoint_gen_empty_callback(void){
    checkpoint_gen_empty_check_cb();
}

extern NEMUState *local_nemu_state;
void helper_checkpoint_sync_check(uint32_t cpu_index, uint64_t udata) {
    if (local_nemu_state) {
        try_take_cpt(local_nemu_state, udata, cpu_index, false);
    }
}
