#ifndef DEV_RQ11_H_
#define DEV_RQ11_H_

#include <stddef.h>

#define RQ11_MAX_UNITS 4

int rq11_init(void);
void rq11_reset(void);
void rq11_poll(void);

int rq11_open_image(const char *path);
int rq11_open_image_unit(unsigned unit, const char *path);
void rq11_close_image(void);

int rq11_irq_pending(void);
void rq11_irq_ack(void);

#endif
