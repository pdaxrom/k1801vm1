#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "../bus.h"
#include "../dev_rl11.h"
#include "../irq.h"

#define RLCS 0174400
#define RLBA 0174402
#define RLDA 0174404
#define RLMP 0174406
#define RLBAE 0174410

#define RLCS_DRDY      0000001
#define RLCS_FUNC_MASK 0000016
#define RLCS_BA_MASK   0000060
#define RLCS_IE        0000100
#define RLCS_CRDY      0000200
#define RLCS_DS_MASK   0001400
#define RLCS_E_MASK    0036000

#define RLCS_FN_WCHK  0000002
#define RLCS_FN_GSTAT 0000004
#define RLCS_FN_WRITE 0000012
#define RLCS_FN_READ  0000014

#define RLCS_E_SHIFT 10
#define RLCS_E_OPI   0001
#define RLCS_E_NXM   0010

#define RLMP_DT 0000200

#define RL_SECTORS_PER_TRACK 0000050
#define RL_HEADS_PER_CYL     0000002
#define RL_BYTES_PER_SECTOR  0000400
#define RL01_CYLINDERS       0000400
#define RL02_CYLINDERS       0001000

static int g_fail = 0;

static void check(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        g_fail = 1;
    }
}

static size_t rl_bytes_for_type(int type)
{
    size_t cyl = (type == RL11_TYPE_RL02) ? RL02_CYLINDERS : RL01_CYLINDERS;
    return cyl * RL_HEADS_PER_CYL * RL_SECTORS_PER_TRACK * RL_BYTES_PER_SECTOR;
}

static int make_image(char *path, size_t bytes, uint16_t w0, uint16_t w1)
{
    int fd = mkstemp(path);
    uint8_t b[4];

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

static void start_rl_cmd(uint16_t mp, uint16_t ba, uint16_t da, uint8_t cs_low,
                         uint8_t cs_high)
{
    bus_write16(RLMP, mp);
    bus_write16(RLBA, ba);
    bus_write16(RLDA, da);
    bus_write8((uint16_t)(RLCS + 1), cs_high);
    bus_write8(RLCS, cs_low);
}

static void wait_rl_done(void)
{
    int i;

    for (i = 0; i < 50; i++) {
        rl11_poll();
        if ((bus_read16(RLCS) & RLCS_CRDY) != 0) {
            return;
        }
        usleep(1000);
    }
}

int main(void)
{
    char err[128];
    char rl01_img[] = "/tmp/rl11-rl01-XXXXXX";
    char rl02_img[] = "/tmp/rl11-rl02-XXXXXX";
    regs r;
    uint16_t vec = 0;
    uint16_t cs = 0;
    uint16_t mp = 0;
    uint16_t w = 0;
    int fd;
    uint8_t b[4];

    memset(&r, 0, sizeof(r));
    r.model = K1801VM2;
    r.psw = 0;

    bus_reset_config();
    if (bus_configure(BUS_MACHINE_LSI11_1104, 0, err, sizeof(err)) != 0) {
        fprintf(stderr, "FAIL: bus_configure: %s\n", err);
        return 1;
    }
    bus_init();

    if (rl11_init() != 0) {
        fprintf(stderr, "FAIL: rl11_init\n");
        return 1;
    }
    rl11_reset();

    if (make_image(rl01_img, rl_bytes_for_type(RL11_TYPE_RL01), 012345, 006543) !=
            0) {
        fprintf(stderr, "FAIL: make rl01 image\n");
        return 1;
    }
    if (rl11_open_image_typed(rl01_img, RL11_TYPE_AUTO) != 0) {
        fprintf(stderr, "FAIL: rl11_open_image rl01\n");
        unlink(rl01_img);
        return 1;
    }

    cs = bus_read16(RLCS);
    check((cs & RLCS_DRDY) != 0, "RL11 DRDY set when image attached");
    check((cs & RLCS_CRDY) != 0, "RL11 CRDY set after reset");

    /* READ two words from sector 0 into memory. */
    start_rl_cmd(0177776, 000200, 0000000, RLCS_FN_READ | RLCS_IE, 0);
    wait_rl_done();
    check(bus_read16(000200) == 012345, "RL11 READ word0");
    check(bus_read16(000202) == 006543, "RL11 READ word1");
    check((bus_read16(RLMP) & 017777) == 000000, "RL11 WC reaches zero");
    check(irq_poll(&r, &vec) == 1, "RL11 IRQ after READ");
    check((vec & 0000777) == 000160, "RL11 vector is 000160");
    vec = 0;
    check(irq_poll(&r, &vec) == 0, "RL11 IRQ does not repeat while DONE==1");

    /* WRITE two words into sector 1 and verify file contents. */
    bus_write16(000204, 011111);
    bus_write16(000206, 022222);
    start_rl_cmd(0177776, 000204, 0000001, RLCS_FN_WRITE, 0);
    wait_rl_done();

    fd = open(rl01_img, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "FAIL: open rl01 image\n");
        rl11_close_image();
        unlink(rl01_img);
        return 1;
    }
    if (pread(fd, b, sizeof(b), RL_BYTES_PER_SECTOR) != (ssize_t)sizeof(b)) {
        fprintf(stderr, "FAIL: pread rl01 image\n");
        close(fd);
        rl11_close_image();
        unlink(rl01_img);
        return 1;
    }
    close(fd);
    w = (uint16_t)(b[0] | ((uint16_t)b[1] << 8));
    check(w == 011111, "RL11 WRITE word0");
    w = (uint16_t)(b[2] | ((uint16_t)b[3] << 8));
    check(w == 022222, "RL11 WRITE word1");

    /* GET STATUS on RL01: DT bit must be 0. */
    start_rl_cmd(0000000, 000000, 0000003, RLCS_FN_GSTAT, 0);
    wait_rl_done();
    mp = bus_read16(RLMP);
    check((mp & RLMP_DT) == 0, "RL11 GET STATUS: RL01 DT bit clear");

    rl11_close_image();

    if (make_image(rl02_img, rl_bytes_for_type(RL11_TYPE_RL02), 001234, 005432) !=
            0) {
        fprintf(stderr, "FAIL: make rl02 image\n");
        unlink(rl01_img);
        return 1;
    }
    if (rl11_open_image_typed(rl02_img, RL11_TYPE_AUTO) != 0) {
        fprintf(stderr, "FAIL: rl11_open_image rl02\n");
        unlink(rl01_img);
        unlink(rl02_img);
        return 1;
    }

    /* GET STATUS on RL02: DT bit must be 1. */
    start_rl_cmd(0000000, 000000, 0000003, RLCS_FN_GSTAT, 0);
    wait_rl_done();
    mp = bus_read16(RLMP);
    check((mp & RLMP_DT) != 0, "RL11 GET STATUS: RL02 DT bit set");

    /* NXM: transfer 2 words from last aligned word of RAM into I/O page. */
    start_rl_cmd(0177776, 0157776, 0000000, RLCS_FN_READ | RLCS_IE, 0);
    wait_rl_done();
    cs = bus_read16(RLCS);
    check(((cs & RLCS_E_MASK) >> RLCS_E_SHIFT) == RLCS_E_NXM,
          "RL11 sets NXM error code");
    check((cs & RLCS_CRDY) != 0, "RL11 sets CRDY on NXM completion");
    vec = 0;
    check(irq_poll(&r, &vec) == 1, "RL11 IRQ on NXM completion");
    vec = 0;
    check(irq_poll(&r, &vec) == 0, "RL11 no repeat IRQ after NXM completion");

    /* WRITE CHECK mismatch should set DCRC/WCE error code. */
    bus_write16(000210, 077777);
    start_rl_cmd(0177777, 000210, 0000000, RLCS_FN_WCHK, 0);
    wait_rl_done();
    cs = bus_read16(RLCS);
    check(((cs & RLCS_E_MASK) >> RLCS_E_SHIFT) == 0002,
          "RL11 WRITE CHECK sets DCRC/WCE code");

    /* RL11 has no implicit track crossing: overflow to sector 050 -> OPI. */
    start_rl_cmd(0177400, 000400, 0000047, RLCS_FN_READ, 0);
    wait_rl_done();
    cs = bus_read16(RLCS);
    check(((cs & RLCS_E_MASK) >> RLCS_E_SHIFT) == RLCS_E_OPI,
          "RL11 sector overflow sets OPI");
    check((bus_read16(RLDA) & 0000077) == 0000050,
          "RL11 DA advances to illegal sector 050 on overflow");

    bus_write16(RLBAE, 0000045);
    check((bus_read16(RLBAE) & 0000077) == 0000045,
          "RL11 RLBAE stores 6-bit extension");
    cs = bus_read16(RLCS);
    check((cs & RLCS_BA_MASK) == 0000020,
          "RL11 RLCS mirrors low 2 bits of RLBAE");

    bus_write8(RLCS, (uint8_t)((RLCS_CRDY | 0000040) & 0000377));
    check((bus_read16(RLBAE) & 0000003) == 0000002,
          "RL11 RLCS BA bits update low 2 bits of RLBAE");

    rl11_close_image();
    unlink(rl01_img);
    unlink(rl02_img);

    if (!g_fail) {
        printf("PASS: test_rl11\n");
        return 0;
    }
    return 1;
}
