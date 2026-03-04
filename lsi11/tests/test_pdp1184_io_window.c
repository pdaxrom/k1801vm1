#include <stdio.h>
#include <stdint.h>

#include "../bus.h"
#include "../dev_sr.h"

static int g_fail = 0;

static void check(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        g_fail = 1;
    }
}

int main(void)
{
    char err[128];

    bus_reset_config();
    if (bus_configure(BUS_MACHINE_PDP1184, 4096, err, sizeof(err)) != 0) {
        fprintf(stderr, "FAIL: bus_configure: %s\n", err);
        return 1;
    }
    bus_init();
    if (sr_init() != 0) {
        fprintf(stderr, "FAIL: sr_init\n");
        return 1;
    }

    sr_set(012345);

    /* 16-bit mode: peripheral window at 0160000..0177777. */
    bus_set_pdp1184_io_16bit(1);
    bus_set_pdp1184_m22e(0);
    check(bus_read16(0177570) == 012345,
          "16-bit I/O window must decode SR at 0177570");
    check(bus_read16(0777570) == 012345,
          "18-bit I/O alias must decode SR at 0777570");
    check(!bus_addr_is_ram(0172540),
          "16-bit mode must treat undecoded low I/O page addresses as non-RAM");
    check(bus_is_nxm(0172540),
          "16-bit mode undecoded low I/O page address must raise NXM");
    check(!bus_addr_is_ram(0760000),
          "16-bit mode must reserve full 18-bit I/O alias page as non-RAM");

    /* 18-bit MMU mode: low 017xxxx is RAM, 077xxxx remains the I/O page. */
    bus_set_pdp1184_io_16bit(0);
    bus_set_pdp1184_m22e(0);
    bus_write16(0177570, 065432);
    check(bus_read16(0177570) == 065432,
          "18-bit mode must treat low 0177570 as RAM");
    check(bus_read16(0777570) == 012345,
          "18-bit mode must decode SR at 0777570");
    check(bus_addr_is_ram(0172540),
          "18-bit mode must treat low 0172540 as RAM");
    check(!bus_is_nxm(0172540),
          "18-bit mode low 0172540 must not raise NXM");
    check(!bus_addr_is_ram(0760000),
          "18-bit mode must reserve full 18-bit I/O page as non-RAM");

    /* 22-bit mode: low windows (017xxxx and 077xxxx) are RAM, high 017777xxx is I/O. */
    bus_set_pdp1184_m22e(1);
    bus_write16(0777570, 054321);
    check(bus_read16(0177570) == 065432,
          "22-bit mode must keep low 0177570 as RAM");
    check(bus_read16(0777570) == 054321,
          "22-bit mode must treat low 0777570 as RAM");
    check(bus_read16(017777570) == 012345,
          "22-bit I/O window must decode SR at 017777570");
    check(!bus_addr_is_ram(017777000),
          "22-bit mode must reserve full high I/O page as non-RAM");

    /* Switching back to 16-bit mode restores low-window device decode. */
    bus_set_pdp1184_io_16bit(1);
    bus_set_pdp1184_m22e(0);
    check(bus_read16(0177570) == 012345,
          "switch back to 16-bit mode must restore SR decode at 0177570");

    if (!g_fail) {
        printf("PASS: test_pdp1184_io_window\n");
        return 0;
    }
    return 1;
}
