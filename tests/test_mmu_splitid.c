#include "mmu_test_common.h"

#if !defined(ENABLE_MMU)
#define ENABLE_MMU 0
#endif

#if defined(ENABLE_MMU) && (ENABLE_MMU)

#define MMU_SSR3_KD 0000004

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

static int test_vector_fetch_uses_kernel_d_space(void)
{
    mmu_fixture fx;

    mmu_set_test("vector_fetch_uses_kernel_d_space");
    mmu_fixture_setup(&fx);

    fx.r.mmu_ssr0 = 000001;
    fx.r.mmu_ssr3 = MMU_SSR3_KD;

    fx.r.mmu_par[0][1][0] = 0100;
    fx.r.mmu_pdr[0][1][0] = 0177006;

    fx.r.mmu_par[3][0][0] = 0140;
    fx.r.mmu_pdr[3][0][0] = 0177006;

    fx.r.psw = 0140000;
    fx.r.r[7] = 000000;
    /* Keep kernel stack above fixed J11 yellow-zone threshold (0400). */
    fx.r.r[6] = 001000;

    mmu_phys_write_word(&fx, 014000, mmu_op_bpt());

    mmu_phys_write_word(&fx, 000014, 002222);
    mmu_phys_write_word(&fx, 000016, 000340);

    mmu_phys_write_word(&fx, 010014, 003333);
    mmu_phys_write_word(&fx, 010016, 000000);
    mmu_phys_write_word(&fx, 014014, 004444);
    mmu_phys_write_word(&fx, 014016, 000000);

    MMU_ASSERT_EQ(core_step(&fx.r), 0, "BPT trap in user mode");

    MMU_ASSERT_EQ(fx.r.r[7], 003333, "vector fetch must use kernel D-space mapping");
    MMU_ASSERT_EQ(fx.r.psw, 030000, "PSW loaded from kernel D-space vector with PM=old mode");

    mmu_fixture_teardown(&fx);
    return 0;
}

static INLINE word mmu_op_fp11(word op11_8, byte ac, word dst)
{
    return 0170000 | ((op11_8 & 017) << 8) | ((ac & 03) << 6) | (dst & 077);
}

static int test_fp11_immediate_uses_i_space(void)
{
    mmu_fixture fx;
    const word fps_value = 000355;

    mmu_set_test("fp11_immediate_uses_i_space");
    mmu_fixture_setup(&fx);

    fx.r.mmu_ssr0 = 000001;
    fx.r.mmu_ssr3 = MMU_SSR3_KD;

    fx.r.mmu_par[0][0][0] = 0100;
    fx.r.mmu_pdr[0][0][0] = 0177006;

    fx.r.mmu_par[0][1][0] = 0120;
    fx.r.mmu_pdr[0][1][0] = 0177006;

    fx.r.r[7] = 000000;

    mmu_phys_write_word(&fx, 010000, mmu_op_fp11(000, 1, mmu_operand(2, 7)));
    mmu_phys_write_word(&fx, 010002, fps_value);
    mmu_phys_write_word(&fx, 012002, 000000);

    MMU_ASSERT_EQ(core_step(&fx.r), 0, "LDFPS immediate step");

    MMU_ASSERT_EQ(fx.r.fpu_fps, fps_value, "FP11 immediate word must come from I-space");
    MMU_ASSERT_EQ(fx.r.r[7], 000004, "LDFPS # should consume one extension word");

    mmu_fixture_teardown(&fx);
    return 0;
}

int main(void)
{
    int failed = 0;

    failed += test_split_id_data_uses_d_space();
    failed += test_vector_fetch_uses_kernel_d_space();
    failed += test_fp11_immediate_uses_i_space();

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
