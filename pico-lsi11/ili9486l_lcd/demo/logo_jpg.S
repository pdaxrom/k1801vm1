#ifndef LOGO_JPG_PATH
#define LOGO_JPG_PATH "demo/assets/logo.jpg"
#endif

    .section .rodata.logo_jpg,"a",%progbits
    .balign 4
    .global g_logo_jpg_start
    .type g_logo_jpg_start, %object
g_logo_jpg_start:
    .incbin LOGO_JPG_PATH
    .global g_logo_jpg_end
    .type g_logo_jpg_end, %object
g_logo_jpg_end:
    .balign 4
