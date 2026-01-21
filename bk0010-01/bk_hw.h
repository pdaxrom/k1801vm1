#ifndef BK_HW_H
#define BK_HW_H

#include "core/core.h"

#define BK_VRAM_BASE 040000
#define BK_VRAM_SIZE 040000
#define BK_VRAM_BASE_RP 070000
#define BK_VRAM_SIZE_RP 010000
#define BK_SCREEN_WIDTH 512
#define BK_SCREEN_HEIGHT 256

void bk_hw_connect(regs *r);
void bk_hw_set_rom_segment(const byte *rom, word base, word size);
void bk_hw_reset_state(void);
void bk_hw_handle_key(int ch);
byte *bk_hw_vram_ptr(void);
word bk_hw_vram_base(void);
word bk_hw_vram_size(void);
word bk_hw_shift_reg(void);

#endif
