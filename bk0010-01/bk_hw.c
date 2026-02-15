#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bk_hw.h"
#include "core/hardware.h"
#include "bk_tape.h"

#define MEM_SIZE 65536

/* BK-0010-01 system I/O (octal) */
#define BK_KBD_STATUS 0177660
#define BK_KBD_DATA   0177662
#define BK_SHIFT_REG  0177664
#define BK_EXT_PORT   0177714
#define BK_SYS_CTRL   0177716
#define BK_KBD_VECTOR 0000060
#define BK_KBD_VECTOR2 0000274    /* AR2 (ALT) vector for console */
#define BK_ROM_LO     0100000
#define BK_ROM_HI     0177777
#define BK_BUS_VECTOR 0000004

#define CSR_READY 0200
#define SYS_PORT_TAPE_BIT 0040
#define SYS_PORT_TAPE_BIT2 0100
#define SYS_PORT_KEY_BIT  0100
#define SYS_PORT_MOTOR_BIT 0200
#define SYS_PORT_OUT_MASK 0360
#define SYS_PORT_IN_MASK  0370

static byte *mem;
static size_t mem_size;

static byte kbd_status = CSR_READY;
static byte kbd_data = 0;
static word shift_reg = 01330;
static word ext_port = 0177777;
static word sys_ctrl = 0100000;
static byte sys_port_out = 0220;
static byte sys_port_in = 0370;
static byte kbd_irq_pending = 0;
static byte bus_error_pending = 0;
static byte tape_in_bit = 1;
static int beeper_on = 0;
static int beeper_pulse = 0;

static int bk_poll_irq(regs *r, word *vector);

typedef struct {
    const byte *data;
    word base;
    word size;
} rom_segment;

static rom_segment roms[2];
static int rom_count;

static INLINE const rom_segment *rom_for_addr(word addr)
{
    uint32_t addr32 = addr;

    for (int i = 0; i < rom_count; i++) {
        const rom_segment *seg = &roms[i];
        if (!seg->data || seg->size == 0) {
            continue;
        }
        uint32_t base = seg->base;
        uint32_t end = base + seg->size;
        if (end <= 0x10000) {
            if (addr32 >= base && addr32 < end) {
                return seg;
            }
        } else {
            if (addr32 >= base || addr32 < (end & 0xFFFF)) {
                return seg;
            }
        }
    }
    return NULL;
}

static INLINE uint32_t rom_offset(const rom_segment *seg, word addr)
{
    uint32_t base = seg->base;
    uint32_t addr32 = addr;
    if (addr32 >= base) {
        return addr32 - base;
    }
    return addr32 + 0x10000 - base;
}

static INLINE int is_rom_addr(word addr)
{
    return addr >= BK_ROM_LO && addr <= BK_ROM_HI;
}

static INLINE int tape_motor_on(void)
{
    return (sys_port_out & SYS_PORT_MOTOR_BIT) == 0;
}

static byte mem_read(word addr)
{
    const rom_segment *seg = rom_for_addr(addr);
    if (seg) {
        return seg->data[rom_offset(seg, addr)];
    }
    if (is_rom_addr(addr)) {
        bus_error_pending = 1;
        return 0;
    }
    return mem[addr];
}

static void mem_write(word addr, byte value)
{
    if (rom_for_addr(addr)) {
        return;
    }
    if (is_rom_addr(addr)) {
        bus_error_pending = 1;
        return;
    }
    mem[addr] = value;
}

static byte bk_load_byte(regs *r, word offset)
{
    switch (offset) {
    case BK_KBD_STATUS:
        return (byte)((kbd_status & CSR_READY) | (kbd_status & 0100));
    case (BK_KBD_STATUS + 1):
        return 0;
    case BK_KBD_DATA:
        kbd_status &= (byte)~CSR_READY;
        sys_port_in |= SYS_PORT_KEY_BIT;
        kbd_irq_pending = 0;
        return kbd_data & 0177;
    case (BK_KBD_DATA + 1):
        return 0;
    case BK_SHIFT_REG:
        return (byte)(shift_reg & 0377);
    case (BK_SHIFT_REG + 1):
        return (byte)(shift_reg >> 8) & 02;
    case BK_EXT_PORT:
        return (byte)(ext_port & 0377);
    case (BK_EXT_PORT + 1):
        return (byte)(ext_port >> 8);
    case BK_SYS_CTRL: {
        word value = (word)(sys_port_in & SYS_PORT_IN_MASK);
        return (byte)(value & 0377);
    }
    case (BK_SYS_CTRL + 1): {
        word value = sys_ctrl;
        return (byte)(value >> 8);
    }
    default:
        if (offset >= 0177600) {
            bus_error_pending = 1;
            return 0;
        }
        return mem_read(offset);
    }
}

static void bk_store_byte(regs *r, word offset, byte value)
{
    switch (offset) {
    case BK_KBD_STATUS:
        kbd_status = (byte)((kbd_status & CSR_READY) | (value & 0100));
        break;
    case (BK_KBD_STATUS + 1):
        break;
    case BK_KBD_DATA:
        kbd_data = value;
        kbd_status |= CSR_READY;
        break;
    case (BK_KBD_DATA + 1):
        break;
    case BK_SHIFT_REG:
        shift_reg = (word)((shift_reg & 0177400) | value);
        break;
    case (BK_SHIFT_REG + 1):
        shift_reg = (word)((shift_reg & 0377) | (((word)value << 8) & 01000));
        break;
    case BK_EXT_PORT:
        ext_port = (word)((ext_port & 0177400) | value);
        break;
    case (BK_EXT_PORT + 1):
        ext_port = (word)((ext_port & 0377) | ((word)value << 8));
        break;
    case BK_SYS_CTRL:
        sys_port_out = (byte)(value & SYS_PORT_OUT_MASK);
        beeper_on = (value & SYS_PORT_TAPE_BIT2) ? 1 : 0;
        if (beeper_on) {
            beeper_pulse++;
        }
        bk_tape_write(tape_motor_on(),
                      (value & SYS_PORT_TAPE_BIT2) ? 1 : 0);
        break;
    case (BK_SYS_CTRL + 1):
        break;
    default:
        if (offset >= 0177600) {
            bus_error_pending = 1;
            break;
        }
        mem_write(offset, value);
        break;
    }
}

static word bk_load_word(regs *r, word offset)
{
    byte lo = bk_load_byte(r, offset);
    byte hi = bk_load_byte(r, (word)(offset + 1));
    return (word)(lo | (hi << 8));
}

static void bk_store_word(regs *r, word offset, word value)
{
    bk_store_byte(r, offset, (byte)(value & 0377));
    bk_store_byte(r, (word)(offset + 1), (byte)(value >> 8));
}

static int bk_init(regs *r)
{
    (void)r;
    if (!mem || mem_size < MEM_SIZE) {
        return -1;
    }
    memset(mem, 0, MEM_SIZE);
    bk_tape_init();
    return 0;
}

static void bk_reset(regs *r)
{
    kbd_status = CSR_READY;
    kbd_data = 0;
    shift_reg = 01330;
    ext_port = 0177777;
    sys_ctrl = 0100000;
    sys_port_out = 0220;
    sys_port_in = 0370;
    kbd_irq_pending = 0;
    bus_error_pending = 0;
    beeper_on = 0;
    beeper_pulse = 0;
//    sys_ctrl = 0160000;
    bk_tape_reset();
}

static void bk_fini(regs *r)
{
    (void)r;
    mem = NULL;
    mem_size = 0;
    bk_tape_set_input(NULL, 0);
    bk_tape_output_clear();
}

static byte *bk_ramptr(regs *r, word offset)
{
    (void)r;
    return &mem[offset];
}

void bk_hw_connect(regs *r)
{
    r->load_byte = bk_load_byte;
    r->store_byte = bk_store_byte;
    r->load_word = bk_load_word;
    r->store_word = bk_store_word;
    r->init = bk_init;
    r->reset = bk_reset;
    r->fini = bk_fini;
    r->poll_irq = bk_poll_irq;
    r->ramptr = bk_ramptr;
}

size_t bk_hw_required_memory_size(void)
{
    return MEM_SIZE;
}

int bk_hw_set_memory(byte *memory, size_t size)
{
    if (!memory || size < MEM_SIZE) {
        mem = NULL;
        mem_size = 0;
        return -1;
    }
    mem = memory;
    mem_size = size;
    return 0;
}

void bk_hw_set_rom_segment(const byte *rom, word base, word size)
{
    if (!rom || size == 0 || rom_count >= (int)(sizeof(roms) / sizeof(roms[0]))) {
        return;
    }
    roms[rom_count].data = rom;
    roms[rom_count].base = base;
    roms[rom_count].size = size;
    rom_count++;
}

void bk_hw_reset_state(void)
{
    kbd_status = CSR_READY;
    kbd_data = 0;
    shift_reg = 01330;
    ext_port = 0177777;
    sys_ctrl = 0100000;
    sys_port_out = 0220;
    sys_port_in = 0370;
    kbd_irq_pending = 0;
    bus_error_pending = 0;
    bk_tape_reset();
}

int bk_hw_tape_set_input(const byte *data, size_t size)
{
    return bk_tape_set_input_bin(data, size, NULL);
}

int bk_hw_tape_set_input_named(const byte *data, size_t size, const char *name)
{
    return bk_tape_set_input_bin(data, size, name);
}

int bk_hw_tape_set_input_raw(const byte *data, size_t size)
{
    return bk_tape_set_input(data, size);
}

void bk_hw_tape_set_output_enabled(int enable)
{
    bk_tape_set_output_enabled(enable);
}

const byte *bk_hw_tape_output_data(size_t *size)
{
    return bk_tape_output_data(size);
}

void bk_hw_tape_output_clear(void)
{
    bk_tape_output_clear();
}

void bk_hw_tape_rewind(void)
{
    bk_tape_rewind();
}

void bk_hw_handle_key(int code)
{
    if (code < 0) {
        return;
    }
    kbd_data = (byte)code;
    kbd_status |= CSR_READY;
    sys_port_in &= (byte)~SYS_PORT_KEY_BIT;
    if ((kbd_status & 0100) == 0) {
        kbd_irq_pending = 1;
    }
}

void bk_hw_set_tick_hz(unsigned int hz)
{
    bk_tape_set_tick_hz(hz);
}

void bk_hw_tick(void)
{
    bk_tape_tick();
    tape_in_bit = (byte)bk_tape_read();
    if (tape_in_bit) {
        sys_port_in |= SYS_PORT_TAPE_BIT;
    } else {
        sys_port_in &= (byte)~SYS_PORT_TAPE_BIT;
    }
}

void bk_hw_tick_n(unsigned int ticks)
{
    while (ticks--) {
        bk_hw_tick();
    }
}

int bk_hw_beeper_on(void)
{
    return beeper_on;
}

int bk_hw_beeper_pulse(void)
{
    int v = beeper_pulse;
    beeper_pulse = 0;
    return v;
}

byte *bk_hw_vram_ptr(void)
{
    if (!mem) {
        return NULL;
    }
    return &mem[bk_hw_vram_base()];
}

static int bk_poll_irq(regs *r, word *vector)
{
    if (is_rom_addr(r->r[7]) && !rom_for_addr(r->r[7])) {
        bus_error_pending = 0;
        if (vector) {
            *vector = BK_BUS_VECTOR;
        }
        return 1;
    }
    if (bus_error_pending) {
        bus_error_pending = 0;
        if (vector) {
            *vector = BK_BUS_VECTOR;
        }
        return 1;
    }
    if (kbd_irq_pending && (kbd_status & CSR_READY) && ((kbd_status & 0100) == 0)) {
        kbd_irq_pending = 0;
        if (vector) {
            *vector = (kbd_data & 0200) ? BK_KBD_VECTOR2 : BK_KBD_VECTOR;
        }
        return 1;
    }
    return 0;
}

word bk_hw_vram_base(void)
{
    if (shift_reg & 01000) {
        return BK_VRAM_BASE;
    }
    return BK_VRAM_BASE_RP;
}

word bk_hw_vram_size(void)
{
    if (shift_reg & 01000) {
        return BK_VRAM_SIZE;
    }
    return BK_VRAM_SIZE_RP;
}

word bk_hw_shift_reg(void)
{
    return shift_reg;
}
