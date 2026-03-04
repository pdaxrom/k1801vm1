#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adapter_core.h"
#include "bus.h"
#include "dev_dl11.h"
#include "dev_dz11.h"
#include "dev_kw11.h"
#include "dev_rl11.h"
#include "dev_rh11.h"
#include "dev_rk11.h"
#include "dev_sr.h"
#include "dev_tq11.h"
#include "ubmap.h"
#include "dev_vm1sel.h"
#include "dev_vm1sav.h"

/* core headers (read-only) */
#include "../core/core.h" /* TODO: replace with actual header providing regs + cpu step/run */
#include "../core/disas.h"

#if defined(LSI11_TARGET_1184)
#define LSI11_TARGET_PDP1184 1
#endif

#define VM2_MIN_HALT_RAM_BYTES 0004000u /* 2 KB */
#define VM2_BUSERR_VECTOR      0000004u
#define VM2_HALT_VECTOR        0000170u
#define VM2_HANDLER_ADDR       0000400u
#define VM2_VECTOR_PSW         0000400u

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define RL11_MAX_DRIVES 4
#define VM2_TX_IMM(ch) 0112711u, (uint16_t)(uint8_t)(ch)

enum {
    BOOT_DEV_NONE = 0,
    BOOT_DEV_RK,
    BOOT_DEV_RH,
    BOOT_DEV_RL,
    BOOT_DEV_TQ
};

/*
 * VM2 HALT-bank runtime stub:
 *   MOV  #177566, R1         ; DL11 TBUF
 *   MOVB #'<char>', (R1)     ; emit text
 *   ...
 *   BR   .                   ; stop in place
 */
static const uint16_t vm2_halt_stub_prog[] = {
    0012701u, 0177566u, /* MOV #DL11_TBUF, R1 */

    /* "HALT mode - vector 000004\r\n" */
    VM2_TX_IMM('H'), VM2_TX_IMM('A'), VM2_TX_IMM('L'), VM2_TX_IMM('T'),
    VM2_TX_IMM(' '), VM2_TX_IMM('m'), VM2_TX_IMM('o'), VM2_TX_IMM('d'),
    VM2_TX_IMM('e'), VM2_TX_IMM(' '), VM2_TX_IMM('-'), VM2_TX_IMM(' '),
    VM2_TX_IMM('v'), VM2_TX_IMM('e'), VM2_TX_IMM('c'), VM2_TX_IMM('t'),
    VM2_TX_IMM('o'), VM2_TX_IMM('r'), VM2_TX_IMM(' '), VM2_TX_IMM('0'),
    VM2_TX_IMM('0'), VM2_TX_IMM('0'), VM2_TX_IMM('0'), VM2_TX_IMM('0'),
    VM2_TX_IMM('4'), VM2_TX_IMM('\r'), VM2_TX_IMM('\n'),

    0000777u /* BR . */
};

static int cpu_is_vm2(byte model)
{
    return (model == K1801VM2 || model == K1806VM2) ? 1 : 0;
}

static int vm2_halt_store_word(uint16_t addr, uint16_t v, char *err, size_t err_len)
{
    if (bus_vm2_cpu_is_nxm(addr, 1) ||
            bus_vm2_cpu_is_nxm((uint16_t)(addr + 1), 1)) {
        if (err && err_len) {
            snprintf(err, err_len, "VM2 HALT RAM NXM at %06o", (unsigned)addr);
        }
        return -1;
    }
    bus_vm2_cpu_write16(addr, 1, v);
    return 0;
}

static int vm2_install_halt_stub(char *err, size_t err_len)
{
    uint16_t pc = VM2_HANDLER_ADDR;
    uint16_t stop_pc = VM2_HANDLER_ADDR +
                       (uint16_t)((ARRAY_SIZE(vm2_halt_stub_prog) - 1u) * 2u);

    if (vm2_halt_store_word(VM2_BUSERR_VECTOR, VM2_HANDLER_ADDR, err, err_len) != 0) {
        return -1;
    }
    if (vm2_halt_store_word((uint16_t)(VM2_BUSERR_VECTOR + 2), VM2_VECTOR_PSW, err,
                            err_len) != 0) {
        return -1;
    }

    for (size_t i = 0; i < ARRAY_SIZE(vm2_halt_stub_prog); i++) {
        if (vm2_halt_store_word(pc, vm2_halt_stub_prog[i], err, err_len) != 0) {
            return -1;
        }
        pc += 2;
    }

    if (vm2_halt_store_word(VM2_HALT_VECTOR, stop_pc, err, err_len) != 0) {
        return -1;
    }
    if (vm2_halt_store_word((uint16_t)(VM2_HALT_VECTOR + 2), VM2_VECTOR_PSW, err,
                            err_len) != 0) {
        return -1;
    }

    return 0;
}

static int parse_cpu_model(const char *name, byte *model)
{
    if (!name || !model) {
        return -1;
    }

    if (!strcmp(name, "11/84")) {
        *model = DCJ11;
        return 0;
    }
    if (!strcmp(name, "11/34")) {
        *model = K1801VM2;
        return 0;
    }
    if (!strcmp(name, "dcj11") || !strcmp(name, "11/03")) {
        *model = DCJ11;
        return 0;
    }
    if (!strcmp(name, "k1801vm1") || !strcmp(name, "vm1")) {
        *model = K1801VM1;
        return 0;
    }
    if (!strcmp(name, "k1801vm2") || !strcmp(name, "vm2")) {
        *model = K1801VM2;
        return 0;
    }
    if (!strcmp(name, "k1806vm2")) {
        *model = K1806VM2;
        return 0;
    }

    return -1;
}

static int parse_boot_device(const char *name, int *kind_out, int *unit_out)
{
    const char *suffix = NULL;
    int kind = BOOT_DEV_NONE;
    unsigned max_units = 0;
    unsigned long unit = 0;
    char *endp = NULL;

    if (!name || !kind_out || !unit_out) {
        return -1;
    }

    if (!strncmp(name, "rk", 2)) {
        kind = BOOT_DEV_RK;
        suffix = name + 2;
        max_units = RK11_MAX_DRIVES;
    } else if (!strncmp(name, "rh", 2) || !strncmp(name, "hk", 2)) {
        kind = BOOT_DEV_RH;
        suffix = name + 2;
        max_units = RH11_MAX_DRIVES;
    } else if (!strncmp(name, "rl", 2)) {
        kind = BOOT_DEV_RL;
        suffix = name + 2;
        max_units = RL11_MAX_DRIVES;
    } else if (!strncmp(name, "tq", 2)) {
        kind = BOOT_DEV_TQ;
        suffix = name + 2;
        max_units = TQ11_MAX_UNITS;
    } else {
        return -1;
    }

    if (!suffix || *suffix == '\0') {
        unit = 0;
    } else {
        unit = strtoul(suffix, &endp, 10);
        if (!endp || *endp != '\0') {
            return -1;
        }
    }

    if (unit >= max_units) {
        return -1;
    }

    *kind_out = kind;
    *unit_out = (int)unit;
    return 0;
}

static const char *cpu_model_name(byte model)
{
    switch (model) {
    case K1801VM1:
        return "k1801vm1";
    case K1801VM2:
        return "k1801vm2";
    case K1806VM2:
        return "k1806vm2";
    case DCJ11:
        return "dcj11";
    default:
        return "unknown";
    }
}

#define MMR3_BME 0000040u
#define MMR3_M22E 0000020u
#define MMR0_RELO_ENABLE 0000001u

static void ubmap_sync_from_cpu(const regs *r, lsi11_machine_t machine)
{
    static int trace_inited = 0;
    static int trace_on = 0;
    static int last_enabled = -1;
    static uint16_t last_ssr3 = 0177777;
    int enabled = 0;
    uint16_t ssr3_snapshot = 0;

    if (!trace_inited) {
        trace_on = (getenv("LSI11_TRACE_UBMAP") != NULL) ? 1 : 0;
        trace_inited = 1;
    }

    if (getenv("LSI11_UBMAP_OFF") != NULL) {
        enabled = 0;
        ubmap_set_enabled(0);
    } else if (!r || machine != LSI11_MACHINE_1184 || r->model != DCJ11) {
        enabled = 0;
        ubmap_set_enabled(0);
    } else {
#if defined(ENABLE_MMU) && (ENABLE_MMU)
        ssr3_snapshot = (uint16_t)r->mmu_ssr3;
        enabled = (r->mmu_ssr3 & MMR3_BME) ? 1 : 0;
#else
        enabled = 0;
#endif
        ubmap_set_enabled(enabled);
    }

    if (trace_on && r &&
            (enabled != last_enabled || ssr3_snapshot != last_ssr3)) {
        fprintf(stderr, "UBMAP sync ssr3=%06o on=%o\n", ssr3_snapshot, enabled & 1);
    }
    last_enabled = enabled;
    last_ssr3 = ssr3_snapshot;
}

static void bus_iowin_sync_from_cpu(const regs *r, lsi11_machine_t machine)
{
    int io16 = 1;
    int m22e = 0;

#if defined(ENABLE_MMU) && (ENABLE_MMU)
    if (r && machine == LSI11_MACHINE_1184 && r->model == DCJ11) {
        if (r->mmu_ssr0 & MMR0_RELO_ENABLE) {
            io16 = 0;
            m22e = (r->mmu_ssr3 & MMR3_M22E) ? 1 : 0;
        }
    }
#else
    (void)r;
    (void)machine;
#endif
    bus_set_pdp1184_io_16bit(io16);
    bus_set_pdp1184_m22e(m22e);
}

/*
 * RK11 ROM bootstrap (DEC/SIMH compatible sequence).
 * Reads one 256-word block from RK unit 0 into 000000 and jumps to 000000.
 * Loaded/executed from 02000 to avoid clobbering low memory before transfer.
 */
#define RK_BOOT_ADDR  002000
#define RK_BOOT_ENTRY (RK_BOOT_ADDR + 000002)
static const uint16_t rk_bootstrap[] = {
    0042113,                  /* "KD" */
    0012706, RK_BOOT_ADDR,    /* MOV #BOOT_ADDR,SP */
    0012700, 0000000,         /* MOV #0,R0          ; unit 0 */
    0010003,                  /* MOV R0,R3 */
    0000303,                  /* SWAB R3 */
    0006303,                  /* ASL R3 */
    0006303,                  /* ASL R3 */
    0006303,                  /* ASL R3 */
    0006303,                  /* ASL R3 */
    0006303,                  /* ASL R3 */
    0012701, 0177412,         /* MOV #RKDA,R1 */
    0010311,                  /* MOV R3,(R1)        ; DA */
    0005041,                  /* CLR -(R1)          ; BA */
    0012741, 0177000,         /* MOV #-256.*2,-(R1) ; WC */
    0012741, 0000005,         /* MOV #READ+GO,-(R1) */
    0005002,                  /* CLR R2 */
    0005003,                  /* CLR R3 */
    0012704, RK_BOOT_ADDR + 000020, /* MOV #BOOT_ADDR+20,R4 */
    0005005,                  /* CLR R5 */
    0105711,                  /* WAIT: TSTB (R1) */
    0100376,                  /*       BPL WAIT */
    0105011,                  /* CLRB (R1) */
    0005007                   /* CLR PC */
};

/*
 * RL11 ROM bootstrap (DEC/SIMH compatible sequence).
 * Reads one 256-word block from RL unit 0 into 000000 and jumps to 000000.
 * Loaded/executed from 02000 to avoid clobbering low memory before transfer.
 */
#define RL_BOOT_ADDR  002000
#define RL_BOOT_ENTRY RL_BOOT_ADDR
static const uint16_t rl_bootstrap[] = {
    0012701, 0174400,         /* MOV  #RLCS,R1 */
    0012761, 0000013, 0000004,/* MOV  #13,4(R1)      ; clear error */
    0012711, 0000004,         /* MOV  #4,(R1)         ; GET STATUS */
    0105711,                  /* WAIT1: TSTB (R1) */
    0100376,                  /*        BPL  WAIT1 */
    0005061, 0000002,         /* CLR  2(R1)           ; BA = 0 */
    0005061, 0000004,         /* CLR  4(R1)           ; DA = 0 */
    0012761, 0177400, 0000006,/* MOV  #-256,6(R1)     ; WC = -256 words */
    0012711, 0000014,         /* MOV  #14,(R1)        ; READ + GO */
    0105711,                  /* WAIT2: TSTB (R1) */
    0100376,                  /*        BPL  WAIT2 */
    0005007                   /* CLR  PC */
};

/*
 * RH11/RK611 ROM bootstrap (DEC/SIMH HK-compatible sequence).
 * Reads one 256-word block from drive 0 into 000000 and jumps to 000000.
 * Loaded/executed from 02000 to avoid clobbering low memory before transfer.
 */
#define RH_BOOT_ADDR  002000
#define RH_BOOT_ENTRY (RH_BOOT_ADDR + 000002)
static const uint16_t rh_bootstrap[] = {
    0042115,                  /* "MD" */
    0012706, RH_BOOT_ADDR,    /* MOV #BOOT_ADDR,SP */
    0012700, 0000000,         /* MOV #0,R0          ; unit 0 */
    0012701, 0177440,         /* MOV #RHCS1,R1 */
    0012761, 0000040, 0000010,/* MOV #SCLR,10(R1)   ; reset */
    0010061, 0000010,         /* MOV R0,10(R1)      ; set unit */
    0016102, 0000012,         /* MOV 12(R1),R2      ; drive type */
    0100375,                  /* BPL .-4            ; valid? */
    0042702, 0177377,         /* BIC #177377,R2 */
    0006302,                  /* ASL R2 */
    0006302,                  /* ASL R2 */
    0012703, 0000003,         /* MOV #PACK+GO,R3 */
    0050203,                  /* BIS R2,R3 */
    0010311,                  /* MOV R3,(R1) */
    0105711,                  /* WAIT1: TSTB (R1) */
    0100376,                  /*        BPL WAIT1 */
    0012761, 0177000, 0000002,/* MOV #-512.,2(R1)   ; WC */
    0005061, 0000004,         /* CLR 4(R1)          ; BA */
    0005061, 0000006,         /* CLR 6(R1)          ; DA */
    0005061, 0000020,         /* CLR 20(R1)         ; DC */
    0012703, 0000021,         /* MOV #READ+GO,R3 */
    0050203,                  /* BIS R2,R3 */
    0010311,                  /* MOV R3,(R1) */
    0105711,                  /* WAIT2: TSTB (R1) */
    0100376,                  /*        BPL WAIT2 */
    0005002,                  /* CLR R2 */
    0005003,                  /* CLR R3 */
    0012704, RH_BOOT_ADDR + 000020, /* MOV #BOOT_ADDR+20,R4 */
    0005005,                  /* CLR R5 */
    0005007                   /* CLR PC */
};

/*
 * TQ11/TMSCP ROM bootstrap (SIMH TQ-compatible sequence).
 * Initializes the controller, issues ONLINE, REWIND, and READ,
 * reads one 512-byte block to 000000, and then jumps to 000000.
 * Loaded/executed from 016000.
 */
#define TQ_BOOT_ADDR   016000
#define TQ_BOOT_ENTRY  (TQ_BOOT_ADDR + 000002)
#define TQ_BOOT_UNIT   (TQ_BOOT_ADDR + 000010)
#define TQ_BOOT_CSR    (TQ_BOOT_ADDR + 000014)
#define TQ_B_CMDINT    (TQ_BOOT_ADDR - 001000)
#define TQ_B_RSPINT    (TQ_B_CMDINT + 000002)
#define TQ_B_RING      (TQ_B_RSPINT + 000002)
#define TQ_B_RSPH      (TQ_B_RING + 000010)
#define TQ_B_TKRSP     (TQ_B_RSPH + 000004)
#define TQ_B_CMDH      (TQ_B_TKRSP + 000060)
#define TQ_B_TKCMD     (TQ_B_CMDH + 000004)
#define TQ_B_UNIT      (TQ_B_TKCMD + 000004)
static const uint16_t tq_bootstrap[] = {
    0046525,                        /* "UM" */

    0012706, TQ_BOOT_ADDR,          /* MOV #016000,SP */
    0012700, 0000000,               /* MOV #UNIT,R0 */
    0012701, 0174500,               /* MOV #0174500,R1      ; IP */
    0005021,                        /* CLR (R1)+            ; init */
    0012704, 0004000,               /* MOV #004000,R4       ; S1 mask */
    0005002,                        /* CLR R2 */
    0005022,                        /* 10$: CLR (R2)+       ; clear up to 016000 */
    0020237, TQ_BOOT_ADDR - 000002, /*      CMP R2,#015776 */
    0103774,                        /*      BLO 10$ */
    0012705, TQ_BOOT_ADDR + 000312, /* MOV #016312,R5       ; cmd table */

    0005711,                        /* 20$: TST (R1)        ; err? */
    0100001,                        /*      BPL 30$ */
    0000000,                        /*      HALT */
    0030411,                        /* 30$: BIT R4,(R1)     ; step set? */
    0001773,                        /*      BEQ 20$ */
    0012511,                        /*      MOV (R5)+,(R1)  ; send next */
    0006304,                        /*      ASL R4          ; next mask */
    0100370,                        /*      BPL 20$         ; until S4 */

    0012737, 0000400, TQ_B_CMDH + 000002,   /* MOV #000400,CMDH+2 ; VCID=1 */
    0012737, 0000044, TQ_B_CMDH,            /* MOV #000044,CMDH   ; len */
    0010037, TQ_B_UNIT,                     /* MOV R0,UNIT */
    0012737, 0000011, TQ_B_TKCMD + 000010,  /* MOV #000011,TKCMD+10 ; ONL */
    0012737, 0020000, TQ_B_TKCMD + 000012,  /* MOV #020000,TKCMD+12 ; clr exc */
    0012702, TQ_B_RING,                     /* MOV #RING,R2 */
    0012722, TQ_B_TKRSP,                    /* MOV #TKRSP,(R2)+ */
    0010203,                                /* MOV R2,R3 */
    0010423,                                /* MOV R4,(R3)+       ; TK own */
    0012723, TQ_B_TKCMD,                    /* MOV #TKCMD,(R3)+ */
    0010423,                                /* MOV R4,(R3)+       ; TK own */
    0005741,                                /* TST -(R1)          ; start poll */
    0005712,                                /* 40$: TST (R2)      ; wait rsp */
    0100776,                                /*      BMI 40$ */
    0105737, TQ_B_TKRSP + 000012,           /* TSTB TKRSP+12      ; stat */
    0001401,                                /* BEQ 50$ */
    0000000,                                /* HALT */
    0012703, TQ_B_TKCMD + 000010,           /* 50$: MOV #TKCMD+10,R3 */
    0012723, 0000045,                       /* MOV #000045,(R3)+  ; POS */
    0012723, 0020002,                       /* MOV #020002,(R3)+  ; REW */
    0012723, 0000001,                       /* MOV #000001,(R3)+  ; rec count */
    0005023,                                /* CLR (R3)+ */
    0005023,                                /* CLR (R3)+ */
    0005023,                                /* CLR (R3)+ */
    0010412,                                /* MOV R4,(R2)        ; TK own rsp */
    0010437, TQ_B_RING + 000006,            /* MOV R4,RING+6      ; TK own cmd */
    0005711,                                /* TST (R1)           ; start poll */
    0005712,                                /* 60$: TST (R2)      ; wait rsp */
    0100776,                                /*      BMI 60$ */
    0105737, TQ_B_TKRSP + 000012,           /* TSTB TKRSP+12      ; stat */
    0001401,                                /* BEQ 70$ */
    0000000,                                /* HALT */
    0012703, TQ_B_TKCMD + 000010,           /* 70$: MOV #TKCMD+10,R3 */
    0012723, 0000041,                       /* MOV #000041,(R3)+  ; READ */
    0012723, 0020000,                       /* MOV #020000,(R3)+  ; clr exc */
    0012723, 0001000,                       /* MOV #001000,(R3)+  ; 512. bytes */
    0005023,                                /* CLR (R3)+ */
    0005023,                                /* CLR (R3)+          ; BA low */
    0010412,                                /* MOV R4,(R2)        ; TK own rsp */
    0010437, TQ_B_RING + 000006,            /* MOV R4,RING+6      ; TK own cmd */
    0005711,                                /* TST (R1)           ; start poll */
    0005712,                                /* 80$: TST (R2)      ; wait rsp */
    0100776,                                /*      BMI 80$ */
    0105737, TQ_B_TKRSP + 000012,           /* TSTB TKRSP+12      ; stat */
    0001401,                                /* BEQ 90$ */
    0000000,                                /* HALT */

    0005003,                                /* 90$: CLR R3 */
    0012704, TQ_BOOT_ADDR + 000020,         /* MOV #016020,R4 */
    0005005,                                /* CLR R5 */
    0005007,                                /* CLR PC */

    0100000,                                /* cmdtbl: S1 */
    TQ_B_RING,                              /* ring base */
    0000000,                                /* ring base high */
    0000001                                 /* GO */
};

static int install_bootstrap(uint16_t base, const uint16_t *words, size_t word_count)
{
    uint8_t *ram = bus_ram_ptr(base);
    size_t n = word_count * sizeof(uint16_t);

    if (!ram || !bus_range_is_ram(base, n)) {
        return -1;
    }

    for (size_t i = 0; i < word_count; i++) {
        uint16_t w = words[i];
        ram[i * 2 + 0] = (uint8_t)(w & 000377);
        ram[i * 2 + 1] = (uint8_t)((w >> 8) & 000377);
    }

    return 0;
}

static int preload_rt11_boot_block(const char *path)
{
    const size_t n = 01000;
    uint8_t buf[01000];
    uint8_t *ram0;
    FILE *f;
    size_t got;
    int all_zero = 1;

    if (!path) {
        return -1;
    }

    f = fopen(path, "rb");
    if (!f) {
        return -1;
    }

    got = fread(buf, 1, n, f);
    if (got == n) {
        for (size_t i = 0; i < n; i++) {
            if (buf[i]) {
                all_zero = 0;
                break;
            }
        }
    }
    if (got != n || all_zero) {
        if (fseek(f, (long)n, SEEK_SET) == 0) {
            got = fread(buf, 1, n, f);
            if (got == n) {
                all_zero = 0;
            }
        }
    }
    fclose(f);

    if (got != n || all_zero) {
        return -1;
    }

    ram0 = bus_ram_ptr(0);
    if (!ram0 || !bus_range_is_ram(0, n)) {
        return -1;
    }
    memcpy(ram0, buf, n);
    return 0;
}

static void usage(const char *argv0)
{
#if defined(LSI11_TARGET_PDP1184)
    const char *target = "pdp1184";
#else
    const char *target = "lsi11";
#endif

    fprintf(stderr,
            "Usage:\n"
            "  %s [-rk <rk05.img>] [-rh <rk06rk07.img>] [-rl <rl.img>] "
            "[-tq <tk50.tap>] "
            "[-boot <dev>|-bootcopy|-bootrt11|-boottq] [-cpu <model>]\n"
            "\n"
            "Target profile:\n"
            "  %s\n"
            "\n"
            "Options:\n"
#if defined(LSI11_TARGET_PDP1184)
            "  -cpu <model>    CPU model: dcj11 (default), 11/03, 11/84, 11/34, "
            "k1801vm1, k1801vm2, k1806vm2\n"
#else
            "  -cpu <model>    CPU model: dcj11 (default), 11/03, "
            "k1801vm1, k1801vm2, k1806vm2\n"
#endif
            "  -force-fis      Enable FIS for unsupported CPUs\n"
            "  -force-fp11     Enable FP11-A for unsupported CPUs\n"
            "  -rk <path>      Attach RK05 image (repeatable: rk0,rk1,...)\n"
            "  -rh <path>      Attach RH11 (RK06/RK07) image (repeatable: rh0,rh1,...)\n"
            "  -rl <path>      Attach RL image (repeatable: rl0,rl1,...; auto RL01/RL02)\n"
            "  -rl01 <path>    Attach image as RL01 (repeatable)\n"
            "  -rl02 <path>    Attach image as RL02 (repeatable)\n"
            "  -tq <path>      Attach TK50 TMSCP tape image (.tap) (repeatable: tq0,tq1,...)\n"
            "  -dz <port>      Enable DZ11 and listen for line connections on TCP <port>\n"
            "  -disable-dl     Disable DL11\n"
            "  -disable-dz     Disable DZ11\n"
            "  -disable-kw     Disable KW11\n"
            "  -enable-kw11-l  Enable KW11-L decode\n"
            "  -disable-kw11-l Disable KW11-L decode\n"
            "  -enable-kw11-p  Enable KW11-P decode\n"
            "  -disable-kw11-p Disable KW11-P decode\n"
            "  -disable-lp     Disable LP11\n"
            "  -disable-rk     Disable RK11\n"
            "  -disable-rh     Disable RH11\n"
            "  -disable-rl     Disable RL11\n"
            "  -disable-tq     Disable TQ11/TMSCP tape controller\n"
            "  -disable-sr     Disable SR\n"
            "  -boot <dev>     Boot selected unit: rkN|rhN|hkN|rlN|tqN (N defaults to 0)\n"
            "  -bootcopy       Copy first 010000 bytes from RK/RH/RL image into RAM at "
            "000000\n"
            "  -bootrt11       Run built-in RK/RH/RL bootstrap for the selected "
            "controller\n"
            "  -boottq         Run built-in TQ/TMSCP bootstrap at 016000 for unit 0\n"
            "  -trace          Trace each instruction\n"
            "  -trace-after N  Start tracing only after N executed instructions\n"
            "  -trace-regs     With -trace, also dump registers\n"
            "  -traceirq       Trace delivered IRQ vectors\n"
            "  -tracenxm       Trace NXM traps\n"
            "  -tty7b          DL11 console 7-bit mode (default)\n"
            "  -tty8b          DL11 console 8-bit mode\n"
            "  -nl-to-cr       Map host newline (\\n) to CR (\\r) for input\n"
            "  -exit-on-abort  Exit emulator on HALT/abort\n"
            "  -steps N        Emulate N steps then exit\n"
            "  -check-config   Validate machine config and exit\n",
            argv0, target);
    fprintf(stderr,
            "  -load <file>    Load binary file into RAM\n"
            "  -addr <oct>     Load address (octal) for -load (default 0)\n"
            "  -pc <oct>       Set initial PC (R7) to octal address\n"
            "  -sr <oct>       Set SR switch register (0177570) value\n"
#if defined(LSI11_TARGET_PDP1184)
            "  -ram <kb>       RAM size in KB (default 4096, must be multiple of 4)\n"
            "  --mem-kb <kb>   Same as -ram (pdp1184 only)\n"
            "  -dl11-alias     Enable DL11 alias 0176500..0176507\n"
            "  -no-dl11-alias  Disable DL11 alias 0176500..0176507 (default)\n");
#else
            "  -dl11-alias     Keep DL11 alias enabled (default)\n"
            "  -no-dl11-alias  Disable DL11 alias (non-standard for this target)\n");
#endif
}

int main(int argc, char **argv)
{
    const char *rk_path[RK11_MAX_DRIVES] = {0};
    const char *rh_path[RH11_MAX_DRIVES] = {0};
    const char *tq_path[TQ11_MAX_UNITS] = {0};
    struct {
        const char *path;
        int type;
    } rl_path[RL11_MAX_DRIVES];
    int rk_count = 0;
    int rh_count = 0;
    int rl_count = 0;
    int tq_count = 0;
    const char *load_path = NULL;
    int do_bootcopy = 0;
    int do_bootrt11 = 0;
    int do_boottq = 0;
    int do_boot = 0;
    int boot_kind = BOOT_DEV_NONE;
    int boot_unit = 0;
    long load_addr = 0;
    long start_pc = -1;
    long sr_value = -1;
    long ram_kb_arg = -1;
    int force_dl11_alias = -1;
    int disable_dl = 0;
    int disable_dz = 0;
    int disable_kw = 0;
    int kw11_l_override = -1;
    int kw11_p_override = -1;
    int disable_lp = 0;
    int disable_rk = 0;
    int disable_rh = 0;
    int disable_rl = 0;
    int disable_tq = 0;
    int disable_sr = 0;
    long dz_port = -1;
    int dz_port_set = 0;
#if defined(LSI11_TARGET_PDP1184)
    byte cpu_model = DCJ11;
#else
    byte cpu_model = DCJ11;
#endif
    int force_fis = 0;
    int force_fp11 = 0;
    int trace = 0;
    int trace_regs = 0;
    long trace_after = -1;
    long max_steps = -1;
    int dl11_8bit = 0;
    int do_nl_to_cr = 0;
    int exit_on_abort = 0;
    int check_config_only = 0;
    int trace_loopvals = 0;
    char cfg_err[160] = {0};

#if defined(LSI11_TARGET_PDP1184)
    const lsi11_machine_t machine_kind = LSI11_MACHINE_1184;
#else
    const lsi11_machine_t machine_kind = LSI11_MACHINE_1104;
#endif

    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-rk") && i + 1 < argc) {
            if (rk_count >= RK11_MAX_DRIVES) {
                fprintf(stderr, "Too many -rk images (max %d)\n", RK11_MAX_DRIVES);
                return 2;
            }
            rk_path[rk_count++] = argv[++i];
        } else if (!strcmp(argv[i], "-rh") && i + 1 < argc) {
            if (rh_count >= RH11_MAX_DRIVES) {
                fprintf(stderr, "Too many -rh images (max %d)\n", RH11_MAX_DRIVES);
                return 2;
            }
            rh_path[rh_count++] = argv[++i];
        } else if (!strcmp(argv[i], "-rl") && i + 1 < argc) {
            if (rl_count >= RL11_MAX_DRIVES) {
                fprintf(stderr, "Too many -rl images (max %d)\n", RL11_MAX_DRIVES);
                return 2;
            }
            rl_path[rl_count].path = argv[++i];
            rl_path[rl_count].type = RL11_TYPE_AUTO;
            rl_count++;
        } else if (!strcmp(argv[i], "-rl01") && i + 1 < argc) {
            if (rl_count >= RL11_MAX_DRIVES) {
                fprintf(stderr, "Too many -rl images (max %d)\n", RL11_MAX_DRIVES);
                return 2;
            }
            rl_path[rl_count].path = argv[++i];
            rl_path[rl_count].type = RL11_TYPE_RL01;
            rl_count++;
        } else if (!strcmp(argv[i], "-rl02") && i + 1 < argc) {
            if (rl_count >= RL11_MAX_DRIVES) {
                fprintf(stderr, "Too many -rl images (max %d)\n", RL11_MAX_DRIVES);
                return 2;
            }
            rl_path[rl_count].path = argv[++i];
            rl_path[rl_count].type = RL11_TYPE_RL02;
            rl_count++;
        } else if (!strcmp(argv[i], "-tq") && i + 1 < argc) {
            if (tq_count >= TQ11_MAX_UNITS) {
                fprintf(stderr, "Too many -tq images (max %d)\n", TQ11_MAX_UNITS);
                return 2;
            }
            tq_path[tq_count++] = argv[++i];
        } else if (!strcmp(argv[i], "-dz") && i + 1 < argc) {
            dz_port = strtol(argv[++i], NULL, 10);
            dz_port_set = 1;
        } else if (!strcmp(argv[i], "-disable-dl")) {
            disable_dl = 1;
        } else if (!strcmp(argv[i], "-disable-dz")) {
            disable_dz = 1;
        } else if (!strcmp(argv[i], "-disable-kw")) {
            disable_kw = 1;
        } else if (!strcmp(argv[i], "-enable-kw11-l")) {
            kw11_l_override = 1;
        } else if (!strcmp(argv[i], "-disable-kw11-l")) {
            kw11_l_override = 0;
        } else if (!strcmp(argv[i], "-enable-kw11-p")) {
            kw11_p_override = 1;
        } else if (!strcmp(argv[i], "-disable-kw11-p")) {
            kw11_p_override = 0;
        } else if (!strcmp(argv[i], "-disable-lp")) {
            disable_lp = 1;
        } else if (!strcmp(argv[i], "-disable-rk")) {
            disable_rk = 1;
        } else if (!strcmp(argv[i], "-disable-rh")) {
            disable_rh = 1;
        } else if (!strcmp(argv[i], "-disable-rl")) {
            disable_rl = 1;
        } else if (!strcmp(argv[i], "-disable-tq")) {
            disable_tq = 1;
        } else if (!strcmp(argv[i], "-disable-sr")) {
            disable_sr = 1;
        } else if (!strcmp(argv[i], "-bootcopy")) {
            do_bootcopy = 1;
        } else if (!strcmp(argv[i], "-bootrt11")) {
            do_bootrt11 = 1;
        } else if (!strcmp(argv[i], "-boottq")) {
            do_boottq = 1;
        } else if (!strcmp(argv[i], "-boot") && i + 1 < argc) {
            if (parse_boot_device(argv[++i], &boot_kind, &boot_unit) != 0) {
                fprintf(stderr, "Invalid -boot device: %s\n", argv[i]);
                return 2;
            }
            do_boot = 1;
        } else if (!strcmp(argv[i], "-traceirq")) {
            lsi11_set_trace_irq(1);
        } else if (!strcmp(argv[i], "-tracenxm")) {
            lsi11_set_trace_nxm(1);
        } else if (!strcmp(argv[i], "-trace")) {
            trace = 1;
        } else if (!strcmp(argv[i], "-trace-after") && i + 1 < argc) {
            trace_after = strtol(argv[++i], NULL, 10);
            trace = 1;
        } else if (!strcmp(argv[i], "-trace-regs")) {
            trace = 1;
            trace_regs = 1;
        } else if (!strcmp(argv[i], "-tty7b")) {
            dl11_8bit = 0;
        } else if (!strcmp(argv[i], "-tty8b")) {
            dl11_8bit = 1;
        } else if (!strcmp(argv[i], "-nl-to-cr")) {
            do_nl_to_cr = 1;
        } else if (!strcmp(argv[i], "-exit-on-abort")) {
            exit_on_abort = 1;
        } else if (!strcmp(argv[i], "-check-config")) {
            check_config_only = 1;
        } else if (!strcmp(argv[i], "-steps") && i + 1 < argc) {
            max_steps = strtol(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "-load") && i + 1 < argc) {
            load_path = argv[++i];
        } else if (!strcmp(argv[i], "-addr") && i + 1 < argc) {
            load_addr = strtol(argv[++i], NULL, 8);
        } else if (!strcmp(argv[i], "-pc") && i + 1 < argc) {
            start_pc = strtol(argv[++i], NULL, 8);
        } else if (!strcmp(argv[i], "-sr") && i + 1 < argc) {
            sr_value = strtol(argv[++i], NULL, 8);
        } else if (!strcmp(argv[i], "-ram") && i + 1 < argc) {
            ram_kb_arg = strtol(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--mem-kb") && i + 1 < argc) {
            ram_kb_arg = strtol(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "-dl11-alias")) {
            force_dl11_alias = 1;
        } else if (!strcmp(argv[i], "-no-dl11-alias")) {
            force_dl11_alias = 0;
        } else if (!strcmp(argv[i], "-cpu") && i + 1 < argc) {
            if (parse_cpu_model(argv[++i], &cpu_model) != 0) {
                fprintf(stderr, "Unknown CPU model: %s\n", argv[i]);
                usage(argv[0]);
                return 2;
            }
        } else if (!strcmp(argv[i], "-force-fis")) {
            force_fis = 1;
        } else if (!strcmp(argv[i], "-force-fp11")) {
            force_fp11 = 1;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    trace_loopvals = (getenv("LSI11_TRACE_LOOPVALS") != NULL) ? 1 : 0;

#if !defined(LSI11_TARGET_PDP1184)
    if (ram_kb_arg >= 0) {
        fprintf(stderr,
                "This lsi11 target is fixed 56KB RAM; -ram is not supported.\n");
        return 2;
    }
#endif

    if (ram_kb_arg < 0) {
        ram_kb_arg = 0;
    }

    if (lsi11_machine_configure(machine_kind, (uint32_t)ram_kb_arg, cfg_err,
                                sizeof(cfg_err)) != 0) {
        fprintf(stderr, "Machine configuration error: %s\n", cfg_err);
        return 2;
    }
    if (cpu_is_vm2(cpu_model)) {
        if (bus_vm2_configure(BUS_VM2_DEFAULT_USER_RAM_BYTES,
                              VM2_MIN_HALT_RAM_BYTES, cfg_err,
                              sizeof(cfg_err)) != 0) {
            fprintf(stderr, "VM2 configuration error: %s\n", cfg_err);
            return 2;
        }
    }
    if (force_dl11_alias >= 0) {
        lsi11_set_dl11_alias(force_dl11_alias);
    }
    if (dz_port_set &&
            dz11_set_listen_port((int)dz_port, cfg_err, sizeof(cfg_err)) != 0) {
        fprintf(stderr, "DZ11 configuration error: %s\n", cfg_err);
        return 2;
    }

    {
        int kw11_l_on = 1;
        int kw11_p_on = 0;

        if (kw11_l_override >= 0) {
            kw11_l_on = kw11_l_override;
        }
        if (kw11_p_override >= 0) {
            kw11_p_on = kw11_p_override;
        }
        kw11_set_visibility(kw11_l_on, kw11_p_on);
        if (getenv("LSI11_KW11_REALTIME") != NULL) {
            kw11_set_step_clock(0);
        } else {
            kw11_set_step_clock(1);
        }
    }

    if (cpu_model == K1801VM1) {
        if (lsi11_set_device_enabled("vm1sel", 1, cfg_err, sizeof(cfg_err)) != 0 ||
                lsi11_set_device_enabled("vm1sav", 1, cfg_err,
                                         sizeof(cfg_err)) != 0) {
            fprintf(stderr, "Device configuration error: %s\n", cfg_err);
            return 2;
        }
    } else {
        if (lsi11_set_device_enabled("vm1sel", 0, cfg_err, sizeof(cfg_err)) != 0 ||
                lsi11_set_device_enabled("vm1sav", 0, cfg_err,
                                         sizeof(cfg_err)) != 0) {
            fprintf(stderr, "Device configuration error: %s\n", cfg_err);
            return 2;
        }
    }

    if (disable_dl &&
            lsi11_set_device_enabled("dl11", 0, cfg_err, sizeof(cfg_err)) != 0) {
        fprintf(stderr, "Device configuration error: %s\n", cfg_err);
        return 2;
    }
    if (disable_dz &&
            lsi11_set_device_enabled("dz11", 0, cfg_err, sizeof(cfg_err)) != 0) {
        fprintf(stderr, "Device configuration error: %s\n", cfg_err);
        return 2;
    }
    if (disable_kw &&
            lsi11_set_device_enabled("kw11", 0, cfg_err, sizeof(cfg_err)) != 0) {
        fprintf(stderr, "Device configuration error: %s\n", cfg_err);
        return 2;
    }
    if (disable_lp &&
            lsi11_set_device_enabled("lp11", 0, cfg_err, sizeof(cfg_err)) != 0) {
        fprintf(stderr, "Device configuration error: %s\n", cfg_err);
        return 2;
    }
    if (disable_rk &&
            lsi11_set_device_enabled("rk11", 0, cfg_err, sizeof(cfg_err)) != 0) {
        fprintf(stderr, "Device configuration error: %s\n", cfg_err);
        return 2;
    }
    if (disable_rh &&
            lsi11_set_device_enabled("rh11", 0, cfg_err, sizeof(cfg_err)) != 0) {
        fprintf(stderr, "Device configuration error: %s\n", cfg_err);
        return 2;
    }
    if (disable_rl &&
            lsi11_set_device_enabled("rl11", 0, cfg_err, sizeof(cfg_err)) != 0) {
        fprintf(stderr, "Device configuration error: %s\n", cfg_err);
        return 2;
    }
    if (disable_tq &&
            lsi11_set_device_enabled("tq11", 0, cfg_err, sizeof(cfg_err)) != 0) {
        fprintf(stderr, "Device configuration error: %s\n", cfg_err);
        return 2;
    }
    if (tq_count > 0 && !disable_tq &&
            lsi11_set_device_enabled("tq11", 1, cfg_err, sizeof(cfg_err)) != 0) {
        fprintf(stderr, "Device configuration error: %s\n", cfg_err);
        return 2;
    }
    if (disable_sr &&
            lsi11_set_device_enabled("sr", 0, cfg_err, sizeof(cfg_err)) != 0) {
        fprintf(stderr, "Device configuration error: %s\n", cfg_err);
        return 2;
    }
    if (dz_port_set && !disable_dz &&
            lsi11_set_device_enabled("dz11", 1, cfg_err, sizeof(cfg_err)) != 0) {
        fprintf(stderr, "Device configuration error: %s\n", cfg_err);
        return 2;
    }
    if (rk_count > 0 && !lsi11_device_enabled("rk11")) {
        fprintf(stderr, "-rk is not allowed with -disable-rk\n");
        return 2;
    }
    if (dz_port_set && (dz_port > 65535 || dz_port <= 0)) {
        fprintf(stderr, "-dz requires TCP port in range 1..65535\n");
        return 2;
    }
    if (dz_port_set && !lsi11_device_enabled("dz11")) {
        fprintf(stderr, "-dz is not allowed with -disable-dz\n");
        return 2;
    }
    if (rh_count > 0 && !lsi11_device_enabled("rh11")) {
        fprintf(stderr, "-rh is not allowed with -disable-rh\n");
        return 2;
    }
    if (rl_count > 0 && !lsi11_device_enabled("rl11")) {
        fprintf(stderr, "-rl/-rl01/-rl02 is not allowed with -disable-rl\n");
        return 2;
    }
    if (tq_count > 0 && !lsi11_device_enabled("tq11")) {
        fprintf(stderr, "-tq is not allowed with -disable-tq\n");
        return 2;
    }
    if (do_boottq && tq_count == 0) {
        fprintf(stderr, "-boottq requires -tq <image>\n");
        return 2;
    }
    if (do_boot && (do_bootcopy || do_bootrt11 || do_boottq)) {
        fprintf(stderr, "-boot cannot be combined with -bootcopy/-bootrt11/-boottq\n");
        return 2;
    }
    if (do_boot) {
        switch (boot_kind) {
        case BOOT_DEV_RK:
            if (!lsi11_device_enabled("rk11")) {
                fprintf(stderr, "-boot rk* requires RK11 enabled\n");
                return 2;
            }
            if (boot_unit >= rk_count || !rk_path[boot_unit]) {
                fprintf(stderr, "-boot rk%o requires matching -rk attachment\n", boot_unit);
                return 2;
            }
            break;
        case BOOT_DEV_RH:
            if (!lsi11_device_enabled("rh11")) {
                fprintf(stderr, "-boot rh*/hk* requires RH11 enabled\n");
                return 2;
            }
            if (boot_unit >= rh_count || !rh_path[boot_unit]) {
                fprintf(stderr, "-boot rh%o requires matching -rh attachment\n", boot_unit);
                return 2;
            }
            break;
        case BOOT_DEV_RL:
            if (!lsi11_device_enabled("rl11")) {
                fprintf(stderr, "-boot rl* requires RL11 enabled\n");
                return 2;
            }
            if (boot_unit >= rl_count || !rl_path[boot_unit].path) {
                fprintf(stderr, "-boot rl%o requires matching -rl attachment\n", boot_unit);
                return 2;
            }
            break;
        case BOOT_DEV_TQ:
            if (!lsi11_device_enabled("tq11")) {
                fprintf(stderr, "-boot tq* requires TQ11 enabled\n");
                return 2;
            }
            if (boot_unit >= tq_count || !tq_path[boot_unit]) {
                fprintf(stderr, "-boot tq%o requires matching -tq attachment\n", boot_unit);
                return 2;
            }
            break;
        default:
            fprintf(stderr, "Internal error: unknown boot device\n");
            return 2;
        }
    }

    if (check_config_only) {
        const char *m = (lsi11_machine_current() == LSI11_MACHINE_1184) ? "pdp1184"
                        : "lsi11";
        int rh11_on = lsi11_device_enabled("rh11");
        int kw11_master = lsi11_device_enabled("kw11");
        int kw11_l_on = kw11_master ? kw11_l_enabled() : 0;
        int kw11_p_on = kw11_master ? kw11_p_enabled() : 0;
        fprintf(stderr,
                "CONFIG machine=%s cpu=%s ram_kb=%u dl11_alias=%d rh11=%d "
                "dev_dl=%d dev_kw=%d dev_kw11_l=%d dev_kw11_p=%d "
                "dev_dz=%d dev_lp=%d dev_rk=%d dev_rh=%d dev_rl=%d dev_tq=%d dev_sr=%d "
                "dev_vm1sel=%d dev_vm1sav=%d\n",
                m, cpu_model_name(cpu_model), lsi11_machine_ram_kb(),
                lsi11_dl11_alias(), rh11_on, lsi11_device_enabled("dl11"),
                kw11_master, kw11_l_on, kw11_p_on,
                lsi11_device_enabled("dz11"), lsi11_device_enabled("lp11"),
                lsi11_device_enabled("rk11"),
                lsi11_device_enabled("rh11"), lsi11_device_enabled("rl11"),
                lsi11_device_enabled("tq11"),
                lsi11_device_enabled("sr"), lsi11_device_enabled("vm1sel"),
                lsi11_device_enabled("vm1sav"));
        return 0;
    }

    regs r;
    memset(&r, 0, sizeof(r));
    r.model = cpu_model;

    dl11_set_8bit(dl11_8bit);
    dl11_set_nl_to_cr(do_nl_to_cr);
    dz11_set_8bit(dl11_8bit);
    lsi11_hw_connect(&r);

    if (r.model == K1806VM2) {
        r.model = K1801VM2;
    }
    if (r.model == DCJ11) {
        r.has_fis = 0;
        r.has_fpu = 1;
    } else {
        r.has_fis = 0;
        r.has_fpu = 0;
    }

    /* Initialize devices once before attaching media. */
    if (r.init(&r) != 0) {
        fprintf(stderr, "init failed\n");
        return 1;
    }

    /* Attach images by ascending unit index. */
    for (int unit = 0; unit < rk_count; unit++) {
        if (rk11_open_image_unit((unsigned)unit, rk_path[unit]) != 0) {
            fprintf(stderr, "rk11_open_image failed: rk%o %s\n", unit, rk_path[unit]);
            r.fini(&r);
            return 1;
        }
    }
    for (int unit = 0; unit < rh_count; unit++) {
        if (rh11_open_image_unit((unsigned)unit, rh_path[unit]) != 0) {
            fprintf(stderr, "rh11_open_image failed: rh%o %s\n", unit, rh_path[unit]);
            rk11_close_image();
            r.fini(&r);
            return 1;
        }
    }
    for (int unit = 0; unit < rl_count; unit++) {
        if (rl11_open_image_typed_unit((unsigned)unit, rl_path[unit].path,
                                       rl_path[unit].type) != 0) {
            fprintf(stderr, "rl11_open_image failed: rl%o %s\n", unit,
                    rl_path[unit].path);
            rh11_close_image();
            rk11_close_image();
            r.fini(&r);
            return 1;
        }
    }
    for (int unit = 0; unit < tq_count; unit++) {
        if (tq11_open_image_unit((unsigned)unit, tq_path[unit]) != 0) {
            fprintf(stderr, "tq11_open_image failed: tq%o %s\n", unit, tq_path[unit]);
            rl11_close_image();
            rh11_close_image();
            rk11_close_image();
            r.fini(&r);
            return 1;
        }
    }

    if (force_fis) {
        r.has_fis = 1;
    }
    if (force_fp11) {
        r.has_fpu = 1;
    }

    core_reset(&r);
    bus_iowin_sync_from_cpu(&r, machine_kind);
    ubmap_sync_from_cpu(&r, machine_kind);

    if (sr_value >= 0) {
        sr_set((uint16_t)sr_value);
    }

    // FIXME: No-address register for initial configuration
    if (r.model == K1801VM2 || r.model == K1806VM2) {
        r.SEL0 = 0;
        r.SEL0 = 0200; // Disable FIS trap by default
        if (vm2_install_halt_stub(cfg_err, sizeof(cfg_err)) != 0) {
            fprintf(stderr, "VM2 HALT stub error: %s\n", cfg_err);
            r.fini(&r);
            return 1;
        }
    }

    if (do_boot) {
        switch (boot_kind) {
        case BOOT_DEV_RK:
            if (preload_rt11_boot_block(rk_path[boot_unit]) != 0) {
                fprintf(stderr, "RT11 boot block not found in rk%o image\n", boot_unit);
                r.fini(&r);
                return 1;
            }
            if (install_bootstrap(RK_BOOT_ADDR, rk_bootstrap,
                                  sizeof(rk_bootstrap) / sizeof(rk_bootstrap[0])) !=
                    0) {
                fprintf(stderr, "boot destination is outside RAM\n");
                r.fini(&r);
                return 1;
            }
            bus_write16((uint16_t)(RK_BOOT_ADDR + 000010u), (uint16_t)boot_unit);
            r.r[7] = RK_BOOT_ENTRY;
            break;

        case BOOT_DEV_RH:
            if (preload_rt11_boot_block(rh_path[boot_unit]) != 0) {
                fprintf(stderr, "RT11 boot block not found in rh%o image\n", boot_unit);
                r.fini(&r);
                return 1;
            }
            if (install_bootstrap(RH_BOOT_ADDR, rh_bootstrap,
                                  sizeof(rh_bootstrap) / sizeof(rh_bootstrap[0])) !=
                    0) {
                fprintf(stderr, "boot destination is outside RAM\n");
                r.fini(&r);
                return 1;
            }
            bus_write16((uint16_t)(RH_BOOT_ADDR + 000010u), (uint16_t)boot_unit);
            r.r[7] = RH_BOOT_ENTRY;
            break;

        case BOOT_DEV_RL: {
            uint16_t ds = (uint16_t)((boot_unit & 03) << 8);

            if (install_bootstrap(RL_BOOT_ADDR, rl_bootstrap,
                                  sizeof(rl_bootstrap) / sizeof(rl_bootstrap[0])) !=
                    0) {
                fprintf(stderr, "boot destination is outside RAM\n");
                r.fini(&r);
                return 1;
            }
            bus_write16((uint16_t)(RL_BOOT_ADDR + 000014u),
                        (uint16_t)(0000004u | ds));
            bus_write16((uint16_t)(RL_BOOT_ADDR + 000036u),
                        (uint16_t)(0000014u | ds));
            r.r[7] = RL_BOOT_ENTRY;
            break;
        }

        case BOOT_DEV_TQ:
            if (install_bootstrap(TQ_BOOT_ADDR, tq_bootstrap,
                                  sizeof(tq_bootstrap) / sizeof(tq_bootstrap[0])) !=
                    0) {
                fprintf(stderr, "boot destination is outside RAM\n");
                r.fini(&r);
                return 1;
            }
            bus_write16(TQ_BOOT_UNIT, (uint16_t)boot_unit);
            bus_write16(TQ_BOOT_CSR, 0174500u);
            r.r[7] = TQ_BOOT_ENTRY;
            break;

        default:
            fprintf(stderr, "Internal error: unknown boot device\n");
            r.fini(&r);
            return 1;
        }
    }

    if (do_bootcopy) {
        /* Copy first 010000 bytes (4 KB) into RAM[000000..007777] by default.
           Adjust if your bootstrap needs a different size. */
        const size_t n = 010000;
        int rc = -1;
        uint8_t *ram0 = bus_ram_ptr(0);
        if (!ram0 || !bus_range_is_ram(0, n)) {
            fprintf(stderr, "bootcopy destination is outside RAM\n");
            r.fini(&r);
            return 1;
        }
        if (rk_count > 0) {
            rc = rk11_boot_copy(ram0, n);
        } else if (rh_count > 0) {
            rc = rh11_boot_copy(ram0, n);
        } else if (rl_count > 0) {
            rc = rl11_boot_copy(ram0, n);
        } else {
            fprintf(stderr,
                    "-bootcopy requires -rk <image>, -rh <image>, or -rl <image>\n");
            r.fini(&r);
            return 1;
        }
        if (rc != 0) {
            fprintf(stderr, "bootcopy failed (check -rk/-rh image)\n");
            r.fini(&r);
            return 1;
        }
        /* start execution at 000000 (common simple bootstrap scenario) */
        r.r[7] = 000000;
    }

    if (do_bootrt11) {
        if (rl_count > 0) {
            if (install_bootstrap(RL_BOOT_ADDR, rl_bootstrap,
                                  sizeof(rl_bootstrap) / sizeof(rl_bootstrap[0])) !=
                    0) {
                fprintf(stderr, "bootrt11 destination is outside RAM\n");
                r.fini(&r);
                return 1;
            }
            r.r[7] = RL_BOOT_ENTRY;
        } else if (rk_count > 0) {
            if (preload_rt11_boot_block(rk_path[0]) != 0) {
                fprintf(stderr, "RT11 boot block not found in image\n");
                r.fini(&r);
                return 1;
            }
            if (install_bootstrap(RK_BOOT_ADDR, rk_bootstrap,
                                  sizeof(rk_bootstrap) / sizeof(rk_bootstrap[0])) !=
                    0) {
                fprintf(stderr, "bootrt11 destination is outside RAM\n");
                r.fini(&r);
                return 1;
            }
            r.r[7] = RK_BOOT_ENTRY;
        } else if (rh_count > 0) {
            if (preload_rt11_boot_block(rh_path[0]) != 0) {
                fprintf(stderr, "RT11 boot block not found in image\n");
                r.fini(&r);
                return 1;
            }
            if (install_bootstrap(RH_BOOT_ADDR, rh_bootstrap,
                                  sizeof(rh_bootstrap) / sizeof(rh_bootstrap[0])) !=
                    0) {
                fprintf(stderr, "bootrt11 destination is outside RAM\n");
                r.fini(&r);
                return 1;
            }
            r.r[7] = RH_BOOT_ENTRY;
        } else {
            fprintf(stderr,
                    "-bootrt11 requires -rk <image>, -rh <image>, or -rl <image>\n");
            r.fini(&r);
            return 1;
        }
    }

    if (do_boottq) {
        if (install_bootstrap(TQ_BOOT_ADDR, tq_bootstrap,
                              sizeof(tq_bootstrap) / sizeof(tq_bootstrap[0])) != 0) {
            fprintf(stderr, "boottq destination is outside RAM\n");
            r.fini(&r);
            return 1;
        }
        bus_write16(TQ_BOOT_UNIT, 0000000u);
        bus_write16(TQ_BOOT_CSR, 0174500u);
        r.r[7] = TQ_BOOT_ENTRY;
    }

    if (load_path) {
        FILE *fload = fopen(load_path, "rb");
        if (!fload) {
            fprintf(stderr, "Cannot open file: %s\n", load_path);
            r.fini(&r);
            return 1;
        }

        if (fseek(fload, 0, SEEK_END) != 0) {
            fprintf(stderr, "Seek failed: %s\n", load_path);
            fclose(fload);
            r.fini(&r);
            return 1;
        }
        long fsize = ftell(fload);
        fseek(fload, 0, SEEK_SET);

        if (load_addr < 0 || fsize < 0 ||
                !bus_range_is_ram((paddr_t)load_addr, (size_t)fsize)) {
            fprintf(stderr, "Load address/size out of RAM bounds\n");
            fclose(fload);
            r.fini(&r);
            return 1;
        }

        {
            uint8_t *dst = bus_ram_ptr((paddr_t)load_addr);
            if (!dst) {
                fprintf(stderr, "Load destination is outside RAM\n");
                fclose(fload);
                r.fini(&r);
                return 1;
            }
            if (fread(dst, 1, fsize, fload) != (size_t)fsize) {
                fprintf(stderr, "Read failed: %s\n", load_path);
                fclose(fload);
                r.fini(&r);
                return 1;
            }
        }
        fclose(fload);
        fprintf(stderr, "Loaded %ld bytes from %s to octal %lo\n", fsize, load_path,
                load_addr);
    }

    if (start_pc >= 0) {
        r.r[7] = (uint16_t)start_pc;
        fprintf(stderr, "Set PC to octal %lo\n", start_pc);
    }

    /* -------- main emulation loop --------
       Replace cpu_step(&r) with your core's actual stepping API. */
    long steps_done = 0;
    for (;;) {
        if (max_steps == 0) {
            break;
        }
        /* Keep device service latency low so boot ROM wait loops do not stall. */
        int step_chunk = trace ? 1 : 64;
        int steps_executed = 0;
        if (max_steps > 0 && step_chunk > max_steps) {
            step_chunk = (int)max_steps;
        }
        for (int k = 0; k < step_chunk; k++) {
            bus_iowin_sync_from_cpu(&r, machine_kind);
            int trace_step = trace &&
                             (trace_after < 0 || steps_done >= trace_after);
            if (trace_step) {
                char buf[128];
                word start_pc = r.r[7];
                word tmp = start_pc;
                disas(&r, &tmp, buf);
                fprintf(stderr, "%06o ", start_pc);
                int i = 0;
                for (word a = start_pc; a < tmp; a += 2) {
                    fprintf(stderr, "%06o ", r.load_word(&r, a));
                    i++;
                }
                while (i < 3) {
                    fprintf(stderr, "       ");
                    i++;
                }
                fprintf(stderr, "%s\n", buf);
                if (trace_loopvals && start_pc == 0002032) {
                    word v177766 = r.load_word(&r, 0177766);
                    word v177776 = r.load_word(&r, 0177776);
                    word v60542 = r.load_word(&r, 060542);
                    word v60560 = r.load_word(&r, 060560);
                    fprintf(stderr,
                            "LOOPVALS 177766=%06o 177776=%06o 60542=%06o 60560=%06o\n",
                            v177766, v177776, v60542, v60560);
                }
                if (trace_loopvals && start_pc == 0002214) {
                    word sp = r.r[6];
                    word stk_pc = r.load_word(&r, sp);
                    word stk_ps = r.load_word(&r, (word)(sp + 2));
                    fprintf(stderr, "LOOPSP  SP=%06o (SP)=%06o (SP+2)=%06o\n",
                            sp, stk_pc, stk_ps);
                }
                if (trace_regs) {
                    fprintf(stderr,
                            "R0=%06o R1=%06o R2=%06o R3=%06o R4=%06o R5=%06o SP=%06o PS=%06o\n",
                            r.r[0], r.r[1], r.r[2], r.r[3], r.r[4], r.r[5], r.r[6],
                            r.psw);
                }
            }
            /* TODO: replace with your core single-instruction executor */
            core_step(&r); /* must exist in your core */
            steps_done++;
            steps_executed++;
            if (max_steps > 0) {
                max_steps--;
            }
            if (r.fAbort) {
                break;
            }
            if (max_steps == 0) {
                break;
            }
        }

        ubmap_sync_from_cpu(&r, machine_kind);
        lsi11_poll_devices_steps((uint32_t)steps_executed);

        if (r.fAbort) {
            if (exit_on_abort) {
                break;
            }
            /* RT-11 and some monitor code may use HALT/abort vector path. */
            r.fAbort = 0;
        }
    }

    r.fini(&r);
    tq11_close_image();
    rl11_close_image();
    rh11_close_image();
    rk11_close_image();
    return 0;
}
