#ifndef DEV_TM11_H_
#define DEV_TM11_H_

int tm11_init(void);
void tm11_reset(void);
void tm11_poll(void);

int tm11_irq_pending(void);
void tm11_irq_ack(void);

#endif
