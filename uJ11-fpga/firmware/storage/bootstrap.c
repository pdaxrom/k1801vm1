#include "storage.h"
#include "../../build/hc7000-mmu-iop/bootstrap.h"

/* The board holds J11 in reset until every bootstrap word reaches SRAM.
 * This runs again on board reset, but never on a guest RESET instruction. */
void bootstrap(void)
{
	DMA_MODE = 1;
	for (unsigned i = 0; i < sizeof(j11_bootstrap) / sizeof(j11_bootstrap[0]); i++) {
		DMA_ADDR = 004000 + 2 * i;
		DIRECT = j11_bootstrap[i];
		ENGINE = 7;
		u32 status;
		while ((status = ENGINE) & 1) {
		}
		if (status & 14) {
			/* Do not execute a partially installed bootstrap. */
			for (;;) {
			}
		}
	}
	CPU_START = 1;
}
