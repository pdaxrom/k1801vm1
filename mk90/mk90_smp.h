#ifndef MK90_SMP_H
#define MK90_SMP_H

#include "mk90_machine.h"

void mk90_smp_reset(mk90_state_t *state);
void mk90_smp_close(mk90_state_t *state);
int mk90_smp_load(mk90_state_t *state, unsigned slot, const char *path,
                  char *err, size_t err_len);
byte mk90_smp_cmd(mk90_state_t *state, unsigned slot, byte value);
byte mk90_smp_data(mk90_state_t *state, unsigned slot, byte value);

#endif
