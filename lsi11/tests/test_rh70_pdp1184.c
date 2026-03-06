#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "../bus.h"
#include "../dev_kw11.h"
#include "../dev_rl11.h"
#include "../dev_rh11.h"
#include "../dev_rk11.h"
#include "../irq.h"
#include "../ubmap.h"

#define RHCS1 0177440
#define RHWC  0177442
#define RHBA  0177444
#define RHDA  0177446
#define RHCS2 0177450
#define RHER  0177454
#define RHBAE 0177474
#define RHCS3 0177476
#define UBM0L 0170200
#define UBM0H 0170202

#define RHCS1_GO        0000001
#define RHCS1_FUNC_MASK 0000076
#define RHCS1_IE        0000100
#define RHCS1_DONEB     0000200
#define RHCS1_BA16      0000400
#define RHCS1_BA17      0001000
#define RHCS1_BAEXT     (RHCS1_BA16 | RHCS1_BA17)
#define RHCS1_CCLR      0100000

#define RH11_FUNC_READ  0000020
#define RHER_NXM        0000002
#define RHCS2_NEM       0004000
#define RHCS2_PGE       0002000

static int g_fail = 0;

static void check(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        g_fail = 1;
    }
}

static int make_image(char *path, size_t bytes)
{
    int fd;
    uint16_t w0 = 012345;
    uint16_t w1 = 006543;
    uint8_t b[4];

    fd = mkstemp(path);
    if (fd < 0) {
        return -1;
    }
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

static int poll_irq_vec(regs *r, uint16_t *vec)
{
    return irq_poll(r, vec);
}

static void start_rh_read(uint16_t wc, uint16_t ba, uint16_t da, int ie)
{
    uint8_t cs1 = (uint8_t)(RH11_FUNC_READ | RHCS1_GO);
    if (ie) {
        cs1 |= RHCS1_IE;
    }
    bus_write16(RHWC, wc);
    bus_write16(RHBA, ba);
    bus_write16(RHDA, da);
    bus_write8(RHCS1, cs1);
}

int main(void)
{
    char err[128];
    char img[] = "/tmp/rh70-img-XXXXXX";
    regs r;
    uint16_t vec = 0;

    memset(&r, 0, sizeof(r));
    r.model = K1801VM1;
    r.psw = 0;

    bus_reset_config();
    if (bus_configure(BUS_MACHINE_PDP1184, 4096, err, sizeof(err)) != 0) {
        fprintf(stderr, "FAIL: bus_configure: %s\n", err);
        return 1;
    }
    bus_init();

    if (rh11_set_mode(RH11_MODE_RH70) != 0) {
        fprintf(stderr, "FAIL: rh11_set_mode(RH70)\n");
        return 1;
    }
    if (rh11_init() != 0 || rk11_init() != 0 || rl11_init() != 0 ||
            ubmap_init() != 0 ||
            kw11_init() != 0) {
        fprintf(stderr, "FAIL: device init\n");
        return 1;
    }
    rh11_reset();
    rk11_reset();
    rl11_reset();
    kw11_reset();
    bus_set_pdp1184_io_16bit(1);
    bus_set_pdp1184_m22e(1);

    /* 1) RH70 CSR decode + BAE/CS1 UAE coupling. */
    check(bus_is_nxm(RHCS1) == 0, "RH70 CS1 must decode");
    check(bus_is_nxm(RHCS3) == 0, "RH70 CS3 must decode");
    check(bus_is_nxm(RHBAE) == 0, "RH70 BAE must decode");
    bus_write16(RHBAE, 0000045);
    check((bus_read16(RHBAE) & 0000077) == 0000045,
          "RH70 BAE keeps low 6 bits");
    check((bus_read16(RHCS1) & RHCS1_BAEXT) == RHCS1_BA16,
          "RH70 CS1 BA16/BA17 mirror BAE<1:0>");
    bus_write8((uint16_t)(RHCS1 + 1), 0000002);
    check((bus_read16(RHBAE) & 0000077) == 0000046,
          "RH70 CS1 high-byte write updates BAE<1:0> only");

    /* 1b) RH70 CS3 IE mirror path. */
    bus_write16(RHCS3, RHCS1_IE);
    check((bus_read16(RHCS1) & RHCS1_IE) != 0,
          "RH70 CS3 IE write mirrors into CS1");
    vec = 0;
    check(poll_irq_vec(&r, &vec) == 0,
          "RH70 CS3 IE write does not generate immediate IRQ");
    bus_write16(RHCS3, 0);
    check((bus_read16(RHCS1) & RHCS1_IE) == 0,
          "RH70 CS3 IE clear mirrors into CS1");

    /* RH/RK-compatible rule: CCLR and IE-only do not spuriously re-IRQ. */
    rh11_reset();
    vec = 0;
    bus_write8((uint16_t)(RHCS1 + 1), 0000200);
    check(poll_irq_vec(&r, &vec) == 0, "RH70 CCLR alone does not IRQ");
    bus_write8(RHCS1, RHCS1_IE);
    check(poll_irq_vec(&r, &vec) == 0, "RH70 IE-only write after CCLR does not IRQ");

    /* CCLR while IE=1 must clear IE/IRQ state. */
    bus_write8((uint16_t)(RHCS1 + 1), 0000200);
    check((bus_read16(RHCS1) & RHCS1_IE) == 0,
          "RH70 CCLR clears IE");
    vec = 0;
    check(poll_irq_vec(&r, &vec) == 0, "RH70 CCLR with IE set does not IRQ");

    if (make_image(img, 0100000) != 0) {
        fprintf(stderr, "FAIL: make image\n");
        return 1;
    }
    if (rh11_open_image(img) != 0) {
        fprintf(stderr, "FAIL: rh11_open_image\n");
        unlink(img);
        return 1;
    }

    /* 2) DMA must work above 18-bit range in RH70 mode. */
    rh11_reset();
    bus_write16(RHBAE, 0000004);
    start_rh_read(0177776, 0000200, 000000, 0);
    rh11_poll();
    check(bus_read16(01000200) == 012345,
          "RH70 READ reaches >18-bit physical address (first word)");
    check(bus_read16(01000202) == 006543,
          "RH70 READ reaches >18-bit physical address (second word)");

    /* 3) RH70 compatibility: with BAE<5:2>=0 and UBMAP enabled, map via UBA. */
    rh11_reset();
    ubmap_reset();
    ubmap_set_enabled(1);
    bus_write16(UBM0L, 0);
    bus_write8(UBM0H, 0000004); /* map UBA page 0 -> physical 01000000 */
    bus_write16(0000200, 0);
    bus_write16(0000202, 0);
    start_rh_read(0177776, 0000200, 000000, 0);
    rh11_poll();
    check(bus_read16(01000200) == 012345,
          "RH70 UBMAP compat maps legacy HK DMA (first word)");
    check(bus_read16(01000202) == 006543,
          "RH70 UBMAP compat maps legacy HK DMA (second word)");
    check(bus_read16(0000200) == 0,
          "RH70 UBMAP compat does not write unmapped low RAM");
    ubmap_set_enabled(0);

    /* 4) BA overflow must carry into BAE in RH70 mode. */
    rh11_reset();
    bus_write16(RHBAE, 0000004);
    start_rh_read(0177776, 0177776, 000000, 0);
    rh11_poll();
    check(bus_read16(01177776) == 012345,
          "RH70 BA overflow test first word written");
    check(bus_read16(01200000) == 006543,
          "RH70 BA overflow carries into BAE");
    check(bus_read16(RHBA) == 0000002, "RH70 BA advances after two-word transfer");
    check((bus_read16(RHBAE) & 0000077) == 0000005,
          "RH70 BAE increments on BA overflow");
    check((bus_read16(RHCS1) & RHCS1_BAEXT) == RHCS1_BA16,
          "RH70 CS1 BA extension mirrors updated BAE<1:0>");

    /* GO while busy should raise PGE and not complete second command. */
    rh11_reset();
    bus_write16(RHBAE, 0);
    bus_write16(RHWC, 0177777);
    bus_write16(RHBA, 0000400);
    bus_write16(RHDA, 000000);
    bus_write8(RHCS1, (uint8_t)(RH11_FUNC_READ | RHCS1_GO | RHCS1_IE));
    bus_write8(RHCS1, (uint8_t)(RH11_FUNC_READ | RHCS1_GO | RHCS1_IE));
    check((bus_read16(RHCS2) & RHCS2_PGE) != 0,
          "RH70 GO while busy sets PGE");
    check((bus_read16(RHCS1) & RHCS1_GO) == 0,
          "RH70 busy GO attempt completes command (GO cleared)");
    check((bus_read16(RHCS1) & RHCS1_DONEB) != 0,
          "RH70 busy GO attempt sets DONE");

    /* 5) IRQ must remain single-shot while DONE=1. */
    rh11_reset();
    bus_write16(RHBAE, 0);
    start_rh_read(0177777, 0000400, 000000, 1);
    rh11_poll();
    check(poll_irq_vec(&r, &vec) == 1, "RH70 first IRQ delivered");
    check((vec & 0000777) == 000210, "RH70 vector is 000210");
    vec = 0;
    check(poll_irq_vec(&r, &vec) == 0, "RH70 no repeat IRQ while DONE==1");
    start_rh_read(0177777, 0000402, 000000, 1);
    rh11_poll();
    check(poll_irq_vec(&r, &vec) == 1, "RH70 IRQ re-arms after next GO");

    /* 6) NXM in RH70 path sets error and completes with DONE. */
    if (bus_configure(BUS_MACHINE_PDP1184, 64, err, sizeof(err)) != 0) {
        fprintf(stderr, "FAIL: bus_configure(64KB): %s\n", err);
        rh11_close_image();
        unlink(img);
        return 1;
    }
    bus_init();
    bus_set_pdp1184_io_16bit(1);
    bus_set_pdp1184_m22e(1);
    rh11_reset();
    bus_write16(RHBAE, 0000004);
    start_rh_read(0177777, 0000200, 000000, 1);
    rh11_poll();
    check(((bus_read16(RHCS2) & RHCS2_NEM) != 0) ||
          ((bus_read16(RHER) & RHER_NXM) != 0),
          "RH70 sets NEM/NXM on invalid DMA address");
    check((bus_read16(RHCS1) & RHCS1_GO) == 0, "RH70 clears GO on completion");
    check((bus_read16(RHCS1) & RHCS1_DONEB) != 0, "RH70 sets DONE on completion");
    vec = 0;
    check(poll_irq_vec(&r, &vec) == 1, "RH70 IRQ delivered for NXM completion");
    vec = 0;
    check(poll_irq_vec(&r, &vec) == 0, "RH70 no repeat IRQ after NXM completion");

    rh11_close_image();
    unlink(img);

    if (!g_fail) {
        printf("PASS: test_rh70_pdp1184\n");
        return 0;
    }
    return 1;
}
