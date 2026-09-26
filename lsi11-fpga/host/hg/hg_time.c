#include "hg_time.h"
#include <errno.h>

static void put_word(uint8_t *p, uint16_t word)
{
	p[0] = (uint8_t)word;
	p[1] = (uint8_t)(word >> 8);
}

int hg_time_encode(const struct tm *local, long nanoseconds, unsigned int hz,
	uint8_t payload[HG_TIME_SIZE])
{
	static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
	unsigned int year, age, day_limit;
	uint32_t ticks;
	uint16_t date;

	if (!local || !payload || (hz != 50 && hz != 60) ||
	    nanoseconds < 0 || nanoseconds >= 1000000000L) {
		errno = EINVAL;
		return -1;
	}
	if (local->tm_year < 72 || local->tm_year > 135 ||
	    local->tm_mon < 0 || local->tm_mon > 11 ||
	    local->tm_hour < 0 || local->tm_hour > 23 ||
	    local->tm_min < 0 || local->tm_min > 59 ||
	    local->tm_sec < 0 || local->tm_sec > 59) {
		errno = ERANGE;
		return -1;
	}
	year = (unsigned int)local->tm_year + 1900u;
	day_limit = (unsigned int)days[local->tm_mon];
	if (local->tm_mon == 1 && year % 4u == 0 &&
	    (year % 100u != 0 || year % 400u == 0))
		day_limit++;
	if (local->tm_mday < 1 || (unsigned int)local->tm_mday > day_limit) {
		errno = ERANGE;
		return -1;
	}
	age = year - 1972u;
	date = (uint16_t)(((age >> 5) << 14) |
		((unsigned int)(local->tm_mon + 1) << 10) |
		((unsigned int)local->tm_mday << 5) | (age & 31u));
	ticks = (uint32_t)((local->tm_hour * 3600 + local->tm_min * 60 +
		local->tm_sec) * hz + (uint64_t)nanoseconds * hz / 1000000000u);
	put_word(payload, date);
	put_word(payload + 2, (uint16_t)(ticks >> 16));
	put_word(payload + 4, (uint16_t)ticks);
	return 0;
}

int hg_time_now(unsigned int hz, uint8_t payload[HG_TIME_SIZE])
{
	struct timespec now;
	struct tm local;

	if (clock_gettime(CLOCK_REALTIME, &now) != 0 ||
	    !localtime_r(&now.tv_sec, &local))
		return -1;
	return hg_time_encode(&local, now.tv_nsec, hz, payload);
}
