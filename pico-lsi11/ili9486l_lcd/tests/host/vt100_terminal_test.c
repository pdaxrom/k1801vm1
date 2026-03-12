#include "vt100_terminal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    TEST_CHARSET_US = 0,
    TEST_CHARSET_UK = 1,
    TEST_CHARSET_DEC_SPECIAL = 2,
    TEST_STYLE_BLINK = 1u << 3,
};

typedef struct {
    char data[128];
    size_t len;
} output_buffer_t;

extern unsigned ili9486l_stub_begin_write_calls;
extern unsigned ili9486l_stub_wire_write_calls;
extern unsigned ili9486l_stub_wire_rect_calls;
void ili9486l_stub_reset_counters(void);

static void fail_check(const char *expr, const char *file, int line)
{
    fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expr);
}

#define CHECK(expr)                  \
  do {                               \
    if (!(expr)) {                   \
      fail_check(#expr, __FILE__, __LINE__); \
      return 1;                      \
    }                                \
  } while (0)

static void output_capture(const char *data, size_t len, void *user_data)
{
    output_buffer_t *buffer = (output_buffer_t *)user_data;

    if (buffer->len + len > sizeof(buffer->data)) {
        len = sizeof(buffer->data) - buffer->len;
    }

    memcpy(&buffer->data[buffer->len], data, len);
    buffer->len += len;
}

static void feed(vt100_terminal_t *terminal, const char *text)
{
    while (*text != '\0') {
        vt100_terminal_putc(terminal, *text++);
    }
}

static int test_osc_is_skipped(void)
{
    vt100_terminal_t terminal;

    vt100_terminal_init(&terminal, 0u, 0u);
    feed(&terminal, "A\x1b]0;ignored\x07" "B");

    CHECK(terminal.cells[0][0].ch == 'A');
    CHECK(terminal.cells[0][1].ch == 'B');
    CHECK(terminal.cursor_row == 0u);
    CHECK(terminal.cursor_col == 2u);
    return 0;
}

static int test_string_controls_are_skipped(void)
{
    vt100_terminal_t terminal;

    vt100_terminal_init(&terminal, 0u, 0u);
    feed(&terminal, "A\x1bPdcs\x1b\\B\x1b^pm\x1b\\C\x1b_apc\x1b\\D\x1bXsos\x1b\\E");

    CHECK(terminal.cells[0][0].ch == 'A');
    CHECK(terminal.cells[0][1].ch == 'B');
    CHECK(terminal.cells[0][2].ch == 'C');
    CHECK(terminal.cells[0][3].ch == 'D');
    CHECK(terminal.cells[0][4].ch == 'E');
    CHECK(terminal.cursor_col == 5u);
    return 0;
}

static int test_write_n_accepts_embedded_nul(void)
{
    vt100_terminal_t terminal;
    static const char data[] = {'A', '\0', 'B'};

    vt100_terminal_init(&terminal, 0u, 0u);
    vt100_terminal_write_n(&terminal, data, sizeof(data));

    CHECK(terminal.cells[0][0].ch == 'A');
    CHECK(terminal.cells[0][1].ch == 'B');
    CHECK(terminal.cursor_row == 0u);
    CHECK(terminal.cursor_col == 2u);
    return 0;
}

static int test_write_n_batches_printable_ascii_runs(void)
{
    vt100_terminal_t terminal;

    vt100_terminal_init(&terminal, 0u, 0u);
    ili9486l_stub_reset_counters();
    vt100_terminal_write_n(&terminal, "ABC", 3u);

    CHECK(terminal.cells[0][0].ch == 'A');
    CHECK(terminal.cells[0][1].ch == 'B');
    CHECK(terminal.cells[0][2].ch == 'C');
    CHECK(terminal.cursor_row == 0u);
    CHECK(terminal.cursor_col == 3u);
    CHECK(ili9486l_stub_begin_write_calls == 1u);
    CHECK(ili9486l_stub_wire_write_calls == VT100_TERMINAL_CELL_HEIGHT);
    CHECK(ili9486l_stub_wire_rect_calls == 2u);
    return 0;
}

static int test_can_and_sub_cancel_sequences(void)
{
    vt100_terminal_t terminal;

    vt100_terminal_init(&terminal, 0u, 0u);
    feed(&terminal, "A\x1b[31");
    vt100_terminal_putc(&terminal, '\x18');
    feed(&terminal, "B\x1b]title");
    vt100_terminal_putc(&terminal, '\x1a');
    feed(&terminal, "C");

    CHECK(terminal.cells[0][0].ch == 'A');
    CHECK(terminal.cells[0][1].ch == 'B');
    CHECK(terminal.cells[0][2].ch == 'C');
    CHECK(terminal.cells[0][1].attr == 0x0Fu);
    CHECK(terminal.cells[0][2].attr == 0x0Fu);
    CHECK(terminal.cursor_col == 3u);
    return 0;
}

static int test_decom_cpr_is_relative(void)
{
    vt100_terminal_t terminal;
    output_buffer_t output = {{0}, 0u};

    vt100_terminal_init(&terminal, 0u, 0u);
    vt100_terminal_set_output(&terminal, output_capture, &output);
    feed(&terminal, "\x1b[5;10r\x1b[?6h\x1b[3;4H\x1b[6n");

    CHECK(output.len == strlen("\x1b[3;4R"));
    CHECK(memcmp(output.data, "\x1b[3;4R", output.len) == 0);
    CHECK(terminal.cursor_row == 6u);
    CHECK(terminal.cursor_col == 3u);
    return 0;
}

static int test_vt52_cursoring_and_exit(void)
{
    vt100_terminal_t terminal;

    vt100_terminal_init(&terminal, 0u, 0u);
    feed(&terminal, "\x1b[?2l\x1bY\x22\x24Z");

    CHECK(terminal.vt52_mode);
    CHECK(terminal.cells[2][4].ch == 'Z');

    feed(&terminal, "\x1b<\x1b[2;3HQ");

    CHECK(!terminal.vt52_mode);
    CHECK(terminal.cells[1][2].ch == 'Q');
    return 0;
}

static int test_single_shift_uses_g2_and_g3_once(void)
{
    vt100_terminal_t terminal;

    vt100_terminal_init(&terminal, 0u, 0u);
    feed(&terminal, "\x1b*0\x1b+A\x1bNq\x1bO#x");

    CHECK(terminal.cells[0][0].ch == 'q');
    CHECK(terminal.cells[0][0].charset == TEST_CHARSET_DEC_SPECIAL);
    CHECK(terminal.cells[0][1].ch == '#');
    CHECK(terminal.cells[0][1].charset == TEST_CHARSET_UK);
    CHECK(terminal.cells[0][2].ch == 'x');
    CHECK(terminal.cells[0][2].charset == TEST_CHARSET_US);
    CHECK(!terminal.single_shift_pending);
    return 0;
}

static int test_decsc_decrc_restore_g2_g3(void)
{
    vt100_terminal_t terminal;

    vt100_terminal_init(&terminal, 0u, 0u);
    feed(&terminal, "\x1b*0\x1b+A\x1b" "7\x1b*B\x1b+0\x1b" "8\x1bNq\x1bO#");

    CHECK(terminal.cells[0][0].ch == 'q');
    CHECK(terminal.cells[0][0].charset == TEST_CHARSET_DEC_SPECIAL);
    CHECK(terminal.cells[0][1].ch == '#');
    CHECK(terminal.cells[0][1].charset == TEST_CHARSET_UK);
    return 0;
}

static int test_decsc_decrc_restore_modes(void)
{
    vt100_terminal_t terminal;

    vt100_terminal_init(&terminal, 0u, 0u);
    feed(&terminal, "\x1b[?7l\x1b[?1h\x1b=\x1b*0\x1b+A\x1b" "7");

    terminal.autowrap = true;
    terminal.cursor_key_application_mode = false;
    terminal.keypad_application_mode = false;
    terminal.g2_charset = TEST_CHARSET_US;
    terminal.g3_charset = TEST_CHARSET_US;

    feed(&terminal, "\x1b" "8");

    CHECK(!terminal.autowrap);
    CHECK(terminal.cursor_key_application_mode);
    CHECK(terminal.keypad_application_mode);
    CHECK(terminal.g2_charset == TEST_CHARSET_DEC_SPECIAL);
    CHECK(terminal.g3_charset == TEST_CHARSET_UK);
    return 0;
}

static int test_esc_hash_3_to_6_are_noop(void)
{
    vt100_terminal_t terminal;

    vt100_terminal_init(&terminal, 0u, 0u);
    feed(&terminal, "A\x1b#3\x1b#4\x1b#5\x1b#6B");

    CHECK(terminal.cells[0][0].ch == 'A');
    CHECK(terminal.cells[0][1].ch == 'B');
    CHECK(terminal.cursor_row == 0u);
    CHECK(terminal.cursor_col == 2u);
    return 0;
}

static int test_blink_tick_toggles_visibility(void)
{
    vt100_terminal_t terminal;

    vt100_terminal_init(&terminal, 0u, 0u);
    feed(&terminal, "\x1b[5mA");

    CHECK((terminal.cells[0][0].style & TEST_STYLE_BLINK) != 0u);
    CHECK(terminal.blink_visible);

    vt100_terminal_tick(&terminal, VT100_TERMINAL_BLINK_INTERVAL_MS - 1u);
    CHECK(terminal.blink_visible);

    vt100_terminal_tick(&terminal, 1u);
    CHECK(!terminal.blink_visible);

    vt100_terminal_tick(&terminal, VT100_TERMINAL_BLINK_INTERVAL_MS);
    CHECK(terminal.blink_visible);
    return 0;
}

static int test_console_shortcuts_are_handled_in_library(void)
{
    vt100_terminal_t terminal;

    vt100_terminal_init(&terminal, 0u, 0u);
    CHECK(!vt100_terminal_getch(&terminal, -1));
    CHECK(terminal.scroll_bottom == 33u);

    CHECK(!vt100_terminal_getch(&terminal, '\x05'));
    CHECK(!vt100_terminal_getch(&terminal, '2'));
    CHECK(terminal.scroll_bottom == 29u);

    CHECK(!vt100_terminal_getch(&terminal, '\x05'));
    CHECK(!vt100_terminal_getch(&terminal, '3'));
    CHECK(terminal.scroll_bottom == 23u);

    CHECK(!vt100_terminal_getch(&terminal, '\x05'));
    CHECK(!vt100_terminal_getch(&terminal, '1'));
    CHECK(terminal.scroll_bottom == 33u);

    CHECK(!vt100_terminal_getch(&terminal, '\x05'));
    CHECK(!vt100_terminal_getch(&terminal, 'p'));

    for (uint8_t row = 0; row < 34u; ++row) {
        vt100_terminal_write(&terminal, "L\r\n");
    }
    vt100_terminal_write(&terminal, "Y");
    CHECK(terminal.cells[33][0].ch != 'Y');

    CHECK(!vt100_terminal_getch(&terminal, '\x05'));
    CHECK(!vt100_terminal_getch(&terminal, 's'));
    CHECK(terminal.cells[33][0].ch == 'Y');
    return 0;
}

static int test_paged_mode_emits_xoff_xon(void)
{
    vt100_terminal_t terminal;
    output_buffer_t output = {{0}, 0u};

    vt100_terminal_init(&terminal, 0u, 0u);
    vt100_terminal_set_output(&terminal, output_capture, &output);
    CHECK(!vt100_terminal_getch(&terminal, -1));
    CHECK(!vt100_terminal_getch(&terminal, '\x05'));
    CHECK(!vt100_terminal_getch(&terminal, 'p'));

    terminal.cursor_row = 33u;
    terminal.cursor_col = 79u;
    terminal.wrap_pending = true;
    vt100_terminal_write(&terminal, "Y");

    CHECK(output.len == 1u);
    CHECK((unsigned char)output.data[0] == 0x13u);
    CHECK(terminal.cells[0][0].ch != 'Y');

    CHECK(!vt100_terminal_getch(&terminal, 'a'));
    CHECK(!vt100_terminal_getch(&terminal, ' '));
    CHECK(output.len == 2u);
    CHECK((unsigned char)output.data[1] == 0x11u);

    CHECK(!vt100_terminal_getch(&terminal, -1));
    CHECK(terminal.cells[0][0].ch == 'Y');
    CHECK(terminal.cells[0][1].ch == ' ');
    return 0;
}

int main(void)
{
    static const struct {
        const char *name;
        int (*fn)(void);
    } tests[] = {
        {"osc_is_skipped", test_osc_is_skipped},
        {"string_controls_are_skipped", test_string_controls_are_skipped},
        {"write_n_accepts_embedded_nul", test_write_n_accepts_embedded_nul},
        {"write_n_batches_printable_ascii_runs", test_write_n_batches_printable_ascii_runs},
        {"can_and_sub_cancel_sequences", test_can_and_sub_cancel_sequences},
        {"decom_cpr_is_relative", test_decom_cpr_is_relative},
        {"vt52_cursoring_and_exit", test_vt52_cursoring_and_exit},
        {"single_shift_uses_g2_and_g3_once", test_single_shift_uses_g2_and_g3_once},
        {"decsc_decrc_restore_g2_g3", test_decsc_decrc_restore_g2_g3},
        {"decsc_decrc_restore_modes", test_decsc_decrc_restore_modes},
        {"esc_hash_3_to_6_are_noop", test_esc_hash_3_to_6_are_noop},
        {"blink_tick_toggles_visibility", test_blink_tick_toggles_visibility},
        {"console_shortcuts_are_handled_in_library", test_console_shortcuts_are_handled_in_library},
        {"paged_mode_emits_xoff_xon", test_paged_mode_emits_xoff_xon},
    };

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        if (tests[i].fn() != 0) {
            fprintf(stderr, "test failed: %s\n", tests[i].name);
            return 1;
        }
    }

    puts("all host VT100 tests passed");
    return 0;
}
