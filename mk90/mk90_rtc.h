#ifndef MK90_RTC_H
#define MK90_RTC_H

#include "mk90_machine.h"

void mk90_rtc_reset(mk90_state_t *state);
void mk90_rtc_tick_ms(mk90_state_t *state, uint32_t elapsed_ms);
word mk90_rtc_read_word(mk90_state_t *state, word offset);
byte mk90_rtc_read_byte(mk90_state_t *state, word offset);
void mk90_rtc_write_word(mk90_state_t *state, word offset, word value);
void mk90_rtc_write_byte(mk90_state_t *state, word offset, byte value);

#endif
