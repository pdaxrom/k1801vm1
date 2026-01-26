/* Minimal DL11 console device (memory‑mapped UART) */
#ifndef DL11_H_
#define DL11_H_

#include <stdint.h>

/* CSR bits – only the bits required for polling are defined */
#define DL11_CSR_DONE   0200  /* Receive character available */
#define DL11_CSR_READY  0200  /* Transmitter ready */

/* Initialise the device (clear registers). */
void dl11_reset(void);

/* Register accessors used by the bus implementation. */
uint8_t dl11_read_rcsr(void);
uint8_t dl11_read_rbuf(void);
uint8_t dl11_read_xcsr(void);
uint8_t dl11_read_xbuf(void);   /* reading XBUF returns last transmitted byte */

void dl11_write_rcsr(uint8_t v);
void dl11_write_rbuf(uint8_t v);
void dl11_write_xcsr(uint8_t v);
void dl11_write_xbuf(uint8_t v);

/* Host‑side helper – called each main‑loop iteration to poll stdin. */
void dl11_poll_input(void);

#endif /* DL11_H_ */

