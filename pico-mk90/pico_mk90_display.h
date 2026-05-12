#ifndef PICO_MK90_DISPLAY_H
#define PICO_MK90_DISPLAY_H

#include <stdbool.h>

bool pico_mk90_display_init(void);
void pico_mk90_display_clear(void);
void pico_mk90_display_update_from_machine(void);

#endif
