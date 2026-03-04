#include <stdio.h>
#include <string.h>

#include "../bus.h"
#include "../dev_dz11.h"
#include "../irq.h"

#define DZ_CSR  0160100
#define DZ_RBUF 0160102
#define DZ_LPR  0160102
#define DZ_TCR  0160104

#define CSR_MSE   0000040
#define CSR_RIE   0000100
#define CSR_RDONE 0000200
#define CSR_TIE   0040000
#define CSR_TRDY  0100000

#define RBUF_CHAR    0000377
#define RBUF_V_RLINE 8
#define RBUF_VALID   0100000

#define LPR_RCVE 0010000

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
    regs r;
    char err[128];
    uint16_t vec = 0;
    uint16_t csr = 0;
    uint16_t rbuf = 0;

    memset(&r, 0, sizeof(r));
    memset(err, 0, sizeof(err));

    bus_reset_config();
    if (bus_configure(BUS_MACHINE_LSI11_1104, 0, err, sizeof(err)) != 0) {
        fprintf(stderr, "FAIL: bus_configure: %s\n", err);
        return 1;
    }
    bus_init();

    if (dz11_set_listen_port(0, err, sizeof(err)) != 0) {
        fprintf(stderr, "FAIL: dz11_set_listen_port: %s\n", err);
        return 1;
    }
    if (dz11_init() != 0) {
        fprintf(stderr, "FAIL: dz11_init\n");
        return 1;
    }
    dz11_reset();

    check(bus_is_nxm(DZ_CSR) == 0, "DZ11 CSR must be decoded");
    check(bus_is_nxm(DZ_RBUF) == 0, "DZ11 RBUF/LPR must be decoded");
    check(bus_is_nxm(DZ_TCR) == 0, "DZ11 TCR must be decoded");
    check(bus_is_nxm(0160110) == 1, "Address after DZ11 range must be NXM");

    /* Enable transmitter on line 0 and master scan + TX IE. */
    bus_write16(DZ_TCR, 0000001);
    bus_write16(DZ_CSR, (uint16_t)(CSR_MSE | CSR_TIE));

    csr = bus_read16(DZ_CSR);
    check((csr & CSR_TRDY) != 0, "DZ11 must set TRDY when at least one line is enabled");

    vec = 0;
    check(irq_poll(&r, &vec) == 1, "DZ11 TX IRQ must be delivered");
    check((vec & 0000777) == 000304, "DZ11 TX vector must be 000304");

    /* Enable RX on line 0 and inject one character. */
    bus_write16(DZ_LPR, LPR_RCVE);
    bus_write16(DZ_CSR, (uint16_t)(CSR_MSE | CSR_RIE | CSR_TIE));
    dz11_test_inject_rx(0, (uint8_t)'A', 0);

    csr = bus_read16(DZ_CSR);
    check((csr & CSR_RDONE) != 0, "DZ11 must set RDONE when RX data is queued");

    vec = 0;
    check(irq_poll(&r, &vec) == 1, "DZ11 RX IRQ must be delivered");
    check((vec & 0000777) == 000300, "DZ11 RX vector must be 000300");

    rbuf = bus_read16(DZ_RBUF);
    check((rbuf & RBUF_VALID) != 0, "DZ11 RBUF must set VALID bit");
    check((rbuf & RBUF_CHAR) == (uint16_t)'A', "DZ11 RBUF must return injected byte");
    check(((rbuf >> RBUF_V_RLINE) & 07u) == 0u, "DZ11 RBUF line field must be 0");

    csr = bus_read16(DZ_CSR);
    check((csr & CSR_RDONE) == 0, "DZ11 RDONE must clear after reading last RBUF entry");

    /* Disable scanner and make sure status-only bits clear. */
    bus_write16(DZ_CSR, (uint16_t)(CSR_RIE | CSR_TIE));
    csr = bus_read16(DZ_CSR);
    check((csr & (CSR_RDONE | CSR_TRDY)) == 0,
          "DZ11 RDONE/TRDY must clear when MSE is disabled");

    dz11_shutdown();

    if (g_fail) {
        return 1;
    }
    printf("PASS: test_dz11\n");
    return 0;
}
