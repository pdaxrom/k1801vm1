#include "mmu_test_common.h"

#if !defined(ENABLE_MMU)
#define ENABLE_MMU 0
#endif

#if defined(ENABLE_MMU) && (ENABLE_MMU)

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

    MMU_ASSERT_EQ(fx.r.mmu_ssr3, 012340, "SSR3 write/read");
    MMU_ASSERT_EQ(fx.r.r[0], 012340, "SSR3 decode via MOV");
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

int main(void)
{
    int failed = 0;

    failed += test_mmu_register_decode();
    failed += test_mmu_translation_fetch_and_data();

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
