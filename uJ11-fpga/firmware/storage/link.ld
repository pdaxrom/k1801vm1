OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY { ram (rwx) : ORIGIN = 0, LENGTH = 12288 }
SECTIONS {
    .text : { KEEP(*(.text.start)) *(.text*) *(.rodata*) } > ram
    .data : { *(.data*) *(.sdata*) } > ram
    .bss (NOLOAD) : {
        . = ALIGN(4); __bss_start = .;
        *(.bss*) *(.sbss*) *(COMMON)
        . = ALIGN(4); __bss_end = .;
    } > ram
    __stack_top = 12288;
    __stack_bottom = __stack_top - 768;
    ASSERT(. <= __stack_bottom, "Storage IOP overlaps the reserved 768-byte stack")
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.riscv.attributes) }
}
