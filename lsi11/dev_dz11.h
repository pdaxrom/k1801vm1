#ifndef DEV_DZ11_H_
#define DEV_DZ11_H_

#include <stddef.h>
#include <stdint.h>

#define DZ11_LINES 8u

int dz11_init(void);
void dz11_reset(void);
void dz11_poll(void);
void dz11_shutdown(void);

void dz11_set_8bit(int on);
int dz11_set_listen_port(int port, char *err, size_t err_len);

int dz11_rx_irq_pending(void);
void dz11_rx_irq_ack(void);
int dz11_tx_irq_pending(void);
void dz11_tx_irq_ack(void);

#ifdef LSI11_TESTS
void dz11_test_inject_rx(unsigned line, uint8_t ch, int framing_error);
#endif

#endif
