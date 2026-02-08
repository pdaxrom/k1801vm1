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
    fx.r.mmu_ssr3 = 000000;
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
    fx.r.mmu_ssr3 = 0000001; /* KD split I/D */

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

int main(void)
{
    int failed = 0;

    failed += test_ifetch_from_22bit_memory();
    failed += test_data_store_to_22bit_memory();

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
