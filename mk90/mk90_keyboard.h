#ifndef MK90_KEYBOARD_H
#define MK90_KEYBOARD_H

#include "mk90_machine.h"

void mk90_keyboard_reset(mk90_state_t *state);
void mk90_keyboard_press(mk90_state_t *state, word scan_code);
void mk90_keyboard_release(mk90_state_t *state);
word mk90_keyboard_scan_code(const mk90_state_t *state);

#endif
