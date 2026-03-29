#ifndef UBMAP_H_
#define UBMAP_H_

#include <stdint.h>

#include "bus.h"

int ubmap_init(void);
void ubmap_reset(void);
void ubmap_set_enabled(int on);
int ubmap_enabled(void);

/* Translate an 18-bit UNIBUS address into a physical bus address. */
bus_paddr_t ubmap_map_addr(uint32_t unibus_addr);

#endif
