#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../bus.h"
#include "../irq.h"
#include "../tq11.h"

#define TQ_IP            0174500u
#define TQ_SA            0174502u
#define TQ_VECTOR        0000260u

#define TQ_SA_S1         0x0800u
#define TQ_SA_S4         0x4000u
#define TQ_SA_S3         0x2000u
#define TQ_SA_S2         0x1000u
#define TQ_S1C_Q22       0x0200u
#define TQ_S1C_DI        0x0100u
#define TQ_S1C_MP        0x0040u

#define TQ_S1H_VL        0x8000u
#define TQ_S1H_IE        0x0080u

#define TQ_S3H_PP        0x8000u
#define TQ_S4H_GO        0x0001u

#define TQ_DESC_OWN      0x80000000u

#define TQ_HDR_OFF       (-4)
#define TQ_HLNT          0u
#define TQ_HCTC          1u
#define TQ_HCTC_SEQ_TMSCP 0x0100u

#define TQ_CMD_REFL      2u
#define TQ_CMD_REFH      3u
#define TQ_CMD_UN        4u
#define TQ_CMD_OPC       6u
#define TQ_CMD_MOD       7u

#define TQ_RSP_OPF       6u
#define TQ_RSP_STS       7u

#define TQ_SCC_LNT       32u
#define TQ_SCC_CFL       9u
#define TQ_GCS_LNT       20u
#define TQ_GUS_LNT       44u
#define TQ_ONL_LNT       44u
#define TQ_FLU_LNT       32u
#define TQ_SUC_LNT       44u
#define TQ_POS_LNT       32u
#define TQ_RW_LNT        36u
#define TQ_WTM_LNT       32u

#define TQ_ONL_UFL       9u

#define TQ_GCS_STSL      10u
#define TQ_GCS_STSH      11u

#define TQ_POS_RCL       8u
#define TQ_POS_RCH       9u
#define TQ_POS_TMCL      10u
#define TQ_POS_TMCH      11u

#define TQ_RW_BCL        8u
#define TQ_RW_BCH        9u
#define TQ_RW_BAL        10u
#define TQ_RW_BAH        11u
#define TQ_RW_RSZL       18u
#define TQ_RW_RSZH       19u

#define TQ_OP_GCS        0002u
#define TQ_OP_GUS        0003u
#define TQ_OP_SCC        0004u
#define TQ_OP_ONL        0011u
#define TQ_OP_SUC        0012u
#define TQ_OP_ACC        0020u
#define TQ_OP_FLU        0023u
#define TQ_OP_CMP        0040u
#define TQ_OP_RD         0041u
#define TQ_OP_WR         0042u
#define TQ_OP_WTM        0044u
#define TQ_OP_POS        0045u

#define TQ_MD_REV        0x0008u
#define TQ_MD_RWD        0x0002u

#define TQ_ST_SUC        0u
#define TQ_ST_AVL        4u
#define TQ_ST_TMK        14u

#define TQ_CF_ATN        0x0080u

#define COMM_BASE        0000400u
#define RSP_DESC_ADDR    COMM_BASE
#define CMD_DESC_ADDR    (COMM_BASE + 4u)
#define CI_ADDR          (COMM_BASE - 4u)
#define RI_ADDR          (COMM_BASE - 2u)

#define CMD_PKT_PAYLOAD  0001204u
#define RSP_PKT_PAYLOAD  0001404u

#define HOST_BUF_A       0002000u
#define HOST_BUF_B       0002020u

static int g_fail = 0;
static uint16_t g_ref = 1;
static regs g_regs;

static void check(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        g_fail = 1;
    }
}

static void put_u32(uint16_t addr, uint32_t v)
{
    bus_write16(addr, (uint16_t)(v & 0xFFFFu));
    bus_write16((uint16_t)(addr + 2u), (uint16_t)((v >> 16) & 0xFFFFu));
}

static void pkt_put_u32(uint16_t *pkt, unsigned lo_idx, uint32_t v)
{
    pkt[lo_idx] = (uint16_t)(v & 0xFFFFu);
    pkt[lo_idx + 1u] = (uint16_t)((v >> 16) & 0xFFFFu);
}

static uint32_t pkt_get_u32(const uint16_t *pkt, unsigned lo_idx)
{
    return (uint32_t)pkt[lo_idx] | ((uint32_t)pkt[lo_idx + 1u] << 16);
}

static void write_packet(uint16_t payload_addr, const uint16_t *pkt)
{
    size_t i;
    for (i = 0; i < 32u; i++) {
        bus_write16((uint16_t)(payload_addr + TQ_HDR_OFF + (uint16_t)(i * 2u)),
                    pkt[i]);
    }
}

static void read_packet(uint16_t payload_addr, uint16_t *pkt)
{
    size_t i;
    for (i = 0; i < 32u; i++) {
        pkt[i] = bus_read16((uint16_t)(payload_addr + TQ_HDR_OFF +
                                       (uint16_t)(i * 2u)));
    }
}

static void bootstrap_controller(void)
{
    uint16_t s1 = (uint16_t)(TQ_S1H_VL | TQ_S1H_IE);

    check(bus_read16(TQ_SA) == (TQ_SA_S1 | TQ_S1C_Q22 | TQ_S1C_DI | TQ_S1C_MP),
          "TQ11 starts with QBUS SA capabilities");

    bus_write16(TQ_SA, s1);
    check((bus_read16(TQ_SA) & TQ_SA_S2) != 0, "TQ11 enters step 2");

    bus_write16(TQ_SA, COMM_BASE);
    check((bus_read16(TQ_SA) & TQ_SA_S3) != 0, "TQ11 enters step 3");

    bus_write16(TQ_SA, 0);
    check((bus_read16(TQ_SA) & TQ_SA_S4) != 0, "TQ11 enters step 4");

    bus_write16(TQ_SA, TQ_S4H_GO);
    check(bus_read16(TQ_SA) == 0, "TQ11 enters online state");
}

static void issue_cmd(uint16_t *cmd, uint16_t *rsp)
{
    uint16_t vec = 0;

    cmd[TQ_CMD_REFL] = g_ref++;
    cmd[TQ_CMD_REFH] = 0;
    cmd[TQ_CMD_UN] = 0;

    write_packet(CMD_PKT_PAYLOAD, cmd);

    bus_write16(CI_ADDR, 0);
    bus_write16(RI_ADDR, 0);
    put_u32(RSP_DESC_ADDR, TQ_DESC_OWN | RSP_PKT_PAYLOAD);
    put_u32(CMD_DESC_ADDR, TQ_DESC_OWN | CMD_PKT_PAYLOAD);

    (void)bus_read16(TQ_IP);
    tq11_poll();

    check(irq_poll(&g_regs, &vec) == 1, "TQ11 raises one IRQ");
    check((vec & 0000777u) == TQ_VECTOR, "TQ11 vector is 000260");
    vec = 0;
    check(irq_poll(&g_regs, &vec) == 0, "TQ11 IRQ does not repeat");

    check(bus_read16(CI_ADDR) == 000001u, "TQ11 sets CI flag");
    check(bus_read16(RI_ADDR) == 000001u, "TQ11 sets RI flag");

    read_packet(RSP_PKT_PAYLOAD, rsp);
    check(rsp[TQ_HCTC] == TQ_HCTC_SEQ_TMSCP,
          "TQ11 response HCTC is SEQ/TMSCP with 0 credits");
}

static void expect_attention(uint16_t *rsp, uint16_t op)
{
    uint16_t vec = 0;

    bus_write16(RI_ADDR, 0);
    put_u32(RSP_DESC_ADDR, TQ_DESC_OWN | RSP_PKT_PAYLOAD);
    (void)bus_read16(TQ_IP);
    tq11_poll();

    check(irq_poll(&g_regs, &vec) == 1, "TQ11 raises IRQ for attention");
    check((vec & 0000777u) == TQ_VECTOR, "TQ11 attention vector is 000260");
    check(bus_read16(RI_ADDR) == 000001u, "TQ11 sets RI for attention");
    read_packet(RSP_PKT_PAYLOAD, rsp);
    check(rsp[TQ_HCTC] == TQ_HCTC_SEQ_TMSCP,
          "TQ11 attention HCTC is SEQ/TMSCP with 0 credits");
    check((rsp[TQ_RSP_OPF] & 0x00FFu) == op, "TQ11 attention opcode matches");
    check(rsp[TQ_RSP_STS] == 0u, "TQ11 attention status is zero");
}

int main(void)
{
    char err[128];
    char img[] = "/tmp/tq11-tap-XXXXXX";
    int fd;
    uint16_t cmd[32];
    uint16_t rsp[32];
    static const uint8_t rec[] = {'A', 'B', 'C'};

    memset(&g_regs, 0, sizeof(g_regs));
    g_regs.model = K1801VM2;
    g_regs.psw = 0;

    fd = mkstemp(img);
    if (fd < 0) {
        fprintf(stderr, "FAIL: mkstemp\n");
        return 1;
    }
    close(fd);

    bus_reset_config();
    if (bus_configure(BUS_MACHINE_LSI11_1104, 0, err, sizeof(err)) != 0) {
        fprintf(stderr, "FAIL: bus_configure: %s\n", err);
        unlink(img);
        return 1;
    }
    bus_init();

    if (tq11_init() != 0) {
        fprintf(stderr, "FAIL: tq11_init\n");
        unlink(img);
        return 1;
    }
    tq11_reset();

    if (tq11_open_image(img) != 0) {
        fprintf(stderr, "FAIL: tq11_open_image\n");
        unlink(img);
        return 1;
    }

    bootstrap_controller();

    memset(cmd, 0, sizeof(cmd));
    cmd[TQ_HLNT] = TQ_SCC_LNT;
    cmd[TQ_CMD_OPC] = TQ_OP_SCC;
    cmd[TQ_SCC_CFL] = TQ_CF_ATN;
    issue_cmd(cmd, rsp);
    check(rsp[TQ_RSP_STS] == TQ_ST_SUC, "TQ11 SCC succeeds");
    expect_attention(rsp, 0100u);

    memset(cmd, 0, sizeof(cmd));
    cmd[TQ_HLNT] = TQ_GUS_LNT;
    cmd[TQ_CMD_OPC] = TQ_OP_GUS;
    issue_cmd(cmd, rsp);
    check(rsp[TQ_RSP_STS] == TQ_ST_AVL, "TQ11 GUS reports available before online");

    memset(cmd, 0, sizeof(cmd));
    cmd[TQ_HLNT] = TQ_ONL_LNT;
    cmd[TQ_CMD_OPC] = TQ_OP_ONL;
    cmd[TQ_ONL_UFL] = 0;
    issue_cmd(cmd, rsp);
    check(rsp[TQ_RSP_STS] == TQ_ST_SUC, "TQ11 ONL succeeds");

    memset(cmd, 0, sizeof(cmd));
    cmd[TQ_HLNT] = TQ_GCS_LNT;
    cmd[TQ_CMD_OPC] = TQ_OP_GCS;
    issue_cmd(cmd, rsp);
    check(rsp[TQ_RSP_STS] == TQ_ST_SUC, "TQ11 GCS succeeds");
    check(rsp[TQ_GCS_STSL] == 0u, "TQ11 GCS low status is zero");
    check(rsp[TQ_GCS_STSH] == 0u, "TQ11 GCS high status is zero");

    memset(cmd, 0, sizeof(cmd));
    cmd[TQ_HLNT] = TQ_SUC_LNT;
    cmd[TQ_CMD_OPC] = TQ_OP_SUC;
    cmd[TQ_ONL_UFL] = 0;
    issue_cmd(cmd, rsp);
    check(rsp[TQ_RSP_STS] == TQ_ST_SUC, "TQ11 SUC succeeds");

    memset(cmd, 0, sizeof(cmd));
    cmd[TQ_HLNT] = TQ_FLU_LNT;
    cmd[TQ_CMD_OPC] = TQ_OP_FLU;
    issue_cmd(cmd, rsp);
    check(rsp[TQ_RSP_STS] == TQ_ST_SUC, "TQ11 FLU succeeds");

    bus_write8(HOST_BUF_A + 0u, rec[0]);
    bus_write8(HOST_BUF_A + 1u, rec[1]);
    bus_write8(HOST_BUF_A + 2u, rec[2]);

    memset(cmd, 0, sizeof(cmd));
    cmd[TQ_HLNT] = TQ_RW_LNT;
    cmd[TQ_CMD_OPC] = TQ_OP_WR;
    pkt_put_u32(cmd, TQ_RW_BCL, (uint32_t)sizeof(rec));
    pkt_put_u32(cmd, TQ_RW_BAL, HOST_BUF_A);
    issue_cmd(cmd, rsp);
    check(rsp[TQ_RSP_STS] == TQ_ST_SUC, "TQ11 WR succeeds");
    check(pkt_get_u32(rsp, TQ_RW_BCL) == sizeof(rec), "TQ11 WR reports byte count");

    memset(cmd, 0, sizeof(cmd));
    cmd[TQ_HLNT] = TQ_WTM_LNT;
    cmd[TQ_CMD_OPC] = TQ_OP_WTM;
    issue_cmd(cmd, rsp);
    check(rsp[TQ_RSP_STS] == TQ_ST_SUC, "TQ11 WTM succeeds");

    memset(cmd, 0, sizeof(cmd));
    cmd[TQ_HLNT] = TQ_POS_LNT;
    cmd[TQ_CMD_OPC] = TQ_OP_POS;
    cmd[TQ_CMD_MOD] = TQ_MD_RWD;
    issue_cmd(cmd, rsp);
    check(rsp[TQ_RSP_STS] == TQ_ST_SUC, "TQ11 rewind succeeds");

    memset(cmd, 0, sizeof(cmd));
    cmd[TQ_HLNT] = TQ_RW_LNT;
    cmd[TQ_CMD_OPC] = TQ_OP_CMP;
    pkt_put_u32(cmd, TQ_RW_BCL, 8u);
    pkt_put_u32(cmd, TQ_RW_BAL, HOST_BUF_A);
    issue_cmd(cmd, rsp);
    check(rsp[TQ_RSP_STS] == TQ_ST_SUC, "TQ11 CMP succeeds");

    memset(cmd, 0, sizeof(cmd));
    cmd[TQ_HLNT] = TQ_POS_LNT;
    cmd[TQ_CMD_OPC] = TQ_OP_POS;
    cmd[TQ_CMD_MOD] = TQ_MD_REV;
    pkt_put_u32(cmd, TQ_POS_RCL, 1u);
    issue_cmd(cmd, rsp);
    check(rsp[TQ_RSP_STS] == TQ_ST_SUC,
          "TQ11 reverse-space record succeeds after compare");

    memset(cmd, 0, sizeof(cmd));
    cmd[TQ_HLNT] = TQ_RW_LNT;
    cmd[TQ_CMD_OPC] = TQ_OP_ACC;
    pkt_put_u32(cmd, TQ_RW_BCL, 8u);
    pkt_put_u32(cmd, TQ_RW_BAL, HOST_BUF_B);
    issue_cmd(cmd, rsp);
    check(rsp[TQ_RSP_STS] == TQ_ST_SUC, "TQ11 ACC succeeds");
    check(pkt_get_u32(rsp, TQ_RW_BCL) == sizeof(rec), "TQ11 ACC reports byte count");

    memset(cmd, 0, sizeof(cmd));
    cmd[TQ_HLNT] = TQ_POS_LNT;
    cmd[TQ_CMD_OPC] = TQ_OP_POS;
    cmd[TQ_CMD_MOD] = TQ_MD_RWD;
    issue_cmd(cmd, rsp);
    check(rsp[TQ_RSP_STS] == TQ_ST_SUC, "TQ11 rewind succeeds before read");

    memset(cmd, 0, sizeof(cmd));
    cmd[TQ_HLNT] = TQ_RW_LNT;
    cmd[TQ_CMD_OPC] = TQ_OP_RD;
    pkt_put_u32(cmd, TQ_RW_BCL, 8u);
    pkt_put_u32(cmd, TQ_RW_BAL, HOST_BUF_B);
    issue_cmd(cmd, rsp);
    check(rsp[TQ_RSP_STS] == TQ_ST_SUC, "TQ11 RD succeeds");
    check(pkt_get_u32(rsp, TQ_RW_BCL) == sizeof(rec), "TQ11 RD reports byte count");
    check(pkt_get_u32(rsp, TQ_RW_RSZL) == sizeof(rec), "TQ11 RD reports record size");
    check(bus_read8(HOST_BUF_B + 0u) == rec[0], "TQ11 RD byte 0");
    check(bus_read8(HOST_BUF_B + 1u) == rec[1], "TQ11 RD byte 1");
    check(bus_read8(HOST_BUF_B + 2u) == rec[2], "TQ11 RD byte 2");

    memset(cmd, 0, sizeof(cmd));
    cmd[TQ_HLNT] = TQ_RW_LNT;
    cmd[TQ_CMD_OPC] = TQ_OP_RD;
    pkt_put_u32(cmd, TQ_RW_BCL, 8u);
    pkt_put_u32(cmd, TQ_RW_BAL, HOST_BUF_B);
    issue_cmd(cmd, rsp);
    check(rsp[TQ_RSP_STS] == TQ_ST_TMK, "TQ11 RD sees tape mark");

    memset(cmd, 0, sizeof(cmd));
    cmd[TQ_HLNT] = TQ_POS_LNT;
    cmd[TQ_CMD_OPC] = TQ_OP_POS;
    cmd[TQ_CMD_MOD] = TQ_MD_REV;
    pkt_put_u32(cmd, TQ_POS_TMCL, 1u);
    issue_cmd(cmd, rsp);
    check(rsp[TQ_RSP_STS] == TQ_ST_SUC, "TQ11 reverse-space tape mark succeeds");

    memset(cmd, 0, sizeof(cmd));
    cmd[TQ_HLNT] = TQ_RW_LNT;
    cmd[TQ_CMD_OPC] = TQ_OP_RD;
    pkt_put_u32(cmd, TQ_RW_BCL, 8u);
    pkt_put_u32(cmd, TQ_RW_BAL, HOST_BUF_B);
    issue_cmd(cmd, rsp);
    check(rsp[TQ_RSP_STS] == TQ_ST_TMK, "TQ11 RD sees tape mark again after reverse-space mark");

    memset(cmd, 0, sizeof(cmd));
    cmd[TQ_HLNT] = TQ_POS_LNT;
    cmd[TQ_CMD_OPC] = TQ_OP_POS;
    cmd[TQ_CMD_MOD] = TQ_MD_REV;
    pkt_put_u32(cmd, TQ_POS_TMCL, 1u);
    issue_cmd(cmd, rsp);
    check(rsp[TQ_RSP_STS] == TQ_ST_SUC,
          "TQ11 reverse-space tape mark succeeds again");

    memset(cmd, 0, sizeof(cmd));
    cmd[TQ_HLNT] = TQ_POS_LNT;
    cmd[TQ_CMD_OPC] = TQ_OP_POS;
    cmd[TQ_CMD_MOD] = TQ_MD_REV;
    pkt_put_u32(cmd, TQ_POS_RCL, 1u);
    issue_cmd(cmd, rsp);
    check(rsp[TQ_RSP_STS] == TQ_ST_SUC, "TQ11 reverse-space record succeeds");

    memset(cmd, 0, sizeof(cmd));
    cmd[TQ_HLNT] = TQ_RW_LNT;
    cmd[TQ_CMD_OPC] = TQ_OP_RD;
    pkt_put_u32(cmd, TQ_RW_BCL, 8u);
    pkt_put_u32(cmd, TQ_RW_BAL, HOST_BUF_B);
    issue_cmd(cmd, rsp);
    check(rsp[TQ_RSP_STS] == TQ_ST_SUC, "TQ11 RD succeeds again after reverse-space record");
    check(bus_read8(HOST_BUF_B + 0u) == rec[0], "TQ11 RD byte 0 after reverse-space");

    memset(cmd, 0, sizeof(cmd));
    cmd[TQ_HLNT] = TQ_POS_LNT;
    cmd[TQ_CMD_OPC] = TQ_OP_POS;
    cmd[TQ_CMD_MOD] = TQ_MD_RWD;
    issue_cmd(cmd, rsp);
    check(rsp[TQ_RSP_STS] == TQ_ST_SUC, "TQ11 rewind succeeds again");

    memset(cmd, 0, sizeof(cmd));
    cmd[TQ_HLNT] = TQ_POS_LNT;
    cmd[TQ_CMD_OPC] = TQ_OP_POS;
    pkt_put_u32(cmd, TQ_POS_RCL, 1u);
    issue_cmd(cmd, rsp);
    check(rsp[TQ_RSP_STS] == TQ_ST_SUC, "TQ11 forward-space record succeeds");

    memset(cmd, 0, sizeof(cmd));
    cmd[TQ_HLNT] = TQ_RW_LNT;
    cmd[TQ_CMD_OPC] = TQ_OP_RD;
    pkt_put_u32(cmd, TQ_RW_BCL, 8u);
    pkt_put_u32(cmd, TQ_RW_BAL, HOST_BUF_B);
    issue_cmd(cmd, rsp);
    check(rsp[TQ_RSP_STS] == TQ_ST_TMK, "TQ11 RD sees tape mark after forward-space record");

    tq11_close_image();
    unlink(img);

    if (!g_fail) {
        printf("PASS: test_tq11_tap\n");
        return 0;
    }
    return 1;
}
