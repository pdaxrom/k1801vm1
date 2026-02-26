#include "mmu_test_common.h"

#if !defined(ENABLE_MMU)
#define ENABLE_MMU 0
#endif

#if defined(ENABLE_MMU) && (ENABLE_MMU)

#define MMU_TRAP_VECTOR 0000250
#define MMU_SSR0_ENABLE 0000001
#define MMU_SSR0_PROT 0020000
#define MMU_SSR0_LENGTH 0040000
#define MMU_SSR0_NONRES 0100000
#define MMU_SSR0_FREEZE 0160000

static INLINE void setup_mmu_fault_vector(mmu_fixture *fx)
{
    fx->r.mmu_ssr0 = MMU_SSR0_ENABLE;

    /* Enable kernel split I/D so vector fetch uses D-space. */
    fx->r.mmu_ssr3 = 0000004; /* KD (J11 MMR3<KDS>) */

    fx->r.mmu_par[0][1][0] = 0000;
    fx->r.mmu_pdr[0][1][0] = 0177006;

    mmu_phys_write_word(fx, MMU_TRAP_VECTOR + 0, 000400);
    mmu_phys_write_word(fx, MMU_TRAP_VECTOR + 2, 000340);
}

static int test_fault_nonresident_ifetch(void)
{
    mmu_fixture fx;

    mmu_set_test("fault_nonresident_ifetch");
    mmu_fixture_setup(&fx);
    setup_mmu_fault_vector(&fx);

    fx.r.mmu_par[0][0][0] = 0000;
    fx.r.mmu_pdr[0][0][0] = 0000000;
    fx.r.r[7] = 000000;

    MMU_ASSERT_EQ(core_step(&fx.r), 0, "MMU nonresident trap");

    MMU_ASSERT_EQ(fx.r.r[7], 000400, "PC must load MMU trap vector");
    MMU_ASSERT_EQ(fx.r.psw, 000340, "PSW must load MMU trap vector");
    MMU_ASSERT_EQ(fx.r.r[6], 000374, "stack must contain trap frame");
    MMU_ASSERT_EQ(mmu_phys_read_word(&fx, 000374), 000000, "fault PC pushed");
    MMU_ASSERT_EQ(mmu_phys_read_word(&fx, 000376), 000000, "old PSW pushed");

    MMU_ASSERT_EQ(fx.r.mmu_ssr2, 000000, "SSR2 records fault PC");
    MMU_ASSERT_EQ(fx.r.mmu_ssr1, 000000, "MMR1 remains zero for simple fault");
    MMU_ASSERT_TRUE((fx.r.mmu_ssr0 & MMU_SSR0_ENABLE) != 0, "MMR0 enable preserved");
    MMU_ASSERT_TRUE((fx.r.mmu_ssr0 & MMU_SSR0_FREEZE) != 0, "MMR0 freeze/fault latched");
    MMU_ASSERT_TRUE((fx.r.mmu_ssr0 & MMU_SSR0_NONRES) != 0,
                    "MMR0 must contain nonresident fault");

    mmu_fixture_teardown(&fx);
    return 0;
}

static int test_fault_length_on_data_write(void)
{
    mmu_fixture fx;

    mmu_set_test("fault_length_data_write");
    mmu_fixture_setup(&fx);
    setup_mmu_fault_vector(&fx);

    /* I-space seg 0: permissive (instruction fetch). */
    fx.r.mmu_par[0][0][0] = 0000;
    fx.r.mmu_pdr[0][0][0] = 0177006;
    /* D-space seg 1: length=0 triggers length fault on data write to block > 0.
     * Remap seg 1 D-space PAR to physical 0 so VA 020200 -> phys 000200. */
    fx.r.mmu_par[0][1][1] = 0000;
    fx.r.mmu_pdr[0][1][1] = 0000006;
    fx.r.r[7] = 000000;

    /* MOV #1, @#020200 — data write targets segment 1 via D-space */
    mmu_phys_write_word(&fx, 000000,
                        mmu_op_mov(mmu_operand(2, 7), mmu_operand(3, 7)));
    mmu_phys_write_word(&fx, 000002, 000001);
    mmu_phys_write_word(&fx, 000004, 020200);
    mmu_phys_write_word(&fx, 000200, 012345);

    MMU_ASSERT_EQ(core_step(&fx.r), 0, "MMU length trap");

    MMU_ASSERT_EQ(fx.r.r[7], 000400, "PC must load MMU trap handler");
    MMU_ASSERT_TRUE((fx.r.mmu_ssr0 & MMU_SSR0_ENABLE) != 0, "MMR0 enable preserved");
    MMU_ASSERT_TRUE((fx.r.mmu_ssr0 & MMU_SSR0_FREEZE) != 0, "MMR0 freeze/fault latched");
    MMU_ASSERT_TRUE((fx.r.mmu_ssr0 & MMU_SSR0_LENGTH) != 0,
                    "MMR0 must contain length fault");
    MMU_ASSERT_EQ(mmu_phys_read_word(&fx, 000200), 012345,
                  "faulting write must not commit");

    mmu_fixture_teardown(&fx);
    return 0;
}

static int test_fault_protect_on_data_write(void)
{
    mmu_fixture fx;

    mmu_set_test("fault_protect_data_write");
    mmu_fixture_setup(&fx);
    setup_mmu_fault_vector(&fx);

    /* I-space seg 0: permissive (instruction fetch). */
    fx.r.mmu_par[0][0][0] = 0000;
    fx.r.mmu_pdr[0][0][0] = 0177006;
    /* D-space seg 1: read-only (J11 ACF=2) triggers protect fault on data write.
     * Remap seg 1 D-space PAR to physical 0 so VA 020200 -> phys 000200. */
    fx.r.mmu_par[0][1][1] = 0000;
    fx.r.mmu_pdr[0][1][1] = 0177002;
    fx.r.r[7] = 000000;

    /* MOV #1, @#020200 — data write targets segment 1 via D-space */
    mmu_phys_write_word(&fx, 000000,
                        mmu_op_mov(mmu_operand(2, 7), mmu_operand(3, 7)));
    mmu_phys_write_word(&fx, 000002, 000001);
    mmu_phys_write_word(&fx, 000004, 020200);
    mmu_phys_write_word(&fx, 000200, 076543);

    MMU_ASSERT_EQ(core_step(&fx.r), 0, "MMU protection trap");

    MMU_ASSERT_EQ(fx.r.r[7], 000400, "PC must load MMU trap handler");
    MMU_ASSERT_TRUE((fx.r.mmu_ssr0 & MMU_SSR0_ENABLE) != 0, "MMR0 enable preserved");
    MMU_ASSERT_TRUE((fx.r.mmu_ssr0 & MMU_SSR0_FREEZE) != 0, "MMR0 freeze/fault latched");
    MMU_ASSERT_TRUE((fx.r.mmu_ssr0 & MMU_SSR0_PROT) != 0,
                    "MMR0 must contain protection fault");
    MMU_ASSERT_EQ(mmu_phys_read_word(&fx, 000200), 076543,
                  "protected write must not commit");

    mmu_fixture_teardown(&fx);
    return 0;
}

int main(void)
{
    int failed = 0;

    failed += test_fault_nonresident_ifetch();
    failed += test_fault_length_on_data_write();
    failed += test_fault_protect_on_data_write();

    if (failed) {
        return 1;
    }

    printf("PASS: test_mmu_faults\n");
    return 0;
}

#else

int main(void)
{
    printf("SKIP: test_mmu_faults (ENABLE_MMU=0)\n");
    return 0;
}

#endif
