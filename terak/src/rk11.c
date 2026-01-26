/* Minimal RK11 controller implementation.
 * This only supports enough behavior to run the RT-11 bootstrap sequence:
 * - CSR registers at 0177400-0177416
 * - READ + GO (000005) transfers from disk image to RAM
 * - RDY (bit 7) indicates completion (byte read sees it)
 *
 * Disk model assumption (RK05 geometry):
 *   RKDA bits:
 *     00-03  sector (0..0013)
 *     04     surface (0..1)
 *     05-12  cylinder (0..0312)
 *     13-15  drive (ignored)
 *   LBA = ((cylinder * 2 + surface) * 0014) + sector
 *   Byte offset = LBA * 01000 (01000 bytes per sector)
 */

#include "rk11.h"
#include "bus.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

/* CSR registers. */
static uint16_t rkds = 0;   /* drive status */
static uint16_t rker = 0;   /* error */
static uint16_t rkcs = 0;   /* command/status */
static uint16_t rkwc = 0;   /* word count (negative for count) */
static uint16_t rkba = 0;   /* bus address (byte address) */
static uint16_t rkda = 0;   /* disk address (linear, per note above) */
static uint16_t rkdb = 0;   /* data buffer (unused) */

static FILE *disk_fp = NULL;   /* opened RK05 image */
static int rk11_irq_pending_flag = 0;
static int rk11_irq_enable = 0;
static int rk11_sector_one_based = 0;

/* RK11 CSR bits (octal). */
#define RKCS_GO   0000001  /* GO */
#define RKCS_IE   0000100  /* interrupt enable (ignored) */
#define RKCS_RDY  0000200  /* RDY/DONE */
#define RKCS_DONE 00020000 /* completion flag (for RT-11 handler) */
#define RKCS_FUNC_MASK 0000016 /* function bits (bits 000001..000003) */
#define RKCS_FUNC_READ 0000004

#define RKDA_SECTOR_MASK 0000017
#define RKDA_SURFACE_MASK 0000020
#define RKDA_CYL_MASK 0017740
#define RKDA_DRIVE_MASK 0160000

#define RK_SECTORS_PER_TRACK 0000014
#define RK_WORDS_PER_SECTOR 0000400
#define RK_MAX_CYL 0000312

static uint16_t rkda_get_sector(uint16_t v) { return v & RKDA_SECTOR_MASK; }
static uint16_t rkda_get_surface(uint16_t v) { return (v & RKDA_SURFACE_MASK) >> 4; }
static uint16_t rkda_get_cyl(uint16_t v) { return (v & RKDA_CYL_MASK) >> 5; }
static uint16_t rkda_get_drive(uint16_t v) { return v & RKDA_DRIVE_MASK; }

static uint32_t rkda_to_lba(uint16_t v) {
    uint32_t cyl = rkda_get_cyl(v);
    uint32_t surf = rkda_get_surface(v);
    uint32_t sec = rkda_get_sector(v);
    if (rk11_sector_one_based) {
        if (sec == 0) sec = RK_SECTORS_PER_TRACK;
        sec -= 1;
    }
    return ((cyl * 0000002 + surf) * RK_SECTORS_PER_TRACK) + sec;
}

static int rkda_next_sector(uint16_t *v) {
    uint16_t sec = rkda_get_sector(*v);
    uint16_t surf = rkda_get_surface(*v);
    uint16_t cyl = rkda_get_cyl(*v);
    uint16_t drive = rkda_get_drive(*v);

    sec++;
    if (sec >= RK_SECTORS_PER_TRACK) {
        sec = 0000000;
        surf++;
        if (surf >= 0000002) {
            surf = 0000000;
            cyl++;
            if (cyl > RK_MAX_CYL) return -1;
        }
    }

    *v = (uint16_t)(drive | (cyl << 5) | (surf << 4) | sec);
    return 0;
}

static uint16_t *rk11_reg_for_addr(uint16_t addr) {
    switch (addr & 0177776) {
        case 0177400: return &rkds;
        case 0177402: return &rker;
        case 0177404: return &rkcs;
        case 0177406: return &rkwc;
        case 0177410: return &rkba;
        case 0177412: return &rkda;
        case 0177416: return &rkdb;
        default: return NULL;
    }
}

void rk11_reset(void) {
    rkds = 0;
    rker = 0;
    rkwc = 0;
    rkba = 0;
    rkda = 0;
    rkdb = 0;
    rkcs = RKCS_RDY;
    rk11_irq_pending_flag = 0;
    rk11_irq_enable = 0;
}

uint8_t rk11_read_byte(uint16_t addr) {
    uint16_t *reg = rk11_reg_for_addr(addr);
    if (!reg) return 0;
    if (addr & 000001) return (uint8_t)((*reg >> 000010) & 000377);
    return (uint8_t)(*reg & 000377);
}

void rk11_write_byte(uint16_t addr, uint8_t val) {
    uint16_t *reg = rk11_reg_for_addr(addr);
    if (!reg) return;
    if (addr & 000001) {
        *reg = (uint16_t)((*reg & 000377) | ((uint16_t)val << 000010));
    } else {
        *reg = (uint16_t)((*reg & 0177400) | val);
    }

    if ((addr & 0177776) == 0177404 && !(addr & 000001)) {
        /* Low byte write to RKCS. If GO set, clear RDY until completion. */
        if (rkcs & RKCS_GO) {
            rkcs &= (uint16_t)~(RKCS_RDY | RKCS_DONE);
            rk11_irq_enable = (rkcs & RKCS_IE) ? 1 : 0;
        }
    }
}

void rk11_poll(void) {
    /* Process a command when GO is set. */
    if (!(rkcs & RKCS_GO)) return;      /* GO not asserted */

    /* Command encoding: function bits 000001..000003 */
    if ((rkcs & RKCS_FUNC_MASK) != RKCS_FUNC_READ) {
        rkcs = (uint16_t)(RKCS_RDY | RKCS_DONE | (rk11_irq_enable ? RKCS_IE : 0));
        return;
    }

    int16_t wc = (int16_t)rkwc;        /* word count is stored as negative */
    int words = (wc < 0) ? -wc : 0;
    if (words <= 0) {
        rkcs = (uint16_t)(RKCS_RDY | RKCS_DONE | (rk11_irq_enable ? RKCS_IE : 0));
        return;
    }

    if (!disk_fp) {
        rker = 000001;
        rkcs = (uint16_t)(RKCS_RDY | RKCS_DONE | (rk11_irq_enable ? RKCS_IE : 0));
        return;
    }

    uint32_t mem_addr = rkba;                /* byte address in RAM */
    uint16_t cur_rkda = rkda;
    uint16_t cur_rkwc = rkwc;
    int word_in_sector = 0;

    for (int i = 0; i < words; ++i) {
        uint8_t lo = 0, hi = 0;
        uint32_t lba = rkda_to_lba(cur_rkda);
        uint32_t disk_base = lba * 01000;    /* 01000 bytes per sector */
        uint32_t off = disk_base + (uint32_t)word_in_sector * 000002;
        if (fseek(disk_fp, (long)off, SEEK_SET) != 0) {
            rker = 000001;
            break;
        }
        if (fread(&lo, 000001, 000001, disk_fp) != 000001) { rker = 000001; break; }
        if (fread(&hi, 000001, 000001, disk_fp) != 000001) { rker = 000001; break; }
        bus_write_word((paddr_t)mem_addr, (uint16_t)(lo | ((uint16_t)hi << 000010)));
        mem_addr += 000002;
        rkba += 000002;
        cur_rkwc += 000001;

        word_in_sector++;
        if (word_in_sector >= RK_WORDS_PER_SECTOR) {
            word_in_sector = 0;
            if (rkda_next_sector(&cur_rkda) != 0) {
                rker = 000001;
                break;
            }
        }
    }
    rkda = cur_rkda;
    rkwc = cur_rkwc;

    /* Command complete - clear GO, set RDY. */
    rkcs = (uint16_t)(RKCS_RDY | RKCS_DONE | (rk11_irq_enable ? RKCS_IE : 0));
    if (rk11_irq_enable) {
        rk11_irq_pending_flag = 1;
    }
}

int rk11_irq_pending(void) {
    return rk11_irq_pending_flag;
}

void rk11_irq_ack(void) {
    rk11_irq_pending_flag = 0;
}

void rk11_set_sector_base(int one_based) {
    rk11_sector_one_based = one_based ? 1 : 0;
}

int rk11_boot_copy(void *dest, size_t len) {
    if (!disk_fp) return -1;
    fseek(disk_fp, 0, SEEK_SET);
    size_t got = fread(dest, 000001, len, disk_fp);
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
