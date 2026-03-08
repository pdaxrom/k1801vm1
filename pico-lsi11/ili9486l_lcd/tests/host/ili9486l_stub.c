#include "ili9486l.h"

unsigned ili9486l_stub_begin_write_calls = 0u;
unsigned ili9486l_stub_wire_write_calls = 0u;
unsigned ili9486l_stub_wire_rect_calls = 0u;

void ili9486l_stub_reset_counters(void)
{
    ili9486l_stub_begin_write_calls = 0u;
    ili9486l_stub_wire_write_calls = 0u;
    ili9486l_stub_wire_rect_calls = 0u;
}

bool ili9486l_configure_vertical_scroll(uint16_t top_fixed, uint16_t scroll_area, uint16_t bottom_fixed)
{
    (void)top_fixed;
    (void)scroll_area;
    (void)bottom_fixed;
    return true;
}

bool ili9486l_begin_write(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    ++ili9486l_stub_begin_write_calls;
    return true;
}

bool ili9486l_scroll_vertical_by(int16_t delta)
{
    (void)delta;
    return true;
}

bool ili9486l_set_vertical_scroll_start(uint16_t start)
{
    (void)start;
    return true;
}

uint16_t ili9486l_width(void)
{
    return 480u;
}

uint16_t ili9486l_height(void)
{
    return 320u;
}

void ili9486l_draw_char(uint16_t x, uint16_t y, char c, lcd_color_t fg, lcd_color_t bg, uint8_t scale)
{
    (void)x;
    (void)y;
    (void)c;
    (void)fg;
    (void)bg;
    (void)scale;
}

void ili9486l_draw_pixel(uint16_t x, uint16_t y, lcd_color_t color)
{
    (void)x;
    (void)y;
    (void)color;
}

void ili9486l_draw_rgb666_bitmap(const uint8_t *bitmap, uint16_t w, uint16_t h)
{
    (void)bitmap;
    (void)w;
    (void)h;
}

void ili9486l_draw_rgb666_rect(const uint8_t *bitmap, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    (void)bitmap;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

void ili9486l_draw_rgb666_wire_rect(const uint8_t *bitmap, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    (void)bitmap;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    ++ili9486l_stub_wire_rect_calls;
}

void ili9486l_draw_rgb888_as_rgb666_rect(const uint8_t *bitmap, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    (void)bitmap;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

void ili9486l_draw_string(uint16_t x, uint16_t y, const char *text, lcd_color_t fg, lcd_color_t bg, uint8_t scale)
{
    (void)x;
    (void)y;
    (void)text;
    (void)fg;
    (void)bg;
    (void)scale;
}

void ili9486l_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, lcd_color_t color)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)color;
}

void ili9486l_fill_rect_rgb666(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t red, uint8_t green,
                               uint8_t blue)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)red;
    (void)green;
    (void)blue;
}

void ili9486l_fill_screen(lcd_color_t color)
{
    (void)color;
}

void ili9486l_init(void)
{
}

void ili9486l_reset_vertical_scroll(void)
{
}

void ili9486l_set_rotation(uint8_t rotation)
{
    (void)rotation;
}

void ili9486l_write_rgb666_pixels(const uint8_t *pixels, size_t pixel_count)
{
    (void)pixels;
    (void)pixel_count;
}

void ili9486l_write_rgb666_wire_pixels(const uint8_t *pixels, size_t pixel_count)
{
    (void)pixels;
    (void)pixel_count;
    ++ili9486l_stub_wire_write_calls;
}

void ili9486l_write_rgb888_as_rgb666_pixels(const uint8_t *pixels, size_t pixel_count)
{
    (void)pixels;
    (void)pixel_count;
}
