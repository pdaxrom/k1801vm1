#include "adapter_core.h"

#include "bus.h"
#include "irq.h"

/* device modules */
#include "dev_dl11.h"
#include "dev_kw11.h"
#include "dev_lp11.h"
#include "dev_rk11.h"
#include "dev_sr.h"

#include "util_term.h"

#include <stdio.h>

/* ---- core integration ----
   You must include the correct core header(s) here.
   Typical: ../core/core.h or ../core/pdp11.h etc. */
#include "../core/core.h"  /* TODO: replace with your actual core header */
#include "../core/disas.h" /* optional if you want disassembly on NXM */

/* If your core defines word/byte types, use them; otherwise define here.
   TODO: remove these if core already defines them. */
#ifndef word
typedef uint16_t word;
#endif
#ifndef byte
typedef uint8_t byte;
#endif

static int trace_irq_flag = 0;
static int trace_nxm_flag = 0;

/* ---------- NXM trap ----------
   This matches your previous approach: push PSW and PC, then vector through
   000004/000006. If your core already provides a bus error trap helper, use
   that instead. */
static void nxm_trap(regs *r, word addr) {
  (void)addr;

  /* Optional trace */
  if (trace_nxm_flag) {
    word pc = r->r[7];
    word disas_pc = (pc >= 0000002) ? (word)(pc - 0000002) : pc;
    char buf[128];
    word tmp = disas_pc;
    disas(r, &tmp, buf);
    fprintf(stderr, "NXM at %06o PC=%06o IR=%06o %s\n", addr, disas_pc, r->ir,
            buf);
    fprintf(stderr,
            "R0=%06o R1=%06o R2=%06o R3=%06o R4=%06o R5=%06o SP=%06o PS=%06o\n",
            r->r[0], r->r[1], r->r[2], r->r[3], r->r[4], r->r[5], r->r[6],
            r->psw);
  }

  /* Match core bus-error semantics: push current PC. */
  word fault_pc = r->r[7];
  /* push PSW then PC */
  r->r[6] -= 0000002;
  r->store_word(r, r->r[6], r->psw);
  r->r[6] -= 0000002;
  r->store_word(r, r->r[6], fault_pc);

  r->r[7] = r->load_word(r, 0000004);
  r->psw = r->load_word(r, 0000006);

  r->fAbort = 1;
}

/* ---------- bus callbacks for core ---------- */

static byte core_load_byte(regs *r, word addr) {
  if ((addr & 0177776) == 0177776) {
    return (byte)((addr & 1) ? ((r->psw >> 8) & 000377) : (r->psw & 000377));
  }
  if (bus_is_nxm((paddr_t)addr)) {
    nxm_trap(r, addr);
    return 0;
  }
  return bus_read8((paddr_t)addr);
}

static void core_store_byte(regs *r, word addr, byte v) {
  if ((addr & 0177776) == 0177776) {
    word psw = r->psw;
    if (addr & 1)
      psw = (word)((psw & 000377) | ((word)v << 8));
    else
      psw = (word)((psw & 0177400) | v);
    r->psw = psw;
    return;
  }
  if (bus_is_nxm((paddr_t)addr)) {
    nxm_trap(r, addr);
    return;
  }
  bus_write8((paddr_t)addr, v);
}

static word core_load_word(regs *r, word addr) {
  if (addr == 0177776) return r->psw;
  if (bus_is_nxm((paddr_t)addr) || bus_is_nxm((paddr_t)(addr + 1))) {
    nxm_trap(r, addr);
    return 0;
  }
  return (word)bus_read16((paddr_t)addr);
}

static void core_store_word(regs *r, word addr, word v) {
  if (addr == 0177776) {
    r->psw = v;
    return;
  }
  if (bus_is_nxm((paddr_t)addr) || bus_is_nxm((paddr_t)(addr + 1))) {
    nxm_trap(r, addr);
    return;
  }
  bus_write16((paddr_t)addr, (uint16_t)v);
}

/* core expects init/reset/fini */
static int impl_init(regs *r) {
  (void)r;
  /* init bus RAM; devices register their I/O in *_init() */
  bus_init();

  /* init host terminal */
  util_term_init_raw();

  /* init device frameworks (devio/irq are implicit singletons) */
  if (dl11_init() != 0)
    return -1;
  if (kw11_init() != 0)
    return -1;
  if (rk11_init() != 0)
    return -1;
  if (lp11_init() != 0)
    return -1;
  if (sr_init() != 0)
    return -1;

  return 0;
}

static void impl_reset(regs *r) {
  (void)r;
  dl11_reset();
  kw11_reset();
  rk11_reset();
  lp11_reset();
  sr_reset();
}

static void impl_fini(regs *r) {
  (void)r;
  util_term_restore();
}

/* IRQ poll callback: delegates to irq_poll() */
static int core_poll_irq(regs *r, word *vec) {
  uint16_t v = 0;
  if (!irq_poll(r, &v))
    return 0;

  *vec = (word)v;

  if (trace_irq_flag) {
    word vec_addr = (word)(*vec & 0000777);
    word handler = bus_read16(vec_addr);
    word psw = bus_read16((word)(vec_addr + 2));
    fprintf(stderr, "IRQ vec=%06o at %06o -> %06o PS=%06o\n", *vec, r->r[7],
            handler, psw);
  }

  return 1;
}

/* RAM pointer for core: core may use this for fast access.
   Only RAM 000000..0157777 is “real”, but backing array is 64K. */
static uint8_t *core_ramptr(regs *r, word offset) {
  (void)r;
  return &ram[offset];
}

void lsi11_hw_connect(regs *r) {
  r->load_byte = core_load_byte;
  r->store_byte = core_store_byte;
  r->load_word = core_load_word;
  r->store_word = core_store_word;

  r->init = impl_init;
  r->reset = impl_reset;
  r->fini = impl_fini;

  r->poll_irq = core_poll_irq;
  r->ramptr = core_ramptr;
}

void lsi11_poll_devices(void) {
  dl11_poll();
  kw11_poll();
  rk11_poll();
  lp11_poll();
}

void lsi11_set_trace_irq(int on) { trace_irq_flag = on ? 1 : 0; }
void lsi11_set_trace_nxm(int on) { trace_nxm_flag = on ? 1 : 0; }
