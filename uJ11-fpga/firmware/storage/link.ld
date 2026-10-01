OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY {
    ram (rwx) : ORIGIN = 0, LENGTH = 18432
    cold (rw) : ORIGIN = 18432, LENGTH = 512
    sector (rw) : ORIGIN = 18944, LENGTH = 512
}
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
    __stack_top = ORIGIN(ram) + LENGTH(ram);
    __stack_bottom = __stack_top - 640;
    /* Optional zero-initialized cold data in the serialized 16-bit tail.
     * Keep all ordinary BSS and the entire stack in full-width banks. */
    .cold_bss (NOLOAD) : {
        __cold_start = .;
        *(.cold_bss*)
        . = ALIGN(4); __cold_end = .;
    } > cold
    __cold_limit = ORIGIN(cold) + LENGTH(cold);
    /* The autonomous SD/DMA engine owns the last 512 bytes of shared EBR. */
    .sector (NOLOAD) : {
        __sector_start = .;
        . += LENGTH(sector);
        __sector_end = .;
    } > sector
    ASSERT(__bss_end <= __stack_bottom, "Storage IOP overlaps the reserved 640-byte stack")
    ASSERT(__cold_end <= __cold_limit, "Storage IOP exceeds its 512-byte cold-data bank")
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.riscv.attributes) }
}
