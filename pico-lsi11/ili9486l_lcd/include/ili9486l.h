#ifndef ILI9486L_H
#define ILI9486L_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LCD_NATIVE_WIDTH  320u
#define LCD_NATIVE_HEIGHT 480u

typedef uint32_t lcd_color_t;

#define LCD_RGB666(r, g, b) \
  (lcd_color_t)((((uint32_t)(r) & 0x3Fu) << 12) | (((uint32_t)(g) & 0x3Fu) << 6) | ((uint32_t)(b) & 0x3Fu))

#define LCD_COLOR_BLACK   LCD_RGB666(0x00, 0x00, 0x00)
#define LCD_COLOR_WHITE   LCD_RGB666(0x3F, 0x3F, 0x3F)
#define LCD_COLOR_RED     LCD_RGB666(0x3F, 0x00, 0x00)
#define LCD_COLOR_GREEN   LCD_RGB666(0x00, 0x3F, 0x00)
#define LCD_COLOR_BLUE    LCD_RGB666(0x00, 0x00, 0x3F)
#define LCD_COLOR_CYAN    LCD_RGB666(0x00, 0x3F, 0x3F)
#define LCD_COLOR_MAGENTA LCD_RGB666(0x3F, 0x00, 0x3F)
#define LCD_COLOR_YELLOW  LCD_RGB666(0x3F, 0x3F, 0x00)

void ili9486l_init(void);
void ili9486l_set_rotation(uint8_t rotation);
uint16_t ili9486l_width(void);
uint16_t ili9486l_height(void);
bool ili9486l_configure_vertical_scroll(uint16_t top_fixed, uint16_t scroll_area, uint16_t bottom_fixed);
bool ili9486l_set_vertical_scroll_start(uint16_t start);
bool ili9486l_scroll_vertical_by(int16_t delta);
void ili9486l_reset_vertical_scroll(void);

void ili9486l_draw_pixel(uint16_t x, uint16_t y, lcd_color_t color);
void ili9486l_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, lcd_color_t color);
void ili9486l_fill_rect_rgb666(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t red, uint8_t green,
                               uint8_t blue);
void ili9486l_fill_screen(lcd_color_t color);
bool ili9486l_begin_write(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void ili9486l_write_rgb666_pixels(const uint8_t *pixels, size_t pixel_count);
void ili9486l_write_rgb666_wire_pixels(const uint8_t *pixels, size_t pixel_count);
void ili9486l_write_rgb888_as_rgb666_pixels(const uint8_t *pixels, size_t pixel_count);
void ili9486l_draw_rgb666_rect(const uint8_t *bitmap, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void ili9486l_draw_rgb666_wire_rect(const uint8_t *bitmap, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void ili9486l_draw_rgb666_bitmap(const uint8_t *bitmap, uint16_t w, uint16_t h);
void ili9486l_draw_rgb888_as_rgb666_rect(const uint8_t *bitmap, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void ili9486l_draw_char(uint16_t x, uint16_t y, char c, lcd_color_t fg, lcd_color_t bg, uint8_t scale);
void ili9486l_draw_string(uint16_t x, uint16_t y, const char *text, lcd_color_t fg, lcd_color_t bg, uint8_t scale);

#ifdef __cplusplus
}
#endif

#endif
