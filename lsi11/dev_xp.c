#include "dev_xp.h"

#include "bus.h"
#include "devio.h"
#include "irq.h"
#include "irq_latch.h"
#include "emu_file.h"

#include <stdint.h>
#include <string.h>

/* XP (RP/RM over RH70 MBA) CSR block (octal). */
#define XP_BASE 0176700
#define XPCS1   0176700
#define XPWC    0176702
#define XPBA    0176704
#define XPDA    0176706
#define XPCS2   0176710
#define XPDS    0176712
#define XPER1   0176714
#define XPAS    0176716
#define XPLA    0176720
#define XPDB    0176722
#define XPMR    0176724
#define XPDT    0176726
#define XPSN    0176730
#define XPOF    0176732
#define XPDC    0176734
#define XPCC    0176736
#define XPER2   0176740
#define XPER3   0176742
#define XPEC1   0176744
#define XPEC2   0176746
#define XPBAE   0176750
#define XPCS3   0176752

/* CS1 bits */
#define XPCS1_GO       0000001
#define XPCS1_FNC_MASK 0000076
#define XPCS1_IE       0000100
#define XPCS1_DONE     0000200
#define XPCS1_UAE      0001400
#define XPCS1_DVA      0004000
#define XPCS1_TRE      0040000
#define XPCS1_SC       0100000

/* CS2 bits */
#define XPCS2_UNIT_MASK 0000007
#define XPCS2_UAI       0000010
#define XPCS2_PAT       0000020
#define XPCS2_CLR       0000040
#define XPCS2_IR        0000100
#define XPCS2_OR        0000200
#define XPCS2_MDPE      0000400
#define XPCS2_MXF       0001000
#define XPCS2_PGE       0002000
#define XPCS2_NEM       0004000
#define XPCS2_NED       0010000
#define XPCS2_PE        0020000
#define XPCS2_WCE       0040000
#define XPCS2_DLT       0100000
#define XPCS2_RW_MASK                                                     \
  (XPCS2_UNIT_MASK | XPCS2_UAI | XPCS2_PAT | XPCS2_MXF | XPCS2_PE)
#define XPCS2_ERR_MASK                                                    \
  (XPCS2_MDPE | XPCS2_MXF | XPCS2_PGE | XPCS2_NEM | XPCS2_NED | XPCS2_PE | \
   XPCS2_WCE | XPCS2_DLT)

/* CS3 bits */
#define XPCS3_WCO      0010000
#define XPCS3_WCE      0004000
#define XPCS3_ERR_MASK (XPCS3_WCO | XPCS3_WCE)
#define XPCS3_RW_MASK  (XPCS1_IE | 0000017)

/* Drive status (DS) bits */
#define XPDS_VV  0000100
#define XPDS_RDY 0000200
#define XPDS_DPR 0000400
#define XPDS_LST 0002000
#define XPDS_MOL 0010000
#define XPDS_ERR 0040000
#define XPDS_ATA 0100000

/* Drive error register bits (ER1) */
#define XPER1_ILF 0000001
#define XPER1_IAE 0002000
#define XPER1_WLE 0004000
#define XPER1_AOE 0001000
#define XPER1_UNS 0040000

/* DA/DC fields */
#define XPDA_SEC_MASK 0000077
#define XPDA_SF_SHIFT 8
#define XPDA_SF_MASK  (0000077 << XPDA_SF_SHIFT)
#define XPDC_CYL_MASK 0001777

/* RPOF mask */
#define XPOF_MBZ 0161400

/* RM05 geometry */
#define XP_SECTORS_PER_SURF 32u
#define XP_SURFACES         19u
#define XP_CYLINDERS        823u
#define XP_WORDS_PER_SECTOR 256u
#define XP_BYTES_PER_SECTOR 512u
#define XP_TOTAL_SECTORS (XP_SECTORS_PER_SURF * XP_SURFACES * XP_CYLINDERS)
#define XP_TOTAL_WORDS   (XP_TOTAL_SECTORS * XP_WORDS_PER_SECTOR)
#define XP_DT_RM05       020027u

/* Function codes (unshifted; CS1 stores function in bits 1..5). */
#define XPF_NOP     000u
#define XPF_UNLOAD  001u
#define XPF_SEEK    002u
#define XPF_RECAL   003u
#define XPF_DCLR    004u
#define XPF_RELEASE 005u
#define XPF_OFFSET  006u
#define XPF_RETURN  007u
#define XPF_PRESET  010u
#define XPF_PACK    011u
#define XPF_SEARCH  014u
#define XPF_XFER    024u
#define XPF_WCHK    024u
#define XPF_WRITE   030u
#define XPF_WRITEH  031u
#define XPF_READ    034u
#define XPF_READH   035u

#define XP_BAE_IMP 0000077

static uint16_t xpcs1, xpwc, xpba, xpcs2, xpdb, xpbae, xpcs3;

static uint16_t xpds[XP_MAX_DRIVES];
static uint16_t xper1[XP_MAX_DRIVES];
static uint16_t xpda[XP_MAX_DRIVES];
static uint16_t xpmr[XP_MAX_DRIVES];
static uint16_t xpof[XP_MAX_DRIVES];
static uint16_t xpdc[XP_MAX_DRIVES];
static uint16_t xpcc[XP_MAX_DRIVES];
static uint16_t xper2[XP_MAX_DRIVES];
static uint16_t xper3[XP_MAX_DRIVES];
static uint16_t xpec1[XP_MAX_DRIVES];
static uint16_t xpec2[XP_MAX_DRIVES];

static irq_latch_t xp_l;
static emu_file_t *xp_fp[XP_MAX_DRIVES];
static uint8_t xp_img_read_only[XP_MAX_DRIVES];

static int xp_drive_valid(uint16_t unit)
{
    return (unit < XP_MAX_DRIVES) ? 1 : 0;
}

static uint16_t xp_selected_unit(void)
{
    return (uint16_t)(xpcs2 & XPCS2_UNIT_MASK);
}

static emu_file_t *xp_drive_fp(uint16_t unit)
{
    if (!xp_drive_valid(unit)) {
        return NULL;
    }
    return xp_fp[unit];
}

static int xp_drive_read_only(uint16_t unit)
{
    if (!xp_drive_valid(unit)) {
        return 0;
    }
    return xp_img_read_only[unit] ? 1 : 0;
}

static void xp_cs1_uae_sync_from_bae(void)
{
    xpcs1 &= (uint16_t)~XPCS1_UAE;
    xpcs1 |= (uint16_t)(((uint16_t)(xpbae & 03)) << 8);
}

static void xp_bae_sync_from_cs1_uae(uint16_t uae)
{
    xpbae &= (uint16_t)~03;
    xpbae |= (uint16_t)((uae >> 8) & 03);
    xp_cs1_uae_sync_from_bae();
}

static void xp_update_drive_status(uint16_t unit)
{
    uint16_t ds = 0;

    if (!xp_drive_valid(unit)) {
        return;
    }

    ds |= XPDS_DPR;
    if (xp_drive_fp(unit)) {
        ds |= XPDS_MOL | XPDS_RDY;
    }
    if (xpds[unit] & XPDS_VV) {
        ds |= XPDS_VV;
    }
    if (xpds[unit] & XPDS_LST) {
        ds |= XPDS_LST;
    }
    if (xpds[unit] & XPDS_ATA) {
        ds |= XPDS_ATA;
    }
    if (xper1[unit] || xper2[unit] || xper3[unit]) {
        ds |= XPDS_ERR;
    }
    xpds[unit] = ds;
}

static void xp_update_all_drive_status(void)
{
    uint16_t u;
    for (u = 0; u < XP_MAX_DRIVES; u++) {
        xp_update_drive_status(u);
    }
}

static uint16_t xp_attention_summary(void)
{
    uint16_t as = 0;
    uint16_t u;
    for (u = 0; u < XP_MAX_DRIVES; u++) {
        if (xpds[u] & XPDS_ATA) {
            as |= (uint16_t)(1u << u);
        }
    }
    return as;
}

static void xp_sync_status(void)
{
    uint16_t unit = xp_selected_unit();
    uint16_t err = 0;

    xpcs2 |= (XPCS2_IR | XPCS2_OR);
    xp_update_all_drive_status();

    xpcs1 &= (uint16_t)~(XPCS1_DONE | XPCS1_IE | XPCS1_TRE | XPCS1_SC | XPCS1_DVA);
    if (xp_l.done) {
        xpcs1 |= XPCS1_DONE;
    }
    if (xp_l.ie) {
        xpcs1 |= XPCS1_IE;
    }
    xpcs1 |= XPCS1_DVA;

    if (xpcs2 & XPCS2_ERR_MASK) {
        err = 1;
    } else if (xp_drive_valid(unit) &&
               (xper1[unit] || xper2[unit] || xper3[unit])) {
        err = 1;
    }
    if (err) {
        xpcs1 |= XPCS1_TRE | XPCS1_SC;
    } else if (xp_drive_valid(unit) && (xpds[unit] & XPDS_ATA)) {
        xpcs1 |= XPCS1_SC;
    }

    xpcs3 &= (uint16_t)~XPCS1_IE;
    xpcs3 |= (xpcs1 & XPCS1_IE);
    xp_cs1_uae_sync_from_bae();
}

static void xp_set_cs2_error(uint16_t bit)
{
    xpcs2 |= bit;
    xp_sync_status();
}

static void xp_set_er1(uint16_t unit, uint16_t bit)
{
    if (!xp_drive_valid(unit)) {
        return;
    }
    xper1[unit] |= bit;
    xpds[unit] |= XPDS_ATA;
    xp_sync_status();
}

static void xp_finish_command(void)
{
    xpcs1 &= (uint16_t)~XPCS1_GO;
    irq_latch_event_set_done(&xp_l);
    xp_sync_status();
}

static paddr_t xp_dma_pa(uint16_t ba, uint8_t bae)
{
    uint32_t pa = (((uint32_t)(bae & XP_BAE_IMP)) << 16) | (uint32_t)ba;
    return (paddr_t)(pa & 017777777u);
}

static int xp_dma_read_word(uint16_t ba, uint8_t bae, uint16_t *w)
{
    paddr_t pa = xp_dma_pa(ba, bae);
    if (!bus_range_is_ram(pa, 2)) {
        return -1;
    }
    if (bus_is_nxm(pa) || bus_is_nxm((paddr_t)(pa + 1))) {
        return -1;
    }
    *w = bus_read16(pa);
    return 0;
}

static int xp_dma_write_word(uint16_t ba, uint8_t bae, uint16_t w)
{
    paddr_t pa = xp_dma_pa(ba, bae);
    if (!bus_range_is_ram(pa, 2)) {
        return -1;
    }
    if (bus_is_nxm(pa) || bus_is_nxm((paddr_t)(pa + 1))) {
        return -1;
    }
    bus_write16(pa, w);
    return 0;
}

static int xp_read_sector(emu_file_t *f, uint32_t lba, uint8_t *buf)
{
    uint32_t off = lba * XP_BYTES_PER_SECTOR;
    size_t got;

    if (!f) {
        return -1;
    }
    if (emu_fseek(f, (long)off, EMU_SEEK_SET) != 0) {
        return -1;
    }
    got = emu_fread(buf, 1, XP_BYTES_PER_SECTOR, f);
    if (got == XP_BYTES_PER_SECTOR) {
        return 0;
    }
    if (got < XP_BYTES_PER_SECTOR && emu_feof(f)) {
        memset(buf + got, 0, XP_BYTES_PER_SECTOR - got);
        emu_clearerr(f);
        return 0;
    }
    return -1;
}

static int xp_write_sector(emu_file_t *f, uint32_t lba, const uint8_t *buf)
{
    uint32_t off = lba * XP_BYTES_PER_SECTOR;
    if (!f) {
        return -1;
    }
    if (emu_fseek(f, (long)off, EMU_SEEK_SET) != 0) {
        return -1;
    }
    return (emu_fwrite(buf, 1, XP_BYTES_PER_SECTOR, f) == XP_BYTES_PER_SECTOR)
           ? 0
           : -1;
}

static int xp_da_dc_to_sector(uint16_t dc, uint16_t da, uint32_t *sector_out)
{
    uint32_t cyl = (uint32_t)(dc & XPDC_CYL_MASK);
    uint32_t surf = (uint32_t)((da & XPDA_SF_MASK) >> XPDA_SF_SHIFT);
    uint32_t sec = (uint32_t)(da & XPDA_SEC_MASK);

    if (cyl >= XP_CYLINDERS || surf >= XP_SURFACES || sec >= XP_SECTORS_PER_SURF) {
        return -1;
    }

    *sector_out = ((cyl * XP_SURFACES) + surf) * XP_SECTORS_PER_SURF + sec;
    return 0;
}

static void xp_sector_to_da_dc(uint32_t sector, uint16_t *dc_out, uint16_t *da_out)
{
    uint32_t cyl;
    uint32_t surf;
    uint32_t sec;
    uint32_t rem;

    if (sector >= XP_TOTAL_SECTORS) {
        sector = XP_TOTAL_SECTORS - 1;
    }

    cyl = sector / (XP_SURFACES * XP_SECTORS_PER_SURF);
    rem = sector % (XP_SURFACES * XP_SECTORS_PER_SURF);
    surf = rem / XP_SECTORS_PER_SURF;
    sec = rem % XP_SECTORS_PER_SURF;

    *dc_out = (uint16_t)(cyl & XPDC_CYL_MASK);
    *da_out = (uint16_t)(((surf & XPDA_SEC_MASK) << XPDA_SF_SHIFT) |
                         (sec & XPDA_SEC_MASK));
}

static uint16_t xp_func_get(void)
{
    return (uint16_t)((xpcs1 & XPCS1_FNC_MASK) >> 1);
}

static void xp_transfer(uint16_t fnc)
{
    uint16_t unit = xp_selected_unit();
    emu_file_t *fp = xp_drive_fp(unit);
    uint32_t start_sector = 0;
    uint32_t start_word = 0;
    uint32_t words_req;
    uint32_t words = 0;
    uint32_t i;
    uint16_t cur_wc = xpwc;
    uint16_t cur_ba = xpba;
    uint8_t cur_bae = (uint8_t)(xpbae & XP_BAE_IMP);
    uint8_t secbuf[XP_BYTES_PER_SECTOR];
    uint32_t sec_lba = 0;
    int sec_valid = 0;
    int sec_dirty = 0;
    int had_error = 0;

    if (!xp_drive_valid(unit)) {
        xp_set_cs2_error(XPCS2_NED);
        xp_finish_command();
        return;
    }
    if (!fp) {
        xp_set_er1(unit, XPER1_UNS);
        xp_finish_command();
        return;
    }
    if (xp_da_dc_to_sector(xpdc[unit], xpda[unit], &start_sector) != 0) {
        xp_set_er1(unit, XPER1_IAE);
        xp_finish_command();
        return;
    }

    words_req = (uint32_t)((0200000u - (uint32_t)cur_wc) & 0177777u);
    if (words_req == 0) {
        xp_finish_command();
        return;
    }

    start_word = start_sector * XP_WORDS_PER_SECTOR;
    if (start_word >= XP_TOTAL_WORDS) {
        xp_set_er1(unit, XPER1_AOE);
        xp_finish_command();
        return;
    }

    words = words_req;
    if (start_word + words > XP_TOTAL_WORDS) {
        words = XP_TOTAL_WORDS - start_word;
        xp_set_er1(unit, XPER1_AOE);
    }

    if ((fnc == XPF_WRITE || fnc == XPF_WRITEH) && xp_drive_read_only(unit)) {
        xp_set_er1(unit, XPER1_WLE);
        xp_finish_command();
        return;
    }

    for (i = 0; i < words; i++) {
        uint32_t sec_idx = start_sector + (i / XP_WORDS_PER_SECTOR);
        uint32_t sec_off = (i % XP_WORDS_PER_SECTOR) * 2u;
        uint16_t w = 0;

        if (!sec_valid || sec_lba != sec_idx) {
            if (sec_dirty) {
                if (xp_write_sector(fp, sec_lba, secbuf) != 0) {
                    xp_set_cs2_error(XPCS2_DLT);
                    had_error = 1;
                    break;
                }
                sec_dirty = 0;
            }
            if (xp_read_sector(fp, sec_idx, secbuf) != 0) {
                xp_set_cs2_error(XPCS2_DLT);
                had_error = 1;
                break;
            }
            sec_lba = sec_idx;
            sec_valid = 1;
        }

        w = (uint16_t)(secbuf[sec_off] | ((uint16_t)secbuf[sec_off + 1] << 8));

        if (fnc == XPF_READ || fnc == XPF_READH) {
            if (xp_dma_write_word(cur_ba, cur_bae, w) != 0) {
                xp_set_cs2_error(XPCS2_NEM);
                had_error = 1;
                break;
            }
        } else if (fnc == XPF_WRITE || fnc == XPF_WRITEH) {
            if (xp_dma_read_word(cur_ba, cur_bae, &w) != 0) {
                xp_set_cs2_error(XPCS2_NEM);
                had_error = 1;
                break;
            }
            secbuf[sec_off] = (uint8_t)(w & 000377);
            secbuf[sec_off + 1] = (uint8_t)((w >> 8) & 000377);
            sec_dirty = 1;
        } else { /* XPF_WCHK */
            uint16_t memw = 0;
            if (xp_dma_read_word(cur_ba, cur_bae, &memw) != 0) {
                xp_set_cs2_error(XPCS2_NEM);
                had_error = 1;
                break;
            }
            if (memw != w) {
                xp_set_cs2_error(XPCS2_WCE);
                xpcs3 |= XPCS3_WCE;
                had_error = 1;
                break;
            }
        }

        cur_wc = (uint16_t)(cur_wc + 1);
        if (!(xpcs2 & XPCS2_UAI)) {
            uint32_t ba22 =
                ((((uint32_t)cur_bae) << 16) | (uint32_t)cur_ba) + 2u;
            ba22 &= 017777777u;
            cur_ba = (uint16_t)(ba22 & 0177776u);
            cur_bae = (uint8_t)((ba22 >> 16) & XP_BAE_IMP);
        }
    }

    if (sec_dirty) {
        if (xp_write_sector(fp, sec_lba, secbuf) != 0) {
            xp_set_cs2_error(XPCS2_DLT);
            had_error = 1;
        }
    }
    if ((fnc == XPF_WRITE || fnc == XPF_WRITEH) && fp) {
        emu_fflush(fp);
    }

    xpwc = cur_wc;
    xpba = cur_ba;
    xpbae = (uint16_t)(cur_bae & XP_BAE_IMP);

    if (i > 0) {
        uint32_t adv = (i + (XP_WORDS_PER_SECTOR - 1u)) / XP_WORDS_PER_SECTOR;
        uint32_t new_sector = start_sector + adv;
        if (new_sector >= XP_TOTAL_SECTORS) {
            new_sector = XP_TOTAL_SECTORS - 1u;
            xpds[unit] |= XPDS_LST;
        } else {
            xpds[unit] &= (uint16_t)~XPDS_LST;
        }
        xp_sector_to_da_dc(new_sector, &xpdc[unit], &xpda[unit]);
        xpcc[unit] = xpdc[unit];
    }

    (void)had_error;
    xp_finish_command();
}

static void xp_controller_clear(void)
{
    uint16_t unit = xp_selected_unit();
    uint16_t u;

    xpcs1 &= (uint16_t)~(XPCS1_GO | XPCS1_FNC_MASK | XPCS1_UAE);
    xpwc = 0;
    xpba = 0;
    xpcs2 = (uint16_t)(unit & XPCS2_UNIT_MASK);
    xpdb = 0;
    xpbae = 0;
    xpcs3 = 0;
    for (u = 0; u < XP_MAX_DRIVES; u++) {
        xper1[u] = 0;
        xper2[u] = 0;
        xper3[u] = 0;
        xpec1[u] = 0;
        xpec2[u] = 0;
        xpmr[u] = 0;
    }

    irq_latch_reset(&xp_l);
    irq_latch_event_set_done(&xp_l);
    xp_sync_status();
}

static void xp_start_command(void)
{
    uint16_t unit = xp_selected_unit();
    uint16_t fnc = xp_func_get();

    if (!xp_drive_valid(unit)) {
        xp_set_cs2_error(XPCS2_NED);
        xp_finish_command();
        return;
    }

    switch (fnc) {
    case XPF_DCLR:
        xper1[unit] = xper2[unit] = xper3[unit] = 0;
        xpec1[unit] = xpec2[unit] = 0;
        xpds[unit] &= (uint16_t)~(XPDS_ATA | XPDS_LST);
        xp_finish_command();
        return;
    case XPF_NOP:
    case XPF_RELEASE:
        xp_finish_command();
        return;
    case XPF_PRESET:
        xpdc[unit] = 0;
        xpda[unit] = 0;
        xpcc[unit] = 0;
        xpof[unit] = 0;
        xpds[unit] |= XPDS_VV;
        xp_finish_command();
        return;
    case XPF_PACK:
        xpds[unit] |= XPDS_VV;
        xp_finish_command();
        return;
    case XPF_RECAL:
        xpdc[unit] = 0;
        xpcc[unit] = 0;
        xpds[unit] |= XPDS_ATA;
        xp_finish_command();
        return;
    case XPF_SEEK:
    case XPF_SEARCH:
        if ((xpdc[unit] & XPDC_CYL_MASK) >= XP_CYLINDERS) {
            xp_set_er1(unit, XPER1_IAE);
        }
        xpcc[unit] = (uint16_t)(xpdc[unit] & XPDC_CYL_MASK);
        xpds[unit] |= XPDS_ATA;
        xp_finish_command();
        return;
    case XPF_READ:
    case XPF_READH:
    case XPF_WRITE:
    case XPF_WRITEH:
    case XPF_WCHK:
        xp_transfer(fnc);
        return;
    default:
        xp_set_er1(unit, XPER1_ILF);
        xp_finish_command();
        return;
    }
}

static uint16_t xp_read16(uint16_t addr)
{
    uint16_t base = (uint16_t)(addr & 0177776);
    uint16_t unit = xp_selected_unit();

    xp_sync_status();

    switch (base) {
    case XPCS1:
        return xpcs1;
    case XPWC:
        return xpwc;
    case XPBA:
        return (uint16_t)(xpba & 0177776);
    case XPDA:
        return xp_drive_valid(unit) ? (uint16_t)(xpda[unit] & (XPDA_SEC_MASK | XPDA_SF_MASK)) : 0;
    case XPCS2:
        return (uint16_t)((xpcs2 & ~XPCS2_CLR) | XPCS2_IR | XPCS2_OR);
    case XPDS:
        return xp_drive_valid(unit) ? xpds[unit] : 0;
    case XPER1:
        return xp_drive_valid(unit) ? xper1[unit] : 0;
    case XPAS:
        return xp_attention_summary();
    case XPLA:
        if (!xp_drive_valid(unit)) {
            return 0;
        }
        return (uint16_t)(((xpda[unit] & XPDA_SEC_MASK) & 077) << 6);
    case XPDB:
        return xpdb;
    case XPMR:
        return xp_drive_valid(unit) ? xpmr[unit] : 0;
    case XPDT:
        return XP_DT_RM05;
    case XPSN:
        return (uint16_t)(020 + (unit + 1));
    case XPOF:
        return xp_drive_valid(unit) ? (uint16_t)(xpof[unit] & ~XPOF_MBZ) : 0;
    case XPDC:
        return xp_drive_valid(unit) ? (uint16_t)(xpdc[unit] & XPDC_CYL_MASK) : 0;
    case XPCC:
        return xp_drive_valid(unit) ? (uint16_t)(xpcc[unit] & XPDC_CYL_MASK) : 0;
    case XPER2:
        return xp_drive_valid(unit) ? xper2[unit] : 0;
    case XPER3:
        return xp_drive_valid(unit) ? xper3[unit] : 0;
    case XPEC1:
        return xp_drive_valid(unit) ? xpec1[unit] : 0;
    case XPEC2:
        return xp_drive_valid(unit) ? xpec2[unit] : 0;
    case XPBAE:
        return (uint16_t)(xpbae & XP_BAE_IMP);
    case XPCS3:
        xpcs3 &= (uint16_t)~XPCS1_IE;
        xpcs3 |= (xpcs1 & XPCS1_IE);
        return xpcs3;
    default:
        return 0;
    }
}

static uint8_t xp_read8(uint16_t addr)
{
    uint16_t v = xp_read16((uint16_t)(addr & 0177776));
    if (addr & 1) {
        return (uint8_t)((v >> 8) & 000377);
    }
    return (uint8_t)(v & 000377);
}

static void xp_write16(uint16_t addr, uint16_t v)
{
    uint16_t base = (uint16_t)(addr & 0177776);
    uint16_t unit = xp_selected_unit();

    switch (base) {
    case XPCS1: {
        int old_go = (xpcs1 & XPCS1_GO) ? 1 : 0;
        int old_ie = (xpcs1 & XPCS1_IE) ? 1 : 0;
        uint16_t low = (uint16_t)(v & 0000377);
        uint16_t high = (uint16_t)(v & 0177400);
        uint16_t fnc = (uint16_t)((low & XPCS1_FNC_MASK) >> 1);
        int is_xfer = (fnc >= XPF_XFER) ? 1 : 0;

        if (high & XPCS1_TRE) {
            xpcs2 &= (uint16_t)~XPCS2_ERR_MASK;
            xpcs3 &= (uint16_t)~XPCS3_ERR_MASK;
        }

        if (xpcs1 & XPCS1_DONE) {
            xp_bae_sync_from_cs1_uae(high & XPCS1_UAE);
        }

        xpcs1 &= (uint16_t)~(XPCS1_FNC_MASK | XPCS1_IE);
        xpcs1 |= (uint16_t)(low & (XPCS1_FNC_MASK | XPCS1_IE));
        irq_latch_set_ie(&xp_l, (xpcs1 & XPCS1_IE) ? 1 : 0);
        if (!old_ie && xp_l.ie && (xpcs1 & XPCS1_DONE)) {
            xp_l.irq_req = 1;
        }

        if ((low & XPCS1_GO) && old_go && !xp_l.done) {
            xp_set_cs2_error(XPCS2_PGE);
            xp_finish_command();
            return;
        }

        if ((low & XPCS1_GO) && !old_go) {
            if (is_xfer && !xp_l.done) {
                xp_set_cs2_error(XPCS2_PGE);
                xp_sync_status();
                return;
            }
            irq_latch_sw_clear_done(&xp_l);
            xpcs1 |= XPCS1_GO;
            if (is_xfer) {
                xpcs2 &= (uint16_t)~XPCS2_ERR_MASK;
                xpcs3 &= (uint16_t)~XPCS3_ERR_MASK;
            }
        }

        xp_sync_status();
        return;
    }
    case XPWC:
        xpwc = v;
        return;
    case XPBA:
        xpba = (uint16_t)(v & 0177776);
        return;
    case XPDA:
        if (xp_drive_valid(unit)) {
            xpda[unit] = (uint16_t)(v & (XPDA_SEC_MASK | XPDA_SF_MASK));
        }
        return;
    case XPCS2:
        if (v & XPCS2_CLR) {
            xp_controller_clear();
            return;
        }
        xpcs2 &= (uint16_t)~XPCS2_RW_MASK;
        xpcs2 |= (uint16_t)(v & XPCS2_RW_MASK);
        xpcs2 |= (XPCS2_IR | XPCS2_OR);
        xp_sync_status();
        return;
    case XPAS: {
        uint16_t mask = v;
        uint16_t u;
        for (u = 0; u < XP_MAX_DRIVES; u++) {
            if (mask & (uint16_t)(1u << u)) {
                xpds[u] &= (uint16_t)~XPDS_ATA;
            }
        }
        xp_sync_status();
        return;
    }
    case XPDB:
        xpdb = v;
        return;
    case XPMR:
        if (xp_drive_valid(unit)) {
            xpmr[unit] = v;
        }
        return;
    case XPOF:
        if (xp_drive_valid(unit)) {
            xpof[unit] = (uint16_t)(v & ~XPOF_MBZ);
        }
        return;
    case XPDC:
        if (xp_drive_valid(unit)) {
            xpdc[unit] = (uint16_t)(v & XPDC_CYL_MASK);
        }
        return;
    case XPBAE:
        xpbae = (uint16_t)(v & XP_BAE_IMP);
        xp_cs1_uae_sync_from_bae();
        xp_sync_status();
        return;
    case XPCS3:
        xpcs3 &= (uint16_t)~XPCS3_RW_MASK;
        xpcs3 |= (uint16_t)(v & XPCS3_RW_MASK);
        irq_latch_set_ie(&xp_l, (xpcs3 & XPCS1_IE) ? 1 : 0);
        xp_sync_status();
        return;
    default:
        return;
    }
}

static void xp_write8(uint16_t addr, uint8_t b)
{
    uint16_t base = (uint16_t)(addr & 0177776);
    uint16_t old;
    uint16_t v;

    if (base == XPDS || base == XPER1 || base == XPLA || base == XPDT ||
            base == XPSN || base == XPCC || base == XPER2 || base == XPER3 ||
            base == XPEC1 || base == XPEC2) {
        return;
    }

    /* CS1 is byte-addressable in practice; avoid re-interpreting GO on high byte. */
    if (base == XPCS1) {
        if (addr & 1) {
            uint16_t high = (uint16_t)((uint16_t)b << 8);
            if (high & XPCS1_TRE) {
                xpcs2 &= (uint16_t)~XPCS2_ERR_MASK;
                xpcs3 &= (uint16_t)~XPCS3_ERR_MASK;
            }
            if (xpcs1 & XPCS1_DONE) {
                xp_bae_sync_from_cs1_uae((uint16_t)(high & XPCS1_UAE));
            }
            xp_sync_status();
            return;
        }
        v = (uint16_t)((xpcs1 & 0177400) | (uint16_t)b);
        xp_write16(XPCS1, v);
        return;
    }

    old = xp_read16(base);
    if (addr & 1) {
        v = (uint16_t)((old & 0000377) | ((uint16_t)b << 8));
    } else {
        v = (uint16_t)((old & 0177400) | (uint16_t)b);
    }

    if ((xpcs1 & XPCS1_GO) && base != XPCS1) {
        xp_set_cs2_error(XPCS2_PGE);
        return;
    }

    if (base == XPBAE || base == XPCS3) {
        if (addr & 1) {
            return;
        }
    }

    xp_write16(base, v);
}

int xp_init(void)
{
    static const io_range_t io = {XP_BASE, (uint16_t)(XPCS3 + 1), xp_read8,
                                  xp_write8, "XP"
                                 };
    static const irq_source_t irq = {"XP", 000254, 5, xp_irq_pending, xp_irq_ack};

    if (devio_register(&io) != 0) {
        return -1;
    }
    if (irq_register(&irq) != 0) {
        return -1;
    }

    xp_reset();
    return 0;
}

void xp_reset(void)
{
    uint16_t u;

    xpcs1 = 0;
    xpwc = 0;
    xpba = 0;
    xpcs2 = 0;
    xpdb = 0;
    xpbae = 0;
    xpcs3 = 0;

    for (u = 0; u < XP_MAX_DRIVES; u++) {
        xpda[u] = 0;
        xpmr[u] = 0;
        xpof[u] = 0;
        xpdc[u] = 0;
        xpcc[u] = 0;
        xper1[u] = 0;
        xper2[u] = 0;
        xper3[u] = 0;
        xpec1[u] = 0;
        xpec2[u] = 0;
        xpds[u] = XPDS_DPR;
    }

    irq_latch_reset(&xp_l);
    irq_latch_event_set_done(&xp_l);
    xp_sync_status();
}

void xp_poll(void)
{
    if (!(xpcs1 & XPCS1_GO)) {
        return;
    }
    xp_start_command();
}

int xp_open_image_unit(unsigned unit, const char *path)
{
    uint16_t u = (uint16_t)unit;

    if (!path || !xp_drive_valid(u)) {
        return -1;
    }

    if (xp_fp[u]) {
        emu_fclose(xp_fp[u]);
        xp_fp[u] = NULL;
        xp_img_read_only[u] = 0;
    }

    xp_fp[u] = emu_fopen(path, "r+b");
    if (!xp_fp[u]) {
        xp_fp[u] = emu_fopen(path, "rb");
        if (!xp_fp[u]) {
            return -1;
        }
        xp_img_read_only[u] = 1;
    }

    xpds[u] &= (uint16_t)~XPDS_ATA;
    xpds[u] |= XPDS_VV;
    xp_update_drive_status(u);
    xp_sync_status();
    return 0;
}

int xp_open_image(const char *path)
{
    return xp_open_image_unit(0, path);
}

void xp_close_image(void)
{
    uint16_t u;
    for (u = 0; u < XP_MAX_DRIVES; u++) {
        if (xp_fp[u]) {
            emu_fclose(xp_fp[u]);
            xp_fp[u] = NULL;
            xp_img_read_only[u] = 0;
        }
    }
    xp_sync_status();
}

int xp_irq_pending(void)
{
    return xp_l.irq_req ? 1 : 0;
}

void xp_irq_ack(void)
{
    irq_latch_ack(&xp_l);
}
