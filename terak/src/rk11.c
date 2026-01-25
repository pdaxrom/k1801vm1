/* Very small RK11 stub – enough to satisfy the emulator core.
 * The implementation does not emulate a real disk controller; it only
 * provides the CSR registers and a minimal read/write interface used by the
 * bus.  All registers return 0 and writes are ignored.  This allows the
 * emulator to boot a simple host‑side bootstrap that copies a disk image into
 * RAM.
 */

#include "rk11.h"
#include "bus.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

/* CSR registers – stored as bytes for simplicity. */
static uint16_t rkds = 0;   /* drive status */
static uint16_t rker = 0;   /* error */
static uint16_t rkcs = 0;   /* command/status */
static uint16_t rkwc = 0;   /* word count (negative for count) */
static uint16_t rkba = 0;   /* bus address (byte address) */
static uint16_t rkda = 0;   /* disk address (word offset) */

static FILE *disk_fp = NULL;   /* opened RK05 image */

/* RK11 CSR bits (simplified) */
#define RKCS_GO   0x80   /* GO flag – bit 7 */
#define RKCS_DONE 0x80   /* DONE flag re‑uses same bit for simplicity */

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
    /* Process a command when GO is set. */
    if (!disk_fp) return;               /* no image attached */
    if (!(rkcs & RKCS_GO)) return;      /* GO not asserted */

    /* Command encoding – lower three bits of RKCS (0=NOP, 1=WRITE, 2=READ). */
    int cmd = rkcs & 0x07;
    int16_t wc = (int16_t)rkwc;        /* word count is stored as negative */
    int words = -wc;                    /* number of words to transfer */
    if (words <= 0) {
        rkcs = (rkcs & ~RKCS_GO) | RKCS_DONE;
        return;
    }

    uint32_t mem_addr = rkba;           /* byte address in RAM */
    uint32_t disk_offset = ((uint32_t)rkda) * 2; /* rkda is word offset */

    if (cmd == 1) { /* WRITE: RAM -> disk */
        for (int i = 0; i < words; ++i) {
            uint16_t val = bus_read_word((paddr_t)(mem_addr + i * 2));
            uint8_t lo = val & 0xFF;
            uint8_t hi = (val >> 8) & 0xFF;
            fseek(disk_fp, disk_offset + i * 2, SEEK_SET);
            fwrite(&lo, 1, 1, disk_fp);
            fwrite(&hi, 1, 1, disk_fp);
        }
        fflush(disk_fp);
    } else if (cmd == 2) { /* READ: disk -> RAM */
        for (int i = 0; i < words; ++i) {
            uint8_t lo = 0, hi = 0;
            fseek(disk_fp, disk_offset + i * 2, SEEK_SET);
            fread(&lo, 1, 1, disk_fp);
            fread(&hi, 1, 1, disk_fp);
            uint16_t val = lo | ((uint16_t)hi << 8);
            bus_write_word((paddr_t)(mem_addr + i * 2), val);
        }
    }

    /* Command complete – clear GO, set DONE. */
    rkcs = (rkcs & ~RKCS_GO) | RKCS_DONE;
}

int rk11_boot_copy(void *dest, size_t len) {
    if (!disk_fp) return -1;
    fseek(disk_fp, 0, SEEK_SET);
    size_t got = fread(dest, 1, len, disk_fp);
    return (got == len) ? 0 : -1;
}

/* Open an RK05 image file. */
int rk11_open_image(const char *path) {
    if (disk_fp) fclose(disk_fp);
    disk_fp = fopen(path, "r+b");
    if (!disk_fp) {
        perror("rk11_open_image");
        return -1;
    }
    return 0;
}

void rk11_close_image(void) {
    if (disk_fp) {
        fclose(disk_fp);
        disk_fp = NULL;
    }
}
