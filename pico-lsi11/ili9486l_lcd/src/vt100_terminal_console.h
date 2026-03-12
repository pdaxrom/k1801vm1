#ifndef VT100_TERMINAL_CONSOLE_H
#define VT100_TERMINAL_CONSOLE_H

#include "vt100_terminal.h"

bool vt100_terminal_console_attach(vt100_terminal_t *terminal);
bool vt100_terminal_console_is_attached(const vt100_terminal_t *terminal);
bool vt100_terminal_console_getch(vt100_terminal_t *terminal, int ch);
bool vt100_terminal_console_write_n(vt100_terminal_t *terminal, const char *text, size_t len);
void vt100_terminal_console_tick(vt100_terminal_t *terminal, uint32_t elapsed_ms);
void vt100_terminal_console_render(vt100_terminal_t *terminal);

#endif
