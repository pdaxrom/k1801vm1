#ifndef DEV_SR_H_
#define DEV_SR_H_

#include <stdint.h>

int  sr_init(void);
void sr_reset(void);
void sr_set(uint16_t v);  /* optional config */

#endif
