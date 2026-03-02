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
    check(bus_read16(0177570) == 012345,
          "16-bit I/O window must decode SR at 0177570");

    /* 22-bit mode: low 0177570 is RAM, high 017777570 is SR. */
    bus_set_pdp1184_io_16bit(0);
    bus_write16(0177570, 065432);
    check(bus_read16(0177570) == 065432,
          "22-bit mode must treat low 0177570 as RAM");
    check(bus_read16(017777570) == 012345,
          "22-bit I/O window must decode SR at 017777570");

    /* Switching back to 16-bit mode restores low-window device decode. */
    bus_set_pdp1184_io_16bit(1);
    check(bus_read16(0177570) == 012345,
          "switch back to 16-bit mode must restore SR decode at 0177570");

    if (!g_fail) {
        printf("PASS: test_pdp1184_io_window\n");
        return 0;
    }
    return 1;
}
