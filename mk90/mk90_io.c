#include "mk90_io.h"

#include "mk90_keyboard.h"
#include "mk90_smp.h"

static word mk90_io_fetch_input(mk90_state_t *state)
{
    switch (state->io.regs[2] & 0017u) {
    case 0000u:
        return (word)mk90_smp_data(state, 0u, 0u);
    case 0001u:
        return (word)mk90_smp_data(state, 1u, 0u);
    case 0002u:
        return mk90_keyboard_scan_code(state);
    default:
        return 0177777u;
    }
}

static void mk90_io_raise_data_irq(mk90_state_t *state)
{
    if ((state->io.regs[2] & 0040u) == 0) {
        mk90_machine_raise_data_ready(state);
    }
}

static void mk90_io_commit_reg_write(mk90_state_t *state, unsigned reg_index)
{
    byte channel = (byte)(state->io.regs[2] & 0007u);
    byte mode = (byte)(state->io.regs[2] & 0017u);

    switch (reg_index) {
    case 0:
        state->io.shiftreg = state->io.regs[0];
        state->io.rgrq = (word)(state->io.rgrq | (1u << channel));
        mk90_machine_tracef(state, "io data<=%06o mode=%03o sel=%d\n",
                            state->io.shiftreg, mode, state->io.select_active);
        if (state->io.select_active) {
            mk90_io_raise_data_irq(state);
            switch (mode) {
            case 0000u:
                state->io.shiftreg = mk90_smp_data(state, 0u,
                                                   (byte)state->io.shiftreg);
                break;
            case 0001u:
                state->io.shiftreg = mk90_smp_data(state, 1u,
                                                   (byte)state->io.shiftreg);
                break;
            case 0002u:
                state->io.shiftreg = mk90_keyboard_scan_code(state);
                break;
            case 0010u:
                (void)mk90_smp_data(state, 0u, (byte)state->io.shiftreg);
                break;
            case 0011u:
                (void)mk90_smp_data(state, 1u, (byte)state->io.shiftreg);
                break;
            default:
                break;
            }
        }
        break;
    case 2:
        mk90_machine_tracef(state, "io ctrl<=%06o mode=%03o sel=%d\n",
                            state->io.regs[2], mode, state->io.select_active);
        if (state->io.select_active) {
            mk90_io_raise_data_irq(state);
            switch (mode) {
            case 0000u:
                state->io.shiftreg = mk90_smp_data(state, 0u, 0u);
                break;
            case 0001u:
                state->io.shiftreg = mk90_smp_data(state, 1u, 0u);
                break;
            case 0002u:
                state->io.shiftreg = mk90_keyboard_scan_code(state);
                break;
            default:
                break;
            }
        }
        break;
    case 3:
        state->io.select_active = 1;
        state->io.shiftreg = state->io.regs[3];
        state->io.rgrq = (word)(state->io.rgrq | (1u << channel));
        mk90_machine_tracef(state, "io cmd<=%06o mode=%03o channel=%o\n",
                            state->io.shiftreg, mode, channel);
        mk90_io_raise_data_irq(state);
        switch (mode) {
        case 0000u:
            state->io.shiftreg = mk90_smp_cmd(state, 0u,
                                              (byte)state->io.shiftreg);
            break;
        case 0001u:
            state->io.shiftreg = mk90_smp_cmd(state, 1u,
                                              (byte)state->io.shiftreg);
            break;
        case 0002u:
            state->io.shiftreg = mk90_keyboard_scan_code(state);
            break;
        case 0010u:
            (void)mk90_smp_cmd(state, 0u, (byte)state->io.shiftreg);
            break;
        case 0011u:
            (void)mk90_smp_cmd(state, 1u, (byte)state->io.shiftreg);
            break;
        case 0013u:
            state->io.beeper_level = (state->io.shiftreg & 1u) ? 1 : 0;
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
}

void mk90_io_reset(mk90_state_t *state)
{
    state->io.regs[0] = 0;
    state->io.regs[1] = 0;
    state->io.regs[2] = 0;
    state->io.regs[3] = 0;
    state->io.shiftreg = 0177777u;
    state->io.rgrq = 0177777u;
    state->io.select_active = 0;
    state->io.beeper_level = 0;
}

byte mk90_io_read_byte(mk90_state_t *state, word offset)
{
    word value = mk90_io_read_word(state, offset);

    if (offset & 1u) {
        return (byte)((value >> 8) & 0377u);
    }
    return (byte)(value & 0377u);
}

word mk90_io_read_word(mk90_state_t *state, word offset)
{
    unsigned reg_index = (offset >> 1) & 03u;
    word value;

    switch (reg_index) {
    case 0:
        value = state->io.shiftreg;
        state->io.shiftreg = 0177777u;
        state->io.rgrq = (word)(state->io.rgrq | (1u << (state->io.regs[2] & 7u)));
        mk90_machine_tracef(state, "io data=>%06o mode=%03o sel=%d\n",
                            value,
                            (byte)(state->io.regs[2] & 0017u),
                            state->io.select_active);
        if (state->io.select_active) {
            mk90_io_raise_data_irq(state);
            state->io.shiftreg = mk90_io_fetch_input(state);
        }
        break;
    case 1:
        value = state->io.rgrq;
        break;
    case 2:
        value = (word)((state->io.regs[2] & 00160u) | 0177604u);
        if (!state->io.select_active) {
            value = (word)(value | 0000010u);
        }
        break;
    case 3:
        if (state->io.select_active) {
            mk90_io_raise_data_irq(state);
        }
        value = state->io.shiftreg;
        mk90_machine_tracef(state, "io cmd=>%06o mode=%03o\n",
                            value,
                            (byte)(state->io.regs[2] & 0017u));
        state->io.select_active = 0;
        break;
    default:
        value = 0177777u;
        break;
    }

    return value;
}

void mk90_io_write_word(mk90_state_t *state, word offset, word value)
{
    unsigned reg_index = (offset >> 1) & 03u;

    state->io.regs[reg_index] = value;
    mk90_io_commit_reg_write(state, reg_index);
}

void mk90_io_write_byte(mk90_state_t *state, word offset, byte value)
{
    word word_value = state->io.regs[(offset >> 1) & 03u];

    if (offset & 1u) {
        word_value = (word)((word_value & 000377u) | ((word)value << 8));
    } else {
        word_value = (word)((word_value & 0177400u) | value);
    }
    mk90_io_write_word(state, offset, word_value);
}

void mk90_io_timer_irq(mk90_state_t *state)
{
    if ((state->io.regs[2] & 00100u) == 0) {
        mk90_machine_raise_inrt(state);
    }
}

void mk90_io_key_irq(mk90_state_t *state)
{
    state->io.rgrq = (word)(state->io.rgrq & ~0004u);
    if ((state->io.regs[2] & 00020u) == 0) {
        mk90_machine_raise_keyboard(state);
    }
}
