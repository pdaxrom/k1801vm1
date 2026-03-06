#ifndef DEV_XP_H_
#define DEV_XP_H_

#include <stddef.h>

#define XP_MAX_DRIVES 8

int xp_init(void);
void xp_reset(void);
void xp_poll(void);

int xp_open_image(const char *path);
int xp_open_image_unit(unsigned unit, const char *path);
void xp_close_image(void);

int xp_irq_pending(void);
void xp_irq_ack(void);

#endif
