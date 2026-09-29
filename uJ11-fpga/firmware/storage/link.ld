OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY { ram (rwx) : ORIGIN = 0, LENGTH = 12288 }
SECTIONS {
    .text : { KEEP(*(.text.start)) *(.text*) *(.rodata*) *(.srodata*) } > ram
    .data : { *(.data*) *(.sdata*) } > ram
    .bss (NOLOAD) : {
        . = ALIGN(4); __bss_start = .;
        *(.bss*) *(.sbss*) *(COMMON)
        . = ALIGN(4); __bss_end = .;
    } > ram
    /* Both the constant tables before BSS and all live data fit +/-2 KiB. */
    __global_pointer$ = __bss_start;
    __stack_top = 12288;
    __stack_bottom = __stack_top - 640;
    ASSERT(. <= __stack_bottom, "Storage IOP overlaps the reserved 640-byte stack")
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.riscv.attributes) }
}
