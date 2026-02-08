#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../bus.h"
#include "../devio.h"
#include "../irq.h"

#include "../dev_dl11.h"
#include "../dev_kw11.h"
#include "../dev_lp11.h"
#include "../dev_rk11.h"
#include "../dev_sr.h"

/* test helper from dev_dl11.c (built with LSI11_TESTS) */
void dl11_test_inject_rx(uint8_t ch);

static int g_fail = 0;
static void check(int cond, const char *msg) {
  if (!cond) {
    fprintf(stderr, "FAIL: %s\n", msg);
    g_fail = 1;
  }
}

static void reset_devices(void) {
  dl11_reset();
  kw11_reset();
  rk11_reset();
  lp11_reset();
  sr_reset();
}

/* Helper: poll and return 1 if IRQ delivered */
static int poll_irq(regs *r, uint16_t *vec) { return irq_poll((void *)r, vec); }

/* --- Device-specific helpers (OCTAL addresses) --- */

/* DL11: primary base 0177560 */
#define DL11_RCSR 0177560
#define DL11_RBUF 0177562
#define DL11_TCSR 0177564
#define DL11_TBUF 0177566

/* KW11 */
#define KW11_CSR 0177546

/* LP11 */
#define LP11_CSR 0177514
#define LP11_DBR 0177516

/* RK11 */
#define RKCS 0177404

static uint8_t rd8(uint16_t a) { return bus_read8(a); }
static void wr8(uint16_t a, uint8_t v) { bus_write8(a, v); }

static void test_dl11_tx(regs *r) {
  uint16_t vec = 0;

  reset_devices();

  /* enable TX interrupts: write IE bit (0100) into TCSR */
  wr8(DL11_TCSR, 000100);

  /* clear DONE by writing TBUF (software clears DONE) */
  wr8(DL11_TBUF, 'A');

  /* Allow TX to complete */
  dl11_poll();

  /* TX device model sets DONE=1 after completion; if IE=1, should assert IRQ */
  check(poll_irq(r, &vec) == 1, "DL11 TX: first IRQ delivered");
  check(vec == 000064,
        "DL11 TX: vector is 000064 (octal)"); /* unless DCJ11 encoding changes
                                                 it */

  /* While DONE==1 and IE==1, IRQ is level and should repeat */
  vec = 0;
  check(poll_irq(r, &vec) == 1, "DL11 TX: repeat IRQ while DONE==1");

  /* Now clear DONE again by writing TBUF -> should allow a NEW IRQ */
  wr8(DL11_TBUF, 'B');
  dl11_poll();
  vec = 0;
  check(poll_irq(r, &vec) == 1,
        "DL11 TX: new IRQ after software clears DONE and event happens");

  /* cleanup */
  wr8(DL11_TCSR, 000000);
}

static void test_dl11_rx(regs *r) {
  uint16_t vec = 0;

  reset_devices();

  /* enable RX interrupts: write IE bit (0100) into RCSR */
  wr8(DL11_RCSR, 000100);

  /* Inject one received char => should assert IRQ once */
  dl11_test_inject_rx('R');

  check(poll_irq(r, &vec) == 1, "DL11 RX: first IRQ delivered");
  check((vec & 0000777) == 000060, "DL11 RX: vector 000060");

  /* While DONE==1 and IE==1, IRQ is level and should repeat */
  vec = 0;
  check(poll_irq(r, &vec) == 1, "DL11 RX: repeat IRQ while DONE==1");

  /* Software clears DONE: reading RBUF */
  (void)rd8(DL11_RBUF);

  /* Still no IRQ just from clearing DONE */
  vec = 0;
  check(poll_irq(r, &vec) == 0, "DL11 RX: clearing DONE alone does not IRQ");

  /* Inject again => new IRQ */
  dl11_test_inject_rx('S');
  vec = 0;
  check(poll_irq(r, &vec) == 1, "DL11 RX: new IRQ after next event");

  /* cleanup */
  (void)rd8(DL11_RBUF);
  wr8(DL11_RCSR, 000000);
}

static void test_kw11(regs *r) {
  uint16_t vec = 0;

  reset_devices();

  /* enable IE */
  wr8(KW11_CSR, 000100);

  /* Wait/poll until an IRQ arrives (kw11_poll uses host time).
     Give it some tries; each loop calls kw11_poll then polls IRQ. */
  int got = 0;
  for (int i = 0; i < 5000; i++) {
    kw11_poll();
    if (poll_irq(r, &vec)) {
      got = 1;
      break;
    }
    usleep(100);
  }
  check(got, "KW11: got an IRQ at 50 Hz");
  /* vector check (if no DCJ11 encoding): */
  check((vec & 0000777) == 000100, "KW11: vector low bits 000100");

  /* While DONE==1 and IE==1, IRQ is level and should repeat */
  vec = 0;
  got = 0;
  for (int i = 0; i < 2000; i++) {
    kw11_poll();
    if (poll_irq(r, &vec)) {
      got = 1;
      break;
    }
    usleep(100);
  }
  check(got, "KW11: repeat IRQ while DONE==1");

  /* Software clears DONE: write CSR low byte */
  wr8(KW11_CSR, 000100);

  /* After DONE cleared, next tick should generate another IRQ */
  vec = 0;
  got = 0;
  for (int i = 0; i < 5000; i++) {
    kw11_poll();
    if (poll_irq(r, &vec)) {
      got = 1;
      break;
    }
    usleep(100);
  }
  check(got, "KW11: new IRQ after clearing DONE");
}

static void test_lp11(regs *r) {
  uint16_t vec = 0;

  reset_devices();

  /* enable IE */
  wr8(LP11_CSR, 000100);

  /* software clears DONE by writing DBR; device then sets DONE=1 again (event)
   */
  wr8(LP11_DBR, 'X');

  check(poll_irq(r, &vec) == 1, "LP11: IRQ delivered");
  check((vec & 0000777) == 000200, "LP11: vector 000200");

  vec = 0;
  check(poll_irq(r, &vec) == 1, "LP11: repeat IRQ while DONE==1");

  /* write DBR again => clears DONE and generates new DONE event => new IRQ */
  wr8(LP11_DBR, 'Y');
  vec = 0;
  check(poll_irq(r, &vec) == 1, "LP11: new IRQ after DBR write");
}

static void test_rk11(regs *r) {
  uint16_t vec = 0;

  reset_devices();

  /* enable IE */
  wr8(RKCS, 000100);

  /* start new GO command (software clears DONE): write GO+READ+IE low byte
     GO=000001, READ function=000004, IE=000100 => 000105 */
  wr8(RKCS, 000105);

  /* Poll controller to complete command (will error if no image, but still
   * should set DONE once) */
  rk11_poll();

  /* IRQ should deliver once (if IE=1) */
  check(poll_irq(r, &vec) == 1, "RK11: IRQ delivered on completion");
  check((vec & 0000777) == 000220, "RK11: vector 000220");

  /* Repeat while DONE==1 */
  vec = 0;
  check(poll_irq(r, &vec) == 1, "RK11: repeat IRQ while DONE==1");

  /* Start next GO command (software clears DONE) => allow a new completion IRQ
   */
  wr8(RKCS, 000105);
  rk11_poll();
  vec = 0;
  check(poll_irq(r, &vec) == 1, "RK11: new IRQ after next GO");
}

int main(void) {
  regs r;
  memset(&r, 0, sizeof(r));

  bus_init();

  /* init devices (register IO + irq sources) */
  if (dl11_init() != 0) {
    fprintf(stderr, "FAIL: dl11_init\n");
    return 1;
  }
  if (kw11_init() != 0) {
    fprintf(stderr, "FAIL: kw11_init\n");
    return 1;
  }
  if (rk11_init() != 0) {
    fprintf(stderr, "FAIL: rk11_init\n");
    return 1;
  }
  if (lp11_init() != 0) {
    fprintf(stderr, "FAIL: lp11_init\n");
    return 1;
  }
  if (sr_init() != 0) {
    fprintf(stderr, "FAIL: sr_init\n");
    return 1;
  }

  test_dl11_tx(&r);
  test_dl11_rx(&r);
  test_kw11(&r);
  test_lp11(&r);
  test_rk11(&r);

  if (!g_fail) {
    printf("PASS: test_irq_semantics\n");
    return 0;
  }
  return 1;
}
