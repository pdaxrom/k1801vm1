#include "dev_rq11.h"

#include "bus.h"
#include "devio.h"
#include "emu_file.h"
#include "irq.h"
#include "ubmap.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* RQ (MSCP) controller registers (octal). */
#define RQ_BASE     0172150u
#define RQ_IP       0172150u
#define RQ_SA       0172152u
#define RQ_VECTOR   0000154u
#define RQ_PRIORITY 5u

#define RQ_NUMBY 512u

/* UQSSP port bits. */
#define RQ_SA_ER 0x8000u
#define RQ_SA_S4 0x4000u
#define RQ_SA_S3 0x2000u
#define RQ_SA_S2 0x1000u
#define RQ_SA_S1 0x0800u

#define RQ_S1C_NV        0x0400u
#define RQ_S1C_Q22       0x0200u
#define RQ_S1C_DI        0x0100u
#define RQ_S1C_MP        0x0040u
#define RQ_S1C_CAPS_QBUS (RQ_SA_S1 | RQ_S1C_Q22 | RQ_S1C_DI | RQ_S1C_MP)
#define RQ_S1C_CAPS_UBUS (RQ_SA_S1 | RQ_S1C_DI | RQ_S1C_MP)

#define RQ_S1H_VL   0x8000u
#define RQ_S1H_WR   0x4000u
#define RQ_S1H_V_CQ 11u
#define RQ_S1H_M_CQ 0x0007u
#define RQ_S1H_V_RQ 8u
#define RQ_S1H_M_RQ 0x0007u
#define RQ_S1H_IE   0x0080u

#define RQ_S2H_CLO 0xFFFEu
#define RQ_S2H_PI  0x0001u

#define RQ_S3H_PP  0x8000u
#define RQ_S3H_CHI 0x7FFFu

#define RQ_S4H_GO 0x0001u

#define RQ_SA_COMM_CI (-4)
#define RQ_SA_COMM_RI (-2)

#define RQ_DESC_OWN  0x80000000u
#define RQ_DESC_F    0x40000000u
#define RQ_DESC_ADDR 0x003FFFFEu

#define RQ_HDR_OFF (-4)
#define RQ_HLNT    0u
#define RQ_HCTC    1u
#define RQ_HCTC_V_CR  0u
#define RQ_HCTC_V_TYP 4u
#define RQ_HCTC_V_CID 8u
#define RQ_HCTC_TYP_SEQ 0u
#define RQ_HCTC_CID_MSCP 0u

/* MSCP packet fields. */
#define RQ_CMD_REFL 2u
#define RQ_CMD_REFH 3u
#define RQ_CMD_UN   4u
#define RQ_CMD_OPC  6u
#define RQ_CMD_MOD  7u

#define RQ_RSP_LNT  12u
#define RQ_RSP_REFL 2u
#define RQ_RSP_REFH 3u
#define RQ_RSP_UN   4u
#define RQ_RSP_OPF  6u
#define RQ_RSP_STS  7u

#define RQ_UNA_LNT  32u

#define RQ_GCS_LNT  20u
#define RQ_GCS_REFL 8u
#define RQ_GCS_REFH 9u
#define RQ_GCS_STSL 10u
#define RQ_GCS_STSH 11u

#define RQ_GUS_LNT_D 48u
#define RQ_GUS_MLUN  8u
#define RQ_GUS_UFL   9u
#define RQ_GUS_RSVL  10u
#define RQ_GUS_RSVH  11u
#define RQ_GUS_UIDA  12u
#define RQ_GUS_UIDB  13u
#define RQ_GUS_UIDC  14u
#define RQ_GUS_UIDD  15u
#define RQ_GUS_MEDL  16u
#define RQ_GUS_MEDH  17u
#define RQ_GUS_SHUN  18u
#define RQ_GUS_SHST  19u
#define RQ_GUS_TRK   20u
#define RQ_GUS_GRP   21u
#define RQ_GUS_CYL   22u
#define RQ_GUS_UVER  23u
#define RQ_GUS_RCTS  24u
#define RQ_GUS_RBSC  25u

#define RQ_GUS_RB_V_RBNS 0u
#define RQ_GUS_RB_V_RCTC 8u

#define RQ_ONL_LNT  44u
#define RQ_ONL_MLUN 8u
#define RQ_ONL_UFL  9u
#define RQ_ONL_RSVL 10u
#define RQ_ONL_RSVH 11u
#define RQ_ONL_UIDA 12u
#define RQ_ONL_UIDB 13u
#define RQ_ONL_UIDC 14u
#define RQ_ONL_UIDD 15u
#define RQ_ONL_MEDL 16u
#define RQ_ONL_MEDH 17u
#define RQ_ONL_SHUN 18u
#define RQ_ONL_SHST 19u
#define RQ_ONL_SIZL 20u
#define RQ_ONL_SIZH 21u
#define RQ_ONL_VSNL 22u
#define RQ_ONL_VSNH 23u

#define RQ_SCC_LNT  32u
#define RQ_SCC_MSV  8u
#define RQ_SCC_CFL  9u
#define RQ_SCC_TMO  10u
#define RQ_SCC_VER  11u
#define RQ_SCC_CIDA 12u
#define RQ_SCC_CIDB 13u
#define RQ_SCC_CIDC 14u
#define RQ_SCC_CIDD 15u
#define RQ_SCC_MBCL 16u
#define RQ_SCC_MBCH 17u

#define RQ_SCC_VER_V_SVER 0u
#define RQ_SCC_VER_V_HVER 8u
#define RQ_SCC_CIDD_V_MOD 0u
#define RQ_SCC_CIDD_V_CLS 8u

#define RQ_UIDD_V_MOD 0u
#define RQ_UIDD_V_CLS 8u

#define RQ_RW_LNT_D 32u
#define RQ_RW_BCL   8u
#define RQ_RW_BCH   9u
#define RQ_RW_BAL   10u
#define RQ_RW_BAH   11u
#define RQ_RW_MAPL  12u
#define RQ_RW_MAPH  13u
#define RQ_RW_LBNL  16u
#define RQ_RW_LBNH  17u

/* Opcodes. */
#define RQ_OP_GCS 0002u
#define RQ_OP_GUS 0003u
#define RQ_OP_SCC 0004u
#define RQ_OP_AVL 0010u
#define RQ_OP_ONL 0011u
#define RQ_OP_SUC 0012u
#define RQ_OP_ACC 0020u
#define RQ_OP_CMP 0040u
#define RQ_OP_RD  0041u
#define RQ_OP_WR  0042u
#define RQ_OP_AVA 0100u
#define RQ_OP_END 0200u

/* Modifiers and flags. */
#define RQ_MD_NXU 0x0001u
#define RQ_MD_SWP 0x0004u

/* Status codes. */
#define RQ_ST_SUC 0u
#define RQ_ST_CMD 1u
#define RQ_ST_OFL 3u
#define RQ_ST_AVL 4u
#define RQ_ST_WPR 6u
#define RQ_ST_CMP 7u
#define RQ_ST_HST 9u
#define RQ_ST_SXC 18u

#define RQ_ST_V_SUB 5u
#define RQ_ST_V_INV 8u

/* Status subcodes. */
#define RQ_SB_OFL_NV  (1u << RQ_ST_V_SUB)
#define RQ_SB_WPR_SW  (128u << RQ_ST_V_SUB)
#define RQ_SB_WPR_HW  (256u << RQ_ST_V_SUB)
#define RQ_SB_SUC_ON  (8u << RQ_ST_V_SUB)
#define RQ_SB_HST_OA  (1u << RQ_ST_V_SUB)
#define RQ_SB_HST_OC  (2u << RQ_ST_V_SUB)
#define RQ_SB_HST_NXM (3u << RQ_ST_V_SUB)

/* Invalid command subcodes. */
#define RQ_I_OPCD (8u << RQ_ST_V_INV)
#define RQ_I_BCNT (12u << RQ_ST_V_INV)
#define RQ_I_LBN  (28u << RQ_ST_V_INV)
#define RQ_I_VRSN (12u << RQ_ST_V_INV)

/* End flags. */
#define RQ_EF_SXC 0x0010u

/* Controller and unit flags. */
#define RQ_CF_RPL 0x8000u
#define RQ_CF_ATN 0x0080u
#define RQ_CF_MSC 0x0040u
#define RQ_CF_OTH 0x0020u
#define RQ_CF_THS 0x0010u
#define RQ_CF_MSK (RQ_CF_ATN | RQ_CF_MSC | RQ_CF_OTH | RQ_CF_THS)

#define RQ_UF_WPH 0x2000u
#define RQ_UF_WPS 0x1000u
#define RQ_UF_RMV 0x0080u
#define RQ_UF_RPL 0x8000u

#define RQ_UID_DISK 2u
#define RQ_CTRL_CLASS 1u
#define RQ_CTRL_MODEL_QBUS 19u
#define RQ_CTRL_MODEL_UBUS 6u
#define RQ_HVER 1u
#define RQ_SVER 3u

#define RQ_PKT_WORDS 32u

typedef struct {
    const char *name;
    uint32_t lbn;
    uint16_t model;
    uint32_t media;
    uint16_t sect;
    uint16_t tpg;
    uint16_t gpc;
    uint16_t rcts;
    uint8_t removable;
} rq_drive_t;

static const rq_drive_t rq_drives[] = {
    {"RD54", 311200u, 13u, 0x25644036u, 17u, 15u, 1u, 7u, 0u},
    {"RD53", 138672u, 9u,  0x25644035u, 17u, 8u,  1u, 5u, 0u},
    {"RD52", 60480u,  8u,  0x25644034u, 17u, 8u,  1u, 4u, 0u},
    {"RD51", 21600u,  6u,  0x25644033u, 18u, 4u,  1u, 36u, 0u},
    {"RD31", 41560u,  12u, 0x2564401Fu, 17u, 4u,  1u, 3u, 0u},
    {"RD32", 83204u,  15u, 0x25644020u, 17u, 6u,  1u, 4u, 0u},
    {"RA60", 400176u, 4u,  0x22A4103Cu, 42u, 6u,  1u, 1008u, 1u},
    {"RA70", 547041u, 18u, 0x25641046u, 33u, 11u, 1u, 198u, 0u},
    {"RA80", 237212u, 1u,  0x25641050u, 31u, 14u, 1u, 0u, 0u},
    {"RA81", 891072u, 5u,  0x25641051u, 51u, 14u, 1u, 2856u, 0u},
    {"RA82", 1216665u, 11u, 0x25641052u, 57u, 15u, 1u, 3420u, 0u},
    {"RA71", 1367310u, 40u, 0x25641047u, 51u, 14u, 1u, 1428u, 0u},
    {"RX50", 800u,    7u,  0x25658032u, 10u, 5u,  16u, 0u, 1u},
    {"RX33", 2400u,   10u, 0x25658021u, 15u, 2u,  1u, 0u, 1u},
    {NULL, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u}
};

/* Controller state. */
#define RQ_STATE_S1      0u
#define RQ_STATE_S1_WRAP 1u
#define RQ_STATE_S2      2u
#define RQ_STATE_S3      3u
#define RQ_STATE_S3_PPA  4u
#define RQ_STATE_S3_PPB  5u
#define RQ_STATE_S4      6u
#define RQ_STATE_UP      7u
#define RQ_STATE_DEAD    8u

typedef struct {
    uint32_t base;
    uint32_t len;
    uint32_t idx;
    int ioff;
} rq_ring_t;

typedef struct {
    emu_file_t *fp;
    uint8_t read_only;
    uint8_t online;
    uint8_t sw_write_protect;
    uint8_t una_pending;
    const rq_drive_t *drive;
    uint32_t file_blocks;
    uint32_t blocks;
} rq_unit_t;

static uint16_t rq_sa;
static uint16_t rq_sa_latch;
static uint16_t rq_s1_host;
static uint32_t rq_comm;
static uint8_t rq_prgi;
static uint8_t rq_state;
static uint8_t rq_irq_req;
static uint8_t rq_irq_en;
static uint8_t rq_poll_pending;
static uint16_t rq_cflgs;
static uint8_t rq_credits;
static uint8_t rq_trace;
static uint8_t rq_log_onl_rsp;
static uint8_t rq_verify_init;
static uint8_t rq_verify_left;
static uint8_t rq_log_pkt_addr;
static uint8_t rq_ubus_mode;
static uint16_t rq_ctrl_model;
static rq_ring_t rq_cmd_ring;
static rq_ring_t rq_rsp_ring;
static rq_unit_t rq_units[RQ11_MAX_UNITS];

static const rq_drive_t *rq_default_drive(void)
{
    return &rq_drives[0];
}

static const rq_drive_t *rq_pick_drive(uint32_t blocks)
{
    const rq_drive_t *best = rq_default_drive();
    uint32_t best_diff;

    if (best->lbn == 0) {
        return best;
    }

    best_diff = (blocks > best->lbn) ? (blocks - best->lbn) : (best->lbn - blocks);

    for (const rq_drive_t *d = rq_drives; d->name; d++) {
        uint32_t diff = (blocks > d->lbn) ? (blocks - d->lbn) : (d->lbn - blocks);
        if (diff < best_diff) {
            best = d;
            best_diff = diff;
            if (best_diff == 0u) {
                break;
            }
        }
    }

    return best;
}

static const rq_drive_t *rq_drive_for_unit(const rq_unit_t *u)
{
    if (u && u->drive) {
        return u->drive;
    }
    return rq_default_drive();
}

static void rq_log(const char *fmt, ...)
{
    va_list ap;

    if (!rq_trace) {
        return;
    }

    va_start(ap, fmt);
    fprintf(stderr, "RQ ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

static rq_unit_t *rq_unit_ptr(uint16_t unit)
{
    if (unit >= RQ11_MAX_UNITS) {
        return NULL;
    }
    return &rq_units[unit];
}

static uint16_t rq_initial_sa(void)
{
    return rq_ubus_mode ? RQ_S1C_CAPS_UBUS : RQ_S1C_CAPS_QBUS;
}

static void rq_irq_ack(void)
{
    rq_irq_req = 0;
    rq_log("irq ack");
}

static int rq_irq_pending(void)
{
    return rq_irq_req ? 1 : 0;
}

static void rq_raise_irq(void)
{
    if (!rq_irq_en) {
        return;
    }
    if (rq_irq_req) {
        return;
    }
    rq_irq_req = 1;
    rq_log("irq req vec=%06o pri=%o", (unsigned)RQ_VECTOR, (unsigned)RQ_PRIORITY);
}

static paddr_t rq_dma_map_addr(uint32_t bus_addr)
{
    if (rq_ubus_mode) {
        return (paddr_t)(bus_addr & 000777777u);
    }
    if (bus_machine() == BUS_MACHINE_PDP1184) {
        return (paddr_t)(bus_addr & 017777777u);
    }
    return (paddr_t)(bus_addr & 000777777u);
}

static int rq_dma_read8_checked(paddr_t addr, uint8_t *v)
{
    paddr_t pa = rq_dma_map_addr((uint32_t)addr);

    if (!v) {
        return -1;
    }
    if (!bus_range_is_ram(pa, 1) || bus_is_nxm(pa)) {
        return -1;
    }
    *v = bus_read8(pa);
    return 0;
}

static int rq_dma_write8_checked(paddr_t addr, uint8_t v)
{
    paddr_t pa = rq_dma_map_addr((uint32_t)addr);

    if (!bus_range_is_ram(pa, 1) || bus_is_nxm(pa)) {
        return -1;
    }
    bus_write8(pa, v);
    return 0;
}

static int rq_dma_read16_checked(paddr_t addr, uint16_t *v)
{
    uint8_t lo;
    uint8_t hi;

    if (!v) {
        return -1;
    }
    if (rq_dma_read8_checked(addr, &lo) != 0 ||
            rq_dma_read8_checked((paddr_t)(addr + 1u), &hi) != 0) {
        return -1;
    }
    *v = (uint16_t)((uint16_t)lo | ((uint16_t)hi << 8));
    return 0;
}

static int rq_dma_write16_checked(paddr_t addr, uint16_t v)
{
    if (rq_dma_write8_checked(addr, (uint8_t)(v & 000377u)) != 0 ||
            rq_dma_write8_checked((paddr_t)(addr + 1u),
                                  (uint8_t)((v >> 8) & 000377u)) != 0) {
        return -1;
    }
    return 0;
}

static int rq_dma_read32_checked(paddr_t addr, uint32_t *v)
{
    uint16_t lo;
    uint16_t hi;

    if (!v) {
        return -1;
    }
    if (rq_dma_read16_checked(addr, &lo) != 0 ||
            rq_dma_read16_checked(addr + 2u, &hi) != 0) {
        return -1;
    }
    *v = (uint32_t)lo | ((uint32_t)hi << 16);
    return 0;
}

static int rq_dma_write32_checked(paddr_t addr, uint32_t v)
{
    if (rq_dma_write16_checked(addr, (uint16_t)(v & 0xFFFFu)) != 0 ||
            rq_dma_write16_checked(addr + 2u, (uint16_t)((v >> 16) & 0xFFFFu)) !=
                    0) {
        return -1;
    }
    return 0;
}

static int rq_dma_read_words(paddr_t addr, uint16_t *dst, size_t words)
{
    if (!dst) {
        return -1;
    }
    for (size_t i = 0; i < words; i++) {
        if (rq_dma_read16_checked(addr + (paddr_t)(i * 2u), &dst[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

static int rq_dma_write_words(paddr_t addr, const uint16_t *src, size_t words)
{
    if (!src) {
        return -1;
    }
    for (size_t i = 0; i < words; i++) {
        if (rq_dma_write16_checked(addr + (paddr_t)(i * 2u), src[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

static int rq_dma_read_bytes(uint32_t addr, uint8_t *dst, uint32_t len)
{
    if (!dst) {
        return -1;
    }
    for (uint32_t i = 0; i < len; i++) {
        if (rq_dma_read8_checked((paddr_t)(addr + i), &dst[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

static int rq_dma_write_bytes(uint32_t addr, const uint8_t *src, uint32_t len)
{
    if (!src) {
        return -1;
    }
    for (uint32_t i = 0; i < len; i++) {
        if (rq_dma_write8_checked((paddr_t)(addr + i), src[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

static uint32_t rq_get_u32(const uint16_t *pkt, size_t idx)
{
    return (uint32_t)pkt[idx] | ((uint32_t)pkt[idx + 1] << 16);
}

static void rq_put_u32(uint16_t *pkt, size_t idx, uint32_t v)
{
    pkt[idx] = (uint16_t)(v & 0xFFFFu);
    pkt[idx + 1] = (uint16_t)((v >> 16) & 0xFFFFu);
}

static void rq_ring_reset(rq_ring_t *ring)
{
    if (!ring) {
        return;
    }
    ring->base = 0;
    ring->len = 0;
    ring->idx = 0;
    ring->ioff = 0;
}

static uint32_t rq_ring_slot_addr(const rq_ring_t *ring)
{
    return ring->base + ring->idx;
}

static void rq_ring_advance(rq_ring_t *ring)
{
    if (!ring || ring->len == 0) {
        return;
    }
    ring->idx += 4u;
    if (ring->idx >= ring->len) {
        ring->idx = 0;
    }
}

static int rq_ring_peek_owned(const rq_ring_t *ring, uint32_t *desc_out,
                              uint32_t *slot_out)
{
    uint32_t desc;
    uint32_t slot;

    if (!ring || ring->len == 0 || !desc_out || !slot_out) {
        return 0;
    }

    slot = rq_ring_slot_addr(ring);
    if (rq_dma_read32_checked((paddr_t)slot, &desc) != 0) {
        return -1;
    }
    if ((desc & RQ_DESC_OWN) == 0) {
        return 0;
    }

    *desc_out = desc;
    *slot_out = slot;
    return 1;
}

static void rq_write_intr_flag(int off)
{
    int32_t sum;

    if (rq_comm == 0) {
        return;
    }

    sum = (int32_t)rq_comm + off;
    if (sum < 0) {
        return;
    }

    (void)rq_dma_write16_checked((paddr_t)sum, 000001u);
}

static int rq_release_desc(rq_ring_t *ring, uint32_t slot_addr, uint32_t desc)
{
    uint32_t released = (desc & ~RQ_DESC_OWN) | RQ_DESC_F;

    if (rq_dma_write32_checked((paddr_t)slot_addr, released) != 0) {
        return -1;
    }

    if (ring == &rq_cmd_ring) {
        rq_write_intr_flag(RQ_SA_COMM_CI);
    } else if (ring == &rq_rsp_ring) {
        rq_write_intr_flag(RQ_SA_COMM_RI);
    }

    rq_ring_advance(ring);
    return 0;
}

static void rq_fail(uint16_t code)
{
    rq_state = RQ_STATE_DEAD;
    rq_sa = (uint16_t)(RQ_SA_ER | (code & 0x07FFu));
    rq_log("fatal state=%u code=%06o", (unsigned)rq_state, (unsigned)rq_sa);
    rq_raise_irq();
}

static uint32_t rq_ring_desc_count(uint16_t s1, unsigned shift)
{
    uint32_t pow2 = (uint32_t)((s1 >> shift) & RQ_S1H_M_CQ);
    return 1u << pow2;
}

static int rq_clear_comm_area(void)
{
    int32_t start;
    uint32_t end;
    paddr_t addr;

    if (rq_comm < 4u) {
        return -1;
    }

    start = (int32_t)rq_comm + RQ_SA_COMM_CI;
    end = rq_cmd_ring.base + rq_cmd_ring.len;
    if (start < 0 || (uint32_t)start > end) {
        return -1;
    }

    for (addr = (paddr_t)start; addr < end; addr += 2u) {
        if (rq_dma_write16_checked(addr, 0) != 0) {
            return -1;
        }
    }

    return 0;
}

static int rq_step4(void)
{
    uint32_t rsp_descs;
    uint32_t cmd_descs;

    rsp_descs = rq_ring_desc_count(rq_s1_host, RQ_S1H_V_RQ);
    cmd_descs = rq_ring_desc_count(rq_s1_host, RQ_S1H_V_CQ);

    rq_rsp_ring.base = rq_comm;
    rq_rsp_ring.len = rsp_descs * 4u;
    rq_rsp_ring.idx = 0;
    rq_rsp_ring.ioff = RQ_SA_COMM_RI;

    rq_cmd_ring.base = rq_rsp_ring.base + rq_rsp_ring.len;
    rq_cmd_ring.len = cmd_descs * 4u;
    rq_cmd_ring.idx = 0;
    rq_cmd_ring.ioff = RQ_SA_COMM_CI;

    if (rq_clear_comm_area() != 0) {
        rq_fail(0000007u);
        return -1;
    }

    rq_sa = (uint16_t)(RQ_SA_S4 | (rq_ctrl_model << 4) | RQ_SVER);
    rq_state = RQ_STATE_S4;
    rq_log("step4 comm=%07o rq=%u cq=%u", (unsigned)rq_comm, (unsigned)rsp_descs,
           (unsigned)cmd_descs);
    rq_raise_irq();
    return 0;
}

static uint16_t rq_current_unit_flags(const rq_unit_t *u)
{
    uint16_t flags = 0;
    const rq_drive_t *drv = rq_drive_for_unit(u);

    if (!u) {
        return 0;
    }
    flags |= RQ_UF_RPL;
    if (drv->removable) {
        flags |= RQ_UF_RMV;
    }
    if (u->read_only) {
        flags |= RQ_UF_WPH;
    }
    if (u->sw_write_protect) {
        flags |= RQ_UF_WPS;
    }
    return flags;
}

static void rq_apply_onl_flags(const uint16_t *cmd, rq_unit_t *u)
{
    if (!cmd || !u) {
        return;
    }
    if ((cmd[RQ_CMD_MOD] & RQ_MD_SWP) != 0u &&
            (cmd[RQ_ONL_UFL] & RQ_UF_WPS) != 0u) {
        u->sw_write_protect = 1u;
    } else {
        u->sw_write_protect = 0u;
    }
}

static uint16_t rq_response_flags_for_status(uint16_t status)
{
    uint16_t code = (uint16_t)(status & 0x001Fu);

    if (code == RQ_ST_SXC) {
        return RQ_EF_SXC;
    }
    return 0;
}

static uint16_t rq_build_hctc(uint16_t opf)
{
    (void)opf;
    return (uint16_t)((RQ_HCTC_TYP_SEQ << RQ_HCTC_V_TYP) |
                      (RQ_HCTC_CID_MSCP << RQ_HCTC_V_CID));
}

static void rq_apply_credits(uint16_t *rsp)
{
    uint16_t typ;
    uint16_t cr;

    if (!rsp) {
        return;
    }
    if ((rsp[RQ_RSP_OPF] & RQ_OP_END) == 0u) {
        return;
    }
    typ = (uint16_t)((rsp[RQ_HCTC] >> RQ_HCTC_V_TYP) & 0x000Fu);
    if (typ != RQ_HCTC_TYP_SEQ) {
        return;
    }
    cr = (rq_credits >= 14u) ? 14u : rq_credits;
    rq_credits = (uint8_t)(rq_credits - cr);
    rsp[RQ_HCTC] = (uint16_t)(rsp[RQ_HCTC] | (uint16_t)((cr + 1u) << RQ_HCTC_V_CR));
}

static void rq_prepare_rsp(uint16_t *rsp, const uint16_t *cmd, uint16_t op,
                           uint16_t flags, uint16_t status, uint16_t len)
{
    uint16_t opf = (uint16_t)(op | RQ_OP_END | (uint16_t)(flags << 8));

    if (cmd) {
        memcpy(rsp, cmd, RQ_PKT_WORDS * sizeof(uint16_t));
    } else {
        memset(rsp, 0, RQ_PKT_WORDS * sizeof(uint16_t));
    }
    rsp[RQ_HLNT] = len;
    rsp[RQ_HCTC] = rq_build_hctc(opf);
    rsp[RQ_RSP_REFL] = cmd ? cmd[RQ_CMD_REFL] : 0u;
    rsp[RQ_RSP_REFH] = cmd ? cmd[RQ_CMD_REFH] : 0u;
    rsp[RQ_RSP_UN] = cmd ? cmd[RQ_CMD_UN] : 0u;
    rsp[RQ_RSP_OPF] = opf;
    rsp[RQ_RSP_STS] = status;
}

static void rq_rsp_flags_from_status(uint16_t *rsp)
{
    uint16_t flags = rq_response_flags_for_status(rsp[RQ_RSP_STS]);
    rsp[RQ_RSP_OPF] = (uint16_t)(rsp[RQ_RSP_OPF] | (uint16_t)(flags << 8));
}

static void rq_fill_unit_id(uint16_t *rsp, uint16_t uid_idx, uint16_t med_idx,
                            uint16_t unit, const rq_drive_t *drv)
{
    if (!rsp || !drv) {
        return;
    }
    rsp[uid_idx] = unit;
    rsp[uid_idx + 1u] = 0;
    rsp[uid_idx + 2u] = 0;
    rsp[uid_idx + 3u] =
        (uint16_t)((drv->model << RQ_UIDD_V_MOD) | (RQ_UID_DISK << RQ_UIDD_V_CLS));
    rq_put_u32(rsp, med_idx, drv->media);
}

static void rq_fill_online_common(uint16_t *rsp, const rq_unit_t *u, uint16_t unit)
{
    const rq_drive_t *drv = rq_drive_for_unit(u);

    rsp[RQ_ONL_MLUN] = unit;
    rsp[RQ_ONL_UFL] = rq_current_unit_flags(u);
    rsp[RQ_ONL_RSVL] = 0;
    rsp[RQ_ONL_RSVH] = 0;
    rq_fill_unit_id(rsp, RQ_ONL_UIDA, RQ_ONL_MEDL, unit, drv);
    rsp[RQ_ONL_SHUN] = unit;
    rsp[RQ_ONL_SHST] = 0;
}

static void rq_cmd_gcs(const uint16_t *cmd, uint16_t *rsp)
{
    rq_prepare_rsp(rsp, cmd, RQ_OP_GCS, 0, RQ_ST_SUC, RQ_GCS_LNT);
    rsp[RQ_GCS_REFL] = 0;
    rsp[RQ_GCS_REFH] = 0;
    rsp[RQ_GCS_STSL] = 0;
    rsp[RQ_GCS_STSH] = 0;
}

static void rq_cmd_scc(const uint16_t *cmd, uint16_t *rsp)
{
    if (cmd[RQ_SCC_MSV] != 0u) {
        rq_prepare_rsp(rsp, cmd, RQ_OP_SCC, 0, (uint16_t)(RQ_ST_CMD | RQ_I_VRSN),
                       RQ_RSP_LNT);
        rq_rsp_flags_from_status(rsp);
        return;
    }

    rq_cflgs = (uint16_t)((rq_cflgs & RQ_CF_RPL) | cmd[RQ_SCC_CFL]);
    rq_prepare_rsp(rsp, cmd, RQ_OP_SCC, 0, RQ_ST_SUC, RQ_SCC_LNT);
    rsp[RQ_SCC_CFL] = rq_cflgs;
    rsp[RQ_SCC_TMO] = 0000170u;
    rsp[RQ_SCC_VER] =
        (uint16_t)((RQ_HVER << RQ_SCC_VER_V_HVER) | (RQ_SVER << RQ_SCC_VER_V_SVER));
    rsp[RQ_SCC_CIDA] = 0;
    rsp[RQ_SCC_CIDB] = 0;
    rsp[RQ_SCC_CIDC] = 0;
    rsp[RQ_SCC_CIDD] =
        (uint16_t)((rq_ctrl_model << RQ_SCC_CIDD_V_MOD) |
                   (RQ_CTRL_CLASS << RQ_SCC_CIDD_V_CLS));
    rsp[RQ_SCC_MBCL] = 0;
    rsp[RQ_SCC_MBCH] = 0;
}

static void rq_cmd_gus(const uint16_t *cmd, uint16_t *rsp)
{
    uint16_t unit = cmd[RQ_CMD_UN];
    rq_unit_t *u = rq_unit_ptr(unit);
    uint16_t status = RQ_ST_SUC;
    const rq_drive_t *drv = rq_drive_for_unit(u);
    uint16_t rbpar = (drv->rcts != 0u) ? 1u : 0u;

    if (cmd[RQ_CMD_MOD] & RQ_MD_NXU) {
        if (unit >= RQ11_MAX_UNITS) {
            unit = 0;
        }
        u = rq_unit_ptr(unit);
        drv = rq_drive_for_unit(u);
        rbpar = (drv->rcts != 0u) ? 1u : 0u;
    }

    if (!u || !u->fp) {
        status = (uint16_t)(RQ_ST_OFL | RQ_SB_OFL_NV);
    } else if (!u->online) {
        status = RQ_ST_AVL;
    }

    rq_prepare_rsp(rsp, cmd, RQ_OP_GUS, 0, status, RQ_GUS_LNT_D);
    rsp[RQ_RSP_UN] = unit;
    rsp[RQ_GUS_MLUN] = unit;
    rsp[RQ_GUS_UFL] = rq_current_unit_flags(u);
    rsp[RQ_GUS_RSVL] = 0;
    rsp[RQ_GUS_RSVH] = 0;
    rq_fill_unit_id(rsp, RQ_GUS_UIDA, RQ_GUS_MEDL, unit, drv);
    rsp[RQ_GUS_SHUN] = unit;
    rsp[RQ_GUS_SHST] = 0;
    rsp[RQ_GUS_TRK] = drv->sect;
    rsp[RQ_GUS_GRP] = drv->tpg;
    rsp[RQ_GUS_CYL] = drv->gpc;
    rsp[RQ_GUS_UVER] = 0;
    rsp[RQ_GUS_RCTS] = drv->rcts;
    rsp[RQ_GUS_RBSC] =
        (uint16_t)((rbpar << RQ_GUS_RB_V_RBNS) | (rbpar << RQ_GUS_RB_V_RCTC));
    rq_rsp_flags_from_status(rsp);
}

static void rq_cmd_avl(const uint16_t *cmd, uint16_t *rsp)
{
    uint16_t unit = cmd[RQ_CMD_UN];
    rq_unit_t *u = rq_unit_ptr(unit);
    uint16_t status = RQ_ST_SUC;

    if (!u || !u->fp) {
        status = (uint16_t)(RQ_ST_OFL | RQ_SB_OFL_NV);
    } else {
        u->online = 0;
        u->sw_write_protect = 0;
    }

    rq_prepare_rsp(rsp, cmd, RQ_OP_AVL, 0, status, RQ_RSP_LNT);
    rq_rsp_flags_from_status(rsp);
}

static void rq_cmd_onl(const uint16_t *cmd, uint16_t *rsp)
{
    uint16_t unit = cmd[RQ_CMD_UN];
    rq_unit_t *u = rq_unit_ptr(unit);
    uint16_t status = RQ_ST_SUC;

    if (!u || !u->fp) {
        status = (uint16_t)(RQ_ST_OFL | RQ_SB_OFL_NV);
    }

    rq_prepare_rsp(rsp, cmd, RQ_OP_ONL, 0, status, RQ_ONL_LNT);
    if (u) {
        if (status == RQ_ST_SUC) {
            if (u->online) {
                status = (uint16_t)(status | RQ_SB_SUC_ON);
                rsp[RQ_RSP_STS] = status;
            } else {
                u->online = 1;
                rq_apply_onl_flags(cmd, u);
            }
        }
        rq_fill_online_common(rsp, u, unit);
        rq_put_u32(rsp, RQ_ONL_SIZL, u->blocks);
        rq_put_u32(rsp, RQ_ONL_VSNL, (uint32_t)(01234u + unit));
    }
    rq_rsp_flags_from_status(rsp);
}

static void rq_cmd_suc(const uint16_t *cmd, uint16_t *rsp)
{
    uint16_t unit = cmd[RQ_CMD_UN];
    rq_unit_t *u = rq_unit_ptr(unit);
    uint16_t status = RQ_ST_SUC;

    if (!u || !u->fp) {
        status = (uint16_t)(RQ_ST_OFL | RQ_SB_OFL_NV);
    } else {
        rq_apply_onl_flags(cmd, u);
    }

    rq_prepare_rsp(rsp, cmd, RQ_OP_SUC, 0, status, RQ_ONL_LNT);
    if (u) {
        rq_fill_online_common(rsp, u, unit);
        rq_put_u32(rsp, RQ_ONL_SIZL, u->blocks);
        rq_put_u32(rsp, RQ_ONL_VSNL, (uint32_t)(01234u + unit));
    }
    rq_rsp_flags_from_status(rsp);
}

static int rq_read_block(emu_file_t *f, uint32_t lbn, uint8_t *buf)
{
    uint32_t off = lbn * RQ_NUMBY;
    size_t got;

    if (!f || !buf) {
        return -1;
    }
    if (emu_fseek(f, (long)off, EMU_SEEK_SET) != 0) {
        return -1;
    }
    got = emu_fread(buf, 1, RQ_NUMBY, f);
    if (got == RQ_NUMBY) {
        return 0;
    }
    if (got < RQ_NUMBY && emu_feof(f)) {
        memset(buf + got, 0, RQ_NUMBY - got);
        emu_clearerr(f);
        return 0;
    }
    return -1;
}

static int rq_write_block(emu_file_t *f, uint32_t lbn, const uint8_t *buf)
{
    uint32_t off = lbn * RQ_NUMBY;
    if (!f || !buf) {
        return -1;
    }
    if (emu_fseek(f, (long)off, EMU_SEEK_SET) != 0) {
        return -1;
    }
    return (emu_fwrite(buf, 1, RQ_NUMBY, f) == RQ_NUMBY) ? 0 : -1;
}

static void rq_cmd_rw_common(const uint16_t *cmd, uint16_t *rsp, uint16_t op)
{
    uint16_t unit = cmd[RQ_CMD_UN];
    rq_unit_t *u = rq_unit_ptr(unit);
    const rq_drive_t *drv = rq_drive_for_unit(u);
    uint32_t bc = rq_get_u32(cmd, RQ_RW_BCL);
    uint32_t ba = rq_get_u32(cmd, RQ_RW_BAL);
    uint32_t map = rq_get_u32(cmd, RQ_RW_MAPL);
    uint32_t lbn = rq_get_u32(cmd, RQ_RW_LBNL);
    uint32_t blocks;
    uint32_t done_bytes = 0;
    uint32_t remaining;
    uint16_t status = RQ_ST_SUC;
    uint8_t buf[RQ_NUMBY];

    rq_log("rw op=%03o unit=%o bc=%u ba=%011o map=%011o lbn=%u",
           (unsigned)op, (unsigned)unit, (unsigned)bc, (unsigned)ba,
           (unsigned)map, (unsigned)lbn);

    rq_prepare_rsp(rsp, cmd, op, 0, 0, RQ_RW_LNT_D);
    rq_put_u32(rsp, RQ_RW_BCL, 0);
    rq_put_u32(rsp, RQ_RW_LBNL, lbn);

    if (!u || !u->fp) {
        status = (uint16_t)(RQ_ST_OFL | RQ_SB_OFL_NV);
        goto finish;
    }
    if (!u->online) {
        status = RQ_ST_AVL;
        goto finish;
    }
    if ((ba & 1u) != 0u) {
        status = (uint16_t)(RQ_ST_HST | RQ_SB_HST_OA);
        goto finish;
    }
    if ((bc & 1u) != 0u) {
        status = (uint16_t)(RQ_ST_HST | RQ_SB_HST_OC);
        goto finish;
    }
    if ((bc & 0xF0000000u) != 0u) {
        status = (uint16_t)(RQ_ST_CMD | RQ_I_BCNT);
        goto finish;
    }
    if (bc == 0u) {
        status = RQ_ST_SUC;
        goto finish;
    }
    blocks = (bc + (RQ_NUMBY - 1u)) / RQ_NUMBY;
    if (lbn >= u->blocks) {
        if (lbn >= u->blocks + drv->rcts) {
            status = (uint16_t)(RQ_ST_CMD | RQ_I_LBN);
            goto finish;
        }
        if (bc != RQ_NUMBY) {
            status = (uint16_t)(RQ_ST_CMD | RQ_I_BCNT);
            goto finish;
        }
    } else if (lbn + blocks > u->blocks) {
        status = (uint16_t)(RQ_ST_CMD | RQ_I_BCNT);
        goto finish;
    }
    if (op == RQ_OP_WR) {
        if (lbn >= u->blocks) {
            status = (uint16_t)(RQ_ST_CMD | RQ_I_LBN);
            goto finish;
        }
        if (u->sw_write_protect) {
            status = (uint16_t)(RQ_ST_WPR | RQ_SB_WPR_SW);
            goto finish;
        }
        if (u->read_only) {
            status = (uint16_t)(RQ_ST_WPR | RQ_SB_WPR_HW);
            goto finish;
        }
    }

    remaining = bc;
    for (uint32_t i = 0; i < blocks; i++) {
        uint32_t chunk = (remaining < RQ_NUMBY) ? remaining : RQ_NUMBY;
        if (op == RQ_OP_RD || op == RQ_OP_CMP) {
            if (rq_read_block(u->fp, lbn + i, buf) != 0) {
                status = RQ_ST_SXC;
                rq_log("rw err sxc unit=%o lbn=%u", (unsigned)unit,
                       (unsigned)(lbn + i));
                break;
            }
            if (op == RQ_OP_RD) {
                if (rq_dma_write_bytes(ba + done_bytes, buf, chunk) != 0) {
                    status = (uint16_t)(RQ_ST_HST | RQ_SB_HST_NXM);
                    rq_log("rw err nxm wr unit=%o lbn=%u ba=%011o map=%011o",
                           (unsigned)unit, (unsigned)(lbn + i),
                           (unsigned)(ba + done_bytes), (unsigned)map);
                    break;
                }
                if (rq_trace && rq_verify_left) {
                    uint8_t host_buf[RQ_NUMBY];
                    if (rq_dma_read_bytes(ba + done_bytes, host_buf, chunk) == 0 &&
                            memcmp(host_buf, buf, chunk) != 0) {
                        rq_log("dma verify mismatch ba=%011o lbn=%u",
                               (unsigned)(ba + done_bytes), (unsigned)(lbn + i));
                    }
                    rq_verify_left--;
                }
            } else {
                uint8_t host_buf[RQ_NUMBY];
                if (rq_dma_read_bytes(ba + done_bytes, host_buf, chunk) != 0) {
                    status = (uint16_t)(RQ_ST_HST | RQ_SB_HST_NXM);
                    rq_log("rw err nxm cmp unit=%o lbn=%u ba=%011o map=%011o",
                           (unsigned)unit, (unsigned)(lbn + i),
                           (unsigned)(ba + done_bytes), (unsigned)map);
                    break;
                }
                if (memcmp(host_buf, buf, chunk) != 0) {
                    status = RQ_ST_CMP;
                    rq_log("rw err cmp unit=%o lbn=%u ba=%011o map=%011o",
                           (unsigned)unit, (unsigned)(lbn + i),
                           (unsigned)(ba + done_bytes), (unsigned)map);
                    break;
                }
            }
        } else if (op == RQ_OP_WR) {
            if (chunk < RQ_NUMBY) {
                memset(buf, 0, sizeof(buf));
            }
            if (rq_dma_read_bytes(ba + done_bytes, buf, chunk) != 0) {
                status = (uint16_t)(RQ_ST_HST | RQ_SB_HST_NXM);
                rq_log("rw err nxm rd unit=%o lbn=%u ba=%011o map=%011o",
                       (unsigned)unit, (unsigned)(lbn + i),
                       (unsigned)(ba + done_bytes), (unsigned)map);
                break;
            }
            if (rq_write_block(u->fp, lbn + i, buf) != 0) {
                status = RQ_ST_SXC;
                rq_log("rw err sxc unit=%o lbn=%u", (unsigned)unit,
                       (unsigned)(lbn + i));
                break;
            }
        }
        done_bytes += chunk;
        remaining -= chunk;
    }

finish:
    if (status != RQ_ST_SUC) {
        rq_log("rw fail op=%03o unit=%o sts=%06o bc=%u ba=%011o map=%011o lbn=%u done=%u",
               (unsigned)op, (unsigned)unit, (unsigned)status,
               (unsigned)bc, (unsigned)ba, (unsigned)map, (unsigned)lbn,
               (unsigned)done_bytes);
    }
    rq_put_u32(rsp, RQ_RW_BCL, done_bytes);
    rq_put_u32(rsp, RQ_RW_LBNL, lbn);
    rsp[RQ_RSP_STS] = status;
    rq_rsp_flags_from_status(rsp);
}

static void rq_cmd_access(const uint16_t *cmd, uint16_t *rsp)
{
    rq_prepare_rsp(rsp, cmd, RQ_OP_ACC, 0, RQ_ST_SUC, RQ_RSP_LNT);
    rq_rsp_flags_from_status(rsp);
}

static void rq_prepare_una(uint16_t unit, uint16_t *rsp)
{
    rq_unit_t *u = rq_unit_ptr(unit);
    const rq_drive_t *drv = rq_drive_for_unit(u);

    memset(rsp, 0, RQ_PKT_WORDS * sizeof(uint16_t));
    rsp[RQ_HLNT] = RQ_UNA_LNT;
    rsp[RQ_HCTC] = rq_build_hctc(RQ_OP_AVA);
    rsp[RQ_RSP_REFL] = 0;
    rsp[RQ_RSP_REFH] = 0;
    rsp[RQ_RSP_UN] = unit;
    rsp[RQ_RSP_OPF] = RQ_OP_AVA;
    rsp[RQ_RSP_STS] = RQ_ST_SUC;
    if (u) {
        rsp[RQ_GUS_UFL] = rq_current_unit_flags(u);
        rq_fill_unit_id(rsp, RQ_GUS_UIDA, RQ_GUS_MEDL, unit, drv);
    }
}

static int rq_post_attention_una(void)
{
    uint32_t rsp_desc;
    uint32_t rsp_slot;
    uint32_t rsp_addr;
    uint16_t rsp[RQ_PKT_WORDS];
    uint16_t unit;

    if ((rq_cflgs & RQ_CF_ATN) == 0u) {
        return 0;
    }

    for (unit = 0; unit < RQ11_MAX_UNITS; unit++) {
        if (rq_units[unit].una_pending) {
            break;
        }
    }
    if (unit >= RQ11_MAX_UNITS) {
        return 0;
    }

    if (rq_ring_peek_owned(&rq_rsp_ring, &rsp_desc, &rsp_slot) <= 0) {
        return 0;
    }
    rsp_addr = rsp_desc & RQ_DESC_ADDR;
    if (rsp_addr < 4u) {
        rq_fail(0000010u);
        return -1;
    }

    rq_prepare_una(unit, rsp);
    rq_apply_credits(rsp);
    if (rq_dma_write_words((paddr_t)(rsp_addr + RQ_HDR_OFF), rsp,
                           ((uint32_t)rsp[RQ_HLNT] + 4u) / 2u) != 0) {
        rq_fail(0000002u);
        return -1;
    }
    if (rq_release_desc(&rq_rsp_ring, rsp_slot, rsp_desc) != 0) {
        rq_fail(0000010u);
        return -1;
    }

    rq_units[unit].una_pending = 0;
    rq_raise_irq();
    return 1;
}

static void rq_process_command(const uint16_t *cmd, uint16_t *rsp)
{
    uint16_t op = (uint16_t)(cmd[RQ_CMD_OPC] & 0x00FFu);

    rq_log("cmd op=%03o mod=%06o ref=%06o%06o unit=%o", (unsigned)op,
           (unsigned)cmd[RQ_CMD_MOD], (unsigned)cmd[RQ_CMD_REFH],
           (unsigned)cmd[RQ_CMD_REFL], (unsigned)cmd[RQ_CMD_UN]);

    switch (op) {
    case RQ_OP_GCS:
        rq_cmd_gcs(cmd, rsp);
        break;
    case RQ_OP_SCC:
        rq_cmd_scc(cmd, rsp);
        break;
    case RQ_OP_GUS:
        rq_cmd_gus(cmd, rsp);
        break;
    case RQ_OP_AVL:
        rq_cmd_avl(cmd, rsp);
        break;
    case RQ_OP_ONL:
        rq_cmd_onl(cmd, rsp);
        break;
    case RQ_OP_SUC:
        rq_cmd_suc(cmd, rsp);
        break;
    case RQ_OP_ACC:
        rq_cmd_access(cmd, rsp);
        break;
    case RQ_OP_CMP:
        rq_cmd_rw_common(cmd, rsp, RQ_OP_CMP);
        break;
    case RQ_OP_RD:
        rq_cmd_rw_common(cmd, rsp, RQ_OP_RD);
        break;
    case RQ_OP_WR:
        rq_cmd_rw_common(cmd, rsp, RQ_OP_WR);
        break;
    default:
        rq_prepare_rsp(rsp, cmd, op, 0, (uint16_t)(RQ_ST_CMD | RQ_I_OPCD),
                       RQ_RSP_LNT);
        rq_rsp_flags_from_status(rsp);
        break;
    }
}

static int rq_process_one(void)
{
    uint32_t cmd_desc;
    uint32_t rsp_desc;
    uint32_t cmd_slot;
    uint32_t rsp_slot;
    uint32_t cmd_addr;
    uint32_t rsp_addr;
    uint16_t cmd[RQ_PKT_WORDS];
    uint16_t rsp[RQ_PKT_WORDS];
    uint32_t rsp_bytes;
    int rc;

    rc = rq_ring_peek_owned(&rq_cmd_ring, &cmd_desc, &cmd_slot);
    if (rc <= 0) {
        return rc;
    }
    rc = rq_ring_peek_owned(&rq_rsp_ring, &rsp_desc, &rsp_slot);
    if (rc <= 0) {
        return rc;
    }

    cmd_addr = cmd_desc & RQ_DESC_ADDR;
    rsp_addr = rsp_desc & RQ_DESC_ADDR;
    if (cmd_addr < 4u || rsp_addr < 4u) {
        rq_fail(0000010u);
        return -1;
    }
    if (rq_trace && rq_log_pkt_addr < 4u) {
        rq_log("pkt addr cmd=%07o rsp=%07o desc=%010o/%010o",
               (unsigned)cmd_addr, (unsigned)rsp_addr,
               (unsigned)cmd_desc, (unsigned)rsp_desc);
        rq_log_pkt_addr++;
    }

    if (rq_dma_read_words((paddr_t)(cmd_addr + RQ_HDR_OFF), cmd, RQ_PKT_WORDS) != 0) {
        rq_fail(0000001u);
        return -1;
    }

    rq_process_command(cmd, rsp);
    rq_log("rsp op=%03o sts=%06o len=%u",
           (unsigned)(rsp[RQ_RSP_OPF] & 0x00FFu),
           (unsigned)rsp[RQ_RSP_STS],
           (unsigned)rsp[RQ_HLNT]);
    if (rq_trace && !rq_log_onl_rsp &&
            ((rsp[RQ_RSP_OPF] & 0x00FFu) == (RQ_OP_ONL | RQ_OP_END))) {
        rq_log("onl rsp hctc=%06o opf=%06o sts=%06o ufl=%06o uid=%06o/%06o/%06o/%06o"
               " med=%06o/%06o siz=%u",
               (unsigned)rsp[RQ_HCTC], (unsigned)rsp[RQ_RSP_OPF],
               (unsigned)rsp[RQ_RSP_STS], (unsigned)rsp[RQ_ONL_UFL],
               (unsigned)rsp[RQ_ONL_UIDA], (unsigned)rsp[RQ_ONL_UIDB],
               (unsigned)rsp[RQ_ONL_UIDC], (unsigned)rsp[RQ_ONL_UIDD],
               (unsigned)rsp[RQ_ONL_MEDL], (unsigned)rsp[RQ_ONL_MEDH],
               (unsigned)rq_get_u32(rsp, RQ_ONL_SIZL));
        rq_log_onl_rsp = 1;
    }
    rq_apply_credits(rsp);
    rsp_bytes = (uint32_t)rsp[RQ_HLNT] + 4u;
    if (rq_dma_write_words((paddr_t)(rsp_addr + RQ_HDR_OFF), rsp, rsp_bytes / 2u) !=
            0) {
        rq_fail(0000002u);
        return -1;
    }

    if (rq_release_desc(&rq_cmd_ring, cmd_slot, cmd_desc) != 0) {
        rq_fail(0000010u);
        return -1;
    }
    if (rq_release_desc(&rq_rsp_ring, rsp_slot, rsp_desc) != 0) {
        rq_fail(0000010u);
        return -1;
    }

    rq_raise_irq();
    return 1;
}

static void rq_service_poll(void)
{
    int budget = 8;

    while (budget-- > 0) {
        int rc;

        rc = rq_post_attention_una();
        if (rc > 0) {
            continue;
        }
        if (rc < 0) {
            rq_poll_pending = 0;
            return;
        }

        rc = rq_process_one();
        if (rc <= 0) {
            rq_poll_pending = 0;
            return;
        }
    }
}

static void rq_handle_ip_write(void)
{
    rq_log("ip wr");
    rq11_reset();
}

static void rq_handle_ip_read(void)
{
    rq_log("ip rd state=%o", (unsigned)rq_state);
    if (rq_state == RQ_STATE_S3_PPB) {
        (void)rq_step4();
        return;
    }
    if (rq_state == RQ_STATE_UP) {
        rq_poll_pending = 1;
    }
}

static void rq_handle_sa_write(uint16_t v)
{
    switch (rq_state) {
    case RQ_STATE_S1:
    case RQ_STATE_S1_WRAP:
        if ((v & RQ_S1H_WR) != 0u) {
            rq_sa = v;
            rq_state = RQ_STATE_S1_WRAP;
            rq_log("wrap %06o", (unsigned)v);
            return;
        }
        if ((v & RQ_S1H_VL) == 0u) {
            return;
        }
        rq_s1_host = v;
        rq_irq_en = (v & RQ_S1H_IE) ? 1u : 0u;
        rq_sa = (uint16_t)(RQ_SA_S2 | ((rq_s1_host >> 8) & 0x00FFu));
        rq_state = RQ_STATE_S2;
        rq_log("s1 %06o -> s2 %06o", (unsigned)v, (unsigned)rq_sa);
        rq_raise_irq();
        return;

    case RQ_STATE_S2:
        rq_comm = (uint32_t)(v & RQ_S2H_CLO);
        rq_prgi = (v & RQ_S2H_PI) ? 1u : 0u;
        rq_sa = (uint16_t)(RQ_SA_S3 | (rq_s1_host & 0x00FFu));
        rq_state = RQ_STATE_S3;
        rq_log("s2 commlo=%06o prgi=%u", (unsigned)rq_comm, (unsigned)rq_prgi);
        rq_raise_irq();
        return;

    case RQ_STATE_S3:
        rq_comm |= ((uint32_t)(v & RQ_S3H_CHI) << 16);
        if ((v & RQ_S3H_PP) != 0u) {
            rq_sa = 0;
            rq_state = RQ_STATE_S3_PPA;
            rq_log("s3 purge");
            rq_raise_irq();
            return;
        }
        (void)rq_step4();
        return;

    case RQ_STATE_S3_PPA:
        if (v == 0u) {
            rq_state = RQ_STATE_S3_PPB;
            rq_log("purge ack");
        }
        return;

    case RQ_STATE_S4:
        if ((v & RQ_S4H_GO) != 0u) {
            rq_state = RQ_STATE_UP;
            rq_sa = 0;
            rq_log("online");
        }
        return;

    case RQ_STATE_UP:
    case RQ_STATE_DEAD:
    default:
        return;
    }
}

static uint8_t rq11_read8(uint16_t addr)
{
    switch (addr & 03u) {
    case 0u:
    case 1u:
        rq_handle_ip_read();
        return 0;

    case 2u:
        rq_log("sa rd -> %06o", (unsigned)rq_sa);
        return (uint8_t)(rq_sa & 0x00FFu);

    case 3u:
        return (uint8_t)((rq_sa >> 8) & 0x00FFu);

    default:
        return 0;
    }
}

static void rq11_write8(uint16_t addr, uint8_t v)
{
    switch (addr & 03u) {
    case 0u:
    case 1u:
        (void)v;
        rq_handle_ip_write();
        return;

    case 2u:
        rq_sa_latch = (uint16_t)((rq_sa_latch & 0xFF00u) | (uint16_t)v);
        return;

    case 3u:
        rq_sa_latch = (uint16_t)((rq_sa_latch & 0x00FFu) | ((uint16_t)v << 8));
        rq_log("sa wr %06o", (unsigned)rq_sa_latch);
        rq_handle_sa_write(rq_sa_latch);
        return;

    default:
        return;
    }
}

int rq11_init(void)
{
    static const io_range_t io = {
        RQ_BASE, (uint16_t)(RQ_BASE + 3u), rq11_read8, rq11_write8, "RQ11"
    };
    static const irq_source_t irq = {"RQ11", RQ_VECTOR, RQ_PRIORITY, rq11_irq_pending,
                                     rq11_irq_ack
                                    };

    rq_trace = (getenv("LSI11_RQ_TRACE") != NULL) ? 1u : 0u;
    rq_ubus_mode = (getenv("LSI11_RQ_UBUS") != NULL) ? 1u : 0u;
    rq_ctrl_model = rq_ubus_mode ? RQ_CTRL_MODEL_UBUS : RQ_CTRL_MODEL_QBUS;
    {
        const char *env_model = getenv("LSI11_RQ_MODEL");
        if (env_model && *env_model) {
            long v = strtol(env_model, NULL, 0);
            if (v >= 0 && v <= 255) {
                rq_ctrl_model = (uint16_t)v;
            }
        }
    }
    rq_verify_init = 4;
    {
        const char *env_verify = getenv("LSI11_RQ_VERIFY");
        if (env_verify && *env_verify) {
            long v = strtol(env_verify, NULL, 0);
            if (v >= 0 && v <= 255) {
                rq_verify_init = (uint8_t)v;
            }
        }
    }

    if (devio_register(&io) != 0) {
        return -1;
    }
    if (irq_register(&irq) != 0) {
        return -1;
    }

    rq11_reset();
    return 0;
}

void rq11_reset(void)
{
    uint16_t unit;

    rq_sa = rq_initial_sa();
    rq_sa_latch = 0;
    rq_s1_host = 0;
    rq_comm = 0;
    rq_prgi = 0;
    rq_state = RQ_STATE_S1;
    rq_irq_req = 0;
    rq_irq_en = 0;
    rq_poll_pending = 0;
    rq_cflgs = RQ_CF_RPL;
    rq_credits = 7u;
    rq_log_onl_rsp = 0;
    rq_verify_left = rq_verify_init;
    rq_log_pkt_addr = 0;
    for (unit = 0; unit < RQ11_MAX_UNITS; unit++) {
        rq_units[unit].online = 0;
        rq_units[unit].sw_write_protect = 0;
        rq_units[unit].una_pending = 0;
    }
    rq_ring_reset(&rq_cmd_ring);
    rq_ring_reset(&rq_rsp_ring);
    rq_log("reset sa=%06o", (unsigned)rq_sa);
}

void rq11_poll(void)
{
    if (rq_state != RQ_STATE_UP) {
        return;
    }
    if (!rq_poll_pending) {
        return;
    }
    rq_service_poll();
}

int rq11_open_image_unit(unsigned unit, const char *path)
{
    rq_unit_t *u;
    long end_pos = 0;

    if (!path || unit >= RQ11_MAX_UNITS) {
        return -1;
    }

    u = &rq_units[unit];
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

    if (emu_fseek(u->fp, 0, EMU_SEEK_END) != 0) {
        emu_fclose(u->fp);
        u->fp = NULL;
        return -1;
    }
    end_pos = emu_ftell(u->fp);
    if (end_pos < 0) {
        emu_fclose(u->fp);
        u->fp = NULL;
        return -1;
    }
    u->file_blocks = (uint32_t)((uint64_t)end_pos / RQ_NUMBY);
    u->drive = rq_pick_drive(u->file_blocks);
    u->blocks = u->drive->lbn;
    (void)emu_fseek(u->fp, 0, EMU_SEEK_SET);

    u->online = 0;
    u->sw_write_protect = 0;
    u->una_pending = 1;
    rq_log("attach unit=%o blocks=%u file=%u type=%s ro=%u",
           (unsigned)unit, (unsigned)u->blocks, (unsigned)u->file_blocks,
           u->drive ? u->drive->name : "unknown", (unsigned)u->read_only);
    rq_poll_pending = 1;
    return 0;
}

int rq11_open_image(const char *path)
{
    return rq11_open_image_unit(0, path);
}

void rq11_close_image(void)
{
    uint16_t unit;

    for (unit = 0; unit < RQ11_MAX_UNITS; unit++) {
        rq_unit_t *u = &rq_units[unit];
        if (u->fp) {
            emu_fclose(u->fp);
            u->fp = NULL;
        }
        u->read_only = 0;
        u->drive = NULL;
        u->file_blocks = 0;
        u->blocks = 0;
        u->online = 0;
        u->sw_write_protect = 0;
        u->una_pending = 0;
    }
    rq_poll_pending = 0;
}

int rq11_irq_pending(void)
{
    return rq_irq_pending();
}

void rq11_irq_ack(void)
{
    rq_irq_ack();
}
