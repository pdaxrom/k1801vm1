#ifndef ILI9486L_JPEG_H
#define ILI9486L_JPEG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t width;
    uint16_t height;
} ili9486l_jpeg_info_t;

bool ili9486l_jpeg_get_info(const uint8_t *jpeg_data, size_t jpeg_size, ili9486l_jpeg_info_t *out_info);
bool ili9486l_jpeg_draw(const uint8_t *jpeg_data, size_t jpeg_size, uint16_t x, uint16_t y);

#ifdef __cplusplus
}
#endif

#endif
