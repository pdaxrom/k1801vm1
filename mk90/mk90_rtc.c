#include "mk90_rtc.h"

#include "mk90_io.h"

#include <string.h>
#include <time.h>

enum {
    RTC_REG_SECONDS = 0,
    RTC_REG_SECONDS_ALARM = 1,
    RTC_REG_MINUTES = 2,
    RTC_REG_MINUTES_ALARM = 3,
    RTC_REG_HOURS = 4,
    RTC_REG_HOURS_ALARM = 5,
    RTC_REG_WEEKDAY = 6,
    RTC_REG_DAY = 7,
    RTC_REG_MONTH = 8,
    RTC_REG_YEAR = 9,
    RTC_REG_A = 012,
    RTC_REG_B = 013,
    RTC_REG_C = 014,
    RTC_REG_D = 015
};

static void mk90_rtc_bcd_inc(byte *value)
{
    (*value)++;
    if (((*value) & 0017u) > 9u) {
        *value = (byte)(*value + 6u);
    }
}

static int mk90_rtc_periodic_rate_ms(const mk90_state_t *state)
{
    static const int table[16] = {
        0, 8, 8, 8, 8, 8, 8, 8, 8, 8, 16, 31, 63, 125, 250, 500
    };
    return table[state->rtc.data[RTC_REG_A] & 017u];
}

static void mk90_rtc_flag_irq(mk90_state_t *state)
{
    if ((state->rtc.data[RTC_REG_B] & state->rtc.data[RTC_REG_C] & 0160u) != 0u) {
        state->rtc.data[RTC_REG_C] |= 0200u;
        mk90_io_timer_irq(state);
    } else {
        state->rtc.data[RTC_REG_C] &= (byte)~0200u;
    }
}

static void mk90_rtc_update_second(mk90_state_t *state)
{
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    byte *rtc = state->rtc.data;

    if (rtc[RTC_REG_B] & 0200u) {
        rtc[RTC_REG_C] &= (byte)~0020u;
        return;
    }

    if ((rtc[RTC_REG_B] & 0004u) == 0u) {
        mk90_rtc_bcd_inc(&rtc[RTC_REG_SECONDS]);
        if (rtc[RTC_REG_SECONDS] < 0060u) {
            goto check_alarm;
        }
        rtc[RTC_REG_SECONDS] = 0;
        mk90_rtc_bcd_inc(&rtc[RTC_REG_MINUTES]);
        if (rtc[RTC_REG_MINUTES] < 0060u) {
            goto check_alarm;
        }
        rtc[RTC_REG_MINUTES] = 0;
        mk90_rtc_bcd_inc(&rtc[RTC_REG_HOURS]);
        if (rtc[RTC_REG_HOURS] < 0024u) {
            goto check_alarm;
        }
        rtc[RTC_REG_HOURS] = 0;
        mk90_rtc_bcd_inc(&rtc[RTC_REG_WEEKDAY]);
        if (rtc[RTC_REG_WEEKDAY] > 7u) {
            rtc[RTC_REG_WEEKDAY] = 1u;
        }
        mk90_rtc_bcd_inc(&rtc[RTC_REG_DAY]);
        if (rtc[RTC_REG_DAY] <= (byte)days[(rtc[RTC_REG_MONTH] - 1u) % 12u]) {
            goto check_alarm;
        }
        rtc[RTC_REG_DAY] = 1u;
        mk90_rtc_bcd_inc(&rtc[RTC_REG_MONTH]);
        if (rtc[RTC_REG_MONTH] <= 012u) {
            goto check_alarm;
        }
        rtc[RTC_REG_MONTH] = 1u;
        mk90_rtc_bcd_inc(&rtc[RTC_REG_YEAR]);
        if (rtc[RTC_REG_YEAR] <= 0143u) {
            goto check_alarm;
        }
        rtc[RTC_REG_YEAR] = 0u;
    } else {
        rtc[RTC_REG_SECONDS]++;
        if (rtc[RTC_REG_SECONDS] < 60u) {
            goto check_alarm;
        }
        rtc[RTC_REG_SECONDS] = 0u;
        rtc[RTC_REG_MINUTES]++;
        if (rtc[RTC_REG_MINUTES] < 60u) {
            goto check_alarm;
        }
        rtc[RTC_REG_MINUTES] = 0u;
        rtc[RTC_REG_HOURS]++;
        if (rtc[RTC_REG_HOURS] < 24u) {
            goto check_alarm;
        }
        rtc[RTC_REG_HOURS] = 0u;
        rtc[RTC_REG_WEEKDAY]++;
        if (rtc[RTC_REG_WEEKDAY] > 7u) {
            rtc[RTC_REG_WEEKDAY] = 1u;
        }
        rtc[RTC_REG_DAY]++;
        if (rtc[RTC_REG_DAY] <= (byte)days[(rtc[RTC_REG_MONTH] - 1u) % 12u]) {
            goto check_alarm;
        }
        rtc[RTC_REG_DAY] = 1u;
        rtc[RTC_REG_MONTH]++;
        if (rtc[RTC_REG_MONTH] <= 12u) {
            goto check_alarm;
        }
        rtc[RTC_REG_MONTH] = 1u;
        rtc[RTC_REG_YEAR]++;
        if (rtc[RTC_REG_YEAR] <= 99u) {
            goto check_alarm;
        }
        rtc[RTC_REG_YEAR] = 0u;
    }

check_alarm:
    if (rtc[RTC_REG_SECONDS_ALARM] == rtc[RTC_REG_SECONDS] &&
        rtc[RTC_REG_MINUTES_ALARM] == rtc[RTC_REG_MINUTES] &&
        rtc[RTC_REG_HOURS_ALARM] == rtc[RTC_REG_HOURS]) {
        rtc[RTC_REG_C] |= 0040u;
    } else {
        rtc[RTC_REG_C] &= (byte)~0040u;
    }
    rtc[RTC_REG_C] |= 0020u;
    mk90_rtc_flag_irq(state);
}

static void mk90_rtc_periodic_tick(mk90_state_t *state)
{
    if (state->rtc.data[RTC_REG_B] & 0010u) {
        mk90_machine_raise_evnt(state);
    }
    state->rtc.data[RTC_REG_C] |= 0100u;
    if (state->rtc.data[RTC_REG_B] & 0100u) {
        state->rtc.data[RTC_REG_C] |= 0200u;
        mk90_io_timer_irq(state);
    }
}

void mk90_rtc_reset(mk90_state_t *state)
{
    time_t now_time = time(NULL);
    struct tm now_tm;

    memset(state->rtc.data, 0, sizeof(state->rtc.data));
    state->rtc.second_accum_ms = 0;
    state->rtc.periodic_accum_ms = 0;

#if defined(_POSIX_VERSION)
    localtime_r(&now_time, &now_tm);
#else
    now_tm = *localtime(&now_time);
#endif

    state->rtc.data[RTC_REG_YEAR] = (byte)(now_tm.tm_year % 100);
    state->rtc.data[RTC_REG_MONTH] = (byte)(now_tm.tm_mon + 1);
    state->rtc.data[RTC_REG_DAY] = (byte)now_tm.tm_mday;
    state->rtc.data[RTC_REG_WEEKDAY] = (byte)(now_tm.tm_wday == 0 ? 7 : now_tm.tm_wday);
    state->rtc.data[RTC_REG_HOURS] = (byte)now_tm.tm_hour;
    state->rtc.data[RTC_REG_MINUTES] = (byte)now_tm.tm_min;
    state->rtc.data[RTC_REG_SECONDS] = (byte)now_tm.tm_sec;
}

void mk90_rtc_tick_ms(mk90_state_t *state, uint32_t elapsed_ms)
{
    int periodic_ms;

    state->rtc.second_accum_ms += elapsed_ms;
    state->rtc.periodic_accum_ms += elapsed_ms;

    while (state->rtc.second_accum_ms >= 1000u) {
        state->rtc.second_accum_ms -= 1000u;
        mk90_rtc_update_second(state);
    }

    periodic_ms = mk90_rtc_periodic_rate_ms(state);
    if (periodic_ms <= 0) {
        return;
    }

    while (state->rtc.periodic_accum_ms >= (uint32_t)periodic_ms) {
        state->rtc.periodic_accum_ms -= (uint32_t)periodic_ms;
        mk90_rtc_periodic_tick(state);
    }
}

word mk90_rtc_read_word(mk90_state_t *state, word offset)
{
    unsigned index = (unsigned)((offset >> 1) & 0077u);
    word value = (word)(state->rtc.data[index] << 1);

    if (index == RTC_REG_D) {
        state->rtc.data[RTC_REG_D] = 0200u;
    } else if (index == RTC_REG_C) {
        state->rtc.data[RTC_REG_C] = 0u;
    }

    return value;
}

byte mk90_rtc_read_byte(mk90_state_t *state, word offset)
{
    word value = mk90_rtc_read_word(state, offset);

    if (offset & 1u) {
        return (byte)((value >> 8) & 0377u);
    }
    return (byte)(value & 0377u);
}

void mk90_rtc_write_word(mk90_state_t *state, word offset, word value)
{
    unsigned index = (unsigned)((offset >> 1) & 0077u);

    if (index == RTC_REG_C || index == RTC_REG_D) {
        return;
    }

    state->rtc.data[index] = (byte)((value >> 1) & 0377u);
}

void mk90_rtc_write_byte(mk90_state_t *state, word offset, byte value)
{
    word word_value = mk90_rtc_read_word(state, offset);

    if (offset & 1u) {
        word_value = (word)((word_value & 000377u) | ((word)value << 8));
    } else {
        word_value = (word)((word_value & 0177400u) | value);
    }

    mk90_rtc_write_word(state, offset, word_value);
}
