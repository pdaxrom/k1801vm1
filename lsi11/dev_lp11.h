#ifndef DEV_LP11_H_
#define DEV_LP11_H_

#include <stdint.h>

int  lp11_init(void);
void lp11_reset(void);
void lp11_poll(void); /* optional */

int  lp11_irq_pending(void);
void lp11_irq_ack(void);

#endif
