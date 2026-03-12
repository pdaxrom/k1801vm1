#include "ili9486l.h"
#include "ili9486l_jpeg.h"
#include "vt100_terminal.h"

#include "pico/stdlib.h"

#include <stdio.h>

#ifndef ILI9486L_LCD_DEMO_RUN_FPS_TEST
#define ILI9486L_LCD_DEMO_RUN_FPS_TEST 1
#endif

extern const uint8_t g_logo_jpg_start[];
extern const uint8_t g_logo_jpg_end[];

static uint8_t g_full_redraw_scanline[LCD_NATIVE_HEIGHT * 3u];

typedef struct {
    uint32_t rate_x10;
    uint32_t ms_x10;
} benchmark_result_t;

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

static void format_fixed_x10(char *dst, size_t dst_size, uint32_t value_x10)
{
    snprintf(dst, dst_size, "%lu.%01lu", (unsigned long)(value_x10 / 10u), (unsigned long)(value_x10 % 10u));
}

static void draw_full_redraw_benchmark_frame(uint32_t frame_index)
{
    const uint16_t width = ili9486l_width();
    const uint16_t height = ili9486l_height();

    if (!ili9486l_begin_write(0u, 0u, width, height)) {
        return;
    }

    for (uint16_t y = 0; y < height; ++y) {
        const uint8_t y6 = (height > 1u) ? (uint8_t)(((uint32_t)y * 63u) / (uint32_t)(height - 1u)) : 0u;

        for (uint16_t x = 0; x < width; ++x) {
            const uint8_t x6 = (width > 1u) ? (uint8_t)(((uint32_t)x * 63u) / (uint32_t)(width - 1u)) : 0u;
            const uint8_t r6 = (uint8_t)((x6 + frame_index * 3u) & 0x3Fu);
            const uint8_t g6 = (uint8_t)((y6 + frame_index * 5u) & 0x3Fu);
            const uint8_t b6 = (uint8_t)(((x6 ^ y6) + frame_index * 7u) & 0x3Fu);
            const size_t offset = (size_t)x * 3u;

            g_full_redraw_scanline[offset + 0u] = (uint8_t)(r6 << 2);
            g_full_redraw_scanline[offset + 1u] = (uint8_t)(g6 << 2);
            g_full_redraw_scanline[offset + 2u] = (uint8_t)(b6 << 2);
        }

        ili9486l_write_rgb666_wire_pixels(g_full_redraw_scanline, width);
    }
}

static void run_full_redraw_fps_test(benchmark_result_t *result)
{
    static const uint32_t k_frame_count = 12u;
    const uint64_t start_us = time_us_64();
    uint64_t elapsed_us = 0u;

    if (result == NULL) {
        return;
    }

    for (uint32_t frame = 0; frame < k_frame_count; ++frame) {
        draw_full_redraw_benchmark_frame(frame);
    }

    elapsed_us = time_us_64() - start_us;
    if (elapsed_us == 0u) {
        elapsed_us = 1u;
    }

    result->rate_x10 = (uint32_t)(((uint64_t)k_frame_count * 10000000u) / elapsed_us);
    result->ms_x10 = (uint32_t)((elapsed_us * 10u) / ((uint64_t)k_frame_count * 1000u));
}

static void show_full_redraw_fps_test(void)
{
    const uint16_t width = ili9486l_width();
    const uint16_t height = ili9486l_height();
    benchmark_result_t result;
    char fps_text[16];
    char ms_text[16];
    char line[48];

    run_full_redraw_fps_test(&result);
    format_fixed_x10(fps_text, sizeof(fps_text), result.rate_x10);
    format_fixed_x10(ms_text, sizeof(ms_text), result.ms_x10);

    printf("FULL REDRAW FPS TEST: %s FPS, %s ms/frame, %u frames at %ux%u\r\n",
           fps_text,
           ms_text,
           12u,
           width,
           height);

    ili9486l_fill_screen(LCD_RGB666(0x02, 0x03, 0x06));
    ili9486l_fill_rect(16, 18, 448, 284, LCD_RGB666(0x06, 0x08, 0x0C));
    ili9486l_draw_string(34, 34, "FULL REDRAW TEST", LCD_COLOR_YELLOW, LCD_RGB666(0x06, 0x08, 0x0C), 2);
    ili9486l_draw_string(34, 74, "STREAMING RGB666", LCD_COLOR_WHITE, LCD_RGB666(0x06, 0x08, 0x0C), 2);

    snprintf(line, sizeof(line), "%s FPS", fps_text);
    ili9486l_draw_string(34, 128, line, LCD_COLOR_CYAN, LCD_RGB666(0x06, 0x08, 0x0C), 3);

    snprintf(line, sizeof(line), "%s MS/FRAME", ms_text);
    ili9486l_draw_string(34, 180, line, LCD_COLOR_WHITE, LCD_RGB666(0x06, 0x08, 0x0C), 2);

    snprintf(line, sizeof(line), "%u FRAMES @ %ux%u", 12u, width, height);
    ili9486l_draw_string(34, 224, line, LCD_COLOR_GREEN, LCD_RGB666(0x06, 0x08, 0x0C), 2);
    ili9486l_draw_string(34, 268, "RESULT ALSO IN STDIO", LCD_COLOR_WHITE, LCD_RGB666(0x06, 0x08, 0x0C), 1);

    sleep_ms(1500);
}

static void prepare_terminal_render_benchmark(vt100_terminal_t *terminal, uint16_t origin_y)
{
    static const char k_pattern[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789[]{}<>+-*/";

    vt100_terminal_init(terminal, 0u, origin_y);

    for (uint8_t row = 0; row < VT100_TERMINAL_ROWS; ++row) {
        for (uint8_t col = 0; col < VT100_TERMINAL_COLS; ++col) {
            vt100_terminal_cell_t *cell = &terminal->cells[row][col];
            const uint8_t fg = (uint8_t)((8u + row + col) & 0x0Fu);
            const uint8_t bg = (uint8_t)(((row / 5u) + (col / 20u)) & 0x07u);

            cell->ch = k_pattern[(row * 7u + col) % (sizeof(k_pattern) - 1u)];
            cell->attr = (uint8_t)(((bg & 0x0Fu) << 4) | (fg & 0x0Fu));
            cell->style = 0u;
            if (((row + col) % 9u) == 0u) {
                cell->style |= 0x01u;
            }
            if (((row + col) % 11u) == 0u) {
                cell->style |= 0x04u;
            }
            cell->charset = 0u;
        }
    }

    terminal->cursor_row = (uint8_t)(VT100_TERMINAL_ROWS / 2u);
    terminal->cursor_col = (uint8_t)(VT100_TERMINAL_COLS / 2u);
    terminal->wrap_pending = false;
    terminal->blink_visible = true;
}

static size_t build_terminal_benchmark_line(char *dst, uint32_t line_index)
{
    static const char k_pattern[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789[]{}<>|+-*/";
    char prefix[16];
    int prefix_len = snprintf(prefix, sizeof(prefix), "%04lu ", (unsigned long)(line_index % 10000u));

    if (prefix_len < 0) {
        prefix_len = 0;
    }

    for (uint16_t i = 0; i < VT100_TERMINAL_COLS; ++i) {
        if (i < (uint16_t)prefix_len) {
            dst[i] = prefix[i];
        } else {
            dst[i] = k_pattern[(i + line_index) % (sizeof(k_pattern) - 1u)];
        }
    }

    dst[VT100_TERMINAL_COLS + 0u] = '\r';
    dst[VT100_TERMINAL_COLS + 1u] = '\n';
    return VT100_TERMINAL_COLS + 2u;
}

static void run_terminal_render_fps_test(uint16_t origin_y, benchmark_result_t *result)
{
    static const uint32_t k_frame_count = 6u;
    static vt100_terminal_t terminal;
    uint64_t elapsed_us = 0u;
    const uint64_t start_us = time_us_64();

    if (result == NULL) {
        return;
    }

    prepare_terminal_render_benchmark(&terminal, origin_y);

    for (uint32_t frame = 0; frame < k_frame_count; ++frame) {
        vt100_terminal_render(&terminal);
    }

    elapsed_us = time_us_64() - start_us;
    if (elapsed_us == 0u) {
        elapsed_us = 1u;
    }

    result->rate_x10 = (uint32_t)(((uint64_t)k_frame_count * 10000000u) / elapsed_us);
    result->ms_x10 = (uint32_t)((elapsed_us * 10u) / ((uint64_t)k_frame_count * 1000u));
}

static void run_terminal_scroll_fps_test(uint16_t origin_y, benchmark_result_t *result)
{
    static const uint32_t k_scroll_count = 12u;
    static vt100_terminal_t terminal;
    char line[VT100_TERMINAL_COLS + 2u];
    uint64_t elapsed_us = 0u;

    if (result == NULL) {
        return;
    }

    vt100_terminal_init(&terminal, 0u, origin_y);

    for (uint32_t row = 0; row < (uint32_t)(VT100_TERMINAL_ROWS - 1u); ++row) {
        const size_t len = build_terminal_benchmark_line(line, row);
        vt100_terminal_write_n(&terminal, line, len);
    }

    {
        const uint64_t start_us = time_us_64();

        for (uint32_t row = 0; row < k_scroll_count; ++row) {
            const size_t len = build_terminal_benchmark_line(line, 1000u + row);
            vt100_terminal_write_n(&terminal, line, len);
        }

        elapsed_us = time_us_64() - start_us;
    }

    if (elapsed_us == 0u) {
        elapsed_us = 1u;
    }

    result->rate_x10 = (uint32_t)(((uint64_t)k_scroll_count * 10000000u) / elapsed_us);
    result->ms_x10 = (uint32_t)((elapsed_us * 10u) / ((uint64_t)k_scroll_count * 1000u));
}

static void show_terminal_benchmark_results(uint16_t origin_y)
{
    benchmark_result_t render_result;
    benchmark_result_t scroll_result;
    char render_rate_text[16];
    char render_ms_text[16];
    char scroll_rate_text[16];
    char scroll_ms_text[16];
    char line[48];
    const lcd_color_t background = LCD_RGB666(0x03, 0x03, 0x06);
    const lcd_color_t panel = LCD_RGB666(0x06, 0x08, 0x0C);

    run_terminal_render_fps_test(origin_y, &render_result);
    run_terminal_scroll_fps_test(origin_y, &scroll_result);

    format_fixed_x10(render_rate_text, sizeof(render_rate_text), render_result.rate_x10);
    format_fixed_x10(render_ms_text, sizeof(render_ms_text), render_result.ms_x10);
    format_fixed_x10(scroll_rate_text, sizeof(scroll_rate_text), scroll_result.rate_x10);
    format_fixed_x10(scroll_ms_text, sizeof(scroll_ms_text), scroll_result.ms_x10);

    printf("VT100 FULL RENDER TEST: %s FPS, %s ms/frame at 80x35\r\n",
           render_rate_text,
           render_ms_text);
    printf("VT100 SCROLL TEST: %s lines/s, %s ms/line at 80 cols\r\n",
           scroll_rate_text,
           scroll_ms_text);

    ili9486l_fill_screen(background);
    ili9486l_fill_rect(16, 18, 448, 284, panel);
    ili9486l_draw_string(34, 34, "VT100 BENCHMARK", LCD_COLOR_YELLOW, panel, 2);

    snprintf(line, sizeof(line), "RENDER %s FPS", render_rate_text);
    ili9486l_draw_string(34, 88, line, LCD_COLOR_CYAN, panel, 2);
    snprintf(line, sizeof(line), "%s MS/FRAME", render_ms_text);
    ili9486l_draw_string(34, 116, line, LCD_COLOR_WHITE, panel, 2);

    snprintf(line, sizeof(line), "SCROLL %s L/S", scroll_rate_text);
    ili9486l_draw_string(34, 172, line, LCD_COLOR_GREEN, panel, 2);
    snprintf(line, sizeof(line), "%s MS/LINE", scroll_ms_text);
    ili9486l_draw_string(34, 200, line, LCD_COLOR_WHITE, panel, 2);

    ili9486l_draw_string(34, 254, "FULL RESULT ALSO IN STDIO", LCD_COLOR_WHITE, panel, 1);
    sleep_ms(1800);
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
#if ILI9486L_LCD_DEMO_RUN_FPS_TEST
    show_full_redraw_fps_test();
    show_terminal_benchmark_results((uint16_t)((ili9486l_height() - VT100_TERMINAL_HEIGHT_PIXELS) / 2u));
#endif
    terminal_origin_y = (uint16_t)((ili9486l_height() - VT100_TERMINAL_HEIGHT_PIXELS) / 2u);
    ili9486l_fill_screen(LCD_COLOR_BLACK);
    vt100_terminal_init(&terminal, 0u, terminal_origin_y);
    vt100_terminal_set_output(&terminal, terminal_stdio_output, NULL);
    (void)vt100_terminal_getch(&terminal, PICO_ERROR_TIMEOUT);
    vt100_terminal_write(&terminal, "\x1b[2J\x1b[H");
    vt100_terminal_write(&terminal, "ILI9486L VT100 TERMINAL 80X34 + STATUS\r\n");
    vt100_terminal_write(&terminal, "Last line is reserved for status. Ctrl+E enters local command mode.\r\n");
    vt100_terminal_write(&terminal, "Ctrl+E 1/2/3 switches 80x34 / 80x30 / 80x24. Ctrl+E S/P switches SCROLL / PAGED.\r\n");
    vt100_terminal_write(&terminal, "In paged mode the status line asks for SPACE before a new page.\r\n");
    vt100_terminal_write(&terminal, "Commands are handled locally by vt100_terminal_getch().\r\n");
    vt100_terminal_write(&terminal, "Supported terminal core: ANSI, VT100, VT52, DEC graphics.\r\n");
    vt100_terminal_write(&terminal, "\r\n");
    vt100_terminal_write(&terminal, "\x1b[32mREADY\x1b[0m> ");
    last_terminal_tick_ms = (uint32_t)to_ms_since_boot(get_absolute_time());

    while (true) {
        const int ch = getchar_timeout_us(0);
        const uint32_t now_ms = (uint32_t)to_ms_since_boot(get_absolute_time());

        vt100_terminal_tick(&terminal, now_ms - last_terminal_tick_ms);
        last_terminal_tick_ms = now_ms;

        (void)vt100_terminal_getch(&terminal, ch);

        tight_loop_contents();
    }
}
