#include "vt100_terminal.h"

#include "font5x7.h"
#include "ili9486l.h"

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

enum {
    VT100_STATE_GROUND = 0,
    VT100_STATE_ESC,
    VT100_STATE_CSI,
    VT100_STATE_ESC_G0,
    VT100_STATE_ESC_G1,
    VT100_STATE_ESC_G2,
    VT100_STATE_ESC_G3,
    VT100_STATE_ESC_HASH,
    VT100_STATE_OSC,
    VT100_STATE_OSC_ESC,
    VT100_STATE_STR,
    VT100_STATE_STR_ESC,
    VT100_STATE_VT52_CURSOR_ROW,
    VT100_STATE_VT52_CURSOR_COL,
};

enum {
    VT100_STYLE_BOLD = 1u << 0,
    VT100_STYLE_FAINT = 1u << 1,
    VT100_STYLE_UNDERLINE = 1u << 2,
    VT100_STYLE_BLINK = 1u << 3,
    VT100_STYLE_REVERSE = 1u << 4,
    VT100_STYLE_INVISIBLE = 1u << 5,
};

enum {
    VT100_CHARSET_US = 0,
    VT100_CHARSET_UK,
    VT100_CHARSET_DEC_SPECIAL,
};

#define VT100_ATTR(fg, bg) (uint8_t)((((bg) & 0x0Fu) << 4) | ((fg) & 0x0Fu))
#define VT100_ATTR_FG(attr) (uint8_t)((attr) & 0x0Fu)
#define VT100_ATTR_BG(attr) (uint8_t)(((attr) >> 4) & 0x0Fu)

static const uint8_t k_vt100_palette[16][3] = {
    {0u, 0u, 0u},
    {42u, 0u, 0u},
    {0u, 42u, 0u},
    {42u, 42u, 0u},
    {0u, 0u, 42u},
    {42u, 0u, 42u},
    {0u, 42u, 42u},
    {48u, 48u, 48u},
    {24u, 24u, 24u},
    {63u, 0u, 0u},
    {0u, 63u, 0u},
    {63u, 63u, 0u},
    {0u, 0u, 63u},
    {63u, 0u, 63u},
    {0u, 63u, 63u},
    {63u, 63u, 63u},
};

typedef struct {
    const uint8_t *row_masks;
    uint8_t fg[3];
    uint8_t bg[3];
    bool underline;
} vt100_terminal_render_cache_t;

static uint8_t g_scanline_buffer[VT100_TERMINAL_WIDTH_PIXELS * 3u];
static vt100_terminal_render_cache_t g_render_row_cache[VT100_TERMINAL_COLS];
static void vt100_terminal_render_row(vt100_terminal_t *terminal, uint8_t row);
static void vt100_terminal_render_row_range(vt100_terminal_t *terminal, uint8_t row, uint8_t col_start,
        uint8_t col_end);
static void vt100_terminal_scroll_up_region(vt100_terminal_t *terminal, uint8_t top, uint8_t bottom);
static void vt100_terminal_scroll_down_region(vt100_terminal_t *terminal, uint8_t top, uint8_t bottom);
static void vt100_terminal_commit_wrap(vt100_terminal_t *terminal);
static void vt100_terminal_advance(vt100_terminal_t *terminal);
static uint8_t vt100_terminal_row_base(const vt100_terminal_t *terminal);

static uint8_t vt100_terminal_current_attr(const vt100_terminal_t *terminal)
{
    return VT100_ATTR(terminal->fg, terminal->bg);
}

static uint8_t vt100_terminal_current_style(const vt100_terminal_t *terminal)
{
    return terminal->style;
}

static uint8_t vt100_terminal_normal_charset(const vt100_terminal_t *terminal)
{
    if (terminal->vt52_mode) {
        return terminal->vt52_graphics ? VT100_CHARSET_DEC_SPECIAL : terminal->g0_charset;
    }

    return terminal->gl_set == 0u ? terminal->g0_charset : terminal->g1_charset;
}

static uint8_t vt100_terminal_current_charset(const vt100_terminal_t *terminal)
{
    if (terminal->single_shift_pending && !terminal->vt52_mode) {
        return terminal->single_shift_charset;
    }

    return vt100_terminal_normal_charset(terminal);
}

static uint8_t vt100_terminal_charset_from_designator(char ch)
{
    switch (ch) {
    case 'A':
        return VT100_CHARSET_UK;
    case '0':
    case '2':
        return VT100_CHARSET_DEC_SPECIAL;
    case '1':
    case 'B':
    default:
        return VT100_CHARSET_US;
    }
}

static uint8_t vt100_terminal_sanitize_char(char ch)
{
    if ((unsigned char)ch < 0x20u || (unsigned char)ch > 0x7Eu) {
        return '?';
    }

    return (uint8_t)ch;
}

static void vt100_terminal_set_cell(vt100_terminal_t *terminal, uint8_t row, uint8_t col, char ch, uint8_t attr,
                                    uint8_t style, uint8_t charset)
{
    terminal->cells[row][col].ch = ch;
    terminal->cells[row][col].attr = attr;
    terminal->cells[row][col].style = style;
    terminal->cells[row][col].charset = charset;
}

static void vt100_terminal_fill_range(vt100_terminal_t *terminal, uint8_t row, uint8_t col_start, uint8_t col_end,
                                      uint8_t attr, uint8_t style)
{
    if (col_start > col_end) {
        return;
    }

    for (uint8_t col = col_start; col <= col_end; ++col) {
        vt100_terminal_set_cell(terminal, row, col, ' ', attr, style, VT100_CHARSET_US);
    }
}

static vt100_terminal_cell_t vt100_terminal_blank_cell(const vt100_terminal_t *terminal)
{
    vt100_terminal_cell_t cell;

    cell.ch = ' ';
    cell.attr = vt100_terminal_current_attr(terminal);
    cell.style = vt100_terminal_current_style(terminal);
    cell.charset = vt100_terminal_normal_charset(terminal);
    return cell;
}

static void vt100_terminal_reset_tab_stops(vt100_terminal_t *terminal)
{
    for (uint8_t col = 0; col < VT100_TERMINAL_COLS; ++col) {
        terminal->tab_stops[col] = (col != 0u) && ((col % 8u) == 0u);
    }
}

static uint8_t vt100_terminal_find_next_tab_stop(const vt100_terminal_t *terminal, uint8_t col)
{
    for (uint8_t next = (uint8_t)(col + 1u); next < VT100_TERMINAL_COLS; ++next) {
        if (terminal->tab_stops[next]) {
            return next;
        }
    }

    return (uint8_t)(VT100_TERMINAL_COLS - 1u);
}

static uint8_t vt100_terminal_find_prev_tab_stop(const vt100_terminal_t *terminal, uint8_t col)
{
    while (col > 0u) {
        --col;
        if (terminal->tab_stops[col]) {
            return col;
        }
    }

    return 0u;
}

static uint8_t vt100_terminal_row_base(const vt100_terminal_t *terminal)
{
    return terminal->origin_mode ? terminal->scroll_top : 0u;
}

static uint8_t vt100_terminal_row_limit(const vt100_terminal_t *terminal)
{
    return terminal->origin_mode ? terminal->scroll_bottom : (uint8_t)(VT100_TERMINAL_ROWS - 1u);
}

static void vt100_terminal_home_cursor(vt100_terminal_t *terminal)
{
    terminal->cursor_row = vt100_terminal_row_base(terminal);
    terminal->cursor_col = 0u;
    terminal->wrap_pending = false;
}

static void vt100_terminal_reset_parser(vt100_terminal_t *terminal)
{
    terminal->state = VT100_STATE_GROUND;
    terminal->csi_param_count = 0u;
    terminal->csi_have_value = 0u;
    terminal->csi_private = 0u;
    terminal->csi_value = 0u;
}

static uint8_t vt100_terminal_decode_vt52_coord(char ch, uint8_t limit)
{
    const uint8_t value = (uint8_t)(((unsigned char)ch >= 0x20u) ? ((unsigned char)ch - 0x20u) : 0u);
    return value > limit ? limit : value;
}

static void vt100_terminal_apply_vt52_cursor_address(vt100_terminal_t *terminal, uint8_t row, char col_ch)
{
    terminal->wrap_pending = false;
    terminal->cursor_row = row;
    terminal->cursor_col = vt100_terminal_decode_vt52_coord(col_ch, (uint8_t)(VT100_TERMINAL_COLS - 1u));
}

static void vt100_terminal_apply_cursor_address(vt100_terminal_t *terminal, uint16_t row_param, uint16_t col_param)
{
    const uint8_t row_base = vt100_terminal_row_base(terminal);
    const uint8_t row_limit = vt100_terminal_row_limit(terminal);
    const uint8_t row_span = (uint8_t)(row_limit - row_base + 1u);

    terminal->wrap_pending = false;
    terminal->cursor_row = (uint8_t)(row_base + ((row_param == 0u
                                     || row_param > row_span) ? (row_span - 1u) : (row_param - 1u)));
    terminal->cursor_col = (uint8_t)((col_param == 0u
                                      || col_param > VT100_TERMINAL_COLS) ? (VT100_TERMINAL_COLS - 1u) : (col_param - 1u));
}

static void vt100_terminal_apply_row_address(vt100_terminal_t *terminal, uint16_t row_param)
{
    const uint8_t row_base = vt100_terminal_row_base(terminal);
    const uint8_t row_limit = vt100_terminal_row_limit(terminal);
    const uint8_t row_span = (uint8_t)(row_limit - row_base + 1u);

    terminal->wrap_pending = false;
    terminal->cursor_row = (uint8_t)(row_base + ((row_param == 0u
                                     || row_param > row_span) ? (row_span - 1u) : (row_param - 1u)));
}

static void vt100_terminal_render_rows(vt100_terminal_t *terminal, uint8_t top, uint8_t bottom)
{
    if (top > bottom || bottom >= VT100_TERMINAL_ROWS) {
        return;
    }

    for (uint8_t row = top; row <= bottom; ++row) {
        vt100_terminal_render_row(terminal, row);
    }
}

static void vt100_terminal_scroll_region_up(vt100_terminal_t *terminal, uint8_t count)
{
    if (count == 0u) {
        return;
    }

    if (count > (uint8_t)(terminal->scroll_bottom - terminal->scroll_top + 1u)) {
        count = (uint8_t)(terminal->scroll_bottom - terminal->scroll_top + 1u);
    }

    for (uint8_t i = 0; i < count; ++i) {
        vt100_terminal_scroll_up_region(terminal, terminal->scroll_top, terminal->scroll_bottom);
    }
}

static void vt100_terminal_scroll_region_down(vt100_terminal_t *terminal, uint8_t count)
{
    if (count == 0u) {
        return;
    }

    if (count > (uint8_t)(terminal->scroll_bottom - terminal->scroll_top + 1u)) {
        count = (uint8_t)(terminal->scroll_bottom - terminal->scroll_top + 1u);
    }

    for (uint8_t i = 0; i < count; ++i) {
        vt100_terminal_scroll_down_region(terminal, terminal->scroll_top, terminal->scroll_bottom);
    }
}

static void vt100_terminal_alignment_display(vt100_terminal_t *terminal)
{
    const uint8_t attr = vt100_terminal_current_attr(terminal);
    const uint8_t style = vt100_terminal_current_style(terminal);

    for (uint8_t row = 0; row < VT100_TERMINAL_ROWS; ++row) {
        for (uint8_t col = 0; col < VT100_TERMINAL_COLS; ++col) {
            vt100_terminal_set_cell(terminal, row, col, 'E', attr, style, VT100_CHARSET_US);
        }
    }

    vt100_terminal_home_cursor(terminal);
    vt100_terminal_render(terminal);
}

static void vt100_terminal_copy_color(uint8_t dst[3], const uint8_t src[3])
{
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
}

static void vt100_terminal_pack_wire_color(uint8_t color[3])
{
    color[0] = (uint8_t)((color[0] & 0x3Fu) << 2);
    color[1] = (uint8_t)((color[1] & 0x3Fu) << 2);
    color[2] = (uint8_t)((color[2] & 0x3Fu) << 2);
}

static void vt100_terminal_swap_colors(uint8_t first[3], uint8_t second[3])
{
    for (uint8_t i = 0; i < 3u; ++i) {
        const uint8_t tmp = first[i];
        first[i] = second[i];
        second[i] = tmp;
    }
}

static void vt100_terminal_dim_color(uint8_t color[3])
{
    color[0] >>= 1;
    color[1] >>= 1;
    color[2] >>= 1;
}

static bool vt100_terminal_row_has_blink(const vt100_terminal_t *terminal, uint8_t row)
{
    for (uint8_t col = 0; col < VT100_TERMINAL_COLS; ++col) {
        if ((terminal->cells[row][col].style & VT100_STYLE_BLINK) != 0u) {
            return true;
        }
    }

    return false;
}

static void vt100_terminal_emit(vt100_terminal_t *terminal, const char *data, size_t len)
{
    if (terminal->output_fn == NULL || data == NULL || len == 0u) {
        return;
    }

    terminal->output_fn(data, len, terminal->output_user_data);
}

static void vt100_terminal_emit_device_attributes(vt100_terminal_t *terminal)
{
    static const char response[] = "\x1b[?1;0c";
    vt100_terminal_emit(terminal, response, sizeof(response) - 1u);
}

static void vt100_terminal_emit_vt52_ident(vt100_terminal_t *terminal)
{
    static const char response[] = "\x1b/Z";
    vt100_terminal_emit(terminal, response, sizeof(response) - 1u);
}

static void vt100_terminal_emit_device_status(vt100_terminal_t *terminal, uint16_t status_code)
{
    char response[16];
    const int len = snprintf(response, sizeof(response), "\x1b[%un", (unsigned)status_code);

    if (len > 0) {
        vt100_terminal_emit(terminal, response, (size_t)len);
    }
}

static void vt100_terminal_emit_cursor_position(vt100_terminal_t *terminal)
{
    char response[24];
    const int len = snprintf(
                        response,
                        sizeof(response),
                        "\x1b[%u;%uR",
                        (unsigned)(terminal->cursor_row - vt100_terminal_row_base(terminal) + 1u),
                        (unsigned)(terminal->cursor_col + 1u));

    if (len > 0) {
        vt100_terminal_emit(terminal, response, (size_t)len);
    }
}

static const uint8_t *vt100_terminal_get_uk_row_masks(char ch)
{
    static const uint8_t k_uk_hash_row_masks[VT100_TERMINAL_CELL_HEIGHT] = {0x00, 0x0C, 0x12, 0x02, 0x07, 0x02, 0x1F, 0x02, 0x00};

    return ch == '#' ? k_uk_hash_row_masks : NULL;
}

static const uint8_t *vt100_terminal_get_dec_special_row_masks(char ch)
{
    static const uint8_t k_dec_diamond_row_masks[VT100_TERMINAL_CELL_HEIGHT] = {0x00, 0x00, 0x04, 0x0E, 0x1F, 0x0E, 0x04, 0x00, 0x00};
    static const uint8_t k_dec_checkerboard_row_masks[VT100_TERMINAL_CELL_HEIGHT] = {0x00, 0x15, 0x0A, 0x15, 0x0A, 0x15, 0x0A, 0x15, 0x00};
    static const uint8_t k_dec_degree_row_masks[VT100_TERMINAL_CELL_HEIGHT] = {0x00, 0x0C, 0x12, 0x12, 0x0C, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t k_dec_plus_minus_row_masks[VT100_TERMINAL_CELL_HEIGHT] = {0x00, 0x00, 0x04, 0x1F, 0x04, 0x00, 0x1F, 0x00, 0x00};
    static const uint8_t k_dec_lower_right_row_masks[VT100_TERMINAL_CELL_HEIGHT] = {0x00, 0x04, 0x04, 0x04, 0x07, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t k_dec_upper_right_row_masks[VT100_TERMINAL_CELL_HEIGHT] = {0x00, 0x00, 0x00, 0x00, 0x07, 0x04, 0x04, 0x04, 0x00};
    static const uint8_t k_dec_upper_left_row_masks[VT100_TERMINAL_CELL_HEIGHT] = {0x00, 0x00, 0x00, 0x00, 0x1C, 0x04, 0x04, 0x04, 0x00};
    static const uint8_t k_dec_lower_left_row_masks[VT100_TERMINAL_CELL_HEIGHT] = {0x00, 0x04, 0x04, 0x04, 0x1C, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t k_dec_cross_row_masks[VT100_TERMINAL_CELL_HEIGHT] = {0x00, 0x04, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x04, 0x00};
    static const uint8_t k_dec_scan1_row_masks[VT100_TERMINAL_CELL_HEIGHT] = {0x00, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t k_dec_scan3_row_masks[VT100_TERMINAL_CELL_HEIGHT] = {0x00, 0x00, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t k_dec_scan5_row_masks[VT100_TERMINAL_CELL_HEIGHT] = {0x00, 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t k_dec_scan7_row_masks[VT100_TERMINAL_CELL_HEIGHT] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
    static const uint8_t k_dec_scan9_row_masks[VT100_TERMINAL_CELL_HEIGHT] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0x00};
    static const uint8_t k_dec_left_tee_row_masks[VT100_TERMINAL_CELL_HEIGHT] = {0x00, 0x04, 0x04, 0x04, 0x1C, 0x04, 0x04, 0x04, 0x00};
    static const uint8_t k_dec_right_tee_row_masks[VT100_TERMINAL_CELL_HEIGHT] = {0x00, 0x04, 0x04, 0x04, 0x07, 0x04, 0x04, 0x04, 0x00};
    static const uint8_t k_dec_bottom_tee_row_masks[VT100_TERMINAL_CELL_HEIGHT] = {0x00, 0x04, 0x04, 0x04, 0x1F, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t k_dec_top_tee_row_masks[VT100_TERMINAL_CELL_HEIGHT] = {0x00, 0x00, 0x00, 0x00, 0x1F, 0x04, 0x04, 0x04, 0x00};
    static const uint8_t k_dec_vertical_bar_row_masks[VT100_TERMINAL_CELL_HEIGHT] = {0x00, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00};
    static const uint8_t k_dec_less_equal_row_masks[VT100_TERMINAL_CELL_HEIGHT] = {0x00, 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x1F, 0x00};
    static const uint8_t k_dec_greater_equal_row_masks[VT100_TERMINAL_CELL_HEIGHT] = {0x00, 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x1F, 0x00};
    static const uint8_t k_dec_pi_row_masks[VT100_TERMINAL_CELL_HEIGHT] = {0x00, 0x1F, 0x04, 0x04, 0x04, 0x04, 0x05, 0x02, 0x00};
    static const uint8_t k_dec_not_equal_row_masks[VT100_TERMINAL_CELL_HEIGHT] = {0x00, 0x00, 0x10, 0x1F, 0x08, 0x1F, 0x01, 0x00, 0x00};
    static const uint8_t k_dec_sterling_row_masks[VT100_TERMINAL_CELL_HEIGHT] = {0x00, 0x0E, 0x11, 0x01, 0x0F, 0x01, 0x01, 0x1F, 0x00};
    static const uint8_t k_dec_bullet_row_masks[VT100_TERMINAL_CELL_HEIGHT] = {0x00, 0x00, 0x00, 0x04, 0x0E, 0x0E, 0x04, 0x00, 0x00};

    switch (ch) {
    case '`':
        return k_dec_diamond_row_masks;
    case 'a':
        return k_dec_checkerboard_row_masks;
    case 'f':
        return k_dec_degree_row_masks;
    case 'g':
        return k_dec_plus_minus_row_masks;
    case 'j':
        return k_dec_lower_right_row_masks;
    case 'k':
        return k_dec_upper_right_row_masks;
    case 'l':
        return k_dec_upper_left_row_masks;
    case 'm':
        return k_dec_lower_left_row_masks;
    case 'n':
        return k_dec_cross_row_masks;
    case 'o':
        return k_dec_scan1_row_masks;
    case 'p':
        return k_dec_scan3_row_masks;
    case 'q':
        return k_dec_scan5_row_masks;
    case 'r':
        return k_dec_scan7_row_masks;
    case 's':
        return k_dec_scan9_row_masks;
    case 't':
        return k_dec_left_tee_row_masks;
    case 'u':
        return k_dec_right_tee_row_masks;
    case 'v':
        return k_dec_bottom_tee_row_masks;
    case 'w':
        return k_dec_top_tee_row_masks;
    case 'x':
        return k_dec_vertical_bar_row_masks;
    case 'y':
        return k_dec_less_equal_row_masks;
    case 'z':
        return k_dec_greater_equal_row_masks;
    case '{':
        return k_dec_pi_row_masks;
    case '|':
        return k_dec_not_equal_row_masks;
    case '}':
        return k_dec_sterling_row_masks;
    case '~':
        return k_dec_bullet_row_masks;
    default:
        return NULL;
    }
}

static const uint8_t *vt100_terminal_get_cell_row_masks(const vt100_terminal_cell_t *cell)
{
    const char ch = (char)vt100_terminal_sanitize_char(cell->ch);
    const uint8_t *builtin_rows = NULL;

    switch (cell->charset) {
    case VT100_CHARSET_US:
        builtin_rows = font5x7_get_cell6x9_row_masks(ch);
        break;
    case VT100_CHARSET_UK:
        builtin_rows = vt100_terminal_get_uk_row_masks(ch);
        if (builtin_rows == NULL) {
            builtin_rows = font5x7_get_cell6x9_row_masks(ch);
        }
        break;
    case VT100_CHARSET_DEC_SPECIAL:
        builtin_rows = vt100_terminal_get_dec_special_row_masks(ch);
        if (builtin_rows == NULL) {
            builtin_rows = font5x7_get_cell6x9_row_masks(ch);
        }
        break;
    default:
        builtin_rows = font5x7_get_cell6x9_row_masks(ch);
        break;
    }

    return builtin_rows;
}

static void vt100_terminal_resolve_colors(const vt100_terminal_t *terminal, const vt100_terminal_cell_t *cell,
        bool invert, uint8_t fg[3], uint8_t bg[3])
{
    uint8_t fg_index = VT100_ATTR_FG(cell->attr);
    const uint8_t bg_index = VT100_ATTR_BG(cell->attr);

    if ((cell->style & VT100_STYLE_BOLD) != 0u && fg_index < 8u) {
        fg_index = (uint8_t)(fg_index + 8u);
    }

    vt100_terminal_copy_color(fg, k_vt100_palette[fg_index]);
    vt100_terminal_copy_color(bg, k_vt100_palette[bg_index]);

    if ((cell->style & VT100_STYLE_FAINT) != 0u) {
        vt100_terminal_dim_color(fg);
    }

    if ((cell->style & VT100_STYLE_REVERSE) != 0u) {
        vt100_terminal_swap_colors(fg, bg);
    }

    if (terminal->screen_reverse) {
        vt100_terminal_swap_colors(fg, bg);
    }

    if (invert) {
        vt100_terminal_swap_colors(fg, bg);
    }

    if ((cell->style & VT100_STYLE_BLINK) != 0u && !terminal->blink_visible) {
        vt100_terminal_copy_color(fg, bg);
    }

    if ((cell->style & VT100_STYLE_INVISIBLE) != 0u) {
        vt100_terminal_copy_color(fg, bg);
    }

    vt100_terminal_pack_wire_color(fg);
    vt100_terminal_pack_wire_color(bg);
}

static void vt100_terminal_prepare_cell_render(
    const vt100_terminal_t *terminal,
    const vt100_terminal_cell_t *cell,
    bool invert,
    vt100_terminal_render_cache_t *render_data)
{
    render_data->row_masks = vt100_terminal_get_cell_row_masks(cell);
    render_data->underline = (cell->style & VT100_STYLE_UNDERLINE) != 0u;
    vt100_terminal_resolve_colors(terminal, cell, invert, render_data->fg, render_data->bg);
}

static void vt100_terminal_render_cell_internal(const vt100_terminal_t *terminal, uint8_t row, uint8_t col,
        bool invert)
{
    uint8_t cell_pixels[VT100_TERMINAL_CELL_WIDTH * VT100_TERMINAL_CELL_HEIGHT * 3u];
    vt100_terminal_render_cache_t render_data;
    const vt100_terminal_cell_t *cell = &terminal->cells[row][col];

    vt100_terminal_prepare_cell_render(terminal, cell, invert, &render_data);

    for (uint8_t py = 0; py < VT100_TERMINAL_CELL_HEIGHT; ++py) {
        const uint8_t row_mask = (uint8_t)(render_data.row_masks[py] |
                                           ((render_data.underline && py == (VT100_TERMINAL_CELL_HEIGHT - 1u))
                                            ? ((1u << VT100_TERMINAL_GLYPH_WIDTH) - 1u)
                                            : 0u));

        for (uint8_t px = 0; px < VT100_TERMINAL_CELL_WIDTH; ++px) {
            const bool pixel_on = (row_mask & (1u << px)) != 0u;
            const uint8_t *color = pixel_on ? render_data.fg : render_data.bg;
            uint8_t *dst = &cell_pixels[(py * VT100_TERMINAL_CELL_WIDTH + px) * 3u];

            dst[0] = color[0];
            dst[1] = color[1];
            dst[2] = color[2];
        }
    }

    ili9486l_draw_rgb666_wire_rect(
        cell_pixels,
        (uint16_t)(terminal->origin_x + col * VT100_TERMINAL_CELL_WIDTH),
        (uint16_t)(terminal->origin_y + row * VT100_TERMINAL_CELL_HEIGHT),
        VT100_TERMINAL_CELL_WIDTH,
        VT100_TERMINAL_CELL_HEIGHT);
}

static void vt100_terminal_hide_cursor(vt100_terminal_t *terminal)
{
    if (!terminal->cursor_visible) {
        return;
    }

    vt100_terminal_render_cell_internal(terminal, terminal->cursor_row, terminal->cursor_col, false);
}

static void vt100_terminal_show_cursor(vt100_terminal_t *terminal)
{
    if (!terminal->cursor_visible) {
        return;
    }

    vt100_terminal_render_cell_internal(terminal, terminal->cursor_row, terminal->cursor_col, true);
}

static void vt100_terminal_render_row_range(vt100_terminal_t *terminal, uint8_t row, uint8_t col_start,
        uint8_t col_end)
{
    const uint16_t pixel_x = (uint16_t)(terminal->origin_x + col_start * VT100_TERMINAL_CELL_WIDTH);
    const uint16_t pixel_w = (uint16_t)((uint16_t)(col_end - col_start + 1u) * VT100_TERMINAL_CELL_WIDTH);

    if (row >= VT100_TERMINAL_ROWS || col_start > col_end || col_end >= VT100_TERMINAL_COLS) {
        return;
    }

    for (uint8_t col = col_start; col <= col_end; ++col) {
        const vt100_terminal_cell_t *cell = &terminal->cells[row][col];

        vt100_terminal_prepare_cell_render(terminal, cell, false, &g_render_row_cache[col]);
    }

    if (!ili9486l_begin_write(
                pixel_x,
                (uint16_t)(terminal->origin_y + row * VT100_TERMINAL_CELL_HEIGHT),
                pixel_w,
                VT100_TERMINAL_CELL_HEIGHT)) {
        return;
    }

    for (uint8_t py = 0; py < VT100_TERMINAL_CELL_HEIGHT; ++py) {
        uint8_t *dst = g_scanline_buffer;

        for (uint8_t col = col_start; col <= col_end; ++col) {
            const vt100_terminal_render_cache_t *render_data = &g_render_row_cache[col];
            const uint8_t row_mask = (uint8_t)(render_data->row_masks[py] |
                                               ((render_data->underline && py == (VT100_TERMINAL_CELL_HEIGHT - 1u))
                                                ? ((1u << VT100_TERMINAL_GLYPH_WIDTH) - 1u)
                                                : 0u));

            for (uint8_t px = 0; px < VT100_TERMINAL_CELL_WIDTH; ++px) {
                const bool pixel_on = (row_mask & (1u << px)) != 0u;
                const uint8_t *color = pixel_on ? render_data->fg : render_data->bg;
                dst[0] = color[0];
                dst[1] = color[1];
                dst[2] = color[2];
                dst += 3;
            }
        }

        ili9486l_write_rgb666_wire_pixels(g_scanline_buffer, pixel_w);
    }
}

static void vt100_terminal_render_row(vt100_terminal_t *terminal, uint8_t row)
{
    vt100_terminal_render_row_range(terminal, row, 0u, (uint8_t)(VT100_TERMINAL_COLS - 1u));
}

static void vt100_terminal_refresh_blink_rows(vt100_terminal_t *terminal)
{
    bool cursor_row_redrawn = false;

    for (uint8_t row = 0; row < VT100_TERMINAL_ROWS; ++row) {
        if (!vt100_terminal_row_has_blink(terminal, row)) {
            continue;
        }

        vt100_terminal_render_row(terminal, row);
        if (terminal->cursor_visible && row == terminal->cursor_row) {
            cursor_row_redrawn = true;
        }
    }

    if (cursor_row_redrawn) {
        vt100_terminal_show_cursor(terminal);
    }
}

static void vt100_terminal_insert_lines(vt100_terminal_t *terminal, uint8_t count)
{
    if (terminal->cursor_row < terminal->scroll_top || terminal->cursor_row > terminal->scroll_bottom) {
        return;
    }

    if (count == 0u) {
        return;
    }

    if (count > (uint8_t)(terminal->scroll_bottom - terminal->cursor_row + 1u)) {
        count = (uint8_t)(terminal->scroll_bottom - terminal->cursor_row + 1u);
    }

    if (count <= (uint8_t)(terminal->scroll_bottom - terminal->cursor_row)) {
        memmove(
            &terminal->cells[(uint8_t)(terminal->cursor_row + count)][0],
            &terminal->cells[terminal->cursor_row][0],
            (uint8_t)(terminal->scroll_bottom - terminal->cursor_row + 1u - count) *
            VT100_TERMINAL_COLS * sizeof(vt100_terminal_cell_t));
    }

    for (uint8_t row = terminal->cursor_row; row < (uint8_t)(terminal->cursor_row + count); ++row) {
        vt100_terminal_fill_range(
            terminal,
            row,
            0,
            (uint8_t)(VT100_TERMINAL_COLS - 1u),
            vt100_terminal_current_attr(terminal),
            vt100_terminal_current_style(terminal));
    }

    vt100_terminal_render_rows(terminal, terminal->cursor_row, terminal->scroll_bottom);
}

static void vt100_terminal_delete_lines(vt100_terminal_t *terminal, uint8_t count)
{
    if (terminal->cursor_row < terminal->scroll_top || terminal->cursor_row > terminal->scroll_bottom) {
        return;
    }

    if (count == 0u) {
        return;
    }

    if (count > (uint8_t)(terminal->scroll_bottom - terminal->cursor_row + 1u)) {
        count = (uint8_t)(terminal->scroll_bottom - terminal->cursor_row + 1u);
    }

    if (count <= (uint8_t)(terminal->scroll_bottom - terminal->cursor_row)) {
        memmove(
            &terminal->cells[terminal->cursor_row][0],
            &terminal->cells[(uint8_t)(terminal->cursor_row + count)][0],
            (uint8_t)(terminal->scroll_bottom - terminal->cursor_row + 1u - count) *
            VT100_TERMINAL_COLS * sizeof(vt100_terminal_cell_t));
    }

    for (uint8_t row = (uint8_t)(terminal->scroll_bottom + 1u - count); row <= terminal->scroll_bottom; ++row) {
        vt100_terminal_fill_range(
            terminal,
            row,
            0,
            (uint8_t)(VT100_TERMINAL_COLS - 1u),
            vt100_terminal_current_attr(terminal),
            vt100_terminal_current_style(terminal));
    }

    vt100_terminal_render_rows(terminal, terminal->cursor_row, terminal->scroll_bottom);
}

static void vt100_terminal_insert_chars(vt100_terminal_t *terminal, uint8_t count)
{
    const vt100_terminal_cell_t blank = vt100_terminal_blank_cell(terminal);

    if (count == 0u || terminal->cursor_col >= VT100_TERMINAL_COLS) {
        return;
    }

    if (count > (uint8_t)(VT100_TERMINAL_COLS - terminal->cursor_col)) {
        count = (uint8_t)(VT100_TERMINAL_COLS - terminal->cursor_col);
    }

    if (count < (uint8_t)(VT100_TERMINAL_COLS - terminal->cursor_col)) {
        memmove(
            &terminal->cells[terminal->cursor_row][terminal->cursor_col + count],
            &terminal->cells[terminal->cursor_row][terminal->cursor_col],
            (uint8_t)(VT100_TERMINAL_COLS - terminal->cursor_col - count) * sizeof(vt100_terminal_cell_t));
    }

    for (uint8_t col = terminal->cursor_col; col < (uint8_t)(terminal->cursor_col + count); ++col) {
        terminal->cells[terminal->cursor_row][col] = blank;
    }

    vt100_terminal_render_row_range(
        terminal,
        terminal->cursor_row,
        terminal->cursor_col,
        (uint8_t)(VT100_TERMINAL_COLS - 1u));
}

static void vt100_terminal_delete_chars(vt100_terminal_t *terminal, uint8_t count)
{
    const vt100_terminal_cell_t blank = vt100_terminal_blank_cell(terminal);
    const uint8_t available = (uint8_t)(VT100_TERMINAL_COLS - terminal->cursor_col);

    if (count == 0u || terminal->cursor_col >= VT100_TERMINAL_COLS) {
        return;
    }

    if (count > available) {
        count = available;
    }

    if (count < available) {
        memmove(
            &terminal->cells[terminal->cursor_row][terminal->cursor_col],
            &terminal->cells[terminal->cursor_row][terminal->cursor_col + count],
            (uint8_t)(available - count) * sizeof(vt100_terminal_cell_t));
    }

    for (uint8_t col = (uint8_t)(VT100_TERMINAL_COLS - count); col < VT100_TERMINAL_COLS; ++col) {
        terminal->cells[terminal->cursor_row][col] = blank;
    }

    vt100_terminal_render_row_range(
        terminal,
        terminal->cursor_row,
        terminal->cursor_col,
        (uint8_t)(VT100_TERMINAL_COLS - 1u));
}

static void vt100_terminal_erase_chars(vt100_terminal_t *terminal, uint8_t count)
{
    if (count == 0u || terminal->cursor_col >= VT100_TERMINAL_COLS) {
        return;
    }

    if (count > (uint8_t)(VT100_TERMINAL_COLS - terminal->cursor_col)) {
        count = (uint8_t)(VT100_TERMINAL_COLS - terminal->cursor_col);
    }

    vt100_terminal_fill_range(
        terminal,
        terminal->cursor_row,
        terminal->cursor_col,
        (uint8_t)(terminal->cursor_col + count - 1u),
        vt100_terminal_current_attr(terminal),
        vt100_terminal_current_style(terminal));
    vt100_terminal_render_row_range(
        terminal,
        terminal->cursor_row,
        terminal->cursor_col,
        (uint8_t)(terminal->cursor_col + count - 1u));
}

static void vt100_terminal_write_cell(vt100_terminal_t *terminal, const vt100_terminal_cell_t *cell)
{
    if (cell == NULL) {
        return;
    }

    vt100_terminal_commit_wrap(terminal);
    if (terminal->insert_mode) {
        vt100_terminal_insert_chars(terminal, 1u);
    }

    vt100_terminal_set_cell(
        terminal,
        terminal->cursor_row,
        terminal->cursor_col,
        cell->ch,
        cell->attr,
        cell->style,
        cell->charset);
    vt100_terminal_render_cell_internal(terminal, terminal->cursor_row, terminal->cursor_col, false);
    terminal->last_printable = *cell;
    terminal->last_printable_valid = true;
    vt100_terminal_advance(terminal);
}

static bool vt100_terminal_write_cell_fast(
    vt100_terminal_t *terminal,
    const vt100_terminal_cell_t *cell,
    uint8_t *write_row,
    uint8_t *write_col)
{
    if (cell == NULL) {
        return false;
    }

    vt100_terminal_commit_wrap(terminal);
    if (terminal->insert_mode) {
        vt100_terminal_insert_chars(terminal, 1u);
    }

    if (write_row != NULL) {
        *write_row = terminal->cursor_row;
    }

    if (write_col != NULL) {
        *write_col = terminal->cursor_col;
    }

    vt100_terminal_set_cell(
        terminal,
        terminal->cursor_row,
        terminal->cursor_col,
        cell->ch,
        cell->attr,
        cell->style,
        cell->charset);
    terminal->last_printable = *cell;
    terminal->last_printable_valid = true;
    vt100_terminal_advance(terminal);
    return true;
}

static void vt100_terminal_scroll_up_region(vt100_terminal_t *terminal, uint8_t top, uint8_t bottom)
{
    if (top >= bottom || bottom >= VT100_TERMINAL_ROWS) {
        return;
    }

    memmove(
        &terminal->cells[top][0],
        &terminal->cells[(uint8_t)(top + 1u)][0],
        (bottom - top) * VT100_TERMINAL_COLS * sizeof(vt100_terminal_cell_t));

    vt100_terminal_fill_range(
        terminal,
        bottom,
        0,
        (uint8_t)(VT100_TERMINAL_COLS - 1u),
        vt100_terminal_current_attr(terminal),
        vt100_terminal_current_style(terminal));

    for (uint8_t row = top; row <= bottom; ++row) {
        vt100_terminal_render_row(terminal, row);
    }
}

static void vt100_terminal_scroll_down_region(vt100_terminal_t *terminal, uint8_t top, uint8_t bottom)
{
    if (top >= bottom || bottom >= VT100_TERMINAL_ROWS) {
        return;
    }

    memmove(
        &terminal->cells[(uint8_t)(top + 1u)][0],
        &terminal->cells[top][0],
        (bottom - top) * VT100_TERMINAL_COLS * sizeof(vt100_terminal_cell_t));

    vt100_terminal_fill_range(
        terminal,
        top,
        0,
        (uint8_t)(VT100_TERMINAL_COLS - 1u),
        vt100_terminal_current_attr(terminal),
        vt100_terminal_current_style(terminal));

    for (uint8_t row = top; row <= bottom; ++row) {
        vt100_terminal_render_row(terminal, row);
    }
}

static void vt100_terminal_commit_wrap(vt100_terminal_t *terminal)
{
    if (!terminal->wrap_pending) {
        return;
    }

    terminal->wrap_pending = false;
    terminal->cursor_col = 0;
    if (terminal->cursor_row == terminal->scroll_bottom) {
        vt100_terminal_scroll_up_region(terminal, terminal->scroll_top, terminal->scroll_bottom);
    } else if (terminal->cursor_row < VT100_TERMINAL_ROWS - 1u) {
        ++terminal->cursor_row;
    }
}

static void vt100_terminal_newline(vt100_terminal_t *terminal)
{
    terminal->wrap_pending = false;

    if (terminal->cursor_row >= terminal->scroll_top && terminal->cursor_row <= terminal->scroll_bottom) {
        if (terminal->cursor_row == terminal->scroll_bottom) {
            vt100_terminal_scroll_up_region(terminal, terminal->scroll_top, terminal->scroll_bottom);
        } else {
            ++terminal->cursor_row;
        }
    } else if (terminal->cursor_row == VT100_TERMINAL_ROWS - 1u) {
        if (terminal->scroll_top == 0u && terminal->scroll_bottom == VT100_TERMINAL_ROWS - 1u) {
            vt100_terminal_scroll_up_region(terminal, 0u, (uint8_t)(VT100_TERMINAL_ROWS - 1u));
        }
    } else {
        ++terminal->cursor_row;
    }
}

static void vt100_terminal_reverse_index(vt100_terminal_t *terminal)
{
    terminal->wrap_pending = false;

    if (terminal->cursor_row >= terminal->scroll_top && terminal->cursor_row <= terminal->scroll_bottom) {
        if (terminal->cursor_row == terminal->scroll_top) {
            vt100_terminal_scroll_down_region(terminal, terminal->scroll_top, terminal->scroll_bottom);
        } else {
            --terminal->cursor_row;
        }
    } else if (terminal->cursor_row > 0u) {
        --terminal->cursor_row;
    }
}

static void vt100_terminal_advance(vt100_terminal_t *terminal)
{
    if (terminal->cursor_col == VT100_TERMINAL_COLS - 1u) {
        if (terminal->autowrap) {
            terminal->wrap_pending = true;
        }
    } else {
        ++terminal->cursor_col;
    }
}

static uint16_t vt100_terminal_param_or(const vt100_terminal_t *terminal, uint8_t index, uint16_t fallback)
{
    if (index >= terminal->csi_param_count) {
        return fallback;
    }

    return terminal->csi_params[index] == 0u ? fallback : terminal->csi_params[index];
}

static void vt100_terminal_push_csi_value(vt100_terminal_t *terminal)
{
    if (terminal->csi_param_count >= (sizeof(terminal->csi_params) / sizeof(terminal->csi_params[0]))) {
        return;
    }

    if (terminal->csi_have_value || terminal->csi_param_count > 0u) {
        terminal->csi_params[terminal->csi_param_count++] = terminal->csi_value;
    }

    terminal->csi_value = 0;
    terminal->csi_have_value = 0;
}

static void vt100_terminal_erase_display(vt100_terminal_t *terminal, uint16_t mode)
{
    const uint8_t attr = vt100_terminal_current_attr(terminal);
    const uint8_t style = vt100_terminal_current_style(terminal);

    if (mode == 2u) {
        for (uint8_t row = 0; row < VT100_TERMINAL_ROWS; ++row) {
            vt100_terminal_fill_range(terminal, row, 0, (uint8_t)(VT100_TERMINAL_COLS - 1u), attr, style);
            vt100_terminal_render_row(terminal, row);
        }
        return;
    }

    if (mode == 0u) {
        vt100_terminal_fill_range(terminal, terminal->cursor_row, terminal->cursor_col, (uint8_t)(VT100_TERMINAL_COLS - 1u),
                                  attr, style);
        vt100_terminal_render_row_range(
            terminal,
            terminal->cursor_row,
            terminal->cursor_col,
            (uint8_t)(VT100_TERMINAL_COLS - 1u));
        for (uint8_t row = (uint8_t)(terminal->cursor_row + 1u); row < VT100_TERMINAL_ROWS; ++row) {
            vt100_terminal_fill_range(terminal, row, 0, (uint8_t)(VT100_TERMINAL_COLS - 1u), attr, style);
            vt100_terminal_render_row(terminal, row);
        }
        return;
    }

    if (mode == 1u) {
        for (uint8_t row = 0; row < terminal->cursor_row; ++row) {
            vt100_terminal_fill_range(terminal, row, 0, (uint8_t)(VT100_TERMINAL_COLS - 1u), attr, style);
            vt100_terminal_render_row(terminal, row);
        }
        vt100_terminal_fill_range(terminal, terminal->cursor_row, 0, terminal->cursor_col, attr, style);
        vt100_terminal_render_row_range(terminal, terminal->cursor_row, 0u, terminal->cursor_col);
    }
}

static void vt100_terminal_erase_line(vt100_terminal_t *terminal, uint16_t mode)
{
    const uint8_t attr = vt100_terminal_current_attr(terminal);
    const uint8_t style = vt100_terminal_current_style(terminal);

    if (mode == 2u) {
        vt100_terminal_fill_range(terminal, terminal->cursor_row, 0, (uint8_t)(VT100_TERMINAL_COLS - 1u), attr, style);
        vt100_terminal_render_row(terminal, terminal->cursor_row);
    } else if (mode == 1u) {
        vt100_terminal_fill_range(terminal, terminal->cursor_row, 0, terminal->cursor_col, attr, style);
        vt100_terminal_render_row_range(terminal, terminal->cursor_row, 0u, terminal->cursor_col);
    } else {
        vt100_terminal_fill_range(terminal, terminal->cursor_row, terminal->cursor_col, (uint8_t)(VT100_TERMINAL_COLS - 1u),
                                  attr, style);
        vt100_terminal_render_row_range(
            terminal,
            terminal->cursor_row,
            terminal->cursor_col,
            (uint8_t)(VT100_TERMINAL_COLS - 1u));
    }
}

static void vt100_terminal_apply_sgr(vt100_terminal_t *terminal)
{
    if (terminal->csi_param_count == 0u) {
        terminal->fg = terminal->default_fg;
        terminal->bg = terminal->default_bg;
        terminal->style = 0u;
        return;
    }

    for (uint8_t i = 0; i < terminal->csi_param_count; ++i) {
        const uint16_t param = terminal->csi_params[i];

        if (param == 0u) {
            terminal->fg = terminal->default_fg;
            terminal->bg = terminal->default_bg;
            terminal->style = 0u;
        } else if (param == 1u) {
            terminal->style = (uint8_t)((terminal->style | VT100_STYLE_BOLD) & (uint8_t)~VT100_STYLE_FAINT);
        } else if (param == 2u) {
            terminal->style = (uint8_t)((terminal->style | VT100_STYLE_FAINT) & (uint8_t)~VT100_STYLE_BOLD);
        } else if (param == 4u) {
            terminal->style = (uint8_t)(terminal->style | VT100_STYLE_UNDERLINE);
        } else if (param == 5u) {
            terminal->style = (uint8_t)(terminal->style | VT100_STYLE_BLINK);
        } else if (param == 7u) {
            terminal->style = (uint8_t)(terminal->style | VT100_STYLE_REVERSE);
        } else if (param == 8u) {
            terminal->style = (uint8_t)(terminal->style | VT100_STYLE_INVISIBLE);
        } else if (param == 21u) {
            terminal->style = (uint8_t)(terminal->style & (uint8_t)~VT100_STYLE_BOLD);
        } else if (param == 22u) {
            terminal->style = (uint8_t)(terminal->style & (uint8_t)~(VT100_STYLE_BOLD | VT100_STYLE_FAINT));
        } else if (param == 24u) {
            terminal->style = (uint8_t)(terminal->style & (uint8_t)~VT100_STYLE_UNDERLINE);
        } else if (param == 25u) {
            terminal->style = (uint8_t)(terminal->style & (uint8_t)~VT100_STYLE_BLINK);
        } else if (param == 27u) {
            terminal->style = (uint8_t)(terminal->style & (uint8_t)~VT100_STYLE_REVERSE);
        } else if (param == 28u) {
            terminal->style = (uint8_t)(terminal->style & (uint8_t)~VT100_STYLE_INVISIBLE);
        } else if (param >= 30u && param <= 37u) {
            terminal->fg = (uint8_t)(param - 30u);
        } else if (param == 39u) {
            terminal->fg = terminal->default_fg;
        } else if (param >= 40u && param <= 47u) {
            terminal->bg = (uint8_t)(param - 40u);
        } else if (param == 49u) {
            terminal->bg = terminal->default_bg;
        } else if (param >= 90u && param <= 97u) {
            terminal->fg = (uint8_t)(8u + (param - 90u));
        } else if (param >= 100u && param <= 107u) {
            terminal->bg = (uint8_t)(8u + (param - 100u));
        }
    }
}

static void vt100_terminal_dispatch_private_csi(vt100_terminal_t *terminal, char final_char)
{
    if (final_char != 'h' && final_char != 'l') {
        return;
    }

    for (uint8_t i = 0; i < terminal->csi_param_count; ++i) {
        switch (terminal->csi_params[i]) {
        case 1u:
            terminal->cursor_key_application_mode = (final_char == 'h');
            break;
        case 2u:
            terminal->vt52_mode = (final_char == 'l');
            terminal->vt52_graphics = false;
            terminal->single_shift_pending = false;
            terminal->gl_set = 0u;
            break;
        case 5u:
            terminal->screen_reverse = (final_char == 'h');
            vt100_terminal_render(terminal);
            break;
        case 6u:
            terminal->origin_mode = (final_char == 'h');
            vt100_terminal_home_cursor(terminal);
            break;
        case 7u:
            terminal->autowrap = (final_char == 'h');
            if (!terminal->autowrap) {
                terminal->wrap_pending = false;
            }
            break;
        case 25u:
            terminal->cursor_visible = (final_char == 'h');
            break;
        }
    }
}

static void vt100_terminal_dispatch_mode_csi(vt100_terminal_t *terminal, char final_char)
{
    const bool set_mode = final_char == 'h';

    if (final_char != 'h' && final_char != 'l') {
        return;
    }

    for (uint8_t i = 0; i < terminal->csi_param_count; ++i) {
        switch (terminal->csi_params[i]) {
        case 4u:
            terminal->insert_mode = set_mode;
            break;
        case 20u:
            terminal->newline_mode = set_mode;
            break;
        }
    }
}

static void vt100_terminal_dispatch_csi(vt100_terminal_t *terminal, char final_char)
{
    const uint16_t first = vt100_terminal_param_or(terminal, 0, 1u);
    const uint16_t second = vt100_terminal_param_or(terminal, 1, 1u);

    if (terminal->csi_private) {
        vt100_terminal_dispatch_private_csi(terminal, final_char);
        return;
    }

    switch (final_char) {
    case 'A':
        terminal->wrap_pending = false;
        if (terminal->cursor_row >= terminal->scroll_top && terminal->cursor_row <= terminal->scroll_bottom) {
            terminal->cursor_row = (uint8_t)((terminal->cursor_row > (uint8_t)(terminal->scroll_top + first - 1u)) ?
                                             (terminal->cursor_row - first) : terminal->scroll_top);
        } else if (terminal->cursor_row >= first) {
            terminal->cursor_row = (uint8_t)(terminal->cursor_row - first);
        } else {
            terminal->cursor_row = 0;
        }
        break;
    case '@':
        terminal->wrap_pending = false;
        vt100_terminal_insert_chars(terminal, (uint8_t)first);
        break;
    case '`':
        terminal->wrap_pending = false;
        terminal->cursor_col = (uint8_t)((first == 0u
                                          || first > VT100_TERMINAL_COLS) ? (VT100_TERMINAL_COLS - 1u) : (first - 1u));
        break;
    case 'B': {
        const uint8_t bottom = (terminal->cursor_row >= terminal->scroll_top && terminal->cursor_row <= terminal->scroll_bottom)
                               ? terminal->scroll_bottom
                               : (uint8_t)(VT100_TERMINAL_ROWS - 1u);
        const uint16_t next = (uint16_t)(terminal->cursor_row + first);
        terminal->wrap_pending = false;
        terminal->cursor_row = (uint8_t)(next > bottom ? bottom : next);
        break;
    }
    case 'C': {
        const uint16_t next = (uint16_t)(terminal->cursor_col + first);
        terminal->wrap_pending = false;
        terminal->cursor_col = (uint8_t)(next >= VT100_TERMINAL_COLS ? (VT100_TERMINAL_COLS - 1u) : next);
        break;
    }
    case 'D':
        terminal->wrap_pending = false;
        if (terminal->cursor_col >= first) {
            terminal->cursor_col = (uint8_t)(terminal->cursor_col - first);
        } else {
            terminal->cursor_col = 0;
        }
        break;
    case 'E': {
        const uint8_t bottom = (terminal->cursor_row >= terminal->scroll_top && terminal->cursor_row <= terminal->scroll_bottom)
                               ? terminal->scroll_bottom
                               : vt100_terminal_row_limit(terminal);
        const uint16_t next = (uint16_t)(terminal->cursor_row + first);
        terminal->wrap_pending = false;
        terminal->cursor_row = (uint8_t)(next > bottom ? bottom : next);
        terminal->cursor_col = 0u;
        break;
    }
    case 'F': {
        const uint8_t top = (terminal->cursor_row >= terminal->scroll_top && terminal->cursor_row <= terminal->scroll_bottom)
                            ? terminal->scroll_top
                            : vt100_terminal_row_base(terminal);
        terminal->wrap_pending = false;
        terminal->cursor_row = (uint8_t)((terminal->cursor_row > (uint8_t)(top + first - 1u)) ?
                                         (terminal->cursor_row - first) : top);
        terminal->cursor_col = 0u;
        break;
    }
    case 'G':
        terminal->wrap_pending = false;
        terminal->cursor_col = (uint8_t)((first == 0u
                                          || first > VT100_TERMINAL_COLS) ? (VT100_TERMINAL_COLS - 1u) : (first - 1u));
        break;
    case 'H':
    case 'f':
        vt100_terminal_apply_cursor_address(terminal, first, second);
        break;
    case 'J':
        terminal->wrap_pending = false;
        vt100_terminal_erase_display(terminal, vt100_terminal_param_or(terminal, 0, 0u));
        break;
    case 'K':
        terminal->wrap_pending = false;
        vt100_terminal_erase_line(terminal, vt100_terminal_param_or(terminal, 0, 0u));
        break;
    case 'L':
        terminal->wrap_pending = false;
        vt100_terminal_insert_lines(terminal, (uint8_t)first);
        break;
    case 'M':
        terminal->wrap_pending = false;
        vt100_terminal_delete_lines(terminal, (uint8_t)first);
        break;
    case 'P':
        terminal->wrap_pending = false;
        vt100_terminal_delete_chars(terminal, (uint8_t)first);
        break;
    case 'X':
        terminal->wrap_pending = false;
        vt100_terminal_erase_chars(terminal, (uint8_t)first);
        break;
    case 'a': {
        const uint16_t next = (uint16_t)(terminal->cursor_col + first);
        terminal->wrap_pending = false;
        terminal->cursor_col = (uint8_t)(next >= VT100_TERMINAL_COLS ? (VT100_TERMINAL_COLS - 1u) : next);
        break;
    }
    case 'b':
        if (terminal->last_printable_valid) {
            for (uint16_t i = 0; i < first; ++i) {
                vt100_terminal_write_cell(terminal, &terminal->last_printable);
            }
        }
        break;
    case 'c':
        vt100_terminal_emit_device_attributes(terminal);
        break;
    case 'd':
        vt100_terminal_apply_row_address(terminal, first);
        break;
    case 'e': {
        const uint8_t bottom = (terminal->cursor_row >= terminal->scroll_top && terminal->cursor_row <= terminal->scroll_bottom)
                               ? terminal->scroll_bottom
                               : vt100_terminal_row_limit(terminal);
        const uint16_t next = (uint16_t)(terminal->cursor_row + first);
        terminal->wrap_pending = false;
        terminal->cursor_row = (uint8_t)(next > bottom ? bottom : next);
        break;
    }
    case 'g':
        if (vt100_terminal_param_or(terminal, 0, 0u) == 3u) {
            memset(terminal->tab_stops, 0, sizeof(terminal->tab_stops));
        } else {
            terminal->tab_stops[terminal->cursor_col] = false;
        }
        break;
    case 'I':
        terminal->wrap_pending = false;
        for (uint16_t i = 0; i < first; ++i) {
            terminal->cursor_col = vt100_terminal_find_next_tab_stop(terminal, terminal->cursor_col);
        }
        break;
    case 'h':
    case 'l':
        vt100_terminal_dispatch_mode_csi(terminal, final_char);
        break;
    case 'n':
        if (vt100_terminal_param_or(terminal, 0, 0u) == 5u) {
            vt100_terminal_emit_device_status(terminal, 0u);
        } else if (vt100_terminal_param_or(terminal, 0, 0u) == 6u) {
            vt100_terminal_emit_cursor_position(terminal);
        }
        break;
    case 'm':
        vt100_terminal_apply_sgr(terminal);
        break;
    case 'r': {
        const uint16_t top = vt100_terminal_param_or(terminal, 0, 1u);
        const uint16_t bottom = vt100_terminal_param_or(terminal, 1, VT100_TERMINAL_ROWS);

        if (top >= 1u && top < bottom && bottom <= VT100_TERMINAL_ROWS) {
            terminal->scroll_top = (uint8_t)(top - 1u);
            terminal->scroll_bottom = (uint8_t)(bottom - 1u);
            vt100_terminal_home_cursor(terminal);
        }
        break;
    }
    case 'S':
        vt100_terminal_scroll_region_up(terminal, (uint8_t)first);
        break;
    case 's':
        terminal->saved_row = terminal->cursor_row;
        terminal->saved_col = terminal->cursor_col;
        break;
    case 'T':
        vt100_terminal_scroll_region_down(terminal, (uint8_t)first);
        break;
    case 'u':
        terminal->cursor_row = terminal->saved_row;
        terminal->cursor_col = terminal->saved_col;
        break;
    case 'Z':
        terminal->wrap_pending = false;
        for (uint16_t i = 0; i < first; ++i) {
            terminal->cursor_col = vt100_terminal_find_prev_tab_stop(terminal, terminal->cursor_col);
        }
        break;
    }
}

void vt100_terminal_render(vt100_terminal_t *terminal)
{
    vt100_terminal_hide_cursor(terminal);

    for (uint8_t row = 0; row < VT100_TERMINAL_ROWS; ++row) {
        vt100_terminal_render_row(terminal, row);
    }

    vt100_terminal_show_cursor(terminal);
}

void vt100_terminal_reset(vt100_terminal_t *terminal)
{
    memset(terminal->cells, 0, sizeof(terminal->cells));

    terminal->cursor_row = 0;
    terminal->cursor_col = 0;
    terminal->saved_row = 0;
    terminal->saved_col = 0;
    terminal->default_fg = 15u;
    terminal->default_bg = 0u;
    terminal->fg = terminal->default_fg;
    terminal->bg = terminal->default_bg;
    terminal->style = 0u;
    terminal->g0_charset = VT100_CHARSET_US;
    terminal->g1_charset = VT100_CHARSET_US;
    terminal->g2_charset = VT100_CHARSET_US;
    terminal->g3_charset = VT100_CHARSET_US;
    terminal->gl_set = 0u;
    terminal->saved_fg = terminal->fg;
    terminal->saved_bg = terminal->bg;
    terminal->saved_style = terminal->style;
    terminal->saved_g0_charset = terminal->g0_charset;
    terminal->saved_g1_charset = terminal->g1_charset;
    terminal->saved_g2_charset = terminal->g2_charset;
    terminal->saved_g3_charset = terminal->g3_charset;
    terminal->saved_gl_set = terminal->gl_set;
    terminal->scroll_top = 0u;
    terminal->scroll_bottom = (uint8_t)(VT100_TERMINAL_ROWS - 1u);
    terminal->state = VT100_STATE_GROUND;
    terminal->csi_param_count = 0;
    terminal->csi_have_value = 0;
    terminal->csi_private = 0;
    terminal->single_shift_charset = VT100_CHARSET_US;
    terminal->vt52_pending_row = 0u;
    terminal->csi_value = 0;
    terminal->autowrap = true;
    terminal->saved_autowrap = terminal->autowrap;
    terminal->wrap_pending = false;
    terminal->saved_wrap_pending = terminal->wrap_pending;
    terminal->insert_mode = false;
    terminal->newline_mode = false;
    terminal->origin_mode = false;
    terminal->saved_origin_mode = terminal->origin_mode;
    terminal->screen_reverse = false;
    terminal->cursor_key_application_mode = false;
    terminal->saved_cursor_key_application_mode = terminal->cursor_key_application_mode;
    terminal->keypad_application_mode = false;
    terminal->saved_keypad_application_mode = terminal->keypad_application_mode;
    terminal->vt52_mode = false;
    terminal->saved_vt52_mode = terminal->vt52_mode;
    terminal->vt52_graphics = false;
    terminal->saved_vt52_graphics = terminal->vt52_graphics;
    terminal->single_shift_pending = false;
    terminal->last_printable_valid = false;
    terminal->blink_elapsed_ms = 0u;
    terminal->blink_visible = true;
    vt100_terminal_reset_tab_stops(terminal);

    for (uint8_t row = 0; row < VT100_TERMINAL_ROWS; ++row) {
        vt100_terminal_fill_range(
            terminal,
            row,
            0,
            (uint8_t)(VT100_TERMINAL_COLS - 1u),
            vt100_terminal_current_attr(terminal),
            vt100_terminal_current_style(terminal));
    }

    ili9486l_fill_rect(
        terminal->origin_x,
        terminal->origin_y,
        VT100_TERMINAL_WIDTH_PIXELS,
        VT100_TERMINAL_HEIGHT_PIXELS,
        LCD_COLOR_BLACK);

    vt100_terminal_render(terminal);
}

void vt100_terminal_init(vt100_terminal_t *terminal, uint16_t origin_x, uint16_t origin_y)
{
    memset(terminal, 0, sizeof(*terminal));
    terminal->origin_x = origin_x;
    terminal->origin_y = origin_y;
    terminal->cursor_visible = true;
    vt100_terminal_reset(terminal);
}

void vt100_terminal_set_output(vt100_terminal_t *terminal, vt100_terminal_output_fn output_fn, void *user_data)
{
    if (terminal == NULL) {
        return;
    }

    terminal->output_fn = output_fn;
    terminal->output_user_data = user_data;
}

void vt100_terminal_tick(vt100_terminal_t *terminal, uint32_t elapsed_ms)
{
    bool old_blink_visible = false;

    if (terminal == NULL || elapsed_ms == 0u) {
        return;
    }

    old_blink_visible = terminal->blink_visible;
    terminal->blink_elapsed_ms += elapsed_ms;
    terminal->blink_visible =
        ((terminal->blink_elapsed_ms / VT100_TERMINAL_BLINK_INTERVAL_MS) & 1u) == 0u;

    if (terminal->blink_visible != old_blink_visible) {
        vt100_terminal_refresh_blink_rows(terminal);
    }
}

void vt100_terminal_putc(vt100_terminal_t *terminal, char ch)
{
    const unsigned char uch = (unsigned char)ch;
    const uint8_t old_cursor_row = terminal->cursor_row;
    const uint8_t old_cursor_col = terminal->cursor_col;

    if (terminal->state == VT100_STATE_GROUND && uch >= 0x20u && uch != 0x7Fu) {
        uint8_t write_row = terminal->cursor_row;
        uint8_t write_col = terminal->cursor_col;
        const vt100_terminal_cell_t cell = {
            .ch = ch,
            .attr = vt100_terminal_current_attr(terminal),
            .style = vt100_terminal_current_style(terminal),
            .charset = vt100_terminal_current_charset(terminal),
        };
        const bool wrote = vt100_terminal_write_cell_fast(terminal, &cell, &write_row, &write_col);
        const bool cursor_moved = (old_cursor_row != terminal->cursor_row) || (old_cursor_col != terminal->cursor_col);

        terminal->single_shift_pending = false;

        if (!wrote) {
            return;
        }

        if (!cursor_moved) {
            vt100_terminal_render_cell_internal(terminal, write_row, write_col, terminal->cursor_visible);
            return;
        }

        vt100_terminal_render_cell_internal(terminal, write_row, write_col, false);

        if (old_cursor_row != write_row || old_cursor_col != write_col) {
            vt100_terminal_render_cell_internal(terminal, old_cursor_row, old_cursor_col, false);
        }

        if (terminal->cursor_visible) {
            vt100_terminal_render_cell_internal(terminal, terminal->cursor_row, terminal->cursor_col, true);
        }
        return;
    }

    vt100_terminal_hide_cursor(terminal);

    if (uch == 0x7Fu) {
        vt100_terminal_show_cursor(terminal);
        return;
    }

    if (uch == 0x18u || uch == 0x1Au) {
        vt100_terminal_reset_parser(terminal);
        vt100_terminal_show_cursor(terminal);
        return;
    }

    switch (terminal->state) {
    case VT100_STATE_GROUND:
        if (ch == '\x1b') {
            terminal->state = VT100_STATE_ESC;
            break;
        }

        switch (uch) {
        case '\a':
            break;
        case '\b':
            terminal->wrap_pending = false;
            if (terminal->cursor_col > 0u) {
                --terminal->cursor_col;
            }
            break;
        case '\t': {
            vt100_terminal_commit_wrap(terminal);
            const uint8_t next_tab = vt100_terminal_find_next_tab_stop(terminal, terminal->cursor_col);
            terminal->cursor_col = next_tab >= VT100_TERMINAL_COLS ? (VT100_TERMINAL_COLS - 1u) : next_tab;
            break;
        }
        case '\n':
        case '\v':
        case '\f':
            if (terminal->newline_mode) {
                terminal->cursor_col = 0u;
            }
            vt100_terminal_newline(terminal);
            break;
        case '\r':
            terminal->wrap_pending = false;
            terminal->cursor_col = 0;
            break;
        case '\x0e':
            terminal->gl_set = 1u;
            break;
        case '\x0f':
            terminal->gl_set = 0u;
            break;
        default:
            if (uch >= 0x20u) {
                const vt100_terminal_cell_t cell = {
                    .ch = ch,
                    .attr = vt100_terminal_current_attr(terminal),
                    .style = vt100_terminal_current_style(terminal),
                    .charset = vt100_terminal_current_charset(terminal),
                };
                vt100_terminal_write_cell(terminal, &cell);
                terminal->single_shift_pending = false;
            }
            break;
        }
        break;

    case VT100_STATE_ESC:
        terminal->state = VT100_STATE_GROUND;
        if (terminal->vt52_mode) {
            switch (ch) {
            case '<':
                terminal->vt52_mode = false;
                terminal->vt52_graphics = false;
                terminal->single_shift_pending = false;
                terminal->gl_set = 0u;
                break;
            case 'A':
                terminal->wrap_pending = false;
                if (terminal->cursor_row > 0u) {
                    --terminal->cursor_row;
                }
                break;
            case 'B':
                terminal->wrap_pending = false;
                if (terminal->cursor_row < (uint8_t)(VT100_TERMINAL_ROWS - 1u)) {
                    ++terminal->cursor_row;
                }
                break;
            case 'C':
                terminal->wrap_pending = false;
                if (terminal->cursor_col < (uint8_t)(VT100_TERMINAL_COLS - 1u)) {
                    ++terminal->cursor_col;
                }
                break;
            case 'D':
                terminal->wrap_pending = false;
                if (terminal->cursor_col > 0u) {
                    --terminal->cursor_col;
                }
                break;
            case 'F':
                terminal->vt52_graphics = true;
                break;
            case 'G':
                terminal->vt52_graphics = false;
                break;
            case 'H':
                terminal->cursor_row = 0u;
                terminal->cursor_col = 0u;
                terminal->wrap_pending = false;
                break;
            case 'I':
                vt100_terminal_reverse_index(terminal);
                break;
            case 'J':
                terminal->wrap_pending = false;
                vt100_terminal_erase_display(terminal, 0u);
                break;
            case 'K':
                terminal->wrap_pending = false;
                vt100_terminal_erase_line(terminal, 0u);
                break;
            case 'Y':
                terminal->state = VT100_STATE_VT52_CURSOR_ROW;
                break;
            case 'Z':
                vt100_terminal_emit_vt52_ident(terminal);
                break;
            case '=':
                terminal->keypad_application_mode = true;
                break;
            case '>':
                terminal->keypad_application_mode = false;
                break;
            }
            break;
        }

        if (ch == '[') {
            terminal->state = VT100_STATE_CSI;
            terminal->csi_param_count = 0;
            terminal->csi_have_value = 0;
            terminal->csi_private = 0;
            terminal->csi_value = 0;
        } else if (ch == ']') {
            terminal->state = VT100_STATE_OSC;
        } else if (ch == '#') {
            terminal->state = VT100_STATE_ESC_HASH;
        } else if (ch == '(') {
            terminal->state = VT100_STATE_ESC_G0;
        } else if (ch == ')') {
            terminal->state = VT100_STATE_ESC_G1;
        } else if (ch == '*') {
            terminal->state = VT100_STATE_ESC_G2;
        } else if (ch == '+') {
            terminal->state = VT100_STATE_ESC_G3;
        } else if (ch == 'P' || ch == 'X' || ch == '^' || ch == '_') {
            terminal->state = VT100_STATE_STR;
        } else if (ch == '7') {
            terminal->saved_row = terminal->cursor_row;
            terminal->saved_col = terminal->cursor_col;
            terminal->saved_fg = terminal->fg;
            terminal->saved_bg = terminal->bg;
            terminal->saved_style = terminal->style;
            terminal->saved_g0_charset = terminal->g0_charset;
            terminal->saved_g1_charset = terminal->g1_charset;
            terminal->saved_g2_charset = terminal->g2_charset;
            terminal->saved_g3_charset = terminal->g3_charset;
            terminal->saved_gl_set = terminal->gl_set;
            terminal->saved_autowrap = terminal->autowrap;
            terminal->saved_origin_mode = terminal->origin_mode;
            terminal->saved_wrap_pending = terminal->wrap_pending;
            terminal->saved_cursor_key_application_mode = terminal->cursor_key_application_mode;
            terminal->saved_keypad_application_mode = terminal->keypad_application_mode;
            terminal->saved_vt52_mode = terminal->vt52_mode;
            terminal->saved_vt52_graphics = terminal->vt52_graphics;
        } else if (ch == '8') {
            terminal->cursor_row = terminal->saved_row;
            terminal->cursor_col = terminal->saved_col;
            terminal->fg = terminal->saved_fg;
            terminal->bg = terminal->saved_bg;
            terminal->style = terminal->saved_style;
            terminal->g0_charset = terminal->saved_g0_charset;
            terminal->g1_charset = terminal->saved_g1_charset;
            terminal->g2_charset = terminal->saved_g2_charset;
            terminal->g3_charset = terminal->saved_g3_charset;
            terminal->gl_set = terminal->saved_gl_set;
            terminal->autowrap = terminal->saved_autowrap;
            terminal->origin_mode = terminal->saved_origin_mode;
            terminal->wrap_pending = terminal->saved_wrap_pending;
            terminal->cursor_key_application_mode = terminal->saved_cursor_key_application_mode;
            terminal->keypad_application_mode = terminal->saved_keypad_application_mode;
            terminal->vt52_mode = terminal->saved_vt52_mode;
            terminal->vt52_graphics = terminal->saved_vt52_graphics;
        } else if (ch == 'c') {
            vt100_terminal_reset(terminal);
            return;
        } else if (ch == 'D') {
            vt100_terminal_newline(terminal);
        } else if (ch == 'E') {
            terminal->wrap_pending = false;
            terminal->cursor_col = 0;
            vt100_terminal_newline(terminal);
        } else if (ch == 'H') {
            terminal->tab_stops[terminal->cursor_col] = true;
        } else if (ch == 'M') {
            vt100_terminal_reverse_index(terminal);
        } else if (ch == 'N') {
            terminal->single_shift_charset = terminal->g2_charset;
            terminal->single_shift_pending = true;
        } else if (ch == 'O') {
            terminal->single_shift_charset = terminal->g3_charset;
            terminal->single_shift_pending = true;
        } else if (ch == '=') {
            terminal->keypad_application_mode = true;
        } else if (ch == '>') {
            terminal->keypad_application_mode = false;
        } else if (ch == 'Z') {
            vt100_terminal_emit_device_attributes(terminal);
        }
        break;

    case VT100_STATE_CSI:
        if (ch >= '0' && ch <= '9') {
            terminal->csi_value = (uint16_t)(terminal->csi_value * 10u + (uint16_t)(ch - '0'));
            terminal->csi_have_value = 1;
        } else if (ch == ';') {
            vt100_terminal_push_csi_value(terminal);
        } else if (ch == '?') {
            terminal->csi_private = 1;
        } else {
            vt100_terminal_push_csi_value(terminal);
            vt100_terminal_dispatch_csi(terminal, ch);
            terminal->state = VT100_STATE_GROUND;
        }
        break;

    case VT100_STATE_OSC:
        if (ch == '\a') {
            terminal->state = VT100_STATE_GROUND;
        } else if (ch == '\x1b') {
            terminal->state = VT100_STATE_OSC_ESC;
        }
        break;

    case VT100_STATE_OSC_ESC:
        terminal->state = (ch == '\\') ? VT100_STATE_GROUND : VT100_STATE_OSC;
        break;

    case VT100_STATE_STR:
        if (ch == '\x1b') {
            terminal->state = VT100_STATE_STR_ESC;
        }
        break;

    case VT100_STATE_STR_ESC:
        terminal->state = (ch == '\\') ? VT100_STATE_GROUND : VT100_STATE_STR;
        break;

    case VT100_STATE_ESC_G0:
        terminal->g0_charset = vt100_terminal_charset_from_designator(ch);
        terminal->state = VT100_STATE_GROUND;
        break;

    case VT100_STATE_ESC_G1:
        terminal->g1_charset = vt100_terminal_charset_from_designator(ch);
        terminal->state = VT100_STATE_GROUND;
        break;

    case VT100_STATE_ESC_G2:
        terminal->g2_charset = vt100_terminal_charset_from_designator(ch);
        terminal->state = VT100_STATE_GROUND;
        break;

    case VT100_STATE_ESC_G3:
        terminal->g3_charset = vt100_terminal_charset_from_designator(ch);
        terminal->state = VT100_STATE_GROUND;
        break;

    case VT100_STATE_ESC_HASH:
        switch (ch) {
        case '3':
        case '4':
        case '5':
        case '6':
            break;
        case '8':
            vt100_terminal_alignment_display(terminal);
            break;
        }
        terminal->state = VT100_STATE_GROUND;
        break;

    case VT100_STATE_VT52_CURSOR_ROW:
        terminal->vt52_pending_row = vt100_terminal_decode_vt52_coord(ch, (uint8_t)(VT100_TERMINAL_ROWS - 1u));
        terminal->state = VT100_STATE_VT52_CURSOR_COL;
        break;

    case VT100_STATE_VT52_CURSOR_COL:
        vt100_terminal_apply_vt52_cursor_address(terminal, terminal->vt52_pending_row, ch);
        terminal->state = VT100_STATE_GROUND;
        break;
    }

    vt100_terminal_show_cursor(terminal);
}

void vt100_terminal_write_n(vt100_terminal_t *terminal, const char *text, size_t len)
{
    bool batch_active = false;
    uint8_t batch_row = 0u;
    uint8_t batch_col_start = 0u;
    uint8_t batch_col_end = 0u;

    if (text == NULL || len == 0u) {
        return;
    }

    for (size_t i = 0; i < len; ++i) {
        const unsigned char uch = (unsigned char)text[i];

        if (terminal->state == VT100_STATE_GROUND
                && !terminal->insert_mode
                && !terminal->wrap_pending
                && uch >= 0x20u
                && uch <= 0x7Eu) {
            uint8_t write_row = terminal->cursor_row;
            uint8_t write_col = terminal->cursor_col;
            const vt100_terminal_cell_t cell = {
                .ch = text[i],
                .attr = vt100_terminal_current_attr(terminal),
                .style = vt100_terminal_current_style(terminal),
                .charset = vt100_terminal_current_charset(terminal),
            };
            const bool wrote = vt100_terminal_write_cell_fast(terminal, &cell, &write_row, &write_col);

            terminal->single_shift_pending = false;

            if (!wrote) {
                continue;
            }

            if (!batch_active) {
                vt100_terminal_hide_cursor(terminal);
                batch_active = true;
                batch_row = write_row;
                batch_col_start = write_col;
                batch_col_end = write_col;
            } else if (write_row != batch_row) {
                vt100_terminal_render_row_range(terminal, batch_row, batch_col_start, batch_col_end);
                vt100_terminal_show_cursor(terminal);
                vt100_terminal_hide_cursor(terminal);
                batch_row = write_row;
                batch_col_start = write_col;
                batch_col_end = write_col;
            } else if (write_col > batch_col_end) {
                batch_col_end = write_col;
            }

            if (terminal->wrap_pending) {
                vt100_terminal_render_row_range(terminal, batch_row, batch_col_start, batch_col_end);
                vt100_terminal_show_cursor(terminal);
                batch_active = false;
            }
            continue;
        }

        if (batch_active) {
            vt100_terminal_render_row_range(terminal, batch_row, batch_col_start, batch_col_end);
            vt100_terminal_show_cursor(terminal);
            batch_active = false;
        }

        vt100_terminal_putc(terminal, text[i]);
    }

    if (batch_active) {
        vt100_terminal_render_row_range(terminal, batch_row, batch_col_start, batch_col_end);
        vt100_terminal_show_cursor(terminal);
    }
}

void vt100_terminal_write(vt100_terminal_t *terminal, const char *text)
{
    if (text == NULL) {
        return;
    }

    vt100_terminal_write_n(terminal, text, strlen(text));
}
