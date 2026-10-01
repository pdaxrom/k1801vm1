    .section .text.start,"ax",@progbits
    .globl _start
_start:
    /* Set gp before any relaxable address, including the BSS bounds. */
    .option push
    .option norelax
    la gp, __global_pointer$
    /* The additional KiB can put SP outside the signed gp-relative range. */
    la sp, __stack_top
    .option pop
    la t0, __bss_start
    la t1, __bss_end
1:  bgeu t0, t1, 2f
    sw zero, 0(t0)
    addi t0, t0, 4
    j 1b
2:  la t0, __cold_start
    la t1, __cold_end
4:  bgeu t0, t1, 5f
    sw zero, 0(t0)
    addi t0, t0, 4
    j 4b
5:  call main
3:  j 3b
