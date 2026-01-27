#ifndef BUS_H_
#define BUS_H_

#include <stdint.h>

typedef uint32_t paddr_t;

/* PDP-11/03-like fixed map (OCTAL) */
#define RAM_END 0157777
#define IO_PAGE_START 0160000
#define IO_PAGE_END 0177777

void bus_init(void);

int bus_is_nxm(paddr_t addr);

uint8_t bus_read8(paddr_t addr);
uint16_t bus_read16(paddr_t addr);

void bus_write8(paddr_t addr, uint8_t v);
void bus_write16(paddr_t addr, uint16_t v);

extern uint8_t ram[0200000]; /* 64K backing store; only 56K visible */

#endif
