#include "mk90_lcd.h"

#include <string.h>

static word mk90_lcd_word(const mk90_state_t *state, unsigned reg_index)
{
    if (reg_index == 0) {
        return state->lcd.base;
    }
    return state->lcd.config;
}

static void mk90_lcd_set_word(mk90_state_t *state, unsigned reg_index, word value)
{
    if (reg_index == 0) {
        state->lcd.base = value;
    } else {
        state->lcd.config = value;
    }
}

void mk90_lcd_reset(mk90_state_t *state)
{
    state->lcd.base = 0;
    state->lcd.config = MK90_LCD_CONFIG_INIT;
}

word mk90_lcd_read_word(const mk90_state_t *state, word offset)
{
    return mk90_lcd_word(state, (offset >> 1) & 1u);
}

byte mk90_lcd_read_byte(const mk90_state_t *state, word offset)
{
    word value = mk90_lcd_read_word(state, offset);
    if (offset & 1u) {
        return (byte)((value >> 8) & 0377u);
    }
    return (byte)(value & 0377u);
}

void mk90_lcd_write_word(mk90_state_t *state, word offset, word value)
{
    mk90_lcd_set_word(state, (offset >> 1) & 1u, value);
}

void mk90_lcd_write_byte(mk90_state_t *state, word offset, byte value)
{
    word word_value = mk90_lcd_read_word(state, offset);

    if (offset & 1u) {
        word_value = (word)((word_value & 000377u) | ((word)value << 8));
    } else {
        word_value = (word)((word_value & 0177400u) | value);
    }
    mk90_lcd_write_word(state, offset, word_value);
}

void mk90_lcd_render(const mk90_state_t *state, uint32_t *pixels, int pitch_pixels)
{
    word base = state->lcd.base;
    int page;
    int row;
    int col;
    int x = 0;
    int y = 0;
    word index = 0;

    if (!pixels || pitch_pixels <= 0) {
        return;
    }

    for (page = 0; page < 2; page++) {
        for (row = 0; row < 32; row++) {
            x = 0;
            for (col = 0; col < 15; col++) {
                byte value = 0377u;
                int bit;

                if ((word)(base + index) <= state->ram_end) {
                    value = state->ram[(word)(base + index)];
                }

                for (bit = 0; bit < 8; bit++) {
                    pixels[y * pitch_pixels + x] =
                        (value & 0200u) ? 0xFF000000u : 0xFFFFFFFFu;
                    value <<= 1;
                    x++;
                }
                index = (word)(index + 2);
            }
            y++;
        }
        index = (word)(index - (MK90_SCREEN_BYTES - 1u));
    }
}
