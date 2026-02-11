#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "../bus.h"
#include "../dev_rk11.h"
#include "../irq.h"

#define RKDS 0177400
#define RKER 0177402
#define RKCS 0177404
#define RKWC 0177406
#define RKBA 0177410
#define RKDA 0177412

#define RKCS_GO        0000001
#define RKCS_FUNC_MASK 0000016
#define RKCS_MEX_MASK  0000060
#define RKCS_IE        0000100
#define RKCS_RDY       0000200

#define RKCS_FN_CTLRESET 0000000
#define RKCS_FN_READ     0000004

#define RKER_NXS 0000040
#define RKER_NXC 0000100
#define RKER_NXD 0000200
#define RKER_NXM 0002000
#define RKER_HARD_MASK 0177740

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

static void start_read(uint16_t wc, uint16_t ba, uint16_t da, uint8_t cs_low)
{
    bus_write16(RKWC, wc);
    bus_write16(RKBA, ba);
    bus_write16(RKDA, da);
    bus_write8(RKCS, cs_low);
}

int main(void)
{
    char err[128];
    char img[] = "/tmp/rk11-img-XXXXXX";
    uint16_t v0, v1;
    regs r;
    uint16_t vec = 0;

    memset(&r, 0, sizeof(r));
    r.model = K1801VM2;
    r.psw = 0;

    bus_reset_config();
    if (bus_configure(BUS_MACHINE_LSI11_1104, 0, err, sizeof(err)) != 0) {
        fprintf(stderr, "FAIL: bus_configure: %s\n", err);
        return 1;
    }
    bus_init();

    if (rk11_init() != 0) {
        fprintf(stderr, "FAIL: rk11_init\n");
        return 1;
    }
    rk11_reset();

    if (make_image(img, 0100000) != 0) {
        fprintf(stderr, "FAIL: make image\n");
        return 1;
    }
    if (rk11_open_image(img) != 0) {
        fprintf(stderr, "FAIL: rk11_open_image\n");
        unlink(img);
        return 1;
    }

    /* RKDS/RKER are read-only. */
    v0 = bus_read16(RKDS);
    v1 = bus_read16(RKER);
    bus_write16(RKDS, 000000);
    bus_write16(RKER, 000000);
    check(bus_read16(RKDS) == v0, "RKDS is read-only");
    check(bus_read16(RKER) == v1, "RKER is read-only");

    /* RKDA writes must be ignored while controller busy. */
    rk11_reset();
    bus_write16(RKDA, 000001);
    start_read(0177777, 000200, 000001, RKCS_FN_READ | RKCS_GO);
    check((bus_read16(RKCS) & RKCS_RDY) == 0, "GO clears RDY while command active");
    bus_write16(RKDA, 000123);
    check(bus_read16(RKDA) == 000001, "RKDA write ignored while busy");
    rk11_poll();
    check((bus_read16(RKCS) & RKCS_RDY) != 0, "RDY set on completion");

    /* Address decode errors: NXD, NXC, NXS. */
    rk11_reset();
    start_read(0177777, 000200, 0020000, RKCS_FN_READ | RKCS_GO);
    rk11_poll();
    check((bus_read16(RKER) & RKER_NXD) != 0, "RK11 sets NXD for nonexistent drive");

    bus_write8(RKCS, RKCS_FN_CTLRESET | RKCS_GO);
    rk11_poll();
    check((bus_read16(RKER) & RKER_HARD_MASK) == 0,
          "Control Reset clears hard error bits");

    start_read(0177777, 000200, 0015460, RKCS_FN_READ | RKCS_GO);
    rk11_poll();
    check((bus_read16(RKER) & RKER_NXC) != 0, "RK11 sets NXC for bad cylinder");

    bus_write8(RKCS, RKCS_FN_CTLRESET | RKCS_GO);
    rk11_poll();
    start_read(0177777, 000200, 0000014, RKCS_FN_READ | RKCS_GO);
    rk11_poll();
    check((bus_read16(RKER) & RKER_NXS) != 0, "RK11 sets NXS for bad sector");

    /* NXM during DMA must set error, complete, and IRQ once if IE=1. */
    bus_write8(RKCS, RKCS_FN_CTLRESET | RKCS_GO);
    rk11_poll();
    start_read(0177777, 0157777, 0000000, RKCS_FN_READ | RKCS_IE | RKCS_GO);
    rk11_poll();
    check((bus_read16(RKER) & RKER_NXM) != 0, "RK11 sets NXM on invalid DMA");
    check((bus_read16(RKCS) & RKCS_GO) == 0, "RK11 clears GO on completion");
    check((bus_read16(RKCS) & RKCS_RDY) != 0, "RK11 sets RDY on completion");
    check(irq_poll(&r, &vec) == 1, "RK11 delivers IRQ once on completion");
    vec = 0;
    check(irq_poll(&r, &vec) == 0, "RK11 IRQ does not repeat while DONE=1");

    /* MEX bits are writable and increment on RKBA overflow. */
    if (bus_configure(BUS_MACHINE_PDP1184, 4096, err, sizeof(err)) != 0) {
        fprintf(stderr, "FAIL: bus_configure pdp1184: %s\n", err);
        rk11_close_image();
        unlink(img);
        return 1;
    }
    bus_init();
    rk11_reset();
    start_read(0177776, 0177776, 0000000, RKCS_FN_READ | RKCS_GO | RKCS_MEX_MASK);
    rk11_poll();
    check(bus_read16(RKBA) == 0000002, "RKBA wraps after overflow");
    check((bus_read16(RKCS) & RKCS_MEX_MASK) == 0000000, "MEX increments on RKBA overflow");
    check(bus_read16((paddr_t)0777776) == 012345, "MEX transfer writes high-memory word");
    check(bus_read16((paddr_t)0000000) == 006543, "MEX overflow transfer continues at next bank");

    rk11_close_image();
    unlink(img);

    if (!g_fail) {
        printf("PASS: test_rk11_regs\n");
        return 0;
    }
    return 1;
}
