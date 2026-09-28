#ifndef HG_MPSSE_H
#define HG_MPSSE_H

#include <stddef.h>
#include <stdint.h>

struct ftdi_context;

struct hg_mpsse {
	struct ftdi_context *ftdi;
	unsigned int clock_hz;
	uint8_t low_value;
	uint8_t low_direction;
	int jtag_adbus7;
};

int hg_mpsse_open(struct hg_mpsse *link, int vendor, int product,
                  const char *serial, unsigned int index, unsigned int clock_hz);
void hg_mpsse_close(struct hg_mpsse *link);
/* HC7000 jumper: enable=1 releases ADBUS7 to the JTAGENB pull-up. */
int hg_mpsse_jtag_enable(struct hg_mpsse *link, int enable);
int hg_mpsse_request_pending(struct hg_mpsse *link);
int hg_mpsse_select(struct hg_mpsse *link, int selected);
int hg_mpsse_exchange(struct hg_mpsse *link, const uint8_t *tx,
                      uint8_t *rx, size_t length);

#endif
