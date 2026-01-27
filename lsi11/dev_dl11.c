#include "dev_dl11.h"
#include "devio.h"
#include "irq.h"
#include "irq_latch.h"
#include "util_term.h"

/* CSR addresses (octal) */
#define DL11_BASE_PRIMARY 0177560
#define DL11_BASE_ALIAS   0176500

#define DL11_RCSR 0177560
#define DL11_RBUF 0177562
#define DL11_TCSR 0177564
#define DL11_TBUF 0177566

/* Bits (byte): DONE=0200, IE=0100 */
#define CSR_DONE 000200
#define CSR_IE   000100

static irq_latch_t rx_l;
static irq_latch_t tx_l;

static uint8_t rx_buf = 0;

static uint16_t norm(uint16_t a)
{
    if ((a & 0177770) == DL11_BASE_ALIAS)
        return (uint16_t)(DL11_BASE_PRIMARY | (a & 0000007));
    return a;
}

/* --- I/O callbacks --- */
static uint8_t dl11_read8(uint16_t a)
{
    a = norm(a);
    switch (a) {
    case DL11_RCSR: {
        uint8_t v = 0;
        if (rx_l.done) v |= CSR_DONE;
        if (rx_l.ie)   v |= CSR_IE;
        return v;
    }
    case DL11_RBUF: {
        /* “Software clears DONE”: reading RBUF clears RX DONE */
        uint8_t ch = rx_buf;
        irq_latch_sw_clear_done(&rx_l);
        return ch;
    }
    case DL11_TCSR: {
        uint8_t v = 0;
        if (tx_l.done) v |= CSR_DONE;
        if (tx_l.ie)   v |= CSR_IE;
        return v;
    }
    case DL11_TBUF:
        return 0;
    default:
        return 0;
    }
}

static void dl11_write8(uint16_t a, uint8_t v)
{
    a = norm(a);
    switch (a) {
    case DL11_RCSR:
        /* allow writing IE only */
        irq_latch_set_ie(&rx_l, (v & CSR_IE) ? 1 : 0);
        return;
    case DL11_TCSR:
        irq_latch_set_ie(&tx_l, (v & CSR_IE) ? 1 : 0);
        return;
    case DL11_TBUF:
        /* “Software clears DONE”: writing TBUF clears TX DONE */
        irq_latch_sw_clear_done(&tx_l);

        /* output low byte */
        util_term_putc((char)v);

        /* transmitter becomes ready again -> DONE=1 event */
        irq_latch_event_set_done(&tx_l);
        return;
    default:
        return;
    }
}

/* --- IRQ sources --- */
int dl11_rx_irq_pending(void) { return rx_l.irq_req ? 1 : 0; }
void dl11_rx_irq_ack(void) { irq_latch_ack(&rx_l); }

int dl11_tx_irq_pending(void) { return tx_l.irq_req ? 1 : 0; }
void dl11_tx_irq_ack(void) { irq_latch_ack(&tx_l); }

int dl11_init(void)
{
    /* Register BOTH ranges to same callbacks (aliasing) */
    static const io_range_t primary = { 0177560, 0177567, dl11_read8, dl11_write8, "DL11(primary)" };
    static const io_range_t alias   = { 0176500, 0176507, dl11_read8, dl11_write8, "DL11(alias)" };

    if (devio_register(&primary) != 0) return -1;
    if (devio_register(&alias) != 0) return -1;

    static const irq_source_t rxsrc = { "DL11 RX", 000060, 4, dl11_rx_irq_pending, dl11_rx_irq_ack };
    static const irq_source_t txsrc = { "DL11 TX", 000064, 4, dl11_tx_irq_pending, dl11_tx_irq_ack };

    if (irq_register(&rxsrc) != 0) return -1;
    if (irq_register(&txsrc) != 0) return -1;

    dl11_reset();
    return 0;
}

void dl11_reset(void)
{
    irq_latch_reset(&rx_l);
    irq_latch_reset(&tx_l);

    /* RX starts empty: DONE=0 */
    rx_buf = 0;

    /* TX starts ready: DONE=1 (event without IRQ unless IE already set) */
    tx_l.done = 0;
    tx_l.irq_armed = 1;
    irq_latch_event_set_done(&tx_l);
}

void dl11_poll_input(void)
{
    int c;
    /* host input -> if RX DONE already set, drop or buffer? minimal: drop */
    if (rx_l.done) return;

    c = util_term_getc_nonblock();
    if (c < 0) return;

    rx_buf = (uint8_t)c;
    irq_latch_event_set_done(&rx_l);
}

#ifdef LSI11_TESTS
void dl11_test_inject_rx(uint8_t ch)
{
    /* Do not overwrite if RX still holds unread data (DONE==1) */
    if (rx_l.done) return;

    rx_buf = ch;
    irq_latch_event_set_done(&rx_l);
}
#endif
