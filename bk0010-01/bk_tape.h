#ifndef BK_TAPE_H
#define BK_TAPE_H

#include <stddef.h>

#include "core/core.h"

void bk_tape_init(void);
void bk_tape_reset(void);
void bk_tape_set_tick_hz(unsigned int hz);
void bk_tape_tick(void);
void bk_tape_write(int motor_on, int val);
int bk_tape_read(void);
void bk_tape_set_name_pad(int use_space);

int bk_tape_set_input(const byte *data, size_t size);
int bk_tape_set_input_bin(const byte *data, size_t size, const char *name);
int bk_tape_encode_bin_to_raw(const byte *data, size_t size, const char *name,
                              byte **out_raw, size_t *out_size);
void bk_tape_set_output_enabled(int enable);
const byte *bk_tape_output_data(size_t *size);
void bk_tape_output_clear(void);
void bk_tape_rewind(void);
int bk_tape_decode_raw_to_bin(const byte *raw, size_t raw_size,
                              byte **out_data, size_t *out_size,
                              char *name_out, size_t name_size,
                              byte *name_raw, size_t name_raw_size);

#endif
