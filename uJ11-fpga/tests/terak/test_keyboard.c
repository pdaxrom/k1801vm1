#include <assert.h>
#include <stdio.h>
#include "keyboard.h"
#include "keyboard_test.h"

unsigned keyboard_ready = 1;
static unsigned scan, count;
static unsigned char output[32];

unsigned keyboard_scan(void)
{
	unsigned value = scan;
	scan = 0;
	return value;
}

void keyboard_send(unsigned ch)
{
	assert(keyboard_ready && count < sizeof(output));
	output[count++] = ch;
}

static void key(unsigned code)
{
	assert(!scan);
	scan = code | 256;
	for (unsigned i = 0; i < 8; i++) {
		keyboard_service(BUS_KEYBOARD_PENDING);
	}
	assert(!scan);
}

int main(void)
{
	keyboard_reset();
	key(0x1c);
	key(0x14);
	key(0x21);
	key(0xf0);
	key(0x14);
	key(0xe0);
	key(0x75);
	key(0xe0);
	key(0x72);
	key(0xe0);
	key(0x74);
	key(0xe0);
	key(0x6b);
	assert(count == 6 && output[0] == 'a' && output[1] == 3 && output[2] == 032 && output[3] == 014 &&
	       output[4] == 013 && output[5] == 027);
	key(0x66);
	key(0xe0);
	key(0x71);
	assert(count == 8 && output[6] == 8 && output[7] == 127);
	keyboard_ready = 0;
	key(0x32);
	assert(count == 8 && keyboard_output_pending);
	keyboard_ready = 1;
	keyboard_poll(0);
	assert(count == 9 && output[8] == 'b' && !keyboard_output_pending);
	puts("PASS Terak ASCII/control cursor keys and keyboard backpressure");
	return 0;
}
