ENTRY(_start)
OUTPUT_FORMAT("binary")
SECTIONS
{
    . = 0x10000;
    .text   : { entry.o(.text) *(.text*) }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { __bss_start = .; *(.bss*) *(COMMON) __bss_end = .; }
}
