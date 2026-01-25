/* Bus abstraction for TERAK emulator */
#ifndef BUS_H_
#define BUS_H_

#include <stdint.h>

/* 16‑bit address space (64 KB). */
typedef uint16_t paddr_t;

/* Forward declarations of device handlers */
uint16_t bus_read_word(paddr_t addr);
void     bus_write_word(paddr_t addr, uint16_t val);
uint8_t  bus_read_byte(paddr_t addr);
void     bus_write_byte(paddr_t addr, uint8_t val);

/* Initialise RAM (called by machine init). */
void bus_init(void);

/* Expose RAM for direct access (e.g., boot loader). */
extern uint8_t ram[0177776 + 2]; /* full 64 KB */

#endif /* BUS_H_ */

