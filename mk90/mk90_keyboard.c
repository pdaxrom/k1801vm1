#include "mk90_keyboard.h"

void mk90_keyboard_reset(mk90_state_t *state)
{
    state->keyboard.scan_code = 0;
}

void mk90_keyboard_press(mk90_state_t *state, word scan_code)
{
    state->keyboard.scan_code = scan_code;
}

void mk90_keyboard_release(mk90_state_t *state)
{
    state->keyboard.scan_code = 0;
}

word mk90_keyboard_scan_code(const mk90_state_t *state)
{
    return state->keyboard.scan_code;
}
