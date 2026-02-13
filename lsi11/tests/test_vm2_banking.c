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
    if (bus_configure(BUS_MACHINE_LSI11_1104, 0, err, sizeof(err)) != 0) {
        fprintf(stderr, "FAIL: bus_configure: %s\n", err);
        return 1;
    }

    if (bus_vm2_configure(BUS_VM2_DEFAULT_USER_RAM_BYTES, 0000000,
                          err, sizeof(err)) != 0) {
        fprintf(stderr, "FAIL: bus_vm2_configure(USER=0200000 HALT=0000000): %s\n",
                err);
        return 1;
    }
    bus_init();
    if (sr_init() != 0) {
        fprintf(stderr, "FAIL: sr_init\n");
        return 1;
    }

    bus_vm2_cpu_write16(000100, 0, 012345);
    check(bus_vm2_cpu_read16(000100, 0) == 012345,
          "USER RAM write/read at 000100");
    check(bus_vm2_cpu_is_nxm(000100, 1) == 1,
          "HALT RAM absent: access at 000100 must be NXM");

    sr_set(005432);
    check(bus_vm2_cpu_read16(0177570, 0) == 005432,
          "I/O 0177570 must read SR in USER mode");
    check(bus_vm2_cpu_read16(0177570, 1) == 005432,
          "I/O 0177570 must read SR in HALT mode (not banked)");
    check(bus_vm2_cpu_is_nxm(0177776, 0) == 1,
          "Undecoded I/O 0177776 must stay NXM in USER mode");
    check(bus_vm2_cpu_is_nxm(0177776, 1) == 1,
          "Undecoded I/O 0177776 must stay NXM in HALT mode");

    if (bus_vm2_configure(BUS_VM2_DEFAULT_USER_RAM_BYTES, 0200000,
                          err, sizeof(err)) != 0) {
        fprintf(stderr, "FAIL: bus_vm2_configure(USER=0200000 HALT=0200000): %s\n",
                err);
        return 1;
    }
    bus_init();

    bus_vm2_cpu_write16(000120, 0, 001111);
    bus_vm2_cpu_write16(000120, 1, 002222);

    check(bus_vm2_cpu_is_nxm(000120, 1) == 0,
          "HALT RAM present: access at 000120 must not be NXM");
    check(bus_vm2_cpu_read16(000120, 0) == 001111,
          "USER RAM value at 000120 must stay isolated from HALT RAM");
    check(bus_vm2_cpu_read16(000120, 1) == 002222,
          "HALT RAM value at 000120 must differ from USER RAM");
    check(bus_read16(000120) == 001111,
          "Physical bus RAM at 000120 must remain USER bank");

    sr_set(001234);
    check(bus_vm2_cpu_read16(0177570, 0) == 001234,
          "I/O 0177570 USER read after HALT RAM enable");
    check(bus_vm2_cpu_read16(0177570, 1) == 001234,
          "I/O 0177570 HALT read after HALT RAM enable");

    if (!g_fail) {
        printf("PASS: test_vm2_banking\n");
        return 0;
    }
    return 1;
}
