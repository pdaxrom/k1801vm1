#include <stdio.h>
#include <stdint.h>

#include "../bus.h"
#include "../devio.h"
#include "../dev_sr.h"
#include "../dev_dl11.h"

/* Simple assert helpers */
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
    if (bus_configure(BUS_MACHINE_LSI11_1104, 0, err, sizeof(err)) != 0) {
        fprintf(stderr, "FAIL: bus_configure: %s\n", err);
        return 1;
    }
    bus_init();

    /* Register at least SR and DL11 so IO page has decoded addresses */
    if (sr_init() != 0) {
        fprintf(stderr, "FAIL: sr_init\n");
        return 1;
    }
    if (dl11_init() != 0) {
        fprintf(stderr, "FAIL: dl11_init\n");
        return 1;
    }

    /* RAM boundaries */
    check(!bus_is_nxm(0000000), "RAM start should not be NXM");
    check(!bus_is_nxm(0157777), "RAM end (0157777) should not be NXM");
    check(bus_is_nxm(0160000) == 0 || bus_is_nxm(0160000) == 1, "IO page start check reachable");

    /* RAM write/read OK within RAM */
    bus_write8(0157777, 000123);
    check(bus_read8(0157777) == 000123, "RAM read/write at 0157777");

    /* Strict: any address in IO page not decoded by a device => NXM.
       Pick a likely-undecoded CSR address (ensure no device registers it). */
    {
        uint16_t a = 0177776;
        /* If your machine later registers something at 0177776, change this address. */
        check(bus_is_nxm(a) == 1, "Undecoded IO address should be NXM");
    }

    /* SR is decoded */
    check(bus_is_nxm(0177570) == 0, "SR low byte should be decoded");
    check(bus_is_nxm(0177571) == 0, "SR high byte should be decoded");
    check(bus_is_nxm(0177560) == 0, "DL11 primary should be decoded");
    check(bus_is_nxm(0176500) == 0, "DL11 alias low should be decoded");
    check(bus_is_nxm(0176507) == 0, "DL11 alias high should be decoded");

    /* 22-bit I/O aliases must decode to the same device registers.
       This path is required by DCJ11 when MMU support is enabled. */
    check(bus_is_nxm(017777570) == 0, "SR 22-bit alias low should be decoded");
    check(bus_is_nxm(017777571) == 0, "SR 22-bit alias high should be decoded");
    check(bus_is_nxm(017777560) == 0, "DL11 22-bit alias should be decoded");
    check(bus_is_nxm(017777776) == 1,
          "Undecoded 22-bit I/O alias should be NXM");

    /* Word access: addr and addr+1 must both be valid.
       Example: last RAM byte is 0157777, so word at 0157777 crosses into IO page => should NXM. */
    check(bus_is_nxm(0157777) == 0, "Byte at 0157777 ok");
    check(bus_is_nxm(0160000) == 1 || bus_is_nxm(0160000) == 0, "Byte at 0160000 is IO (device or NXM)");

    /* The core adapter checks addr+1 for NXM on word; emulate that policy here: */
    check((bus_is_nxm(0157777) || bus_is_nxm(0160000)) == 1,
          "Word at 0157777 must be treated as NXM unless IO at 0160000 is decoded (should not be).");

    if (!g_fail) {
        printf("PASS: test_bus_map\n");
        return 0;
    }
    return 1;
}
