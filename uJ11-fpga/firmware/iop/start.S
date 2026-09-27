    .section .text.start,"ax",@progbits
    .globl _start
_start:
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop
    li sp, 2048
    call main
1:  j 1b
