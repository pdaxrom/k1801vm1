#ifndef BK_HW_H
#define BK_HW_H

#include <stddef.h>

#include "core/core.h"
#include "bk_model.h"

#define BK_VRAM_BASE 040000
#define BK_VRAM_SIZE 040000
#define BK_VRAM_BASE_RP 070000
#define BK_VRAM_SIZE_RP 010000
#define BK_SCREEN_WIDTH 512
#define BK_SCREEN_HEIGHT 256

void bk_hw_connect(regs *r);
size_t bk_hw_required_memory_size(void);
int bk_hw_set_memory(byte *memory, size_t size);
void bk_hw_set_rom_segment(const byte *rom, word base, word size);
int bk_hw_select_model(unsigned int model);
unsigned int bk_hw_model(void);
int bk_hw_load_rom_segment(const byte *rom, uint32_t physical, size_t size);
uint16_t bk_hw_bank_register(void);
uint16_t bk_hw_video_register(void);
void bk_hw_frame_tick(void);
void bk_hw_set_color(int color);
void bk_hw_render_pixels(uint32_t *pixels);
void bk_hw_reset_state(void);
void bk_hw_handle_key(int code);
void bk_hw_set_tick_hz(unsigned int hz);
void bk_hw_tick(void);
void bk_hw_tick_n(unsigned int ticks);
int bk_hw_beeper_on(void);
int bk_hw_beeper_pulse(void);
int bk_hw_tape_set_input(const byte *data, size_t size);
int bk_hw_tape_set_input_named(const byte *data, size_t size, const char *name);
int bk_hw_tape_set_input_raw(const byte *data, size_t size);
void bk_hw_tape_set_output_enabled(int enable);
const byte *bk_hw_tape_output_data(size_t *size);
void bk_hw_tape_output_clear(void);
void bk_hw_tape_rewind(void);
byte *bk_hw_vram_ptr(void);
word bk_hw_vram_base(void);
word bk_hw_vram_size(void);
word bk_hw_shift_reg(void);

#endif
