/* Simple bus implementation for TERAK emulator */

#include "bus.h"
#include "dl11.h"
#include "rk11.h"
#include <string.h>

/* Full 64 KB RAM (addressable up to 177777 octal). */
uint8_t ram[0177776 + 2];

void bus_init(void) {
    memset(ram, 0, sizeof(ram));
    dl11_reset();
    rk11_reset();
}

static inline int is_io(paddr_t a) {
    return a >= 0160000; /* I/O page starts at 160000 octal */
}

int bus_is_nxm(paddr_t addr) {
    if (addr < 0160000) return 0;
    if (addr >= 0177400 && addr <= 0177417) return 0; /* RK11 */
    if (addr >= 0177560 && addr <= 0177567) return 0; /* DL11 */
    return 1;
}

uint8_t bus_read_byte(paddr_t addr) {
    if (!is_io(addr)) {
        return ram[addr];
    }
    /* Dispatch to devices based on address */
    if (addr >= 0177400 && addr <= 0177417) {
        return rk11_read_byte(addr);
    }
    switch (addr) {
        case 0177560: return dl11_read_rcsr();
        case 0177562: return dl11_read_rbuf();
        case 0177564: return dl11_read_xcsr();
        case 0177566: return dl11_read_xbuf();
        default: return 0; /* unmapped reads return 0 */
    }
}

void bus_write_byte(paddr_t addr, uint8_t val) {
    if (!is_io(addr)) {
        ram[addr] = val;
        return;
    }
    switch (addr) {
        case 0177560: dl11_write_rcsr(val); break;
        case 0177562: dl11_write_rbuf(val); break;
        case 0177564: dl11_write_xcsr(val); break;
        case 0177566: dl11_write_xbuf(val); break;
        default: break;
    }
    if (addr >= 0177400 && addr <= 0177417) {
        rk11_write_byte(addr, val);
    }
}

uint16_t bus_read_word(paddr_t addr) {
    /* Little‑endian: low byte at addr, high byte at addr+1 */
    uint16_t lo = bus_read_byte(addr);
    uint16_t hi = bus_read_byte((paddr_t)(addr + 1));
    return lo | (hi << 8);
}

void bus_write_word(paddr_t addr, uint16_t val) {
    bus_write_byte(addr, (uint8_t)(val & 0377));
    bus_write_byte((paddr_t)(addr + 1), (uint8_t)((val >> 8) & 0377));
}
