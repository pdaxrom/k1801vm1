#include "mmu_test_common.h"

#if !defined(ENABLE_MMU)
#define ENABLE_MMU 0
#endif

#if defined(ENABLE_MMU) && (ENABLE_MMU)

static int test_ifetch_from_22bit_memory(void)
{
    mmu_fixture fx;

    mmu_set_test("ifetch_from_22bit_memory");
    mmu_fixture_setup(&fx);

    fx.r.mmu_ssr0 = 000001;
    fx.r.mmu_ssr3 = 000020;
    fx.r.mmu_par[0][0][0] = 02000;
    fx.r.mmu_pdr[0][0][0] = 0177006;
    fx.r.r[7] = 000000;

    mmu_phys22_write_word(&fx, 0200000,
                          mmu_op_mov(mmu_operand(2, 7), mmu_operand(0, 0)));
    mmu_phys22_write_word(&fx, 0200002, 012345);

    MMU_ASSERT_EQ(core_step(&fx.r), 0, "mapped fetch step");
    MMU_ASSERT_EQ(fx.r.r[0], 012345, "instruction fetched from >64KB physical memory");
    MMU_ASSERT_EQ(fx.r.r[7], 000004, "PC advanced in virtual address space");

    mmu_fixture_teardown(&fx);
    return 0;
}

static int test_data_store_to_22bit_memory(void)
{
    mmu_fixture fx;

    mmu_set_test("data_store_to_22bit_memory");
    mmu_fixture_setup(&fx);

    fx.r.mmu_ssr0 = 000001;
    fx.r.mmu_ssr3 = 000024; /* M22E + KD split I/D */

    fx.r.mmu_par[0][0][0] = 02000;
    fx.r.mmu_pdr[0][0][0] = 0177006;

    fx.r.mmu_par[0][1][0] = 02400;
    fx.r.mmu_pdr[0][1][0] = 0177006;

    fx.r.r[7] = 000000;

    mmu_phys22_write_word(&fx, 0200000,
                          mmu_op_mov(mmu_operand(2, 7), mmu_operand(3, 7)));
    mmu_phys22_write_word(&fx, 0200002, 045671);
    mmu_phys22_write_word(&fx, 0200004, 000200);

    mmu_phys22_write_word(&fx, 0240200, 000000);
    mmu_phys22_write_word(&fx, 0200200, 000000);

    MMU_ASSERT_EQ(core_step(&fx.r), 0, "mapped store step");
    MMU_ASSERT_EQ(mmu_phys22_read_word(&fx, 0240200), 045671,
                  "data store goes to D-space physical address >64KB");
    MMU_ASSERT_EQ(mmu_phys22_read_word(&fx, 0200200), 000000,
                  "I-space physical address remains untouched");

    mmu_fixture_teardown(&fx);
    return 0;
}

static int test_io_window_mmu_disabled_16bit(void)
{
    mmu_fixture fx;

    mmu_set_test("io_window_mmu_disabled_16bit");
    mmu_fixture_setup(&fx);

    fx.r.mmu_ssr0 = 000000; /* MMU disabled */
    fx.r.mmu_ssr3 = 000020; /* M22E must not matter when MMU is disabled */

    mmu_phys22_write_word(&fx, MMU_TEST_BASE + 0000,
                          mmu_op_mov(mmu_operand(3, 7), mmu_operand(0, 0)));
    mmu_phys22_write_word(&fx, MMU_TEST_BASE + 0002, 0177570);

    mmu_phys22_write_word(&fx, 0177570, 011111);
    mmu_phys22_write_word(&fx, 017777570, 022222);

    MMU_ASSERT_EQ(core_step(&fx.r), 0, "MMU off step");
    MMU_ASSERT_EQ(fx.r.r[0], 011111, "MMU off: I/O window uses 16-bit address space");

    mmu_fixture_teardown(&fx);
    return 0;
}

static int test_io_window_mmu_enabled_16bit(void)
{
    mmu_fixture fx;

    mmu_set_test("io_window_mmu_enabled_16bit");
    mmu_fixture_setup(&fx);

    fx.r.mmu_ssr0 = 000001; /* MMU enable */
    fx.r.mmu_ssr3 = 000000; /* M22E=0 -> 18-bit physical, low I/O decode */
    fx.r.mmu_par[0][0][0] = 000000; /* identity for instruction fetch */
    fx.r.mmu_pdr[0][0][0] = 0177006;
    fx.r.mmu_par[0][0][7] = 01600;   /* maps VA 0177570 to PA 0177570 */
    fx.r.mmu_pdr[0][0][7] = 0177006; /* resident RW expand-up */

    mmu_phys22_write_word(&fx, MMU_TEST_BASE + 0000,
                          mmu_op_mov(mmu_operand(3, 7), mmu_operand(0, 0)));
    mmu_phys22_write_word(&fx, MMU_TEST_BASE + 0002, 0177570);

    mmu_phys22_write_word(&fx, 0177570, 033333);
    mmu_phys22_write_word(&fx, 017777570, 044444);

    MMU_ASSERT_EQ(core_step(&fx.r), 0, "MMU on M22E=0 step");
    MMU_ASSERT_EQ(fx.r.r[0], 033333,
                  "MMU on + M22E=0: I/O window stays in low space");

    mmu_fixture_teardown(&fx);
    return 0;
}

static int test_io_window_mmu_enabled_22bit(void)
{
    mmu_fixture fx;

    mmu_set_test("io_window_mmu_enabled_22bit");
    mmu_fixture_setup(&fx);

    fx.r.mmu_ssr0 = 000001; /* MMU enable */
    fx.r.mmu_ssr3 = 000020; /* M22E=1 -> 22-bit address mode */
    fx.r.mmu_par[0][0][0] = 000000; /* identity for instruction fetch */
    fx.r.mmu_pdr[0][0][0] = 0177006;
    fx.r.mmu_par[0][0][7] = 0177600; /* maps VA 0177570 to PA 017777570 */
    fx.r.mmu_pdr[0][0][7] = 0177006; /* resident RW expand-up */

    mmu_phys22_write_word(&fx, MMU_TEST_BASE + 0000,
                          mmu_op_mov(mmu_operand(3, 7), mmu_operand(0, 0)));
    mmu_phys22_write_word(&fx, MMU_TEST_BASE + 0002, 0177570);

    mmu_phys22_write_word(&fx, 0177570, 055555);
    mmu_phys22_write_word(&fx, 017777570, 066666);

    MMU_ASSERT_EQ(core_step(&fx.r), 0, "MMU on M22E=1 step");
    MMU_ASSERT_EQ(fx.r.r[0], 066666, "MMU on + M22E=1: I/O window is 22-bit");

    mmu_fixture_teardown(&fx);
    return 0;
}

static int test_io_window_mmu_enabled_18bit_alias(void)
{
    mmu_fixture fx;

    mmu_set_test("io_window_mmu_enabled_18bit_alias");
    mmu_fixture_setup(&fx);

    fx.r.mmu_ssr0 = 000001; /* MMU enable */
    fx.r.mmu_ssr3 = 000000; /* M22E=0 -> 18-bit physical address mode */
    fx.r.mmu_par[0][0][0] = 000000;  /* identity for instruction fetch */
    fx.r.mmu_pdr[0][0][0] = 0177006;
    fx.r.mmu_par[0][0][7] = 0177600; /* maps VA 0177570 to PA 0777570 */
    fx.r.mmu_pdr[0][0][7] = 0177006; /* resident RW expand-up */

    mmu_phys22_write_word(&fx, MMU_TEST_BASE + 0000,
                          mmu_op_mov(mmu_operand(3, 7), mmu_operand(0, 0)));
    mmu_phys22_write_word(&fx, MMU_TEST_BASE + 0002, 0177570);

    mmu_phys22_write_word(&fx, 0177570, 011111);
    mmu_phys22_write_word(&fx, 0777570, 022222);
    mmu_phys22_write_word(&fx, 017777570, 033333);

    MMU_ASSERT_EQ(core_step(&fx.r), 0, "MMU on 18-bit alias step");
    MMU_ASSERT_EQ(fx.r.r[0], 022222,
                  "MMU on + M22E=0: VA segment 7 must map to 18-bit I/O alias");

    mmu_fixture_teardown(&fx);
    return 0;
}

static int test_m22e1_does_not_decode_18bit_io_alias_regs(void)
{
    mmu_fixture fx;

    mmu_set_test("m22e1_does_not_decode_18bit_io_alias_regs");
    mmu_fixture_setup(&fx);

    fx.r.mmu_ssr0 = 000001; /* MMU enable */
    fx.r.mmu_ssr3 = 000020; /* M22E=1 -> 22-bit mode */
    fx.r.mmu_par[0][0][0] = 000000; /* identity for instruction fetch */
    fx.r.mmu_pdr[0][0][0] = 0177006;
    fx.r.mmu_par[0][0][7] = 07600;  /* maps VA 0177572 to PA 0777572 */
    fx.r.mmu_pdr[0][0][7] = 0177006;

    mmu_phys22_write_word(&fx, MMU_TEST_BASE + 0000,
                          mmu_op_mov(mmu_operand(3, 7), mmu_operand(0, 0)));
    mmu_phys22_write_word(&fx, MMU_TEST_BASE + 0002, 0177572);

    /*
     * In M22E=1 this must be plain RAM read from PA 0777572.
     * Wrong 18-bit alias decode would route to MMU SSR0 (0177572).
     */
    mmu_phys22_write_word(&fx, 0777572, 012345);

    MMU_ASSERT_EQ(core_step(&fx.r), 0, "M22E=1 18-bit alias reject step");
    MMU_ASSERT_EQ(fx.r.r[0], 012345,
                  "M22E=1 must not decode 18-bit alias window as internal I/O");

    mmu_fixture_teardown(&fx);
    return 0;
}

int main(void)
{
    int failed = 0;

    failed += test_ifetch_from_22bit_memory();
    failed += test_data_store_to_22bit_memory();
    failed += test_io_window_mmu_disabled_16bit();
    failed += test_io_window_mmu_enabled_16bit();
    failed += test_io_window_mmu_enabled_22bit();
    failed += test_io_window_mmu_enabled_18bit_alias();
    failed += test_m22e1_does_not_decode_18bit_io_alias_regs();

    if (failed) {
        return 1;
    }

    printf("PASS: test_mmu_22bit\n");
    return 0;
}

#else

int main(void)
{
    printf("SKIP: test_mmu_22bit (ENABLE_MMU=0)\n");
    return 0;
}

#endif
