#include "display_backend.h"

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
    return true;
}

void display_backend_task(void)
{
}

#elif defined(PICO_LSI11_DISPLAY_BACKEND_ILI9486L)

#include "ili9486l.h"
#include "vt100_terminal.h"

#include "pico/critical_section.h"
#include "pico/stdio/driver.h"
#include "pico/time.h"

static vt100_terminal_t g_terminal;
static critical_section_t g_terminal_lock;
static bool g_term_display_initialized = false;
static uint32_t g_last_terminal_tick_ms = 0u;

static void ili9486l_stdio_out_chars(const char *buf, int len)
{
    if (!g_term_display_initialized || buf == NULL || len <= 0) {
        return;
    }

    critical_section_enter_blocking(&g_terminal_lock);
    for (int i = 0; i < len; ++i) {
        vt100_terminal_putc(&g_terminal, buf[i]);
    }
    critical_section_exit(&g_terminal_lock);
}

static stdio_driver_t g_ili9486l_stdio_driver = {
    .out_chars = ili9486l_stdio_out_chars,
    .out_flush = NULL,
    .in_chars = NULL,
#if PICO_SDK_VERSION_MAJOR >= 1 && PICO_SDK_VERSION_MINOR >= 4
    .next = NULL,
#endif
};

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

    if (g_term_display_initialized) {
        return true;
    }

    critical_section_init(&g_terminal_lock);
    ili9486l_init();

    origin_y = (uint16_t)((ili9486l_height() - VT100_TERMINAL_HEIGHT_PIXELS) / 2u);
    vt100_terminal_init(&g_terminal, 0u, origin_y);

    g_last_terminal_tick_ms = (uint32_t)to_ms_since_boot(get_absolute_time());
    g_term_display_initialized = true;
    stdio_set_driver_enabled(&g_ili9486l_stdio_driver, true);
    return true;
}

void display_backend_task(void)
{
    const uint32_t now_ms = (uint32_t)to_ms_since_boot(get_absolute_time());
    const uint32_t elapsed_ms = now_ms - g_last_terminal_tick_ms;

    if (!g_term_display_initialized || elapsed_ms == 0u) {
        return;
    }

    g_last_terminal_tick_ms = now_ms;
    critical_section_enter_blocking(&g_terminal_lock);
    vt100_terminal_tick(&g_terminal, elapsed_ms);
    critical_section_exit(&g_terminal_lock);
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
