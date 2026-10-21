OUTPUT_ARCH(riscv)
ENTRY(_native_start)
SECTIONS
{
    . = 0x80300000;
    .text : { *(.text.entry) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) *(.sdata*) }
    . = ALIGN(16);
    .bss (NOLOAD) : { *(.bss*) *(.sbss*) *(COMMON) }
    _end = .;
    ASSERT(_end <= 0x80400000, "native checkpoint work area overflow")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
