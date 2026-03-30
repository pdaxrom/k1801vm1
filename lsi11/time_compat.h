#ifndef LSI11_TIME_COMPAT_H
#define LSI11_TIME_COMPAT_H

#include <stdint.h>

#if defined(PICO_ON_DEVICE)
#include "pico/time.h"
#else
#include <time.h>
#include <sys/time.h>
#endif

static inline uint64_t lsi11_now_ns(void)
{
#if defined(PICO_ON_DEVICE)
    return (uint64_t)time_us_64() * 1000ull;
#elif defined(CLOCK_MONOTONIC)
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#else
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000000ull +
           (uint64_t)tv.tv_usec * 1000ull;
#endif
}

#endif
