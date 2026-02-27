#include "mmu_test_common.h"

#if !defined(ENABLE_MMU)
#define ENABLE_MMU 0
#endif

#if defined(ENABLE_MMU) && (ENABLE_MMU)

#define MMU_SSR0_FREEZE 0160000
#define MMU_SSR1 0177574

static int test_mmu_register_decode(void)
{
    mmu_fixture fx;

    mmu_set_test("mmu_register_decode");
    mmu_fixture_setup(&fx);

    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0000,
                        mmu_op_mov(mmu_operand(2, 7), mmu_operand(3, 7)));
    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0002, 012340);
    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0004, 0177516);

    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0006,
                        mmu_op_mov(mmu_operand(3, 7), mmu_operand(0, 0)));
    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0010, 0177516);

    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0012,
                        mmu_op_mov(mmu_operand(2, 7), mmu_operand(3, 7)));
    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0014, 012345);
    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0016, 0172340);

    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0020,
                        mmu_op_mov(mmu_operand(3, 7), mmu_operand(0, 1)));
    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0022, 0172340);

    MMU_ASSERT_EQ(core_step(&fx.r), 0, "step 1");
    MMU_ASSERT_EQ(core_step(&fx.r), 0, "step 2");
    MMU_ASSERT_EQ(core_step(&fx.r), 0, "step 3");
    MMU_ASSERT_EQ(core_step(&fx.r), 0, "step 4");

    MMU_ASSERT_EQ(fx.r.mmu_ssr3, 000040, "SSR3 write/read (J11 MM3 mask)");
    MMU_ASSERT_EQ(fx.r.r[0], 000040, "SSR3 decode via MOV");
    MMU_ASSERT_EQ(fx.r.mmu_par[0][0][0], 012345, "KISA0 write/read");
    MMU_ASSERT_EQ(fx.r.r[1], 012345, "KISA0 decode via MOV");

    mmu_fixture_teardown(&fx);
    return 0;
}

static int test_mmu_translation_fetch_and_data(void)
{
    mmu_fixture fx;

    mmu_set_test("mmu_translation_fetch_data");
    mmu_fixture_setup(&fx);

    fx.r.mmu_ssr0 = 000001;
    fx.r.mmu_ssr3 = 000000;
    fx.r.mmu_par[0][0][0] = 0100;
    fx.r.mmu_pdr[0][0][0] = 0177006;

    fx.r.r[7] = 000000;

    mmu_phys_write_word(&fx, 010000,
                        mmu_op_mov(mmu_operand(2, 7), mmu_operand(0, 0)));
    mmu_phys_write_word(&fx, 010002, 01234);

    mmu_phys_write_word(&fx, 010004,
                        mmu_op_mov(mmu_operand(2, 7), mmu_operand(3, 7)));
    mmu_phys_write_word(&fx, 010006, 07777);
    mmu_phys_write_word(&fx, 010010, 000200);

    MMU_ASSERT_EQ(core_step(&fx.r), 0, "mapped fetch step");
    MMU_ASSERT_EQ(fx.r.r[0], 01234, "mapped instruction stream");
    MMU_ASSERT_EQ(fx.r.r[7], 000004, "PC after mapped fetch");

    MMU_ASSERT_EQ(core_step(&fx.r), 0, "mapped data step");
    MMU_ASSERT_EQ(mmu_phys_read_word(&fx, 010200), 07777, "data uses translated address");
    MMU_ASSERT_EQ(fx.r.r[7], 000012, "PC after mapped write");

    mmu_fixture_teardown(&fx);
    return 0;
}

static int test_mmu_ssr2_tracks_fetch_and_freeze(void)
{
    mmu_fixture fx;

    mmu_set_test("mmu_ssr2_tracks_fetch_freeze");
    mmu_fixture_setup(&fx);

    fx.r.mmu_ssr0 = 000000;
    fx.r.r[7] = MMU_TEST_BASE;

    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0000,
                        mmu_op_mov(mmu_operand(2, 7), mmu_operand(0, 0)));
    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0002, 000001);
    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0004,
                        mmu_op_mov(mmu_operand(2, 7), mmu_operand(0, 1)));
    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0006, 000002);
    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0010,
                        mmu_op_mov(mmu_operand(2, 7), mmu_operand(0, 2)));
    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0012, 000003);

    MMU_ASSERT_EQ(core_step(&fx.r), 0, "step 1");
    MMU_ASSERT_EQ(fx.r.mmu_ssr2, MMU_TEST_BASE, "MMR2 captures first instruction VA");

    fx.r.mmu_ssr0 = MMU_SSR0_FREEZE;
    MMU_ASSERT_EQ(core_step(&fx.r), 0, "step 2 frozen");
    MMU_ASSERT_EQ(fx.r.mmu_ssr2, MMU_TEST_BASE, "MMR2 must remain frozen");

    fx.r.mmu_ssr0 = 000000;
    MMU_ASSERT_EQ(core_step(&fx.r), 0, "step 3 unfrozen");
    MMU_ASSERT_EQ(fx.r.mmu_ssr2, MMU_TEST_BASE + 0010,
                  "MMR2 resumes updates after freeze clear");

    mmu_fixture_teardown(&fx);
    return 0;
}

static int test_mmu_ssr1_read_hides_immediate_entries(void)
{
    mmu_fixture fx;

    mmu_set_test("mmu_ssr1_read_hides_immediate");
    mmu_fixture_setup(&fx);

    fx.r.mmu_ssr0 = 000000;
    fx.r.r[7] = MMU_TEST_BASE;
    fx.r.r[1] = 002000;

    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0000,
                        mmu_op_mov(mmu_operand(2, 7), mmu_operand(0, 0)));
    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0002, 01234);
    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0004,
                        mmu_op_mov(mmu_operand(3, 7), mmu_operand(0, 3)));
    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0006, MMU_SSR1);

    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0010,
                        mmu_op_mov(mmu_operand(2, 7), mmu_operand(2, 1)));
    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0012, 05670);
    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0014,
                        mmu_op_mov(mmu_operand(3, 7), mmu_operand(0, 4)));
    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0016, MMU_SSR1);

    MMU_ASSERT_EQ(core_step(&fx.r), 0, "immediate-to-register");
    MMU_ASSERT_EQ(fx.r.mmu_ssr1, 000027, "raw MMR1 keeps immediate marker");
    fx.r.mmu_ssr0 = MMU_SSR0_FREEZE;
    MMU_ASSERT_EQ(core_step(&fx.r), 0, "read MMR1 after immediate-to-register");
    MMU_ASSERT_EQ(fx.r.r[3], 000000, "MMR1 read hides standalone immediate marker");
    fx.r.mmu_ssr0 = 000000;

    MMU_ASSERT_EQ(core_step(&fx.r), 0, "immediate-to-autoinc");
    MMU_ASSERT_EQ(fx.r.mmu_ssr1, 010427, "raw MMR1 records immediate then R1+");
    fx.r.mmu_ssr0 = MMU_SSR0_FREEZE;
    MMU_ASSERT_EQ(core_step(&fx.r), 0, "read MMR1 after immediate-to-autoinc");
    MMU_ASSERT_EQ(fx.r.r[4], 000021, "MMR1 read should keep only non-immediate update");

    mmu_fixture_teardown(&fx);
    return 0;
}

int main(void)
{
    int failed = 0;

    failed += test_mmu_register_decode();
    failed += test_mmu_translation_fetch_and_data();
    failed += test_mmu_ssr2_tracks_fetch_and_freeze();
    failed += test_mmu_ssr1_read_hides_immediate_entries();

    if (failed) {
        return 1;
    }

    printf("PASS: test_mmu_basic\n");
    return 0;
}

#else

int main(void)
{
    printf("SKIP: test_mmu_basic (ENABLE_MMU=0)\n");
    return 0;
}

#endif
