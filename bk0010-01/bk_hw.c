#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bk_hw.h"

#define MEM_SIZE 65536

/* PDP-11 compatible console addresses (octal) */
#define BK_RCSR 0177560
#define BK_RBUF 0177562
#define BK_TCSR 0177564
#define BK_TBUF 0177566

/* BK-0010-01 system I/O (octal) */
#define BK_KBD_STATUS 0177660
#define BK_KBD_DATA   0177662
#define BK_SHIFT_REG  0177664
#define BK_EXT_PORT   0177714
#define BK_SYS_CTRL   0177716
#define BK_KBD_VECTOR 000060
#define BK_ROM_LO     0100000
#define BK_ROM_HI     0177777
#define BK_BUS_VECTOR 000004

#define CSR_READY 0200

static byte *mem;
static byte rcsr = CSR_READY;
static byte rbuf = 0;

static byte kbd_status = CSR_READY;
static byte kbd_data = 0;
static word shift_reg = 01330;
static word ext_port = 0177777;
static word sys_ctrl = 0100000;
static byte sys_port_out = 0;
static byte sys_port_in = 0170;
static byte kbd_irq_pending = 0;
static byte bus_error_pending = 0;

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
    (void)r;
    switch (offset) {
    case BK_RCSR:
        return rcsr;
    case BK_RBUF:
        rcsr &= (byte)~CSR_READY;
        return rbuf;
    case BK_TCSR:
        return CSR_READY;
    case BK_TBUF:
        return 0;
    case BK_KBD_STATUS:
        return (byte)((kbd_status & CSR_READY) | (kbd_status & 0100));
    case BK_KBD_DATA:
        kbd_status &= (byte)~CSR_READY;
        sys_port_in |= 040;
        kbd_irq_pending = 0;
        return kbd_data;
    case BK_SHIFT_REG:
        return (byte)(shift_reg & 0377);
    case (BK_SHIFT_REG + 1):
        return (byte)(shift_reg >> 8) & 02;
    case BK_EXT_PORT:
        return (byte)(ext_port & 0377);
    case (BK_EXT_PORT + 1):
        return (byte)(ext_port >> 8);
    case BK_SYS_CTRL: {
        word value = sys_ctrl;
        value &= 0177400;
        value |= (word)(sys_port_in & 0170);
        return (byte)(value & 0377);
    }
    case (BK_SYS_CTRL + 1): {
        word value = sys_ctrl;
        value &= 0177400;
        value |= (word)(sys_port_in & 0170);
        return (byte)(value >> 8);
    }
    default:
        if (offset >= 0177600) {
            return 0;
        }
        return mem_read(offset);
    }
}

static void bk_store_byte(regs *r, word offset, byte value)
{
    (void)r;
    switch (offset) {
    case BK_RCSR:
        rcsr = value;
        break;
    case BK_RBUF:
        rbuf = value;
        rcsr |= CSR_READY;
        break;
    case BK_TCSR:
        break;
    case BK_TBUF:
        putchar(value);
        fflush(stdout);
        break;
    case BK_KBD_STATUS:
        kbd_status = (byte)((kbd_status & CSR_READY) | (value & 0100));
        break;
    case BK_KBD_DATA:
        kbd_data = value;
        kbd_status |= CSR_READY;
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
        sys_port_out = (byte)(value & 0170);
        break;
    case (BK_SYS_CTRL + 1):
        sys_port_out = (byte)(value & 0170);
        break;
    default:
        if (offset >= 0177600) {
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
    if (!mem) {
        mem = (byte *)malloc(MEM_SIZE);
        if (!mem) {
            return -1;
        }
    }
    memset(mem, 0, MEM_SIZE);
    return 0;
}

static void bk_reset(regs *r)
{
    (void)r;
    rcsr = CSR_READY;
    rbuf = 0;
    kbd_status = CSR_READY;
    kbd_data = 0;
    shift_reg = 01330;
    ext_port = 0177777;
    sys_ctrl = 0100000;
    sys_port_out = 0;
    sys_port_in = 0170;
    kbd_irq_pending = 0;
    bus_error_pending = 0;
}

static void bk_fini(regs *r)
{
    (void)r;
    if (mem) {
        free(mem);
        mem = NULL;
    }
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
    rcsr = CSR_READY;
    rbuf = 0;
    kbd_status = CSR_READY;
    kbd_data = 0;
    shift_reg = 01330;
    ext_port = 0177777;
    sys_ctrl = 0100000;
    sys_port_out = 0;
    sys_port_in = 0170;
    kbd_irq_pending = 0;
    bus_error_pending = 0;
}

void bk_hw_handle_key(int code)
{
    if (code < 0) {
        return;
    }
    rbuf = (byte)code;
    rcsr |= CSR_READY;
    kbd_data = (byte)code;
    kbd_status |= CSR_READY;
    sys_port_in &= (byte)~040;
    if ((kbd_status & 0100) == 0) {
        kbd_irq_pending = 1;
    }
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
            *vector = BK_KBD_VECTOR;
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
