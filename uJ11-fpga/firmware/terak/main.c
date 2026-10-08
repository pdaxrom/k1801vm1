#include "terak.h"
#include "../storage/keyboard.h"
#include "../storage/terminal.h"

/* Common UJ11SD partitions; geometry and sector status live within each floppy. */
#define QX(n) MMIO(0x40000800u + 4u * (n))
#define QBUF(n) QX(64u + (n))
#ifndef UJ11_TERAK_IOP_RAM_BYTES
#define UJ11_TERAK_IOP_RAM_BYTES 19456u
#endif
volatile uint8_t *const sector = (volatile uint8_t *)(UJ11_TERAK_IOP_RAM_BYTES - 512u);
static u32 speed;

unsigned terminal_keymode(void)
{
	return 0;
}

void poll_io(void)
{
	keyboard_poll(BUS_STATUS);
	qb_poll();
}

#ifdef UJ11_TERAK_BIOS
void publish_media(unsigned menu)
{
	u32 present = 0, readonly = 0;
	for (unsigned i = 0; i < 4; i++) {
		if (media[i].start) {
			present |= 1u << i;
			readonly |= media[i].readonly << i;
		}
	}
	unsigned qb_boot = media[0].start && track_load(0, 1, 0) && track_meta[6];
	/* Write the complete media state once. Bit 31 is a read/acknowledge
	 * latch for long Reset and must not be echoed by a media update. */
	QX(3) = present | (readonly << 4) | (qb_boot << 8) | (menu << 9)
#ifdef UJ11_TERAK_RL
	        | ((rl_media[0].start != 0) << 10)
#endif
	        ;
}
#endif

static int elapsed(u32 start, u32 limit)
{
	return ((TIME - start) & 65535u) >= limit;
}

static void pause_ms(u32 delay)
{
	u32 start = TIME;
	while (!elapsed(start, delay)) {
		poll_io();
	}
}

void close_card(void)
{
	CONTROL = speed | 1;
	(void)SPI;
}

static u32 send(u32 opcode, u32 arg, u32 crc)
{
	CONTROL = speed;
	SPI = opcode;
	SPI = arg >> 24;
	SPI = arg >> 16;
	SPI = arg >> 8;
	SPI = arg;
	SPI = crc;
	for (unsigned n = 0; n < 16; n++) {
		u32 r = SPI;
		if (!(r & 128)) {
			return r;
		}
	}
	return 255;
}

u32 transfer(u32 op)
{
	ENGINE = op;
	u32 status;
	while ((status = ENGINE) & 1) {
		poll_io();
	}
	return status;
}

int fetch(u32 lba)
{
	if (send(0x51, lba, 1)) {
		close_card();
		return 0;
	}
	u32 start = TIME;
	for (;;) {
		poll_io();
		u32 r = SPI;
		if (r == 0xfe) {
			break;
		}
		if (r != 255 || elapsed(start, 250)) {
			close_card();
			return 0;
		}
	}
	u32 crc = transfer(RX) >> 16;
	u32 received = SPI << 8;
	received |= SPI;
	close_card();
	return crc == received;
}

int store(u32 lba)
{
	if (send(0x58, lba, 1)) {
		close_card();
		return 0;
	}
	SPI = 0xfe;
	u32 crc = transfer(TX) >> 16;
	SPI = crc >> 8;
	SPI = crc;
	u32 r, start = TIME;
	do {
		r = SPI;
		if (elapsed(start, 250)) {
			close_card();
			return 0;
		}
	} while (r == 255);
	if ((r & 31) != 5) {
		close_card();
		return 0;
	}
	start = TIME;
	do {
		pause_ms(1);
		r = SPI;
		if (elapsed(start, 1000)) {
			close_card();
			return 0;
		}
	} while (!r);
	close_card();
	return 1;
}

static int initialize(void)
{
	BUS_CONTROL = 1;
	while (!(BUS_STATUS & 8)) {
		poll_io();
	}
	pause_ms(10);
	CONTROL = 1;
	for (unsigned i = 0; i < 10; i++) {
		SPI = 255;
	}
	if (send(0x40, 0, 0x95) != 1) {
		return 0;
	}
	close_card();
	if (send(0x48, 0x1aa, 0x87) != 1) {
		return 0;
	}
	u32 echo = 0;
	for (unsigned i = 0; i < 4; i++) {
		echo = (echo << 8) | SPI;
	}
	close_card();
	if (echo != 0x1aa) {
		return 0;
	}
	u32 start = TIME;
	for (;;) {
		u32 r = send(0x77, 0, 1);
		close_card();
		if (r > 1) {
			return 0;
		}
		r = send(0x69, 0x40000000, 1);
		close_card();
		if (!r) {
			break;
		}
		if (r != 1 || elapsed(start, 2000)) {
			return 0;
		}
	}
	if (send(0x7a, 0, 1)) {
		return 0;
	}
	u32 ocr = SPI;
	for (unsigned i = 0; i < 3; i++) {
		(void)SPI;
	}
	close_card();
	if ((ocr & 0xc0) != 0xc0) {
		return 0;
	}
	speed = 2;
	if (send(0x49, 0, 1)) {
		return 0;
	}
	start = TIME;
	while (SPI != 0xfe) {
		if (elapsed(start, 250)) {
			return 0;
		}
	}
	uint8_t csd[16];
	for (unsigned i = 0; i < 16; i++) {
		csd[i] = SPI;
	}
	(void)SPI;
	(void)SPI;
	close_card();
	if ((csd[0] >> 6) != 1) {
		return 0;
	}
	u32 size = ((u32)(csd[7] & 63) << 16) | ((u32)csd[8] << 8) | csd[9];
	u32 capacity = size == 0x3fffff ? 0xffffffffu : (size + 1) << 10;
	if (media_mount(capacity)) {
#ifdef UJ11_TERAK_BIOS
		publish_media(0);
		return 1;
#else
		u32 present = 0, readonly = 0;
		for (unsigned i = 0; i < 4; i++) {
			if (media[i].start) {
				present |= 1u << i;
				readonly |= media[i].readonly << i;
			}
		}
		unsigned qb_boot = media[0].start && track_load(0, 1, 0) && track_meta[6];
		QX(3) = present | (readonly << 4) | (qb_boot << 8);
		return present != 0;
#endif
	}
	return 0;
}

static u32 service(u32 descriptor)
{
	u32 unit = (descriptor >> 12) & 3, track = (descriptor >> 5) & 127, number = descriptor & 31;
	u32 op = (descriptor >> 14) & 7;
	if (!track_load(unit, track, 0) || track_meta[6] || track_meta[5] || track_meta[4] != 26) {
		return 0xc000;
	}
	unsigned index;
	for (index = 0; index < track_meta[4]; index++) {
		if (track_meta[32 + index * 8 + 2] == number) {
			break;
		}
	}
	if (index == track_meta[4]) {
		return 0xc000;
	}
	u32 flag_offset = 32 + index * 8 + 4, flag = track_meta[flag_offset];
	if (op == 5 && !(flag & 1)) {
		return 0xc000;
	}
	u32 position = track_data(unit, track, 0) + index * 128;
	u32 lba = media[unit].start + (position >> 9), offset = position & 511;
	if (op == 7 && media[unit].format != MEDIA_FD) {
		return 0xc000;
	}
	if (!fetch(lba)) {
		return 0xc000;
	}
	for (unsigned i = 0; i < 64; i++) {
		if (!(QX(0) & 1) || QX(1) != descriptor) {
			return 0;
		}
		if (op == 5) {
			QBUF(i) = (descriptor & 0xff000000u) | sector[offset + i * 2] |
			          ((u32)sector[offset + i * 2 + 1] << 8);
		} else {
			u32 value = QBUF(i);
			sector[offset + i * 2] = value;
			sector[offset + i * 2 + 1] = value >> 8;
		}
	}
	if (op == 5) {
		return ((flag & 2) ? 0x8400 : 0) | ((flag & 4) ? 0x9000 : 0);
	}
	if (media[unit].readonly) {
		return 0x8800;
	}
	if (!(QX(0) & 1) || QX(1) != descriptor) {
		return 0;
	}
	if (!store(lba)) {
		return 0xc000;
	}
	track_meta[flag_offset] = op == 7 ? 3 : 1;
	return track_save(unit, track, 0) ? 0 : 0xc000;
}

void main(void)
{
	keyboard_reset();
#ifdef UJ11_TERAK_RL
	rl_reset();
#endif
	(void)initialize();
	close_card();
#ifdef UJ11_TERAK_BIOS
	/* A corrupt/missing BIOS never releases the CPU into a partial image. */
	while (BUS_STATUS & 2) {
		poll_io();
	}
	if (!bios_load()) {
		for (;;) {
			poll_io();
		}
	}
#endif
	/* An absent/invalid card leaves empty drives; the boot ROM waits for READY. */
	CPU_START = 1;
	for (;;) {
		poll_io();
		qb_service();
#ifdef UJ11_TERAK_RL
		rl_service();
		qb_publish();
#endif
		if (QX(0) & 1) {
			u32 descriptor = QX(1);
			u32 error = service(descriptor);
			QX(2) = (descriptor & 0xff000000u) | error;
		}
	}
}
