#include "dev_tm11.h"

#include "devio.h"
#include "irq.h"
#include "irq_latch.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Minimal TM11 presence model for BSD 2.9 autoconfig probing.
 * It decodes CSR space at 0172520 and raises an IRQ on GO with IE.
 */
#define TM11_ER  0172520
#define TM11_CSR 0172522
#define TM11_BC  0172524
#define TM11_BA  0172526

#define TMCS_GO   0000001
#define TMCS_IE   0000100
#define TMCS_DONE 0000200

static irq_latch_t tm_l;
static uint16_t tmer;
static uint16_t tmcs;
static uint16_t tmbc;
static uint16_t tmba;
static int tm_debug;
static unsigned tm_debug_rd_ctr;

static uint16_t tmcs_visible(void)
{
    uint16_t v = (uint16_t)(tmcs & (uint16_t)~(TMCS_IE | TMCS_DONE));
    if (tm_l.ie) {
        v |= TMCS_IE;
    }
    if (tm_l.done) {
        v |= TMCS_DONE;
    }
    return v;
}

static uint8_t tm11_read8(uint16_t addr)
{
    uint16_t base = (uint16_t)(addr & 0177776);
    uint16_t v = 0;

    switch (base) {
    case TM11_ER:
        v = tmer;
        break;
    case TM11_CSR:
        v = tmcs_visible();
        break;
    case TM11_BC:
        v = tmbc;
        break;
    case TM11_BA:
        v = tmba;
        break;
    default:
        return 0;
    }

    if (addr & 1) {
        if (tm_debug && tm_debug_rd_ctr < 400) {
            fprintf(stderr, "TM11 RD addr=%06o base=%06o -> %03o\n",
                    addr, base, (unsigned)((v >> 8) & 000377));
            tm_debug_rd_ctr++;
        }
        return (uint8_t)((v >> 8) & 000377);
    }
    if (tm_debug && tm_debug_rd_ctr < 400) {
        fprintf(stderr, "TM11 RD addr=%06o base=%06o -> %03o\n",
                addr, base, (unsigned)(v & 000377));
        tm_debug_rd_ctr++;
    }
    return (uint8_t)(v & 000377);
}

static void tm11_start_cmd(void)
{
    irq_latch_sw_clear_done(&tm_l);
    /* Complete immediately for probe/boot compatibility. */
    irq_latch_event_set_done(&tm_l);
    tmcs &= (uint16_t)~TMCS_GO;
}

static void tm11_write8(uint16_t addr, uint8_t b)
{
    uint16_t base = (uint16_t)(addr & 0177776);
    uint16_t *reg = 0;
    uint16_t old = 0;
    uint16_t v = 0;

    switch (base) {
    case TM11_ER:
        reg = &tmer;
        break;
    case TM11_CSR:
        reg = &tmcs;
        break;
    case TM11_BC:
        reg = &tmbc;
        break;
    case TM11_BA:
        reg = &tmba;
        break;
    default:
        return;
    }

    old = *reg;
    if (addr & 1) {
        v = (uint16_t)((old & 000377) | ((uint16_t)b << 8));
    } else {
        v = (uint16_t)((old & 0177400) | b);
    }
    *reg = v;
    if (tm_debug) {
        fprintf(stderr,
                "TM11 WR addr=%06o base=%06o b=%03o old=%06o new=%06o ie=%d done=%d\n",
                addr, base, b, old, v, tm_l.ie ? 1 : 0, tm_l.done ? 1 : 0);
    }

    if (base == TM11_CSR && !(addr & 1)) {
        int old_ie = tm_l.ie ? 1 : 0;
        int new_ie = (v & TMCS_IE) ? 1 : 0;

        irq_latch_set_ie(&tm_l, new_ie);
        if (!old_ie && new_ie && tm_l.done) {
            tm_l.irq_req = 1;
        }

        if (v & TMCS_GO) {
            tm11_start_cmd();
        }
    }
}

int tm11_irq_pending(void)
{
    return tm_l.irq_req ? 1 : 0;
}

void tm11_irq_ack(void)
{
    irq_latch_ack(&tm_l);
}

int tm11_init(void)
{
    static const io_range_t r = {TM11_ER, (uint16_t)(TM11_BA + 1), tm11_read8,
                                 tm11_write8, "TM11"
                                };
    static const irq_source_t s = {"TM11", 000224, 5, tm11_irq_pending, tm11_irq_ack};

    if (devio_register(&r) != 0) {
        return -1;
    }
    if (irq_register(&s) != 0) {
        return -1;
    }
    tm_debug = (getenv("TM11_DEBUG") != NULL) ? 1 : 0;
    tm_debug_rd_ctr = 0;

    tm11_reset();
    return 0;
}

void tm11_reset(void)
{
    tmer = 0;
    tmcs = 0;
    tmbc = 0;
    tmba = 0;
    irq_latch_reset(&tm_l);
    irq_latch_event_set_done(&tm_l);
}

void tm11_poll(void)
{
    /* No background timing in minimal model. */
}
