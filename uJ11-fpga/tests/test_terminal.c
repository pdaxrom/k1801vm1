#include <assert.h>
#include <stdio.h>
#include "storage.h"
#include "terminal.h"
#include "terminal_test.h"

u32 terminal_test_byte, terminal_test_enable;
u32 terminal_test_text[2400], terminal_test_pixels[19200];

void poll_io(void)
{
}
static u32 video[8];

u32 *terminal_test_video(unsigned n)
{
	assert(n < 8);
	if (n == 4) {
		video[n] = 0;
	}
	return video + n;
}

static void output(const char *text)
{
	while (*text) {
		terminal_test_byte = 256u | (unsigned char)*text++;
		terminal_input();
	}
	terminal_test_byte = 0;
}

static unsigned letter(unsigned y, unsigned x)
{
	return terminal_test_text[y * 80 + x] & 127;
}

int main(void)
{
	terminal_init();
	assert(terminal_test_enable == 1);
	assert((video[1] & 65535) == (TERMINAL_FRAME & 65535));
	output("ABC\r\nXYZ");
	assert(letter(0, 0) == 'A' && letter(0, 2) == 'C' && letter(1, 0) == 'X');
	output("\033[2J\033[HHELLO\033[2;10HWORLD");
	assert(letter(0, 0) == 'H' && letter(1, 9) == 'W' && letter(1, 13) == 'D');
	assert(letter(1, 0) == ' ');
	output("\033[1;1H\033[31;44;7mR\033[0mW\033[K");
	assert(terminal_test_text[0] == ('R' | 9u << 8 | 4u << 12 | 1u << 16));
	assert(terminal_test_text[1] == ('W' | 15u << 8));
	output("\033[999;999HZ\r\n");
	/* Scroll rotates physical rows instead of copying the 75-KiB bitmap. */
	assert(letter(29, 79) == 'Z');
	for (unsigned i = 0; i < 2500; i++) {
		terminal_render();
	}
	assert((video[3] & 65535) == 8);
	output("\033[2J\033[H\033[?2l\033Y!#Q\033D!");
	assert(letter(2, 3) == '!'); /* origin 1 plus logical row 1 */
	output("\033<\033[?25l\033[H\033(0qx\033(B");
	assert(letter(1, 0) == '-' && letter(1, 1) == '|');
	output("\r\na\bZ\tT\177");
	assert(letter(2, 0) == 'Z' && letter(2, 8) == 'T' && letter(2, 9) == ' ');
	for (unsigned i = 0; i < 2500; i++) {
		terminal_render();
	}
	/* The letter Z has lit pixels; erased cells and the hidden cursor remain black. */
	unsigned lit = 0;
	for (unsigned scan = 0; scan < 8; scan++) {
		lit |= terminal_test_pixels[(2 * 8 + scan) * 80];
		assert(terminal_test_pixels[(2 * 8 + scan) * 80 + 9] == 0);
	}
	assert(lit != 0);
	puts("PASS terminal: CR/LF, erase, cursor, SGR, bounds, ring scroll, VT52 and graphics");
	return 0;
}
