#include "dev_rl11.h"

#include "bus.h"
#include "devio.h"
#include "irq.h"
#include "irq_latch.h"
#include "emu_file.h"
#include "time_compat.h"
#include "ubmap.h"

#include <stdint.h>
#include <string.h>

/* RL11 registers (octal) */
#define RL11_BASE 0174400
#define RLCS      0174400
#define RLBA      0174402
#define RLDA      0174404
#define RLMP      0174406
#define RLBAE     0174410

/* RLCS bits */
#define RLCS_DRDY      0000001
#define RLCS_FUNC_MASK 0000016
#define RLCS_BA_MASK   0000060
#define RLCS_IE        0000100
#define RLCS_CRDY      0000200
#define RLCS_DS_MASK   0001400
#define RLCS_E_MASK    0036000
#define RLCS_DE        0040000
#define RLCS_ERR       0100000

/* RLCS function codes (stored in bits 1..3) */
#define RLCS_FN_NOOP   0000000
#define RLCS_FN_WCHK   0000002
#define RLCS_FN_GSTAT  0000004
#define RLCS_FN_SEEK   0000006
#define RLCS_FN_RHDR   0000010
#define RLCS_FN_WRITE  0000012
#define RLCS_FN_READ   0000014
#define RLCS_FN_RDNHC  0000016

/* CSR error code (bits 10..13) */
#define RLCS_E_OPI  0001
#define RLCS_E_DCRC 0002 /* read DCRC or write-check error */
#define RLCS_E_HCRC 0003
#define RLCS_E_DLT  0004
#define RLCS_E_HNF  0005
#define RLCS_E_NXM  0010
#define RLCS_E_MPE  0011

/* RLV12 bus address extension register (octal). */
#define RLBAE_IMP 0000077

/* MP (Get Status) bits */
#define RLMP_ST_LOCKON 0000005 /* STC:STA = 101 */
#define RLMP_BH        0000010
#define RLMP_HO        0000020
#define RLMP_HS        0000100
#define RLMP_DT        0000200 /* 0=RL01, 1=RL02 */
#define RLMP_DSE       0000400
#define RLMP_VC        0001000
#define RLMP_WGE       0002000
#define RLMP_SPE       0004000
#define RLMP_SKTO      0010000
#define RLMP_WL        0020000

/* DA fields for data transfer commands */
#define RLDA_SA_MASK 0000077
#define RLDA_HS      0000100
#define RLDA_CA_MASK 0177600
#define RLDA_CA_SHIFT 7

/* DA fields for seek command */
#define RLDA_SEEK_MARK 0000001
#define RLDA_SEEK_BIT  0000002
#define RLDA_DIR       0000004
#define RLDA_SEEK_HS   0000020
#define RLDA_DIFF_MASK 0177600
#define RLDA_DIFF_SHIFT 7

/* Geometry (from RL01/RL02 docs) */
#define RL_SECTORS_PER_TRACK 0000050 /* 40 decimal */
#define RL_HEADS_PER_CYL     0000002
#define RL_WORDS_PER_SECTOR  0000200 /* 128 words */
#define RL_BYTES_PER_SECTOR  0000400 /* 256 bytes */
#define RL01_CYLINDERS       0000400 /* 256 */
#define RL02_CYLINDERS       0001000 /* 512 */

#define RL_MAX_DRIVES 4

typedef struct {
    emu_file_t *fp;
    int type;
    int read_only;
    uint16_t cyl;
    uint16_t head;
    uint16_t rot_sector;
    uint16_t status_latch;
} rl_drive_t;

static uint16_t rlcs, rlba, rlda, rlmp, rlbae;
static uint8_t rl_err_code;
static uint8_t rl_drive_error;
static uint8_t rl_busy;
static uint64_t rl_ready_ns;

static irq_latch_t rl_l;
static rl_drive_t rl_drv[RL_MAX_DRIVES];

/* Read Header returns three words sequentially via MP reads. */
static uint16_t rl_rhdr_fifo[3];
static int rl_rhdr_len;
static int rl_rhdr_idx;

static size_t rl_image_bytes_for_type(int type)
{
    size_t cyl = (type == RL11_TYPE_RL02) ? RL02_CYLINDERS : RL01_CYLINDERS;
    return cyl * RL_HEADS_PER_CYL * RL_SECTORS_PER_TRACK * RL_BYTES_PER_SECTOR;
}

static uint16_t rl_max_cyl_for_type(int type)
{
    return (type == RL11_TYPE_RL02) ? (RL02_CYLINDERS - 1) : (RL01_CYLINDERS - 1);
}

static int rl_selected_drive_index(void)
{
    return (int)((rlcs & RLCS_DS_MASK) >> 8);
}

static rl_drive_t *rl_selected_drive(void)
{
    int idx = rl_selected_drive_index();
    if (idx < 0 || idx >= RL_MAX_DRIVES) {
        return NULL;
    }
    return &rl_drv[idx];
}

static uint8_t rl_ba_ext_get(void)
{
    return (uint8_t)(rlbae & RLBAE_IMP);
}

static void rl_ba_ext_set(uint8_t ext)
{
    rlbae = (uint16_t)(ext & RLBAE_IMP);
    rlcs &= (uint16_t)~RLCS_BA_MASK;
    rlcs |= (uint16_t)(((uint16_t)(rlbae & 03)) << 4);
}

static void rl_clear_errors(void)
{
    rl_err_code = 0;
    rl_drive_error = 0;
}

static void rl_set_error(uint8_t ecode, int drive_error)
{
    rl_err_code = (uint8_t)(ecode & 017);
    if (drive_error) {
        rl_drive_error = 1;
    }
}

static void rl_sync_cs(void)
{
    uint16_t rw;
    rl_drive_t *d = rl_selected_drive();
    uint16_t drdy = 0;

    rw = (uint16_t)(rlcs & (RLCS_FUNC_MASK | RLCS_DS_MASK));
    if (rl_l.ie) {
        rw |= RLCS_IE;
    }
    if (rl_l.done) {
        rw |= RLCS_CRDY;
    }
    if (d && d->fp && !rl_busy) {
        drdy = RLCS_DRDY;
    }
    rw |= drdy;
    rw |= (uint16_t)(((uint16_t)(rl_err_code & 017)) << 10);
    if (rl_drive_error) {
        rw |= RLCS_DE;
    }
    if (rl_drive_error || rl_err_code) {
        rw |= RLCS_ERR;
    }
    rlcs = rw;
    rlcs |= (uint16_t)(((uint16_t)(rlbae & 03)) << 4);
}

static void rl_rhdr_reset_fifo(void)
{
    rl_rhdr_len = 0;
    rl_rhdr_idx = 0;
}

static uint16_t rl_rhdr_peek_word(void)
{
    if (rl_rhdr_len > 0 && rl_rhdr_idx < rl_rhdr_len) {
        return rl_rhdr_fifo[rl_rhdr_idx];
    }
    return rlmp;
}

static void rl_rhdr_advance_word(void)
{
    if (rl_rhdr_len > 0 && rl_rhdr_idx < rl_rhdr_len) {
        rl_rhdr_idx++;
        if (rl_rhdr_idx < rl_rhdr_len) {
            rlmp = rl_rhdr_fifo[rl_rhdr_idx];
        } else {
            rl_rhdr_reset_fifo();
        }
    }
}

static bus_paddr_t rl_pa(uint16_t ba, uint8_t ext)
{
    /* On 11/84 DMA is presented on 18-bit UBA and translated by UBMAP. */
    if (bus_machine() == BUS_MACHINE_PDP1184) {
        uint32_t uba = (((uint32_t)(ext & 03u) << 16) | (uint32_t)ba) & 000777777u;
        return ubmap_map_addr(uba);
    }
    return (bus_paddr_t)(((uint32_t)(ext & RLBAE_IMP) << 16) | ba);
}

static void rl_ba_inc(uint16_t *ba, uint8_t *ext)
{
    uint16_t prev = *ba;
    *ba = (uint16_t)(*ba + 2);
    if (*ba < prev) {
        *ext = (uint8_t)((*ext + 1) & RLBAE_IMP);
    }
}

/* CRC16 as implemented by DEC 9401-compatible logic (SIMH-compatible). */
static uint16_t rl_calc_crc16(int wc, const uint16_t *data)
{
    uint32_t crc = 0;
    int i;

    for (i = 0; i < wc; i++) {
        uint32_t d = data[i];
        uint32_t j;
        for (j = 0; j < 16; j++) {
            crc = (crc & (uint32_t)~1u) | ((crc & 1u) ^ (d & 1u));
            if (crc & 1u) {
                crc = (crc >> 1) ^ 0120001u;
            } else {
                crc >>= 1;
            }
            d >>= 1;
        }
    }
    return (uint16_t)crc;
}

static int rl_read_sector(rl_drive_t *d, uint32_t lba, uint8_t *buf)
{
    uint32_t off = lba * RL_BYTES_PER_SECTOR;
    size_t got;

    if (emu_fseek(d->fp, (long)off, EMU_SEEK_SET) != 0) {
        return -1;
    }
    got = emu_fread(buf, 1, RL_BYTES_PER_SECTOR, d->fp);
    if (got == RL_BYTES_PER_SECTOR) {
        return 0;
    }
    if (got < RL_BYTES_PER_SECTOR && emu_feof(d->fp)) {
        memset(buf + got, 0, RL_BYTES_PER_SECTOR - got);
        emu_clearerr(d->fp);
        return 0;
    }
    return -1;
}

static int rl_write_sector(rl_drive_t *d, uint32_t lba, const uint8_t *buf)
{
    uint32_t off = lba * RL_BYTES_PER_SECTOR;
    if (emu_fseek(d->fp, (long)off, EMU_SEEK_SET) != 0) {
        return -1;
    }
    return (emu_fwrite(buf, 1, RL_BYTES_PER_SECTOR, d->fp) == RL_BYTES_PER_SECTOR)
           ? 0
           : -1;
}

static int rl_dma_read_word(uint16_t ba, uint8_t ext, uint16_t *w)
{
    bus_paddr_t pa = rl_pa(ba, ext);
    if (!bus_range_is_ram(pa, 2)) {
        return -1;
    }
    if (bus_is_nxm(pa) || bus_is_nxm((bus_paddr_t)(pa + 1))) {
        return -1;
    }
    *w = bus_read16(pa);
    return 0;
}

static int rl_dma_write_word(uint16_t ba, uint8_t ext, uint16_t w)
{
    bus_paddr_t pa = rl_pa(ba, ext);
    if (!bus_range_is_ram(pa, 2)) {
        return -1;
    }
    if (bus_is_nxm(pa) || bus_is_nxm((bus_paddr_t)(pa + 1))) {
        return -1;
    }
    bus_write16(pa, w);
    return 0;
}

static int rl_da_to_lba(const rl_drive_t *d, uint16_t da, uint32_t *lba_out)
{
    uint16_t sec = (uint16_t)(da & RLDA_SA_MASK);
    uint16_t head = (da & RLDA_HS) ? 1 : 0;
    uint16_t cyl = (uint16_t)((da & RLDA_CA_MASK) >> RLDA_CA_SHIFT);

    if (sec >= RL_SECTORS_PER_TRACK) {
        return -1;
    }
    if (head >= RL_HEADS_PER_CYL) {
        return -1;
    }
    if (cyl > rl_max_cyl_for_type(d->type)) {
        return -1;
    }

    *lba_out = (((uint32_t)cyl * RL_HEADS_PER_CYL) + head) * RL_SECTORS_PER_TRACK +
               (uint32_t)sec;
    return 0;
}

static uint16_t rl_status_word(const rl_drive_t *d)
{
    uint16_t st = RLMP_ST_LOCKON | RLMP_BH | RLMP_HO;

    if (!d || !d->fp) {
        return (uint16_t)(st | RLMP_DSE);
    }
    if (d->head) {
        st |= RLMP_HS;
    }
    if (d->type == RL11_TYPE_RL02) {
        st |= RLMP_DT;
    }
    if (d->read_only) {
        st |= RLMP_WL;
    }
    st |= d->status_latch;
    return st;
}

static void rl_finish_command(void)
{
    rl_busy = 0;
    irq_latch_event_set_done(&rl_l);
    rl_sync_cs();
}

static void rl_exec_noop(void)
{
    rl_finish_command();
}

static void rl_exec_get_status(void)
{
    rl_drive_t *d = rl_selected_drive();
    uint16_t da_flags = (uint16_t)(rlda & 0000017);

    /* Per RL11 programming model, bits 0 and 1 must be 1 for GET STATUS. */
    if ((da_flags & 0000003) != 0000003) {
        rl_set_error(RLCS_E_OPI, 0);
        rl_finish_command();
        return;
    }
    /* Bits 2 and 4..15 are not valid for GET STATUS command encoding. */
    if (da_flags & 0000004) {
        rl_set_error(RLCS_E_OPI, 0);
        rl_finish_command();
        return;
    }

    if (!d || !d->fp) {
        rl_set_error(RLCS_E_HNF, 0);
        rlmp = RLMP_DSE;
        rl_finish_command();
        return;
    }

    if (rlda & 0000010) { /* DA bit 3: reset drive faults */
        d->status_latch &= (uint16_t)~(RLMP_VC | RLMP_WGE | RLMP_SPE | RLMP_SKTO);
        rl_drive_error = 0;
    }

    rlmp = rl_status_word(d);
    rl_finish_command();
}

static void rl_exec_seek(void)
{
    rl_drive_t *d = rl_selected_drive();
    uint16_t diff;
    uint16_t cyl;
    int dir;

    if (!d || !d->fp) {
        rl_set_error(RLCS_E_HNF, 0);
        rl_finish_command();
        return;
    }

    /* Marker/seek format check for RL11 seek parameter word in DAR. */
    if ((rlda & RLDA_SEEK_MARK) == 0 || (rlda & RLDA_SEEK_BIT) != 0) {
        rl_set_error(RLCS_E_OPI, 0);
        rl_finish_command();
        return;
    }

    diff = (uint16_t)((rlda & RLDA_DIFF_MASK) >> RLDA_DIFF_SHIFT);
    dir = (rlda & RLDA_DIR) ? 1 : 0;
    cyl = d->cyl;

    d->head = (rlda & RLDA_SEEK_HS) ? 1 : 0;

    if (dir) {
        uint32_t nc = (uint32_t)cyl + diff;
        if (nc > rl_max_cyl_for_type(d->type)) {
            d->cyl = rl_max_cyl_for_type(d->type);
        } else {
            d->cyl = (uint16_t)nc;
        }
    } else {
        if (diff > cyl) {
            d->cyl = 0;
        } else {
            d->cyl = (uint16_t)(cyl - diff);
        }
    }

    rl_finish_command();
}

static void rl_exec_read_header(void)
{
    rl_drive_t *d = rl_selected_drive();
    uint16_t sec;
    uint16_t w1;
    uint16_t hdr[2];

    if (!d || !d->fp) {
        rl_set_error(RLCS_E_HNF, 0);
        rl_finish_command();
        return;
    }

    sec = (uint16_t)(d->rot_sector % RL_SECTORS_PER_TRACK);

    w1 = (uint16_t)(((d->cyl & 0777) << RLDA_CA_SHIFT) |
                    ((d->head ? 1 : 0) << 6) | (sec & RLDA_SA_MASK));
    hdr[0] = w1;
    hdr[1] = 0;
    rl_rhdr_fifo[0] = hdr[0];
    rl_rhdr_fifo[1] = hdr[1];
    rl_rhdr_fifo[2] = rl_calc_crc16(2, hdr);
    rl_rhdr_len = 3;
    rl_rhdr_idx = 0;
    rlmp = rl_rhdr_fifo[0];
    d->rot_sector = (uint16_t)((d->rot_sector + 1) % RL_SECTORS_PER_TRACK);
    rl_finish_command();
}

enum rl_xfer_mode {
    RL_XFER_WRITE = 0,
    RL_XFER_READ = 1,
    RL_XFER_WCHK = 2,
};

static void rl_exec_xfer(enum rl_xfer_mode mode)
{
    rl_drive_t *d = rl_selected_drive();
    uint16_t fn = (uint16_t)(rlcs & RLCS_FUNC_MASK);
    int is_rdnhc = (mode == RL_XFER_READ && fn == RLCS_FN_RDNHC) ? 1 : 0;
    uint16_t cur_mp = rlmp;
    uint16_t wc_req;
    uint16_t wc_lim;
    uint16_t wc_xfer;
    uint16_t wc_done = 0;
    uint16_t cur_ba = rlba;
    uint16_t cur_da = rlda;
    uint8_t cur_ext = rl_ba_ext_get();
    uint16_t cur_sec = 0;
    uint16_t maxwc = 0;
    int w_in_sector = 0;
    int had_error = 0;
    uint8_t secbuf[RL_BYTES_PER_SECTOR];
    uint32_t sec_lba = 0;
    int sec_valid = 0;
    int sec_dirty = 0;
    int sec_word = 0;

    if (!d || !d->fp) {
        rl_set_error(RLCS_E_HNF, 0);
        rl_finish_command();
        return;
    }

    if (mode == RL_XFER_WRITE && d->read_only) {
        d->status_latch |= RLMP_WL;
        rl_set_error(RLCS_E_OPI, 1);
        rl_finish_command();
        return;
    }

    if (is_rdnhc) {
        /*
         * READ WITHOUT HEADER CHECK starts from current rotational sector
         * on the currently selected cylinder/head.
         */
        if (d->rot_sector >= RL_SECTORS_PER_TRACK) {
            rl_set_error(RLCS_E_HNF, 0);
            rl_finish_command();
            return;
        }
        cur_da = (uint16_t)((d->cyl << RLDA_CA_SHIFT) | (d->head ? RLDA_HS : 0) |
                            (d->rot_sector & RLDA_SA_MASK));
    } else {
        uint16_t da_sec = (uint16_t)(cur_da & RLDA_SA_MASK);
        uint16_t da_cyl = (uint16_t)(cur_da & RLDA_CA_MASK);
        uint16_t cur_cyl = (uint16_t)((d->cyl << RLDA_CA_SHIFT) & RLDA_CA_MASK);
        /*
         * SIMH-compatible rule: header-checked data transfer compares cylinder,
         * while sector must be in range.
         */
        if (da_sec >= RL_SECTORS_PER_TRACK || da_cyl != cur_cyl) {
            rl_set_error(RLCS_E_HNF, 0);
            rl_finish_command();
            return;
        }
    }

    cur_sec = (uint16_t)(cur_da & RLDA_SA_MASK);
    maxwc = (uint16_t)((RL_SECTORS_PER_TRACK - cur_sec) * RL_WORDS_PER_SECTOR);
    wc_req = (uint16_t)((0200000u - cur_mp) & 0177777u);
    wc_lim = wc_req;
    if (wc_lim > maxwc) {
        wc_lim = maxwc;
    }
    wc_xfer = wc_lim;

    while (wc_xfer != 0) {
        uint32_t lba = 0;
        uint16_t w = 0;
        unsigned sec_off = 0;

        if (rl_da_to_lba(d, cur_da, &lba) != 0) {
            rl_set_error(RLCS_E_HNF, 0);
            had_error = 1;
            break;
        }

        if (!sec_valid || sec_lba != lba) {
            if (sec_dirty) {
                if (rl_write_sector(d, sec_lba, secbuf) != 0) {
                    rl_set_error(RLCS_E_DLT, 0);
                    had_error = 1;
                    break;
                }
                sec_dirty = 0;
            }
            if (rl_read_sector(d, lba, secbuf) != 0) {
                rl_set_error(RLCS_E_DLT, 0);
                had_error = 1;
                break;
            }
            sec_lba = lba;
            sec_valid = 1;
            sec_word = 0;
        }

        sec_off = (unsigned)sec_word * 2u;
        if (mode == RL_XFER_READ || mode == RL_XFER_WCHK) {
            w = (uint16_t)(secbuf[sec_off] |
                           ((uint16_t)secbuf[sec_off + 1] << 8));
        }

        if (mode == RL_XFER_READ) {
            if (rl_dma_write_word(cur_ba, cur_ext, w) != 0) {
                rl_set_error(RLCS_E_NXM, 0);
                had_error = 1;
                break;
            }
        } else if (mode == RL_XFER_WRITE) {
            if (rl_dma_read_word(cur_ba, cur_ext, &w) != 0) {
                rl_set_error(RLCS_E_NXM, 0);
                had_error = 1;
                break;
            }
            secbuf[sec_off] = (uint8_t)(w & 000377);
            secbuf[sec_off + 1] = (uint8_t)((w >> 8) & 000377);
            sec_dirty = 1;
        } else { /* RL_XFER_WCHK */
            uint16_t mw = 0;
            if (rl_dma_read_word(cur_ba, cur_ext, &mw) != 0) {
                rl_set_error(RLCS_E_NXM, 0);
                had_error = 1;
                break;
            }
            if (mw != w) {
                rl_set_error(RLCS_E_DCRC, 0);
                had_error = 1;
                break;
            }
        }

        wc_xfer--;
        wc_done++;
        rl_ba_inc(&cur_ba, &cur_ext);

        sec_word++;
        w_in_sector++;
        if (w_in_sector >= RL_WORDS_PER_SECTOR) {
            uint16_t sec = (uint16_t)(cur_da & RLDA_SA_MASK);
            uint16_t head = (cur_da & RLDA_HS) ? 1 : 0;
            uint16_t cyl = (uint16_t)((cur_da & RLDA_CA_MASK) >> RLDA_CA_SHIFT);

            if (sec_dirty) {
                if (rl_write_sector(d, sec_lba, secbuf) != 0) {
                    rl_set_error(RLCS_E_DLT, 0);
                    had_error = 1;
                    break;
                }
                sec_dirty = 0;
            }
            sec_valid = 0;
            sec_word = 0;
            w_in_sector = 0;
            sec++;
            cur_da = (uint16_t)((cyl << RLDA_CA_SHIFT) | (head ? RLDA_HS : 0) |
                                (sec & RLDA_SA_MASK));
        }
    }

    /*
     * For partial-sector write completion with no transfer error, RL11 fills
     * the remainder of the current sector with zeros before command completion.
     */
    if (!had_error && mode == RL_XFER_WRITE && w_in_sector > 0) {
        if (!sec_valid) {
            uint32_t lba = 0;
            if (rl_da_to_lba(d, cur_da, &lba) != 0 || rl_read_sector(d, lba, secbuf) != 0) {
                rl_set_error(RLCS_E_DLT, 0);
                had_error = 1;
            } else {
                sec_lba = lba;
                sec_valid = 1;
                sec_word = w_in_sector;
            }
        }
        while (!had_error && w_in_sector < RL_WORDS_PER_SECTOR) {
            unsigned off = (unsigned)sec_word * 2u;
            secbuf[off] = 0;
            secbuf[off + 1] = 0;
            sec_word++;
            w_in_sector++;
        }
        if (!had_error) {
            sec_dirty = 1;
        }
    }

    if (sec_dirty) {
        if (rl_write_sector(d, sec_lba, secbuf) != 0) {
            rl_set_error(RLCS_E_DLT, 0);
            had_error = 1;
        }
    }

    /*
     * RL11 keeps MP as a full 16-bit incrementing counter.
     * Track overrun/incomplete is reflected by a non-zero final MP.
     */
    cur_mp = (uint16_t)((cur_mp + wc_done) & 0177777u);
    rlmp = cur_mp;
    if (!had_error && wc_req > maxwc) {
        rl_set_error(RLCS_E_OPI, 0);
    } else if (!had_error && rlmp != 0) {
        rl_set_error(RLCS_E_HNF, 0);
    }

    rlba = (uint16_t)(cur_ba & 0177776);
    rl_ba_ext_set(cur_ext);
    rlda = (uint16_t)(rlda + ((wc_done + (RL_WORDS_PER_SECTOR - 1)) /
                              RL_WORDS_PER_SECTOR));

    /*
     * SIMH-compatible head position update.
     * For RDNHC, position advances from current rotational sector.
     * For header-checked commands, track follows DA (with sector wrap to 0).
     */
    if (is_rdnhc) {
        uint16_t sec = (uint16_t)(d->rot_sector +
                                  ((wc_done + (RL_WORDS_PER_SECTOR - 1)) /
                                   RL_WORDS_PER_SECTOR));
        if (sec >= RL_SECTORS_PER_TRACK) {
            d->rot_sector = 0;
        } else {
            d->rot_sector = sec;
        }
    } else {
        uint16_t sec = (uint16_t)(rlda & RLDA_SA_MASK);
        d->head = (rlda & RLDA_HS) ? 1 : 0;
        d->cyl = (uint16_t)((rlda & RLDA_CA_MASK) >> RLDA_CA_SHIFT);
        if (sec >= RL_SECTORS_PER_TRACK) {
            d->rot_sector = 0;
        } else {
            d->rot_sector = sec;
        }
    }

    if (mode == RL_XFER_WRITE) {
        emu_fflush(d->fp);
    }

    rl_finish_command();
}

static void rl_exec_command(void)
{
    uint16_t fn = (uint16_t)(rlcs & RLCS_FUNC_MASK);

    switch (fn) {
    case RLCS_FN_NOOP:
        rl_exec_noop();
        return;
    case RLCS_FN_WCHK:
        rl_exec_xfer(RL_XFER_WCHK);
        return;
    case RLCS_FN_GSTAT:
        rl_exec_get_status();
        return;
    case RLCS_FN_SEEK:
        rl_exec_seek();
        return;
    case RLCS_FN_RHDR:
        rl_exec_read_header();
        return;
    case RLCS_FN_WRITE:
        rl_exec_xfer(RL_XFER_WRITE);
        return;
    case RLCS_FN_READ:
    case RLCS_FN_RDNHC:
        rl_exec_xfer(RL_XFER_READ);
        return;
    default:
        rl_set_error(RLCS_E_OPI, 0);
        rl_finish_command();
        return;
    }
}

static uint8_t rl_read8(uint16_t addr)
{
    uint16_t base = (uint16_t)(addr & 0177776);
    uint16_t v = 0;

    rl_sync_cs();

    switch (base) {
    case RLCS:
        v = rlcs;
        break;
    case RLBA:
        v = rlba;
        break;
    case RLDA:
        v = rlda;
        break;
    case RLMP:
        v = rl_rhdr_peek_word();
        break;
    case RLBAE:
        v = (uint16_t)(rlbae & RLBAE_IMP);
        break;
    default:
        return 0;
    }

    if (addr & 1) {
        if (base == RLMP) {
            rl_rhdr_advance_word();
        }
        return (uint8_t)((v >> 8) & 000377);
    }
    return (uint8_t)(v & 000377);
}

static void rl_write8(uint16_t addr, uint8_t b)
{
    uint16_t base = (uint16_t)(addr & 0177776);
    uint16_t old = 0;
    uint16_t v;

    if (base == RLCS) {
        old = rlcs;
    } else if (base == RLBA) {
        old = rlba;
    } else if (base == RLDA) {
        old = rlda;
    } else if (base == RLMP) {
        old = rlmp;
    } else if (base == RLBAE) {
        old = rlbae;
    } else {
        return;
    }

    if (addr & 1) {
        v = (uint16_t)((old & 000377) | ((uint16_t)b << 8));
    } else {
        v = (uint16_t)((old & 0177400) | b);
    }

    switch (base) {
    case RLCS:
        if (addr & 1) {
            rlcs &= (uint16_t)~RLCS_DS_MASK;
            rlcs |= (uint16_t)(((uint16_t)b << 8) & RLCS_DS_MASK);
            rl_sync_cs();
            return;
        }

        rlcs &= (uint16_t)~(RLCS_FUNC_MASK | RLCS_BA_MASK | RLCS_IE);
        rlcs |= (uint16_t)(b & (RLCS_FUNC_MASK | RLCS_BA_MASK | RLCS_IE));
        rlbae = (uint16_t)((rlbae & (uint16_t)~0000003u) |
                           (uint16_t)((b & RLCS_BA_MASK) >> 4));
        irq_latch_set_ie(&rl_l, (rlcs & RLCS_IE) ? 1 : 0);

        if ((b & RLCS_CRDY) == 0 && rl_l.done) {
            irq_latch_sw_clear_done(&rl_l);
            rl_busy = 1;
            /*
             * Autoconfig probes expect RL11 completion/IRQ almost immediately.
             * A fixed wall-clock 10ms delay causes "didn't interrupt" during
             * BSD 2.9.1 device probing on fast hosts.
             */
            rl_ready_ns = lsi11_now_ns();
            rl_clear_errors();
            rl_rhdr_reset_fifo();
            rl_sync_cs();
            return;
        }
        rl_sync_cs();
        return;

    case RLBA:
        rlba = (uint16_t)(v & 0177776);
        return;

    case RLDA:
        rlda = v;
        return;

    case RLMP:
        rlmp = v;
        rl_rhdr_reset_fifo();
        return;

    case RLBAE:
        rl_ba_ext_set((uint8_t)v);
        rl_sync_cs();
        return;

    default:
        return;
    }
}

int rl11_irq_pending(void)
{
    return rl_l.irq_req ? 1 : 0;
}

void rl11_irq_ack(void)
{
    irq_latch_ack(&rl_l);
}

int rl11_init(void)
{
    static const io_range_t r = {RL11_BASE, (uint16_t)(RLBAE + 1), rl_read8,
                                 rl_write8, "RL11"
                                };
    static const irq_source_t s = {"RL11", 000160, 5, rl11_irq_pending,
                                   rl11_irq_ack
                                  };

    if (devio_register(&r) != 0) {
        return -1;
    }
    if (irq_register(&s) != 0) {
        return -1;
    }

    rl11_reset();
    return 0;
}

void rl11_reset(void)
{
    for (int i = 0; i < RL_MAX_DRIVES; i++) {
        rl_drv[i].cyl = 0;
        rl_drv[i].head = 0;
        rl_drv[i].rot_sector = 0;
        rl_drv[i].status_latch = 0;
    }

    rlcs = 0;
    rlba = 0;
    rlda = 0;
    rlmp = 0;
    rlbae = 0;
    rl_busy = 0;
    rl_clear_errors();
    rl_rhdr_reset_fifo();

    irq_latch_reset(&rl_l);
    irq_latch_event_set_done(&rl_l);
    rl_sync_cs();
}

void rl11_poll(void)
{
    if (!rl_busy) {
        return;
    }
    if (lsi11_now_ns() < rl_ready_ns) {
        return;
    }
    rl_exec_command();
}

int rl11_open_image_typed_unit(unsigned unit, const char *path, int type)
{
    emu_file_t *f = NULL;
    int read_only = 0;
    long sz = 0;
    int dtype = type;
    rl_drive_t *d = NULL;

    if (!path || unit >= RL_MAX_DRIVES) {
        rl_sync_cs();
        return -1;
    }
    d = &rl_drv[unit];

    if (d->fp) {
        emu_fclose(d->fp);
        d->fp = NULL;
    }

    f = emu_fopen(path, "r+b");
    if (!f) {
        f = emu_fopen(path, "rb");
        if (!f) {
            rl_sync_cs();
            return -1;
        }
        read_only = 1;
    }

    if (emu_fseek(f, 0, EMU_SEEK_END) == 0) {
        sz = emu_ftell(f);
        emu_fseek(f, 0, EMU_SEEK_SET);
    }

    if (dtype == RL11_TYPE_AUTO) {
        if (sz > 0 && (size_t)sz > rl_image_bytes_for_type(RL11_TYPE_RL01)) {
            dtype = RL11_TYPE_RL02;
        } else {
            dtype = RL11_TYPE_RL01;
        }
    }

    if (dtype != RL11_TYPE_RL01 && dtype != RL11_TYPE_RL02) {
        emu_fclose(f);
        return -1;
    }

    d->fp = f;
    d->type = dtype;
    d->read_only = read_only;
    d->cyl = 0;
    d->head = 0;
    d->rot_sector = 0;
    d->status_latch = read_only ? RLMP_WL : 0;
    d->status_latch |= RLMP_VC;

    rl_sync_cs();
    return 0;
}

int rl11_open_image(const char *path)
{
    return rl11_open_image_typed_unit(0, path, RL11_TYPE_AUTO);
}

int rl11_open_image_typed(const char *path, int type)
{
    return rl11_open_image_typed_unit(0, path, type);
}

void rl11_close_image(void)
{
    size_t i;

    for (i = 0; i < RL_MAX_DRIVES; i++) {
        rl_drive_t *d = &rl_drv[i];
        if (d->fp) {
            emu_fclose(d->fp);
            d->fp = NULL;
        }
        d->type = RL11_TYPE_AUTO;
        d->read_only = 0;
        d->cyl = 0;
        d->head = 0;
        d->rot_sector = 0;
        d->status_latch = 0;
    }
    rl_sync_cs();
}

void rl11_set_debug(int on)
{
    (void)on;
}

int rl11_boot_copy(void *dest, size_t len)
{
    rl_drive_t *d = &rl_drv[0];
    if (!d->fp) {
        return -1;
    }
    if (emu_fseek(d->fp, 0, EMU_SEEK_SET) != 0) {
        return -1;
    }
    return (emu_fread(dest, 1, len, d->fp) == len) ? 0 : -1;
}
