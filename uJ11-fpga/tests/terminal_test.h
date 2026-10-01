#ifndef UJ11_TERMINAL_TEST_H
#define UJ11_TERMINAL_TEST_H
extern u32 terminal_test_byte, terminal_test_enable;
extern u32 terminal_test_text[2400], terminal_test_pixels[19200];
extern u32 terminal_test_glyphs[768];
u32 *terminal_test_video(unsigned n);
u32 terminal_test_pop(void);
u32 *terminal_test_pixel(unsigned n);
#define TERM_BYTE terminal_test_pop()
#define TERM_ENABLE terminal_test_enable
#define VIDEO(n) (*terminal_test_video(n))
#define TEXT(n) terminal_test_text[n]
#define GLYPH(n) terminal_test_glyphs[n]
#define PIXEL(n) (*terminal_test_pixel(n))
/* The host test has no PDP bus request; production uses the real bridge. */
#undef BUS_STATUS
#define BUS_STATUS 0u
#endif
