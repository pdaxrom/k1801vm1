#include <stdio.h>
#include <string.h>

#include "../bus.h"
#include "../dev_dl11.h"
#include "../dev_rh11.h"
#include "../dev_kw11.h"
#include "../dev_lp11.h"
#include "../dev_rk11.h"
#include "../dev_sr.h"

#define RH11_BASE 0177440
#define RH11_END  0177476

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

    if (dl11_init() != 0 || kw11_init() != 0 || rk11_init() != 0 ||
            rh11_init() != 0 ||
            lp11_init() != 0 || sr_init() != 0) {
        fprintf(stderr, "FAIL: init devices\n");
        return 1;
    }

    rh11_reset();
    check(bus_is_nxm(RH11_BASE) == 0, "RH11 base must decode on lsi11");
    check(bus_is_nxm(RH11_END) == 0, "RH11 end must decode on lsi11");

    /* Writes to RH11 register space must be visible. */
    bus_write16(RH11_BASE, 012345);
    check((bus_read16(RH11_BASE) & 000077) == (012345 & 000077),
          "RH11 CS1 low control bits must be writable on lsi11");

    if (!g_fail) {
        printf("PASS: test_rh11_lsi11_absent\n");
        return 0;
    }
    return 1;
}
