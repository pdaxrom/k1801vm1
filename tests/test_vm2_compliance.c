#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/core.h"
#include "core/hardware.h"
#include "cpu_diag.h"

static int test_vm2_mtps_compliance(void)
{
    cpu_diag_fixture fx;
    cpu_diag_fixture_init(&fx, K1801VM2);

    fx.r.psw = 0000420; // bit 8 (H) and bit 4 (T) set
    fx.r.r[7] = 001000;
    fx.r.fAbort = 0;

    // Set up TRACE vector at 014/016 to prevent double trap / failure
    fx.r.store_word(&fx.r, 000014, 003000);
    fx.r.store_word(&fx.r, 000016, 000000);

    // Verify vector setup
    if (fx.r.load_word(&fx.r, 000014) != 003000) {
        fprintf(stderr, "FAIL T1: Vector setup failed at 014! Load returned %06o\n", fx.r.load_word(&fx.r, 000014));
    }

    /* MTPS #347 -> 106427 000347 */
    fx.r.store_word(&fx.r, 001000, 0106427);
    fx.r.store_word(&fx.r, 001002, 0000347);

    core_step(&fx.r);

    int failed = 0;
    // Pushed state is at SP. oldPS is pushed first, then oldPC.
    // SP was TEST_STACK (0400). After 2 pushes, SP should be 0374.
    // [0374] = oldPC
    // [0376] = oldPS
    word pushed_psw = fx.r.load_word(&fx.r, 000376);

    if ((pushed_psw & 0000400) != 0000400) {
        fprintf(stderr, "FAIL T1: bit 8 (H) not preserved in pushed PS. PS=%06o\n", pushed_psw);
        failed = 1;
    }
    if ((pushed_psw & 0000020) != 0000020) {
        fprintf(stderr, "FAIL T1: bit 4 (T) not preserved in pushed PS. PS=%06o\n", pushed_psw);
        failed = 1;
    }
    if ((pushed_psw & 0000340) != 0000340) {
        fprintf(stderr, "FAIL T1: bits 7..5 not updated correctly in pushed PS. PS=%06o\n", pushed_psw);
        failed = 1;
    }
    if ((pushed_psw & 0000007) != 0000007) {
        fprintf(stderr, "FAIL T1: bits 2..0 not updated correctly in pushed PS. PS=%06o\n", pushed_psw);
        failed = 1;
    }

    cpu_diag_fixture_fini(&fx);
    return failed;
}

static int test_vm2_user_trap_forces_bit8_zero(void)
{
    cpu_diag_fixture fx;
    cpu_diag_fixture_init(&fx, K1801VM2);

    fx.r.psw = 0000000;
    fx.r.r[7] = 001000;
    fx.r.r[6] = 000400; // SP
    fx.r.fAbort = 0;

    // BPT vector at 014
    fx.r.store_word(&fx.r, 000014, 002000); // New PC
    fx.r.store_word(&fx.r, 000016, 000400); // New PSW with bit 8=1

    // Instruction BPT
    fx.r.store_word(&fx.r, 001000, 0000003);

    core_step(&fx.r);

    int failed = 0;
    if (fx.r.psw & 0000400) {
        fprintf(stderr, "FAIL T2: USER trap incorrectly loaded bit 8 from vector. PS=%06o\n", fx.r.psw);
        failed = 1;
    }

    cpu_diag_fixture_fini(&fx);
    return failed;
}

static int test_vm2_halt_trap_allows_bit8(void)
{
    cpu_diag_fixture fx;
    cpu_diag_fixture_init(&fx, K1801VM2);

    fx.r.psw = 0000400;
    fx.r.r[7] = 001000;
    fx.r.r[6] = 000400; // SP
    fx.r.fAbort = 0;

    // BPT vector at 014
    fx.r.store_word(&fx.r, 000014, 002000); // New PC
    fx.r.store_word(&fx.r, 000016, 000400); // New PSW with bit 8=1

    // Instruction BPT
    fx.r.store_word(&fx.r, 001000, 0000003);

    core_step(&fx.r);

    int failed = 0;
    if (!(fx.r.psw & 0000400)) {
        fprintf(stderr, "FAIL T3: HALT trap failed to load bit 8 from vector. PS=%06o\n", fx.r.psw);
        failed = 1;
    }

    cpu_diag_fixture_fini(&fx);
    return failed;
}

int main(void)
{
    int failed = 0;
    printf("Starting VM2 Compliance tests...\n");
    failed += test_vm2_mtps_compliance();
    failed += test_vm2_user_trap_forces_bit8_zero();
    failed += test_vm2_halt_trap_allows_bit8();

    if (failed) {
        printf("VM2 Compliance tests: FAILED (%d)\n", failed);
        return 1;
    }
    printf("VM2 Compliance tests: PASSED\n");
    return 0;
}
