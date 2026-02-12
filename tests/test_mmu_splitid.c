#include "mmu_test_common.h"

#if !defined(ENABLE_MMU)
#define ENABLE_MMU 0
#endif

#if defined(ENABLE_MMU) && (ENABLE_MMU)

#define MMU_SSR3_KD 0000001

static int test_split_id_data_uses_d_space(void)
{
    mmu_fixture fx;

    mmu_set_test("split_id_data_uses_d_space");
    mmu_fixture_setup(&fx);

    fx.r.mmu_ssr0 = 000001;
    fx.r.mmu_ssr3 = MMU_SSR3_KD;

    fx.r.mmu_par[0][0][0] = 0100;
    fx.r.mmu_pdr[0][0][0] = 0177006;

    fx.r.mmu_par[0][1][0] = 0120;
    fx.r.mmu_pdr[0][1][0] = 0177006;

    fx.r.r[7] = 000000;

    mmu_phys_write_word(&fx, 010000,
                        mmu_op_mov(mmu_operand(2, 7), mmu_operand(3, 7)));
    mmu_phys_write_word(&fx, 010002, 012345);
    mmu_phys_write_word(&fx, 010004, 000200);

    MMU_ASSERT_EQ(core_step(&fx.r), 0, "split I/D write step");

    MMU_ASSERT_EQ(mmu_phys_read_word(&fx, 012200), 012345, "D-space translated target");
    MMU_ASSERT_EQ(mmu_phys_read_word(&fx, 010200), 000000, "I-space target unchanged");

    mmu_fixture_teardown(&fx);
    return 0;
}

static int test_vector_fetch_uses_physical_space(void)
{
    mmu_fixture fx;

    mmu_set_test("vector_fetch_uses_physical");
    mmu_fixture_setup(&fx);

    fx.r.mmu_ssr0 = 000001;
    fx.r.mmu_ssr3 = MMU_SSR3_KD;

    fx.r.mmu_par[0][1][0] = 0100;
    fx.r.mmu_pdr[0][1][0] = 0177006;

    fx.r.mmu_par[3][0][0] = 0140;
    fx.r.mmu_pdr[3][0][0] = 0177006;

    fx.r.psw = 0140000;
    fx.r.r[7] = 000000;

    mmu_phys_write_word(&fx, 014000, mmu_op_bpt());

    mmu_phys_write_word(&fx, 000014, 002222);
    mmu_phys_write_word(&fx, 000016, 000340);

    mmu_phys_write_word(&fx, 010014, 003333);
    mmu_phys_write_word(&fx, 010016, 000000);
    mmu_phys_write_word(&fx, 014014, 004444);
    mmu_phys_write_word(&fx, 014016, 000000);

    MMU_ASSERT_EQ(core_step(&fx.r), 0, "BPT trap in user mode");

    MMU_ASSERT_EQ(fx.r.r[7], 002222, "vector fetch must use physical vector table");
    MMU_ASSERT_EQ(fx.r.psw, 030340, "PSW loaded from physical vector with PM=old mode");

    mmu_fixture_teardown(&fx);
    return 0;
}

int main(void)
{
    int failed = 0;

    failed += test_split_id_data_uses_d_space();
    failed += test_vector_fetch_uses_physical_space();

    if (failed) {
        return 1;
    }

    printf("PASS: test_mmu_splitid\n");
    return 0;
}

#else

int main(void)
{
    printf("SKIP: test_mmu_splitid (ENABLE_MMU=0)\n");
    return 0;
}

#endif
