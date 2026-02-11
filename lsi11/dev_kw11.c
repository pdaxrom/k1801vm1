#define _POSIX_C_SOURCE 200809L
#include "dev_kw11.h"
#include "devio.h"
#include "irq.h"
#include <stdint.h>
#include <time.h>

/* KW11-L address (octal) */
#define KW11L_CSR 0177546

/* KW11-P addresses (octal) */
#define KW11P_CSR 0172540
#define KW11P_CSB 0172542
#define KW11P_CTR 0172544

/* KW11-L CSR bits */
#define KW11L_DONE 000200
#define KW11L_IE   000100

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

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
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
        if (kwp_ctr == 0)
            kwp_terminal_event();
        return;
    }

    kwp_ctr = (uint16_t)(kwp_ctr - 1);
    if (prev == 0)
        kwp_terminal_event();
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
    if ((v & KW11P_DONE) == 0)
        kwp_sw_clear_done();

    /* IE disable suppresses request immediately. */
    if ((kwp_csr & KW11P_IE) == 0)
        kwp_irq_req = 0;

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
    if ((v & 000001) == 0)
        kwp_sw_clear_err();
}

/* --- I/O callbacks --- */
static uint8_t kw11_read8(uint16_t a)
{
    if (a == KW11L_CSR) {
        uint8_t v = 0;
        if (kwl_done) v |= KW11L_DONE;
        if (kwl_ie)   v |= KW11L_IE;
        /* LKS monitor is read/clear. */
        kwl_done = 0;
        kwl_irq_req = 0;
        return v;
    }
    if (a == (uint16_t)(KW11L_CSR + 1))
        return 0;

    if (a == KW11P_CSR)
        return (uint8_t)(kwp_csr & 000377);
    if (a == (uint16_t)(KW11P_CSR + 1))
        return (uint8_t)((kwp_csr >> 8) & 000001);
    if (a == KW11P_CSB)
        return (uint8_t)(kwp_csb & 000377);
    if (a == (uint16_t)(KW11P_CSB + 1))
        return (uint8_t)((kwp_csb >> 8) & 000377);
    if (a == KW11P_CTR)
        return (uint8_t)(kwp_ctr & 000377);
    if (a == (uint16_t)(KW11P_CTR + 1))
        return (uint8_t)((kwp_ctr >> 8) & 000377);

    return 0;
}

static void kw11_write8(uint16_t a, uint8_t v)
{
    if (a == KW11L_CSR) {
        kwl_ie = (v & KW11L_IE) ? 1 : 0;
        /*
         * If IE is set while monitor is already set, request interrupt
         * immediately.
         */
        if (kwl_ie && kwl_done) {
            kwl_irq_req = 1;
        }
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
    if (kwp_irq_req)
        return 1;
    return kwl_irq_req ? 1 : 0;
}

void kw11_irq_ack(void)
{
    if (kwp_irq_req) {
        kwp_irq_req = 0;
        return;
    }
    kwl_irq_req = 0;
}

int kw11_init(void)
{
    static const io_range_t l = { 0177546, 0177547, kw11_read8, kw11_write8, "KW11-L" };
    static const io_range_t p = { 0172540, 0172545, kw11_read8, kw11_write8, "KW11-P" };
    if (devio_register(&l) != 0)
        return -1;
    if (devio_register(&p) != 0)
        return -1;

    static const irq_source_t s = { "KW11", 000100, 6, kw11_irq_pending, kw11_irq_ack };
    if (irq_register(&s) != 0)
        return -1;

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

    kwp_csr = 0;
    kwp_csb = 0;
    kwp_ctr = 0;
    kwp_irq_req = 0;
    kwp_irq_armed = 1;
    kwp_clock_init = 0;
    kwp_last_tick_ns = 0;
}

/* KW11-L: 50 Hz ticks; monitor set on each tick.
   KW11-P: programmable timer (100 kHz / 10 kHz / 60 Hz / external). */
void kw11_poll(void)
{
    uint64_t t = now_ns();
    const uint64_t kwl_period_ns = 20000000ull; /* 20 ms -> 50 Hz */

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
            if (kwl_ie)
                kwl_irq_req = 1;
        }
    }

    /* --- KW11-P --- */
    if ((kwp_csr & KW11P_RUN) == 0)
        return;

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
    if (period_ns == 0)
        return;

    uint64_t elapsed = t - kwp_last_tick_ns;
    if (elapsed < period_ns)
        return;

    uint64_t pulses = elapsed / period_ns;
    kwp_last_tick_ns += pulses * period_ns;
    kwp_advance_pulses(pulses);
}
