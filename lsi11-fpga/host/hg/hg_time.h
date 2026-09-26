#ifndef HG_TIME_H
#define HG_TIME_H

#include "hg_protocol.h"
#include <time.h>

/* Local civil time, RT-11 date, then high/low ticks since midnight.
 * The V5.03 .SDTTM interface accepts nonnegative dates (1972..2035). */
int hg_time_encode(const struct tm *local, long nanoseconds, unsigned int hz,
	uint8_t payload[HG_TIME_SIZE]);
int hg_time_now(unsigned int hz, uint8_t payload[HG_TIME_SIZE]);

#endif
