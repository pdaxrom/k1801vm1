#include "vt100_terminal_console.h"
#include "vt100_terminal_internal.h"

#include "ili9486l.h"

#include <stdio.h>
#include <string.h>

#define STATUS_ROW ((uint8_t)(VT100_TERMINAL_ROWS - 1u))
#ifndef VT100_TERMINAL_CONSOLE_PENDING_QUEUE_SIZE
#define VT100_TERMINAL_CONSOLE_PENDING_QUEUE_SIZE 512u
#endif

enum {
    SIZE_80X34 = 0,
    SIZE_80X30,
    SIZE_80X24,
};

enum {
    MODE_SCROLL = 0,
    MODE_PAGED = 1,
};

typedef struct {
    vt100_terminal_t *t;
    uint16_t head;
    uint16_t tail;
    uint16_t count;
    uint8_t size;
    uint8_t rows;
    uint8_t mode;
    bool in_use;
    bool dirty;
    bool wait_page;
    bool cmd;
    bool overflow;
    bool remote_paused;
    bool last_vt52;
    uint8_t q[VT100_TERMINAL_CONSOLE_PENDING_QUEUE_SIZE];
} console_t;

static console_t g;

static console_t *find(const vt100_terminal_t *t)
{
    return (t != NULL && g.in_use && g.t == t) ? &g : NULL;
}

static uint8_t rows_for(uint8_t size)
{
    switch (size) {
    case SIZE_80X30:
        return 30u;
    case SIZE_80X24:
        return 24u;
    case SIZE_80X34:
    default:
        return 34u;
    }
}

static const char *size_str(uint8_t size)
{
    switch (size) {
    case SIZE_80X30:
        return "80x30";
    case SIZE_80X24:
        return "80x24";
    case SIZE_80X34:
    default:
        return "80x34";
    }
}

static const char *mode_str(uint8_t mode)
{
    return mode == MODE_PAGED ? "PAGED" : "SCROLL";
}

static uint8_t attr(uint8_t fg, uint8_t bg)
{
    return (uint8_t)((((bg) & 0x0Fu) << 4) | ((fg) & 0x0Fu));
}

static void fill(vt100_terminal_t *t, uint8_t top, uint8_t bottom, char ch, uint8_t a, uint8_t s, uint8_t cs)
{
    if (top > bottom || bottom >= VT100_TERMINAL_ROWS) {
        return;
    }

    for (uint8_t row = top; row <= bottom; ++row) {
        for (uint8_t col = 0; col < VT100_TERMINAL_COLS; ++col) {
            t->cells[row][col].ch = ch;
            t->cells[row][col].attr = a;
            t->cells[row][col].style = s;
            t->cells[row][col].charset = cs;
        }
    }
}

static void q_reset(console_t *c)
{
    c->head = 0u;
    c->tail = 0u;
    c->count = 0u;
    c->overflow = false;
}

static void flow(console_t *c, bool pause)
{
    static const char k_xoff = '\x13';
    static const char k_xon = '\x11';
    vt100_terminal_t *t = c->t;

    if (c->remote_paused == pause) {
        return;
    }

    c->remote_paused = pause;
    if (t == NULL || t->output_fn == NULL) {
        return;
    }

    t->output_fn(pause ? &k_xoff : &k_xon, 1u, t->output_user_data);
}

static bool q_push(console_t *c, char ch)
{
    if (c->count >= VT100_TERMINAL_CONSOLE_PENDING_QUEUE_SIZE) {
        c->overflow = true;
        c->dirty = true;
        return false;
    }

    c->q[c->tail] = (uint8_t)ch;
    c->tail = (uint16_t)((c->tail + 1u) % VT100_TERMINAL_CONSOLE_PENDING_QUEUE_SIZE);
    ++c->count;
    return true;
}

static bool q_pop(console_t *c, char *out)
{
    if (c->count == 0u || out == NULL) {
        return false;
    }

    *out = (char)c->q[c->head];
    c->head = (uint16_t)((c->head + 1u) % VT100_TERMINAL_CONSOLE_PENDING_QUEUE_SIZE);
    --c->count;
    return true;
}

static void status(console_t *c)
{
    char line[81];
    char buf[81];
    vt100_terminal_t *t = c->t;
    const uint16_t x = t->origin_x;
    const uint16_t y = (uint16_t)(t->origin_y + STATUS_ROW * VT100_TERMINAL_CELL_HEIGHT);
    const lcd_color_t bg = c->cmd ? LCD_RGB666(0x08, 0x12, 0x18) : LCD_RGB666(0x04, 0x09, 0x10);
    const lcd_color_t fg = (c->wait_page || c->cmd) ? LCD_COLOR_YELLOW : LCD_COLOR_WHITE;
    size_t n = 0u;

    if (!c->dirty && c->last_vt52 == t->vt52_mode) {
        return;
    }

    c->last_vt52 = t->vt52_mode;
    if (c->cmd) {
        snprintf(line, sizeof(line), " %s %s %s  CMD 1/2/3 SIZE  S/P MODE",
                 t->vt52_mode ? "VT52" : "ANSI", size_str(c->size), mode_str(c->mode));
    } else if (c->wait_page) {
        snprintf(line, sizeof(line), " %s %s %s  SPACE NEXT PAGE%s",
                 t->vt52_mode ? "VT52" : "ANSI", size_str(c->size), mode_str(c->mode),
                 c->overflow ? "  INPUT DROPPED" : "");
    } else {
        snprintf(line, sizeof(line), " %s %s %s  ^E 1/2/3 SIZE  S/P MODE",
                 t->vt52_mode ? "VT52" : "ANSI", size_str(c->size), mode_str(c->mode));
    }

    memset(buf, ' ', VT100_TERMINAL_COLS);
    buf[VT100_TERMINAL_COLS] = '\0';
    n = strlen(line);
    if (n > VT100_TERMINAL_COLS) {
        n = VT100_TERMINAL_COLS;
    }
    memcpy(buf, line, n);

    ili9486l_fill_rect(x, y, VT100_TERMINAL_WIDTH_PIXELS, VT100_TERMINAL_CELL_HEIGHT, bg);
    ili9486l_draw_string(x, (uint16_t)(y + 1u), buf, fg, bg, 1u);
    c->dirty = false;
}

static void layout(console_t *c, bool clear)
{
    vt100_terminal_t *t = c->t;
    const uint8_t blank = attr(t->default_fg, t->default_bg);

    c->rows = rows_for(c->size);
    t->scroll_top = 0u;
    t->scroll_bottom = (uint8_t)(c->rows - 1u);

    if (clear) {
        const uint8_t cur = attr(t->fg, t->bg);
        fill(t, 0u, (uint8_t)(c->rows - 1u), ' ', cur, t->style, 0u);
        t->cursor_row = 0u;
        t->cursor_col = 0u;
        t->wrap_pending = false;
    } else if (t->cursor_row >= c->rows) {
        t->cursor_row = (uint8_t)(c->rows - 1u);
    }

    if (c->rows < STATUS_ROW) {
        fill(t, c->rows, (uint8_t)(STATUS_ROW - 1u), ' ', blank, 0u, 0u);
    }
    fill(t, STATUS_ROW, STATUS_ROW, ' ', blank, 0u, 0u);
    c->dirty = true;
}

static void sanitize(console_t *c)
{
    vt100_terminal_t *t = c->t;

    if (t->state != 0u) {
        return;
    }
    if (t->scroll_top >= c->rows || t->scroll_bottom >= c->rows || t->scroll_top > t->scroll_bottom) {
        t->scroll_top = 0u;
        t->scroll_bottom = (uint8_t)(c->rows - 1u);
    }
    if (t->cursor_row >= c->rows) {
        t->cursor_row = (uint8_t)(c->rows - 1u);
    }
    if (t->saved_row >= c->rows) {
        t->saved_row = (uint8_t)(c->rows - 1u);
    }
}

static bool pause_for_page(const console_t *c, char ch)
{
    const vt100_terminal_t *t = c->t;
    const unsigned char u = (unsigned char)ch;

    if (c->mode != MODE_PAGED || c->wait_page || t->state != 0u) {
        return false;
    }
    if (u == '\n' || u == '\v' || u == '\f') {
        return t->cursor_row == (uint8_t)(c->rows - 1u);
    }
    if (u == '\t' || (u >= 0x20u && u <= 0x7Eu)) {
        return t->wrap_pending && t->cursor_row == (uint8_t)(c->rows - 1u);
    }
    return false;
}

static void feed_remote(console_t *c, char ch)
{
    vt100_terminal_t *t = c->t;
    const uint8_t old_state = t->state;
    const bool old_vt52 = t->vt52_mode;

    if (pause_for_page(c, ch)) {
        flow(c, true);
        c->wait_page = true;
        c->dirty = true;
        (void)q_push(c, ch);
        return;
    }

    vt100_terminal_core_putc(t, ch);
    sanitize(c);

    if (old_state != 0u || old_vt52 != t->vt52_mode || (unsigned char)ch < 0x20u || ch == '\x1b') {
        c->dirty = true;
    }
}

static void poll(console_t *c)
{
    char ch = '\0';
    uint16_t n = 0u;

    while (!c->wait_page && n < 128u && q_pop(c, &ch)) {
        feed_remote(c, ch);
        ++n;
    }
    status(c);
}

static void apply_size(console_t *c, uint8_t size)
{
    if (size > SIZE_80X24) {
        return;
    }
    c->size = size;
    flow(c, false);
    c->wait_page = false;
    c->cmd = false;
    q_reset(c);
    layout(c, true);
    vt100_terminal_core_render(c->t);
    status(c);
}

static void apply_mode(console_t *c, uint8_t mode)
{
    c->mode = mode;
    c->cmd = false;
    c->dirty = true;
    if (mode == MODE_SCROLL && c->wait_page) {
        flow(c, false);
        c->wait_page = false;
        poll(c);
        return;
    }
    if (mode == MODE_SCROLL) {
        flow(c, false);
    }
    status(c);
}

static void next_page(console_t *c)
{
    flow(c, false);
    c->wait_page = false;
    c->overflow = false;
    c->cmd = false;
    layout(c, true);
    vt100_terminal_core_render(c->t);
    status(c);
}

static bool local(console_t *c, char ch)
{
    if ((unsigned char)ch == 0x05u && c->t->state == 0u) {
        c->cmd = true;
        c->dirty = true;
        status(c);
        return true;
    }

    if (c->cmd) {
        switch (ch) {
        case '1': apply_size(c, SIZE_80X34); return true;
        case '2': apply_size(c, SIZE_80X30); return true;
        case '3': apply_size(c, SIZE_80X24); return true;
        case 's':
        case 'S': apply_mode(c, MODE_SCROLL); return true;
        case 'p':
        case 'P': apply_mode(c, MODE_PAGED); return true;
        default:
            c->cmd = false;
            c->dirty = true;
            status(c);
            return true;
        }
    }

    if (c->wait_page) {
        if (ch == ' ') {
            next_page(c);
        }
        return true;
    }

    return false;
}

static void write_buf(console_t *c, const char *text, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        if (c->wait_page) {
            (void)q_push(c, text[i]);
        } else {
            feed_remote(c, text[i]);
        }
    }
    status(c);
}

bool vt100_terminal_console_attach(vt100_terminal_t *terminal)
{
    if (find(terminal) != NULL) {
        return true;
    }
    if (terminal == NULL || g.in_use) {
        return false;
    }

    memset(&g, 0, sizeof(g));
    g.in_use = true;
    g.t = terminal;
    g.size = SIZE_80X34;
    g.mode = MODE_SCROLL;
    layout(&g, false);
    vt100_terminal_core_render(terminal);
    g.last_vt52 = terminal->vt52_mode;
    status(&g);
    return true;
}

bool vt100_terminal_console_is_attached(const vt100_terminal_t *terminal)
{
    return find(terminal) != NULL;
}

bool vt100_terminal_console_getch(vt100_terminal_t *terminal, int ch)
{
    console_t *c = NULL;

    if (terminal == NULL || !vt100_terminal_console_attach(terminal)) {
        return false;
    }

    c = find(terminal);
    if (c == NULL) {
        return false;
    }
    if (ch < 0) {
        poll(c);
        return false;
    }
    if (local(c, (char)ch)) {
        return false;
    }
    if (terminal->getch_hook != NULL && terminal->getch_hook(terminal, (char)ch, terminal->getch_hook_user_data)) {
        return false;
    }

    feed_remote(c, (char)ch);
    return true;
}

bool vt100_terminal_console_write_n(vt100_terminal_t *terminal, const char *text, size_t len)
{
    console_t *c = find(terminal);

    if (c == NULL || text == NULL || len == 0u) {
        return false;
    }

    write_buf(c, text, len);
    return true;
}

void vt100_terminal_console_tick(vt100_terminal_t *terminal, uint32_t elapsed_ms)
{
    console_t *c = find(terminal);

    if (c == NULL) {
        return;
    }

    vt100_terminal_core_tick(c->t, elapsed_ms);
    status(c);
}

void vt100_terminal_console_render(vt100_terminal_t *terminal)
{
    console_t *c = find(terminal);

    if (c == NULL) {
        return;
    }

    vt100_terminal_core_render(c->t);
    c->dirty = true;
    status(c);
}
