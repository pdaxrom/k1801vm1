/* Minimal DL11 console implementation.
 * The device is memory‑mapped as defined in the README. All registers are
 * byte‑addressable. Only the bits required for simple polling are modelled.
 */

#include "dl11.h"
#include "util_term.h"
#include <stdio.h>

static uint8_t rcsr = 0;   /* receive status */
static uint8_t rbuf = 0;   /* received character */
static uint8_t xcsr = DL11_CSR_READY; /* transmitter ready */
static uint8_t xbuf = 0;   /* last transmitted byte */

void dl11_reset(void) {
    rcsr = 0;
    rbuf = 0;
    xcsr = DL11_CSR_READY;
    xbuf = 0;
}

/* Host side polling – called each emulator loop iteration. */
void dl11_poll_input(void) {
    int ch = host_getch_nonblock();
    if (ch >= 0) {
        rbuf = (uint8_t)ch;
        rcsr = DL11_CSR_DONE; /* indicate data ready */
    }
}

/* Register accessors – the bus calls these. */
uint8_t dl11_read_rcsr(void) { return rcsr; }
uint8_t dl11_read_rbuf(void) {
    uint8_t v = rbuf;
    rcsr = 0;            /* clear DONE after read */
    return v;
}
uint8_t dl11_read_xcsr(void) { return xcsr; }
uint8_t dl11_read_xbuf(void) { return xbuf; }

void dl11_write_rcsr(uint8_t v) { (void)v; /* read‑only in this model */ }
void dl11_write_rbuf(uint8_t v) { (void)v; /* not used */ }
void dl11_write_xcsr(uint8_t v) { (void)v; /* ignore */ }
void dl11_write_xbuf(uint8_t v) {
    xbuf = v;
    /* Echo to host stdout. Convert CR to LF for terminal friendliness. */
    if (v == '\r') {
        putchar('\n');
    } else {
        putchar(v);
    }
    fflush(stdout);
    xcsr = DL11_CSR_READY; /* always ready */
}

