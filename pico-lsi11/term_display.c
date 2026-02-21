#include "term_display.h"
#include "font5x7.h"

#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdio/driver.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

// Pin definitions
#define PIN_BL 9
#define PIN_DC 10
#define PIN_RST 11
#define PIN_CS 13
#define PIN_SCK 14
#define PIN_MOSI 15

// ST7565 Config
#define LCD_WIDTH 128
#define LCD_HEIGHT 64
#define LCD_PAGES (LCD_HEIGHT / 8)

static uint8_t lcd_buffer[LCD_WIDTH * LCD_PAGES];

#define COLUMNS (LCD_WIDTH / 6)
#define ROWS (LCD_HEIGHT / 8)

static int cursor_x = 0;
static int cursor_y = 0;

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
        lcd_send_cmd(0xB0 | p); // Set page address
        lcd_send_cmd(0x10);     // Set col address upper 4 bits
        lcd_send_cmd(0x00);     // Set col address lower 4 bits
        lcd_send_data(&lcd_buffer[p * LCD_WIDTH], LCD_WIDTH);
    }
}

static void lcd_update_char(int x, int y)
{
    int p = y;
    int col = x * 6;
    lcd_send_cmd(0xB0 | p);
    lcd_send_cmd(0x10 | ((col >> 4) & 0x0F));
    lcd_send_cmd(0x00 | (col & 0x0F));
    lcd_send_data(&lcd_buffer[p * LCD_WIDTH + col], 6);
}

static void terminal_scroll(void)
{
    // move rows 1-7 up to 0-6
    memmove(lcd_buffer, lcd_buffer + LCD_WIDTH, LCD_WIDTH * (LCD_PAGES - 1));
    // clear last row
    memset(lcd_buffer + LCD_WIDTH * (LCD_PAGES - 1), 0, LCD_WIDTH);
    lcd_update_all();
}

static int cursor_visible = 0;

static void draw_cursor(int x, int y, int show)
{
    if (cursor_visible == show) {
        return;
    }

    int p = y;
    int col = x * 6;
    for (int i = 0; i < 5; i++) {
        lcd_buffer[p * LCD_WIDTH + col + i] ^= 0x7F;
    }
    lcd_update_char(x, y);
    cursor_visible = show;
}

static void terminal_putchar(char c)
{
    draw_cursor(cursor_x, cursor_y, 0); // Clear cursor before moving

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
            // Erase char
            int p = cursor_y;
            int col = cursor_x * 6;
            for (int i = 0; i < 6; i++) {
                lcd_buffer[p * LCD_WIDTH + col + i] = 0;
            }
            cursor_visible = 0; // Buffer modified, cursor effectively erased
            lcd_update_char(cursor_x, cursor_y);
        }
        draw_cursor(cursor_x, cursor_y, 1);
        return;
    }
    if (c < 32 || c > 127) {
        draw_cursor(cursor_x, cursor_y, 1);
        return; // Ignore non-printable
    }

    int idx = c - 32;
    int p = cursor_y;
    int col = cursor_x * 6;
    const uint8_t *glyph = font5x7[idx];

    for (int i = 0; i < 5; i++) {
        lcd_buffer[p * LCD_WIDTH + col + i] = glyph[i];
    }
    lcd_buffer[p * LCD_WIDTH + col + 5] = 0;
    cursor_visible = 0; // Buffer modified, cursor effectively erased
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

// Stdio driver wrapper
static void stdio_term_out_chars(const char *buf, int len)
{
    for (int i = 0; i < len; i++) {
        terminal_putchar(buf[i]);
    }
}

static stdio_driver_t term_stdio_app = {.out_chars = stdio_term_out_chars,
                                        .out_flush = NULL,
                                        .in_chars = NULL,
#if PICO_SDK_VERSION_MAJOR >= 1 && PICO_SDK_VERSION_MINOR >= 4
                                        .next = NULL
#endif
                                       };

void term_display_init(void)
{
    // SPI init
    spi_init(spi1, 4000000); // 4 MHz

    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    gpio_init(PIN_BL);
    gpio_set_dir(PIN_BL, GPIO_OUT);
    gpio_put(PIN_BL, 1); // Backlight on

    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);

    gpio_init(PIN_DC);
    gpio_set_dir(PIN_DC, GPIO_OUT);
    gpio_put(PIN_DC, 0);

    gpio_init(PIN_RST);
    gpio_set_dir(PIN_RST, GPIO_OUT);

    // Reset sequence
    gpio_put(PIN_RST, 1);
    sleep_ms(1);
    gpio_put(PIN_RST, 0);
    sleep_ms(1);
    gpio_put(PIN_RST, 1);
    sleep_ms(1);

    lcd_send_cmd(0xE2); // Internal reset
    sleep_ms(50);
    lcd_send_cmd(0xAE); // Display OFF
    sleep_ms(50);
    lcd_send_cmd(0x40); // Set display start line to 0
    sleep_ms(50);
    lcd_send_cmd(0xA0); // ADC set to normal
    sleep_ms(50);
    lcd_send_cmd(0xC8); // Common output mode reverse
    sleep_ms(50);
    lcd_send_cmd(0xA6); // Normal display
    sleep_ms(50);
    lcd_send_cmd(0xA2); // LCD bias 1/9
    sleep_ms(50);
    lcd_send_cmd(0x2F); // Power control: Booster, Regulator, Follower ON
    sleep_ms(50);
    lcd_send_cmd(0xF8); // Booster ratio
    sleep_ms(50);
    lcd_send_cmd(0x00); // 4x
    sleep_ms(50);
    lcd_send_cmd(0x25); // V0 voltage resistor ratio to max
    sleep_ms(50);
    lcd_send_cmd(0x81); // set contrast
    sleep_ms(50);
    lcd_send_cmd(0x22); // contrast value
    sleep_ms(50);
    lcd_send_cmd(0xAF); // enable display
    sleep_ms(50);

    memset(lcd_buffer, 0, sizeof(lcd_buffer));
    lcd_update_all();
    draw_cursor(cursor_x, cursor_y, 1);

    // Register stdio filter driver
    stdio_set_driver_enabled(&term_stdio_app, true);
}
