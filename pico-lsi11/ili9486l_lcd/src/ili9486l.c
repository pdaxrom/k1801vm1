#include "ili9486l.h"

#include "font5x7.h"
#include "ili9486l_internal.h"

#include "hardware/dma.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

#include <stddef.h>

#ifndef LCD_SPI_PORT
#define LCD_SPI_PORT spi1
#endif

#ifndef LCD_SPI_BAUDRATE_HZ
#define LCD_SPI_BAUDRATE_HZ (60u * 1000u * 1000u)
#endif

#ifndef LCD_PIN_SCK
#define LCD_PIN_SCK 14
#endif

#ifndef LCD_PIN_MOSI
#define LCD_PIN_MOSI 15
#endif

#ifndef LCD_PIN_RST
#define LCD_PIN_RST 11
#endif

#ifndef LCD_PIN_DC
#define LCD_PIN_DC 10
#endif

#ifndef LCD_PIN_BLK
#define LCD_PIN_BLK (-1)
#endif

#define ILI9486L_FAST_TEXT_SCALE_MAX 4u
#define ILI9486L_DMA_MIN_TRANSFER_BYTES 256u

static uint16_t g_width = LCD_NATIVE_WIDTH;
static uint16_t g_height = LCD_NATIVE_HEIGHT;
static uint16_t g_scroll_top_fixed = 0;
static uint16_t g_scroll_area = LCD_NATIVE_HEIGHT;
static uint16_t g_scroll_bottom_fixed = 0;
static uint16_t g_scroll_start = 0;
static int g_spi_tx_dma_channel = -1;
static dma_channel_config_t g_spi_tx_dma_config;
static bool g_spi_tx_dma_in_flight = false;

static void __not_in_flash_func(ili9486l_finish_spi_write)(void);

static void __not_in_flash_func(ili9486l_set_dc)(uint8_t data_mode)
{
    if (g_spi_tx_dma_in_flight) {
        dma_channel_wait_for_finish_blocking((uint)g_spi_tx_dma_channel);
        g_spi_tx_dma_in_flight = false;
        ili9486l_finish_spi_write();
    }

    gpio_put(LCD_PIN_DC, data_mode);
    busy_wait_us_32(1);
}

static void __not_in_flash_func(ili9486l_finish_spi_write)(void)
{
    while (spi_is_readable(LCD_SPI_PORT)) {
        (void)spi_get_hw(LCD_SPI_PORT)->dr;
    }
    while (spi_is_busy(LCD_SPI_PORT)) {
        tight_loop_contents();
    }
    while (spi_is_readable(LCD_SPI_PORT)) {
        (void)spi_get_hw(LCD_SPI_PORT)->dr;
    }

    spi_get_hw(LCD_SPI_PORT)->icr = SPI_SSPICR_RORIC_BITS;
}

static void ili9486l_init_dma(void)
{
    const int channel = dma_claim_unused_channel(false);

    if (channel < 0) {
        return;
    }

    g_spi_tx_dma_channel = channel;
    g_spi_tx_dma_config = dma_channel_get_default_config((uint)channel);
    channel_config_set_transfer_data_size(&g_spi_tx_dma_config, DMA_SIZE_8);
    channel_config_set_read_increment(&g_spi_tx_dma_config, true);
    channel_config_set_write_increment(&g_spi_tx_dma_config, false);
    channel_config_set_dreq(&g_spi_tx_dma_config, spi_get_dreq(LCD_SPI_PORT, true));
}

static bool __not_in_flash_func(ili9486l_write_dma_if_beneficial)(const uint8_t *data, size_t len)
{
    if (data == NULL || len < ILI9486L_DMA_MIN_TRANSFER_BYTES || g_spi_tx_dma_channel < 0) {
        return false;
    }

    dma_channel_configure(
        (uint)g_spi_tx_dma_channel,
        &g_spi_tx_dma_config,
        &spi_get_hw(LCD_SPI_PORT)->dr,
        data,
        dma_encode_transfer_count((uint)len),
        true);
    dma_channel_wait_for_finish_blocking((uint)g_spi_tx_dma_channel);
    ili9486l_finish_spi_write();
    return true;
}

void __not_in_flash_func(ili9486l_wait_for_pending_write)(void)
{
    if (!g_spi_tx_dma_in_flight) {
        return;
    }

    dma_channel_wait_for_finish_blocking((uint)g_spi_tx_dma_channel);
    g_spi_tx_dma_in_flight = false;
    ili9486l_finish_spi_write();
}

static void ili9486l_write_command(uint8_t command)
{
    ili9486l_set_dc(0);
    spi_write_blocking(LCD_SPI_PORT, &command, 1);
}

static void ili9486l_write_data(const uint8_t *data, size_t len)
{
    if (len == 0) {
        return;
    }

    ili9486l_set_dc(1);
    spi_write_blocking(LCD_SPI_PORT, data, len);
}

static void ili9486l_write_command_data(uint8_t command, const uint8_t *data, size_t len)
{
    ili9486l_write_command(command);
    ili9486l_write_data(data, len);
}

static void ili9486l_write_u16_command(uint8_t command, uint16_t value)
{
    const uint8_t data[] = {
        (uint8_t)(value >> 8),
        (uint8_t)(value & 0xFFu),
    };

    ili9486l_write_command_data(command, data, sizeof(data));
}

static void ili9486l_init_panel_registers(void)
{
    static const uint8_t pwctr1[] = {0x17, 0x15};
    static const uint8_t pwctr2[] = {0x41};
    static const uint8_t vmctr1[] = {0x00, 0x12, 0x80};
    static const uint8_t frmctr1[] = {0xA0};
    static const uint8_t invctr[] = {0x02};
    static const uint8_t dfunctr[] = {0x02, 0x22, 0x3B};
    static const uint8_t etmod[] = {0xC6};
    static const uint8_t adjctl3[] = {0xA9, 0x51, 0x2C, 0x82};
    static const uint8_t gmctrp1[] = {
        0x0F, 0x1F, 0x1C, 0x0C, 0x0F, 0x08, 0x48, 0x98,
        0x37, 0x0A, 0x13, 0x04, 0x11, 0x0D, 0x00,
    };
    static const uint8_t gmctrn1[] = {
        0x0F, 0x32, 0x2E, 0x0B, 0x0D, 0x05, 0x47, 0x75,
        0x37, 0x06, 0x10, 0x03, 0x24, 0x20, 0x00,
    };

    ili9486l_write_command_data(0xC0, pwctr1, sizeof(pwctr1));
    ili9486l_write_command_data(0xC1, pwctr2, sizeof(pwctr2));
    ili9486l_write_command_data(0xC5, vmctr1, sizeof(vmctr1));
    ili9486l_write_command_data(0xB1, frmctr1, sizeof(frmctr1));
    ili9486l_write_command_data(0xB4, invctr, sizeof(invctr));
    ili9486l_write_command_data(0xB6, dfunctr, sizeof(dfunctr));
    ili9486l_write_command_data(0xB7, etmod, sizeof(etmod));
    ili9486l_write_command_data(0xF7, adjctl3, sizeof(adjctl3));
    ili9486l_write_command_data(0xE0, gmctrp1, sizeof(gmctrp1));
    ili9486l_write_command_data(0xE1, gmctrn1, sizeof(gmctrn1));
}

static void ili9486l_hard_reset(void)
{
    gpio_put(LCD_PIN_RST, 1);
    sleep_ms(20);
    gpio_put(LCD_PIN_RST, 0);
    sleep_ms(20);
    gpio_put(LCD_PIN_RST, 1);
    sleep_ms(150);
}

static inline void ili9486l_pack_rgb666(uint8_t red, uint8_t green, uint8_t blue, uint8_t out[3])
{
    out[0] = (uint8_t)((red & 0x3Fu) << 2);
    out[1] = (uint8_t)((green & 0x3Fu) << 2);
    out[2] = (uint8_t)((blue & 0x3Fu) << 2);
}

static inline void ili9486l_color_to_rgb666(lcd_color_t color, uint8_t out[3])
{
    out[0] = (uint8_t)(((color >> 12) & 0x3Fu) << 2);
    out[1] = (uint8_t)(((color >> 6) & 0x3Fu) << 2);
    out[2] = (uint8_t)((color & 0x3Fu) << 2);
}

static void __not_in_flash_func(ili9486l_set_address_window)(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    const uint8_t column_address[] = {
        (uint8_t)(x0 >> 8), (uint8_t)(x0 & 0xFFu),
        (uint8_t)(x1 >> 8), (uint8_t)(x1 & 0xFFu),
    };
    const uint8_t row_address[] = {
        (uint8_t)(y0 >> 8), (uint8_t)(y0 & 0xFFu),
        (uint8_t)(y1 >> 8), (uint8_t)(y1 & 0xFFu),
    };

    ili9486l_write_command_data(0x2A, column_address, sizeof(column_address));
    ili9486l_write_command_data(0x2B, row_address, sizeof(row_address));
    ili9486l_write_command(0x2C);
}

static void __not_in_flash_func(ili9486l_write_rgb666_repeat)(const uint8_t pixel[3], uint32_t pixel_count)
{
    uint8_t burst[64 * 3];

    for (size_t i = 0; i < 64; ++i) {
        burst[i * 3 + 0] = pixel[0];
        burst[i * 3 + 1] = pixel[1];
        burst[i * 3 + 2] = pixel[2];
    }

    ili9486l_set_dc(1);

    while (pixel_count > 0) {
        const uint32_t chunk = pixel_count > 64u ? 64u : pixel_count;

        spi_write_blocking(LCD_SPI_PORT, burst, chunk * 3u);
        pixel_count -= chunk;
    }
}

static void __not_in_flash_func(ili9486l_write_color_repeat)(lcd_color_t color, uint32_t pixel_count)
{
    uint8_t pixel[3];

    ili9486l_color_to_rgb666(color, pixel);
    ili9486l_write_rgb666_repeat(pixel, pixel_count);
}

bool __not_in_flash_func(ili9486l_begin_write)(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    if (w == 0 || h == 0 || x >= g_width || y >= g_height) {
        return false;
    }

    if ((uint32_t)x + w > g_width || (uint32_t)y + h > g_height) {
        return false;
    }

    ili9486l_set_address_window(x, y, (uint16_t)(x + w - 1u), (uint16_t)(y + h - 1u));
    return true;
}

bool ili9486l_configure_vertical_scroll(uint16_t top_fixed, uint16_t scroll_area, uint16_t bottom_fixed)
{
    const uint32_t total = (uint32_t)top_fixed + scroll_area + bottom_fixed;
    const uint8_t data[] = {
        (uint8_t)(top_fixed >> 8), (uint8_t)(top_fixed & 0xFFu),
        (uint8_t)(scroll_area >> 8), (uint8_t)(scroll_area & 0xFFu),
        (uint8_t)(bottom_fixed >> 8), (uint8_t)(bottom_fixed & 0xFFu),
    };

    if (scroll_area == 0 || total != g_height) {
        return false;
    }

    g_scroll_top_fixed = top_fixed;
    g_scroll_area = scroll_area;
    g_scroll_bottom_fixed = bottom_fixed;
    g_scroll_start = 0;

    ili9486l_write_command_data(0x33, data, sizeof(data));
    ili9486l_write_u16_command(0x37, g_scroll_top_fixed);
    return true;
}

bool ili9486l_set_vertical_scroll_start(uint16_t start)
{
    uint16_t vsp = 0;

    if (g_scroll_area == 0) {
        return false;
    }

    start %= g_scroll_area;
    g_scroll_start = start;
    vsp = (uint16_t)(g_scroll_top_fixed + start);
    ili9486l_write_u16_command(0x37, vsp);
    return true;
}

bool ili9486l_scroll_vertical_by(int16_t delta)
{
    int32_t next = 0;

    if (g_scroll_area == 0) {
        return false;
    }

    next = (int32_t)g_scroll_start + delta;
    next %= (int32_t)g_scroll_area;
    if (next < 0) {
        next += g_scroll_area;
    }

    return ili9486l_set_vertical_scroll_start((uint16_t)next);
}

void ili9486l_reset_vertical_scroll(void)
{
    (void)ili9486l_configure_vertical_scroll(0, g_height, 0);
    (void)ili9486l_set_vertical_scroll_start(0);
}

void __not_in_flash_func(ili9486l_write_rgb666_pixels)(const uint8_t *pixels, size_t pixel_count)
{
    uint8_t burst[64 * 3];
    const uint8_t *src = pixels;

    if (pixels == NULL || pixel_count == 0) {
        return;
    }

    ili9486l_set_dc(1);

    while (pixel_count > 0) {
        const size_t chunk = pixel_count > 64u ? 64u : pixel_count;

        for (size_t i = 0; i < chunk; ++i) {
            burst[i * 3u + 0] = (uint8_t)((src[0] & 0x3Fu) << 2);
            burst[i * 3u + 1] = (uint8_t)((src[1] & 0x3Fu) << 2);
            burst[i * 3u + 2] = (uint8_t)((src[2] & 0x3Fu) << 2);
            src += 3;
        }

        spi_write_blocking(LCD_SPI_PORT, burst, chunk * 3u);
        pixel_count -= chunk;
    }
}

void __not_in_flash_func(ili9486l_write_rgb666_wire_pixels)(const uint8_t *pixels, size_t pixel_count)
{
    const size_t len = pixel_count * 3u;

    if (pixels == NULL || pixel_count == 0u) {
        return;
    }

    ili9486l_set_dc(1);
    if (!ili9486l_write_dma_if_beneficial(pixels, len)) {
        spi_write_blocking(LCD_SPI_PORT, pixels, len);
    }
}

void __not_in_flash_func(ili9486l_write_rgb666_wire_pixels_async)(const uint8_t *pixels, size_t pixel_count)
{
    const size_t len = pixel_count * 3u;

    if (pixels == NULL || pixel_count == 0u) {
        return;
    }

    ili9486l_wait_for_pending_write();
    ili9486l_set_dc(1);

    if (len < ILI9486L_DMA_MIN_TRANSFER_BYTES || g_spi_tx_dma_channel < 0) {
        spi_write_blocking(LCD_SPI_PORT, pixels, len);
        return;
    }

    dma_channel_configure(
        (uint)g_spi_tx_dma_channel,
        &g_spi_tx_dma_config,
        &spi_get_hw(LCD_SPI_PORT)->dr,
        pixels,
        dma_encode_transfer_count((uint)len),
        true);
    g_spi_tx_dma_in_flight = true;
}

void __not_in_flash_func(ili9486l_write_rgb888_as_rgb666_pixels)(const uint8_t *pixels, size_t pixel_count)
{
    uint8_t burst[64 * 3];
    const uint8_t *src = pixels;

    if (pixels == NULL || pixel_count == 0) {
        return;
    }

    ili9486l_set_dc(1);

    while (pixel_count > 0) {
        const size_t chunk = pixel_count > 64u ? 64u : pixel_count;

        for (size_t i = 0; i < chunk; ++i) {
            burst[i * 3u + 0] = (uint8_t)(src[0] & 0xFCu);
            burst[i * 3u + 1] = (uint8_t)(src[1] & 0xFCu);
            burst[i * 3u + 2] = (uint8_t)(src[2] & 0xFCu);
            src += 3;
        }

        spi_write_blocking(LCD_SPI_PORT, burst, chunk * 3u);
        pixel_count -= chunk;
    }
}

void ili9486l_draw_rgb666_rect(const uint8_t *bitmap, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    if (bitmap == NULL || !ili9486l_begin_write(x, y, w, h)) {
        return;
    }

    ili9486l_write_rgb666_pixels(bitmap, (size_t)w * h);
}

void ili9486l_draw_rgb666_wire_rect(const uint8_t *bitmap, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    if (bitmap == NULL || !ili9486l_begin_write(x, y, w, h)) {
        return;
    }

    ili9486l_write_rgb666_wire_pixels(bitmap, (size_t)w * h);
}

void ili9486l_draw_rgb666_bitmap(const uint8_t *bitmap, uint16_t w, uint16_t h)
{
    if (bitmap == NULL || !ili9486l_begin_write(0, 0, w, h)) {
        return;
    }

    ili9486l_write_rgb666_pixels(bitmap, (size_t)w * h);
}

void ili9486l_draw_rgb888_as_rgb666_rect(const uint8_t *bitmap, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    if (bitmap == NULL || !ili9486l_begin_write(x, y, w, h)) {
        return;
    }

    ili9486l_write_rgb888_as_rgb666_pixels(bitmap, (size_t)w * h);
}

uint16_t ili9486l_width(void)
{
    return g_width;
}

uint16_t ili9486l_height(void)
{
    return g_height;
}

void ili9486l_set_rotation(uint8_t rotation)
{
    uint8_t madctl = 0x48;

    switch (rotation & 0x03u) {
    case 0:
        madctl = 0x48;
        g_width = LCD_NATIVE_WIDTH;
        g_height = LCD_NATIVE_HEIGHT;
        break;
    case 1:
        madctl = 0x68;
        g_width = LCD_NATIVE_HEIGHT;
        g_height = LCD_NATIVE_WIDTH;
        break;
    case 2:
        madctl = 0x88;
        g_width = LCD_NATIVE_WIDTH;
        g_height = LCD_NATIVE_HEIGHT;
        break;
    case 3:
        madctl = 0xE8;
        g_width = LCD_NATIVE_HEIGHT;
        g_height = LCD_NATIVE_WIDTH;
        break;
    }

    ili9486l_write_command_data(0x36, &madctl, 1);
    ili9486l_reset_vertical_scroll();
}

void ili9486l_init(void)
{
    spi_init(LCD_SPI_PORT, LCD_SPI_BAUDRATE_HZ);
    spi_set_format(LCD_SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    ili9486l_init_dma();

    gpio_set_function(LCD_PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(LCD_PIN_MOSI, GPIO_FUNC_SPI);

    gpio_init(LCD_PIN_DC);
    gpio_set_dir(LCD_PIN_DC, GPIO_OUT);
    gpio_put(LCD_PIN_DC, 1);

    gpio_init(LCD_PIN_RST);
    gpio_set_dir(LCD_PIN_RST, GPIO_OUT);
    gpio_put(LCD_PIN_RST, 1);

#if LCD_PIN_BLK >= 0
    gpio_init(LCD_PIN_BLK);
    gpio_set_dir(LCD_PIN_BLK, GPIO_OUT);
    gpio_put(LCD_PIN_BLK, 1);
#endif

    ili9486l_hard_reset();
    ili9486l_write_command(0x01);
    sleep_ms(150);

    {
        const uint8_t interface_mode = 0x00;
        ili9486l_write_command_data(0xB0, &interface_mode, 1);
    }

    ili9486l_init_panel_registers();

    ili9486l_write_command(0x11);
    sleep_ms(120);

    {
        /* ILI9486L stays in 18-bit SPI mode; regular drawing uses RGB666, JPEG paths truncate RGB888 to top 6 bits. */
        const uint8_t pixel_format = 0x66;
        ili9486l_write_command_data(0x3A, &pixel_format, 1);
    }

    ili9486l_set_rotation(1);

    ili9486l_write_command(0x38);

    ili9486l_write_command(0x29);
    sleep_ms(100);

    ili9486l_fill_screen(LCD_COLOR_BLACK);
}

void ili9486l_draw_pixel(uint16_t x, uint16_t y, lcd_color_t color)
{
    if (x >= g_width || y >= g_height) {
        return;
    }

    ili9486l_set_address_window(x, y, x, y);
    ili9486l_write_color_repeat(color, 1);
}

void __not_in_flash_func(ili9486l_fill_rect)(uint16_t x, uint16_t y, uint16_t w, uint16_t h, lcd_color_t color)
{
    if (w == 0 || h == 0 || x >= g_width || y >= g_height) {
        return;
    }

    if ((uint32_t)x + w > g_width) {
        w = (uint16_t)(g_width - x);
    }
    if ((uint32_t)y + h > g_height) {
        h = (uint16_t)(g_height - y);
    }

    ili9486l_set_address_window(x, y, (uint16_t)(x + w - 1u), (uint16_t)(y + h - 1u));
    ili9486l_write_color_repeat(color, (uint32_t)w * h);
}

void __not_in_flash_func(ili9486l_fill_rect_rgb666)(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t red,
                                                     uint8_t green, uint8_t blue)
{
    uint8_t pixel[3];

    if (w == 0 || h == 0 || x >= g_width || y >= g_height) {
        return;
    }

    if ((uint32_t)x + w > g_width) {
        w = (uint16_t)(g_width - x);
    }
    if ((uint32_t)y + h > g_height) {
        h = (uint16_t)(g_height - y);
    }

    ili9486l_pack_rgb666(red, green, blue, pixel);
    ili9486l_set_address_window(x, y, (uint16_t)(x + w - 1u), (uint16_t)(y + h - 1u));
    ili9486l_write_rgb666_repeat(pixel, (uint32_t)w * h);
}

void ili9486l_fill_screen(lcd_color_t color)
{
    ili9486l_fill_rect(0, 0, g_width, g_height, color);
}

static void ili9486l_draw_char_slow(uint16_t x, uint16_t y, char c, lcd_color_t fg, lcd_color_t bg, uint8_t scale)
{
    const uint8_t *glyph = font5x7_get_glyph(c);

    ili9486l_fill_rect(x, y, (uint16_t)(6u * scale), (uint16_t)(8u * scale), bg);

    for (uint8_t col = 0; col < FONT5X7_WIDTH; ++col) {
        const uint8_t column_bits = glyph[col];

        for (uint8_t row = 0; row < FONT5X7_HEIGHT; ++row) {
            if ((column_bits & (1u << row)) != 0u) {
                ili9486l_fill_rect((uint16_t)(x + col * scale), (uint16_t)(y + row * scale), scale, scale, fg);
            }
        }
    }
}

void __not_in_flash_func(ili9486l_draw_char)(uint16_t x, uint16_t y, char c, lcd_color_t fg, lcd_color_t bg,
                                             uint8_t scale)
{
    const uint8_t *glyph_rows;
    const uint16_t glyph_w = (uint16_t)(6u * scale);
    const uint16_t glyph_h = (uint16_t)(8u * scale);
    uint8_t row_pixels[6u * ILI9486L_FAST_TEXT_SCALE_MAX * 3u];
    uint8_t fg_wire[3];
    uint8_t bg_wire[3];

    if (scale == 0 || x >= g_width || y >= g_height) {
        return;
    }

    glyph_rows = font5x7_get_cell6x8_row_masks(c);
    if (scale > ILI9486L_FAST_TEXT_SCALE_MAX || (uint32_t)x + glyph_w > g_width || (uint32_t)y + glyph_h > g_height) {
        ili9486l_draw_char_slow(x, y, c, fg, bg, scale);
        return;
    }

    ili9486l_color_to_rgb666(fg, fg_wire);
    ili9486l_color_to_rgb666(bg, bg_wire);

    if (!ili9486l_begin_write(x, y, glyph_w, glyph_h)) {
        return;
    }

    for (uint8_t glyph_row = 0; glyph_row < 8u; ++glyph_row) {
        const uint8_t row_mask = glyph_rows[glyph_row];

        for (uint8_t sy = 0; sy < scale; ++sy) {
            uint8_t *dst = row_pixels;

            for (uint8_t col = 0; col < 6u; ++col) {
                const bool pixel_on = (row_mask & (1u << col)) != 0u;
                const uint8_t *color = pixel_on ? fg_wire : bg_wire;

                for (uint8_t sx = 0; sx < scale; ++sx) {
                    dst[0] = color[0];
                    dst[1] = color[1];
                    dst[2] = color[2];
                    dst += 3;
                }
            }

            ili9486l_write_rgb666_wire_pixels(row_pixels, glyph_w);
        }
    }
}

void ili9486l_draw_string(uint16_t x, uint16_t y, const char *text, lcd_color_t fg, lcd_color_t bg, uint8_t scale)
{
    const uint16_t start_x = x;
    const uint16_t advance_x = (uint16_t)(6u * scale);
    const uint16_t advance_y = (uint16_t)(8u * scale);

    if (text == NULL || scale == 0) {
        return;
    }

    while (*text != '\0') {
        if (*text == '\n') {
            x = start_x;
            y = (uint16_t)(y + advance_y);
            ++text;
            continue;
        }

        if ((uint32_t)x + advance_x > g_width) {
            x = start_x;
            y = (uint16_t)(y + advance_y);
        }

        if ((uint32_t)y + advance_y > g_height) {
            break;
        }

        ili9486l_draw_char(x, y, *text, fg, bg, scale);
        x = (uint16_t)(x + advance_x);
        ++text;
    }
}
