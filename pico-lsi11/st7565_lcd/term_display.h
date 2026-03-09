#ifndef PICO_LSI11_ST7565_TERM_DISPLAY_H
#define PICO_LSI11_ST7565_TERM_DISPLAY_H

#include <stdbool.h>

void st7565_term_display_init(void);
void st7565_term_display_clear(void);
void st7565_term_display_putc(char c);
bool st7565_term_display_show_mono_bmp(const char *path);

#endif
