#include "dev_tq11.h"

#include "bus.h"
#include "devio.h"
#include "emu_file.h"
#include "irq.h"
#include "ubmap.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* TQ11/TMSCP controller registers (octal). */
#define TQ_BASE            0174500u
#define TQ_IP              0174500u
#define TQ_SA              0174502u
#define TQ_VECTOR          0000260u
#define TQ_PRIORITY        5u

/* UQSSP/TMSCP port bits. */
#define TQ_SA_ER           0x8000u
#define TQ_SA_S4           0x4000u
#define TQ_SA_S3           0x2000u
#define TQ_SA_S2           0x1000u
#define TQ_SA_S1           0x0800u

#define TQ_S1C_NV          0x0400u
#define TQ_S1C_Q22         0x0200u
#define TQ_S1C_DI          0x0100u
#define TQ_S1C_MP          0x0040u
#define TQ_S1C_CAPS_QBUS   (TQ_SA_S1 | TQ_S1C_Q22 | TQ_S1C_DI | TQ_S1C_MP)
#define TQ_S1C_CAPS_UBUS   (TQ_SA_S1 | TQ_S1C_DI | TQ_S1C_MP)

#define TQ_S1H_VL          0x8000u
#define TQ_S1H_WR          0x4000u
#define TQ_S1H_V_CQ        11u
#define TQ_S1H_M_CQ        0x0007u
#define TQ_S1H_V_RQ        8u
#define TQ_S1H_M_RQ        0x0007u
#define TQ_S1H_IE          0x0080u

#define TQ_S2H_CLO         0xFFFEu
#define TQ_S2H_PI          0x0001u

#define TQ_S3H_PP          0x8000u
#define TQ_S3H_CHI         0x7FFFu

#define TQ_S4H_GO          0x0001u

#define TQ_SA_COMM_CI      (-4)
#define TQ_SA_COMM_RI      (-2)

#define TQ_DESC_OWN        0x80000000u
#define TQ_DESC_F          0x40000000u
#define TQ_DESC_ADDR       0x003FFFFEu

#define TQ_HDR_OFF         (-4)
#define TQ_HLNT            0u
#define TQ_HCTC            1u
#define TQ_HCTC_SEQ_TMSCP  0x0100u /* 0 credits, TMSCP CID, sequential type */

/* Controller state. */
#define TQ_STATE_S1        0u
#define TQ_STATE_S1_WRAP   1u
#define TQ_STATE_S2        2u
#define TQ_STATE_S3        3u
#define TQ_STATE_S3_PPA    4u
#define TQ_STATE_S3_PPB    5u
#define TQ_STATE_S4        6u
#define TQ_STATE_UP        7u
#define TQ_STATE_DEAD      8u

/* TMSCP packet fields. */
#define TQ_CMD_REFL        2u
#define TQ_CMD_REFH        3u
#define TQ_CMD_UN          4u
#define TQ_CMD_OPC         6u
#define TQ_CMD_MOD         7u

#define TQ_RSP_LNT         12u
#define TQ_RSP_REFL        2u
#define TQ_RSP_REFH        3u
#define TQ_RSP_UN          4u
#define TQ_RSP_OPF         6u
#define TQ_RSP_STS         7u

#define TQ_SCC_LNT         32u
#define TQ_SCC_MSV         8u
#define TQ_SCC_CFL         9u
#define TQ_SCC_TMO         10u
#define TQ_SCC_VER         11u
#define TQ_SCC_CIDD        15u
#define TQ_SCC_MBCL        16u
#define TQ_SCC_MBCH        17u

#define TQ_GUS_LNT         44u
#define TQ_GUS_UFL         9u
#define TQ_GUS_UIDD        15u
#define TQ_GUS_MEDL        16u
#define TQ_GUS_MEDH        17u
#define TQ_GUS_FMT         18u
#define TQ_GUS_SPEED       19u
#define TQ_GUS_MENU        20u
#define TQ_GUS_CAP         21u
#define TQ_GUS_FVER        22u
#define TQ_GUS_UVER        23u

#define TQ_ONL_LNT         44u
#define TQ_ONL_UFL         9u
#define TQ_ONL_UIDD        15u
#define TQ_ONL_MEDL        16u
#define TQ_ONL_MEDH        17u
#define TQ_ONL_FMT         18u
#define TQ_ONL_SPEED       19u
#define TQ_ONL_MAXL        20u
#define TQ_ONL_MAXH        21u
#define TQ_ONL_NREC        22u

#define TQ_FLU_LNT         32u
#define TQ_FLU_POSL        16u
#define TQ_FLU_POSH        17u

#define TQ_GCS_LNT         20u
#define TQ_GCS_REFL        8u
#define TQ_GCS_REFH        9u
#define TQ_GCS_STSL        10u
#define TQ_GCS_STSH        11u

#define TQ_SUC_LNT         44u

#define TQ_POS_LNT         32u
#define TQ_POS_RCL         8u
#define TQ_POS_RCH         9u
#define TQ_POS_TMCL        10u
#define TQ_POS_TMCH        11u
#define TQ_POS_POSL        16u
#define TQ_POS_POSH        17u

#define TQ_RW_LNT          36u
#define TQ_RW_BCL          8u
#define TQ_RW_BCH          9u
#define TQ_RW_BAL          10u
#define TQ_RW_BAH          11u
#define TQ_RW_POSL         16u
#define TQ_RW_POSH         17u
#define TQ_RW_RSZL         18u
#define TQ_RW_RSZH         19u

#define TQ_WTM_LNT         32u
#define TQ_WTM_POSL        16u
#define TQ_WTM_POSH        17u

/* Opcodes. */
#define TQ_OP_GCS          0002u
#define TQ_OP_GUS          0003u
#define TQ_OP_SCC          0004u
#define TQ_OP_AVL          0010u
#define TQ_OP_ONL          0011u
#define TQ_OP_SUC          0012u
#define TQ_OP_ACC          0020u
#define TQ_OP_FLU          0023u
#define TQ_OP_CMP          0040u
#define TQ_OP_RD           0041u
#define TQ_OP_WR           0042u
#define TQ_OP_WTM          0044u
#define TQ_OP_POS          0045u
#define TQ_OP_AVA          0100u
#define TQ_OP_END          0200u

/* Modifiers and flags. */
#define TQ_MD_CSE          0x2000u
#define TQ_MD_DLE          0x0080u
#define TQ_MD_IMM          0x0040u
#define TQ_MD_UNL          0x0010u
#define TQ_MD_REV          0x0008u
#define TQ_MD_SWP          0x0004u
#define TQ_MD_OBC          0x0004u
#define TQ_MD_RWD          0x0002u
#define TQ_MD_NXU          0x0001u

#define TQ_EF_SXC          0x0010u
#define TQ_EF_EOT          0x0008u

/* Status codes. */
#define TQ_ST_SUC          0u
#define TQ_ST_CMD          1u
#define TQ_ST_OFL          3u
#define TQ_ST_AVL          4u
#define TQ_ST_WPR          6u
#define TQ_ST_HST          9u
#define TQ_ST_BOT          13u
#define TQ_ST_TMK          14u
#define TQ_ST_RDT          16u
#define TQ_ST_POL          17u
#define TQ_ST_SXC          18u
#define TQ_ST_LED          19u

#define TQ_ST_V_SUB        5u
#define TQ_ST_V_INV        8u

#define TQ_SB_SUC_IGN      (1u << TQ_ST_V_SUB)
#define TQ_SB_SUC_ON       (8u << TQ_ST_V_SUB)
#define TQ_SB_SUC_EOT      (32u << TQ_ST_V_SUB)
#define TQ_SB_OFL_NV       (1u << TQ_ST_V_SUB)
#define TQ_SB_WPR_SW       (128u << TQ_ST_V_SUB)
#define TQ_SB_WPR_HW       (256u << TQ_ST_V_SUB)
#define TQ_SB_HST_NXM      (3u << TQ_ST_V_SUB)

#define TQ_I_OPCD          (8u << TQ_ST_V_INV)
#define TQ_I_BCNT          (12u << TQ_ST_V_INV)
#define TQ_I_VRSN          (12u << TQ_ST_V_INV)

/* Controller and unit identity. */
#define TQ_CF_ATN          0x0080u
#define TQ_CF_MSK          0x00F0u

#define TQ_UF_WPH          0x2000u
#define TQ_UF_WPS          0x1000u

#define TQ_UID_TAPE        3u
#define TQ_CTRL_CLASS      1u
#define TQ_CTRL_MODEL      9u  /* TQK50 */
#define TQ_UNIT_MODEL      3u  /* TK50 */
#define TQ_CTRL_REV        0405u
#define TQ_TAPE_FORMAT     0x0201u /* TK50 low-density cartridge tape */
#define TQ_MEDIA_ID        0x6D68B032u
#define TQ_MAX_TRANSFER    0x0000FFFEu
#define TQ_MAX_RECORD      0x0000FFFEu
#define TQ_NOISE_RECORD    0000016u
#define TQ_IO_CHUNK        256u

/* Minimal SIMH .tap backend markers. */
#define TQ_TAP_TMK         0x00000000u
#define TQ_TAP_EOM         0xFFFFFFFFu
#define TQ_TAP_MAXLEN      0x00FFFFFFu

#define TQ_PKT_WORDS       32u

typedef struct {
    uint32_t base;
    uint32_t len;
    uint32_t idx;
    int ioff;
} tq_ring_t;

typedef enum {
    TQ_TAP_OK = 0,
    TQ_TAP_MARK = 1,
    TQ_TAP_BOT = 2,
    TQ_TAP_EOM_STATUS = 3,
    TQ_TAP_IOERR = 4,
    TQ_TAP_INVRL = 5,
    TQ_TAP_WRP_STATUS = 6
} tq_tap_status_t;

static uint16_t tq_sa;
static uint16_t tq_sa_latch;
static uint16_t tq_s1_host;
static uint32_t tq_comm;
static uint8_t tq_prgi;
static uint8_t tq_state;
static uint8_t tq_irq_req;
static uint8_t tq_irq_en;
static uint8_t tq_poll_pending;
static uint16_t tq_cflgs;
static uint8_t tq_trace;
static tq_ring_t tq_cmd_ring;
static tq_ring_t tq_rsp_ring;
static uint16_t tq_cmd_unit;

typedef struct {
    emu_file_t *fp;
    uint8_t read_only;
    uint8_t online;
    uint8_t sw_write_protect;
    uint8_t una_pending;
    uint32_t tape_pos;
} tq_unit_t;

static tq_unit_t tq_units[TQ11_MAX_UNITS];

static tq_unit_t *tq_unit_ptr(uint16_t unit)
{
    if (unit >= TQ11_MAX_UNITS) {
        return NULL;
    }
    return &tq_units[unit];
}

static uint16_t tq_effective_unit(void)
{
    if (tq_cmd_unit < TQ11_MAX_UNITS) {
        return tq_cmd_unit;
    }
    return 0;
}

static tq_unit_t *tq_cur_unit(void)
{
    return tq_unit_ptr(tq_effective_unit());
}

#define TQ_UNIT    (tq_cur_unit())
#define tq_fp      (TQ_UNIT->fp)
#define tq_read_only (TQ_UNIT->read_only)
#define tq_online  (TQ_UNIT->online)
#define tq_sw_write_protect (TQ_UNIT->sw_write_protect)
#define tq_una_pending (TQ_UNIT->una_pending)
#define tq_tape_pos (TQ_UNIT->tape_pos)

static uint16_t tq_initial_sa(void)
{
    if (bus_machine() == BUS_MACHINE_PDP1184) {
        return TQ_S1C_CAPS_UBUS;
    }
    return TQ_S1C_CAPS_QBUS;
}

static void tq_log(const char *fmt, ...)
{
    va_list ap;

    if (!tq_trace) {
        return;
    }

    va_start(ap, fmt);
    fprintf(stderr, "TQ ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

static uint32_t tq_get_u32(const uint16_t *pkt, unsigned lo_idx)
{
    return (uint32_t)pkt[lo_idx] | ((uint32_t)pkt[lo_idx + 1u] << 16);
}

static void tq_put_u32(uint16_t *pkt, unsigned lo_idx, uint32_t v)
{
    pkt[lo_idx] = (uint16_t)(v & 0xFFFFu);
    pkt[lo_idx + 1u] = (uint16_t)((v >> 16) & 0xFFFFu);
}

static int tq_irq_pending(void)
{
    return tq_irq_req ? 1 : 0;
}

static void tq_irq_ack(void)
{
    tq_irq_req = 0;
    tq_log("irq ack");
}

static void tq_raise_irq(void)
{
    if (!tq_irq_en) {
        return;
    }
    if (tq_irq_req) {
        return;
    }
    tq_irq_req = 1;
    tq_log("irq req vec=%06o pri=%o", (unsigned)TQ_VECTOR, (unsigned)TQ_PRIORITY);
}

static bus_paddr_t tq_dma_map_addr(uint32_t bus_addr)
{
    /*
     * TQ11 is modelled as a UNIBUS DMA device on pdp1184:
     * DMA address is 18-bit, optionally translated by UBM (MMR3<040>).
     */
    if (bus_machine() == BUS_MACHINE_PDP1184) {
        return ubmap_map_addr(bus_addr & 000777777u);
    }
    return (bus_paddr_t)bus_addr;
}

static int tq_dma_read8_checked(bus_paddr_t addr, uint8_t *v)
{
    bus_paddr_t pa = tq_dma_map_addr((uint32_t)addr);

    if (!v) {
        return -1;
    }
    if (!bus_range_is_ram(pa, 1) || bus_is_nxm(pa)) {
        return -1;
    }
    *v = bus_read8(pa);
    return 0;
}

static int tq_dma_write8_checked(bus_paddr_t addr, uint8_t v)
{
    bus_paddr_t pa = tq_dma_map_addr((uint32_t)addr);

    if (!bus_range_is_ram(pa, 1) || bus_is_nxm(pa)) {
        return -1;
    }
    bus_write8(pa, v);
    return 0;
}

static int tq_dma_read16_checked(bus_paddr_t addr, uint16_t *v)
{
    uint8_t lo;
    uint8_t hi;

    if (!v) {
        return -1;
    }
    if (tq_dma_read8_checked(addr, &lo) != 0 ||
            tq_dma_read8_checked((bus_paddr_t)(addr + 1u), &hi) != 0) {
        return -1;
    }
    *v = (uint16_t)((uint16_t)lo | ((uint16_t)hi << 8));
    return 0;
}

static int tq_dma_write16_checked(bus_paddr_t addr, uint16_t v)
{
    if (tq_dma_write8_checked(addr, (uint8_t)(v & 000377u)) != 0 ||
            tq_dma_write8_checked((bus_paddr_t)(addr + 1u),
                                  (uint8_t)((v >> 8) & 000377u)) != 0) {
        return -1;
    }
    return 0;
}

static int tq_dma_read32_checked(bus_paddr_t addr, uint32_t *v)
{
    uint16_t lo;
    uint16_t hi;

    if (!v) {
        return -1;
    }
    if (tq_dma_read16_checked(addr, &lo) != 0 ||
            tq_dma_read16_checked(addr + 2u, &hi) != 0) {
        return -1;
    }
    *v = (uint32_t)lo | ((uint32_t)hi << 16);
    return 0;
}

static int tq_dma_write32_checked(bus_paddr_t addr, uint32_t v)
{
    if (tq_dma_write16_checked(addr, (uint16_t)(v & 0xFFFFu)) != 0 ||
            tq_dma_write16_checked(addr + 2u, (uint16_t)((v >> 16) & 0xFFFFu)) !=
            0) {
        return -1;
    }
    return 0;
}

static int tq_dma_read_words(bus_paddr_t addr, uint16_t *dst, size_t words)
{
    size_t i;

    if (!dst) {
        return -1;
    }
    for (i = 0; i < words; i++) {
        if (tq_dma_read16_checked(addr + (bus_paddr_t)(i * 2u), &dst[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

static int tq_dma_write_words(bus_paddr_t addr, const uint16_t *src, size_t words)
{
    size_t i;

    if (!src) {
        return -1;
    }
    for (i = 0; i < words; i++) {
        if (tq_dma_write16_checked(addr + (bus_paddr_t)(i * 2u), src[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

static int tq_dma_read_bytes(uint32_t addr, uint8_t *dst, uint32_t len)
{
    uint32_t i;

    if (!dst) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        if (tq_dma_read8_checked((bus_paddr_t)(addr + i), &dst[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

static int tq_dma_readable_range(uint32_t addr, uint32_t len)
{
    uint32_t i;

    for (i = 0; i < len; i++) {
        bus_paddr_t pa = tq_dma_map_addr(addr + i);
        if (!bus_range_is_ram(pa, 1) || bus_is_nxm(pa)) {
            return -1;
        }
    }
    return 0;
}

static int tq_dma_write_bytes(uint32_t addr, const uint8_t *src, uint32_t len)
{
    uint32_t i;

    if (!src) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        if (tq_dma_write8_checked((bus_paddr_t)(addr + i), src[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

static void tq_ring_reset(tq_ring_t *ring)
{
    if (!ring) {
        return;
    }
    ring->base = 0;
    ring->len = 0;
    ring->idx = 0;
    ring->ioff = 0;
}

static uint32_t tq_ring_slot_addr(const tq_ring_t *ring)
{
    return ring->base + ring->idx;
}

static void tq_ring_advance(tq_ring_t *ring)
{
    if (!ring || ring->len == 0) {
        return;
    }
    ring->idx += 4u;
    if (ring->idx >= ring->len) {
        ring->idx = 0;
    }
}

static int tq_ring_peek_owned(const tq_ring_t *ring, uint32_t *desc_out,
                              uint32_t *slot_out)
{
    uint32_t desc;
    uint32_t slot;

    if (!ring || ring->len == 0 || !desc_out || !slot_out) {
        return 0;
    }

    slot = tq_ring_slot_addr(ring);
    if (tq_dma_read32_checked((bus_paddr_t)slot, &desc) != 0) {
        return -1;
    }
    if ((desc & TQ_DESC_OWN) == 0) {
        return 0;
    }

    *desc_out = desc;
    *slot_out = slot;
    return 1;
}

static void tq_write_intr_flag(int off)
{
    int32_t sum;

    if (tq_comm == 0) {
        return;
    }

    sum = (int32_t)tq_comm + off;
    if (sum < 0) {
        return;
    }

    (void)tq_dma_write16_checked((bus_paddr_t)sum, 000001u);
}

static int tq_release_desc(tq_ring_t *ring, uint32_t slot_addr, uint32_t desc)
{
    uint32_t released = desc & ~TQ_DESC_OWN;

    if (tq_dma_write32_checked((bus_paddr_t)slot_addr, released) != 0) {
        return -1;
    }

    if (ring == &tq_cmd_ring) {
        tq_write_intr_flag(TQ_SA_COMM_CI);
    } else if (ring == &tq_rsp_ring) {
        tq_write_intr_flag(TQ_SA_COMM_RI);
    }

    tq_ring_advance(ring);
    return 0;
}

static void tq_fail(uint16_t code)
{
    tq_state = TQ_STATE_DEAD;
    tq_sa = (uint16_t)(TQ_SA_ER | (code & 0x07FFu));
    tq_log("fatal state=%u code=%06o", (unsigned)tq_state, (unsigned)tq_sa);
    tq_raise_irq();
}

static uint32_t tq_ring_desc_count(uint16_t s1, unsigned shift)
{
    uint32_t pow2 = (uint32_t)((s1 >> shift) & TQ_S1H_M_CQ);
    return 1u << pow2;
}

static int tq_clear_comm_area(void)
{
    int32_t start;
    uint32_t end;
    bus_paddr_t addr;

    if (tq_comm < 4u) {
        return -1;
    }

    start = (int32_t)tq_comm + TQ_SA_COMM_CI;
    end = tq_cmd_ring.base + tq_cmd_ring.len;
    if (start < 0 || (uint32_t)start > end) {
        return -1;
    }

    for (addr = (bus_paddr_t)start; addr < end; addr += 2u) {
        if (tq_dma_write16_checked(addr, 0) != 0) {
            return -1;
        }
    }

    return 0;
}

static int tq_step4(void)
{
    uint32_t rsp_descs;
    uint32_t cmd_descs;

    rsp_descs = tq_ring_desc_count(tq_s1_host, TQ_S1H_V_RQ);
    cmd_descs = tq_ring_desc_count(tq_s1_host, TQ_S1H_V_CQ);

    tq_rsp_ring.base = tq_comm;
    tq_rsp_ring.len = rsp_descs * 4u;
    tq_rsp_ring.idx = 0;
    tq_rsp_ring.ioff = TQ_SA_COMM_RI;

    tq_cmd_ring.base = tq_rsp_ring.base + tq_rsp_ring.len;
    tq_cmd_ring.len = cmd_descs * 4u;
    tq_cmd_ring.idx = 0;
    tq_cmd_ring.ioff = TQ_SA_COMM_CI;

    if (tq_clear_comm_area() != 0) {
        tq_fail(0000007u);
        return -1;
    }

    tq_sa = (uint16_t)(TQ_SA_S4 | ((uint16_t)3u << 4) | 1u);
    tq_state = TQ_STATE_S4;
    tq_log("step4 comm=%07o rq=%u cq=%u", (unsigned)tq_comm, (unsigned)rsp_descs,
           (unsigned)cmd_descs);
    tq_raise_irq();
    return 0;
}

static int tq_tap_seek(uint32_t pos)
{
    if (!tq_fp) {
        return -1;
    }
    if (emu_fseek(tq_fp, (long)pos, EMU_SEEK_SET) != 0) {
        return -1;
    }
    tq_tape_pos = pos;
    return 0;
}

static int tq_tap_read_u32(uint32_t *v)
{
    uint8_t b[4];

    if (!v || !tq_fp) {
        return -1;
    }
    if (emu_fread(b, 1, sizeof(b), tq_fp) != sizeof(b)) {
        return -1;
    }
    *v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
         ((uint32_t)b[3] << 24);
    tq_tape_pos += 4u;
    return 0;
}

static int tq_tap_write_u32(uint32_t v)
{
    uint8_t b[4];

    if (!tq_fp) {
        return -1;
    }
    b[0] = (uint8_t)(v & 0xFFu);
    b[1] = (uint8_t)((v >> 8) & 0xFFu);
    b[2] = (uint8_t)((v >> 16) & 0xFFu);
    b[3] = (uint8_t)((v >> 24) & 0xFFu);
    if (emu_fwrite(b, 1, sizeof(b), tq_fp) != sizeof(b)) {
        return -1;
    }
    tq_tape_pos += 4u;
    return 0;
}

static uint32_t tq_tap_padded(uint32_t len)
{
    return (len + 1u) & ~1u;
}

typedef int (*tq_tap_sink_fn)(void *opaque, uint32_t offset,
                              const uint8_t *data, uint32_t len);

static tq_tap_status_t tq_tap_read_forward_stream(uint32_t max_len,
        uint32_t *rec_len, uint32_t *xfer_len, tq_tap_sink_fn sink,
        void *opaque, int *sink_result)
{
    uint8_t chunk[TQ_IO_CHUNK];
    uint32_t mark;
    uint32_t data_len;
    uint32_t padded;
    uint32_t trailer;
    uint32_t offset;
    uint8_t pad;

    if (rec_len) {
        *rec_len = 0;
    }
    if (xfer_len) {
        *xfer_len = 0;
    }
    if (sink_result) {
        *sink_result = 0;
    }
    if (!tq_fp) {
        return TQ_TAP_EOM_STATUS;
    }
    if (tq_tap_read_u32(&mark) != 0) {
        return TQ_TAP_EOM_STATUS;
    }
    if (mark == TQ_TAP_TMK) {
        return TQ_TAP_MARK;
    }
    if (mark == TQ_TAP_EOM || mark > TQ_TAP_MAXLEN) {
        return TQ_TAP_INVRL;
    }

    data_len = mark;
    padded = tq_tap_padded(data_len);

    if (rec_len) {
        *rec_len = data_len;
    }

    for (offset = 0; offset < data_len;) {
        uint32_t chunk_len = data_len - offset;
        uint32_t deliver_len;

        if (chunk_len > TQ_IO_CHUNK) {
            chunk_len = TQ_IO_CHUNK;
        }
        if (emu_fread(chunk, 1, chunk_len, tq_fp) != chunk_len) {
            return TQ_TAP_IOERR;
        }
        tq_tape_pos += chunk_len;

        deliver_len = chunk_len;
        if (offset >= max_len) {
            deliver_len = 0;
        } else if (deliver_len > max_len - offset) {
            deliver_len = max_len - offset;
        }
        if (sink && deliver_len != 0u &&
                (!sink_result || *sink_result == 0)) {
            int rc = sink(opaque, offset, chunk, deliver_len);
            if (sink_result) {
                *sink_result = rc;
            }
        }
        offset += chunk_len;
    }

    if ((padded & 1u) != (data_len & 1u)) {
        if (emu_fread(&pad, 1, 1, tq_fp) != 1) {
            return TQ_TAP_IOERR;
        }
        tq_tape_pos += 1u;
    }

    if (tq_tap_read_u32(&trailer) != 0) {
        return TQ_TAP_IOERR;
    }
    if (trailer != mark) {
        return TQ_TAP_INVRL;
    }

    if (xfer_len) {
        *xfer_len = (data_len < max_len) ? data_len : max_len;
    }
    return TQ_TAP_OK;
}

typedef struct {
    uint8_t *buf;
} tq_buffer_sink_t;

static int tq_tap_buffer_sink(void *opaque, uint32_t offset,
                              const uint8_t *data, uint32_t len)
{
    tq_buffer_sink_t *sink = (tq_buffer_sink_t *)opaque;

    memcpy(sink->buf + offset, data, len);
    return 0;
}

static tq_tap_status_t tq_tap_read_forward(uint8_t *buf, uint32_t max_len,
        uint32_t *rec_len, uint32_t *xfer_len)
{
    tq_buffer_sink_t sink = {buf};

    return tq_tap_read_forward_stream(max_len, rec_len, xfer_len,
                                      buf ? tq_tap_buffer_sink : NULL,
                                      buf ? &sink : NULL, NULL);
}

static tq_tap_status_t tq_tap_peek_prev(uint32_t *mark, uint32_t *start,
                                        uint32_t *rec_len)
{
    uint32_t meta;
    uint32_t padded;
    uint32_t obj_start;

    if (mark) {
        *mark = 0;
    }
    if (start) {
        *start = tq_tape_pos;
    }
    if (rec_len) {
        *rec_len = 0;
    }

    if (!tq_fp) {
        return TQ_TAP_EOM_STATUS;
    }
    if (tq_tape_pos == 0u) {
        return TQ_TAP_BOT;
    }
    if (tq_tape_pos < 4u) {
        return TQ_TAP_BOT;
    }
    if (tq_tap_seek(tq_tape_pos - 4u) != 0) {
        return TQ_TAP_IOERR;
    }
    if (tq_tap_read_u32(&meta) != 0) {
        return TQ_TAP_IOERR;
    }
    if (meta == TQ_TAP_TMK) {
        obj_start = tq_tape_pos - 4u;
        if (mark) {
            *mark = meta;
        }
        if (start) {
            *start = obj_start;
        }
        if (tq_tap_seek(tq_tape_pos) != 0) {
            return TQ_TAP_IOERR;
        }
        return TQ_TAP_MARK;
    }
    if (meta == TQ_TAP_EOM || meta > TQ_TAP_MAXLEN) {
        return TQ_TAP_INVRL;
    }

    padded = tq_tap_padded(meta);
    if (tq_tape_pos < (8u + padded)) {
        return TQ_TAP_INVRL;
    }
    obj_start = tq_tape_pos - 4u - padded - 4u;
    if (mark) {
        *mark = meta;
    }
    if (start) {
        *start = obj_start;
    }
    if (rec_len) {
        *rec_len = meta;
    }
    if (tq_tap_seek(tq_tape_pos) != 0) {
        return TQ_TAP_IOERR;
    }
    return TQ_TAP_OK;
}

#if defined(LSI11_TESTS)
static tq_tap_status_t tq_tap_write_record(const uint8_t *buf, uint32_t len)
{
    static const uint8_t zero = 0;
    uint32_t padded;

    if (!tq_fp) {
        return TQ_TAP_EOM_STATUS;
    }
    if (tq_read_only) {
        return TQ_TAP_WRP_STATUS;
    }
    if (len > TQ_TAP_MAXLEN) {
        return TQ_TAP_INVRL;
    }

    padded = tq_tap_padded(len);

    if (tq_tap_write_u32(len) != 0) {
        return TQ_TAP_IOERR;
    }
    if (len != 0u && emu_fwrite(buf, 1, len, tq_fp) != len) {
        return TQ_TAP_IOERR;
    }
    tq_tape_pos += len;
    if (padded != len) {
        if (emu_fwrite(&zero, 1, 1, tq_fp) != 1) {
            return TQ_TAP_IOERR;
        }
        tq_tape_pos += 1u;
    }
    if (tq_tap_write_u32(len) != 0) {
        return TQ_TAP_IOERR;
    }
    if (emu_fflush(tq_fp) != 0) {
        return TQ_TAP_IOERR;
    }
    return TQ_TAP_OK;
}
#endif

static tq_tap_status_t tq_tap_write_record_dma(uint32_t addr, uint32_t len,
        int *dma_error)
{
    static const uint8_t zero = 0;
    uint8_t chunk[TQ_IO_CHUNK];
    uint32_t padded;
    uint32_t offset;

    if (dma_error) {
        *dma_error = 0;
    }
    if (!tq_fp) {
        return TQ_TAP_EOM_STATUS;
    }
    if (tq_read_only) {
        return TQ_TAP_WRP_STATUS;
    }
    if (len > TQ_TAP_MAXLEN) {
        return TQ_TAP_INVRL;
    }
    if (tq_dma_readable_range(addr, len) != 0) {
        if (dma_error) {
            *dma_error = 1;
        }
        return TQ_TAP_IOERR;
    }

    padded = tq_tap_padded(len);
    if (tq_tap_write_u32(len) != 0) {
        return TQ_TAP_IOERR;
    }

    for (offset = 0; offset < len;) {
        uint32_t chunk_len = len - offset;

        if (chunk_len > TQ_IO_CHUNK) {
            chunk_len = TQ_IO_CHUNK;
        }
        if (tq_dma_read_bytes(addr + offset, chunk, chunk_len) != 0) {
            if (dma_error) {
                *dma_error = 1;
            }
            return TQ_TAP_IOERR;
        }
        if (emu_fwrite(chunk, 1, chunk_len, tq_fp) != chunk_len) {
            return TQ_TAP_IOERR;
        }
        tq_tape_pos += chunk_len;
        offset += chunk_len;
    }

    if (padded != len) {
        if (emu_fwrite(&zero, 1, 1, tq_fp) != 1) {
            return TQ_TAP_IOERR;
        }
        tq_tape_pos += 1u;
    }
    if (tq_tap_write_u32(len) != 0) {
        return TQ_TAP_IOERR;
    }
    if (emu_fflush(tq_fp) != 0) {
        return TQ_TAP_IOERR;
    }
    return TQ_TAP_OK;
}

static tq_tap_status_t tq_tap_write_mark(void)
{
    if (!tq_fp) {
        return TQ_TAP_EOM_STATUS;
    }
    if (tq_read_only) {
        return TQ_TAP_WRP_STATUS;
    }
    if (tq_tap_write_u32(TQ_TAP_TMK) != 0) {
        return TQ_TAP_IOERR;
    }
    if (emu_fflush(tq_fp) != 0) {
        return TQ_TAP_IOERR;
    }
    return TQ_TAP_OK;
}

static tq_tap_status_t tq_tap_rewind(void)
{
    if (!tq_fp) {
        return TQ_TAP_EOM_STATUS;
    }
    if (tq_tap_seek(0) != 0) {
        return TQ_TAP_IOERR;
    }
    return TQ_TAP_OK;
}

static tq_tap_status_t tq_tap_space_forward_records(uint32_t count,
        uint32_t *skipped)
{
    uint32_t i;

    if (skipped) {
        *skipped = 0;
    }

    for (i = 0; i < count; i++) {
        uint32_t mark;
        uint32_t rec_len;
        tq_tap_status_t st;

        st = tq_tap_read_forward(NULL, 0, &rec_len, NULL);
        if (st == TQ_TAP_OK) {
            if (skipped) {
                *skipped += 1u;
            }
            continue;
        }
        if (st == TQ_TAP_MARK) {
            return TQ_TAP_MARK;
        }
        if (st == TQ_TAP_EOM_STATUS) {
            return TQ_TAP_EOM_STATUS;
        }
        if (st == TQ_TAP_INVRL) {
            return TQ_TAP_INVRL;
        }
        if (st == TQ_TAP_IOERR) {
            return TQ_TAP_IOERR;
        }
        (void)mark;
        return st;
    }

    return TQ_TAP_OK;
}

static tq_tap_status_t tq_tap_space_forward_marks(uint32_t count,
        uint32_t *skipped)
{
    uint32_t marks = 0;

    if (skipped) {
        *skipped = 0;
    }

    while (marks < count) {
        tq_tap_status_t st = tq_tap_read_forward(NULL, 0, NULL, NULL);
        if (st == TQ_TAP_OK) {
            continue;
        }
        if (st == TQ_TAP_MARK) {
            marks++;
            continue;
        }
        if (skipped) {
            *skipped = marks;
        }
        return st;
    }

    if (skipped) {
        *skipped = marks;
    }
    return TQ_TAP_OK;
}

static tq_tap_status_t tq_tap_space_reverse_records(uint32_t count,
        uint32_t *skipped)
{
    uint32_t done = 0;

    if (skipped) {
        *skipped = 0;
    }

    while (done < count) {
        uint32_t start = 0;
        tq_tap_status_t st = tq_tap_peek_prev(NULL, &start, NULL);
        if (st == TQ_TAP_OK) {
            if (tq_tap_seek(start) != 0) {
                return TQ_TAP_IOERR;
            }
            done++;
            continue;
        }
        if (st == TQ_TAP_MARK) {
            if (tq_tap_seek(start) != 0) {
                return TQ_TAP_IOERR;
            }
            if (skipped) {
                *skipped = done;
            }
            return TQ_TAP_MARK;
        }
        if (skipped) {
            *skipped = done;
        }
        return st;
    }

    if (skipped) {
        *skipped = done;
    }
    return TQ_TAP_OK;
}

static tq_tap_status_t tq_tap_space_reverse_marks(uint32_t count,
        uint32_t *skipped)
{
    uint32_t done = 0;

    if (skipped) {
        *skipped = 0;
    }

    while (done < count) {
        uint32_t start = 0;
        tq_tap_status_t st = tq_tap_peek_prev(NULL, &start, NULL);
        if (st == TQ_TAP_OK) {
            if (tq_tap_seek(start) != 0) {
                return TQ_TAP_IOERR;
            }
            continue;
        }
        if (st == TQ_TAP_MARK) {
            if (tq_tap_seek(start) != 0) {
                return TQ_TAP_IOERR;
            }
            done++;
            continue;
        }
        if (skipped) {
            *skipped = done;
        }
        return st;
    }

    if (skipped) {
        *skipped = done;
    }
    return TQ_TAP_OK;
}

static uint16_t tq_current_unit_flags(void)
{
    uint16_t flags = 0;

    if (tq_read_only) {
        flags |= TQ_UF_WPH;
    }
    if (tq_sw_write_protect) {
        flags |= TQ_UF_WPS;
    }
    return flags;
}

static uint16_t tq_motion_valid(uint32_t op)
{
    (void)op;

    if (!tq_fp) {
        return (uint16_t)(TQ_ST_OFL | TQ_SB_OFL_NV);
    }
    if (!tq_online) {
        return TQ_ST_AVL;
    }
    return TQ_ST_SUC;
}

static void tq_prepare_rsp(uint16_t *rsp, const uint16_t *cmd, uint16_t op,
                           uint16_t flags, uint16_t status, uint16_t len)
{
    memset(rsp, 0, TQ_PKT_WORDS * sizeof(uint16_t));
    rsp[TQ_HLNT] = len;
    rsp[TQ_HCTC] = TQ_HCTC_SEQ_TMSCP;
    rsp[TQ_RSP_REFL] = cmd[TQ_CMD_REFL];
    rsp[TQ_RSP_REFH] = cmd[TQ_CMD_REFH];
    rsp[TQ_RSP_UN] = cmd[TQ_CMD_UN];
    rsp[TQ_RSP_OPF] = (uint16_t)(op | TQ_OP_END | (uint16_t)(flags << 8));
    rsp[TQ_RSP_STS] = status;
}

static void tq_fill_online_status(uint16_t *rsp)
{
    rsp[TQ_ONL_UFL] = tq_current_unit_flags();
    rsp[TQ_ONL_UIDD] =
        (uint16_t)(((uint16_t)TQ_UID_TAPE << 8) | (uint16_t)TQ_UNIT_MODEL);
    tq_put_u32(rsp, TQ_ONL_MEDL, TQ_MEDIA_ID);
    rsp[TQ_ONL_FMT] = TQ_TAPE_FORMAT;
    rsp[TQ_ONL_SPEED] = 0;
    tq_put_u32(rsp, TQ_ONL_MAXL, TQ_MAX_RECORD);
    rsp[TQ_ONL_NREC] = TQ_NOISE_RECORD;
}

static uint16_t tq_response_flags_for_status(uint16_t status)
{
    uint16_t flags = 0;
    uint16_t code = (uint16_t)(status & 0000037u);

    if ((status & TQ_SB_SUC_EOT) != 0u) {
        flags |= TQ_EF_EOT;
    }
    if (code == TQ_ST_TMK || code == TQ_ST_RDT || code == TQ_ST_SXC ||
            code == TQ_ST_WPR) {
        flags |= TQ_EF_SXC;
    }

    return flags;
}

static void tq_rsp_or_flags(uint16_t *rsp, uint16_t flags)
{
    rsp[TQ_RSP_OPF] =
        (uint16_t)(rsp[TQ_RSP_OPF] | (uint16_t)(flags << 8));
}

static void tq_rsp_flags_from_status(uint16_t *rsp)
{
    tq_rsp_or_flags(rsp, tq_response_flags_for_status(rsp[TQ_RSP_STS]));
}

static void tq_schedule_una(uint16_t unit)
{
    tq_unit_t *u = tq_unit_ptr(unit);

    if (tq_state != TQ_STATE_UP) {
        return;
    }
    if (!u || !u->fp) {
        return;
    }
    if ((tq_cflgs & TQ_CF_ATN) == 0u) {
        return;
    }

    u->una_pending = 1;
    tq_poll_pending = 1;
}

static void tq_prepare_una(uint16_t unit, uint16_t *rsp)
{
    tq_unit_t *u = tq_unit_ptr(unit);

    memset(rsp, 0, TQ_PKT_WORDS * sizeof(uint16_t));
    rsp[TQ_HLNT] = 32u;
    rsp[TQ_HCTC] = TQ_HCTC_SEQ_TMSCP;
    rsp[TQ_RSP_REFL] = 0;
    rsp[TQ_RSP_REFH] = 0;
    rsp[TQ_RSP_UN] = unit;
    rsp[TQ_RSP_OPF] = TQ_OP_AVA;
    rsp[TQ_RSP_STS] = 0;
    rsp[8] = 0;
    rsp[TQ_GUS_UFL] = u ? ((u->read_only ? TQ_UF_WPH : 0u) |
                           (u->sw_write_protect ? TQ_UF_WPS : 0u))
                      : 0u;
    rsp[TQ_GUS_UIDD] =
        (uint16_t)(((uint16_t)TQ_UID_TAPE << 8) | (uint16_t)TQ_UNIT_MODEL);
    tq_put_u32(rsp, TQ_GUS_MEDL, TQ_MEDIA_ID);
}

static uint16_t tq_status_from_tap_motion(tq_tap_status_t st)
{
    switch (st) {
    case TQ_TAP_OK:
        return TQ_ST_SUC;
    case TQ_TAP_MARK:
        return TQ_ST_TMK;
    case TQ_TAP_BOT:
        return TQ_ST_BOT;
    case TQ_TAP_EOM_STATUS:
        return (uint16_t)(TQ_ST_SUC | TQ_SB_SUC_EOT);
    case TQ_TAP_WRP_STATUS:
        return tq_read_only ? (uint16_t)(TQ_ST_WPR | TQ_SB_WPR_HW)
               : (uint16_t)(TQ_ST_WPR | TQ_SB_WPR_SW);
    case TQ_TAP_IOERR:
    case TQ_TAP_INVRL:
    default:
        return TQ_ST_SXC;
    }
}

static void tq_cmd_scc(const uint16_t *cmd, uint16_t *rsp)
{
    uint16_t unit;

    if (cmd[TQ_SCC_MSV] != 0u) {
        tq_prepare_rsp(rsp, cmd, TQ_OP_SCC, 0, (uint16_t)(TQ_ST_CMD | TQ_I_VRSN),
                       TQ_SCC_LNT);
        return;
    }

    tq_cflgs = (uint16_t)(cmd[TQ_SCC_CFL] & TQ_CF_MSK);
    tq_prepare_rsp(rsp, cmd, TQ_OP_SCC, 0, TQ_ST_SUC, TQ_SCC_LNT);
    rsp[TQ_SCC_CFL] = tq_cflgs;
    rsp[TQ_SCC_TMO] = 0000170u;
    rsp[TQ_SCC_VER] = TQ_CTRL_REV;
    rsp[TQ_SCC_CIDD] =
        (uint16_t)(((uint16_t)TQ_CTRL_CLASS << 8) | (uint16_t)TQ_CTRL_MODEL);
    tq_put_u32(rsp, TQ_SCC_MBCL, TQ_MAX_TRANSFER);
    for (unit = 0; unit < TQ11_MAX_UNITS; unit++) {
        tq_schedule_una(unit);
    }
}

static void tq_cmd_gcs(const uint16_t *cmd, uint16_t *rsp)
{
    tq_prepare_rsp(rsp, cmd, TQ_OP_GCS, 0, TQ_ST_SUC, TQ_GCS_LNT);
    rsp[TQ_GCS_REFL] = 0;
    rsp[TQ_GCS_REFH] = 0;
    rsp[TQ_GCS_STSL] = 0;
    rsp[TQ_GCS_STSH] = 0;
}

static void tq_cmd_suc(const uint16_t *cmd, uint16_t *rsp)
{
    tq_prepare_rsp(rsp, cmd, TQ_OP_SUC, 0, 0, TQ_SUC_LNT);

    if (!tq_fp) {
        rsp[TQ_RSP_STS] = (uint16_t)(TQ_ST_OFL | TQ_SB_OFL_NV);
        tq_rsp_flags_from_status(rsp);
        return;
    }

    tq_sw_write_protect = (cmd[TQ_ONL_UFL] & TQ_UF_WPS) ? 1u : 0u;
    rsp[TQ_RSP_STS] = TQ_ST_SUC;
    tq_fill_online_status(rsp);
}

static void tq_cmd_flu(const uint16_t *cmd, uint16_t *rsp)
{
    tq_prepare_rsp(rsp, cmd, TQ_OP_FLU, 0, 0, TQ_FLU_LNT);
    tq_put_u32(rsp, TQ_FLU_POSL, tq_tape_pos);
    rsp[TQ_RSP_STS] = tq_motion_valid(TQ_OP_FLU);
    tq_rsp_flags_from_status(rsp);
}

static void tq_cmd_gus(const uint16_t *cmd, uint16_t *rsp)
{
    uint16_t status;

    tq_prepare_rsp(rsp, cmd, TQ_OP_GUS, 0, 0, TQ_GUS_LNT);

    if (!tq_fp) {
        status = (uint16_t)(TQ_ST_OFL | TQ_SB_OFL_NV);
    } else if (!tq_online) {
        status = TQ_ST_AVL;
    } else {
        status = TQ_ST_SUC;
    }

    rsp[TQ_RSP_STS] = status;
    rsp[TQ_GUS_UFL] = tq_current_unit_flags();
    rsp[TQ_GUS_UIDD] =
        (uint16_t)(((uint16_t)TQ_UID_TAPE << 8) | (uint16_t)TQ_UNIT_MODEL);
    tq_put_u32(rsp, TQ_GUS_MEDL, TQ_MEDIA_ID);
    rsp[TQ_GUS_FMT] = TQ_TAPE_FORMAT;
    rsp[TQ_GUS_SPEED] = 0;
    rsp[TQ_GUS_MENU] = TQ_TAPE_FORMAT;
    rsp[TQ_GUS_CAP] = 0;
    rsp[TQ_GUS_FVER] = 0;
    rsp[TQ_GUS_UVER] = 0;
    tq_rsp_flags_from_status(rsp);
}

static void tq_cmd_avl(const uint16_t *cmd, uint16_t *rsp)
{
    uint16_t status;

    tq_prepare_rsp(rsp, cmd, TQ_OP_AVL, 0, 0, TQ_RSP_LNT);

    if (!tq_fp) {
        rsp[TQ_RSP_STS] = (uint16_t)(TQ_ST_OFL | TQ_SB_OFL_NV);
        return;
    }

    tq_online = 0;
    tq_sw_write_protect = 0;
    if (tq_tap_rewind() != TQ_TAP_OK) {
        rsp[TQ_RSP_STS] = TQ_ST_SXC;
        return;
    }

    status = TQ_ST_SUC;
    if ((cmd[TQ_CMD_MOD] & TQ_MD_UNL) != 0u) {
        status = (uint16_t)(status | TQ_SB_SUC_IGN);
    }
    rsp[TQ_RSP_STS] = status;
    tq_rsp_flags_from_status(rsp);
}

static void tq_cmd_onl(const uint16_t *cmd, uint16_t *rsp)
{
    uint16_t status;

    tq_prepare_rsp(rsp, cmd, TQ_OP_ONL, 0, 0, TQ_ONL_LNT);

    if (!tq_fp) {
        rsp[TQ_RSP_STS] = (uint16_t)(TQ_ST_OFL | TQ_SB_OFL_NV);
        return;
    }

    if (tq_online) {
        status = (uint16_t)(TQ_ST_SUC | TQ_SB_SUC_ON);
    } else {
        tq_sw_write_protect = (cmd[TQ_ONL_UFL] & TQ_UF_WPS) ? 1u : 0u;
        if (tq_tap_rewind() != TQ_TAP_OK) {
            rsp[TQ_RSP_STS] = TQ_ST_SXC;
            return;
        }
        tq_online = 1;
        status = TQ_ST_SUC;
    }

    rsp[TQ_RSP_STS] = status;
    tq_fill_online_status(rsp);
    tq_rsp_flags_from_status(rsp);
}

static void tq_rw_rsp_common(uint16_t *rsp, uint32_t transferred, uint32_t pos,
                             uint32_t rec_size)
{
    tq_put_u32(rsp, TQ_RW_BCL, transferred);
    tq_put_u32(rsp, TQ_RW_POSL, pos);
    tq_put_u32(rsp, TQ_RW_RSZL, rec_size);
}

static int tq_dma_compare_bytes(uint32_t ba, const uint8_t *buf, uint32_t len)
{
    uint32_t i;

    for (i = 0; i < len; i++) {
        uint8_t host_byte = 0;

        if (tq_dma_read_bytes((bus_paddr_t)(ba + i), &host_byte, 1u) != 0) {
            return -1;
        }
        if (host_byte != buf[i]) {
            return 1;
        }
    }

    return 0;
}

typedef struct {
    uint32_t ba;
    uint16_t op;
} tq_dma_sink_t;

static int tq_tap_dma_sink(void *opaque, uint32_t offset,
                           const uint8_t *data, uint32_t len)
{
    tq_dma_sink_t *sink = (tq_dma_sink_t *)opaque;

    if (sink->op == TQ_OP_RD) {
        return tq_dma_write_bytes(sink->ba + offset, data, len);
    }
    if (sink->op == TQ_OP_CMP) {
        return tq_dma_compare_bytes(sink->ba + offset, data, len);
    }
    return 0;
}

static void tq_cmd_read_like(const uint16_t *cmd, uint16_t *rsp, uint16_t op)
{
    uint16_t status;
    uint32_t bc;
    uint32_t ba;
    uint32_t rec_len = 0;
    uint32_t xfer_len = 0;
    int sink_result = 0;
    tq_dma_sink_t sink;
    tq_tap_status_t tap_st;

    tq_prepare_rsp(rsp, cmd, op, 0, 0, TQ_RW_LNT);
    tq_rw_rsp_common(rsp, 0, tq_tape_pos, 0);

    status = tq_motion_valid(op);
    if (status != TQ_ST_SUC) {
        rsp[TQ_RSP_STS] = status;
        tq_rsp_flags_from_status(rsp);
        return;
    }

    bc = tq_get_u32(cmd, TQ_RW_BCL);
    ba = tq_get_u32(cmd, TQ_RW_BAL);
    if (bc == 0u || bc > TQ_MAX_TRANSFER) {
        rsp[TQ_RSP_STS] = (uint16_t)(TQ_ST_CMD | TQ_I_BCNT);
        return;
    }

    sink.ba = ba;
    sink.op = op;
    tap_st = tq_tap_read_forward_stream(bc, &rec_len, &xfer_len,
                                        op == TQ_OP_ACC ? NULL : tq_tap_dma_sink,
                                        &sink, &sink_result);
    if (tap_st == TQ_TAP_OK) {
        if (sink_result < 0) {
            rsp[TQ_RSP_STS] = (uint16_t)(TQ_ST_HST | TQ_SB_HST_NXM);
            tq_rw_rsp_common(rsp, 0, tq_tape_pos, rec_len);
            tq_rsp_flags_from_status(rsp);
            return;
        }
        if (sink_result > 0) {
            rsp[TQ_RSP_STS] = TQ_ST_SXC;
            tq_rw_rsp_common(rsp, xfer_len, tq_tape_pos, rec_len);
            tq_rsp_flags_from_status(rsp);
            return;
        }

        if (rec_len > bc) {
            rsp[TQ_RSP_STS] = TQ_ST_RDT;
        } else {
            rsp[TQ_RSP_STS] = TQ_ST_SUC;
        }
        tq_rw_rsp_common(rsp, xfer_len, tq_tape_pos, rec_len);
        tq_rsp_flags_from_status(rsp);
        return;
    }

    rsp[TQ_RSP_STS] = tq_status_from_tap_motion(tap_st);
    tq_rw_rsp_common(rsp, 0, tq_tape_pos, 0);
    tq_rsp_flags_from_status(rsp);
}

static void tq_cmd_read(const uint16_t *cmd, uint16_t *rsp)
{
    tq_cmd_read_like(cmd, rsp, TQ_OP_RD);
}

static void tq_cmd_access(const uint16_t *cmd, uint16_t *rsp)
{
    tq_cmd_read_like(cmd, rsp, TQ_OP_ACC);
}

static void tq_cmd_compare(const uint16_t *cmd, uint16_t *rsp)
{
    tq_cmd_read_like(cmd, rsp, TQ_OP_CMP);
}

static void tq_cmd_write(const uint16_t *cmd, uint16_t *rsp)
{
    uint16_t status;
    uint32_t bc;
    uint32_t ba;
    int dma_error = 0;
    tq_tap_status_t tap_st;

    tq_prepare_rsp(rsp, cmd, TQ_OP_WR, 0, 0, TQ_RW_LNT);
    tq_rw_rsp_common(rsp, 0, tq_tape_pos, 0);

    status = tq_motion_valid(TQ_OP_WR);
    if (status != TQ_ST_SUC) {
        rsp[TQ_RSP_STS] = status;
        tq_rsp_flags_from_status(rsp);
        return;
    }

    if (tq_sw_write_protect || tq_read_only) {
        rsp[TQ_RSP_STS] = tq_read_only ? (uint16_t)(TQ_ST_WPR | TQ_SB_WPR_HW)
                          : (uint16_t)(TQ_ST_WPR | TQ_SB_WPR_SW);
        tq_rsp_flags_from_status(rsp);
        return;
    }

    bc = tq_get_u32(cmd, TQ_RW_BCL);
    ba = tq_get_u32(cmd, TQ_RW_BAL);
    if (bc == 0u || bc > TQ_MAX_TRANSFER) {
        rsp[TQ_RSP_STS] = (uint16_t)(TQ_ST_CMD | TQ_I_BCNT);
        return;
    }

    tap_st = tq_tap_write_record_dma(ba, bc, &dma_error);
    if (dma_error) {
        rsp[TQ_RSP_STS] = (uint16_t)(TQ_ST_HST | TQ_SB_HST_NXM);
        tq_rsp_flags_from_status(rsp);
        return;
    }

    rsp[TQ_RSP_STS] = tq_status_from_tap_motion(tap_st);
    if (tap_st == TQ_TAP_OK) {
        tq_rw_rsp_common(rsp, bc, tq_tape_pos, bc);
    } else {
        tq_rw_rsp_common(rsp, 0, tq_tape_pos, 0);
    }
    tq_rsp_flags_from_status(rsp);
}

static void tq_cmd_wtm(const uint16_t *cmd, uint16_t *rsp)
{
    tq_tap_status_t tap_st;

    tq_prepare_rsp(rsp, cmd, TQ_OP_WTM, 0, 0, TQ_WTM_LNT);
    tq_put_u32(rsp, TQ_WTM_POSL, tq_tape_pos);

    if (tq_motion_valid(TQ_OP_WTM) != TQ_ST_SUC) {
        rsp[TQ_RSP_STS] = tq_motion_valid(TQ_OP_WTM);
        tq_rsp_flags_from_status(rsp);
        return;
    }

    if (tq_sw_write_protect || tq_read_only) {
        rsp[TQ_RSP_STS] = tq_read_only ? (uint16_t)(TQ_ST_WPR | TQ_SB_WPR_HW)
                          : (uint16_t)(TQ_ST_WPR | TQ_SB_WPR_SW);
        tq_rsp_flags_from_status(rsp);
        return;
    }

    tap_st = tq_tap_write_mark();
    rsp[TQ_RSP_STS] = tq_status_from_tap_motion(tap_st);
    tq_put_u32(rsp, TQ_WTM_POSL, tq_tape_pos);
    tq_rsp_flags_from_status(rsp);
}

static void tq_cmd_pos(const uint16_t *cmd, uint16_t *rsp)
{
    uint32_t rec_count;
    uint32_t tmk_count;
    uint32_t rec_done = 0;
    uint32_t tmk_done = 0;
    tq_tap_status_t tap_st = TQ_TAP_OK;
    uint16_t status;

    tq_prepare_rsp(rsp, cmd, TQ_OP_POS, 0, 0, TQ_POS_LNT);
    tq_put_u32(rsp, TQ_POS_RCL, 0);
    tq_put_u32(rsp, TQ_POS_TMCL, 0);
    tq_put_u32(rsp, TQ_POS_POSL, tq_tape_pos);

    status = tq_motion_valid(TQ_OP_POS);
    if (status != TQ_ST_SUC) {
        rsp[TQ_RSP_STS] = status;
        tq_rsp_flags_from_status(rsp);
        return;
    }

    rec_count = tq_get_u32(cmd, TQ_POS_RCL);
    tmk_count = tq_get_u32(cmd, TQ_POS_TMCL);

    if ((cmd[TQ_CMD_MOD] & TQ_MD_RWD) != 0u) {
        tap_st = tq_tap_rewind();
    } else if ((cmd[TQ_CMD_MOD] & TQ_MD_REV) != 0u) {
        if (tmk_count != 0u) {
            tap_st = tq_tap_space_reverse_marks(tmk_count, &tmk_done);
        } else if (rec_count != 0u) {
            tap_st = tq_tap_space_reverse_records(rec_count, &rec_done);
        }
    } else {
        if (tmk_count != 0u) {
            tap_st = tq_tap_space_forward_marks(tmk_count, &tmk_done);
        } else if (rec_count != 0u) {
            tap_st = tq_tap_space_forward_records(rec_count, &rec_done);
        }
    }

    rsp[TQ_RSP_STS] = tq_status_from_tap_motion(tap_st);
    tq_put_u32(rsp, TQ_POS_RCL, rec_done);
    tq_put_u32(rsp, TQ_POS_TMCL, tmk_done);
    tq_put_u32(rsp, TQ_POS_POSL, tq_tape_pos);
    tq_rsp_flags_from_status(rsp);
}

static void tq_process_command(const uint16_t *cmd, uint16_t *rsp)
{
    uint16_t op = (uint16_t)(cmd[TQ_CMD_OPC] & 0x00FFu);
    uint16_t unit = cmd[TQ_CMD_UN];

    tq_log("cmd op=%03o mod=%06o ref=%06o%06o unit=%o", (unsigned)op,
           (unsigned)cmd[TQ_CMD_MOD], (unsigned)cmd[TQ_CMD_REFH],
           (unsigned)cmd[TQ_CMD_REFL], (unsigned)cmd[TQ_CMD_UN]);

    if (unit >= TQ11_MAX_UNITS) {
        tq_prepare_rsp(rsp, cmd, op, 0, TQ_ST_OFL, TQ_RSP_LNT);
        rsp[TQ_RSP_UN] = unit;
        return;
    }
    tq_cmd_unit = unit;

    switch (op) {
    case TQ_OP_GCS:
        tq_cmd_gcs(cmd, rsp);
        return;

    case TQ_OP_SCC:
        tq_cmd_scc(cmd, rsp);
        return;

    case TQ_OP_GUS:
        tq_cmd_gus(cmd, rsp);
        return;

    case TQ_OP_AVL:
        tq_cmd_avl(cmd, rsp);
        return;

    case TQ_OP_ONL:
        tq_cmd_onl(cmd, rsp);
        return;

    case TQ_OP_SUC:
        tq_cmd_suc(cmd, rsp);
        return;

    case TQ_OP_FLU:
        tq_cmd_flu(cmd, rsp);
        return;

    case TQ_OP_ACC:
        tq_cmd_access(cmd, rsp);
        return;

    case TQ_OP_CMP:
        tq_cmd_compare(cmd, rsp);
        return;

    case TQ_OP_RD:
        tq_cmd_read(cmd, rsp);
        return;

    case TQ_OP_WR:
        tq_cmd_write(cmd, rsp);
        return;

    case TQ_OP_WTM:
        tq_cmd_wtm(cmd, rsp);
        return;

    case TQ_OP_POS:
        tq_cmd_pos(cmd, rsp);
        return;

    default:
        tq_prepare_rsp(rsp, cmd, op, 0, (uint16_t)(TQ_ST_CMD | TQ_I_OPCD),
                       TQ_RSP_LNT);
        return;
    }
}

static int tq_process_one(void)
{
    uint32_t cmd_desc;
    uint32_t rsp_desc;
    uint32_t cmd_slot;
    uint32_t rsp_slot;
    uint32_t cmd_addr;
    uint32_t rsp_addr;
    uint16_t cmd[TQ_PKT_WORDS];
    uint16_t rsp[TQ_PKT_WORDS];
    uint32_t rsp_bytes;
    int rc;

    rc = tq_ring_peek_owned(&tq_cmd_ring, &cmd_desc, &cmd_slot);
    if (rc <= 0) {
        return rc;
    }

    rc = tq_ring_peek_owned(&tq_rsp_ring, &rsp_desc, &rsp_slot);
    if (rc <= 0) {
        return rc;
    }

    cmd_addr = cmd_desc & TQ_DESC_ADDR;
    rsp_addr = rsp_desc & TQ_DESC_ADDR;
    if (cmd_addr < 4u || rsp_addr < 4u) {
        tq_fail(0000006u);
        return -1;
    }

    if (tq_dma_read_words((bus_paddr_t)(cmd_addr + TQ_HDR_OFF), cmd, TQ_PKT_WORDS) != 0) {
        tq_fail(0000001u);
        return -1;
    }

    tq_process_command(cmd, rsp);
    rsp_bytes = (uint32_t)rsp[TQ_HLNT] + 4u;
    if (tq_dma_write_words((bus_paddr_t)(rsp_addr + TQ_HDR_OFF), rsp, rsp_bytes / 2u) !=
            0) {
        tq_fail(0000002u);
        return -1;
    }

    if (tq_release_desc(&tq_cmd_ring, cmd_slot, cmd_desc) != 0) {
        tq_fail(0000007u);
        return -1;
    }
    if (tq_release_desc(&tq_rsp_ring, rsp_slot, rsp_desc) != 0) {
        tq_fail(0000010u);
        return -1;
    }

    tq_log("rsp op=%03o sts=%06o", (unsigned)(rsp[TQ_RSP_OPF] & 0x00FFu),
           (unsigned)rsp[TQ_RSP_STS]);
    tq_raise_irq();
    return 1;
}

static int tq_post_attention_una(void)
{
    uint32_t rsp_desc;
    uint32_t rsp_slot;
    uint32_t rsp_addr;
    uint16_t rsp[TQ_PKT_WORDS];
    uint16_t unit;
    int found = 0;

    for (unit = 0; unit < TQ11_MAX_UNITS; unit++) {
        if (tq_units[unit].una_pending) {
            found = 1;
            break;
        }
    }
    if (!found) {
        return 0;
    }

    if (tq_ring_peek_owned(&tq_rsp_ring, &rsp_desc, &rsp_slot) <= 0) {
        return 0;
    }

    rsp_addr = rsp_desc & TQ_DESC_ADDR;
    if (rsp_addr < 4u) {
        tq_fail(0000006u);
        return -1;
    }

    tq_prepare_una(unit, rsp);
    if (tq_dma_write_words((bus_paddr_t)(rsp_addr + TQ_HDR_OFF), rsp,
                           ((uint32_t)rsp[TQ_HLNT] + 4u) / 2u) != 0) {
        tq_fail(0000002u);
        return -1;
    }

    if (tq_release_desc(&tq_rsp_ring, rsp_slot, rsp_desc) != 0) {
        tq_fail(0000010u);
        return -1;
    }

    tq_units[unit].una_pending = 0;
    tq_log("attn op=%03o unit=%o", (unsigned)TQ_OP_AVA, (unsigned)unit);
    tq_raise_irq();
    return 1;
}

static void tq_service_poll(void)
{
    int budget = 8;

    while (budget-- > 0) {
        int rc;

        rc = tq_post_attention_una();
        if (rc > 0) {
            continue;
        }
        if (rc < 0) {
            tq_poll_pending = 0;
            return;
        }

        rc = tq_process_one();
        if (rc <= 0) {
            tq_poll_pending = 0;
            return;
        }
    }
}

static void tq_handle_ip_write(void)
{
    tq_log("ip wr");
    tq11_reset();
}

static void tq_handle_ip_read(void)
{
    tq_log("ip rd state=%o", (unsigned)tq_state);
    if (tq_state == TQ_STATE_S3_PPB) {
        (void)tq_step4();
        return;
    }
    if (tq_state == TQ_STATE_UP) {
        tq_poll_pending = 1;
    }
}

static void tq_handle_sa_write(uint16_t v)
{
    switch (tq_state) {
    case TQ_STATE_S1:
    case TQ_STATE_S1_WRAP:
        if ((v & TQ_S1H_WR) != 0u) {
            tq_sa = v;
            tq_state = TQ_STATE_S1_WRAP;
            tq_log("wrap %06o", (unsigned)v);
            return;
        }
        if ((v & TQ_S1H_VL) == 0u) {
            return;
        }
        tq_s1_host = v;
        tq_irq_en = (v & TQ_S1H_IE) ? 1u : 0u;
        tq_sa = (uint16_t)(TQ_SA_S2 | ((tq_s1_host >> 8) & 0x00FFu));
        tq_state = TQ_STATE_S2;
        tq_log("s1 %06o -> s2 %06o", (unsigned)v, (unsigned)tq_sa);
        tq_raise_irq();
        return;

    case TQ_STATE_S2:
        tq_comm = (uint32_t)(v & TQ_S2H_CLO);
        tq_prgi = (v & TQ_S2H_PI) ? 1u : 0u;
        tq_sa = (uint16_t)(TQ_SA_S3 | (tq_s1_host & 0x00FFu));
        tq_state = TQ_STATE_S3;
        tq_log("s2 commlo=%06o prgi=%u", (unsigned)tq_comm, (unsigned)tq_prgi);
        tq_raise_irq();
        return;

    case TQ_STATE_S3:
        tq_comm |= ((uint32_t)(v & TQ_S3H_CHI) << 16);
        if ((v & TQ_S3H_PP) != 0u) {
            tq_sa = 0;
            tq_state = TQ_STATE_S3_PPA;
            tq_log("s3 purge");
            tq_raise_irq();
            return;
        }
        (void)tq_step4();
        return;

    case TQ_STATE_S3_PPA:
        if (v == 0u) {
            tq_state = TQ_STATE_S3_PPB;
            tq_log("purge ack");
        }
        return;

    case TQ_STATE_S4:
        if ((v & TQ_S4H_GO) != 0u) {
            tq_state = TQ_STATE_UP;
            tq_sa = 0;
            tq_log("online");
        }
        return;

    case TQ_STATE_UP:
    case TQ_STATE_DEAD:
    default:
        return;
    }
}

static uint8_t tq11_read8(uint16_t addr)
{
    switch (addr & 03u) {
    case 0u:
    case 1u:
        tq_handle_ip_read();
        return 0;

    case 2u:
        tq_log("sa rd -> %06o", (unsigned)tq_sa);
        return (uint8_t)(tq_sa & 0x00FFu);

    case 3u:
        return (uint8_t)((tq_sa >> 8) & 0x00FFu);

    default:
        return 0;
    }
}

static void tq11_write8(uint16_t addr, uint8_t v)
{
    switch (addr & 03u) {
    case 0u:
    case 1u:
        (void)v;
        tq_handle_ip_write();
        return;

    case 2u:
        tq_sa_latch = (uint16_t)((tq_sa_latch & 0xFF00u) | (uint16_t)v);
        return;

    case 3u:
        tq_sa_latch =
            (uint16_t)((tq_sa_latch & 0x00FFu) | ((uint16_t)v << 8));
        tq_log("sa wr %06o", (unsigned)tq_sa_latch);
        tq_handle_sa_write(tq_sa_latch);
        return;

    default:
        return;
    }
}

int tq11_init(void)
{
    static const io_range_t io = {
        TQ_BASE, (uint16_t)(TQ_BASE + 3u), tq11_read8, tq11_write8, "TQ11"
    };
    static const irq_source_t irq = {"TQ11", TQ_VECTOR, TQ_PRIORITY, tq_irq_pending,
                                     tq_irq_ack
                                    };

    tq_trace = 0u;

    if (devio_register(&io) != 0) {
        return -1;
    }
    if (irq_register(&irq) != 0) {
        return -1;
    }

    tq11_reset();
    return 0;
}

void tq11_reset(void)
{
    uint16_t unit;

    tq_sa = tq_initial_sa();
    tq_sa_latch = 0;
    tq_s1_host = 0;
    tq_comm = 0;
    tq_prgi = 0;
    tq_cmd_unit = 0;
    tq_state = TQ_STATE_S1;
    tq_irq_req = 0;
    tq_irq_en = 0;
    tq_poll_pending = 0;
    tq_cflgs = 0;
    for (unit = 0; unit < TQ11_MAX_UNITS; unit++) {
        tq_units[unit].online = 0;
        tq_units[unit].sw_write_protect = 0;
        tq_units[unit].una_pending = 0;
        tq_units[unit].tape_pos = 0;
    }
    tq_ring_reset(&tq_cmd_ring);
    tq_ring_reset(&tq_rsp_ring);
    tq_log("reset sa=%06o", (unsigned)tq_sa);
}

void tq11_poll(void)
{
    if (tq_state != TQ_STATE_UP) {
        return;
    }
    if (!tq_poll_pending) {
        return;
    }
    tq_service_poll();
}

int tq11_open_image_unit(unsigned unit, const char *path)
{
    tq_unit_t *u;
    uint16_t saved_unit;

    if (!path || unit >= TQ11_MAX_UNITS) {
        return -1;
    }

    u = &tq_units[unit];
    if (u->fp) {
        emu_fclose(u->fp);
        u->fp = NULL;
    }

    u->fp = emu_fopen(path, "r+b");
    u->read_only = 0;
    if (!u->fp) {
        u->fp = emu_fopen(path, "rb");
        u->read_only = 1;
    }
    if (!u->fp) {
        return -1;
    }

    u->tape_pos = 0;
    u->online = 0;
    u->sw_write_protect = 0;
    u->una_pending = 0;
    saved_unit = tq_cmd_unit;
    tq_cmd_unit = (uint16_t)unit;
    if (tq_tap_seek(0) != 0) {
        tq_cmd_unit = saved_unit;
        emu_fclose(u->fp);
        u->fp = NULL;
        u->read_only = 0;
        return -1;
    }
    tq_cmd_unit = saved_unit;
    tq_log("attach unit=%o pos=%07o ro=%u", (unsigned)unit,
           (unsigned)u->tape_pos, (unsigned)u->read_only);
    tq_schedule_una((uint16_t)unit);
    return 0;
}

int tq11_open_image(const char *path)
{
    return tq11_open_image_unit(0, path);
}

void tq11_close_image(void)
{
    uint16_t unit;

    for (unit = 0; unit < TQ11_MAX_UNITS; unit++) {
        tq_unit_t *u = &tq_units[unit];
        if (u->fp) {
            emu_fclose(u->fp);
            u->fp = NULL;
        }
        u->read_only = 0;
        u->tape_pos = 0;
        u->online = 0;
        u->sw_write_protect = 0;
        u->una_pending = 0;
    }
    tq_poll_pending = 0;
}

int tq11_attached(void)
{
    uint16_t unit;

    for (unit = 0; unit < TQ11_MAX_UNITS; unit++) {
        if (tq_units[unit].fp) {
            return 1;
        }
    }
    return 0;
}

#if defined(LSI11_TESTS)
int tq11_test_tap_read_record(uint8_t *buf, size_t max_len, size_t *rec_len)
{
    uint32_t got = 0;
    tq_tap_status_t st =
        tq_tap_read_forward(buf, (uint32_t)max_len, &got, NULL);
    if (rec_len) {
        *rec_len = (size_t)got;
    }
    return (int)st;
}

int tq11_test_tap_write_record(const uint8_t *buf, size_t len)
{
    return (int)tq_tap_write_record(buf, (uint32_t)len);
}

int tq11_test_tap_write_mark(void)
{
    return (int)tq_tap_write_mark();
}

int tq11_test_tap_space_forward_record(void)
{
    return (int)tq_tap_space_forward_records(1u, NULL);
}

int tq11_test_tap_space_reverse_record(void)
{
    return (int)tq_tap_space_reverse_records(1u, NULL);
}

int tq11_test_tap_rewind(void)
{
    return (int)tq_tap_rewind();
}
#endif
