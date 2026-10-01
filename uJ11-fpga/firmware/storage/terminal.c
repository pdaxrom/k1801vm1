#include "storage.h"
#include "terminal.h"

#ifdef UJ11_TERMINAL
#ifdef UJ11_TERMINAL_TEST
#include "terminal_test.h"
#else
#define TERM_BYTE MMIO(0x40000500u)
#define TERM_ENABLE MMIO(0x40000504u)
#define VIDEO(n) MMIO(0x40000400u + 4u * (n))
#define TEXT(n) MMIO(0x60000000u + TERMINAL_TEXT + 4u * (n))
#define PIXEL(n) MMIO(0x60000000u + TERMINAL_FRAME + 4u * (n))
#endif
#include "terminal_font.h"
static const uint16_t pixels4[16] = {
	0x0000, 0x000f, 0x00f0, 0x00ff, 0x0f00, 0x0f0f, 0x0ff0, 0x0fff,
	0xf000, 0xf00f, 0xf0f0, 0xf0ff, 0xff00, 0xff0f, 0xfff0, 0xffff
};

/* Output-only console mirror. The host UART remains the input endpoint and
 * answers terminal queries. No competing device-identification reply is sent. */
static uint8_t row, col, saved_row, saved_col, origin, scroll_stage;
static uint8_t fg, bg, reverse, underline, wrap, state, private_mode;
static uint8_t graphics, graphics_g0, graphics_g1, charset, cursor, vt52;
static uint8_t low[TERMINAL_ROWS], high[TERMINAL_ROWS];
static uint16_t params[3];
static uint8_t parameter;
static uint16_t scroll_word;

static unsigned disk_pending(void)
{
#ifndef UJ11_TERMINAL_TEST
	for (unsigned bank = 0; bank < NCONTROLLERS; bank++) {
		if (controllers[bank].busy) {
			return 1;
		}
	}
#endif
	return 0;
}

static unsigned physical_row(unsigned y)
{
	unsigned n = origin + y;
	return n >= TERMINAL_ROWS ? n - TERMINAL_ROWS : n;
}

static void dirty(unsigned y, unsigned x)
{
	unsigned n = physical_row(y);
	if (low[n] == TERMINAL_COLS) {
		low[n] = high[n] = x;
		return;
	}
	if (low[n] > x) {
		low[n] = x;
	}
	if (high[n] < x) {
		high[n] = x;
	}
}

static u32 blank(void)
{
	return 32u | ((u32)fg << 8) | ((u32)bg << 12);
}

static void erase(unsigned y, unsigned first, unsigned last)
{
	unsigned n = physical_row(y);
	u32 cell = blank();
	for (unsigned x = first; x <= last; x++) {
		poll_io();
		if (TEXT(n * TERMINAL_COLS + x) != cell) {
			TEXT(n * TERMINAL_COLS + x) = cell;
			dirty(y, x);
		}
	}
}

static void linefeed(void)
{
	wrap = 0;
	if (row < TERMINAL_ROWS - 1) {
		row++;
	} else {
		origin++;
		if (origin == TERMINAL_ROWS) {
			origin = 0;
		}
		erase(row, 0, TERMINAL_COLS - 1);
		/* Pixel fill will clear the old row. Any text written by an automatic
		 * wrap after this point must keep its own new dirty range. */
		low[physical_row(row)] = TERMINAL_COLS;
		scroll_stage = 1;
	}
}

static unsigned value(unsigned n)
{
	return params[n] ? params[n] : 1;
}

static unsigned bound(unsigned n, unsigned limit)
{
	return n < limit ? n : limit - 1;
}

static void clear_display(unsigned mode)
{
	if (mode == 0) {
		erase(row, col, TERMINAL_COLS - 1);
		for (unsigned y = row + 1; y < TERMINAL_ROWS; y++) {
			erase(y, 0, TERMINAL_COLS - 1);
		}
	} else if (mode == 1) {
		for (unsigned y = 0; y < row; y++) {
			erase(y, 0, TERMINAL_COLS - 1);
		}
		erase(row, 0, col);
	} else if (mode == 2) {
		for (unsigned y = 0; y < TERMINAL_ROWS; y++) {
			erase(y, 0, TERMINAL_COLS - 1);
		}
	}
}

static void csi(unsigned ch)
{
	unsigned n = value(0);
	if (ch == 'H' || ch == 'f') {
		row = bound(value(0) - 1, TERMINAL_ROWS);
		col = bound(value(1) - 1, TERMINAL_COLS);
	} else if (ch == 'A') {
		row = n > row ? 0 : row - n;
	} else if (ch == 'B') {
		row = bound(row + n, TERMINAL_ROWS);
	} else if (ch == 'C') {
		col = bound(col + n, TERMINAL_COLS);
	} else if (ch == 'D') {
		col = n > col ? 0 : col - n;
	} else if (ch == 'J') {
		clear_display(params[0]);
	} else if (ch == 'K') {
		if (params[0] <= 2) {
			erase(row, params[0] == 0 ? col : 0, params[0] == 1 ? col : TERMINAL_COLS - 1);
		}
	} else if (ch == 'm') {
		for (unsigned i = 0; i <= parameter; i++) {
			n = params[i];
			if (!n) {
				fg = 15;
				bg = reverse = underline = 0;
			} else if (n == 4 || n == 24) {
				underline = n == 4;
			} else if (n == 7 || n == 27) {
				reverse = n == 7;
			} else if (n >= 30 && n <= 37) {
				fg = n - 30 + 8;
			} else if (n >= 40 && n <= 47) {
				bg = n - 40;
			}
		}
	} else if ((ch == 'h' || ch == 'l') && private_mode && params[0] == 25) {
		cursor = ch == 'h';
	} else if ((ch == 'h' || ch == 'l') && private_mode && params[0] == 2) {
		vt52 = ch == 'l';
	} else if (ch == 's') {
		saved_row = row;
		saved_col = col;
	} else if (ch == 'u') {
		row = saved_row;
		col = saved_col;
	}
	wrap = 0;
}

static void put(unsigned ch)
{
	ch &= 127;
	dirty(row, col);
	if (ch == 27) {
		state = 1;
	} else if (state == 1) {
		state = 0;
		if (ch == '[') {
			state = 2;
			parameter = private_mode = 0;
			params[0] = params[1] = params[2] = 0;
		} else if (ch == 'Y') {
			state = 3;
		} else if (ch == '(' || ch == ')') {
			state = ch == '(' ? 5 : 6;
		} else if (ch == 'H') {
			row = col = wrap = 0;
		} else if (ch == 'A' && row) {
			row--;
		} else if (ch == 'B') {
			row = bound(row + 1, TERMINAL_ROWS);
		} else if (ch == 'C') {
			col = bound(col + 1, TERMINAL_COLS);
		} else if (ch == 'D') {
			if (!vt52) {
				linefeed();
			} else if (col) {
				col--;
			}
		} else if (ch == '<') {
			vt52 = 0;
		} else if (ch == 'J') {
			clear_display(0);
		} else if (ch == 'K') {
			erase(row, col, TERMINAL_COLS - 1);
		} else if (ch == '7') {
			saved_row = row;
			saved_col = col;
		} else if (ch == '8') {
			row = saved_row;
			col = saved_col;
		} else if (ch == 'F' || ch == 'G') {
			graphics = ch == 'F';
		}
	} else if (state == 2) {
		if (ch >= '0' && ch <= '9') {
			unsigned n = params[parameter] * 10u + ch - '0';
			params[parameter] = n > 999 ? 999 : n;
		} else if (ch == ';') {
			if (parameter < 2) {
				parameter++;
			}
		} else if (ch == '?') {
			private_mode = 1;
		} else {
			csi(ch);
			state = 0;
		}
	} else if (state == 3) {
		row = bound(ch < 32 ? 0 : ch - 32, TERMINAL_ROWS);
		state = 4;
	} else if (state == 4) {
		col = bound(ch < 32 ? 0 : ch - 32, TERMINAL_COLS);
		state = wrap = 0;
	} else if (state == 5 || state == 6) {
		if (state == 5) {
			graphics_g0 = ch == '0';
		} else {
			graphics_g1 = ch == '0';
		}
		state = 0;
	} else if (ch == 14 || ch == 15) {
		charset = ch == 14;
	} else if (ch == '\r') {
		col = wrap = 0;
	} else if (ch == '\n') {
		linefeed();
	} else if (ch == '\b') {
		if (col) {
			col--;
		}
		wrap = 0;
	} else if (ch == '\t') {
		col = bound((col + 8u) & ~7u, TERMINAL_COLS);
		wrap = 0;
	} else if (ch >= 32 && ch < 127) {
		if (wrap) {
			col = 0;
			linefeed();
		}
		if (graphics || (charset ? graphics_g1 : graphics_g0)) {
			if (ch == 'q') {
				ch = '-';
			} else if (ch == 'x') {
				ch = '|';
			} else if (ch >= 'j' && ch <= 'n') {
				ch = '+';
			}
		}
		TEXT(physical_row(row) * TERMINAL_COLS + col) = ch | ((u32)fg << 8) | ((u32)bg << 12) |
		        ((u32)reverse << 16) | ((u32)underline << 17);
		dirty(row, col);
		if (col == TERMINAL_COLS - 1) {
			wrap = 1;
		} else {
			col++;
		}
	}
	dirty(row, col);
}

void terminal_input(void)
{
	/* Keep the ring mapping stable until PAL has applied this linefeed and
	 * the recycled row is clear. Disks continue through the main scheduler. */
	if (scroll_stage || disk_pending()) {
		return;
	}
	u32 byte = TERM_BYTE;
	if (byte & 256) {
		put(byte);
	}
}

void terminal_render(void)
{
	/* Disk register requests win. One input byte and one painted cell per
	 * scheduler turn keep a continuous output stream visible as it arrives. */
	if ((BUS_STATUS & 7) || disk_pending()) {
		return;
	}
	if (scroll_stage == 1) {
		if (!(VIDEO(4) & 1)) {
			VIDEO(3) = origin * 8u | 0x30000u;
			VIDEO(4) = 0x30001u;
			scroll_stage = 2;
		}
		return;
	}
	if (scroll_stage == 2) {
		if (!(VIDEO(4) & 1)) {
			scroll_stage = 3;
			scroll_word = 0;
		}
	}
	if (scroll_stage == 3) {
		/* The former top row is now at the bottom. Clear its pixels before
		 * parsing new text, rather than displaying new glyphs at the old top.
		 * Uniform stores avoid 80 font lookups and resume after disk work. */
		unsigned y = physical_row(TERMINAL_ROWS - 1);
		u32 colour = bg | (bg << 4);
		colour |= colour << 8;
		colour |= colour << 16;
		while (scroll_word < 8u * TERMINAL_COLS) {
			if (!(scroll_word & 15u)) {
				poll_io();
				if (disk_pending()) {
					return;
				}
			}
			PIXEL(y * 8u * TERMINAL_COLS + scroll_word) = colour;
			scroll_word++;
		}
		scroll_stage = 0;
		return;
	}
	for (unsigned y = 0; y < TERMINAL_ROWS; y++) {
		/* This physical row still contains the visible old top until ack.
		 * Other pending cells can be painted during the vertical-blank wait. */
		if (scroll_stage == 2 && y == physical_row(TERMINAL_ROWS - 1)) {
			continue;
		}
		unsigned x = low[y];
		if (x >= TERMINAL_COLS) {
			continue;
		}
		u32 cell = TEXT(y * TERMINAL_COLS + x);
		unsigned ch = cell & 127u, a = (cell >> 8) & 15u, b = (cell >> 12) & 15u;
		if (cell & 65536u) {
			unsigned t = a;
			a = b;
			b = t;
		}
		if (ch < 32 || ch >= 127) {
			ch = 32;
		}
		u32 front = a | (a << 4), back = b | (b << 4);
		front |= front << 8;
		front |= front << 16;
		back |= back << 8;
		back |= back << 16;
		for (unsigned scan = 0; scan < 8; scan++) {
			poll_io();
			if (disk_pending()) {
				return;
			}
			unsigned bits = terminal_font[ch - 32][scan];
			if (scan == 7 && ((cell & 131072u) || (cursor && y == physical_row(row) && x == col))) {
				bits = 255;
			}
			u32 mask = pixels4[bits & 15] | ((u32)pixels4[bits >> 4] << 16);
			PIXEL((y * 8 + scan) * 80 + x) = back ^ ((front ^ back) & mask);
		}
		low[y] = x == high[y] ? TERMINAL_COLS : x + 1;
		return;
	}
}

void terminal_init(void)
{
	/* Keep RAM reservation and PAL active before J11 prints its first byte. */
	fg = 15;
	cursor = 1;
	for (unsigned i = 0; i < TERMINAL_COLS * TERMINAL_ROWS; i++) {
		TEXT(i) = blank();
	}
	for (unsigned i = 0; i < 19200; i++) {
		PIXEL(i) = 0;
	}
	for (unsigned y = 0; y < TERMINAL_ROWS; y++) {
		low[y] = TERMINAL_COLS;
	}
	for (unsigned i = 0; i < 16; i++) {
		unsigned rgb = ((i & 1) ? 0xe0u : 0) | ((i & 2) ? 0x1cu : 0) | ((i & 4) ? 3u : 0);
		if (i < 8) {
			rgb = (rgb >> 1) & 0x6du;
		}
		VIDEO(5) = i | 0x30000u;
		VIDEO(6) = rgb | 0x30000u;
	}
	VIDEO(1) = (TERMINAL_FRAME & 65535u) | 0x30000u;
	VIDEO(2) = (TERMINAL_FRAME >> 16) | 0x30000u;
	VIDEO(3) = 0x30000u;
	VIDEO(0) = 0x30001u;
	VIDEO(4) = 0x30001u;
	while (VIDEO(4) & 1) {
	}
	TERM_ENABLE = 1;
}
#endif
