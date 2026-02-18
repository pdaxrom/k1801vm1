#ifndef DEV_RK11_H_
#define DEV_RK11_H_

#include <stdint.h>
#include <stddef.h>

int  rk11_init(void);
void rk11_reset(void);
void rk11_poll(void);

int  rk11_open_image(const char *path);
void rk11_close_image(void);
void rk11_set_debug(int on);

/* optional: one-based sector mapping */
void rk11_set_sector_base(int one_based);

int  rk11_irq_pending(void);
void rk11_irq_ack(void);

/* Bootstrap helper: copy first len bytes into RAM destination */
int rk11_boot_copy(void *dest, size_t len);

#endif
