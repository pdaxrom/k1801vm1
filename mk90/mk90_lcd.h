#ifndef MK90_LCD_H
#define MK90_LCD_H

#include "mk90_machine.h"

void mk90_lcd_reset(mk90_state_t *state);
word mk90_lcd_read_word(const mk90_state_t *state, word offset);
byte mk90_lcd_read_byte(const mk90_state_t *state, word offset);
void mk90_lcd_write_word(mk90_state_t *state, word offset, word value);
void mk90_lcd_write_byte(mk90_state_t *state, word offset, byte value);
void mk90_lcd_render(const mk90_state_t *state, uint32_t *pixels,
                     int pitch_pixels);

#endif
