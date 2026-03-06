#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../bus.h"
#include "../dev_rh11.h"

#define RHCS1 0177440
#define RHWC  0177442
#define RHBA  0177444
#define RHDA  0177446
#define RHCS2 0177450

#define RHCS1_GO       0000001
#define RHCS1_IE       0000100
#define RH11_FUNC_READ 0000020

static void dump_step(const char *name)
{
    printf("%s CS1=%06o CS2=%06o\n", name, bus_read16(RHCS1), bus_read16(RHCS2));
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

    if (rh11_set_mode(RH11_MODE_RH70) != 0) {
        fprintf(stderr, "FAIL: rh11_set_mode(RH70)\n");
        return 1;
    }
    if (rh11_init() != 0) {
        fprintf(stderr, "FAIL: rh11_init\n");
        return 1;
    }
    if (rh11_open_image("disks/rt11v5.3/system.dsk") != 0) {
        fprintf(stderr, "FAIL: rh11_open_image(disk)\n");
        return 1;
    }

    rh11_reset();
    dump_step("INIT");

    bus_write8((uint16_t)(RHCS1 + 1), 0000200);
    dump_step("CCLR");

    bus_write8(RHCS1, RHCS1_IE);
    dump_step("IEONLY");

    bus_write16(RHWC, 0177777);
    bus_write16(RHBA, 0000400);
    bus_write16(RHDA, 000000);
    bus_write8(RHCS1, (uint8_t)(RH11_FUNC_READ | RHCS1_GO | RHCS1_IE));
    dump_step("GO1");

    bus_write8(RHCS1, (uint8_t)(RH11_FUNC_READ | RHCS1_GO | RHCS1_IE));
    dump_step("GO2");

    rh11_close_image();
    return 0;
}
