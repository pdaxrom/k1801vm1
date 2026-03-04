#if !defined(PICO_ON_DEVICE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "dev_kw11.h"
#include "bus.h"
#include "devio.h"
#include "irq.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#if defined(PICO_ON_DEVICE)
#include "pico/time.h"
#else
#include <time.h>
#endif

/* KW11-L address (octal) */
#define KW11L_CSR 0177546

/* KW11-P addresses (octal) */
#define KW11P_CSR 0172540
#define KW11P_CSB 0172542
#define KW11P_CTR 0172544

/* KW11-L CSR bits */
#define KW11L_DONE 000200
#define KW11L_IE   000100
/* Approximate 1 MHz CPU: 1,000,000 / 60 ~= 16667 instructions per line tick. */
#define KW11L_DEFAULT_STEPS_PER_TICK 16667u

/* KW11-P CSR bits */
#define KW11P_RUN         000001
#define KW11P_RATE_MASK   000006
#define KW11P_RATE_100KHZ 000000
#define KW11P_RATE_10KHZ  000002
#define KW11P_RATE_LINE   000004
#define KW11P_RATE_EXT    000006
#define KW11P_REPEAT      000010
#define KW11P_UP          000020
#define KW11P_FIX         000040
#define KW11P_IE          000100
#define KW11P_DONE        000200
#define KW11P_ERR         000400

/* --- KW11-L state --- */
static uint8_t kwl_done = 0;
static uint8_t kwl_ie = 0;
static uint8_t kwl_irq_req = 0;
static uint64_t kwl_last_tick_ns = 0;
static int kwl_clock_init = 0;

/* --- KW11-P state --- */
static uint16_t kwp_csr = 0;
static uint16_t kwp_csb = 0;
static uint16_t kwp_ctr = 0;
static uint8_t kwp_irq_req = 0;
static uint8_t kwp_irq_armed = 1;
static uint64_t kwp_last_tick_ns = 0;
static int kwp_clock_init = 0;
static int kw11_l_visible = -1;
static int kw11_p_visible = -1;
static int kw11_trace = 0;
static int kw11_step_clock = 0;
static uint32_t kw11_steps_per_tick = KW11L_DEFAULT_STEPS_PER_TICK;
static uint64_t kwl_steps_accum = 0;

static int kw11_default_l_visible(void)
{
    return 1;
}

static int kw11_default_p_visible(void)
{
    return 0;
}

static int kw11_l_visible_effective(void)
{
    return (kw11_l_visible >= 0) ? kw11_l_visible : kw11_default_l_visible();
}

static int kw11_p_visible_effective(void)
{
    return (kw11_p_visible >= 0) ? kw11_p_visible : kw11_default_p_visible();
}

static uint64_t now_ns(void)
{
#if defined(PICO_ON_DEVICE)
    return (uint64_t)time_us_64() * 1000ull;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

void kw11_set_visibility(int enable_l, int enable_p)
{
    kw11_l_visible = enable_l ? 1 : 0;
    kw11_p_visible = enable_p ? 1 : 0;
}

void kw11_set_step_clock(int on)
{
    kw11_step_clock = on ? 1 : 0;
}

static void kw11_tracef(const char *fmt, ...)
{
    va_list ap;
    if (!kw11_trace) {
        return;
    }
    va_start(ap, fmt);
    fputs("KW11 ", stderr);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

int kw11_l_enabled(void)
{
    return kw11_l_visible_effective();
}

int kw11_p_enabled(void)
{
    return kw11_p_visible_effective();
}

static void kwp_sw_clear_done(void)
{
    kwp_csr &= (uint16_t)~KW11P_DONE;
    kwp_irq_req = 0;
    kwp_irq_armed = 1;
}

static void kwp_sw_clear_err(void)
{
    kwp_csr &= (uint16_t)~KW11P_ERR;
}

static void kwp_terminal_event(void)
{
    if (kwp_csr & KW11P_DONE) {
        kwp_csr |= KW11P_ERR;
    } else {
        kwp_csr |= KW11P_DONE;
        if ((kwp_csr & KW11P_IE) && kwp_irq_armed) {
            kwp_irq_req = 1;
            kwp_irq_armed = 0;
        }
    }

    if (kwp_csr & KW11P_REPEAT) {
        kwp_ctr = kwp_csb;
    } else {
        kwp_csr &= (uint16_t)~KW11P_RUN;
        kwp_ctr = 0;
    }
}

static void kwp_step_one(void)
{
    uint16_t prev = kwp_ctr;

    if (kwp_csr & KW11P_UP) {
        kwp_ctr = (uint16_t)(kwp_ctr + 1);
        if (kwp_ctr == 0) {
            kwp_terminal_event();
        }
        return;
    }

    kwp_ctr = (uint16_t)(kwp_ctr - 1);
    if (prev == 0) {
        kwp_terminal_event();
    }
}

static void kwp_advance_pulses(uint64_t pulses)
{
    while (pulses-- > 0 && (kwp_csr & KW11P_RUN)) {
        kwp_step_one();
    }
}

static uint64_t kwp_period_ns(void)
{
    switch (kwp_csr & KW11P_RATE_MASK) {
    case KW11P_RATE_100KHZ:
        return 10000ull; /* 10 us */
    case KW11P_RATE_10KHZ:
        return 100000ull; /* 100 us */
    case KW11P_RATE_LINE:
        return 16666667ull; /* 60 Hz */
    case KW11P_RATE_EXT:
    default:
        return 0ull; /* external clock: no host-timer pulses */
    }
}

static void kwp_write_csr_low(uint8_t v)
{
    uint16_t old = kwp_csr;
    uint16_t control_mask = KW11P_RUN | KW11P_RATE_MASK | KW11P_REPEAT |
                            KW11P_UP | KW11P_FIX | KW11P_IE;

    kwp_csr = (uint16_t)((kwp_csr & (uint16_t)~control_mask) | (v & control_mask));

    /* DONE is software-clearable (write 0). */
    if ((v & KW11P_DONE) == 0) {
        kwp_sw_clear_done();
    }

    /* IE disable suppresses request immediately. */
    if ((kwp_csr & KW11P_IE) == 0) {
        kwp_irq_req = 0;
    }

    /* RUN rising edge loads counter from buffer. */
    if ((old & KW11P_RUN) == 0 && (kwp_csr & KW11P_RUN)) {
        kwp_ctr = kwp_csb;
        kwp_clock_init = 0;
        kwp_last_tick_ns = 0;
    }

    /* FIX is a software pulse for one maintenance step. */
    if (kwp_csr & KW11P_FIX) {
        kwp_step_one();
        kwp_csr &= (uint16_t)~KW11P_FIX;
    }
}

static void kwp_write_csr_high(uint8_t v)
{
    /* ERR is software-clearable via high-byte bit0 write=0. */
    if ((v & 000001) == 0) {
        kwp_sw_clear_err();
    }
}

/* --- I/O callbacks --- */
static uint8_t kw11_read8(uint16_t a)
{
    if (a == KW11L_CSR) {
        uint8_t v = 0;
        if (kwl_done) {
            v |= KW11L_DONE;
        }
        if (kwl_ie) {
            v |= KW11L_IE;
        }
        /* KW11-L monitor is read-to-clear. */
        kwl_done = 0;
        kwl_irq_req = 0;
        return v;
    }
    if (a == (uint16_t)(KW11L_CSR + 1)) {
        return 0;
    }

    if (a == KW11P_CSR) {
        return (uint8_t)(kwp_csr & 000377);
    }
    if (a == (uint16_t)(KW11P_CSR + 1)) {
        return (uint8_t)((kwp_csr >> 8) & 000001);
    }
    if (a == KW11P_CSB) {
        return (uint8_t)(kwp_csb & 000377);
    }
    if (a == (uint16_t)(KW11P_CSB + 1)) {
        return (uint8_t)((kwp_csb >> 8) & 000377);
    }
    if (a == KW11P_CTR) {
        return (uint8_t)(kwp_ctr & 000377);
    }
    if (a == (uint16_t)(KW11P_CTR + 1)) {
        return (uint8_t)((kwp_ctr >> 8) & 000377);
    }

    return 0;
}

static void kw11_write8(uint16_t a, uint8_t v)
{
    if (a == KW11L_CSR) {
        kwl_ie = (v & KW11L_IE) ? 1 : 0;
        if ((v & KW11L_DONE) == 0) {
            kwl_done = 0;
            kwl_irq_req = 0;
        }
        /*
         * If IE is set while monitor is already set, request interrupt
         * immediately.
         */
        if (kwl_ie && kwl_done) {
            kwl_irq_req = 1;
        }
        kw11_tracef("L CSR wr=%06o done=%o ie=%o irq=%o\n",
                    (unsigned)(v & 000377), kwl_done, kwl_ie, kwl_irq_req);
        return;
    }
    if (a == (uint16_t)(KW11L_CSR + 1)) {
        /* ignore high byte writes */
        return;
    }

    if (a == KW11P_CSR) {
        kwp_write_csr_low(v);
        return;
    }
    if (a == (uint16_t)(KW11P_CSR + 1)) {
        kwp_write_csr_high(v);
        return;
    }
    if (a == KW11P_CSB) {
        kwp_csb = (uint16_t)((kwp_csb & 0177400) | v);
        return;
    }
    if (a == (uint16_t)(KW11P_CSB + 1)) {
        kwp_csb = (uint16_t)((kwp_csb & 000377) | ((uint16_t)v << 8));
        return;
    }
    if (a == KW11P_CTR) {
        kwp_ctr = (uint16_t)((kwp_ctr & 0177400) | v);
        return;
    }
    if (a == (uint16_t)(KW11P_CTR + 1)) {
        kwp_ctr = (uint16_t)((kwp_ctr & 000377) | ((uint16_t)v << 8));
        return;
    }
}

/* --- IRQ source --- */
int kw11_irq_pending(void)
{
    if (kwp_irq_req) {
        return 1;
    }
    return kwl_irq_req ? 1 : 0;
}

void kw11_irq_ack(void)
{
    if (kwp_irq_req) {
        kwp_irq_req = 0;
        kw11_tracef("P irq ack\n");
        return;
    }
    kwl_irq_req = 0;
    kw11_tracef("L irq ack done=%o ie=%o irq=%o\n", kwl_done, kwl_ie, kwl_irq_req);
}

int kw11_init(void)
{
    static const io_range_t l = { 0177546, 0177547, kw11_read8, kw11_write8, "KW11-L" };
    static const io_range_t p = { 0172540, 0172545, kw11_read8, kw11_write8, "KW11-P" };
    int l_on = kw11_l_visible_effective();
    int p_on = kw11_p_visible_effective();

    kw11_trace = (getenv("LSI11_TRACE_KW11") != NULL) ? 1 : 0;
    {
        const char *env_steps = getenv("LSI11_KW11_STEPS_PER_TICK");
        if (env_steps && *env_steps) {
            char *end = NULL;
            unsigned long v = strtoul(env_steps, &end, 10);
            if (end && *end == '\0' && v > 0 && v <= 1000000ul) {
                kw11_steps_per_tick = (uint32_t)v;
            }
        }
    }

    if (l_on && devio_register(&l) != 0) {
        return -1;
    }
    if (p_on && devio_register(&p) != 0) {
        return -1;
    }
    if (!l_on && !p_on) {
        kw11_reset();
        return 0;
    }

    static const irq_source_t s = { "KW11", 000100, 6, kw11_irq_pending, kw11_irq_ack };
    if (irq_register(&s) != 0) {
        return -1;
    }

    kw11_reset();
    return 0;
}

void kw11_reset(void)
{
    kwl_done = 1; /* Set by INIT */
    kwl_ie = 0;   /* Cleared by INIT */
    kwl_irq_req = 0;
    kwl_clock_init = 0;
    kwl_last_tick_ns = 0;
    kwl_steps_accum = 0;

    kwp_csr = 0;
    kwp_csb = 0;
    kwp_ctr = 0;
    kwp_irq_req = 0;
    kwp_irq_armed = 1;
    kwp_clock_init = 0;
    kwp_last_tick_ns = 0;
}

/* KW11-L/KW11-P realtime poll used for compatibility mode. */
static void kw11_poll_realtime(void)
{
    uint64_t t = now_ns();
    const uint64_t kwl_period_ns = 16666667ull; /* ~16.666 ms -> 60 Hz */

    /* --- KW11-L --- */
    if (!kwl_clock_init) {
        kwl_clock_init = 1;
        kwl_last_tick_ns = t;
    } else if (t < kwl_last_tick_ns) {
        kwl_last_tick_ns = t;
    } else {
        uint64_t elapsed = t - kwl_last_tick_ns;
        if (elapsed >= kwl_period_ns) {
            uint64_t ticks = elapsed / kwl_period_ns;
            kwl_last_tick_ns += ticks * kwl_period_ns;
            kwl_done = 1;
            if (kwl_ie) {
                kwl_irq_req = 1;
            }
            if (kw11_trace && kwl_ie) {
                kw11_tracef("L tick ticks=%06o done=%o ie=%o irq=%o\n",
                            (unsigned)(ticks & 0177777), kwl_done, kwl_ie, kwl_irq_req);
            }
        }
    }

    /* --- KW11-P --- */
    if ((kwp_csr & KW11P_RUN) == 0) {
        return;
    }

    if (!kwp_clock_init) {
        kwp_clock_init = 1;
        kwp_last_tick_ns = t;
        return;
    }
    if (t < kwp_last_tick_ns) {
        kwp_last_tick_ns = t;
        return;
    }

    uint64_t period_ns = kwp_period_ns();
    if (period_ns == 0) {
        return;
    }

    uint64_t elapsed = t - kwp_last_tick_ns;
    if (elapsed < period_ns) {
        return;
    }

    uint64_t pulses = elapsed / period_ns;
    kwp_last_tick_ns += pulses * period_ns;
    kwp_advance_pulses(pulses);
}

/* KW11-L deterministic poll tied to number of executed CPU instructions. */
void kw11_poll_steps(uint32_t cpu_steps)
{
    if (!kw11_step_clock) {
        kw11_poll_realtime();
        return;
    }

    if (cpu_steps == 0) {
        cpu_steps = 1;
    }

    kwl_steps_accum += cpu_steps;
    if (kwl_steps_accum >= kw11_steps_per_tick) {
        uint64_t ticks = kwl_steps_accum / kw11_steps_per_tick;
        kwl_steps_accum %= kw11_steps_per_tick;
        kwl_done = 1;
        if (kwl_ie) {
            kwl_irq_req = 1;
        }
        if (kw11_trace && kwl_ie) {
            kw11_tracef("L step-tick ticks=%06o done=%o ie=%o irq=%o\n",
                        (unsigned)(ticks & 0177777), kwl_done, kwl_ie, kwl_irq_req);
        }
    }

    /* KW11-P is disabled by default on this target. */
}

/* Legacy poll API. */
void kw11_poll(void)
{
    kw11_poll_steps(1);
}
