#ifndef PICO_LSI11_DISPLAY_BACKEND_H
#define PICO_LSI11_DISPLAY_BACKEND_H

#include <stdbool.h>

bool display_backend_supported(void);
const char *display_backend_name(void);
bool display_backend_init(void);
void display_backend_set_output_enabled(bool enabled);
bool display_backend_show_boot_logo(void);
void display_backend_task(void);

#endif
