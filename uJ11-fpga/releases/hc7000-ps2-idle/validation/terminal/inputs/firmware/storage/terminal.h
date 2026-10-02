#ifndef UJ11_TERMINAL_H
#define UJ11_TERMINAL_H
#ifdef UJ11_TERMINAL
#define TERMINAL_RESERVE 0x1e4000u
#define TERMINAL_TEXT 0x1e4000u
/* Expanded 4-bpp glyph masks occupy 3 KiB after the 9600-byte text grid. */
#define TERMINAL_GLYPHS (TERMINAL_TEXT + 9600u)
#define TERMINAL_FRAME 0x1ec000u
#define TERMINAL_COLS 80u
#define TERMINAL_ROWS 30u
#ifdef UJ11_KEYBOARD
unsigned terminal_keymode(void);
#endif
void terminal_init(void);
void terminal_input(void);
void terminal_render(void);
void terminal_service(void);
#else
#define terminal_init() ((void)0)
#define terminal_input() ((void)0)
#define terminal_render() ((void)0)
#define terminal_service() ((void)0)
#endif
#endif
