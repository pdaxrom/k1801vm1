#include "mk90_machine.h"

#include "mk90_io.h"
#include "mk90_keyboard.h"
#include "mk90_lcd.h"
#include "mk90_rtc.h"
#include "mk90_smp.h"
#include "mk90_syscon.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static mk90_state_t g_mk90;

static int mk90_load_file_into(byte *dst, size_t max_size, const char *path,
                               char *err, size_t err_len)
{
    FILE *fp;
    size_t read_count;

    fp = fopen(path, "rb");
    if (!fp) {
        if (err && err_len) {
            snprintf(err, err_len, "Failed to open %s", path);
        }
        return -1;
    }
    read_count = fread(dst, 1, max_size, fp);
    fclose(fp);
    (void)read_count;
    return 0;
}

mk90_state_t *mk90_machine_state(void)
{
    return &g_mk90;
}

void mk90_machine_tracef(const mk90_state_t *state, const char *fmt, ...)
{
    va_list ap;

    if (!state || !state->trace) {
        return;
    }

    va_start(ap, fmt);
    fputs("mk90: ", stderr);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

void mk90_machine_raise_evnt(mk90_state_t *state)
{
    state->irq.evnt = 1;
}

void mk90_machine_raise_c0(mk90_state_t *state)
{
    state->irq.c0 = 1;
}

void mk90_machine_raise_c4(mk90_state_t *state)
{
    state->irq.c4 = 1;
}

void mk90_machine_raise_c8(mk90_state_t *state)
{
    state->irq.c8 = 1;
}

static byte mk90_read_byte(regs *r, word address)
{
    mk90_state_t *state = mk90_machine_state();

    (void)r;
    if (mk90_syscon_rd_ram(state, address)) {
        if (address <= state->ram_end) {
            return state->ram[address];
        }
        if (address >= MK90_LCD_BASE && address <= MK90_LCD_BASE + 7u) {
            return mk90_lcd_read_byte(state, (word)(address - MK90_LCD_BASE));
        }
        if (address >= MK90_IO_BASE && address <= MK90_IO_BASE + 7u) {
            return mk90_io_read_byte(state, (word)(address - MK90_IO_BASE));
        }
        if (address >= MK90_SYS_BASE && address <= MK90_SYS_BASE + 3u) {
            return mk90_syscon_read_byte(state, (word)(address - MK90_SYS_BASE));
        }
        if (address >= MK90_RTC_BASE && address < MK90_RTC_BASE + 0200u) {
            return mk90_rtc_read_byte(state, (word)(address - MK90_RTC_BASE));
        }
        return 0377u;
    }

    if (mk90_syscon_rd_rom(state, address) &&
        address >= MK90_ROM_START &&
        (size_t)(address - MK90_ROM_START) < sizeof(state->rom)) {
        return state->rom[address - MK90_ROM_START];
    }

    return 0377u;
}

static void mk90_write_byte(regs *r, word address, byte value)
{
    mk90_state_t *state = mk90_machine_state();

    (void)r;
    if (!mk90_syscon_wr_ram(state, address)) {
        return;
    }

    if (address <= state->ram_end) {
        state->ram[address] = value;
        return;
    }
    if (address >= MK90_LCD_BASE && address <= MK90_LCD_BASE + 7u) {
        mk90_lcd_write_byte(state, (word)(address - MK90_LCD_BASE), value);
        return;
    }
    if (address >= MK90_IO_BASE && address <= MK90_IO_BASE + 7u) {
        mk90_io_write_byte(state, (word)(address - MK90_IO_BASE), value);
        return;
    }
    if (address >= MK90_SYS_BASE && address <= MK90_SYS_BASE + 3u) {
        mk90_syscon_write_byte(state, (word)(address - MK90_SYS_BASE), value);
        return;
    }
    if (address >= MK90_RTC_BASE && address < MK90_RTC_BASE + 0200u) {
        mk90_rtc_write_byte(state, (word)(address - MK90_RTC_BASE), value);
    }
}

static word mk90_read_word(regs *r, word address)
{
    mk90_state_t *state = mk90_machine_state();

    (void)r;
    if (mk90_syscon_rd_ram(state, address)) {
        if ((word)(address + 1u) <= state->ram_end) {
            return (word)(state->ram[address] |
                          ((word)state->ram[address + 1u] << 8));
        }
        if ((address & 1u) == 0u) {
            if (address >= MK90_LCD_BASE && address <= MK90_LCD_BASE + 6u) {
                word value = mk90_lcd_read_word(state, (word)(address - MK90_LCD_BASE));
                mk90_machine_tracef(state, "bus lcd rd [%06o] -> %06o\n",
                                    address, value);
                return value;
            }
            if (address >= MK90_IO_BASE && address <= MK90_IO_BASE + 6u) {
                word value = mk90_io_read_word(state, (word)(address - MK90_IO_BASE));
                mk90_machine_tracef(state, "bus io rd [%06o] -> %06o\n",
                                    address, value);
                return value;
            }
            if (address >= MK90_SYS_BASE && address <= MK90_SYS_BASE + 2u) {
                word value = mk90_syscon_read_word(state, (word)(address - MK90_SYS_BASE));
                mk90_machine_tracef(state, "bus sys rd [%06o] -> %06o\n",
                                    address, value);
                return value;
            }
            if (address >= MK90_RTC_BASE && address < MK90_RTC_BASE + 0177u) {
                word value = mk90_rtc_read_word(state, (word)(address - MK90_RTC_BASE));
                mk90_machine_tracef(state, "bus rtc rd [%06o] -> %06o\n",
                                    address, value);
                return value;
            }
        }
        return (word)(mk90_read_byte(r, address) |
                      ((word)mk90_read_byte(r, (word)(address + 1u)) << 8));
    }

    if (mk90_syscon_rd_rom(state, address) &&
        address >= MK90_ROM_START &&
        (size_t)(address - MK90_ROM_START + 1u) < sizeof(state->rom)) {
        size_t index = (size_t)(address - MK90_ROM_START);
        return (word)(state->rom[index] | ((word)state->rom[index + 1u] << 8));
    }

    return (word)(mk90_read_byte(r, address) |
                  ((word)mk90_read_byte(r, (word)(address + 1u)) << 8));
}

static void mk90_write_word(regs *r, word address, word value)
{
    mk90_state_t *state = mk90_machine_state();

    (void)r;
    if (!mk90_syscon_wr_ram(state, address)) {
        return;
    }

    if ((word)(address + 1u) <= state->ram_end) {
        state->ram[address] = (byte)(value & 0377u);
        state->ram[address + 1u] = (byte)((value >> 8) & 0377u);
        return;
    }
    if ((address & 1u) == 0u) {
        if (address >= MK90_LCD_BASE && address <= MK90_LCD_BASE + 6u) {
            mk90_machine_tracef(state, "bus lcd wr [%06o] <= %06o\n",
                                address, value);
            mk90_lcd_write_word(state, (word)(address - MK90_LCD_BASE), value);
            return;
        }
        if (address >= MK90_IO_BASE && address <= MK90_IO_BASE + 6u) {
            mk90_machine_tracef(state, "bus io wr [%06o] <= %06o\n",
                                address, value);
            mk90_io_write_word(state, (word)(address - MK90_IO_BASE), value);
            return;
        }
        if (address >= MK90_SYS_BASE && address <= MK90_SYS_BASE + 2u) {
            mk90_machine_tracef(state, "bus sys wr [%06o] <= %06o\n",
                                address, value);
            mk90_syscon_write_word(state, (word)(address - MK90_SYS_BASE), value);
            return;
        }
        if (address >= MK90_RTC_BASE && address < MK90_RTC_BASE + 0177u) {
            mk90_machine_tracef(state, "bus rtc wr [%06o] <= %06o\n",
                                address, value);
            mk90_rtc_write_word(state, (word)(address - MK90_RTC_BASE), value);
            return;
        }
    }

    mk90_write_byte(r, address, (byte)(value & 0377u));
    mk90_write_byte(r, (word)(address + 1u), (byte)((value >> 8) & 0377u));
}

static int mk90_init(regs *r)
{
    mk90_state_t *state = mk90_machine_state();

    state->cpu = r;
    memset(state->ram, 0, sizeof(state->ram));
    return 0;
}

static void mk90_reset(regs *r)
{
    mk90_state_t *state = mk90_machine_state();
    int cold_reset;

    (void)r;
    cold_reset = state->cold_reset_pending;
    state->cold_reset_pending = 0;

    mk90_lcd_reset(state);
    mk90_keyboard_reset(state);
    mk90_syscon_reset(state);
    mk90_smp_reset(state);
    mk90_io_reset(state);
    if (cold_reset) {
        mk90_rtc_reset(state);
    }
    memset(&state->irq, 0, sizeof(state->irq));
    state->cpu->SEL0 = MK90_RESET_PC;
}

static void mk90_fini(regs *r)
{
    mk90_state_t *state = mk90_machine_state();

    (void)r;
    mk90_smp_close(state);
}

static int mk90_poll_irq(regs *r, word *vector)
{
    mk90_state_t *state = mk90_machine_state();

    if (r->psw & FLAG_P) {
        return 0;
    }
    if (state->irq.evnt) {
        state->irq.evnt = 0;
        if (vector) {
            *vector = MK90_VEC_EVNT;
        }
        return 1;
    }
    if (state->irq.c0) {
        state->irq.c0 = 0;
        if (vector) {
            *vector = MK90_VEC_C0;
        }
        return 1;
    }
    if (state->irq.c4) {
        state->irq.c4 = 0;
        if (vector) {
            *vector = MK90_VEC_C4;
        }
        return 1;
    }
    if (state->irq.c8) {
        state->irq.c8 = 0;
        if (vector) {
            *vector = MK90_VEC_C8;
        }
        return 1;
    }
    return 0;
}

void mk90_machine_connect(regs *r)
{
    mk90_state_t *state = mk90_machine_state();

    memset(state, 0, sizeof(*state));
    state->cpu = r;
    state->ram_size = MK90_RAM_MIN_SIZE;
    state->ram_end = (word)(state->ram_size - 1u);
    state->cold_reset_pending = 1;

    r->load_byte = mk90_read_byte;
    r->store_byte = mk90_write_byte;
    r->load_word = mk90_read_word;
    r->store_word = mk90_write_word;
    r->load_byte_pa = NULL;
    r->store_byte_pa = NULL;
    r->load_word_pa = NULL;
    r->store_word_pa = NULL;
    r->init = mk90_init;
    r->reset = mk90_reset;
    r->fini = mk90_fini;
    r->poll_irq = mk90_poll_irq;
    r->ramptr = NULL;
    r->ram_fast = NULL;
    r->ram_fast_size = 0;
}

void mk90_machine_set_trace(int on)
{
    mk90_machine_state()->trace = on ? 1 : 0;
}

int mk90_machine_load_images(const char *rom_path, const char *romt_path,
                             const char *smp0_path, const char *smp1_path,
                             char *err, size_t err_len)
{
    mk90_state_t *state = mk90_machine_state();

    memset(state->rom, 0377, sizeof(state->rom));
    if (romt_path && romt_path[0] != '\0' &&
        mk90_load_file_into(&state->rom[0], MK90_ROM_TEST_SIZE, romt_path,
                            err, err_len) != 0) {
        return -1;
    }
    if (!rom_path || rom_path[0] == '\0') {
        if (err && err_len) {
            snprintf(err, err_len, "ROM path is required");
        }
        return -1;
    }
    if (mk90_load_file_into(&state->rom[MK90_ROM_MAIN_OFFSET], MK90_ROM_MAIN_MAX,
                            rom_path, err, err_len) != 0) {
        return -1;
    }
    if (mk90_smp_load(state, 0u, smp0_path, err, err_len) != 0) {
        return -1;
    }
    if (mk90_smp_load(state, 1u, smp1_path, err, err_len) != 0) {
        return -1;
    }
    return 0;
}

void mk90_machine_tick_ms(uint32_t elapsed_ms)
{
    mk90_rtc_tick_ms(mk90_machine_state(), elapsed_ms);
}

void mk90_machine_key_press(word scan_code)
{
    mk90_state_t *state = mk90_machine_state();

    if (scan_code == 0) {
        return;
    }
    mk90_keyboard_press(state, scan_code);
    mk90_io_key_irq(state);
}

void mk90_machine_key_release(void)
{
    mk90_keyboard_release(mk90_machine_state());
}

void mk90_machine_render(uint32_t *pixels, int pitch_pixels)
{
    mk90_lcd_render(mk90_machine_state(), pixels, pitch_pixels);
}

byte mk90_machine_ram_peek(word addr)
{
    mk90_state_t *state = mk90_machine_state();

    if (addr <= state->ram_end) {
        return state->ram[addr];
    }
    return 0377u;
}
