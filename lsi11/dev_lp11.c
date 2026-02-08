#include "dev_lp11.h"
#include "devio.h"
#include "irq.h"
#include "irq_latch.h"
#include <stdio.h>

/* CSR/DBR (octal) */
#define LP11_CSR 0177514
#define LP11_DBR 0177516

/* Bits (byte) */
#define CSR_DONE 000200
#define CSR_IE   000100

static irq_latch_t lp_l;

static uint8_t lp11_read8(uint16_t a)
{
    if (a == LP11_CSR) {
        uint8_t v = 0;
        if (lp_l.done) v |= CSR_DONE;
        if (lp_l.ie)   v |= CSR_IE;
        return v;
    }
    if (a == (uint16_t)(LP11_CSR + 1)) return 0;

    if (a == LP11_DBR) return 0;
    if (a == (uint16_t)(LP11_DBR + 1)) return 0;

    return 0;
}

static void lp11_write8(uint16_t a, uint8_t v)
{
    if (a == LP11_CSR) {
        /* allow IE writes; do NOT clear DONE here unless you explicitly want that feature */
        irq_latch_set_ie(&lp_l, (v & CSR_IE) ? 1 : 0);
        return;
    }
    if (a == (uint16_t)(LP11_CSR + 1)) return; /* ignore */

    if (a == LP11_DBR) {
        /* Software clears DONE: writing DBR */
        irq_latch_sw_clear_done(&lp_l);

        /* output char */
        fputc((int)v, stdout);
        fflush(stdout);

        /* printer becomes ready again: DONE=1 event */
        irq_latch_event_set_done(&lp_l);
        return;
    }
    if (a == (uint16_t)(LP11_DBR + 1)) {
        /* ignore high byte writes */
        return;
    }
}

int lp11_irq_pending(void) { return (lp_l.ie && lp_l.done) ? 1 : 0; }
void lp11_irq_ack(void) { }

int lp11_init(void)
{
    static const io_range_t r = { 0177514, 0177517, lp11_read8, lp11_write8, "LP11" };
    if (devio_register(&r) != 0) return -1;

    static const irq_source_t s = { "LP11", 000200, 4, lp11_irq_pending, lp11_irq_ack };
    if (irq_register(&s) != 0) return -1;

    lp11_reset();
    return 0;
}

void lp11_reset(void)
{
    irq_latch_reset(&lp_l);

    /* printer starts ready => DONE=1 */
    lp_l.done = 0;
    lp_l.irq_armed = 1;
    irq_latch_event_set_done(&lp_l);
}

void lp11_poll(void)
{
    /* optional: nothing needed for minimal model */
}
