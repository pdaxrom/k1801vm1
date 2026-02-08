#include "dev_rk11.h"
#include "devio.h"
#include "irq.h"
#include "irq_latch.h"
#include "bus.h"
#include <stdio.h>
#include <stdint.h>

/* RK11 CSR base range (octal) */
#define RK_BASE 0177400

/* Registers (octal addresses) */
#define RKDS 0177400
#define RKER 0177402
#define RKCS 0177404
#define RKWC 0177406
#define RKBA 0177410
#define RKDA 0177412
#define RKDB 0177416

/* RKCS bits (keep minimal; all octal) */
#define RKCS_GO   0000001
#define RKCS_FUNC_MASK 0000016
#define RKCS_READ 0000004
#define RKCS_WRITE 0000002
#define RKCS_IE   0000100

/* We expose DONE/RDY through a DONE bit in low byte for polling.
   Use bit 7 (0200) like many bootstraps expect to TSTB. */
#define RKCS_DONEB 0000200

/* RKDA geometry masks (octal) */
#define RKDA_SECTOR_MASK 0000017
#define RKDA_SURF_MASK   0000020
#define RKDA_CYL_MASK    0017740
#define RK_SECTORS_PER_TRACK 0000014
#define RK_WORDS_PER_SECTOR  0000400
#define RK_MAX_CYL           0000312

static uint16_t rkds, rker, rkcs, rkwc, rkba, rkda, rkdb;

static irq_latch_t rk_l;     /* DONE/IE + irq latch */
static FILE *fp = NULL;
static int sector_one_based = 0;

static uint16_t get_sector(uint16_t v) { return (uint16_t)(v & RKDA_SECTOR_MASK); }
static uint16_t get_surf(uint16_t v)   { return (uint16_t)((v & RKDA_SURF_MASK) >> 4); }
static uint16_t get_cyl(uint16_t v)    { return (uint16_t)((v & RKDA_CYL_MASK) >> 5); }

static uint32_t rk_lba(uint16_t v)
{
    uint32_t cyl = get_cyl(v);
    uint32_t srf = get_surf(v);
    uint32_t sec = get_sector(v);

    if (sector_one_based) {
        if (sec == 0) sec = RK_SECTORS_PER_TRACK;
        sec -= 1;
    }

    return ((cyl * 2u + srf) * (uint32_t)RK_SECTORS_PER_TRACK) + sec;
}

static int rk_next_sector(uint16_t *v)
{
    uint16_t sec = get_sector(*v);
    uint16_t srf = get_surf(*v);
    uint16_t cyl = get_cyl(*v);

    sec++;
    if (sec >= RK_SECTORS_PER_TRACK) {
        sec = 0;
        srf++;
        if (srf >= 2) {
            srf = 0;
            cyl++;
            if (cyl > RK_MAX_CYL) return -1;
        }
    }

    /* preserve drive bits (upper) as-is */
    *v = (uint16_t)((*v & 0160000) | (cyl << 5) | (srf << 4) | sec);
    return 0;
}

static uint16_t *reg_ptr(uint16_t a)
{
    switch (a & 0177776) {
    case RKDS: return &rkds;
    case RKER: return &rker;
    case RKCS: return &rkcs;
    case RKWC: return &rkwc;
    case RKBA: return &rkba;
    case RKDA: return &rkda;
    case RKDB: return &rkdb;
    default: return NULL;
    }
}

/* reflect rk_l.done/ie into RKCS low-byte bits */
static void sync_rkcs_bits(void)
{
    rkcs &= (uint16_t)~RKCS_DONEB;
    if (rk_l.done) rkcs |= RKCS_DONEB;

    rkcs &= (uint16_t)~RKCS_IE;
    if (rk_l.ie) rkcs |= RKCS_IE;
}

static uint8_t rk_read8(uint16_t a)
{
    uint16_t *rp = reg_ptr(a);
    if (!rp) return 0;

    /* keep DONE/IE visible */
    if ((a & 0177776) == RKCS) sync_rkcs_bits();

    uint16_t v = *rp;
    if (a & 0000001) return (uint8_t)((v >> 8) & 000377);
    return (uint8_t)(v & 000377);
}

static void rk_write8(uint16_t a, uint8_t b)
{
    uint16_t *rp = reg_ptr(a);
    if (!rp) return;

    uint16_t old = *rp;
    uint16_t v;
    if (a & 0000001) v = (uint16_t)((old & 000377) | ((uint16_t)b << 8));
    else             v = (uint16_t)((old & 0177400) | b); /* minimal low-byte update */
    *rp = v;

    if ((a & 0177776) == RKCS && !(a & 0000001)) {
        /* low byte write to RKCS */

        /* IE follows bit 6. Per global policy:
           do NOT generate IRQ on IE toggle while DONE==1. */
        irq_latch_set_ie(&rk_l, (rkcs & RKCS_IE) ? 1 : 0);

        if (rkcs & RKCS_GO) {
            /* “Software clears DONE”: starting new GO command clears DONE and re-arms */
            irq_latch_sw_clear_done(&rk_l);

            /* clear DONE bit visibility until completion */
            sync_rkcs_bits();
        }
    }
}

int rk11_irq_pending(void) { return (rk_l.ie && rk_l.done) ? 1 : 0; }
void rk11_irq_ack(void) { }

int rk11_init(void)
{
    static const io_range_t r = { 0177400, 0177417, rk_read8, rk_write8, "RK11" };
    if (devio_register(&r) != 0) return -1;

    /* vector 000220, priority 5 */
    static const irq_source_t s = { "RK11", 000220, 5, rk11_irq_pending, rk11_irq_ack };
    if (irq_register(&s) != 0) return -1;

    rk11_reset();
    return 0;
}

void rk11_reset(void)
{
    rkds = 0;
    rker = 0;
    rkcs = 0;
    rkwc = 0;
    rkba = 0;
    rkda = 0;
    rkdb = 0;

    irq_latch_reset(&rk_l);

    /* starts idle: DONE=1 so boot can poll ready if it wants,
       but no IRQ unless IE already set */
    rk_l.done = 0;
    rk_l.irq_armed = 1;
    irq_latch_event_set_done(&rk_l);
    sync_rkcs_bits();
}

void rk11_poll(void)
{
    /* execute command only while GO is set */
    if (!(rkcs & RKCS_GO)) return;

    /* Support READ/WRITE only */
    uint16_t func = (uint16_t)(rkcs & RKCS_FUNC_MASK);
    if (func != RKCS_READ && func != RKCS_WRITE) {
        /* complete with error or just DONE */
        rker = 000001;
        rkcs &= (uint16_t)~RKCS_GO;
        irq_latch_event_set_done(&rk_l);
        sync_rkcs_bits();
        return;
    }

    if (!fp) {
        rker = 000001;
        rkcs &= (uint16_t)~RKCS_GO;
        irq_latch_event_set_done(&rk_l);
        sync_rkcs_bits();
        return;
    }

    int16_t wc = (int16_t)rkwc;
    int words = (wc < 0) ? -wc : 0;
    if (words <= 0) {
        rkcs &= (uint16_t)~RKCS_GO;
        irq_latch_event_set_done(&rk_l);
        sync_rkcs_bits();
        return;
    }

    uint32_t mem = rkba;
    uint16_t cur_da = rkda;
    uint16_t cur_wc = rkwc;
    int w_in_sec = 0;

    for (int i = 0; i < words; i++) {
        uint32_t lba = rk_lba(cur_da);
        uint32_t disk_base = lba * 01000;          /* 01000 bytes/sector */
        uint32_t off = disk_base + (uint32_t)w_in_sec * 2;

        if (fseek(fp, (long)off, SEEK_SET) != 0) { rker = 000001; break; }

        if (func == RKCS_READ) {
            uint8_t lo, hi;
            if (fread(&lo, 1, 1, fp) != 1) { rker = 000001; break; }
            if (fread(&hi, 1, 1, fp) != 1) { rker = 000001; break; }
            bus_write16((paddr_t)mem, (uint16_t)(lo | ((uint16_t)hi << 8)));
        } else {
            uint16_t w = bus_read16((paddr_t)mem);
            uint8_t lo = (uint8_t)(w & 000377);
            uint8_t hi = (uint8_t)((w >> 8) & 000377);
            if (fwrite(&lo, 1, 1, fp) != 1) { rker = 000001; break; }
            if (fwrite(&hi, 1, 1, fp) != 1) { rker = 000001; break; }
        }

        mem += 2;
        rkba = (uint16_t)(rkba + 2);
        cur_wc = (uint16_t)(cur_wc + 1);

        w_in_sec++;
        if (w_in_sec >= RK_WORDS_PER_SECTOR) {
            w_in_sec = 0;
            if (rk_next_sector(&cur_da) != 0) { rker = 000001; break; }
        }
    }

    rkda = cur_da;
    rkwc = cur_wc;

    /* command complete: clear GO, set DONE (once) */
    rkcs &= (uint16_t)~RKCS_GO;
    irq_latch_event_set_done(&rk_l);
    sync_rkcs_bits();
}

int rk11_open_image(const char *path)
{
    if (fp) fclose(fp);
    fp = fopen(path, "r+b");
    if (!fp) return -1;
    return 0;
}

void rk11_close_image(void)
{
    if (fp) { fclose(fp); fp = NULL; }
}

void rk11_set_sector_base(int one_based)
{
    sector_one_based = one_based ? 1 : 0;
}

int rk11_boot_copy(void *dest, size_t len)
{
    if (!fp) return -1;
    if (fseek(fp, 0, SEEK_SET) != 0) return -1;
    return (fread(dest, 1, len, fp) == len) ? 0 : -1;
}
