/*
 * emu11.c
 *
 *  Created on: 19.10.2016
 *      Author: Alexander Chukov <sash@pdaXrom.org>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ncurses.h>

#include "core/core.h"
#include "core/disas.h"

#include "core/hardware.h"

enum {
	WIN_DIS = 0,
	WIN_REGS,
	WIN_DUMP,
	WIN_CONS
};

struct _windows {
	int in_use;
	int scroll;
	int startx, starty, width, height;
	WINDOW *win;
} windows[4];

static void dump_regs(regs *r, WINDOW *win)
{
#define F(f,s) ((p & SET_BIT(f))?s:'-')
	word p = r->psw;
    mvwprintw(win, 0, 0, "%c - - %c %c %c %c %c\n", F(BIT_P, 'P'), F(BIT_T, 'T'), F(BIT_N, 'N'), F(BIT_Z, 'Z'), F(BIT_V, 'V'), F(BIT_C, 'C'));
    mvwprintw(win, 2, 0, "R0=%06o\nR1=%06o\nR2=%06o\nR3=%06o\nR4=%06o\nR5=%06o\nSP=%06o\nPC=%06o\nPS=%06o\n",
	    r->r[0], r->r[1], r->r[2], r->r[3], r->r[4], r->r[5], r->r[6], r->r[7], r->psw);
#undef F
}

void dump_mem(regs *r, word start, word length, byte mode) {
	char buf[9];
	int i = 0;

	buf[8] = 0;

	while (i < length) {
		if (i % 8 == 0) {
			wprintw(windows[WIN_DUMP].win, "%06o: ", start);
		}
		if (mode) {
			byte bl = r->load_byte(r, start++);
			byte bh = r->load_byte(r, start++);
			buf[(i++) % 8] = (bl >= 32)?bl:'.';
			buf[i % 8] = (bh >= 32)?bh:'.';
			word w = (bh << 8) | bl;
			wprintw(windows[WIN_DUMP].win, "%06o ", w);
		} else {
			byte b = r->load_byte(r, start++);
			buf[i % 8] = (b >= 32)?b:'.';
			wprintw(windows[WIN_DUMP].win, "%03o ", b);
		}
		if (i % 8 == 7) {
			wprintw(windows[WIN_DUMP].win, "%s\n", buf);
			wrefresh(windows[WIN_DUMP].win);
			buf[8] = 0;
		}
		i++;
	}
	buf[i % 8] = 0;

	if (i % 8 != 0) {
		wprintw(windows[WIN_DUMP].win, "%s\n", buf);
		wrefresh(windows[WIN_DUMP].win);
	}
}

static void calc_windows()
{
	windows[WIN_DIS].startx = 0;
	windows[WIN_DIS].starty = 0;
	windows[WIN_DIS].width = COLS / 2 - 2;
	windows[WIN_DIS].height = (LINES / 6) * 4 - 1;
	windows[WIN_DIS].in_use = 1;
	windows[WIN_DIS].scroll = 1;

	windows[WIN_REGS].startx = COLS / 2 + 2;
	windows[WIN_REGS].starty = 0;
	windows[WIN_REGS].width = COLS / 2 - 2;
	windows[WIN_REGS].height = (LINES / 6) * 4 - 1;
	windows[WIN_REGS].in_use = 1;

	windows[WIN_DUMP].startx = 0;
	windows[WIN_DUMP].starty = (LINES / 6) * 4;
	windows[WIN_DUMP].width = COLS;
	windows[WIN_DUMP].height = (LINES / 6) * 2;
	windows[WIN_DUMP].in_use = 1;
	windows[WIN_DUMP].scroll = 1;
}

static void update_windows()
{
	int i;
	for (i = 0; i < sizeof(windows) / sizeof(struct _windows); i++) {
		if (!windows[i].in_use) {
			continue;
		}
		if (windows[i].win) {
			delwin(windows[i].win);
		}
		windows[i].win = newwin(windows[i].height, windows[i].width, windows[i].starty, windows[i].startx);
		//box(windows[i].win, 0, 0);
		scrollok(windows[i].win, TRUE);
		wrefresh(windows[i].win);
	}
}

int main(int argc, char *argv[])
{
	char out[1024];
	regs r;
	word addr;
	word length;
	int ch;

	memset(&windows, 0, sizeof(windows));

	initscr();
	cbreak();
	curs_set(FALSE);
	noecho();
	keypad(stdscr, TRUE);
	refresh();

	calc_windows();
	update_windows();

	r.model = K1806VM2;

	r.r[6] = 0;

	hwstub_connect(&r);

	core_init(&r);

	byte *mem = r.ramptr(&r, 0);

	FILE *inf = fopen(argv[1], "rb");
	if (inf) {
		unsigned int tmp;
		sscanf(argv[2], "%o", &tmp);
		addr = tmp & 0177776;
		length = fread(&mem[addr], 1, 65536 - addr, inf);
		wprintw(windows[WIN_DUMP].win, "Loaded file %s to %06o length %06o\n", argv[1], addr, length);
		wrefresh(windows[WIN_DUMP].win);
		fclose(inf);
	} else {
		wprintw(windows[WIN_DUMP].win, "Can not open file %s\n", argv[1]);
		wrefresh(windows[WIN_DUMP].win);

		getch();
		endwin();

		return 1;
	}

	r.SEL1 = addr & 0177400;

	wprintw(windows[WIN_DUMP].win, "Reset address set to %06o\n", r.SEL1);
	wrefresh(windows[WIN_DUMP].win);

	core_reset(&r);

	while (1) {
		addr = r.r[7];
		wprintw(windows[WIN_DIS].win, "%06o %06o ", addr, r.load_word(&r, addr));
		wprintw(windows[WIN_DIS].win, "%s\n", disas(&r, &addr, out));
		wrefresh(windows[WIN_DIS].win);
		dump_regs(&r, windows[WIN_REGS].win);
		wrefresh(windows[WIN_REGS].win);
		ch = getch();

		if (ch == KEY_F(10)) {
			break;
		}

		if (ch == KEY_F(1)) {
			char buf[7];
			int start = 0;
			int len = 0;
			curs_set(TRUE);
			echo();
			wprintw(windows[WIN_DUMP].win, "Mem dump start address: ");
			wrefresh(windows[WIN_DUMP].win);
			wgetnstr(windows[WIN_DUMP].win, buf, 6);
			sscanf(buf, "%o", &start);
			wprintw(windows[WIN_DUMP].win, "Mem dump length: ");
			wrefresh(windows[WIN_DUMP].win);
			wgetnstr(windows[WIN_DUMP].win, buf, 6);
			sscanf(buf, "%o", &len);
			curs_set(FALSE);
			noecho();

			dump_mem(&r, start, len, 1);

//			printf("--> %d\n", key);
//			if (key == 'm' || key == 'M') {
//				dump_mem(&r, start, len, (key == 'M')?1:0);
//			}
		} else {
			core_step(&r);
		}
	}

	core_fini(&r);

	endwin();

	return 0;
}
