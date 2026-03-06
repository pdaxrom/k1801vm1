#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "../bus.h"
#include "../dev_kw11.h"
#include "../dev_rh11.h"
#include "../dev_rk11.h"
#include "../dev_rl11.h"
#include "../dev_xp.h"
#include "../irq.h"

#define XPCS1  0176700
#define XPWC   0176702
#define XPBA   0176704
#define XPDA   0176706
#define XPCS2  0176710
#define XPER1  0176714
#define XPDC   0176734
#define XPBAE  0176750
#define XPCS3  0176752

#define XPCS1_GO      0000001
#define XPCS1_IE      0000100
#define XPCS1_DONE    0000200
#define XPCS1_FNC_RD  0000070 /* FNC_READ (034) << 1 */

#define XPCS2_NEM     0004000
#define XPER1_AOE     0001000

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

static void start_xp_read(uint16_t wc, uint16_t ba, uint16_t dc, uint16_t da, int ie)
{
    uint8_t cs1 = (uint8_t)(XPCS1_FNC_RD | XPCS1_GO);
    if (ie) {
        cs1 |= XPCS1_IE;
    }
    bus_write16(XPWC, wc);
    bus_write16(XPBA, ba);
    bus_write16(XPDC, dc);
    bus_write16(XPDA, da);
    /* Word write is split into two byte writes on bus; controller must not raise PGE. */
    bus_write16(XPCS1, cs1);
}

int main(void)
{
    char err[128];
    char img[] = "/tmp/xp-rm05-img-XXXXXX";
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
    bus_set_pdp1184_io_16bit(1);
    bus_set_pdp1184_m22e(1);

    if (xp_init() != 0 || rh11_init() != 0 || rk11_init() != 0 || rl11_init() != 0 ||
            kw11_init() != 0) {
        fprintf(stderr, "FAIL: device init\n");
        return 1;
    }
    xp_reset();
    rh11_reset();
    rk11_reset();
    rl11_reset();
    kw11_reset();

    check(bus_is_nxm(XPCS1) == 0, "XP CS1 must decode");
    check(bus_is_nxm(XPCS3) == 0, "XP CS3 must decode");

    if (make_image(img, 0100000) != 0) {
        fprintf(stderr, "FAIL: make image\n");
        return 1;
    }
    if (xp_open_image(img) != 0) {
        fprintf(stderr, "FAIL: xp_open_image\n");
        unlink(img);
        return 1;
    }

    bus_write16(XPCS2, 0); /* select unit 0 */
    start_xp_read(0177776, 0000200, 0, 0, 1);
    xp_poll();

    check(bus_read16(0000200) == 012345, "XP READ transfers first word");
    check(bus_read16(0000202) == 006543, "XP READ transfers second word");
    check(bus_read16(XPBA) == 0000204, "XP BA increments by two words");
    check(bus_read16(XPWC) == 0000000, "XP WC reaches zero after two words");
    check((bus_read16(XPCS1) & XPCS1_GO) == 0, "XP clears GO on completion");
    check((bus_read16(XPCS1) & XPCS1_DONE) != 0, "XP sets DONE on completion");

    vec = 0;
    check(poll_irq_vec(&r, &vec) == 1, "XP first IRQ delivered");
    check((vec & 0000777) == 000254, "XP vector is 000254");
    vec = 0;
    check(poll_irq_vec(&r, &vec) == 0, "XP no repeat IRQ while DONE==1");

    /* NXM/error path (address outside configured RAM). */
    if (bus_configure(BUS_MACHINE_PDP1184, 64, err, sizeof(err)) != 0) {
        fprintf(stderr, "FAIL: bus_configure(64KB): %s\n", err);
        xp_close_image();
        unlink(img);
        return 1;
    }
    bus_init();
    bus_set_pdp1184_io_16bit(1);
    bus_set_pdp1184_m22e(1);
    xp_reset();
    bus_write16(XPCS2, 0);
    bus_write16(XPBAE, 0000004);
    start_xp_read(0177777, 0000200, 0, 0, 1);
    xp_poll();
    check(((bus_read16(XPCS2) & XPCS2_NEM) != 0) ||
          ((bus_read16(XPER1) & XPER1_AOE) != 0),
          "XP sets NEM/AOE on invalid DMA path");
    check((bus_read16(XPCS1) & XPCS1_DONE) != 0, "XP sets DONE on NXM completion");

    xp_close_image();
    unlink(img);

    if (!g_fail) {
        printf("PASS: test_xp_pdp1184\n");
        return 0;
    }
    return 1;
}
