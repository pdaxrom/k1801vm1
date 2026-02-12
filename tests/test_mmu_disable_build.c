#include "mmu_test_common.h"

#if !defined(ENABLE_MMU)
#define ENABLE_MMU 0
#endif

#if !(defined(ENABLE_MMU) && (ENABLE_MMU))

static int test_mmu_regs_are_stubbed_and_no_translation(void)
{
    mmu_fixture fx;

    mmu_set_test("mmu_disable_build_stubbed");
    mmu_fixture_setup(&fx);

    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0000,
                        mmu_op_mov(mmu_operand(2, 7), mmu_operand(3, 7)));
    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0002, 000001);
    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0004, 0177572);

    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0006,
                        mmu_op_mov(mmu_operand(2, 7), mmu_operand(3, 7)));
    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0010, 000100);
    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0012, 0172340);

    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0014,
                        mmu_op_mov(mmu_operand(3, 7), mmu_operand(0, 0)));
    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0016, 0177572);

    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0020,
                        mmu_op_mov(mmu_operand(3, 7), mmu_operand(0, 1)));
    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0022, 0172340);

    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0024,
                        mmu_op_mov(mmu_operand(2, 7), mmu_operand(0, 2)));
    mmu_phys_write_word(&fx, MMU_TEST_BASE + 0026, 001234);

    MMU_ASSERT_EQ(core_step(&fx.r), 0, "write SSR0 stub");
    MMU_ASSERT_EQ(core_step(&fx.r), 0, "write PAR stub");
    MMU_ASSERT_EQ(core_step(&fx.r), 0, "read SSR0 stub");
    MMU_ASSERT_EQ(core_step(&fx.r), 0, "read PAR stub");
    MMU_ASSERT_EQ(core_step(&fx.r), 0, "normal execution after stubbed writes");

    MMU_ASSERT_EQ(fx.r.r[0], 000000, "SSR0 read must be zero when MMU build is disabled");
    MMU_ASSERT_EQ(fx.r.r[1], 000000, "PAR read must be zero when MMU build is disabled");
    MMU_ASSERT_EQ(fx.r.r[2], 001234, "execution remains identity mapped");

    mmu_fixture_teardown(&fx);
    return 0;
}

int main(void)
{
    if (test_mmu_regs_are_stubbed_and_no_translation()) {
        return 1;
    }

    printf("PASS: test_mmu_disable_build\n");
    return 0;
}

#else

int main(void)
{
    printf("SKIP: test_mmu_disable_build (ENABLE_MMU=1)\n");
    return 0;
}

#endif
