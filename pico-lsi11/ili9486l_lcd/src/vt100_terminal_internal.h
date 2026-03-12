#ifndef VT100_TERMINAL_INTERNAL_H
#define VT100_TERMINAL_INTERNAL_H

#include "vt100_terminal.h"

void vt100_terminal_core_render(vt100_terminal_t *terminal);
void vt100_terminal_core_putc(vt100_terminal_t *terminal, char ch);
void vt100_terminal_core_tick(vt100_terminal_t *terminal, uint32_t elapsed_ms);

#endif
