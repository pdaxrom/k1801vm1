#include "ili9486l.h"
#include "ili9486l_jpeg.h"
#include "vt100_terminal.h"

#include "pico/stdlib.h"

#include <stdio.h>

extern const uint8_t g_logo_jpg_start[];
extern const uint8_t g_logo_jpg_end[];

static void terminal_stdio_output(const char *data, size_t len, void *user_data)
{
    (void)user_data;

    for (size_t i = 0; i < len; ++i) {
        putchar_raw((uint8_t)data[i]);
    }
}

static void show_boot_logo(void)
{
    const size_t logo_size = (size_t)(g_logo_jpg_end - g_logo_jpg_start);

    if (!ili9486l_jpeg_draw(g_logo_jpg_start, logo_size, 0u, 0u)) {
        ili9486l_fill_screen(LCD_COLOR_BLACK);
        return;
    }

    sleep_ms(3000);
}

static void draw_native_scroll_demo_screen(uint16_t top_fixed, uint16_t bottom_fixed)
{
    static const uint8_t band_colors[][3] = {
        {63u, 10u, 10u},
        {63u, 24u, 8u},
        {63u, 40u, 8u},
        {48u, 56u, 8u},
        {20u, 63u, 16u},
        {8u, 63u, 40u},
        {8u, 52u, 63u},
        {20u, 28u, 63u},
        {40u, 12u, 63u},
        {56u, 8u, 44u},
        {63u, 8u, 24u},
        {36u, 36u, 36u},
    };
    const uint16_t scroll_area = (uint16_t)(ili9486l_height() - top_fixed - bottom_fixed);
    const uint16_t band_height = 24u;
    const lcd_color_t header_bg = LCD_RGB666(0x03, 0x05, 0x0B);
    const lcd_color_t footer_bg = LCD_RGB666(0x08, 0x03, 0x03);
    char label[24];

    ili9486l_fill_screen(LCD_COLOR_BLACK);

    ili9486l_fill_rect(0, 0, ili9486l_width(), top_fixed, header_bg);
    ili9486l_draw_string(16, 6, "NATIVE SCROLL AXIS", LCD_COLOR_WHITE, header_bg, 2);

    for (uint16_t y = 0, band = 0; y < scroll_area; y = (uint16_t)(y + band_height), ++band) {
        const uint16_t actual_y = (uint16_t)(top_fixed + y);
        const uint16_t height = (uint16_t)((y + band_height <= scroll_area) ? band_height : (scroll_area - y));
        const uint8_t *color = band_colors[band % (sizeof(band_colors) / sizeof(band_colors[0]))];
        const lcd_color_t bg = LCD_RGB666(color[0], color[1], color[2]);

        ili9486l_fill_rect(0, actual_y, ili9486l_width(), height, bg);
        snprintf(label, sizeof(label), "ROW %03u", (unsigned)band);
        ili9486l_draw_string(16, (uint16_t)(actual_y + 6), label, LCD_COLOR_BLACK, bg, 2);
    }

    ili9486l_fill_rect(0, (uint16_t)(ili9486l_height() - bottom_fixed), ili9486l_width(), bottom_fixed, footer_bg);
    ili9486l_draw_string(16, (uint16_t)(ili9486l_height() - bottom_fixed + 6), "LANDSCAPE => HORIZONTAL", LCD_COLOR_WHITE,
                         footer_bg, 2);
}

static void show_native_scroll_demo(void)
{
    const uint16_t top_fixed = 28u;
    const uint16_t bottom_fixed = 28u;
    const uint16_t scroll_area = (uint16_t)(ili9486l_height() - top_fixed - bottom_fixed);

    draw_native_scroll_demo_screen(top_fixed, bottom_fixed);

    if (!ili9486l_configure_vertical_scroll(top_fixed, scroll_area, bottom_fixed)) {
        return;
    }

    sleep_ms(500);
    (void)ili9486l_set_vertical_scroll_start((uint16_t)(scroll_area / 4u));
    sleep_ms(500);
    (void)ili9486l_set_vertical_scroll_start((uint16_t)(scroll_area / 2u));
    sleep_ms(500);
    (void)ili9486l_set_vertical_scroll_start((uint16_t)((scroll_area * 3u) / 4u));
    sleep_ms(500);

    for (uint16_t offset = 0; offset < scroll_area; ++offset) {
        (void)ili9486l_set_vertical_scroll_start(offset);
        sleep_ms(6);
    }

    sleep_ms(200);
    (void)ili9486l_set_vertical_scroll_start(0);
    sleep_ms(500);
    ili9486l_reset_vertical_scroll();
}

static void draw_rgb_gradient_strip(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    static const uint8_t stops[][3] = {
        {63u, 0u, 0u},
        {63u, 32u, 0u},
        {63u, 63u, 0u},
        {0u, 63u, 0u},
        {0u, 63u, 63u},
        {0u, 0u, 63u},
        {44u, 0u, 63u},
    };
    const uint16_t max_phase = (uint16_t)((sizeof(stops) / sizeof(stops[0]) - 1u) * 255u);

    if (w == 0 || h == 0) {
        return;
    }

    for (uint16_t i = 0; i < w; ++i) {
        uint16_t segment = 0;
        uint16_t mix = 0;
        uint8_t red;
        uint8_t green;
        uint8_t blue;

        if (w > 1) {
            const uint16_t phase = (uint16_t)(((uint32_t)i * max_phase) / (uint32_t)(w - 1u));

            segment = (uint16_t)(phase / 255u);
            mix = (uint16_t)(phase % 255u);

            if (segment >= (sizeof(stops) / sizeof(stops[0]) - 1u)) {
                segment = (uint16_t)(sizeof(stops) / sizeof(stops[0]) - 2u);
                mix = 255u;
            }
        }

        red = (uint8_t)(((uint16_t)stops[segment][0] * (255u - mix) + (uint16_t)stops[segment + 1u][0] * mix) / 255u);
        green = (uint8_t)(((uint16_t)stops[segment][1] * (255u - mix) + (uint16_t)stops[segment + 1u][1] * mix) / 255u);
        blue = (uint8_t)(((uint16_t)stops[segment][2] * (255u - mix) + (uint16_t)stops[segment + 1u][2] * mix) / 255u);

        ili9486l_fill_rect_rgb666((uint16_t)(x + i), y, 1, h, red, green, blue);
    }
}

static void draw_channel_gradient_strip(uint16_t x, uint16_t y, uint16_t w, uint16_t h, char channel)
{
    if (w == 0 || h == 0) {
        return;
    }

    for (uint16_t i = 0; i < w; ++i) {
        uint8_t level = 0;
        uint8_t r6 = 0;
        uint8_t g6 = 0;
        uint8_t b6 = 0;

        if (w > 1) {
            level = (uint8_t)(((uint32_t)i * 63u) / (uint32_t)(w - 1u));
        }

        switch (channel) {
        case 'R':
            r6 = level;
            break;
        case 'G':
            g6 = level;
            break;
        case 'B':
            b6 = level;
            break;
        }

        ili9486l_fill_rect_rgb666((uint16_t)(x + i), y, 1, h, r6, g6, b6);
    }
}

static void show_color_test_screen(void)
{
    const lcd_color_t background = LCD_RGB666(0x02, 0x04, 0x05);
    const lcd_color_t title_bg = LCD_RGB666(0x04, 0x06, 0x08);
    const lcd_color_t frame = LCD_RGB666(0x00, 0x01, 0x02);
    const lcd_color_t red_label = LCD_RGB666(0x3F, 0x1C, 0x1C);
    const lcd_color_t green_label = LCD_RGB666(0x1C, 0x3F, 0x24);
    const lcd_color_t blue_label = LCD_RGB666(0x1C, 0x28, 0x3F);

    ili9486l_fill_screen(background);
    ili9486l_fill_rect(10, 10, 460, 34, title_bg);
    ili9486l_draw_string(22, 19, "RGB CHANNEL TEST 0..63", LCD_COLOR_WHITE, title_bg, 2);

    ili9486l_fill_rect(10, 52, 460, 254, frame);

    ili9486l_draw_string(24, 70, "R", red_label, frame, 3);
    ili9486l_draw_string(64, 76, "0", LCD_COLOR_WHITE, frame, 2);
    ili9486l_fill_rect(92, 78, 340, 26, LCD_COLOR_BLACK);
    draw_channel_gradient_strip(96, 82, 332, 18, 'R');
    ili9486l_draw_string(438, 76, "63", LCD_COLOR_WHITE, frame, 2);

    ili9486l_draw_string(24, 142, "G", green_label, frame, 3);
    ili9486l_draw_string(64, 148, "0", LCD_COLOR_WHITE, frame, 2);
    ili9486l_fill_rect(92, 150, 340, 26, LCD_COLOR_BLACK);
    draw_channel_gradient_strip(96, 154, 332, 18, 'G');
    ili9486l_draw_string(438, 148, "63", LCD_COLOR_WHITE, frame, 2);

    ili9486l_draw_string(24, 214, "B", blue_label, frame, 3);
    ili9486l_draw_string(64, 220, "0", LCD_COLOR_WHITE, frame, 2);
    ili9486l_fill_rect(92, 222, 340, 26, LCD_COLOR_BLACK);
    draw_channel_gradient_strip(96, 226, 332, 18, 'B');
    ili9486l_draw_string(438, 220, "63", LCD_COLOR_WHITE, frame, 2);

    ili9486l_draw_string(24, 274, "SPECTRUM", LCD_COLOR_WHITE, frame, 1);
    ili9486l_fill_rect(92, 274, 340, 20, LCD_COLOR_BLACK);
    draw_rgb_gradient_strip(96, 278, 332, 12);
    sleep_ms(3000);
}

static void draw_demo_screen(void)
{
    const lcd_color_t background = LCD_RGB666(0x02, 0x04, 0x06);
    const lcd_color_t panel = LCD_RGB666(0x04, 0x08, 0x0A);
    const lcd_color_t accent = LCD_RGB666(0x0C, 0x05, 0x00);
    const lcd_color_t strip_frame = LCD_RGB666(0x01, 0x02, 0x03);

    ili9486l_fill_screen(background);

    ili9486l_fill_rect(10, 10, 460, 68, accent);
    ili9486l_fill_rect(10, 92, 220, 218, panel);
    ili9486l_fill_rect(250, 92, 220, 218, panel);

    ili9486l_draw_string(22, 22, "RP2040 + ILI9486L", LCD_COLOR_YELLOW, accent, 2);
    ili9486l_draw_string(22, 50, "LANDSCAPE 480X320", LCD_COLOR_WHITE, accent, 2);

    ili9486l_draw_string(24, 106, "5X7 FONT DEMO", LCD_COLOR_CYAN, panel, 2);
    ili9486l_draw_string(24, 134, "4-WIRE SPI", LCD_COLOR_WHITE, panel, 2);
    ili9486l_draw_string(24, 176, "SCK  GP14", LCD_COLOR_WHITE, panel, 2);
    ili9486l_draw_string(24, 204, "MOSI GP15", LCD_COLOR_WHITE, panel, 2);
    ili9486l_draw_string(24, 232, "DC   GP10", LCD_COLOR_GREEN, panel, 2);
    ili9486l_draw_string(24, 260, "RST  GP11", LCD_COLOR_GREEN, panel, 2);

    ili9486l_draw_string(264, 106, "TX ONLY SPI", LCD_COLOR_WHITE, panel, 2);
    ili9486l_draw_string(264, 134, "NO MISO PIN", LCD_COLOR_WHITE, panel, 2);
    ili9486l_draw_string(264, 162, "BLK  TO 3V3", LCD_COLOR_GREEN, panel, 2);
    ili9486l_draw_string(264, 190, "320X480 TFT", LCD_COLOR_CYAN, panel, 2);
    ili9486l_draw_string(264, 228, "HELLO,", LCD_COLOR_YELLOW, panel, 3);
    ili9486l_draw_string(264, 258, "PICO!", LCD_COLOR_YELLOW, panel, 3);

    ili9486l_fill_rect(20, 286, 440, 20, strip_frame);
    draw_rgb_gradient_strip(24, 290, 432, 12);
}

int main(void)
{
    static vt100_terminal_t terminal;
    uint16_t terminal_origin_y = 0;
    uint32_t last_terminal_tick_ms = 0u;

    stdio_init_all();
    sleep_ms(1000);

    ili9486l_init();
    show_boot_logo();
    terminal_origin_y = (uint16_t)((ili9486l_height() - VT100_TERMINAL_HEIGHT_PIXELS) / 2u);
    ili9486l_fill_screen(LCD_COLOR_BLACK);
    vt100_terminal_init(&terminal, 0, terminal_origin_y);
    vt100_terminal_set_output(&terminal, terminal_stdio_output, NULL);
    vt100_terminal_write(&terminal, "\x1b[2J\x1b[H");
    vt100_terminal_write(&terminal, "ILI9486L VT100 TERMINAL 80X35\r\n");
    vt100_terminal_write(&terminal, "UART/STDIO input is rendered directly to LCD.\r\n");
    vt100_terminal_write(&terminal, "Supported: CSI A/B/C/D/E/F/G/H/I/S/T/Z/`/a/b/d/e/f/J/K/L/M/@/P/X.\r\n");
    vt100_terminal_write(&terminal, "Plus: CSI m/r/n/c/g/h/l, ESC #8, DEC/UK/VT52, SS2/SS3, tab stops.\r\n");
    vt100_terminal_write(&terminal, "Modes: IRM, LMN, DECSCNM, DECAWM, DECOM, DECANM, DECSTBM, RI, cursor show/hide.\r\n");
    vt100_terminal_write(&terminal, "Blink: feed elapsed ms into vt100_terminal_tick() to animate SGR 5.\r\n");
    vt100_terminal_write(&terminal, "\r\n");
    vt100_terminal_write(&terminal, "\x1b[32mREADY\x1b[0m> ");
    last_terminal_tick_ms = (uint32_t)to_ms_since_boot(get_absolute_time());

    while (true) {
        const int ch = getchar_timeout_us(0);
        const uint32_t now_ms = (uint32_t)to_ms_since_boot(get_absolute_time());

        vt100_terminal_tick(&terminal, now_ms - last_terminal_tick_ms);
        last_terminal_tick_ms = now_ms;

        if (ch != PICO_ERROR_TIMEOUT) {
            vt100_terminal_putc(&terminal, (char)ch);
        }

        tight_loop_contents();
    }
}
