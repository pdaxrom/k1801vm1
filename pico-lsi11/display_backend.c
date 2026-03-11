#include "display_backend.h"

#include "pico/critical_section.h"
#include "pico/stdio/driver.h"
#include "pico/stdlib.h"

#include <string.h>

#define DISPLAY_BACKEND_QUEUE_CAPACITY 4096u
#define DISPLAY_BACKEND_DRAIN_CHUNK 128u
#define DISPLAY_BACKEND_ILI_BATCH_CAPACITY 512u
#define DISPLAY_BACKEND_ILI_BATCH_DELAY_MS 50u

static critical_section_t g_display_queue_lock;
static char g_display_queue[DISPLAY_BACKEND_QUEUE_CAPACITY];
static uint16_t g_display_queue_head = 0u;
static uint16_t g_display_queue_tail = 0u;
static uint16_t g_display_queue_count = 0u;
static bool g_display_backend_initialized = false;
static bool g_display_backend_queue_ready = false;
static bool g_display_backend_output_enabled = false;
static char g_display_last_output_char = '\0';
#if defined(PICO_LSI11_DISPLAY_BACKEND_ILI9486L)
static char g_display_ili_batch[DISPLAY_BACKEND_ILI_BATCH_CAPACITY];
static size_t g_display_ili_batch_len = 0u;
static uint32_t g_display_ili_batch_started_ms = 0u;
#endif

static void display_queue_reset(void)
{
    if (!g_display_backend_queue_ready) {
        g_display_queue_head = 0u;
        g_display_queue_tail = 0u;
        g_display_queue_count = 0u;
        g_display_last_output_char = '\0';
#if defined(PICO_LSI11_DISPLAY_BACKEND_ILI9486L)
        g_display_ili_batch_len = 0u;
        g_display_ili_batch_started_ms = 0u;
#endif
        return;
    }

    critical_section_enter_blocking(&g_display_queue_lock);
    g_display_queue_head = 0u;
    g_display_queue_tail = 0u;
    g_display_queue_count = 0u;
    critical_section_exit(&g_display_queue_lock);
    g_display_last_output_char = '\0';
#if defined(PICO_LSI11_DISPLAY_BACKEND_ILI9486L)
    g_display_ili_batch_len = 0u;
    g_display_ili_batch_started_ms = 0u;
#endif
}

static bool display_queue_try_push(char ch)
{
    bool pushed = false;

    if (!g_display_backend_initialized || !g_display_backend_output_enabled) {
        return false;
    }

    critical_section_enter_blocking(&g_display_queue_lock);
    if (g_display_queue_count < DISPLAY_BACKEND_QUEUE_CAPACITY) {
        g_display_queue[g_display_queue_head] = ch;
        g_display_queue_head++;
        if (g_display_queue_head == DISPLAY_BACKEND_QUEUE_CAPACITY) {
            g_display_queue_head = 0u;
        }
        g_display_queue_count++;
        pushed = true;
    }
    critical_section_exit(&g_display_queue_lock);
    return pushed;
}

static size_t display_queue_pop_n(char *buf, size_t buf_len)
{
    size_t popped = 0u;

    if (!buf || buf_len == 0u || !g_display_backend_initialized) {
        return 0u;
    }

    critical_section_enter_blocking(&g_display_queue_lock);
    while (popped < buf_len && g_display_queue_count > 0u) {
        buf[popped++] = g_display_queue[g_display_queue_tail];
        g_display_queue_tail++;
        if (g_display_queue_tail == DISPLAY_BACKEND_QUEUE_CAPACITY) {
            g_display_queue_tail = 0u;
        }
        g_display_queue_count--;
    }
    critical_section_exit(&g_display_queue_lock);
    return popped;
}

static void display_backend_enqueue_char(char ch)
{
#if defined(PICO_LSI11_DISPLAY_BACKEND_ILI9486L)
    if (ch == '\n' && g_display_last_output_char != '\r') {
        (void)display_queue_try_push('\r');
    }
#endif
    if (display_queue_try_push(ch)) {
        g_display_last_output_char = ch;
    }
}

static void display_backend_stdio_out_chars(const char *buf, int len)
{
    if (!buf || len <= 0) {
        return;
    }

    for (int i = 0; i < len; ++i) {
        display_backend_enqueue_char(buf[i]);
    }
}

static stdio_driver_t g_display_backend_stdio_driver = {
    .out_chars = display_backend_stdio_out_chars,
    .out_flush = NULL,
    .in_chars = NULL,
#if PICO_SDK_VERSION_MAJOR >= 1 && PICO_SDK_VERSION_MINOR >= 4
    .next = NULL,
#endif
};

void display_backend_set_output_enabled(bool enabled)
{
    if (!g_display_backend_initialized) {
        return;
    }

    display_queue_reset();
    g_display_backend_output_enabled = enabled;
    stdio_set_driver_enabled(&g_display_backend_stdio_driver, enabled);
}

#if defined(PICO_LSI11_DISPLAY_BACKEND_ST7565)

#include "st7565_lcd/term_display.h"

bool display_backend_supported(void)
{
    return true;
}

const char *display_backend_name(void)
{
    return "st7565";
}

bool display_backend_init(void)
{
    st7565_term_display_init();
    if (!g_display_backend_queue_ready) {
        critical_section_init(&g_display_queue_lock);
        g_display_backend_queue_ready = true;
    }
    display_queue_reset();
    g_display_backend_initialized = true;
    g_display_backend_output_enabled = false;
    stdio_set_driver_enabled(&g_display_backend_stdio_driver, false);
    return true;
}

bool display_backend_show_boot_logo(void)
{
    if (!g_display_backend_initialized) {
        return false;
    }
    if (!st7565_term_display_show_mono_bmp("0:/bootlogo.bmp")) {
        st7565_term_display_clear();
        return false;
    }

    sleep_ms(2000);
    st7565_term_display_clear();
    return true;
}

void display_backend_task(void)
{
    char buf[DISPLAY_BACKEND_DRAIN_CHUNK];
    size_t len = 0u;

    if (!g_display_backend_initialized) {
        return;
    }

    while ((len = display_queue_pop_n(buf, sizeof(buf))) != 0u) {
        for (size_t i = 0; i < len; ++i) {
            st7565_term_display_putc(buf[i]);
        }
    }
}

#elif defined(PICO_LSI11_DISPLAY_BACKEND_ILI9486L)

#include "ili9486l.h"
#include "ili9486l_jpeg.h"
#include "vt100_terminal.h"

#include "ff.h"
#include "pico/time.h"

#include <limits.h>
#include <stdlib.h>

static vt100_terminal_t g_terminal;
static uint32_t g_last_terminal_tick_ms = 0u;

static void display_backend_ili_flush_batch(void)
{
    if (g_display_ili_batch_len == 0u) {
        return;
    }

    vt100_terminal_write_n(&g_terminal, g_display_ili_batch, g_display_ili_batch_len);
    g_display_ili_batch_len = 0u;
    g_display_ili_batch_started_ms = 0u;
}

static void display_backend_ili_append_batch(const char *buf, size_t len, uint32_t now_ms)
{
    size_t offset = 0u;

    while (offset < len) {
        const size_t available = sizeof(g_display_ili_batch) - g_display_ili_batch_len;
        const size_t chunk = (len - offset < available) ? (len - offset) : available;

        if (g_display_ili_batch_len == 0u) {
            g_display_ili_batch_started_ms = now_ms;
        }

        memcpy(&g_display_ili_batch[g_display_ili_batch_len], &buf[offset], chunk);
        g_display_ili_batch_len += chunk;
        offset += chunk;

        if (g_display_ili_batch_len == sizeof(g_display_ili_batch)) {
            display_backend_ili_flush_batch();
        }
    }
}

static bool display_backend_read_file(const char *path, uint8_t **data_out, size_t *size_out)
{
    FIL file;
    FRESULT fr;
    UINT br = 0;
    FSIZE_t file_size = 0;
    uint8_t *data = NULL;

    if (!path || !data_out || !size_out) {
        return false;
    }

    *data_out = NULL;
    *size_out = 0u;

    fr = f_open(&file, path, FA_READ);
    if (fr != FR_OK) {
        return false;
    }

    file_size = f_size(&file);
    if (file_size == 0u || file_size > SIZE_MAX || file_size > UINT_MAX) {
        (void)f_close(&file);
        return false;
    }

    data = (uint8_t *)malloc((size_t)file_size);
    if (!data) {
        (void)f_close(&file);
        return false;
    }

    fr = f_read(&file, data, (UINT)file_size, &br);
    (void)f_close(&file);
    if (fr != FR_OK || br != (UINT)file_size) {
        free(data);
        return false;
    }

    *data_out = data;
    *size_out = (size_t)file_size;
    return true;
}

bool display_backend_supported(void)
{
    return true;
}

const char *display_backend_name(void)
{
    return "ili9486l";
}

bool display_backend_init(void)
{
    uint16_t origin_y = 0;

    if (g_display_backend_initialized) {
        return true;
    }

    if (!g_display_backend_queue_ready) {
        critical_section_init(&g_display_queue_lock);
        g_display_backend_queue_ready = true;
    }
    display_queue_reset();

    ili9486l_init();

    origin_y = (uint16_t)((ili9486l_height() - VT100_TERMINAL_HEIGHT_PIXELS) / 2u);
    vt100_terminal_init(&g_terminal, 0u, origin_y);

    g_last_terminal_tick_ms = (uint32_t)to_ms_since_boot(get_absolute_time());
    g_display_backend_initialized = true;
    g_display_backend_output_enabled = false;
    stdio_set_driver_enabled(&g_display_backend_stdio_driver, false);
    return true;
}

bool display_backend_show_boot_logo(void)
{
    static const char bootlogo_path[] = "0:/bootlogo.jpg";
    uint8_t *jpeg_data = NULL;
    size_t jpeg_size = 0u;
    ili9486l_jpeg_info_t info;
    uint16_t x = 0u;
    uint16_t y = 0u;
    bool shown = false;

    if (!g_display_backend_initialized) {
        return false;
    }
    if (!display_backend_read_file(bootlogo_path, &jpeg_data, &jpeg_size)) {
        ili9486l_fill_screen(LCD_COLOR_BLACK);
        return false;
    }

    if (ili9486l_jpeg_get_info(jpeg_data, jpeg_size, &info)) {
        if (info.width < ili9486l_width()) {
            x = (uint16_t)((ili9486l_width() - info.width) / 2u);
        }
        if (info.height < ili9486l_height()) {
            y = (uint16_t)((ili9486l_height() - info.height) / 2u);
        }
    }

    ili9486l_fill_screen(LCD_COLOR_BLACK);
    shown = ili9486l_jpeg_draw(jpeg_data, jpeg_size, x, y);
    free(jpeg_data);
    if (shown) {
        sleep_ms(2000);
    }
    ili9486l_fill_screen(LCD_COLOR_BLACK);
    return shown;
}

void display_backend_task(void)
{
    char buf[DISPLAY_BACKEND_DRAIN_CHUNK];
    size_t len = 0u;
    const uint32_t now_ms = (uint32_t)to_ms_since_boot(get_absolute_time());
    const uint32_t elapsed_ms = now_ms - g_last_terminal_tick_ms;

    if (!g_display_backend_initialized) {
        return;
    }

    while ((len = display_queue_pop_n(buf, sizeof(buf))) != 0u) {
        display_backend_ili_append_batch(buf, len, now_ms);
    }

    if (g_display_ili_batch_len > 0u &&
            (now_ms - g_display_ili_batch_started_ms) >= DISPLAY_BACKEND_ILI_BATCH_DELAY_MS) {
        display_backend_ili_flush_batch();
    }

    if (elapsed_ms != 0u) {
        g_last_terminal_tick_ms = now_ms;
        vt100_terminal_tick(&g_terminal, elapsed_ms);
    }
}

#else

bool display_backend_supported(void)
{
    return false;
}

const char *display_backend_name(void)
{
    return "none";
}

bool display_backend_init(void)
{
    return false;
}

bool display_backend_show_boot_logo(void)
{
    return false;
}

void display_backend_task(void)
{
}

#endif
