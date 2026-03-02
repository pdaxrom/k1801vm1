#include "dev_rh11.h"

#include "bus.h"
#include "ubmap.h"
#include "devio.h"
#include "irq.h"
#include "irq_latch.h"
#include "emu_file.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* RK611-compatible controller at RH11 slot in 16-bit I/O page view (octal). */
#define RH11_BASE 0177440

/* Registers (octal) */
#define RHCS1 0177440
#define RHWC  0177442
#define RHBA  0177444
#define RHDA  0177446
#define RHCS2 0177450
#define RHDS  0177452
#define RHER  0177454
#define RHAS  0177456
#define RHDC  0177460
#define RHSPR 0177462
#define RHDB  0177464
#define RHMR  0177466
#define RHEC1 0177470
#define RHEC2 0177472
#define RHMR2 0177474
#define RHMR3 0177476

/* CS1 bits */
#define RHCS1_GO          0000001
#define RHCS1_FUNC_MASK   0000076 /* includes spare bit 5 */
#define RHCS1_IE          0000100
#define RHCS1_RDY         0000200
#define RHCS1_BA16        0000400
#define RHCS1_BA17        0001000
#define RHCS1_BA_EXT_MASK (RHCS1_BA16 | RHCS1_BA17)
#define RHCS1_CERR_CCLR   0100000

/* RK611 function field values (GO bit excluded) */
#define RH11_FUNC_SELECT_DRIVE 0000000
#define RH11_FUNC_PACK_ACK     0000002
#define RH11_FUNC_DRIVE_CLEAR  0000004
#define RH11_FUNC_UNLOAD       0000006
#define RH11_FUNC_START_SPIN   0000010
#define RH11_FUNC_RECALIBRATE  0000012
#define RH11_FUNC_OFFSET       0000014
#define RH11_FUNC_SEEK         0000016
#define RH11_FUNC_READ_DATA    0000020
#define RH11_FUNC_WRITE_DATA   0000022
#define RH11_FUNC_READ_HEADER  0000024
#define RH11_FUNC_WRITE_HEADER 0000026
#define RH11_FUNC_WRITE_CHECK  0000030

/* Legacy probe compatibility */
#define RH11_FUNC_READ70       0000070
#define RH11_FUNC_WRITE60      0000060

/* CS2 bits */
#define RHCS2_DS_MASK  0000007
#define RHCS2_BAI      0000020
#define RHCS2_SCLR     0000040
#define RHCS2_IR       0000100
#define RHCS2_UFE      0000400
#define RHCS2_MDS      0001000
#define RHCS2_PGE      0002000
#define RHCS2_NEM      0004000
#define RHCS2_NED      0010000
#define RHCS2_UPE      0020000
#define RHCS2_WCE      0040000
#define RHCS2_DLT      0100000
#define RHCS2_ERR_MASK                                                     \
  (RHCS2_UFE | RHCS2_MDS | RHCS2_PGE | RHCS2_NEM | RHCS2_NED | RHCS2_UPE | \
   RHCS2_WCE | RHCS2_DLT)
#define RHCS2_RW_MASK (RHCS2_DS_MASK | RHCS2_BAI | RHCS2_SCLR)

/* RKER bits (subset used) */
#define RHER_ILF   0000001
#define RHER_IDAE  0002000
#define RHER_OPI   0020000

/* Legacy compatibility bit for older tests/tools */
#define RHER_LEGACY_NXM 0000002

/* RKDS base signature used by bootstrap/probes */
#define RHDS_BASE_100701 0100701
#define RHDS_DRDY        0000200

/* RK06/RK07 geometry in 16-bit mode */
#define RH11_HEADS_PER_CYL     0000003
#define RH11_SECTORS_PER_TRACK 0000026
#define RH11_WORDS_PER_SECTOR  0000400
#define RH11_SECTOR_BYTES      0001000

#define RKDA_SEC_MASK   0000037
#define RKDA_HEAD_MASK  0003400
#define RKDA_HEAD_SHIFT 8

static uint16_t rhcs1, rhwc, rhba, rhda, rhcs2, rhds, rher, rhas, rhdc;
static uint16_t rhspr, rhdb, rhmr, rhmr2, rhmr3;
static irq_latch_t rh_l;
static emu_file_t *rh_fp = NULL;
static int rh_debug = 0;
static int rh_debug_active = 0;
static int rh_sec_one_based = 0;

static uint8_t rh_cs1_ba_ext_get(void)
{
    return (uint8_t)((rhcs1 & RHCS1_BA_EXT_MASK) >> 8);
}

static void rh_spare_sync_from_cs1(void)
{
    rhspr &= (uint16_t)~0000003;
    rhspr |= (uint16_t)(rh_cs1_ba_ext_get() & 03);
}

static void rh_cs1_ba_ext_set(uint8_t ext)
{
    rhcs1 &= (uint16_t)~RHCS1_BA_EXT_MASK;
    rhcs1 |= (uint16_t)(((uint16_t)(ext & 03)) << 8);
    rh_spare_sync_from_cs1();
}

static uint16_t rh_da_get_sec(uint16_t da)
{
    return (uint16_t)(da & RKDA_SEC_MASK);
}

static uint16_t rh_da_get_head(uint16_t da)
{
    return (uint16_t)((da & RKDA_HEAD_MASK) >> RKDA_HEAD_SHIFT);
}

static uint16_t rh_da_set_head_sec(uint16_t da, uint16_t head, uint16_t sec)
{
    da &= (uint16_t)~(RKDA_HEAD_MASK | RKDA_SEC_MASK);
    da |= (uint16_t)(((head & 07) << RKDA_HEAD_SHIFT) | (sec & RKDA_SEC_MASK));
    return da;
}

static uint16_t *rh_rw_reg_ptr(uint16_t addr)
{
    switch (addr & 0177776) {
    case RHCS1:
        return &rhcs1;
    case RHWC:
        return &rhwc;
    case RHBA:
        return &rhba;
    case RHDA:
        return &rhda;
    case RHCS2:
        return &rhcs2;
    case RHAS:
        return &rhas;
    case RHDC:
        return &rhdc;
    case RHSPR:
        return &rhspr;
    case RHDB:
        return &rhdb;
    case RHMR:
        return &rhmr;
    default:
        return NULL;
    }
}

static int rh_lba(uint16_t dc, uint16_t da, uint32_t *lba_out)
{
    uint16_t sec = rh_da_get_sec(da);
    uint16_t head = rh_da_get_head(da);

    if (rh_sec_one_based) {
        if (sec == 0) {
            sec = RH11_SECTORS_PER_TRACK;
        }
        sec -= 1;
    }

    if (sec >= RH11_SECTORS_PER_TRACK || head >= RH11_HEADS_PER_CYL) {
        return -1;
    }

    *lba_out =
        (((uint32_t)dc * RH11_HEADS_PER_CYL) + (uint32_t)head) * RH11_SECTORS_PER_TRACK +
        (uint32_t)sec;
    return 0;
}

static void rh_advance_sector(uint16_t *dc, uint16_t *da)
{
    uint16_t sec = rh_da_get_sec(*da);
    uint16_t head = rh_da_get_head(*da);

    sec++;
    if (sec >= RH11_SECTORS_PER_TRACK) {
        sec = 0;
        head++;
        if (head >= RH11_HEADS_PER_CYL) {
            head = 0;
            *dc = (uint16_t)(*dc + 1);
        }
    }

    *da = rh_da_set_head_sec(*da, head, sec);
}

static void rh_sync_status(void)
{
    rhcs1 &= (uint16_t)~(RHCS1_RDY | RHCS1_IE | RHCS1_CERR_CCLR);
    if (rh_l.done) {
        rhcs1 |= RHCS1_RDY;
    }
    if (rh_l.ie) {
        rhcs1 |= RHCS1_IE;
    }
    if (rher || (rhcs2 & RHCS2_ERR_MASK)) {
        rhcs1 |= RHCS1_CERR_CCLR;
    }
}

static void rh_set_rher_error(uint16_t bit)
{
    rher |= bit;
    rhcs2 |= RHCS2_IR;
    rh_sync_status();
}

static void rh_set_cs2_error(uint16_t bit)
{
    rhcs2 |= bit;
    rhcs2 |= RHCS2_IR;
    rh_sync_status();
}

static void rh_ba_inc(uint16_t *ba, uint8_t *ext)
{
    uint16_t prev = *ba;
    *ba = (uint16_t)(*ba + 2);
    if (*ba < prev) {
        *ext = (uint8_t)((*ext + 1) & 03);
    }
}

static paddr_t rh_dma_pa(uint16_t ba, uint8_t ext)
{
    uint32_t uba = (((uint32_t)(ext & 03u)) << 16) | (uint32_t)ba; /* 18-bit */
    if (bus_machine() == BUS_MACHINE_PDP1184) {
        return ubmap_map_addr(uba & 000777777u);
    }
    return (paddr_t)(uba & 000777777u);
}

static int rh_dma_read_word(uint16_t ba, uint8_t ext, uint16_t *w)
{
    paddr_t pa = rh_dma_pa(ba, ext);
    if (!bus_range_is_ram(pa, 2)) {
        return -1;
    }
    if (bus_is_nxm(pa) || bus_is_nxm((paddr_t)(pa + 1))) {
        return -1;
    }
    *w = bus_read16(pa);
    return 0;
}

static int rh_dma_write_word(uint16_t ba, uint8_t ext, uint16_t w)
{
    paddr_t pa = rh_dma_pa(ba, ext);
    if (!bus_range_is_ram(pa, 2)) {
        return -1;
    }
    if (bus_is_nxm(pa) || bus_is_nxm((paddr_t)(pa + 1))) {
        return -1;
    }
    bus_write16(pa, w);
    return 0;
}

static void rh_finish_command(void)
{
    rhcs1 &= (uint16_t)~RHCS1_GO;
    rhds |= RHDS_DRDY;
    irq_latch_event_set_done(&rh_l);
    rh_sync_status();

    if (rh_debug_active) {
        fprintf(stderr,
                "RH11 DONE cs1=%06o wc=%06o ba=%06o ext=%o dc=%06o da=%06o cs2=%06o er=%06o\n",
                rhcs1, rhwc, rhba, rh_cs1_ba_ext_get(), rhdc, rhda, rhcs2, rher);
        rh_debug_active = 0;
    }
}

static void rh_controller_clear(void)
{
    rhwc = 0;
    rhba = 0;
    rhda = 0;
    rhdc = 0;
    rhspr = 0;
    rhdb = 0;
    rhmr = 0;
    rhmr2 = 0;
    rhmr3 = 0;
    rher = 0;
    rhcs2 &= RHCS2_DS_MASK;
    rhcs2 |= RHCS2_IR;

    irq_latch_sw_clear_done(&rh_l);
    irq_latch_event_set_done(&rh_l);
    rhcs1 &= (uint16_t)~RHCS1_GO;
    rh_sync_status();
}

enum rh_xfer_mode {
    RH_XFER_READ = 0,
    RH_XFER_WRITE = 1,
    RH_XFER_WCHECK = 2
};

static int rh_read_sector(uint32_t lba, uint8_t *buf)
{
    uint32_t off = lba * RH11_SECTOR_BYTES;
    size_t got;

    if (emu_fseek(rh_fp, (long)off, EMU_SEEK_SET) != 0) {
        return -1;
    }

    got = emu_fread(buf, 1, RH11_SECTOR_BYTES, rh_fp);
    if (got == RH11_SECTOR_BYTES) {
        return 0;
    }
    if (got < RH11_SECTOR_BYTES && emu_feof(rh_fp)) {
        memset(buf + got, 0, RH11_SECTOR_BYTES - got);
        emu_clearerr(rh_fp);
        return 0;
    }
    return -1;
}

static int rh_write_sector(uint32_t lba, const uint8_t *buf)
{
    uint32_t off = lba * RH11_SECTOR_BYTES;
    if (emu_fseek(rh_fp, (long)off, EMU_SEEK_SET) != 0) {
        return -1;
    }
    return (emu_fwrite(buf, 1, RH11_SECTOR_BYTES, rh_fp) == RH11_SECTOR_BYTES)
           ? 0
           : -1;
}

static void rh_transfer(enum rh_xfer_mode mode)
{
    int16_t wc = (int16_t)rhwc;
    int words = (wc < 0) ? -wc : 0;
    uint16_t cur_wc = rhwc;
    uint16_t cur_ba = rhba;
    uint16_t cur_dc = rhdc;
    uint16_t cur_da = rhda;
    uint8_t cur_ext = rh_cs1_ba_ext_get();
    int w_in_sec = 0;
    int bai = (rhcs2 & RHCS2_BAI) ? 1 : 0;
    uint8_t secbuf[RH11_SECTOR_BYTES];
    uint32_t sec_lba = 0;
    int sec_valid = 0;
    int sec_dirty = 0;
    int had_error = 0;

    if ((rhcs2 & RHCS2_DS_MASK) != 0) {
        rh_set_cs2_error(RHCS2_NED);
        rh_finish_command();
        return;
    }

    if (!rh_fp) {
        rh_set_cs2_error(RHCS2_NED);
        rh_finish_command();
        return;
    }

    if (words <= 0) {
        rh_finish_command();
        return;
    }

    if (rh_debug) {
        fprintf(stderr,
                "RH11 XFER start wc=%06o words=%d ba=%06o ext=%o dc=%06o da=%06o mode=%d bai=%d\n",
                rhwc, words, cur_ba, cur_ext, cur_dc, cur_da, mode, bai);
    }

    for (int i = 0; i < words; i++) {
        uint32_t lba = 0;
        uint16_t w = 0;
        unsigned sec_off = 0;

        if (rh_lba(cur_dc, cur_da, &lba) != 0) {
            rh_set_rher_error(RHER_IDAE);
            had_error = 1;
            break;
        }

        if (!sec_valid || sec_lba != lba) {
            if (sec_dirty) {
                if (rh_write_sector(sec_lba, secbuf) != 0) {
                    rh_set_cs2_error(RHCS2_DLT);
                    had_error = 1;
                    break;
                }
                sec_dirty = 0;
            }
            if (rh_read_sector(lba, secbuf) != 0) {
                rh_set_cs2_error(RHCS2_DLT);
                had_error = 1;
                break;
            }
            sec_lba = lba;
            sec_valid = 1;
        }

        sec_off = (unsigned)w_in_sec * 2u;
        if (mode == RH_XFER_READ || mode == RH_XFER_WCHECK) {
            w = (uint16_t)(secbuf[sec_off] |
                           ((uint16_t)secbuf[sec_off + 1] << 8));
        }

        if (mode == RH_XFER_READ) {
            if (rh_dma_write_word(cur_ba, cur_ext, w) != 0) {
                rh_set_cs2_error(RHCS2_NEM);
                rher |= RHER_LEGACY_NXM;
                had_error = 1;
                break;
            }
        } else if (mode == RH_XFER_WRITE) {
            if (rh_dma_read_word(cur_ba, cur_ext, &w) != 0) {
                rh_set_cs2_error(RHCS2_NEM);
                rher |= RHER_LEGACY_NXM;
                had_error = 1;
                break;
            }
            secbuf[sec_off] = (uint8_t)(w & 000377);
            secbuf[sec_off + 1] = (uint8_t)((w >> 8) & 000377);
            sec_dirty = 1;
        } else { /* RH_XFER_WCHECK */
            uint16_t memw = 0;
            if (rh_dma_read_word(cur_ba, cur_ext, &memw) != 0) {
                rh_set_cs2_error(RHCS2_NEM);
                rher |= RHER_LEGACY_NXM;
                had_error = 1;
                break;
            }
            if (memw != w) {
                rh_set_cs2_error(RHCS2_WCE);
                had_error = 1;
                break;
            }
        }

        cur_wc = (uint16_t)(cur_wc + 1);
        if (!bai) {
            rh_ba_inc(&cur_ba, &cur_ext);
        }

        w_in_sec++;
        if (w_in_sec >= RH11_WORDS_PER_SECTOR) {
            if (sec_dirty) {
                if (rh_write_sector(sec_lba, secbuf) != 0) {
                    rh_set_cs2_error(RHCS2_DLT);
                    had_error = 1;
                    break;
                }
                sec_dirty = 0;
            }
            sec_valid = 0;
            w_in_sec = 0;
            rh_advance_sector(&cur_dc, &cur_da);
        }
    }

    if (sec_dirty) {
        if (rh_write_sector(sec_lba, secbuf) != 0) {
            rh_set_cs2_error(RHCS2_DLT);
            had_error = 1;
        }
    }

    if (mode == RH_XFER_WRITE) {
        emu_fflush(rh_fp);
    }

    rhwc = cur_wc;
    rhba = cur_ba;
    rhdc = cur_dc;
    rhda = cur_da;
    rh_cs1_ba_ext_set(cur_ext);
    if (rh_debug) {
        fprintf(stderr,
                "RH11 XFER end   wc=%06o ba=%06o ext=%o dc=%06o da=%06o err=%d\n",
                rhwc, rhba, rh_cs1_ba_ext_get(), rhdc, rhda, had_error);
    }
    (void)had_error;
    rh_finish_command();
}

static uint8_t rh_read8(uint16_t addr)
{
    uint16_t base = (uint16_t)(addr & 0177776);
    uint16_t v = 0;

    rh_sync_status();

    switch (base) {
    case RHCS1:
        v = rhcs1;
        break;
    case RHWC:
        v = rhwc;
        break;
    case RHBA:
        v = rhba;
        break;
    case RHDA:
        v = rhda;
        break;
    case RHCS2:
        v = rhcs2;
        break;
    case RHDS:
        v = rhds;
        break;
    case RHER:
        v = rher;
        break;
    case RHAS:
        v = rhas;
        break;
    case RHDC:
        v = rhdc;
        break;
    case RHSPR:
        v = rhspr;
        break;
    case RHDB:
        v = rhdb;
        break;
    case RHMR:
        v = rhmr;
        break;
    case RHEC1:
    case RHEC2:
        v = 0;
        break;
    case RHMR2:
        v = rhmr2;
        break;
    case RHMR3:
        v = rhmr3;
        break;
    default:
        return 0;
    }

    if (addr & 1) {
        return (uint8_t)((v >> 8) & 000377);
    }
    return (uint8_t)(v & 000377);
}

static void rh_write8(uint16_t addr, uint8_t b)
{
    uint16_t base = (uint16_t)(addr & 0177776);
    uint16_t old;
    uint16_t v;

    if (base == RHDS || base == RHER || base == RHEC1 || base == RHEC2 ||
            base == RHMR2 || base == RHMR3) {
        return; /* read-only registers */
    }

    old = 0;
    if (base == RHCS1) {
        old = rhcs1;
    } else {
        uint16_t *rp = rh_rw_reg_ptr(addr);
        if (!rp) {
            return;
        }
        old = *rp;
    }

    if (addr & 1) {
        v = (uint16_t)((old & 000377) | ((uint16_t)b << 8));
    } else {
        v = (uint16_t)((old & 0177400) | b);
    }

    if ((rhcs1 & RHCS1_GO) && base != RHCS1) {
        rh_set_cs2_error(RHCS2_PGE);
        return;
    }

    switch (base) {
    case RHCS1: {
        int old_go = (rhcs1 & RHCS1_GO) ? 1 : 0;
        int old_ie = (rhcs1 & RHCS1_IE) ? 1 : 0;

        if (addr & 1) {
            /* Controller clear request (bit 15) */
            if (((uint16_t)b << 8) & RHCS1_CERR_CCLR) {
                if (rh_debug && ((old & RHCS1_IE) || rh_l.ie || rh_l.irq_req)) {
                    fprintf(stderr,
                            "RH11 CS1 hi write cclr old=%06o b=%03o done=%o ie=%o irq=%o\n",
                            old, b, rh_l.done, rh_l.ie, rh_l.irq_req);
                }
                rh_controller_clear();
                return;
            }
            /* BA16/17 writable in this model */
            rh_cs1_ba_ext_set((uint8_t)(((uint16_t)b >> 0) & 03));
            if (rh_debug && ((old & RHCS1_IE) || (rhcs1 & RHCS1_IE) || rh_l.irq_req)) {
                fprintf(stderr,
                        "RH11 CS1 hi write old=%06o b=%03o new=%06o done=%o ie=%o irq=%o\n",
                        old, b, rhcs1, rh_l.done, rh_l.ie, rh_l.irq_req);
            }
            rh_sync_status();
            return;
        }

        rhcs1 &= (uint16_t)~(RHCS1_FUNC_MASK | RHCS1_IE);
        rhcs1 |= (uint16_t)(b & (RHCS1_FUNC_MASK | RHCS1_IE));
        irq_latch_set_ie(&rh_l, (rhcs1 & RHCS1_IE) ? 1 : 0);
        if (!old_ie && rh_l.ie && (b & RHCS1_RDY)) {
            rh_l.irq_req = 1;
        }

        if ((b & RHCS1_GO) && !old_go && rh_l.done) {
            irq_latch_sw_clear_done(&rh_l);
            rhcs1 |= RHCS1_GO;
            rher = 0;
            rhcs2 &= RHCS2_RW_MASK;
            rhcs2 |= RHCS2_IR;
            rhds &= (uint16_t)~RHDS_DRDY;
            if (rh_debug) {
                rh_debug_active = 1;
                fprintf(stderr,
                        "RH11 GO cs1=%06o wc=%06o ba=%06o ext=%o dc=%06o da=%06o cs2=%06o func=%03o\n",
                        rhcs1, rhwc, rhba, rh_cs1_ba_ext_get(), rhdc, rhda, rhcs2,
                        rhcs1 & RHCS1_FUNC_MASK);
            }
        }
        if (rh_debug && ((b & RHCS1_IE) || rh_l.irq_req)) {
            fprintf(stderr,
                    "RH11 CS1 lo write old=%06o b=%03o new=%06o done=%o ie=%o irq=%o\n",
                    old, b, rhcs1, rh_l.done, rh_l.ie, rh_l.irq_req);
        }
        rh_sync_status();
        return;
    }

    case RHWC:
        rhwc = v;
        return;

    case RHBA:
        rhba = (uint16_t)(v & 0177776); /* word-aligned */
        return;

    case RHDA:
        rhda = v;
        return;

    case RHCS2:
        if (addr & 1) {
            return; /* high byte is status/error in this model */
        }
        rhcs2 &= (uint16_t)~RHCS2_RW_MASK;
        rhcs2 |= (uint16_t)(b & RHCS2_RW_MASK);
        if (rhcs2 & RHCS2_SCLR) {
            rh11_reset();
            return;
        }
        rh_sync_status();
        return;

    case RHAS:
        rhas = v;
        return;

    case RHDC:
        rhdc = v;
        return;

    case RHSPR:
        rhspr = v;
        rh_cs1_ba_ext_set((uint8_t)(rhspr & 03));
        return;

    case RHDB:
        rhdb = v;
        return;

    case RHMR:
        rhmr = v;
        return;

    default:
        return;
    }
}

int rh11_irq_pending(void)
{
    return rh_l.irq_req ? 1 : 0;
}

void rh11_irq_ack(void)
{
    irq_latch_ack(&rh_l);
}

int rh11_init(void)
{
    static const io_range_t r = {RH11_BASE, (uint16_t)(RHMR3 + 1), rh_read8,
                                 rh_write8, "RH11"
                                };
    static const irq_source_t s = {"RH11", 000210, 5, rh11_irq_pending,
                                   rh11_irq_ack
                                  };

    rh_debug = (getenv("RH11_DEBUG") != NULL);
    rh_sec_one_based = (getenv("RH11_SECTOR_ONE_BASED") != NULL);
    if (devio_register(&r) != 0) {
        return -1;
    }
    if (irq_register(&s) != 0) {
        return -1;
    }

    rh11_reset();
    return 0;
}

void rh11_reset(void)
{
    rhcs1 = 0;
    rhwc = 0;
    rhba = 0;
    rhda = 0;
    rhcs2 = RHCS2_IR;
    rhds = RHDS_BASE_100701;
    rher = 0;
    rhas = 0;
    rhdc = 0;
    rhspr = 0;
    rhdb = 0;
    rhmr = 0;
    rhmr2 = 0;
    rhmr3 = 0;

    irq_latch_reset(&rh_l);
    irq_latch_event_set_done(&rh_l);
    rh_sync_status();
}

void rh11_poll(void)
{
    uint16_t func;

    if (!(rhcs1 & RHCS1_GO)) {
        return;
    }

    func = (uint16_t)(rhcs1 & RHCS1_FUNC_MASK);
    switch (func) {
    case RH11_FUNC_SELECT_DRIVE:
    case RH11_FUNC_PACK_ACK:
    case RH11_FUNC_DRIVE_CLEAR:
    case RH11_FUNC_UNLOAD:
    case RH11_FUNC_START_SPIN:
    case RH11_FUNC_RECALIBRATE:
    case RH11_FUNC_OFFSET:
    case RH11_FUNC_SEEK:
    case RH11_FUNC_READ_HEADER:
    case RH11_FUNC_WRITE_HEADER:
        rh_finish_command();
        return;

    case RH11_FUNC_READ_DATA:
    case RH11_FUNC_READ70:
        rh_transfer(RH_XFER_READ);
        return;

    case RH11_FUNC_WRITE_DATA:
    case RH11_FUNC_WRITE60:
        rh_transfer(RH_XFER_WRITE);
        return;

    case RH11_FUNC_WRITE_CHECK:
        rh_transfer(RH_XFER_WCHECK);
        return;

    default:
        rh_set_rher_error(RHER_ILF);
        rh_finish_command();
        return;
    }
}

int rh11_open_image(const char *path)
{
    if (rh_fp) {
        emu_fclose(rh_fp);
    }
    rh_fp = emu_fopen(path, "r+b");
    if (!rh_fp) {
        return -1;
    }
    return 0;
}

void rh11_close_image(void)
{
    if (rh_fp) {
        emu_fclose(rh_fp);
        rh_fp = NULL;
    }
}

void rh11_set_debug(int on)
{
    rh_debug = on ? 1 : 0;
}

int rh11_boot_copy(void *dest, size_t len)
{
    if (!rh_fp) {
        return -1;
    }
    if (emu_fseek(rh_fp, 0, EMU_SEEK_SET) != 0) {
        return -1;
    }
    return (emu_fread(dest, 1, len, rh_fp) == len) ? 0 : -1;
}
