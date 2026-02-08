#ifndef DEV_DL11_H_
#define DEV_DL11_H_

#include <stdint.h>

int dl11_init(void);
void dl11_reset(void);
void dl11_poll(void); /* host keyboard -> RX + TX timing */

int dl11_rx_irq_pending(void);
void dl11_rx_irq_ack(void);

int dl11_tx_irq_pending(void);
void dl11_tx_irq_ack(void);

#ifdef LSI11_TESTS
void dl11_test_inject_rx(uint8_t ch);
#endif

#endif
