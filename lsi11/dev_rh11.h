#ifndef DEV_RH11_H_
#define DEV_RH11_H_

#include <stdint.h>
#include <stddef.h>

#define RH11_MAX_DRIVES 8

typedef enum rh11_mode {
    RH11_MODE_RH11 = 0,
    RH11_MODE_RH70 = 1
} rh11_mode_t;

int rh11_init(void);
void rh11_reset(void);
void rh11_poll(void);

int rh11_open_image(const char *path);
int rh11_open_image_unit(unsigned unit, const char *path);
void rh11_close_image(void);
void rh11_set_debug(int on);
int rh11_boot_copy(void *dest, size_t len);
int rh11_boot_copy_unit(unsigned unit, void *dest, size_t len);

int rh11_set_mode(rh11_mode_t mode);
rh11_mode_t rh11_get_mode(void);
const char *rh11_mode_name(rh11_mode_t mode);

int rh11_irq_pending(void);
void rh11_irq_ack(void);

#endif
