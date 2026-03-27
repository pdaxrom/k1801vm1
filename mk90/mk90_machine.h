#ifndef MK90_MACHINE_H
#define MK90_MACHINE_H

#include "../core/core.h"
#include "mk90_defs.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    byte *data;
    size_t size;
    uint32_t position;
    uint32_t mask;
    byte cmd;
    int present;
    int dirty;
    char path[MK90_IMAGE_PATH_MAX];
} mk90_smp_slot_t;

typedef struct {
    word base;
    word config;
} mk90_lcd_state_t;

typedef struct {
    byte data[64];
    uint32_t second_accum_ms;
    uint32_t periodic_accum_ms;
} mk90_rtc_state_t;

typedef struct {
    word regs[4];
    word shiftreg;
    word rgrq;
    int select_active;
    int beeper_level;
} mk90_io_state_t;

typedef struct {
    word rg1;
    word rg2;
} mk90_syscon_state_t;

typedef struct {
    word scan_code;
} mk90_keyboard_state_t;

typedef struct {
    byte evnt;
    byte inrt;
    byte data_ready;
    byte keyboard;
} mk90_irq_state_t;

typedef struct mk90_state {
    regs *cpu;
    byte ram[MK90_RAM_MAX_SIZE];
    byte rom[MK90_ROM_SIZE];
    word ram_size;
    word ram_end;
    int cold_reset_pending;
    int trace;
    mk90_lcd_state_t lcd;
    mk90_rtc_state_t rtc;
    mk90_io_state_t io;
    mk90_syscon_state_t syscon;
    mk90_keyboard_state_t keyboard;
    mk90_irq_state_t irq;
    mk90_smp_slot_t smp[2];
} mk90_state_t;

mk90_state_t *mk90_machine_state(void);

void mk90_machine_connect(regs *r);
void mk90_machine_set_trace(int on);
int mk90_machine_load_images(const char *rom_path, const char *romt_path,
                             const char *smp0_path, const char *smp1_path,
                             char *err, size_t err_len);
void mk90_machine_tick_ms(uint32_t elapsed_ms);
void mk90_machine_key_press(word scan_code);
void mk90_machine_key_release(void);
void mk90_machine_render(uint32_t *pixels, int pitch_pixels);
word mk90_machine_lcd_base(void);
byte mk90_machine_ram_peek(word addr);

void mk90_machine_raise_evnt(mk90_state_t *state);
void mk90_machine_raise_inrt(mk90_state_t *state);
void mk90_machine_raise_data_ready(mk90_state_t *state);
void mk90_machine_raise_keyboard(mk90_state_t *state);
void mk90_machine_tracef(const mk90_state_t *state, const char *fmt, ...);
void mk90_machine_sync_fast_ram(mk90_state_t *state);

#endif
