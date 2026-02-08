#include "dev_dl11.h"
#include "devio.h"
#include "irq.h"
#include "irq_latch.h"
#include "util_term.h"
#include <time.h>

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
static uint64_t tx_ready_ns = 0;
static int tx_busy = 0;

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* Do not bind TX completion to host wall-clock by default.
   In unthrottled CPU mode, real-time delays can starve guest polling loops. */
#define DL11_TX_CHAR_NS 0ull

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
        /* allow writing IE only; if DONE is already set, enabling IE raises IRQ */
        if (v & CSR_IE) {
            if (!rx_l.ie && rx_l.done) {
                rx_l.irq_req = 1;
            }
            rx_l.ie = 1;
        } else {
            rx_l.ie = 0;
        }
        return;
    case DL11_TCSR:
        /* if transmitter is already DONE, enabling IE should request IRQ */
        if (v & CSR_IE) {
            if (!tx_l.ie && tx_l.done) {
                tx_l.irq_req = 1;
            }
            tx_l.ie = 1;
        } else {
            tx_l.ie = 0;
        }
        return;
    case DL11_TBUF:
        /* “Software clears DONE”: writing TBUF clears TX DONE */
        irq_latch_sw_clear_done(&tx_l);

        /* output low byte */
        util_term_putc((char)v);

        /* transmitter becomes ready later -> DONE=1 */
        tx_busy = 1;
        tx_ready_ns = now_ns() + DL11_TX_CHAR_NS;
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

    /* TX starts ready: DONE=1 */
    irq_latch_event_set_done(&tx_l);
    tx_busy = 0;
    tx_ready_ns = 0;
}

void dl11_poll(void)
{
    if (tx_busy) {
        uint64_t t = now_ns();
        if (t >= tx_ready_ns) {
            tx_busy = 0;
            irq_latch_event_set_done(&tx_l);
        }
    }

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
