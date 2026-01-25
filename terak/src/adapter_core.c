/* Adapter between TERAK emulator and the read‑only core library. */

#include "adapter_core.h"
#include "bus.h"
#include "dl11.h"
#include "rk11.h"
#include "util_term.h"
#include <stdio.h>

/* Forward declarations for callbacks required by core.h */
static byte  bus_load_byte(regs *r, word addr)   { (void)r; return bus_read_byte((paddr_t)addr); }
static void  bus_store_byte(regs *r, word addr, byte v) { (void)r; bus_write_byte((paddr_t)addr, v); }
static word  bus_load_word(regs *r, word addr)   { (void)r; return bus_read_word((paddr_t)addr); }
static void  bus_store_word(regs *r, word addr, word v) { (void)r; bus_write_word((paddr_t)addr, v); }

static int terak_init(regs *r) {
    (void)r;
    /* core library expects init to succeed */
    return 0;
}

static void terak_reset(regs *r) {
    (void)r;
    bus_init();
    dl11_reset();
    rk11_reset();
    term_raw_init();
}

static void terak_fini(regs *r) {
    (void)r;
    term_raw_restore();
}

static int terak_poll_irq(regs *r, word *vec) {
    (void)r; (void)vec; return 0; /* no interrupts */
}

static uint8_t *terak_ramptr(regs *r, word offset) {
    (void)r; return &ram[offset];
}

void terak_hw_connect(regs *r) {
    r->load_byte   = bus_load_byte;
    r->store_byte  = bus_store_byte;
    r->load_word   = bus_load_word;
    r->store_word  = bus_store_word;
    r->init        = terak_init;
    r->reset       = terak_reset;
    r->fini        = terak_fini;
    r->poll_irq    = terak_poll_irq;
    r->ramptr      = terak_ramptr;
}

void terak_poll_input(void) {
    dl11_poll_input();
    rk11_poll();
}
