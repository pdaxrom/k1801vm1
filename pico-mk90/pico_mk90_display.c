#include "pico_mk90_display.h"

#include "mk90_defs.h"
#include "mk90_machine.h"

#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef PICO_MK90_LCD_X_OFFSET
#define PICO_MK90_LCD_X_OFFSET 4
#endif

#define PIN_BL 9
#define PIN_DC 10
#define PIN_RST 11
#define PIN_CS 13
#define PIN_SCK 14
#define PIN_MOSI 15

#define LCD_WIDTH 128
#define LCD_HEIGHT 64
#define LCD_PAGES (LCD_HEIGHT / 8)

static uint8_t lcd_buffer[LCD_WIDTH * LCD_PAGES];
static bool lcd_ready;

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
    if (!lcd_ready) {
        return;
    }

    for (int p = 0; p < LCD_PAGES; p++) {
        lcd_send_cmd((uint8_t)(0xB0 | p));
        lcd_send_cmd(0x10);
        lcd_send_cmd(0x00);
        lcd_send_data(&lcd_buffer[p * LCD_WIDTH], LCD_WIDTH);
    }
}

static void lcd_set_pixel(int x, int y, bool on)
{
    const uint8_t bit = (uint8_t)(1u << (y & 7));
    uint8_t *cell;

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

bool pico_mk90_display_init(void)
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

    lcd_ready = true;

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

    pico_mk90_display_clear();
    return true;
}

void pico_mk90_display_clear(void)
{
    memset(lcd_buffer, 0, sizeof(lcd_buffer));
    lcd_update_all();
}

void pico_mk90_display_update_from_machine(void)
{
    const word base = mk90_machine_lcd_base();
    word index = 0;

    memset(lcd_buffer, 0, sizeof(lcd_buffer));

    for (int half = 0; half < 2; half++) {
        for (int row = 0; row < 32; row++) {
            const int y = half * 32 + row;
            int x = PICO_MK90_LCD_X_OFFSET;

            for (int col = 0; col < 15; col++) {
                uint8_t value = mk90_machine_ram_peek((word)(base + index));

                for (int bit = 0; bit < 8; bit++) {
                    lcd_set_pixel(x, y, (value & 0200u) != 0u);
                    value <<= 1;
                    x++;
                }
                index = (word)(index + 2u);
            }
        }
        index = (word)(index - (MK90_SCREEN_BYTES - 1u));
    }

    lcd_update_all();
}
