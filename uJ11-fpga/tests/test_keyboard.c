#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "keyboard.h"
#include "keyboard_test.h"
unsigned keyboard_ready = 1;
static unsigned scan, output_count, mode, reads;
static unsigned char output[256];
unsigned keyboard_scan(void)
{
	unsigned n = scan;
	scan = 0;
	reads++;
	return n;
}
void keyboard_send(unsigned ch)
{
	assert(keyboard_ready && output_count < sizeof(output));
	output[output_count++] = ch;
}
unsigned terminal_keymode(void)
{
	return mode;
}
static void key(unsigned n)
{
	assert(!scan);
	scan = n | 256;
	for (unsigned i = 0; i < 8; i++) {
		keyboard_service(BUS_KEYBOARD_PENDING);
	}
	assert(!scan);
}
static void expect(const char *bytes, unsigned n)
{
	assert(output_count == n && !memcmp(output, bytes, n));
	output_count = 0;
}
static void up(unsigned n)
{
	key(0xf0);
	key(n);
}
static void ext(unsigned n)
{
	key(0xe0);
	key(n);
}
int main(void)
{
	keyboard_reset();
	unsigned idle_reads = reads;
	keyboard_poll(0);
	assert(reads == idle_reads);
	key(0x1c);
	up(0x1c);
	key(0x1c);
	expect("aa", 2);
	key(0x12);
	key(0x59);
	up(0x12);
	key(0x1c);
	up(0x59);
	key(0x1c);
	expect("Aa", 2);
	key(0x58);
	key(0x58);
	key(0x1c);
	key(0x12);
	key(0x1c);
	up(0x12);
	up(0x58);
	key(0x58);
	up(0x58);
	key(0x1c);
	expect("Aaa", 3);
	key(0x12);
	key(0x16);
	key(0x55);
	key(0x4a);
	up(0x12);
	expect("!+?", 3);
	key(0x14);
	ext(0x14);
	up(0x14);
	key(0x21);
	key(0x23);
	key(0x3c);
	key(0x29);
	key(0x36);
	key(0x4e);
	key(0xe0);
	up(0x14);
	expect("\003\004\025\000\036\037", 6);
	key(0x11);
	key(0x1c);
	up(0x11);
	expect("\033a", 2);
	key(0x5a);
	ext(0x5a);
	key(0x0d);
	key(0x66);
	ext(0x71);
	key(0x76);
	expect("\r\r\t\177\177\033", 6);
	ext(0x75);
	ext(0x72);
	ext(0x74);
	ext(0x6b);
	expect("\033[A\033[B\033[C\033[D", 12);
	mode = 1;
	ext(0x75);
	key(5);
	expect("\033A\033P", 4);
	mode = 2;
	ext(0x75);
	key(6);
	expect("\033OA\033OQ", 6);
	mode = 0;
	ext(0x6c);
	ext(0x69);
	ext(0x70);
	ext(0x7d);
	ext(0x7a);
	expect("\033[H\033[4~\033[2~\033[5~\033[6~", 19);
	key(0x69);
	key(0x75);
	key(0x79);
	ext(0x4a);
	expect("18+/", 4);
	key(0x77);
	up(0x77);
	key(0x75);
	key(0x12);
	key(0x75);
	up(0x12);
	expect("\033[A8", 4);
	key(0xe1);
	key(0x14);
	key(0x77);
	key(0xe1);
	key(0xf0);
	key(0x14);
	key(0xf0);
	key(0x77);
	ext(0x12);
	ext(0x7c);
	key(0xe0);
	up(0x7c);
	key(0xe0);
	up(0x12);
	key(0x1c);
	expect("a", 1);
	key(0x12);
	key(0xe0);
	key(0);
	key(0x1c);
	expect("a", 1);
	key(0x12);
	key(0xaa);
	key(0x1c);
	expect("a", 1);
	keyboard_ready = 0;
	scan = 0x100 | 0x1c;
	keyboard_service(BUS_KEYBOARD_PENDING);
	unsigned n = reads;
	scan = 0x100 | 0x32;
	for (unsigned i = 0; i < 10; i++) {
		keyboard_service(BUS_KEYBOARD_PENDING);
	}
	assert(reads == n && output_count == 0 && scan);
	keyboard_ready = 1;
	for (unsigned i = 0; i < 8; i++) {
		keyboard_service(BUS_KEYBOARD_PENDING);
	}
	expect("ab", 2);
	keyboard_ready = 0;
	scan = 0x100 | 0x1c;
	keyboard_service(BUS_KEYBOARD_PENDING);
	keyboard_reset();
	keyboard_ready = 1;
	keyboard_service(BUS_KEYBOARD_PENDING);
	expect("", 0);
	puts("PASS keyboard: modifiers, repeats, control/NUL, VT52/VT100, keypad, Pause/PrintScreen, faults, backpressure/reset");
	return 0;
}
