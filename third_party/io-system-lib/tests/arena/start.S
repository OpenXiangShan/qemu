/* QEMU main hart entry; E6 has its own reset entry in e6.S. */
  .option norvc
  .option norelax
  .section .text.nh_start, "ax", @progbits
  .globl _start
_start:
  la sp, __nh_stack_top
  csrw mie, zero
  csrci mstatus, 8
  la t0, nh_trap
  csrw mtvec, t0
  call nh_main
1:
  wfi
  j 1b

  .section .text, "ax", @progbits
  .balign 4
nh_trap:
  csrr a0, mcause
  csrr a1, mepc
  csrr a2, mtval
  call nh_trap_report
  j 1b
