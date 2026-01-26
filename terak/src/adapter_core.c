/* Adapter between TERAK emulator and the read‑only core library. */

#include "adapter_core.h"
#include "bus.h"
#include "dl11.h"
#include "rk11.h"
#include "util_term.h"
#include <stdio.h>

static void bus_error_trap(regs *r) {
    r->r[6] -= 0000002;
    r->store_word(r, r->r[6], r->psw);
    r->r[6] -= 0000002;
    r->store_word(r, r->r[6], r->r[7]);
    r->r[7] = r->load_word(r, 0000004);
    r->psw  = r->load_word(r, 0000006);
    r->fAbort = 1;
}

static int trace_irq_flag = 0;
static int watch_addr_on = 0;
static word watch_addr = 0;
static int watch_silent = 0;
static int trace_pc_on = 0;
static word trace_pc_addr = 0;
static int trace_reg_on = 0;
static int trace_reg_idx = -1;

/* Forward declarations for callbacks required by core.h */
static byte bus_load_byte(regs *r, word addr) {
    if (bus_is_nxm((paddr_t)addr)) {
        bus_error_trap(r);
        return 0;
    }
    return bus_read_byte((paddr_t)addr);
}

static void bus_store_byte(regs *r, word addr, byte v) {
    if (bus_is_nxm((paddr_t)addr)) {
        bus_error_trap(r);
        return;
    }
    if (watch_addr_on && addr == watch_addr) {
        if (!watch_silent) {
            printf("WATCH %06o <= %03o at PC=%06o\n", addr, v, r->r[7]);
        } else {
            printf("WATCH %06o <= %03o\n", addr, v);
        }
    }
    bus_write_byte((paddr_t)addr, v);
}

static word bus_load_word(regs *r, word addr) {
    if (bus_is_nxm((paddr_t)addr) || bus_is_nxm((paddr_t)(addr + 1))) {
        bus_error_trap(r);
        return 0;
    }
    return bus_read_word((paddr_t)addr);
}

static void bus_store_word(regs *r, word addr, word v) {
    if (bus_is_nxm((paddr_t)addr) || bus_is_nxm((paddr_t)(addr + 1))) {
        bus_error_trap(r);
        return;
    }
    if (watch_addr_on && addr == watch_addr) {
        if (!watch_silent) {
            printf("WATCH %06o <= %06o at PC=%06o\n", addr, v, r->r[7]);
        } else {
            printf("WATCH %06o <= %06o\n", addr, v);
        }
    }
    bus_write_word((paddr_t)addr, v);
}

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
    if (rk11_irq_pending()) {
        *vec = 0000220;
        rk11_irq_ack();
        if (trace_irq_flag) {
            word handler = bus_read_word(*vec);
            word psw = bus_read_word((word)(*vec + 2));
            word flag = bus_read_word(0154066);
            printf("IRQ vec=%06o at %06o -> %06o PS=%06o flag=%06o\n",
                   *vec, r->r[7], handler, psw, flag);
        }
        return 1;
    }
    (void)vec;
    (void)r;
    return 0;
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

void terak_set_trace_irq(int on) {
    trace_irq_flag = on ? 1 : 0;
}

void terak_set_watch_addr(word addr, int on) {
    watch_addr = addr;
    watch_addr_on = on ? 1 : 0;
}

void terak_set_watch_silent(int on) {
    watch_silent = on ? 1 : 0;
}

void terak_set_trace_pc(word addr, int on) {
    trace_pc_addr = addr;
    trace_pc_on = on ? 1 : 0;
}

int terak_trace_pc_enabled(void) {
    return trace_pc_on;
}

word terak_trace_pc_addr(void) {
    return trace_pc_addr;
}

void terak_set_trace_reg(int reg, int on) {
    trace_reg_idx = reg;
    trace_reg_on = on ? 1 : 0;
}

int terak_trace_reg_enabled(void) {
    return trace_reg_on;
}

int terak_trace_reg_index(void) {
    return trace_reg_idx;
}
