#include "dev_rk11.h"

#include "bus.h"
#include "devio.h"
#include "irq.h"
#include "irq_latch.h"
#include "emu_file.h"
#include "ubmap.h"

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

/* RKCS bits */
#define RKCS_GO        0000001
#define RKCS_FUNC_MASK 0000016
#define RKCS_MEX_MASK  0000060
#define RKCS_IDE       0000100
#define RKCS_RDY       0000200
#define RKCS_SSE       0000400
#define RKCS_EXB       0001000
#define RKCS_FMT       0002000
#define RKCS_IBA       0004000
#define RKCS_SCP       0020000
#define RKCS_HE        0040000
#define RKCS_ERR       0100000

/* RKCS functions (FUN bits 1..3) */
#define RKCS_FN_CTLRESET 0000000
#define RKCS_FN_WRITE    0000002
#define RKCS_FN_READ     0000004
#define RKCS_FN_WCHK     0000006
#define RKCS_FN_SEEK     0000010
#define RKCS_FN_RCHK     0000012
#define RKCS_FN_DRESET   0000014
#define RKCS_FN_WLOCK    0000016

/* RKER bits */
#define RKER_WCE 0000001
#define RKER_CSE 0000002
#define RKER_NXS 0000040
#define RKER_NXC 0000100
#define RKER_NXD 0000200
#define RKER_TE  0000400
#define RKER_DLT 0001000
#define RKER_NXM 0002000
#define RKER_PGE 0004000
#define RKER_SKE 0010000
#define RKER_WLK 0020000
#define RKER_OVR 0040000
#define RKER_DRE 0100000

#define RKER_SOFT_MASK (RKER_WCE | RKER_CSE)
#define RKER_HARD_MASK 0177740

/* RKDS bits */
#define RKDS_SECTOR_MASK 0000017
#define RKDS_SC_SA       0000020
#define RKDS_WPS         0000040
#define RKDS_RWS_RDY     0000100
#define RKDS_DRY         0000200
#define RKDS_SOK         0000400
#define RKDS_SIN         0001000
#define RKDS_DRU         0002000
#define RKDS_RK05        0004000
#define RKDS_DPL         0010000
#define RKDS_ID_MASK     0160000

/* RKDA fields */
#define RKDA_SECTOR_MASK 0000017
#define RKDA_SURF_MASK   0000020
#define RKDA_CYL_MASK    0017740
#define RKDA_DRIVE_MASK  0160000

/* RK05 geometry */
#define RK_SECTORS_PER_TRACK 0000014 /* 12 */
#define RK_WORDS_PER_SECTOR  0000400 /* 256 */
#define RK_MAX_CYL           0000312 /* 202 */
#define RK_MAX_SURF          1

static uint16_t rkds, rker, rkcs, rkwc, rkba, rkda, rkdb;
static uint8_t rkmex; /* 2-bit extension from RKCS bits 4..5 */
static uint8_t rk_write_lock[RK11_MAX_DRIVES];

static irq_latch_t rk_l;
static emu_file_t *rk_fp[RK11_MAX_DRIVES];
static uint8_t rk_img_read_only[RK11_MAX_DRIVES];
static int sector_one_based = 0;

static int rk_drive_valid(uint16_t drive)
{
    return (drive < RK11_MAX_DRIVES) ? 1 : 0;
}

static emu_file_t *rk_drive_fp(uint16_t drive)
{
    if (!rk_drive_valid(drive)) {
        return NULL;
    }
    return rk_fp[drive];
}

static int rk_drive_read_only(uint16_t drive)
{
    if (!rk_drive_valid(drive)) {
        return 0;
    }
    return rk_img_read_only[drive] ? 1 : 0;
}

static int rk_drive_attached(uint16_t drive)
{
    return (rk_drive_fp(drive) != NULL) ? 1 : 0;
}

static uint16_t rk_get_drive(uint16_t da)
{
    return (uint16_t)((da & RKDA_DRIVE_MASK) >> 13);
}

static uint16_t rk_get_sector(uint16_t da)
{
    return (uint16_t)(da & RKDA_SECTOR_MASK);
}

static uint16_t rk_get_surf(uint16_t da)
{
    return (uint16_t)((da & RKDA_SURF_MASK) >> 4);
}

static uint16_t rk_get_cyl(uint16_t da)
{
    return (uint16_t)((da & RKDA_CYL_MASK) >> 5);
}

static uint16_t rk_set_da(uint16_t drive, uint16_t cyl, uint16_t surf,
                          uint16_t sec)
{
    return (uint16_t)(((drive & 07) << 13) | ((cyl & 0377) << 5) |
                      ((surf & 01) << 4) | (sec & 017));
}

static int rk_da_to_lba(uint16_t da, uint32_t *lba_out)
{
    uint32_t cyl = rk_get_cyl(da);
    uint32_t surf = rk_get_surf(da);
    uint32_t sec = rk_get_sector(da);

    if (sector_one_based) {
        if (sec == 0) {
            sec = RK_SECTORS_PER_TRACK;
        }
        sec -= 1;
    }

    if (sec >= RK_SECTORS_PER_TRACK) {
        return -1;
    }
    if (surf > RK_MAX_SURF) {
        return -1;
    }
    if (cyl > RK_MAX_CYL) {
        return -1;
    }

    *lba_out = ((cyl * 2u + surf) * (uint32_t)RK_SECTORS_PER_TRACK) + sec;
    return 0;
}

static int rk_next_sector(uint16_t *da)
{
    uint16_t drive = rk_get_drive(*da);
    uint16_t cyl = rk_get_cyl(*da);
    uint16_t surf = rk_get_surf(*da);
    uint16_t sec = rk_get_sector(*da);

    sec++;
    if (sec >= RK_SECTORS_PER_TRACK) {
        sec = 0;
        surf++;
        if (surf > RK_MAX_SURF) {
            surf = 0;
            cyl++;
            if (cyl > RK_MAX_CYL) {
                return -1;
            }
        }
    }

    *da = rk_set_da(drive, cyl, surf, sec);
    return 0;
}

static void rk_clear_soft_errors(void)
{
    rker &= (uint16_t)~RKER_SOFT_MASK;
}

static void rk_clear_hard_errors(void)
{
    rker &= (uint16_t)~RKER_HARD_MASK;
}

static int rk_has_hard_error(void)
{
    return (rker & RKER_HARD_MASK) ? 1 : 0;
}

static void rk_sync_rkcs(void)
{
    uint16_t ro = 0;

    if (rk_l.done) {
        ro |= RKCS_RDY;
    }
    if (rkcs & RKCS_SCP) {
        ro |= RKCS_SCP;
    }
    if (rk_has_hard_error()) {
        ro |= RKCS_HE;
    }
    if (rker != 0) {
        ro |= RKCS_ERR;
    }

    rkcs &= (uint16_t)~(RKCS_RDY | RKCS_SCP | RKCS_HE | RKCS_ERR);
    rkcs |= ro;

    rkcs &= (uint16_t)~RKCS_MEX_MASK;
    rkcs |= (uint16_t)((rkmex & 03) << 4);

    if (rk_l.ie) {
        rkcs |= RKCS_IDE;
    } else {
        rkcs &= (uint16_t)~RKCS_IDE;
    }
}

static void rk_sync_rkds(void)
{
    uint16_t drive = rk_get_drive(rkda);
    uint16_t sec = rk_get_sector(rkda);
    uint16_t v = 0;

    v |= (uint16_t)(sec & RKDS_SECTOR_MASK);
    v |= RKDS_SC_SA;
    v |= RKDS_SOK;
    v |= RKDS_RK05;
    v |= (uint16_t)((drive & 07) << 13);

    if (rk_drive_attached(drive)) {
        v |= RKDS_DRY;
    }
    if (rk_l.done) {
        v |= RKDS_RWS_RDY;
    }
    if (rk_drive_read_only(drive) || rk_write_lock[drive]) {
        v |= RKDS_WPS;
    }

    rkds = v;
}

static void rk_sync_status(void)
{
    rk_sync_rkcs();
    rk_sync_rkds();
}

static void rk_set_error(uint16_t bit)
{
    rker |= bit;
    rk_sync_status();
}

static bus_paddr_t rk_pa(uint8_t mex, uint16_t ba)
{
    uint32_t uba = (((uint32_t)(mex & 03u) << 16) | (uint32_t)ba) & 000777777u;
    if (bus_machine() == BUS_MACHINE_PDP1184) {
        return ubmap_map_addr(uba);
    }
    return (bus_paddr_t)uba;
}

static void rk_ba_inc(uint16_t *ba, uint8_t *mex)
{
    uint16_t prev = *ba;

    *ba = (uint16_t)(*ba + 2);
    if (*ba < prev) {
        *mex = (uint8_t)((*mex + 1) & 03);
    }
}

static int rk_dma_read_word(bus_paddr_t pa, uint16_t *w)
{
    if (!bus_range_is_ram(pa, 2)) {
        return -1;
    }
    if (bus_is_nxm(pa) || bus_is_nxm((bus_paddr_t)(pa + 1))) {
        return -1;
    }
    *w = bus_read16(pa);
    return 0;
}

static int rk_dma_write_word(bus_paddr_t pa, uint16_t w)
{
    if (!bus_range_is_ram(pa, 2)) {
        return -1;
    }
    if (bus_is_nxm(pa) || bus_is_nxm((bus_paddr_t)(pa + 1))) {
        return -1;
    }
    bus_write16(pa, w);
    return 0;
}

static void rk_finish_command(void)
{
    rkcs &= (uint16_t)~RKCS_GO;
    irq_latch_event_set_done(&rk_l);
    rk_sync_status();
}

static void rk_do_control_reset(void)
{
    size_t i;

    rk_clear_hard_errors();
    rk_clear_soft_errors();
    for (i = 0; i < RK11_MAX_DRIVES; i++) {
        rk_write_lock[i] = 0;
    }
    rkwc = 0;
    rkba = 0;
    rkda = 0;
    rkdb = 0;
    rkmex = 0;

    rkcs &= (uint16_t)(RKCS_IDE | RKCS_SSE | RKCS_EXB | RKCS_FMT | RKCS_IBA);
    rkcs &= (uint16_t)~RKCS_SCP;

    rk_finish_command();
}

static int rk_validate_da_for_xfer(uint16_t da)
{
    uint16_t drive = rk_get_drive(da);
    uint16_t cyl = rk_get_cyl(da);
    uint16_t sec = rk_get_sector(da);

    if (!rk_drive_valid(drive) || !rk_drive_attached(drive)) {
        rk_set_error(RKER_NXD);
        return -1;
    }
    if (cyl > RK_MAX_CYL) {
        rk_set_error(RKER_NXC);
        return -1;
    }
    if (sec >= RK_SECTORS_PER_TRACK) {
        rk_set_error(RKER_NXS);
        return -1;
    }
    if (rk_get_surf(da) > RK_MAX_SURF) {
        rk_set_error(RKER_SKE);
        return -1;
    }
    return 0;
}

enum rk_xfer_mode {
    RK_XFER_READ = 0,
    RK_XFER_WRITE = 1,
    RK_XFER_WCHK = 2,
    RK_XFER_RCHK = 3
};

static void rk_transfer(enum rk_xfer_mode mode)
{
    uint16_t cur_wc = rkwc;
    uint16_t cur_ba = rkba;
    uint16_t cur_da = rkda;
    uint16_t drive = rk_get_drive(cur_da);
    uint8_t cur_mex = rkmex;
    int w_in_sector = 0;
    emu_file_t *fp = NULL;

    if (rk_validate_da_for_xfer(cur_da) != 0) {
        rk_finish_command();
        return;
    }

    fp = rk_drive_fp(drive);
    if (!fp) {
        rk_set_error(RKER_DRE);
        rk_finish_command();
        return;
    }

    if ((mode == RK_XFER_WRITE || mode == RK_XFER_WCHK) &&
            (rk_drive_read_only(drive) || rk_write_lock[drive])) {
        rk_set_error(RKER_WLK);
        rk_finish_command();
        return;
    }

    while (cur_wc != 0) {
        uint32_t lba = 0;
        uint32_t off = 0;
        uint16_t w = 0;
        uint8_t lo, hi;
        bus_paddr_t pa = rk_pa(cur_mex, cur_ba);

        if (rk_da_to_lba(cur_da, &lba) != 0) {
            rk_set_error(RKER_NXS);
            break;
        }
        if (w_in_sector == 0) {
            off = lba * 01000u;
            if (emu_fseek(fp, (long)off, EMU_SEEK_SET) != 0) {
                rk_set_error(RKER_DLT);
                break;
            }
        }

        if (mode == RK_XFER_READ || mode == RK_XFER_RCHK || mode == RK_XFER_WCHK) {
            if (emu_fread(&lo, 1, 1, fp) != 1 || emu_fread(&hi, 1, 1, fp) != 1) {
                rk_set_error(RKER_DLT);
                break;
            }
            w = (uint16_t)(lo | ((uint16_t)hi << 8));
        }

        if (mode == RK_XFER_READ) {
            if (rk_dma_write_word(pa, w) != 0) {
                rk_set_error(RKER_NXM);
                break;
            }
        } else if (mode == RK_XFER_WRITE) {
            if (rk_dma_read_word(pa, &w) != 0) {
                rk_set_error(RKER_NXM);
                break;
            }
            lo = (uint8_t)(w & 000377);
            hi = (uint8_t)((w >> 8) & 000377);
            if (emu_fwrite(&lo, 1, 1, fp) != 1 || emu_fwrite(&hi, 1, 1, fp) != 1) {
                rk_set_error(RKER_DLT);
                break;
            }
        } else if (mode == RK_XFER_WCHK) {
            uint16_t mw = 0;
            if (rk_dma_read_word(pa, &mw) != 0) {
                rk_set_error(RKER_NXM);
                break;
            }
            if (mw != w) {
                rk_set_error(RKER_WCE);
            }
        }

        if (!(rkcs & RKCS_IBA)) {
            rk_ba_inc(&cur_ba, &cur_mex);
        }
        cur_wc = (uint16_t)(cur_wc + 1);

        w_in_sector++;
        if (w_in_sector >= RK_WORDS_PER_SECTOR) {
            w_in_sector = 0;
            if (rk_next_sector(&cur_da) != 0) {
                if (cur_wc != 0) {
                    rk_set_error(RKER_OVR);
                }
                break;
            }
        }
    }

    rkwc = cur_wc;
    rkba = cur_ba;
    rkmex = (uint8_t)(cur_mex & 03);
    rkda = cur_da;
    rk_sync_status();

    if (mode == RK_XFER_WRITE) {
        emu_fflush(fp);
    }

    rk_finish_command();
}

static void rk_exec_command(void)
{
    uint16_t fn = rkcs & RKCS_FUNC_MASK;

    if (!(rkcs & RKCS_GO)) {
        return;
    }

    switch (fn) {
    case RKCS_FN_CTLRESET:
        rk_do_control_reset();
        return;

    case RKCS_FN_SEEK:
        if (rk_validate_da_for_xfer(rkda) != 0) {
            rk_finish_command();
            return;
        }
        rkcs |= RKCS_SCP;
        rk_finish_command();
        return;

    case RKCS_FN_DRESET:
        if (!rk_drive_valid(rk_get_drive(rkda))) {
            rk_set_error(RKER_NXD);
            rk_finish_command();
            return;
        }
        rkda = rk_set_da(rk_get_drive(rkda), 0, 0, 0);
        rkcs |= RKCS_SCP;
        rk_finish_command();
        return;

    case RKCS_FN_WLOCK:
        if (!rk_drive_valid(rk_get_drive(rkda))) {
            rk_set_error(RKER_NXD);
            rk_finish_command();
            return;
        }
        rk_write_lock[rk_get_drive(rkda)] = 1;
        rk_finish_command();
        return;

    case RKCS_FN_READ:
        rk_transfer(RK_XFER_READ);
        return;

    case RKCS_FN_WRITE:
        rk_transfer(RK_XFER_WRITE);
        return;

    case RKCS_FN_WCHK:
        if (rkcs & RKCS_FMT) {
            rk_set_error(RKER_PGE);
            rk_finish_command();
            return;
        }
        rk_transfer(RK_XFER_WCHK);
        return;

    case RKCS_FN_RCHK:
        if (rkcs & RKCS_FMT) {
            rk_set_error(RKER_PGE);
            rk_finish_command();
            return;
        }
        rk_transfer(RK_XFER_RCHK);
        return;

    default:
        rk_set_error(RKER_PGE);
        rk_finish_command();
        return;
    }
}

static uint8_t rk_read8(uint16_t addr)
{
    uint16_t base = (uint16_t)(addr & 0177776);
    uint16_t v = 0;

    rk_sync_status();

    switch (base) {
    case RKDS:
        v = rkds;
        break;
    case RKER:
        v = rker;
        break;
    case RKCS:
        v = rkcs;
        break;
    case RKWC:
        v = rkwc;
        break;
    case RKBA:
        v = rkba;
        break;
    case RKDA:
        v = rkda;
        break;
    case RKDB:
        v = rkdb;
        break;
    default:
        return 0;
    }

    if (addr & 1) {
        return (uint8_t)((v >> 8) & 000377);
    }
    return (uint8_t)(v & 000377);
}

static void rk_write8(uint16_t addr, uint8_t b)
{
    uint16_t base = (uint16_t)(addr & 0177776);
    uint16_t old;
    uint16_t v;

    rk_sync_status();

    switch (base) {
    case RKDS:
    case RKER:
        /* read-only */
        return;

    case RKWC:
        old = rkwc;
        if (addr & 1) {
            rkwc = (uint16_t)((old & 000377) | ((uint16_t)b << 8));
        } else {
            rkwc = (uint16_t)((old & 0177400) | b);
        }
        return;

    case RKBA:
        old = rkba;
        if (addr & 1) {
            rkba = (uint16_t)((old & 000377) | ((uint16_t)b << 8));
        } else {
            rkba = (uint16_t)((old & 0177400) | b);
        }
        return;

    case RKDA:
        if (!rk_l.done) {
            return;
        }
        old = rkda;
        if (addr & 1) {
            rkda = (uint16_t)((old & 000377) | ((uint16_t)b << 8));
        } else {
            rkda = (uint16_t)((old & 0177400) | b);
        }
        rk_sync_status();
        return;

    case RKDB:
        old = rkdb;
        if (addr & 1) {
            rkdb = (uint16_t)((old & 000377) | ((uint16_t)b << 8));
        } else {
            rkdb = (uint16_t)((old & 0177400) | b);
        }
        return;

    case RKCS:
        old = rkcs;
        if (addr & 1) {
            v = (uint16_t)(old & (uint16_t)~(RKCS_SSE | RKCS_EXB | RKCS_FMT | RKCS_IBA));
            v |= (uint16_t)(((uint16_t)b << 8) & (RKCS_SSE | RKCS_EXB | RKCS_FMT | RKCS_IBA));
            rkcs = v;
            rk_sync_status();
            return;
        }

        /* low byte: FUN/MEX/IDE writable; GO starts a command */
        rkcs &= (uint16_t)~(RKCS_FUNC_MASK | RKCS_MEX_MASK | RKCS_IDE);
        rkcs |= (uint16_t)(b & (RKCS_FUNC_MASK | RKCS_MEX_MASK | RKCS_IDE));
        rkmex = (uint8_t)((rkcs & RKCS_MEX_MASK) >> 4);
        irq_latch_set_ie(&rk_l, (rkcs & RKCS_IDE) ? 1 : 0);

        if (b & RKCS_GO) {
            if (rk_l.done) {
                rkcs |= RKCS_GO;
                rkcs &= (uint16_t)~RKCS_SCP;
                rk_clear_soft_errors();
                irq_latch_sw_clear_done(&rk_l);
            }
        } else {
            rkcs &= (uint16_t)~RKCS_GO;
        }
        rk_sync_status();
        return;

    default:
        return;
    }
}

int rk11_irq_pending(void)
{
    return rk_l.irq_req ? 1 : 0;
}

void rk11_irq_ack(void)
{
    irq_latch_ack(&rk_l);
}

int rk11_init(void)
{
    static const io_range_t r = {RK_BASE, RKDB, rk_read8, rk_write8, "RK11"};
    static const irq_source_t s = {"RK11", 000220, 5, rk11_irq_pending,
                                   rk11_irq_ack
                                  };

    if (devio_register(&r) != 0) {
        return -1;
    }
    if (irq_register(&s) != 0) {
        return -1;
    }

    rk11_reset();
    return 0;
}

void rk11_reset(void)
{
    size_t i;

    rkds = 0;
    rker = 0;
    rkcs = 0;
    rkwc = 0;
    rkba = 0;
    rkda = 0;
    rkdb = 0;
    rkmex = 0;
    for (i = 0; i < RK11_MAX_DRIVES; i++) {
        rk_write_lock[i] = 0;
    }

    irq_latch_reset(&rk_l);
    irq_latch_event_set_done(&rk_l);
    rk_sync_status();
}

void rk11_poll(void)
{
    rk_exec_command();
}

int rk11_open_image_unit(unsigned unit, const char *path)
{
    emu_file_t *f = NULL;
    uint16_t drive = (uint16_t)unit;

    if (!path || !rk_drive_valid(drive)) {
        rk_sync_status();
        return -1;
    }

    if (rk_fp[drive]) {
        emu_fclose(rk_fp[drive]);
        rk_fp[drive] = NULL;
    }

    rk_img_read_only[drive] = 0;
    f = emu_fopen(path, "r+b");
    if (!f) {
        f = emu_fopen(path, "rb");
        if (!f) {
            rk_sync_status();
            return -1;
        }
        rk_img_read_only[drive] = 1;
    }

    rk_fp[drive] = f;
    rk_sync_status();
    return 0;
}

int rk11_open_image(const char *path)
{
    return rk11_open_image_unit(0, path);
}

void rk11_close_image(void)
{
    size_t i;

    for (i = 0; i < RK11_MAX_DRIVES; i++) {
        if (rk_fp[i]) {
            emu_fclose(rk_fp[i]);
            rk_fp[i] = NULL;
        }
        rk_img_read_only[i] = 0;
        rk_write_lock[i] = 0;
    }
    rk_sync_status();
}

void rk11_set_sector_base(int one_based)
{
    sector_one_based = one_based ? 1 : 0;
}

void rk11_set_debug(int on)
{
    (void)on;
}

int rk11_boot_copy(void *dest, size_t len)
{
    return rk11_boot_copy_unit(0, dest, len);
}

int rk11_boot_copy_unit(unsigned unit, void *dest, size_t len)
{
    if (unit >= RK11_MAX_DRIVES || !rk_fp[unit]) {
        return -1;
    }
    if (emu_fseek(rk_fp[unit], 0, EMU_SEEK_SET) != 0) {
        return -1;
    }
    return (emu_fread(dest, 1, len, rk_fp[unit]) == len) ? 0 : -1;
}
