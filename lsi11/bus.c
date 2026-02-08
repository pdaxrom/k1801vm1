#include "bus.h"
#include "devio.h"

#include <string.h>

uint8_t ram[0200000] __attribute__((aligned(0x1000)));

void bus_init(void) { memset(ram, 0, sizeof(ram)); }

/* Strict PDP-11/03-like: RAM only up to 0157777; IO page 0160000..0177777 */
int bus_is_nxm(paddr_t addr) {
  uint16_t a = (uint16_t)addr;
  if (a <= RAM_END)
    return 0; /* RAM */

  if (a >= IO_PAGE_START && a <= IO_PAGE_END) {
    /* IO page: only decoded device registers are valid */
    return devio_has(a) ? 0 : 1;
  }

  return 1; /* should not happen in 16-bit machine, but keep strict */
}

uint8_t bus_read8(paddr_t addr) {
  uint16_t a = (uint16_t)addr;
  if (a <= RAM_END)
    return ram[a];

  /* IO page */
  return devio_read8(a);
}

void bus_write8(paddr_t addr, uint8_t v) {
  uint16_t a = (uint16_t)addr;
  if (a <= RAM_END) {
    ram[a] = v;
    return;
  }

  devio_write8(a, v);
}

uint16_t bus_read16(paddr_t addr) {
  uint16_t a = (uint16_t)addr;
  uint8_t lo = bus_read8((paddr_t)a);
  uint8_t hi = bus_read8((paddr_t)(a + 1));
  return (uint16_t)(lo | ((uint16_t)hi << 8));
}

void bus_write16(paddr_t addr, uint16_t v) {
  uint16_t a = (uint16_t)addr;
  bus_write8((paddr_t)a, (uint8_t)(v & 000377));
  bus_write8((paddr_t)(a + 1), (uint8_t)((v >> 8) & 000377));
}
