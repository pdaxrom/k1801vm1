#ifndef MK90_SYSCON_H
#define MK90_SYSCON_H

#include "mk90_machine.h"

void mk90_syscon_reset(mk90_state_t *state);
word mk90_syscon_read_word(const mk90_state_t *state, word offset);
byte mk90_syscon_read_byte(const mk90_state_t *state, word offset);
void mk90_syscon_write_word(mk90_state_t *state, word offset, word value);
void mk90_syscon_write_byte(mk90_state_t *state, word offset, byte value);
int mk90_syscon_wr_ram(const mk90_state_t *state, word address);
int mk90_syscon_rd_ram(const mk90_state_t *state, word address);
int mk90_syscon_rd_rom(const mk90_state_t *state, word address);

#endif
