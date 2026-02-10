#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "../bus.h"
#include "../dev_rh11.h"
#include "../dev_rk11.h"
#include "../irq.h"

#define RHCS1 0177440
#define RHWC  0177442
#define RHBA  0177444
#define RHDA  0177446
#define RHCS2 0177450
#define RHER  0177454
#define RHDB  0177462

#define RKCS  0177404

#define RHCS1_GO        0000001
#define RHCS1_FUNC_MASK 0000076
#define RHCS1_IE        0000100
#define RHCS1_DONEB     0000200
#define RHCS1_BAEXT     0001400

#define RH11_FUNC_READ  0000020
#define RHER_NXM        0000002
#define RHCS2_NEM       0004000
#define RHCS2_NED       0010000

static int g_fail = 0;

static void check(int cond, const char *msg) {
  if (!cond) {
    fprintf(stderr, "FAIL: %s\n", msg);
    g_fail = 1;
  }
}

static int make_image(char *path, size_t bytes) {
  int fd;
  uint16_t w0 = 012345;
  uint16_t w1 = 006543;
  uint8_t b[4];

  fd = mkstemp(path);
  if (fd < 0)
    return -1;
  if (ftruncate(fd, (off_t)bytes) != 0) {
    close(fd);
    return -1;
  }

  b[0] = (uint8_t)(w0 & 000377);
  b[1] = (uint8_t)((w0 >> 8) & 000377);
  b[2] = (uint8_t)(w1 & 000377);
  b[3] = (uint8_t)((w1 >> 8) & 000377);
  if (pwrite(fd, b, sizeof(b), 0) != (ssize_t)sizeof(b)) {
    close(fd);
    return -1;
  }

  close(fd);
  return 0;
}

static int poll_irq_vec(regs *r, uint16_t *vec) { return irq_poll(r, vec); }

static void start_rh_read(uint16_t wc, uint16_t ba, uint16_t da, int ie) {
  uint8_t cs1 = (uint8_t)(RH11_FUNC_READ | RHCS1_GO);
  if (ie)
    cs1 |= RHCS1_IE;
  bus_write16(RHWC, wc);
  bus_write16(RHBA, ba);
  bus_write16(RHDA, da);
  bus_write8(RHCS1, cs1);
}

static void start_rh_read_ext(uint16_t wc, uint16_t ba, uint16_t da,
                              uint16_t cs1_ext, int ie) {
  uint16_t cs1 = (uint16_t)(RH11_FUNC_READ | RHCS1_GO | cs1_ext);
  if (ie)
    cs1 |= RHCS1_IE;
  bus_write16(RHWC, wc);
  bus_write16(RHBA, ba);
  bus_write16(RHDA, da);
  bus_write16(RHCS1, cs1);
}

int main(void) {
  char err[128];
  char img[] = "/tmp/rh11-img-XXXXXX";
  regs r;
  uint16_t vec = 0;

  memset(&r, 0, sizeof(r));
  r.model = K1801VM1;
  r.psw = 0;

  bus_reset_config();
  if (bus_configure(BUS_MACHINE_PDP1134, 4096, err, sizeof(err)) != 0) {
    fprintf(stderr, "FAIL: bus_configure: %s\n", err);
    return 1;
  }
  bus_init();

  if (rh11_init() != 0 || rk11_init() != 0) {
    fprintf(stderr, "FAIL: device init\n");
    return 1;
  }
  rh11_reset();
  rk11_reset();

  /* 1) decode and register accessibility on pdp1134 */
  check(bus_is_nxm(0170000) == 1,
        "pdp1134 low 16-bit I/O page hole is NXM when undecoded");
  check(bus_is_nxm(0200000) == 0, "pdp1134 RAM must exist at 0200000");
  bus_write16(0200000, 045612);
  check(bus_read16(0200000) == 045612, "pdp1134 RAM readback at 0200000");
  check(bus_is_nxm(RHCS1) == 0, "RH11 base must decode on pdp1134");
  check(bus_is_nxm(RHDB) == 0, "RH11 last register must decode on pdp1134");
  bus_write16(RHWC, 012345);
  bus_write16(RHBA, 000120);
  bus_write16(RHDA, 000777);
  check(bus_read16(RHWC) == 012345, "RHWC readback");
  check(bus_read16(RHBA) == 000120, "RHBA readback");
  check(bus_read16(RHDA) == 000777, "RHDA readback");

  if (make_image(img, 0100000) != 0) {
    fprintf(stderr, "FAIL: make image\n");
    return 1;
  }
  if (rh11_open_image(img) != 0) {
    fprintf(stderr, "FAIL: rh11_open_image\n");
    unlink(img);
    return 1;
  }

  /* 3) IRQ non-repeat while DONE==1 */
  rh11_reset();
  start_rh_read(0177777, 000200, 000000, 1);
  rh11_poll();
  check(bus_read16(000200) == 012345, "RH11 READ transfers first word");
  check(poll_irq_vec(&r, &vec) == 1, "RH11 first IRQ delivered");
  check((vec & 0000777) == 000210, "RH11 vector is 000210");
  vec = 0;
  check(poll_irq_vec(&r, &vec) == 0, "RH11 no repeat IRQ while DONE==1");
  start_rh_read(0177777, 000202, 000000, 1);
  rh11_poll();
  check(poll_irq_vec(&r, &vec) == 1, "RH11 IRQ after next GO");

  /* 4) RK11 and RH11 IRQ coexist independently */
  rh11_reset();
  rk11_reset();
  start_rh_read(0177777, 000204, 000000, 1);
  rh11_poll();
  check(poll_irq_vec(&r, &vec) == 1, "RH11 IRQ delivered in coexistence");
  check((vec & 0000777) == 000210, "RH11 coexistence vector");

  rk11_close_image(); /* force no-media completion path */
  bus_write8(RKCS, 000105); /* IE + READ + GO */
  rk11_poll();
  vec = 0;
  check(poll_irq_vec(&r, &vec) == 1, "RK11 IRQ delivered after RH11");
  check((vec & 0000777) == 000220, "RK11 coexistence vector");

  /* 5) DMA NXM must set error and complete cleanly */
  /* Reconfigure to small RAM so a 16-bit BA overflow can hit true NXM. */
  if (bus_configure(BUS_MACHINE_PDP1134, 64, err, sizeof(err)) != 0) {
    fprintf(stderr, "FAIL: bus_configure(64KB): %s\n", err);
    rh11_close_image();
    unlink(img);
    return 1;
  }
  bus_init();
  rh11_reset();
  start_rh_read(0177776, 0177776, 000000, 1);
  rh11_poll();
  check(((bus_read16(RHCS2) & RHCS2_NEM) != 0) ||
            ((bus_read16(RHER) & RHER_NXM) != 0),
        "RH11 sets NEM/NXM error");
  check((bus_read16(RHCS1) & RHCS1_GO) == 0, "RH11 clears GO on completion");
  check((bus_read16(RHCS1) & RHCS1_DONEB) != 0, "RH11 sets DONE on completion");
  vec = 0;
  check(poll_irq_vec(&r, &vec) == 1, "RH11 IRQ delivered for NXM completion");
  vec = 0;
  check(poll_irq_vec(&r, &vec) == 0, "RH11 no repeat after NXM completion");

  /* 6) Non-zero unit select should report nonexistent drive. */
  rh11_reset();
  bus_write16(RHCS2, 000001);
  start_rh_read(0177777, 000200, 000000, 0);
  rh11_poll();
  check((bus_read16(RHCS2) & RHCS2_NED) != 0,
        "RH11 sets NED for non-existent unit");

  /* 7) BA16/BA17 extension should participate in DMA addressing. */
  if (bus_configure(BUS_MACHINE_PDP1134, 4096, err, sizeof(err)) != 0) {
    fprintf(stderr, "FAIL: bus_configure(4096KB): %s\n", err);
    rh11_close_image();
    unlink(img);
    return 1;
  }
  bus_init();
  rh11_reset();
  start_rh_read_ext(0177776, 0177776, 000000, RHCS1_BAEXT, 0);
  rh11_poll();
  check(bus_read16(0777776) == 012345,
        "RH11 read uses BA16/BA17 extension (first word)");
  check(bus_read16(0000000) == 006543,
        "RH11 BA extension increments on RKBA overflow");

  rh11_close_image();
  unlink(img);

  if (!g_fail) {
    printf("PASS: test_rh11_pdp1134\n");
    return 0;
  }
  return 1;
}
