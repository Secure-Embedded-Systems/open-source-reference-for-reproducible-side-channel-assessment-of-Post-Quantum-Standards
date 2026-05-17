OUTPUT_ARCH("riscv")
ENTRY(_start)

MEMORY
{
    VECTORS (rx)  : ORIGIN = 0x10000000, LENGTH = 0x80
    SRAM    (rwx) : ORIGIN = 0x10000080, LENGTH = 0xFF80
}

SECTIONS
{
    .vectors : { . = ORIGIN(VECTORS); KEEP(*(.vectors)); . = ALIGN(4); } > VECTORS
    .init : { *(.init); *(.init.*); } > SRAM
    .text : { *(.text); *(.text.*); *(.rodata); *(.rodata.*); . = ALIGN(4); } > SRAM
    .srodata : { *(.srodata); *(.srodata.*); . = ALIGN(4); } > SRAM
    .data : { __data_start = .; *(.data); *(.data.*); *(.sdata); *(.sdata.*); . = ALIGN(4); __data_end = .; } > SRAM
    PROVIDE(__global_pointer$ = __data_start + 0x800);
    .bss (NOLOAD) : { __bss_start = .; *(.bss); *(.bss.*); *(.sbss); *(.sbss.*); *(COMMON); . = ALIGN(4); __bss_end = .; } > SRAM
    __stack_top = 0x1000FFF0;
    /DISCARD/ : { *(.comment); *(.note.*); *(.eh_frame*); *(.riscv.attributes); }
}
