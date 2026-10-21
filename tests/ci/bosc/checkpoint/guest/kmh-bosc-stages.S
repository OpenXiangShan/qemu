/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef EXTRA_DDR_ADDR
#define EXTRA_DDR_ADDR 0x81100000
#endif
    .option norvc
    .section .text
    .globl _start

    .macro checkpoint
#ifdef QMP_TRIGGER
    li t0, 100000000
1:  addi t0, t0, -1
    bnez t0, 1b
#else
    .word 0x0000006b
#endif
    .endm

_start:
    la t0, fail
    csrw mtvec, t0
    li t0, (3 << 13) | (3 << 9)
    csrs mstatus, t0
    li s0, 0x81000000
    li s1, EXTRA_DDR_ADDR
    li s2, 0x123456789abcdef
    sd s2, 0(s0)
    sd s2, 0(s1)
    csrw mscratch, s2
    li t0, 0xabcdef40
    csrw mepc, t0
    li t0, 1 << 11
    csrs mstatus, t0
    fmv.d.x fs4, s2
    li t0, 2
    vsetvli zero, t0, e64, m1, ta, ma
    vmv.v.x v24, s2
    /* Configure an idle APLIC source and IMSIC delivery without pending IRQs. */
    li t0, 0x1e020000
    li t1, 4
    sw t1, 508(t0)              /* sourcecfg[127] */
    li t0, 0x70
    csrw 0x350, t0
    li t0, 1
    csrw 0x351, t0
    /* Keep timer/software IRQs pending but masked across the first slice. */
    li t0, 0x19004000
    sd zero, 0(t0)
    li t0, 0x19000000
    li t1, 1
    sw t1, 0(t0)
    la a0, stage1
    call puts
    csrwi vcsr, 3
    csrwi vstart, 1
    checkpoint

    call check_state
    csrr t0, mip
    andi t0, t0, (1 << 7) | (1 << 3)
    li t1, (1 << 7) | (1 << 3)
    bne t0, t1, fail
    li t0, 0x19000000
    sw zero, 0(t0)
    csrr t0, mepc
    li t1, 0xabcdef40
    bne t0, t1, fail
    csrr t0, mstatus
    li t1, 3 << 11
    and t0, t0, t1
    li t1, 1 << 11
    bne t0, t1, fail
    csrr t0, mscratch
    bne t0, s2, fail
    li t0, 0x1e020000
    lw t1, 508(t0)
    li t0, 4
    bne t0, t1, fail
    li t0, 0x70
    csrw 0x350, t0
    csrr t1, 0x351
    li t0, 1
    bne t0, t1, fail
    /* Enter S mode with identity translation and all RAM allowed by PMP. */
    li t0, -1
    csrw pmpaddr0, t0
    li t0, 0x1f
    csrw pmpcfg0, t0
    la t0, supervisor
    csrw mepc, t0
    li t0, 3 << 11
    csrc mstatus, t0
    li t0, 1 << 11
    csrs mstatus, t0
    mret

supervisor:
    li s2, 0xfedcba987654321
    sd s2, 0(s0)
    sd s2, 0(s1)
    csrw sscratch, s2
    fmv.d.x fs4, s2
    vmv.v.x v24, s2
    la a0, stage2
    call puts
    csrwi vcsr, 3
    csrwi vstart, 1
    checkpoint

    call check_state
    csrr t0, sscratch
    bne t0, s2, fail
    la a0, stage3
    call puts
    la a1, passed_exit
    j exit
1:  j 1b

check_state:
    csrr t0, vcsr
    li t1, 3
    bne t0, t1, fail
    csrr t0, vstart
    li t1, 1
    bne t0, t1, fail
    csrwi vstart, 0
    ld t0, 0(s0)
    bne t0, s2, fail
    ld t0, 0(s1)
    bne t0, s2, fail
    fmv.x.d t0, fs4
    bne t0, s2, fail
    vmv.x.s t0, v24
    bne t0, s2, fail
    csrr t0, vl
    li t1, 2
    bne t0, t1, fail
    ret

puts:
    li t2, 0x04000000
1:  lbu t0, 0(a0)
    beqz t0, 3f
2:  lbu t1, 20(t2)
    andi t1, t1, 0x20
    beqz t1, 2b
    sb t0, 0(t2)
    addi a0, a0, 1
    j 1b
3:  ret

fail:
    la a0, failed
    call puts
    la a1, failed_exit
exit:
    li a0, 0x20
    .balign 16
    slli zero, zero, 31
    ebreak
    srai zero, zero, 7
1:  j 1b

    .section .rodata
stage1: .asciz "STAGE1_M_READY\n"
stage2: .asciz "STAGE2_S_READY\n"
stage3: .asciz "STAGE3_PASS\n"
failed: .asciz "STAGE_FAIL\n"

    .balign 8
passed_exit: .dword 0x20026, 0
failed_exit: .dword 0x20026, 1
