#include <assert.h>
#include <stdio.h>
#include "storage.h"
#include "terminal.h"
#include "terminal_test.h"
#include "terminal_font.h"

u32 terminal_test_byte, terminal_test_enable;
u32 terminal_test_text[2400], terminal_test_pixels[19200];
u32 terminal_test_glyphs[768];

void poll_io(void)
{
}
static u32 video[8];
static unsigned video_busy, pixel_writes;

u32 terminal_test_pop(void)
{
	u32 byte = terminal_test_byte;
	terminal_test_byte = 0;
	return byte;
}

u32 *terminal_test_pixel(unsigned n)
{
	assert(n < 19200);
	pixel_writes++;
	return terminal_test_pixels + n;
}

u32 *terminal_test_video(unsigned n)
{
	assert(n < 8);
	if (n == 4) {
		video[n] = video_busy;
	}
	return video + n;
}

static void output(const char *text)
{
	while (*text) {
		terminal_test_byte = 256u | (unsigned char)*text++;
		unsigned steps = 0;
		while (terminal_test_byte) {
			terminal_input();
			if (terminal_test_byte) {
				terminal_render();
			}
			assert(++steps < 2500);
		}
	}
	terminal_test_byte = 0;
}

static unsigned letter(unsigned y, unsigned x)
{
	return terminal_test_text[y * 80 + x] & 127;
}

static void streamed(const char *text)
{
	while (*text) {
		terminal_test_byte = 256u | (unsigned char)*text++;
		unsigned steps = 0;
		do {
			terminal_input();
			terminal_render();
			assert(++steps < 2500);
		} while (terminal_test_byte);
	}
}

static void row_visible(unsigned y)
{
	/* Inspect the bitmap before pausing input or allowing a deferred drain.
	 * Tabs, CR and LF must not leave the directory's right columns pending. */
	for (unsigned x = 0; x < 80; x++) {
		unsigned ch = letter(y, x);
		assert(ch >= 32 && ch < 127);
		for (unsigned scan = 0; scan < 8; scan++) {
			u32 expected = 0;
			for (unsigned bit = 0; bit < 8; bit++) {
				if (terminal_font[ch - 32][scan] & (1u << bit)) {
					expected |= 15u << (bit * 4);
				}
			}
			if (terminal_test_pixels[(y * 8 + scan) * 80 + x] != expected) {
				fprintf(stderr, "Incomplete streamed directory: row=%u col=%u ch=%c scan=%u\n", y, x, ch, scan);
				assert(0);
			}
		}
	}
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
	/* Publish immediately even with an entire dirty screen. Hold the next
	 * byte and recycled-row writes until PAL acknowledges the new mapping. */
	terminal_render();
	assert((video[3] & 65535) == 8);
	video_busy = 1;
	terminal_test_byte = 256u | '!';
	for (unsigned i = 0; i < 640; i++) {
		terminal_test_pixels[i] = 0x11111111;
	}
	for (unsigned i = 0; i < 10; i++) {
		terminal_input();
		terminal_render();
	}
	assert(terminal_test_byte == (256u | '!'));
	for (unsigned i = 0; i < 640; i++) {
		assert(terminal_test_pixels[i] == 0x11111111);
	}
	video_busy = 0;
	unsigned before = pixel_writes;
	terminal_render();
	assert(pixel_writes - before == 640);
	for (unsigned i = 0; i < 640; i++) {
		assert(terminal_test_pixels[i] == 0);
	}
	terminal_test_byte = 0;
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
	/* A clean row must forget the high bound left by a previous full erase. */
	output("\033[H");
	for (unsigned i = 0; i < 2500; i++) {
		terminal_render();
	}
	before = pixel_writes;
	output("X");
	for (unsigned i = 0; i < 2500; i++) {
		terminal_render();
	}
	assert(pixel_writes - before == 8);
	/* The printable byte causing an automatic wrap belongs to the recycled
	 * row and must survive its fast pixel clear. */
	output("\033[30;80HZW");
	for (unsigned i = 0; i < 2500; i++) {
		terminal_render();
	}
	assert(letter(1, 0) == 'W');
	for (unsigned scan = 0; scan < 8; scan++) {
		u32 expected = 0;
		for (unsigned bit = 0; bit < 8; bit++) {
			if (terminal_font['W' - 32][scan] & (1u << bit)) {
				expected |= 15u << (bit * 4);
			}
		}
		assert(terminal_test_pixels[(8 + scan) * 80] == expected);
	}
	output("\033[2J\033[H\033[?25l");
	for (unsigned i = 0; i < 2500; i++) {
		terminal_render();
	}
	unsigned start = (video[3] & 65535u) / 8;
	for (unsigned i = 0; i < 3; i++) {
		streamed("RT11XM.SYS\t111 20-Dec-85\tRT11FB.SYS\t86 20-Dec-85\r\n");
		row_visible((start + i) % 30);
	}
	puts("PASS terminal: parser, ring scroll ordering/ack/clear, bounded redraw, streamed tabbed directory, VT52 and graphics");
	return 0;
}
