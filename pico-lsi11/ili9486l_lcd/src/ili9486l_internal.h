#ifndef ILI9486L_INTERNAL_H
#define ILI9486L_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

void ili9486l_write_rgb666_wire_pixels_async(const uint8_t *pixels, size_t pixel_count);
void ili9486l_wait_for_pending_write(void);

#endif
