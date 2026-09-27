OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY { ram (rwx) : ORIGIN = 0, LENGTH = 2048 }
SECTIONS {
    .text : { KEEP(*(.text.start)) *(.text*) *(.rodata*) } > ram
    .data : { *(.data*) *(.sdata*) } > ram
    PROVIDE(__global_pointer$ = 2048);
    .bss (NOLOAD) : { *(.bss*) *(.sbss*) *(COMMON) } > ram
    ASSERT(SIZEOF(.bss) == 0, "IOP firmware must not depend on retained BSS")
    ASSERT(. <= 1536, "IOP image overlaps the reserved 512-byte stack")
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.riscv.attributes) }
}
