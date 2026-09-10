/* Existing DCJ11 emulator supplies canonical MMR0 software controls word/byte/RESET readback.
 * No legacy alias, CPU privilege, translation or trap coverage is claimed.
 */
#include <stdio.h>
#include <string.h>
#include "../../core/core.c"

int main(void)
{
    regs r;
    unsigned value, lanes;
    word result;
    memset(&r, 0, sizeof(r));
    r.model = DCJ11;
    for (value = 0; value < 65536; ++value) {
        if (!(value & 4095)) {
            dcj11_reset_instruction_state(&r);
            if (!mmu_io_read_word(&r, 0177572, &result)) return 1;
            printf("1 0 0000 %04x\n", (unsigned)result);
        }
        for (lanes = 0; lanes < 4; ++lanes) {
            if (lanes == 3) {
                if (!mmu_io_write_word(&r, 0177572, (word)value)) return 1;
            } else if (lanes) {
                if (!mmu_io_write_byte(&r, (word)(0177572 + (lanes == 2)),
                                      (byte)(lanes == 2 ? value >> 8 : value))) return 1;
            }
            if (!mmu_io_read_word(&r, 0177572, &result)) return 1;
            printf("0 %x %04x %04x\n", lanes, value, (unsigned)result);
        }
    }
    return 0;
}
