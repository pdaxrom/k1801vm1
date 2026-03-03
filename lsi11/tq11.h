#ifndef TQ11_H_
#define TQ11_H_

#include <stddef.h>
#include <stdint.h>

#define TQ11_MAX_UNITS 8

int tq11_init(void);
void tq11_reset(void);
void tq11_poll(void);

int tq11_open_image(const char *path);
int tq11_open_image_unit(unsigned unit, const char *path);
void tq11_close_image(void);

int tq11_attached(void);

#if defined(LSI11_TESTS)
int tq11_test_tap_read_record(uint8_t *buf, size_t max_len, size_t *rec_len);
int tq11_test_tap_write_record(const uint8_t *buf, size_t len);
int tq11_test_tap_write_mark(void);
int tq11_test_tap_space_forward_record(void);
int tq11_test_tap_space_reverse_record(void);
int tq11_test_tap_rewind(void);
#endif

#endif
