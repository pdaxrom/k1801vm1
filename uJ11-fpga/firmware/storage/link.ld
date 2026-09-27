OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY { ram (rwx) : ORIGIN = 0, LENGTH = 8192 }
SECTIONS {
    .text : { KEEP(*(.text.start)) *(.text*) *(.rodata*) } > ram
    .data : { *(.data*) *(.sdata*) } > ram
    .bss (NOLOAD) : {
        . = ALIGN(4); __bss_start = .;
        *(.bss*) *(.sbss*) *(COMMON)
        . = ALIGN(4); __bss_end = .;
    } > ram
    __stack_top = 8192;
    ASSERT(. <= 7168, "Storage IOP overlaps the reserved 1024-byte stack")
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.riscv.attributes) }
}
