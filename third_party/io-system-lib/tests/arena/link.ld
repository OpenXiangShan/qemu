OUTPUT_ARCH("riscv")
ENTRY(_start)
SECTIONS
{
  . = 0x80000000;
  .text.nh_start : { KEEP(*(.text.nh_start)) }
  . = 0x80001000;
  .text.nh : { *(.text) *(.text.*) EXCLUDE_FILE (*e6.o) *(.rodata*) }
  ASSERT(. <= 0x80010000, "host code overlaps E6")
  . = 0x80010000;
  .e6_start : { KEEP(*(.e6_start)) }
  .e6_text : { *(.e6_text) }
  ASSERT(. <= 0x80020000, "E6 code overlaps matrices")
  . = 0x80020000;
  .matrix_a : { *(.matrix_a) }
  . = 0x80020100;
  .matrix_b : { *(.matrix_b) }
  . = 0x80020200;
  .matrix_c : { *(.matrix_c) }
  . = 0x800202c0;
  .guard_before : { *(.guard_before) }
  . = 0x80020300;
  .matrix_o : { *(.matrix_o) }
  .guard_after : { *(.guard_after) }
  . = 0x80020400;
  .expected_o : { *(.expected_o) }
  . = 0x803e0000;
  .stacks (NOLOAD) : {
    __nh_stack_bottom = .;
    . += 0x1000;
    __nh_stack_top = .;
    __e6_stack_bottom = .;
    . += 0x1000;
    __e6_stack_top = .;
  }
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.riscv.attributes) }
  ASSERT(_start == 0x80000000, "host entry must be 0x80000000")
  ASSERT(e6_start == 0x80010000, "E6 entry must be 0x80010000")
  ASSERT(matrix_o == 0x80020300, "output address changed")
  ASSERT(. <= 0x80400000, "image exceeds 4 MiB")
}
