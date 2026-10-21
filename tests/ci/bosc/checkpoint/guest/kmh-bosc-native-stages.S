/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HART_MASK
#define HART_MASK 1
#endif
#ifndef SNAP_PRIV
#define SNAP_PRIV 1
#endif
#ifndef UART_PA
#define UART_PA 0x04000000
#endif
    .option norvc
    .section .text
    .globl _start
_start:
    csrr a6, mhartid
    la sp, stacks
    addi t0, a6, 1
    slli t0, t0, 12
    add sp, sp, t0
    li t0, -1
    csrw pmpaddr0, t0
    li t0, 0x1f
    csrw pmpcfg0, t0
    la t0, trap
    csrw mtvec, t0
    li t0, -1
    csrw 0x30c, t0
    csrw 0x10c, t0
    li t0, 0x6000 | 0x600
    csrs mstatus, t0
    li t0, 0x70
    csrw 0x150, t0
    li t0, 1
    csrw 0x151, t0
    li t0, 0xc0
    csrw 0x150, t0
    li t0, 0x200000
    csrw 0x151, t0
    li t0, 0x80
    csrw 0x150, t0
    csrw 0x151, t0
    /* S timer pending, masked until the test explicitly checks it. */
    li t0, 0x19004000
    slli t1, a6, 3
    add t0, t0, t1
    sd zero, 0(t0)
    csrw mie, zero
    bnez a6, 1f
#if UART_PA == 0x04000000
    li t0, 0x1e024000
#else
    li t0, 0x10001e024000
#endif
    li t1, 4
    sw t1, 508(t0)
    li t1, 127
    li t2, 0x1cdc
    add t2, t2, t0
    sw t1, 0(t2)
1:  li s2, 0x123456789abcdef
    csrw sscratch, s2
    fmv.d.x fs4, s2
    li t0, 2
    vsetvli zero, t0, e64, m1, ta, ma
    vmv.v.x v24, s2
    csrwi vcsr, 3
    la t0, snapshot
    csrw mepc, t0
    li t0, (3 << 11)
    csrc mstatus, t0
    li t0, (SNAP_PRIV << 11)
    csrs mstatus, t0
    mret
snapshot:
    li t0, 1
    sll t0, t0, a6
    la t1, arrived
    amoor.d zero, t0, (t1)
1:  ld t2, 0(t1)
    li t3, HART_MASK
    bne t2, t3, 1b
    bnez a6, wait_resume
#if defined(QMP_TRIGGER)
    la a0, qmp_text
    jal puts
    /* The save operation publishes the native directory before resuming. */
    li t0, 0x92000008
1:  lw t1, 0(t0)
    li t2, 3
    bne t1, t2, 1b
#elif defined(INTERVAL_TRIGGER)
    li t0, 100000000
1:  addi t0, t0, -1
    bnez t0, 1b
#else
    .word 0x0000006b
#endif
    la t0, resumed
    li t1, 1
    sd t1, 0(t0)
wait_resume:
    la t0, resumed
1:  ld t1, 0(t0)
    beqz t1, 1b
    li t0, 0x123456789abcdef
    bne s2, t0, fail
    fmv.x.d t1, fs4
    bne t0, t1, fail
    vmv.x.s t1, v24
    bne t0, t1, fail
    csrr t1, vcsr
    li t0, 3
    bne t0, t1, fail
#if SNAP_PRIV == 1
    csrr t1, sscratch
    bne t1, s2, fail
    li t0, 0xc0
    csrw 0x150, t0
    csrr t1, 0x151
    li t0, 0x200000
    bne t0, t1, fail
    li t0, 0x80
    csrw 0x150, t0
    csrr t1, 0x151
    li t0, 0x80
    bne t0, t1, fail
#endif
    li t0, 0x80000000
    ld t1, 0(t0)
    bnez t1, fail
#if UART_PA == 0x04000000
    li t0, 0x1e024000
#else
    li t0, 0x10001e024000
#endif
    lw t1, 508(t0)
    li t2, 4
    bne t1, t2, fail
    li t2, 0x1c0c
    add t2, t0, t2
    lw t1, 0(t2)
    li t2, 0x80000000
    slli t1, t1, 32
    srli t1, t1, 32
    bne t1, t2, fail
    /* A real machine trap must work after firmware entry bytes are restored. */
    li a7, 0
    ecall
    li t0, 1
    sll t0, t0, a6
    la t1, finished
    amoor.d zero, t0, (t1)
1:  ld t2, 0(t1)
    li t3, HART_MASK
    bne t2, t3, 1b
    bnez a6, park
#ifdef SECOND_SLICE
    .word 0x0000006b
#endif
    la a0, pass_text
    jal puts
    li a7, 1
    ecall
park:
    j park
fail:
    la a0, fail_text
    jal puts
    li a7, 2
    ecall
    j park
trap:
    /* Preserve both scratch registers and advance past the test ecall. */
    csrw mscratch, t0
    bnez a7, exit_test
    csrr t0, mcause
    addi t0, t0, -8
    beqz t0, 1f
    addi t0, t0, -1
    bnez t0, trap_fail
1:  csrr t0, mip
    andi t0, t0, 0x80
    beqz t0, trap_fail
    csrr t0, mepc
    addi t0, t0, 4
    csrw mepc, t0
    csrr t0, mscratch
    mret
trap_fail:
    j fail
exit_test:
    li a0, 0x20
    la a1, exit_ok
    li t0, 1
    beq a7, t0, 1f
    la a1, exit_bad
1:  slli zero, zero, 0x1f
    ebreak
    srai zero, zero, 7
    j park
puts:
    li t0, UART_PA
1:  lbu t1, 0(a0)
    beqz t1, 3f
2:  lbu t2, 20(t0)
    andi t2, t2, 0x20
    beqz t2, 2b
    sb t1, 0(t0)
    addi a0, a0, 1
    j 1b
3:  ret
    .section .rodata
qmp_text: .asciz "CHECKPOINT_QMP_READY\n"
pass_text: .asciz "NATIVE_HARTS_PASS\n"
fail_text: .asciz "NATIVE_HARTS_FAIL\n"
    .balign 8
exit_ok: .quad 0x20026, 0
exit_bad: .quad 0x20026, 1
    .section .data
    .balign 8
arrived: .quad 0
resumed: .quad 0
finished: .quad 0
    .section .bss
    .balign 16
stacks: .space 64 * 4096
