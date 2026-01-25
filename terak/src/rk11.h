/* Minimal RK11 controller stub – enough for linking and basic polling */
#ifndef RK11_H_
#define RK11_H_

#include <stdint.h>

void rk11_reset(void);
uint8_t rk11_read_byte(uint16_t addr);
void rk11_write_byte(uint16_t addr, uint8_t val);

/* In a full implementation these would be called by the bus each step. */
void rk11_poll(void);

#endif /* RK11_H_ */

