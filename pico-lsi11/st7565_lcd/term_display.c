#include "term_display.h"

#include "font5x7.h"

#include "ff.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define PIN_BL 9
#define PIN_DC 10
#define PIN_RST 11
#define PIN_CS 13
#define PIN_SCK 14
#define PIN_MOSI 15

#define LCD_WIDTH 128
#define LCD_HEIGHT 64
#define LCD_PAGES (LCD_HEIGHT / 8)
#define ST7565_BOOT_BMP_MAX_BYTES 4096u

static uint8_t lcd_buffer[LCD_WIDTH * LCD_PAGES];

#define COLUMNS (LCD_WIDTH / 6)
#define ROWS (LCD_HEIGHT / 8)

static int cursor_x = 0;
static int cursor_y = 0;
static int cursor_visible = 0;

static void draw_cursor(int x, int y, int show);

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static void lcd_send_cmd(uint8_t cmd)
{
    gpio_put(PIN_CS, 0);
    gpio_put(PIN_DC, 0);
    spi_write_blocking(spi1, &cmd, 1);
    gpio_put(PIN_CS, 1);
}

static void lcd_send_data(const uint8_t *data, size_t len)
{
    gpio_put(PIN_CS, 0);
    gpio_put(PIN_DC, 1);
    spi_write_blocking(spi1, data, len);
    gpio_put(PIN_CS, 1);
}

static void lcd_update_all(void)
{
    for (int p = 0; p < LCD_PAGES; p++) {
        lcd_send_cmd(0xB0 | p);
        lcd_send_cmd(0x10);
        lcd_send_cmd(0x00);
        lcd_send_data(&lcd_buffer[p * LCD_WIDTH], LCD_WIDTH);
    }
}

static void lcd_update_char(int x, int y)
{
    const int p = y;
    const int col = x * 6;

    lcd_send_cmd(0xB0 | p);
    lcd_send_cmd(0x10 | ((col >> 4) & 0x0F));
    lcd_send_cmd(0x00 | (col & 0x0F));
    lcd_send_data(&lcd_buffer[p * LCD_WIDTH + col], 6);
}

static void lcd_clear_buffer(void)
{
    memset(lcd_buffer, 0, sizeof(lcd_buffer));
}

static void lcd_set_pixel(int x, int y, bool on)
{
    uint8_t *cell = NULL;
    const uint8_t bit = (uint8_t)(1u << (y & 7));

    if (x < 0 || x >= LCD_WIDTH || y < 0 || y >= LCD_HEIGHT) {
        return;
    }

    cell = &lcd_buffer[(y >> 3) * LCD_WIDTH + x];
    if (on) {
        *cell |= bit;
    } else {
        *cell &= (uint8_t)~bit;
    }
}

static void terminal_reset(int show_cursor)
{
    cursor_x = 0;
    cursor_y = 0;
    cursor_visible = 0;
    if (show_cursor) {
        draw_cursor(cursor_x, cursor_y, 1);
    }
}

static void terminal_scroll(void)
{
    memmove(lcd_buffer, lcd_buffer + LCD_WIDTH, LCD_WIDTH * (LCD_PAGES - 1));
    memset(lcd_buffer + LCD_WIDTH * (LCD_PAGES - 1), 0, LCD_WIDTH);
    lcd_update_all();
}

static void draw_cursor(int x, int y, int show)
{
    const int p = y;
    const int col = x * 6;

    if (cursor_visible == show) {
        return;
    }

    for (int i = 0; i < 5; i++) {
        lcd_buffer[p * LCD_WIDTH + col + i] ^= 0x7F;
    }
    lcd_update_char(x, y);
    cursor_visible = show;
}

static void terminal_putchar(char c)
{
    draw_cursor(cursor_x, cursor_y, 0);

    if (c == '\r') {
        cursor_x = 0;
        draw_cursor(cursor_x, cursor_y, 1);
        return;
    }
    if (c == '\n') {
        cursor_y++;
        cursor_x = 0;
        if (cursor_y >= ROWS) {
            cursor_y = ROWS - 1;
            terminal_scroll();
        }
        draw_cursor(cursor_x, cursor_y, 1);
        return;
    }
    if (c == '\b' || c == 0x7F) {
        if (cursor_x > 0) {
            cursor_x--;
            for (int i = 0; i < 6; i++) {
                lcd_buffer[cursor_y * LCD_WIDTH + cursor_x * 6 + i] = 0;
            }
            cursor_visible = 0;
            lcd_update_char(cursor_x, cursor_y);
        }
        draw_cursor(cursor_x, cursor_y, 1);
        return;
    }
    if (c < 32 || c > 127) {
        draw_cursor(cursor_x, cursor_y, 1);
        return;
    }

    {
        const int idx = c - 32;
        const int p = cursor_y;
        const int col = cursor_x * 6;
        const uint8_t *glyph = font5x7[idx];

        for (int i = 0; i < 5; i++) {
            lcd_buffer[p * LCD_WIDTH + col + i] = glyph[i];
        }
        lcd_buffer[p * LCD_WIDTH + col + 5] = 0;
    }
    cursor_visible = 0;
    lcd_update_char(cursor_x, cursor_y);

    cursor_x++;
    if (cursor_x >= COLUMNS) {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= ROWS) {
            cursor_y = ROWS - 1;
            terminal_scroll();
        }
    }

    draw_cursor(cursor_x, cursor_y, 1);
}

void st7565_term_display_init(void)
{
    spi_init(spi1, 4000000);

    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    gpio_init(PIN_BL);
    gpio_set_dir(PIN_BL, GPIO_OUT);
    gpio_put(PIN_BL, 1);

    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);

    gpio_init(PIN_DC);
    gpio_set_dir(PIN_DC, GPIO_OUT);
    gpio_put(PIN_DC, 0);

    gpio_init(PIN_RST);
    gpio_set_dir(PIN_RST, GPIO_OUT);

    gpio_put(PIN_RST, 1);
    sleep_ms(1);
    gpio_put(PIN_RST, 0);
    sleep_ms(1);
    gpio_put(PIN_RST, 1);
    sleep_ms(1);

    lcd_send_cmd(0xE2);
    sleep_ms(50);
    lcd_send_cmd(0xAE);
    sleep_ms(50);
    lcd_send_cmd(0x40);
    sleep_ms(50);
    lcd_send_cmd(0xA0);
    sleep_ms(50);
    lcd_send_cmd(0xC8);
    sleep_ms(50);
    lcd_send_cmd(0xA6);
    sleep_ms(50);
    lcd_send_cmd(0xA2);
    sleep_ms(50);
    lcd_send_cmd(0x2F);
    sleep_ms(50);
    lcd_send_cmd(0xF8);
    sleep_ms(50);
    lcd_send_cmd(0x00);
    sleep_ms(50);
    lcd_send_cmd(0x25);
    sleep_ms(50);
    lcd_send_cmd(0x81);
    sleep_ms(50);
    lcd_send_cmd(0x22);
    sleep_ms(50);
    lcd_send_cmd(0xAF);
    sleep_ms(50);

    lcd_clear_buffer();
    lcd_update_all();
    terminal_reset(1);
}

void st7565_term_display_clear(void)
{
    lcd_clear_buffer();
    lcd_update_all();
    terminal_reset(1);
}

void st7565_term_display_putc(char c)
{
    terminal_putchar(c);
}

bool st7565_term_display_show_mono_bmp(const char *path)
{
    static uint8_t file_data[ST7565_BOOT_BMP_MAX_BYTES];
    FIL file;
    UINT br = 0;
    FRESULT fr;
    FSIZE_t file_size = 0;
    const uint8_t *pixel_data = NULL;
    const uint8_t *palette = NULL;
    uint32_t data_offset = 0;
    uint32_t dib_header_size = 0;
    int32_t width = 0;
    int32_t height_raw = 0;
    int32_t height = 0;
    uint16_t planes = 0;
    uint16_t bits_per_pixel = 0;
    uint32_t compression = 0;
    uint32_t row_stride = 0;
    bool top_down = false;
    bool bit_one_is_lit = true;

    if (!path) {
        return false;
    }

    fr = f_open(&file, path, FA_READ);
    if (fr != FR_OK) {
        return false;
    }

    file_size = f_size(&file);
    if (file_size < 62u || file_size > ST7565_BOOT_BMP_MAX_BYTES) {
        (void)f_close(&file);
        return false;
    }

    fr = f_read(&file, file_data, (UINT)file_size, &br);
    (void)f_close(&file);
    if (fr != FR_OK || br != (UINT)file_size) {
        return false;
    }

    if (file_data[0] != 'B' || file_data[1] != 'M') {
        return false;
    }

    data_offset = read_le32(&file_data[10]);
    dib_header_size = read_le32(&file_data[14]);
    if (dib_header_size < 40u || data_offset >= (uint32_t)file_size ||
            14u + dib_header_size > (uint32_t)file_size) {
        return false;
    }

    width = (int32_t)read_le32(&file_data[18]);
    height_raw = (int32_t)read_le32(&file_data[22]);
    height = (height_raw < 0) ? -height_raw : height_raw;
    planes = read_le16(&file_data[26]);
    bits_per_pixel = read_le16(&file_data[28]);
    compression = read_le32(&file_data[30]);
    row_stride = ((uint32_t)LCD_WIDTH + 31u) / 32u;
    row_stride *= 4u;
    if (width != LCD_WIDTH || height != LCD_HEIGHT || planes != 1u ||
            bits_per_pixel != 1u || compression != 0u) {
        return false;
    }
    if (data_offset + row_stride * LCD_HEIGHT > (uint32_t)file_size) {
        return false;
    }

    palette = &file_data[14u + dib_header_size];
    if ((14u + dib_header_size + 8u) <= data_offset) {
        const uint16_t dark = (uint16_t)palette[0] + (uint16_t)palette[1] + (uint16_t)palette[2];
        const uint16_t light = (uint16_t)palette[4] + (uint16_t)palette[5] + (uint16_t)palette[6];

        bit_one_is_lit = light >= dark;
    }

    top_down = height_raw < 0;
    pixel_data = &file_data[data_offset];
    lcd_clear_buffer();
    for (int y = 0; y < LCD_HEIGHT; ++y) {
        const int src_y = top_down ? y : (LCD_HEIGHT - 1 - y);
        const uint8_t *row = pixel_data + (size_t)src_y * row_stride;

        for (int x = 0; x < LCD_WIDTH; ++x) {
            const uint8_t byte = row[x >> 3];
            const bool bit = ((byte >> (7 - (x & 7))) & 1u) != 0u;
            const bool on = bit ? bit_one_is_lit : !bit_one_is_lit;

            lcd_set_pixel(x, y, on);
        }
    }

    lcd_update_all();
    cursor_x = 0;
    cursor_y = 0;
    cursor_visible = 0;
    return true;
}
