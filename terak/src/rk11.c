/* Very small RK11 stub – enough to satisfy the emulator core.
 * The implementation does not emulate a real disk controller; it only
 * provides the CSR registers and a minimal read/write interface used by the
 * bus.  All registers return 0 and writes are ignored.  This allows the
 * emulator to boot a simple host‑side bootstrap that copies a disk image into
 * RAM.
 */

#include "rk11.h"
#include <stddef.h>

/* CSR registers – stored as bytes for simplicity. */
static uint16_t rkds = 0;   /* drive status */
static uint16_t rker = 0;   /* error */
static uint16_t rkcs = 0;   /* command/status */
static uint16_t rkwc = 0;   /* word count */
static uint16_t rkba = 0;   /* bus address */
static uint16_t rkda = 0;   /* disk address */

void rk11_reset(void) {
    rkds = rker = rkcs = rkwc = rkba = rkda = 0;
}

uint8_t rk11_read_byte(uint16_t addr) {
    switch (addr) {
        case 0177400: return (uint8_t)(rkds & 0xFF);
        case 0177402: return (uint8_t)(rker & 0xFF);
        case 0177404: return (uint8_t)(rkcs & 0xFF);
        case 0177406: return (uint8_t)(rkwc & 0xFF);
        case 0177410: return (uint8_t)(rkba & 0xFF);
        case 0177412: return (uint8_t)(rkda & 0xFF);
        default: return 0;
    }
}

void rk11_write_byte(uint16_t addr, uint8_t val) {
    switch (addr) {
        case 0177400: rkds = (rkds & 0xFF00) | val; break;
        case 0177402: rker = (rker & 0xFF00) | val; break;
        case 0177404: rkcs = (rkcs & 0xFF00) | val; break;
        case 0177406: rkwc = (rkwc & 0xFF00) | val; break;
        case 0177410: rkba = (rkba & 0xFF00) | val; break;
        case 0177412: rkda = (rkda & 0xFF00) | val; break;
        default: break;
    }
}

void rk11_poll(void) {
    /* No asynchronous activity – stub */
}

