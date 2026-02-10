#define _POSIX_C_SOURCE 200809L
#include "dev_kw11.h"
#include "devio.h"
#include "irq.h"
#include <stdint.h>
#include <time.h>

/* CSR address (octal) */
#define KW11_CSR 0177546

/* CSR bits (byte) */
#define CSR_DONE 000200
#define CSR_IE   000100

static uint8_t kw_done = 0;
static uint8_t kw_ie = 0;
static uint8_t kw_irq_req = 0;
static uint64_t last_tick_ns = 0;
static int clock_init = 0;

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* --- I/O callbacks --- */
static uint8_t kw11_read8(uint16_t a)
{
    if (a == KW11_CSR) {
        uint8_t v = 0;
        if (kw_done) v |= CSR_DONE;
        if (kw_ie)   v |= CSR_IE;
        /* LKS monitor is read/clear. */
        kw_done = 0;
        kw_irq_req = 0;
        return v;
    }
    if (a == (uint16_t)(KW11_CSR + 1)) return 0;
    return 0;
}

static void kw11_write8(uint16_t a, uint8_t v)
{
    if (a == KW11_CSR) {
        kw_ie = (v & CSR_IE) ? 1 : 0;
        /*
         * If IE is set while monitor is already set, request interrupt
         * immediately.
         */
        if (kw_ie && kw_done) {
            kw_irq_req = 1;
        }
        return;
    }
    if (a == (uint16_t)(KW11_CSR + 1)) {
        /* ignore high byte writes */
        return;
    }
}

/* --- IRQ source --- */
int kw11_irq_pending(void) { return kw_irq_req ? 1 : 0; }
void kw11_irq_ack(void)
{
    kw_irq_req = 0;
}

int kw11_init(void)
{
    static const io_range_t r = { 0177546, 0177547, kw11_read8, kw11_write8, "KW11-L" };
    if (devio_register(&r) != 0) return -1;

    static const irq_source_t s = { "KW11", 000100, 6, kw11_irq_pending, kw11_irq_ack };
    if (irq_register(&s) != 0) return -1;

    kw11_reset();
    return 0;
}

void kw11_reset(void)
{
    kw_done = 1; /* Set by INIT */
    kw_ie = 0;   /* Cleared by INIT */
    kw_irq_req = 0;
    clock_init = 0;
    last_tick_ns = 0;
}

/* 50 Hz ticks; monitor set on each tick. */
void kw11_poll(void)
{
    const uint64_t period_ns = 20000000ull; /* 20ms */
    uint64_t t = now_ns();

    if (!clock_init) {
        clock_init = 1;
        last_tick_ns = t;
        return;
    }
    if (t < last_tick_ns) { last_tick_ns = t; return; }

    uint64_t elapsed = t - last_tick_ns;
    if (elapsed < period_ns) return;

    /* advance time base (drop missed ticks) */
    uint64_t ticks = elapsed / period_ns;
    last_tick_ns += ticks * period_ns;

    kw_done = 1;
    if (kw_ie) {
        kw_irq_req = 1;
    }
}
