#ifndef MK90_IO_H
#define MK90_IO_H

#include "mk90_machine.h"

void mk90_io_reset(mk90_state_t *state);
word mk90_io_read_word(mk90_state_t *state, word offset);
byte mk90_io_read_byte(mk90_state_t *state, word offset);
void mk90_io_write_word(mk90_state_t *state, word offset, word value);
void mk90_io_write_byte(mk90_state_t *state, word offset, byte value);
void mk90_io_key_irq(mk90_state_t *state);
void mk90_io_timer_irq(mk90_state_t *state);

#endif
