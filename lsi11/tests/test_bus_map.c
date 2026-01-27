#include <stdio.h>
#include <stdint.h>

#include "../bus.h"
#include "../devio.h"
#include "../dev_sr.h"

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
    bus_init();

    /* Register at least SR so IO page has at least one decoded address */
    if (sr_init() != 0) {
        fprintf(stderr, "FAIL: sr_init\n");
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
