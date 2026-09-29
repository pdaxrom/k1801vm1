#include "mmu_test_common.h"

#if defined(ENABLE_MMU) && ENABLE_MMU
/* Exercise actual instructions, including the separate previous-space path.
 * Expected bits come from the PDR access code and length rule, independently
 * of the core's translation helpers. Check first-fault freeze afterwards. */
static int combined_fault(int acf, int down, int operation)
{
	mmu_fixture fx;
	const word va = 020200;
	const word pc = 01000;
	int previous = operation >= 4;
	int writing = operation == 1 || operation == 3 || operation == 5 || operation == 7;
	int mode = previous ? 3 : 0;
	int space = previous ? operation >= 6 : 1;
	word expected = 0040000 | (writing && acf == 2 ? 0020000 : acf == 0 || acf == 4 ? 0100000 : 0);
	word pdr = acf | (down ? 0001410 : 0); /* block 2: above len 0 or below len 3 */
	word instruction;
	word frozen[3];

	mmu_set_test("combined_length_access_abort");
	mmu_fixture_setup(&fx);
	fx.r.mmu_ssr0 = 1;
	fx.r.mmu_ssr3 = 5; /* kernel and user split I/D */
	fx.r.psw = previous ? 030000 : 0;
	fx.r.r[6] = 04000;
	fx.r.r[7] = pc;
	fx.r.r[0] = va;
	fx.r.r[1] = 012345;
	fx.r.mmu_pdr[0][0][0] = 077406;
	fx.r.mmu_pdr[0][1][0] = 077406;
	fx.r.mmu_pdr[mode][space][1] = pdr;
	fx.r.mmu_par[mode][space][1] = 0200;
	mmu_phys_write_word(&fx, 0250, 0600);
	mmu_phys_write_word(&fx, 0252, 0340);
	mmu_phys_write_word(&fx, 04000, 012345);
	mmu_phys_write_word(&fx, 020200, 065432);
	if (previous) {
		instruction = (space ? 0100000 : 0) | (writing ? 006600 : 006500) | 010;
	} else {
		instruction = writing ? 010110 : 011001; /* MOV R1,(R0) or MOV (R0),R1 */
		if (operation >= 2) {
			instruction |= 0100000;
		}
	}
	mmu_phys_write_word(&fx, pc, instruction);
	MMU_ASSERT_EQ(core_step(&fx.r), 0, "instruction abort");
	MMU_ASSERT_EQ(fx.r.r[7], 0600, "MMU vector taken");
	MMU_ASSERT_EQ(fx.r.mmu_ssr0, expected | 1 | (mode << 5) | (space << 4) | 2,
	              "all simultaneous causes and original page must be latched");
	MMU_ASSERT_EQ(fx.r.mmu_ssr2, pc, "faulting instruction PC");
	MMU_ASSERT_EQ(fx.r.mmu_pdr[mode][space][1] & 0100, writing ? 0100 : 0, "J11 PDR.W on abort");
	MMU_ASSERT_EQ(mmu_phys_read_word(&fx, 020200), 065432, "fault must not write memory");
	frozen[0] = fx.r.mmu_ssr0;
	frozen[1] = fx.r.mmu_ssr1;
	frozen[2] = fx.r.mmu_ssr2;
	/* A second abort in the handler must not replace or extend the first. */
	fx.r.r[0] = 040200;
	fx.r.mmu_pdr[0][1][2] = 0;
	mmu_phys_write_word(&fx, 0600, 011001);
	MMU_ASSERT_EQ(core_step(&fx.r), 0, "clear abort latch");
	MMU_ASSERT_EQ(core_step(&fx.r), 0, "second abort");
	MMU_ASSERT_EQ(fx.r.mmu_ssr0, frozen[0], "MMR0 frozen across second abort");
	MMU_ASSERT_EQ(fx.r.mmu_ssr1, frozen[1], "MMR1 frozen across second abort");
	MMU_ASSERT_EQ(fx.r.mmu_ssr2, frozen[2], "MMR2 frozen across second abort");
	mmu_fixture_teardown(&fx);
	return 0;
}
#endif

int main(void)
{
#if defined(ENABLE_MMU) && ENABLE_MMU
	int failed = 0;
	for (int acf = 0; acf <= 6; acf += 2) {
		for (int down = 0; down < 2; down++) {
			for (int op = 0; op < 8; op++) {
				failed += combined_fault(acf, down, op);
			}
		}
	}
	if (failed) {
		return 1;
	}
	puts("PASS: test_mmu_combined (64 cases, MOV/MOVB/MFPI/MTPI/MFPD/MTPD, freeze)");
#else
	puts("SKIP: test_mmu_combined (ENABLE_MMU=0)");
#endif
	return 0;
}
