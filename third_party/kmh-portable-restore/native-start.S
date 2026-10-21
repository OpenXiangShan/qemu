/* SPDX-License-Identifier: GPL-2.0-or-later */
    .option norvc
    .section .text.entry
    .globl _native_start
_native_start:
    csrci mstatus, 8
    csrw mie, zero
    csrr a0, mhartid
    li t0, 64
    bgeu a0, t0, 1f
    la sp, native_stacks
    addi t0, a0, 1
    slli t0, t0, 12
    add sp, sp, t0
    tail kmh_native_main
1:  wfi
    j 1b
    .section .bss.stack,"aw",@nobits
    .balign 16
    .globl native_stacks
native_stacks:
    .space 64 * 4096
