#ifndef VT100_TERMINAL_H
#define VT100_TERMINAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VT100_TERMINAL_COLS 80u
#define VT100_TERMINAL_ROWS 35u
#define VT100_TERMINAL_CELL_WIDTH 6u
#define VT100_TERMINAL_CELL_HEIGHT 9u
#define VT100_TERMINAL_GLYPH_WIDTH 5u
#define VT100_TERMINAL_GLYPH_HEIGHT 7u
#define VT100_TERMINAL_GLYPH_Y_OFFSET 1u
#define VT100_TERMINAL_WIDTH_PIXELS (VT100_TERMINAL_COLS * VT100_TERMINAL_CELL_WIDTH)
#define VT100_TERMINAL_HEIGHT_PIXELS (VT100_TERMINAL_ROWS * VT100_TERMINAL_CELL_HEIGHT)
#define VT100_TERMINAL_BLINK_INTERVAL_MS 500u

typedef void (*vt100_terminal_output_fn)(const char *data, size_t len, void *user_data);

typedef struct {
    char ch;
    uint8_t attr;
    uint8_t style;
    uint8_t charset;
} vt100_terminal_cell_t;

typedef struct {
    uint16_t origin_x;
    uint16_t origin_y;
    uint8_t cursor_row;
    uint8_t cursor_col;
    uint8_t saved_row;
    uint8_t saved_col;
    uint8_t fg;
    uint8_t bg;
    uint8_t default_fg;
    uint8_t default_bg;
    uint8_t style;
    uint8_t saved_fg;
    uint8_t saved_bg;
    uint8_t saved_style;
    uint8_t g0_charset;
    uint8_t g1_charset;
    uint8_t g2_charset;
    uint8_t g3_charset;
    uint8_t gl_set;
    uint8_t saved_g0_charset;
    uint8_t saved_g1_charset;
    uint8_t saved_g2_charset;
    uint8_t saved_g3_charset;
    uint8_t saved_gl_set;
    uint8_t scroll_top;
    uint8_t scroll_bottom;
    uint8_t state;
    uint8_t csi_param_count;
    uint8_t csi_have_value;
    uint8_t csi_private;
    uint8_t single_shift_charset;
    uint8_t vt52_pending_row;
    uint16_t csi_value;
    bool cursor_visible;
    bool autowrap;
    bool saved_autowrap;
    bool wrap_pending;
    bool saved_wrap_pending;
    bool insert_mode;
    bool newline_mode;
    bool origin_mode;
    bool saved_origin_mode;
    bool screen_reverse;
    bool cursor_key_application_mode;
    bool saved_cursor_key_application_mode;
    bool keypad_application_mode;
    bool saved_keypad_application_mode;
    bool vt52_mode;
    bool saved_vt52_mode;
    bool vt52_graphics;
    bool saved_vt52_graphics;
    bool single_shift_pending;
    bool last_printable_valid;
    bool blink_visible;
    vt100_terminal_output_fn output_fn;
    void *output_user_data;
    uint32_t blink_elapsed_ms;
    bool tab_stops[VT100_TERMINAL_COLS];
    vt100_terminal_cell_t cells[VT100_TERMINAL_ROWS][VT100_TERMINAL_COLS];
    vt100_terminal_cell_t last_printable;
    uint16_t csi_params[8];
} vt100_terminal_t;

void vt100_terminal_init(vt100_terminal_t *terminal, uint16_t origin_x, uint16_t origin_y);
void vt100_terminal_set_output(vt100_terminal_t *terminal, vt100_terminal_output_fn output_fn, void *user_data);
void vt100_terminal_reset(vt100_terminal_t *terminal);
void vt100_terminal_putc(vt100_terminal_t *terminal, char ch);
void vt100_terminal_tick(vt100_terminal_t *terminal, uint32_t elapsed_ms);
void vt100_terminal_write_n(vt100_terminal_t *terminal, const char *text, size_t len);
void vt100_terminal_write(vt100_terminal_t *terminal, const char *text);
void vt100_terminal_render(vt100_terminal_t *terminal);

#ifdef __cplusplus
}
#endif

#endif
