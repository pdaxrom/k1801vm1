#ifndef DEV_KW11_H_
#define DEV_KW11_H_

#include <stdint.h>

int  kw11_init(void);
void kw11_reset(void);
void kw11_poll(void);          /* call frequently from main loop */

int  kw11_irq_pending(void);
void kw11_irq_ack(void);

#endif
