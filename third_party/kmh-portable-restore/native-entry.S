/* SPDX-License-Identifier: GPL-2.0-or-later */
    .option norvc
    .section .text
    .globl _start
_start:
    csrci mstatus, 8
    csrw mie, zero
    li t0, 0x80300000
    jr t0
    .space 64 - (. - _start)
