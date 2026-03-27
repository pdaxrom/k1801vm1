#include "mk90_syscon.h"

void mk90_syscon_reset(mk90_state_t *state)
{
    state->syscon.rg1 = 0;
    state->syscon.rg2 = 0;
}

word mk90_syscon_read_word(const mk90_state_t *state, word offset)
{
    return (offset < 2u) ? state->syscon.rg1 : state->syscon.rg2;
}

byte mk90_syscon_read_byte(const mk90_state_t *state, word offset)
{
    word value = mk90_syscon_read_word(state, offset);
    if (offset & 1u) {
        return (byte)((value >> 8) & 0377u);
    }
    return (byte)(value & 0377u);
}

void mk90_syscon_write_word(mk90_state_t *state, word offset, word value)
{
    word *target = (offset < 2u) ? &state->syscon.rg1 : &state->syscon.rg2;

    *target = value;
}

void mk90_syscon_write_byte(mk90_state_t *state, word offset, byte value)
{
    word word_value = mk90_syscon_read_word(state, offset);

    if (offset & 1u) {
        word_value = (word)((word_value & 000377u) | ((word)value << 8));
    } else {
        word_value = (word)((word_value & 0177400u) | value);
    }
    mk90_syscon_write_word(state, offset, word_value);
}

int mk90_syscon_wr_ram(const mk90_state_t *state, word address)
{
    static const word endaddr[4] = {
        0157777u, 0077777u, 0037777u, 0017777u
    };
    unsigned index = (unsigned)((state->syscon.rg1 >> 9) & 03u);

    if (address <= endaddr[index]) {
        return 1;
    }
    return (address >= 0164000u && address <= 0165777u) ? 1 : 0;
}

int mk90_syscon_rd_ram(const mk90_state_t *state, word address)
{
    static const word endaddr[4] = {
        0157777u, 0077777u, 0037777u, 0017777u
    };
    unsigned index = (unsigned)((state->syscon.rg1 >> 11) & 07u);

    if (index < 4u && address <= endaddr[index & 03u]) {
        return 1;
    }
    return (address >= 0164000u && address <= 0165777u) ? 1 : 0;
}

int mk90_syscon_rd_rom(const mk90_state_t *state, word address)
{
    static const word startaddr[8] = {
        0177777u, 0100000u, 0040000u, 0020000u,
        0000000u, 0000000u, 0000000u, 0000000u
    };
    unsigned index;

    if (state->syscon.rg2 & 0020000u) {
        return 0;
    }

    index = (unsigned)((state->syscon.rg1 >> 11) & 07u);
    if (address >= startaddr[index] && address <= 0157777u) {
        return 1;
    }
    if ((state->syscon.rg2 & 0001000u) &&
        address >= 0160000u && address <= 0163777u) {
        return 1;
    }
    return (address >= 0166000u && address <= 0176777u) ? 1 : 0;
}
