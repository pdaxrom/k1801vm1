/* Minimal RK11 controller stub – enough for linking and basic polling */
#ifndef RK11_H_
#define RK11_H_

#include <stdint.h>
#include <stddef.h>

void rk11_reset(void);
uint8_t rk11_read_byte(uint16_t addr);
void rk11_write_byte(uint16_t addr, uint8_t val);

/* Open an RK05 disk image. Returns 0 on success, -1 on failure. */
int rk11_open_image(const char *path);
void rk11_close_image(void);

/* Called each emulator step – processes a pending command if GO is set. */
void rk11_poll(void);

/* Copy the first *len* bytes of the attached RK05 image into *dest*.
   Returns 0 on success, -1 if no image is open. */
int rk11_boot_copy(void *dest, size_t len);

#endif /* RK11_H_ */
