#ifndef DEV_RH11_H_
#define DEV_RH11_H_

#include <stdint.h>
#include <stddef.h>

int rh11_init(void);
void rh11_reset(void);
void rh11_poll(void);

int rh11_open_image(const char *path);
void rh11_close_image(void);
int rh11_boot_copy(void *dest, size_t len);

int rh11_irq_pending(void);
void rh11_irq_ack(void);

#endif
