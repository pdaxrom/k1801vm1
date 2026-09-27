#include "hg_mpsse.h"
#include "hg_protocol.h"

#include <errno.h>
#include <ftdi.h>
#include <string.h>
#include <time.h>

#define HG_PIN_TCK 0x01u
#define HG_PIN_TDI 0x02u
#define HG_PIN_TDO 0x04u
#define HG_PIN_TMS 0x08u
#define HG_MPSSE_DIRECTION (HG_PIN_TCK | HG_PIN_TDI | HG_PIN_TMS)
#define HG_PIN_JTAGENB 0x80u

static void hg_delay_us(unsigned int microseconds)
{
	struct timespec delay;

	delay.tv_sec = microseconds / 1000000u;
	delay.tv_nsec = (long)(microseconds % 1000000u) * 1000L;
	nanosleep(&delay, NULL);
}

static int hg_write_all(struct ftdi_context *ftdi, const uint8_t *data,
	size_t length)
{
	size_t offset = 0;

	while (offset < length) {
		int done = ftdi_write_data(ftdi, data + offset,
			(int)(length - offset));
		if (done < 0)
			return -1;
		if (done == 0) {
			hg_delay_us(1000);
			continue;
		}
		offset += (size_t)done;
	}
	return 0;
}

static int hg_read_exact(struct ftdi_context *ftdi, uint8_t *data,
	size_t length)
{
	size_t offset = 0;
	unsigned int empty_reads = 0;

	while (offset < length) {
		int done = ftdi_read_data(ftdi, data + offset,
			(int)(length - offset));
		if (done < 0)
			return -1;
		if (done == 0) {
			if (++empty_reads > 2000u) {
				errno = ETIMEDOUT;
				return -1;
			}
			hg_delay_us(1000);
			continue;
		}
		empty_reads = 0;
		offset += (size_t)done;
	}
	return 0;
}

static int hg_set_low(struct hg_mpsse *link)
{
	uint8_t command[3] = {
		SET_BITS_LOW, link->low_value, link->low_direction
	};

	return hg_write_all(link->ftdi, command, sizeof(command));
}

static int hg_mpsse_sync(struct hg_mpsse *link)
{
	uint8_t command[2] = {0xaa, SEND_IMMEDIATE};
	uint8_t response[2];

	if (hg_write_all(link->ftdi, command, sizeof(command)) != 0 ||
	    hg_read_exact(link->ftdi, response, sizeof(response)) != 0)
		return -1;
	if (response[0] != 0xfa || response[1] != 0xaa) {
		errno = EPROTO;
		return -1;
	}
	return 0;
}

int hg_mpsse_open(struct hg_mpsse *link, int vendor, int product,
	const char *serial, unsigned int index, unsigned int clock_hz)
{
	uint32_t divisor;
	uint8_t setup[5];

	if (!link || clock_hz == 0 || clock_hz > 1000000u) {
		errno = EINVAL;
		return -1;
	}
	memset(link, 0, sizeof(*link));
	link->ftdi = ftdi_new();
	if (!link->ftdi) {
		errno = ENOMEM;
		return -1;
	}
	if (ftdi_set_interface(link->ftdi, INTERFACE_A) < 0 ||
	    ftdi_usb_open_desc_index(link->ftdi, vendor, product, NULL, serial,
		index) < 0 ||
	    ftdi_usb_reset(link->ftdi) < 0 ||
	    ftdi_set_latency_timer(link->ftdi, 1) < 0 ||
	    ftdi_write_data_set_chunksize(link->ftdi, 4096) < 0 ||
	    ftdi_read_data_set_chunksize(link->ftdi, 4096) < 0 ||
	    ftdi_set_bitmode(link->ftdi, 0, BITMODE_RESET) < 0 ||
	    ftdi_set_bitmode(link->ftdi, HG_MPSSE_DIRECTION, BITMODE_MPSSE) < 0)
		goto fail;
	hg_delay_us(50000);
	if (ftdi_tcioflush(link->ftdi) < 0 || hg_mpsse_sync(link) != 0)
		goto fail;

	/* FT2232D MPSSE runs from a 12 MHz clock: f = 6 MHz/(divisor+1). */
	divisor = 6000000u / clock_hz;
	if (divisor == 0)
		divisor = 1;
	divisor--;
	if (divisor > 0xffffu)
		divisor = 0xffffu;
	setup[0] = TCK_DIVISOR;
	setup[1] = (uint8_t)(divisor & 0xffu);
	setup[2] = (uint8_t)((divisor >> 8) & 0xffu);
	setup[3] = LOOPBACK_END;
	setup[4] = SEND_IMMEDIATE;
	if (hg_write_all(link->ftdi, setup, sizeof(setup)) != 0)
		goto fail;

	link->clock_hz = 6000000u / (divisor + 1u);
	link->low_value = 0;
	link->low_direction = HG_MPSSE_DIRECTION;
	if (hg_set_low(link) != 0)
		goto fail;
	return 0;

fail:
	hg_mpsse_close(link);
	return -1;
}

int hg_mpsse_jtag_enable(struct hg_mpsse *link, int enable)
{
	uint8_t command[2] = {GET_BITS_LOW, SEND_IMMEDIATE};
	uint8_t pins;
	link->jtag_adbus7 = 1;
	/* Emulate open drain: only drive low, otherwise release to board R22. */
	link->low_value &= (uint8_t)~HG_PIN_JTAGENB;
	if (enable)
		link->low_direction &= (uint8_t)~HG_PIN_JTAGENB;
	else
		link->low_direction |= HG_PIN_JTAGENB;
	if (hg_set_low(link) != 0)
		return -1;
	hg_delay_us(1000);
	if (hg_write_all(link->ftdi, command, sizeof(command)) != 0 ||
	    hg_read_exact(link->ftdi, &pins, 1) != 0)
		return -1;
	if (!!(pins & HG_PIN_JTAGENB) != !!enable) {
		errno = EIO;
		return -1;
	}
	return 0;
}

void hg_mpsse_close(struct hg_mpsse *link)
{
	if (!link || !link->ftdi)
		return;
	link->low_value = 0;
	/* Stop clock/select before returning the pins to the FPGA JTAG port. */
	if (link->jtag_adbus7) {
		hg_set_low(link);
		hg_mpsse_jtag_enable(link, 1);
	}
	link->low_direction = 0;
	hg_set_low(link);
	ftdi_set_bitmode(link->ftdi, 0, BITMODE_RESET);
	ftdi_usb_close(link->ftdi);
	ftdi_free(link->ftdi);
	link->ftdi = NULL;
}

int hg_mpsse_request_pending(struct hg_mpsse *link)
{
	uint8_t command[2] = {GET_BITS_LOW, SEND_IMMEDIATE};
	uint8_t pins;

	if (hg_write_all(link->ftdi, command, sizeof(command)) != 0 ||
	    hg_read_exact(link->ftdi, &pins, 1) != 0)
		return -1;
	return (pins & HG_PIN_TDO) != 0;
}

int hg_mpsse_select(struct hg_mpsse *link, int selected)
{
	if (selected)
		link->low_value |= HG_PIN_TMS;
	else
		link->low_value &= (uint8_t)~HG_PIN_TMS;
	return hg_set_low(link);
}

int hg_mpsse_exchange(struct hg_mpsse *link, const uint8_t *tx,
	uint8_t *rx, size_t length)
{
	uint8_t command[5];
	uint8_t input;
	size_t i;

	if (!link || !link->ftdi || length == 0 ||
	    length > HG_BLOCK_SIZE + 2u) {
		errno = EINVAL;
		return -1;
	}
	/*
	 * The PDP-11 side bit-bangs the link in software.  It needs time after
	 * each byte to return from HGTXBY/HGRXBY and prepare the next one.  A
	 * single continuous MPSSE transfer overruns that boundary; at low clocks
	 * a whole 514-byte transfer also exceeds the USB read timeout.  Complete
	 * one byte (and one USB readback) at a time so TCK remains low between
	 * bytes and both ends naturally pace each other.
	 */
	command[0] = MPSSE_DO_WRITE | MPSSE_DO_READ | MPSSE_WRITE_NEG |
		MPSSE_LSB;
	command[1] = 0;
	command[2] = 0;
	command[4] = SEND_IMMEDIATE;
	for (i = 0; i < length; i++) {
		command[3] = tx ? tx[i] : 0;
		if (hg_write_all(link->ftdi, command, sizeof(command)) != 0 ||
		    hg_read_exact(link->ftdi, &input, 1) != 0)
			return -1;
		if (rx)
			rx[i] = input;
	}
	return 0;
}
