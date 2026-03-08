#include "display_backend.h"

#include "pico/critical_section.h"
#include "pico/stdio/driver.h"

#define DISPLAY_BACKEND_QUEUE_CAPACITY 4096u
#define DISPLAY_BACKEND_DRAIN_CHUNK 128u

static critical_section_t g_display_queue_lock;
static char g_display_queue[DISPLAY_BACKEND_QUEUE_CAPACITY];
static uint16_t g_display_queue_head = 0u;
static uint16_t g_display_queue_tail = 0u;
static uint16_t g_display_queue_count = 0u;
static bool g_display_backend_initialized = false;
static bool g_display_backend_queue_ready = false;

static bool display_queue_try_push(char ch)
{
    bool pushed = false;

    if (!g_display_backend_initialized) {
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

static void display_backend_stdio_out_chars(const char *buf, int len)
{
    if (!buf || len <= 0) {
        return;
    }

    for (int i = 0; i < len; ++i) {
        (void)display_queue_try_push(buf[i]);
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
    g_display_queue_head = 0u;
    g_display_queue_tail = 0u;
    g_display_queue_count = 0u;
    g_display_backend_initialized = true;
    stdio_set_driver_enabled(&g_display_backend_stdio_driver, true);
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
#include "vt100_terminal.h"

#include "pico/time.h"

static vt100_terminal_t g_terminal;
static uint32_t g_last_terminal_tick_ms = 0u;

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
    g_display_queue_head = 0u;
    g_display_queue_tail = 0u;
    g_display_queue_count = 0u;

    ili9486l_init();

    origin_y = (uint16_t)((ili9486l_height() - VT100_TERMINAL_HEIGHT_PIXELS) / 2u);
    vt100_terminal_init(&g_terminal, 0u, origin_y);

    g_last_terminal_tick_ms = (uint32_t)to_ms_since_boot(get_absolute_time());
    g_display_backend_initialized = true;
    stdio_set_driver_enabled(&g_display_backend_stdio_driver, true);
    return true;
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
        vt100_terminal_write_n(&g_terminal, buf, len);
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

void display_backend_task(void)
{
}

#endif
