/* AME sequence derived from nanhu-fpga-aps nh_e6_ame_gemm.S. */
  .option norvc
  .option norelax
  .equ E6_PHASE, 0x50000160
  .equ E6_DONE, 0x50000120
  .equ NH_RESULT, 0x50000140
#ifndef E6_ILLEGAL
#define E6_ILLEGAL 0
#endif
#ifndef TEST_CASE
#define TEST_CASE 0
#endif
  .section .e6_start, "ax", @progbits
  .globl e6_start
  .type e6_start, @function
e6_start:
  la sp, __e6_stack_top
  li t0, (3 << 15)
  csrs mstatus, t0
  la t0, e6_trap
  csrw mtvec, t0
  j e6_main
  .size e6_start, . - e6_start

  .section .e6_text, "ax", @progbits
  .type e6_main, @function
e6_main:
#if E6_ILLEGAL
  .word 0xffffffff
#endif
  li a0, 'S'
  call e6_phase_write
  li a0, 2
  .word 0x04055077               # msettilem x0,a0
  li a0, 3
  .word 0x04054077               # msettilen x0,a0
  li a0, 4
  .word 0x04056077               # msettilek x0,a0
  li a0, 'T'
  call e6_phase_write

  la a0, matrix_a
  li a1, 4
  .word 0x04b50077               # mlae8.m Tr0,(a0),a1
  li a0, 'A'
  call e6_phase_write
  la a0, matrix_b
  li a1, 4
  .word 0x08b500f7               # mlbe8.m Tr1,(a0),a1
  li a0, 'B'
  call e6_phase_write
  la a0, matrix_c
  li a1, 12
  .word 0x00b52077               # mlce32.m Acc0,(a0),a1
  li a0, 'C'
  call e6_phase_write
  .word 0x28180877               # mqma.b.mm Acc0,Tr0,Tr1
  li a0, 'M'
  call e6_phase_write
  la a0, matrix_o
  li a1, 12
  .word 0x02b52077               # msce32.m Acc0,(a0),a1
  li a0, 'O'
  call e6_phase_write

  fence rw, rw
  li a4, E6_DONE
  li t0, 1
  sw t0, 0(a4)
  fence rw, rw
  li a4, NH_RESULT
1:
  fence r, r
  lw a0, 0(a4)
  beqz a0, 1b
  call e6_phase_write
2:
  wfi
  j 2b
  .size e6_main, . - e6_main

  .type e6_phase_write, @function
e6_phase_write:
  li t0, E6_PHASE
  sw a0, 0(t0)
  fence rw, rw
  ret
  .size e6_phase_write, . - e6_phase_write

  .type e6_trap, @function
e6_trap:
  csrr a0, mcause
  li t1, 0xff
  and a0, a0, t1
  li t1, 0xe000
  or a0, a0, t1
  call e6_phase_write
  li t0, E6_DONE
  li t1, 0xee
  sw t1, 0(t0)
  fence rw, rw
1:
  wfi
  j 1b
  .size e6_trap, . - e6_trap


  .section .matrix_a, "a", @progbits
matrix_a:
#if TEST_CASE == 0
  .byte 1, -2, 3, 4, -3, 5, 2, -1
#else
  .byte 2, 1, -3, 0, 4, -2, 1, 5
#endif
  .section .matrix_b, "a", @progbits
matrix_b:
#if TEST_CASE == 0
  .byte 2, 1, -1, 3, -4, 2, 1, -2, 3, -1, 2, 1
#else
  .byte 1, -1, 2, 0, 3, 2, 0, -1, -2, 1, 4, 2
#endif
  .section .matrix_c, "a", @progbits
matrix_c:
#if TEST_CASE == 0
  .word 7, -5, 11, 4, 9, -6
#else
  .word 3, -7, 5, -1, 6, 2
#endif
  .section .guard_before, "a", @progbits
  .globl guard_before
guard_before:
  .fill 64, 1, 0xa5
  .section .matrix_o, "aw", @progbits
  .globl matrix_o
matrix_o:
  .fill 6, 4, 0x5a5a5a5a
  .section .guard_after, "a", @progbits
  .globl guard_after
guard_after:
  .fill 64, 1, 0xa5
  .section .expected_o, "a", @progbits
  .globl expected_o
expected_o:
#if TEST_CASE == 0
  .word 16, -18, 26, -2, 35, -17
#else
  .word -2, 1, -10, 7, 9, 6
#endif
