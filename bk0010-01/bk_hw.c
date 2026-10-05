#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bk_hw.h"
#include "core/core.h"
#include "core/hardware.h"
#include "bk_tape.h"
#include "bk_palettes.h"
#include "bk_fdd.h"

#define MEM_SIZE 0x40000

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
#define SYS_PORT_OUT_BEEPER_BIT 0100  /* write: tape data out / beeper */
#define SYS_PORT_IN_KEY_BIT     0100  /* read:  keyboard data available */
#define SYS_PORT_MOTOR_BIT 0200
#define SYS_PORT_OUT_MASK 0360
#define SYS_PORT_IN_MASK  0374

static byte *mem;
static size_t mem_size;
static unsigned int selected_model = BK_MODEL_BASIC;
static bk_mapper mapper = {.model = BK_MODEL_BASIC, .a16_mode = 0160};
static word video_reg = 040000;
static byte frame_irq_pending;
static int color_output;
static unsigned int cpu_mhz = 3;

static int fdd_selected(void)
{
	return selected_model == BK_MODEL_A16M || selected_model == BK_MODEL_11M_FDD;
}

static byte kbd_status = CSR_READY;
static byte kbd_data = 0;
static word shift_reg = 01330;
static word ext_port = 0177777;
static word sys_ctrl = 0100000;
static byte sys_port_out = 0220;
static byte sys_port_in = 0370;
static byte kbd_irq_pending = 0;
static byte tape_in_bit = 1;
static int beeper_on = 0;
static int beeper_pulse = 0;

static int bk_poll_irq(regs *r, word *vector);

typedef struct {
	const byte *data;
	uint32_t base;
	size_t size;
} rom_segment;

static rom_segment roms[16];
static int rom_count;

static INLINE const rom_segment *rom_for_addr(uint32_t addr)
{
	for (int i = 0; i < rom_count; i++) {
		const rom_segment *seg = &roms[i];
		if (addr >= seg->base && addr - seg->base < seg->size) {
			return seg;
		}
	}
	return NULL;
}

static INLINE uint32_t rom_offset(const rom_segment *seg, uint32_t addr)
{
	return addr - seg->base;
}

static INLINE int tape_motor_on(void)
{
	return (sys_port_out & SYS_PORT_MOTOR_BIT) == 0;
}


static byte bk_load_byte(regs *r, word offset)
{
	if (fdd_selected() && (offset & 0177774) == 0177130 &&
	                (selected_model != BK_MODEL_A16M || !(mapper.a16_mode & 4))) {
		word value = bk_fdd_read(offset & 0177776);
		return offset & 1 ? value >> 8 : value;
	}
	switch (offset) {
	case BK_KBD_STATUS:
		return (byte)((kbd_status & CSR_READY) | (kbd_status & 0100));
	case (BK_KBD_STATUS + 1):
		return 0;
	case BK_KBD_DATA:
		kbd_status &= (byte)~CSR_READY;
		sys_port_in |= SYS_PORT_IN_KEY_BIT;
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
		sys_port_in &= ~(1 << 2);
		return (byte)(value & 0377);
	}
	case (BK_SYS_CTRL + 1):
		return (byte)(sys_ctrl >> 8);
	default:
		if (offset >= 0177600) {
			core_bus_error_trap(r);
			return 0;
		}
		{
			uint32_t physical;
			if (!bk_mapper_address(&mapper, offset, 0, &physical)) {
				core_bus_error_trap(r);
				return 0;
			}
			if (physical >= BK_ROM_BASE && physical < BK_A16M_RAM) {
				const rom_segment *seg = rom_for_addr(physical);
				if (!seg) {
					core_bus_error_trap(r);
					return 0;
				}
				return seg->data[rom_offset(seg, physical)];
			}
			return mem[physical];
		}
	}
}

static void bk_store_byte(regs *r, word offset, byte value)
{
	if (fdd_selected() && (offset & 0177774) == 0177130) {
		word data = offset & 1 ? (word)value << 8 : value;
		bk_fdd_write(offset & 0177776, data);
		return;
	}
	switch (offset) {
	case BK_KBD_STATUS:
		kbd_status = (byte)((kbd_status & CSR_READY) | (value & 0100));
		break;
	case (BK_KBD_STATUS + 1):
		break;
	case BK_KBD_DATA:
		if (selected_model < BK_MODEL_11M_FDD) {
			core_bus_error_trap(r);
		}
		break;
	case (BK_KBD_DATA + 1):
		if (selected_model < BK_MODEL_11M_FDD) {
			core_bus_error_trap(r);
		} else {
			video_reg = ((word)value << 8) & 0147400;
			if (video_reg & 040000) {
				frame_irq_pending = 0;
			}
		}
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
		beeper_on = (value & SYS_PORT_OUT_BEEPER_BIT) ? 1 : 0;
		if (beeper_on) {
			beeper_pulse++;
		}
		bk_tape_write(tape_motor_on(),
		              (value & SYS_PORT_OUT_BEEPER_BIT) ? 1 : 0);
		sys_port_in |= (1 << 2);
		break;
	case (BK_SYS_CTRL + 1):
		sys_port_in |= (1 << 2);
		break;
	default:
		if (offset >= 0177600) {
			core_bus_error_trap(r);
			break;
		}
		{
			uint32_t physical;
			if (!bk_mapper_address(&mapper, offset, 1, &physical)) {
				core_bus_error_trap(r);
				break;
			}
			mem[physical] = value;
		}
		break;
	}
}

static word bk_load_word(regs *r, word offset)
{

	if (fdd_selected() && (offset == 0177130 || offset == 0177132) &&
	                (selected_model != BK_MODEL_A16M || !(mapper.a16_mode & 4))) {
		return bk_fdd_read(offset);
	}
	byte lo = bk_load_byte(r, offset);
	if (r->fAbort) {
		return 0;
	}
	byte hi = bk_load_byte(r, (word)(offset + 1));
	return (word)(lo | (hi << 8));
}

static void bk_store_word(regs *r, word offset, word value)
{
	if (offset == BK_SYS_CTRL && selected_model >= BK_MODEL_11M_FDD && (value & 04000)) {
		bk_mapper_system_write(&mapper, value, 3);
		sys_port_in |= (1 << 2);
		return;
	}
	if (offset == 0177130 && selected_model == BK_MODEL_A16M) {
		bk_mapper_a16_write(&mapper, value);
		bk_fdd_write(offset, value & ~014);
		return;
	}
	if (fdd_selected() && (offset == 0177130 || offset == 0177132)) {
		bk_fdd_write(offset, value);
		return;
	}
	bk_store_byte(r, offset, (byte)(value & 0377));
	if (r->fAbort) {
		return;
	}
	bk_store_byte(r, (word)(offset + 1), (byte)(value >> 8));
}

static int bk_init(regs *r)
{
	(void)r;
	if (!mem || mem_size < MEM_SIZE) {
		return -1;
	}
	memset(mem, 0, MEM_SIZE);
	rom_count = 0;
	bk_tape_init();
	return 0;
}

static void bk_reset(regs *r)
{
	if (r) {
		r->ram_fast_size = selected_model >= BK_MODEL_11M_FDD ? 040000 : BK_ROM_LO;
	}
	bk_mapper_reset(&mapper, selected_model);
	video_reg = 040000;
	frame_irq_pending = 0;
	kbd_status = selected_model >= BK_MODEL_11M_FDD ? 0100 : CSR_READY;
	kbd_data = 0;
	shift_reg = 01330;
	ext_port = 0177777;
	sys_ctrl = selected_model == BK_MODEL_A16M ? 0164400 :
	           selected_model >= BK_MODEL_11M_FDD ? 0140000 : 0100000;
	sys_port_out = 0220;
	sys_port_in = 0370;
	kbd_irq_pending = 0;
	beeper_on = 0;
	beeper_pulse = 0;
	bk_tape_reset();
	bk_fdd_reset();
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
	uint32_t physical;
	if (!bk_mapper_address(&mapper, offset, 1, &physical)) {
		return NULL;
	}
	return &mem[physical];
}

void bk_hw_connect(regs *r)
{
	r->ram_fast = NULL;
	r->load_byte = bk_load_byte;
	r->store_byte = bk_store_byte;
	r->load_word = bk_load_word;
	r->store_word = bk_store_word;
	r->init = bk_init;
	r->reset = bk_reset;
	r->fini = bk_fini;
	r->poll_irq = bk_poll_irq;
	r->ramptr = bk_ramptr;
	/* Only the fixed RAM window can bypass the BK-0011M bank mapper. */
	r->ram_fast_size = selected_model >= BK_MODEL_11M_FDD ? 040000 : BK_ROM_LO;
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
	if (base < BK_ROM_LO) {
		return;
	}
	bk_hw_load_rom_segment(rom, BK_ROM_BASE + base - BK_ROM_LO, size);
}

int bk_hw_load_rom_segment(const byte *rom, uint32_t physical, size_t size)
{
	if (!rom || !size || physical < BK_ROM_BASE || physical >= BK_A16M_RAM ||
	                size > BK_A16M_RAM - physical || rom_count >= (int)(sizeof(roms) / sizeof(roms[0]))) {
		return -1;
	}
	for (int index = 0; index < rom_count; index++) {
		if (physical < roms[index].base + roms[index].size && roms[index].base < physical + size) {
			return -1;
		}
	}
	roms[rom_count].data = rom;
	roms[rom_count].base = physical;
	roms[rom_count].size = size;
	rom_count++;
	return 0;
}

int bk_hw_select_model(unsigned int model)
{
	if (model > BK_MODEL_11M_MSTD) {
		return -1;
	}
	selected_model = model;
	bk_hw_reset_state();
	return 0;
}

unsigned int bk_hw_model(void)
{
	return selected_model;
}

uint16_t bk_hw_bank_register(void)
{
	return mapper.bank;
}

uint16_t bk_hw_video_register(void)
{
	return video_reg;
}

void bk_hw_frame_tick(void)
{
	if (selected_model >= BK_MODEL_11M_FDD && !(video_reg & 040000)) {
		frame_irq_pending = 1;
	}
}

void bk_hw_reset_state(void)
{
	bk_reset(NULL);
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
	sys_port_in &= (byte)~SYS_PORT_IN_KEY_BIT;
	if ((kbd_status & 0100) == 0) {
		kbd_irq_pending = 1;
	}
}

void bk_hw_set_tick_hz(unsigned int hz)
{
	bk_tape_set_tick_hz(hz);
	if (hz && hz % 1000000 == 0 && 48 % (hz / 1000000) == 0) {
		cpu_mhz = hz / 1000000;
	}
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
	bk_fdd_advance(ticks, cpu_mhz);
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
	return &mem[selected_model >= BK_MODEL_11M_FDD ?
	            ((video_reg & 0100000) ? 6 : 5) * 0x4000 +
	            ((shift_reg & 01000) ? 0 : 0x3000) : bk_hw_vram_base()];
}

void bk_hw_set_color(int color)
{
	color_output = color != 0;
}

void bk_hw_render_pixels(uint32_t *pixels)
{
	byte *vram = bk_hw_vram_ptr();
	if (!pixels || !vram) {
		return;
	}
	unsigned int size = bk_hw_vram_size();
	unsigned int offset = ((shift_reg - 0330) & 0377) * 64;
	unsigned int palette = selected_model >= BK_MODEL_11M_FDD ? (video_reg >> 8) & 15 : 0;
	for (unsigned int y = 0; y < BK_SCREEN_HEIGHT; y++) {
		for (unsigned int x = 0; x < BK_SCREEN_WIDTH; x++) {
			uint32_t color = 0xff000000u;
			if (selected_model < BK_MODEL_11M_FDD || (shift_reg & 01000) || y < 64) {
				unsigned int bit = color_output ? (x & ~1u) : x;
				byte data = vram[(offset + y * 64 + bit / 8) % size];
				unsigned int value = (data >> (bit & 7)) & (color_output ? 3 : 1);
				color = color_output ? bk_palettes[palette][value] : value ? 0xffffffffu : 0xff000000u;
			}
			pixels[y * BK_SCREEN_WIDTH + x] = color;
		}
	}
}

static int bk_poll_irq(regs *r, word *vector)
{
	/* poll_irq has no acknowledge callback; consume only an accepted VM1 IRQ. */
	if (r->psw & (01000 | FLAG_P)) {
		return 0;
	}
	if (kbd_irq_pending && (kbd_status & CSR_READY) && ((kbd_status & 0100) == 0)) {
		kbd_irq_pending = 0;
		if (vector) {
			*vector = (kbd_data & 0200) ? BK_KBD_VECTOR2 : BK_KBD_VECTOR;
		}
		return 1;
	}
	if (frame_irq_pending) {
		frame_irq_pending = 0;
		if (vector) {
			*vector = 0100;
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
