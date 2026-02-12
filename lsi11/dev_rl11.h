#ifndef DEV_RL11_H_
#define DEV_RL11_H_

#include <stddef.h>

enum {
    RL11_TYPE_AUTO = 0,
    RL11_TYPE_RL01 = 1,
    RL11_TYPE_RL02 = 2,
};

int rl11_init(void);
void rl11_reset(void);
void rl11_poll(void);

int rl11_open_image(const char *path);
int rl11_open_image_typed(const char *path, int type);
void rl11_close_image(void);
int rl11_boot_copy(void *dest, size_t len);

int rl11_irq_pending(void);
void rl11_irq_ack(void);

#endif
