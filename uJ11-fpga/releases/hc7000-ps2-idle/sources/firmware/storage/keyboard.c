#include "storage.h"
#include "terminal.h"
#include "keyboard.h"
#ifdef UJ11_KEYBOARD
#ifdef UJ11_KEYBOARD_TEST
#include "keyboard_test.h"
#else
#define KEY_SCAN MMIO(0x40000600u)
#define KEY_RX MMIO(0x40000604u)
#define KEY_SEND(ch) (KEY_RX = (ch))
#endif

/* PS/2 set 2, US layout. Protocol and extended keys are handled below.
 * Mapping follows the keyboard diagram in Digilent's PmodPS/2 manual. */
static const uint8_t normal[128] = {
	[0x0d] = '\t', [0x0e] = '`', [0x15] = 'q', [0x16] = '1',
	[0x1a] = 'z', [0x1b] = 's', [0x1c] = 'a', [0x1d] = 'w', [0x1e] = '2',
	[0x21] = 'c', [0x22] = 'x', [0x23] = 'd', [0x24] = 'e', [0x25] = '4', [0x26] = '3',
	[0x29] = ' ', [0x2a] = 'v', [0x2b] = 'f', [0x2c] = 't', [0x2d] = 'r', [0x2e] = '5',
	[0x31] = 'n', [0x32] = 'b', [0x33] = 'h', [0x34] = 'g', [0x35] = 'y', [0x36] = '6',
	[0x3a] = 'm', [0x3b] = 'j', [0x3c] = 'u', [0x3d] = '7', [0x3e] = '8',
	[0x41] = ',', [0x42] = 'k', [0x43] = 'i', [0x44] = 'o', [0x45] = '0', [0x46] = '9',
	[0x49] = '.', [0x4a] = '/', [0x4b] = 'l', [0x4c] = ';', [0x4d] = 'p', [0x4e] = '-',
	[0x52] = '\'', [0x54] = '[', [0x55] = '=', [0x5a] = '\r', [0x5b] = ']', [0x5d] = '\\',
	[0x61] = '\\', [0x66] = 127, [0x69] = '1', [0x6b] = '4', [0x6c] = '7',
	[0x70] = '0', [0x71] = '.', [0x72] = '2', [0x73] = '5', [0x74] = '6',
	[0x75] = '8', [0x76] = 27, [0x79] = '+', [0x7a] = '3', [0x7b] = '-', [0x7c] = '*', [0x7d] = '9'
};
static const char unshifted[] = "`1234567890-=[]\\;',./";
static const char shifted[] = "~!@#$%^&*()_+{}|:\"<>?";
static uint8_t modifiers, extended, released, pause_left, locks, lock_down;
unsigned char keyboard_output_pending;
static u32 pending;

void keyboard_reset(void)
{
	modifiers = extended = released = pause_left = lock_down = keyboard_output_pending = 0;
	locks = 2; /* Numeric keypad starts in numeric mode. */
}

static void translate(unsigned code)
{
	if (code == 0 || code == 0xaa || code == 0xff) {
		keyboard_reset();
		return;
	}
	if (pause_left) {
		pause_left--;
		return;
	}
	if (code == 0xe1) {
		pause_left = 7;
		extended = released = 0;
		return;
	}
	if (code == 0xe0) {
		extended = 1;
		return;
	}
	if (code == 0xf0) {
		released = 1;
		return;
	}
	unsigned ext = extended, up = released;
	extended = released = 0;
	unsigned mask = 0;
	if (!ext && code == 0x12) {
		mask = 1;
	} else if (!ext && code == 0x59) {
		mask = 2;
	} else if (code == 0x14) {
		mask = ext ? 8 : 4;
	} else if (code == 0x11) {
		mask = ext ? 32 : 16;
	}
	if (mask) {
		modifiers = up ? modifiers & ~mask : modifiers | mask;
		return;
	}
	if (!ext && (code == 0x58 || code == 0x77)) {
		mask = code == 0x58 ? 1 : 2;
		if (up) {
			lock_down &= ~mask;
		} else if (!(lock_down & mask)) {
			lock_down |= mask;
			locks ^= mask;
		}
		return;
	}
	if (up) {
		return;
	}
	unsigned shift = (modifiers & 3) != 0;
	unsigned ch = 0, suffix = 0, number = 0;
	if (!ext && (code == 5 || code == 6 || code == 4 || code == 12)) {
		suffix = code == 5 ? 'P' : code == 6 ? 'Q' : code == 4 ? 'R' : 'S';
		pending = terminal_keymode() & 1 ? 27u | (suffix << 8) : 27u | ('O' << 8) | (suffix << 16);
		keyboard_output_pending = terminal_keymode() & 1 ? 2 : 3;
		return;
	}
	/* Shift temporarily reverses Num Lock for keypad cursor/navigation keys. */
	if (!ext && ((locks & 2) != 0) == shift && code >= 0x69 && code != 0x76 && code != 0x79 &&
	                code != 0x7b && code != 0x7c) {
		ext = 1;
	}
	if (ext) {
		if (code == 0x5a) {
			ch = '\r';
		} else if (code == 0x4a) {
			ch = '/';
		} else {
			switch (code) {
			case 0x75:
				suffix = 'A';
				break;
			case 0x72:
				suffix = 'B';
				break;
			case 0x74:
				suffix = 'C';
				break;
			case 0x6b:
				suffix = 'D';
				break;
			case 0x6c:
				suffix = 'H';
				break;
			case 0x69:
				number = '4';
				break;
			case 0x70:
				number = '2';
				break;
			case 0x71:
				ch = 127;
				break;
			case 0x7d:
				number = '5';
				break;
			case 0x7a:
				number = '6';
				break;
			default:
				return;
			}
			if (suffix || number) {
				unsigned mode = terminal_keymode();
				pending = 27;
				keyboard_output_pending = 1;
				if (!(mode & 1) || number) {
					pending |= (u32)((mode & 2) && suffix >= 'A' && suffix <= 'D' ? 'O' : '[') << 8;
					keyboard_output_pending++;
				}
				pending |= (u32)(suffix ? suffix : number) << (keyboard_output_pending++ * 8);
				if (number) {
					pending |= (u32)'~' << (keyboard_output_pending++ * 8);
				}
				return;
			}
		}
	} else if (code < 128) {
		ch = normal[code];
		if (!ch) {
			return;
		}
		if (ch >= 'a' && ch <= 'z') {
			if (shift != ((locks & 1) != 0)) {
				ch -= 32;
			}
		} else if (shift && code < 0x69) {
			for (unsigned i = 0; i < sizeof(unshifted) - 1; i++) {
				if (ch == (unsigned)unshifted[i]) {
					ch = shifted[i];
					break;
				}
			}
		}
	} else {
		return;
	}
	if (modifiers & 12) {
		if (ch == ' ' || ch == '2') {
			ch = 0;
		} else if (ch == '?') {
			ch = 127;
		} else if (ch == '6') {
			ch = 30;
		} else if (ch == '-') {
			ch = 31;
		} else if ((ch >= '@' && ch <= '_') || (ch >= 'a' && ch <= 'z')) {
			ch &= 31;
		}
	}
	pending = ch;
	keyboard_output_pending = 1;
	if (modifiers & 48) {
		pending = 27u | (ch << 8);
		keyboard_output_pending = 2;
	}
}

void keyboard_service(unsigned events)
{
	if (keyboard_output_pending) {
		if (KEY_RX & 1) {
			KEY_SEND(pending & 255);
			pending >>= 8;
			keyboard_output_pending--;
		}
		return;
	}
	if (!(events & BUS_KEYBOARD_PENDING)) {
		return;
	}
	u32 scan = KEY_SCAN;
	if (scan & 256) {
		translate(scan & 255);
	}
}
#endif
