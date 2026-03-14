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
#include "dev_xp.h"
#include "dev_rk11.h"
#include "dev_sr.h"
#include "dev_tq11.h"
#include "ubmap.h"
#include "dev_vm1sel.h"
#include "dev_vm1sav.h"
#include "options.h"
#include "util_term.h"

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
#define VM2_TX_IMM(ch) 0112711u, (uint16_t)(uint8_t)(ch)

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

#define MMR3_BME 0000040u
#define MMR3_M22E 0000020u
#define MMR0_RELO_ENABLE 0000001u

static void ubmap_sync_from_cpu(const regs *r, lsi11_machine_t machine)
{
    int enabled = 0;

    if (!r || machine != LSI11_MACHINE_1184 || r->model != DCJ11) {
        enabled = 0;
        ubmap_set_enabled(0);
    } else {
#if defined(ENABLE_MMU) && (ENABLE_MMU)
        enabled = (r->mmu_ssr3 & MMR3_BME) ? 1 : 0;
#else
        enabled = 0;
#endif
        ubmap_set_enabled(enabled);
    }
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

int main(int argc, char **argv)
{
    lsi11_options_t opts;
    int trace_loopvals = 0;
    char cfg_err[160] = {0};

#if defined(LSI11_TARGET_PDP1184)
    const lsi11_machine_t machine_kind = LSI11_MACHINE_1184;
#else
    const lsi11_machine_t machine_kind = LSI11_MACHINE_1104;
#endif

    options_init(&opts);
    if (options_parse(&opts, argc, argv) != 0) {
        return 2;
    }

    if (opts.trace_irq) {
        lsi11_set_trace_irq(1);
    }
    if (opts.trace_nxm) {
        lsi11_set_trace_nxm(1);
    }

    if (util_term_set_socket_path(opts.socket_path, cfg_err, sizeof(cfg_err)) != 0) {
        fprintf(stderr, "Terminal configuration error: %s\n", cfg_err);
        return 2;
    }

    if (rh11_set_mode(opts.rh_mode) != 0) {
        fprintf(stderr, "RH11 mode configuration error\n");
        return 2;
    }

    trace_loopvals = (getenv("LSI11_TRACE_LOOPVALS") != NULL) ? 1 : 0;

#if !defined(LSI11_TARGET_PDP1184)
    if (opts.ram_kb_arg >= 0) {
        fprintf(stderr,
                "This lsi11 target is fixed 56KB RAM; -ram is not supported.\n");
        return 2;
    }
#endif

    if (opts.ram_kb_arg < 0) {
        opts.ram_kb_arg = 0;
    }

    if (lsi11_machine_configure(machine_kind, (uint32_t)opts.ram_kb_arg, cfg_err,
                                sizeof(cfg_err)) != 0) {
        fprintf(stderr, "Machine configuration error: %s\n", cfg_err);
        return 2;
    }
    if (cpu_is_vm2(opts.cpu_model)) {
        if (bus_vm2_configure(BUS_VM2_DEFAULT_USER_RAM_BYTES,
                              VM2_MIN_HALT_RAM_BYTES, cfg_err,
                              sizeof(cfg_err)) != 0) {
            fprintf(stderr, "VM2 configuration error: %s\n", cfg_err);
            return 2;
        }
    }
    if (opts.force_dl11_alias >= 0) {
        lsi11_set_dl11_alias(opts.force_dl11_alias);
    }
    if (opts.dz_port_set &&
            dz11_set_listen_port((int)opts.dz_port, cfg_err, sizeof(cfg_err)) != 0) {
        fprintf(stderr, "DZ11 configuration error: %s\n", cfg_err);
        return 2;
    }

    {
        int kw11_l_on = 1;
        int kw11_p_on = 0;

        if (opts.kw11_l_override >= 0) {
            kw11_l_on = opts.kw11_l_override;
        }
        if (opts.kw11_p_override >= 0) {
            kw11_p_on = opts.kw11_p_override;
        }
        kw11_set_visibility(kw11_l_on, kw11_p_on);
        if (getenv("LSI11_KW11_REALTIME") != NULL) {
            kw11_set_step_clock(0);
        } else {
            kw11_set_step_clock(1);
        }
    }

    if (opts.cpu_model == K1801VM1) {
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

    if (opts.disable_dl &&
            lsi11_set_device_enabled("dl11", 0, cfg_err, sizeof(cfg_err)) != 0) {
        fprintf(stderr, "Device configuration error: %s\n", cfg_err);
        return 2;
    }
    if (opts.disable_dz &&
            lsi11_set_device_enabled("dz11", 0, cfg_err, sizeof(cfg_err)) != 0) {
        fprintf(stderr, "Device configuration error: %s\n", cfg_err);
        return 2;
    }
    if (opts.disable_kw &&
            lsi11_set_device_enabled("kw11", 0, cfg_err, sizeof(cfg_err)) != 0) {
        fprintf(stderr, "Device configuration error: %s\n", cfg_err);
        return 2;
    }
    if (opts.disable_lp &&
            lsi11_set_device_enabled("lp11", 0, cfg_err, sizeof(cfg_err)) != 0) {
        fprintf(stderr, "Device configuration error: %s\n", cfg_err);
        return 2;
    }
    if (opts.disable_rk &&
            lsi11_set_device_enabled("rk11", 0, cfg_err, sizeof(cfg_err)) != 0) {
        fprintf(stderr, "Device configuration error: %s\n", cfg_err);
        return 2;
    }
    if (opts.disable_rh &&
            lsi11_set_device_enabled("rh11", 0, cfg_err, sizeof(cfg_err)) != 0) {
        fprintf(stderr, "Device configuration error: %s\n", cfg_err);
        return 2;
    }
    if (opts.disable_xp &&
            lsi11_set_device_enabled("xp11", 0, cfg_err, sizeof(cfg_err)) != 0) {
        fprintf(stderr, "Device configuration error: %s\n", cfg_err);
        return 2;
    }
    if (opts.disable_rl &&
            lsi11_set_device_enabled("rl11", 0, cfg_err, sizeof(cfg_err)) != 0) {
        fprintf(stderr, "Device configuration error: %s\n", cfg_err);
        return 2;
    }
    if (opts.disable_tq &&
            lsi11_set_device_enabled("tq11", 0, cfg_err, sizeof(cfg_err)) != 0) {
        fprintf(stderr, "Device configuration error: %s\n", cfg_err);
        return 2;
    }
    if (opts.tq_count > 0 && !opts.disable_tq &&
            lsi11_set_device_enabled("tq11", 1, cfg_err, sizeof(cfg_err)) != 0) {
        fprintf(stderr, "Device configuration error: %s\n", cfg_err);
        return 2;
    }
    if (opts.xp_count > 0 && !opts.disable_xp &&
            lsi11_set_device_enabled("xp11", 1, cfg_err, sizeof(cfg_err)) != 0) {
        fprintf(stderr, "Device configuration error: %s\n", cfg_err);
        return 2;
    }
    if (opts.disable_sr &&
            lsi11_set_device_enabled("sr", 0, cfg_err, sizeof(cfg_err)) != 0) {
        fprintf(stderr, "Device configuration error: %s\n", cfg_err);
        return 2;
    }
    if (opts.dz_port_set && !opts.disable_dz &&
            lsi11_set_device_enabled("dz11", 1, cfg_err, sizeof(cfg_err)) != 0) {
        fprintf(stderr, "Device configuration error: %s\n", cfg_err);
        return 2;
    }
    if (opts.rk_count > 0 && !lsi11_device_enabled("rk11")) {
        fprintf(stderr, "-rk is not allowed with -disable-rk\n");
        return 2;
    }
    if (opts.dz_port_set && (opts.dz_port > 65535 || opts.dz_port <= 0)) {
        fprintf(stderr, "-dz requires TCP port in range 1..65535\n");
        return 2;
    }
    if (opts.dz_port_set && !lsi11_device_enabled("dz11")) {
        fprintf(stderr, "-dz is not allowed with -disable-dz\n");
        return 2;
    }
    if (opts.rh_count > 0 && !lsi11_device_enabled("rh11")) {
        fprintf(stderr, "-rh is not allowed with -disable-rh\n");
        return 2;
    }
    if (opts.xp_count > 0 && !lsi11_device_enabled("xp11")) {
        fprintf(stderr, "-xp/-rp is not allowed with -disable-xp\n");
        return 2;
    }
    if (opts.rl_count > 0 && !lsi11_device_enabled("rl11")) {
        fprintf(stderr, "-rl/-rl01/-rl02 is not allowed with -disable-rl\n");
        return 2;
    }
    if (opts.tq_count > 0 && !lsi11_device_enabled("tq11")) {
        fprintf(stderr, "-tq is not allowed with -disable-tq\n");
        return 2;
    }
    if (opts.do_boottq && opts.tq_count == 0) {
        fprintf(stderr, "-boottq requires -tq <image>\n");
        return 2;
    }
    if (opts.do_boot && (opts.do_bootcopy || opts.do_bootrt11 || opts.do_boottq)) {
        fprintf(stderr, "-boot cannot be combined with -bootcopy/-bootrt11/-boottq\n");
        return 2;
    }
    if (opts.do_boot) {
        switch (opts.boot_kind) {
        case BOOT_DEV_RK:
            if (!lsi11_device_enabled("rk11")) {
                fprintf(stderr, "-boot rk* requires RK11 enabled\n");
                return 2;
            }
            if (opts.boot_unit >= opts.rk_count || !opts.rk_path[opts.boot_unit]) {
                fprintf(stderr, "-boot rk%o requires matching -rk attachment\n",
                        opts.boot_unit);
                return 2;
            }
            break;
        case BOOT_DEV_RH:
            if (!lsi11_device_enabled("rh11")) {
                fprintf(stderr, "-boot rh*/hk* requires RH11 enabled\n");
                return 2;
            }
            if (opts.boot_unit >= opts.rh_count || !opts.rh_path[opts.boot_unit]) {
                fprintf(stderr, "-boot rh%o requires matching -rh attachment\n",
                        opts.boot_unit);
                return 2;
            }
            break;
        case BOOT_DEV_RL:
            if (!lsi11_device_enabled("rl11")) {
                fprintf(stderr, "-boot rl* requires RL11 enabled\n");
                return 2;
            }
            if (opts.boot_unit >= opts.rl_count || !opts.rl_path[opts.boot_unit].path) {
                fprintf(stderr, "-boot rl%o requires matching -rl attachment\n",
                        opts.boot_unit);
                return 2;
            }
            break;
        case BOOT_DEV_TQ:
            if (!lsi11_device_enabled("tq11")) {
                fprintf(stderr, "-boot tq* requires TQ11 enabled\n");
                return 2;
            }
            if (opts.boot_unit >= opts.tq_count || !opts.tq_path[opts.boot_unit]) {
                fprintf(stderr, "-boot tq%o requires matching -tq attachment\n",
                        opts.boot_unit);
                return 2;
            }
            break;
        default:
            fprintf(stderr, "Internal error: unknown boot device\n");
            return 2;
        }
    }

    if (opts.check_config_only) {
        const char *m = (lsi11_machine_current() == LSI11_MACHINE_1184) ? "pdp1184"
                        : "lsi11";
        int rh11_on = lsi11_device_enabled("rh11");
        int kw11_master = lsi11_device_enabled("kw11");
        int kw11_l_on = kw11_master ? kw11_l_enabled() : 0;
        int kw11_p_on = kw11_master ? kw11_p_enabled() : 0;
        fprintf(stderr,
                "CONFIG machine=%s cpu=%s ram_kb=%u dl11_alias=%d rh11=%d rh_mode=%s "
                "dev_dl=%d dev_kw=%d dev_kw11_l=%d dev_kw11_p=%d "
                "dev_dz=%d dev_lp=%d dev_rk=%d dev_rh=%d dev_xp=%d dev_rl=%d dev_tq=%d dev_sr=%d "
                "dev_vm1sel=%d dev_vm1sav=%d\n",
                m, cpu_model_name(opts.cpu_model), lsi11_machine_ram_kb(),
                lsi11_dl11_alias(), rh11_on, rh11_mode_name(rh11_get_mode()),
                lsi11_device_enabled("dl11"),
                kw11_master, kw11_l_on, kw11_p_on,
                lsi11_device_enabled("dz11"), lsi11_device_enabled("lp11"),
                lsi11_device_enabled("rk11"),
                lsi11_device_enabled("rh11"), lsi11_device_enabled("xp11"),
                lsi11_device_enabled("rl11"),
                lsi11_device_enabled("tq11"),
                lsi11_device_enabled("sr"), lsi11_device_enabled("vm1sel"),
                lsi11_device_enabled("vm1sav"));
        return 0;
    }

    regs r;
    memset(&r, 0, sizeof(r));
    r.model = opts.cpu_model;

    dl11_set_8bit(opts.dl11_8bit);
    dl11_set_nl_to_cr(opts.do_nl_to_cr);
    dz11_set_8bit(opts.dl11_8bit);
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
    for (int unit = 0; unit < opts.rk_count; unit++) {
        if (rk11_open_image_unit((unsigned)unit, opts.rk_path[unit]) != 0) {
            fprintf(stderr, "rk11_open_image failed: rk%o %s\n", unit,
                    opts.rk_path[unit]);
            r.fini(&r);
            return 1;
        }
    }
    for (int unit = 0; unit < opts.rh_count; unit++) {
        if (rh11_open_image_unit((unsigned)unit, opts.rh_path[unit]) != 0) {
            fprintf(stderr, "rh11_open_image failed: rh%o %s\n", unit,
                    opts.rh_path[unit]);
            rk11_close_image();
            r.fini(&r);
            return 1;
        }
    }
    for (int unit = 0; unit < opts.xp_count; unit++) {
        if (xp_open_image_unit((unsigned)unit, opts.xp_path[unit]) != 0) {
            fprintf(stderr, "xp_open_image failed: xp%o %s\n", unit,
                    opts.xp_path[unit]);
            rh11_close_image();
            rk11_close_image();
            r.fini(&r);
            return 1;
        }
    }
    for (int unit = 0; unit < opts.rl_count; unit++) {
        if (rl11_open_image_typed_unit((unsigned)unit, opts.rl_path[unit].path,
                                       opts.rl_path[unit].type) != 0) {
            fprintf(stderr, "rl11_open_image failed: rl%o %s\n", unit,
                    opts.rl_path[unit].path);
            xp_close_image();
            rh11_close_image();
            rk11_close_image();
            r.fini(&r);
            return 1;
        }
    }
    for (int unit = 0; unit < opts.tq_count; unit++) {
        if (tq11_open_image_unit((unsigned)unit, opts.tq_path[unit]) != 0) {
            fprintf(stderr, "tq11_open_image failed: tq%o %s\n", unit,
                    opts.tq_path[unit]);
            rl11_close_image();
            xp_close_image();
            rh11_close_image();
            rk11_close_image();
            r.fini(&r);
            return 1;
        }
    }

    if (opts.force_fis) {
        r.has_fis = 1;
    }
    if (opts.force_fp11) {
        r.has_fpu = 1;
    }

    core_reset(&r);
    bus_iowin_sync_from_cpu(&r, machine_kind);
    ubmap_sync_from_cpu(&r, machine_kind);

    if (opts.sr_value >= 0) {
        sr_set((uint16_t)opts.sr_value);
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

    if (opts.do_boot) {
        switch (opts.boot_kind) {
        case BOOT_DEV_RK:
            if (preload_rt11_boot_block(opts.rk_path[opts.boot_unit]) != 0) {
                fprintf(stderr, "RT11 boot block not found in rk%o image\n",
                        opts.boot_unit);
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
            bus_write16((uint16_t)(RK_BOOT_ADDR + 000010u),
                        (uint16_t)opts.boot_unit);
            r.r[7] = RK_BOOT_ENTRY;
            break;

        case BOOT_DEV_RH:
            if (preload_rt11_boot_block(opts.rh_path[opts.boot_unit]) != 0) {
                fprintf(stderr, "RT11 boot block not found in rh%o image\n",
                        opts.boot_unit);
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
            bus_write16((uint16_t)(RH_BOOT_ADDR + 000010u),
                        (uint16_t)opts.boot_unit);
            r.r[7] = RH_BOOT_ENTRY;
            break;

        case BOOT_DEV_RL: {
            uint16_t ds = (uint16_t)((opts.boot_unit & 03) << 8);

            if (install_bootstrap(RL_BOOT_ADDR, rl_bootstrap,
                                  sizeof(rl_bootstrap) / sizeof(rl_bootstrap[0])) !=
                    0) {
                fprintf(stderr, "boot destination is outside RAM\n");
                r.fini(&r);
                return 1;
            }
            bus_write16((uint16_t)(RL_BOOT_ADDR + 000014u),
                        (uint16_t)(0000004u | ds));
            /* Patch READ+GO command word (second immediate of MOV #14,(R1)). */
            bus_write16((uint16_t)(RL_BOOT_ADDR + 000042u),
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
            bus_write16(TQ_BOOT_UNIT, (uint16_t)opts.boot_unit);
            bus_write16(TQ_BOOT_CSR, 0174500u);
            r.r[7] = TQ_BOOT_ENTRY;
            break;

        default:
            fprintf(stderr, "Internal error: unknown boot device\n");
            r.fini(&r);
            return 1;
        }
    }

    if (opts.do_bootcopy) {
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
        if (opts.rk_count > 0) {
            rc = rk11_boot_copy(ram0, n);
        } else if (opts.rh_count > 0) {
            rc = rh11_boot_copy(ram0, n);
        } else if (opts.rl_count > 0) {
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

    if (opts.do_bootrt11) {
        if (opts.rl_count > 0) {
            if (install_bootstrap(RL_BOOT_ADDR, rl_bootstrap,
                                  sizeof(rl_bootstrap) / sizeof(rl_bootstrap[0])) !=
                    0) {
                fprintf(stderr, "bootrt11 destination is outside RAM\n");
                r.fini(&r);
                return 1;
            }
            r.r[7] = RL_BOOT_ENTRY;
        } else if (opts.rk_count > 0) {
            if (preload_rt11_boot_block(opts.rk_path[0]) != 0) {
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
        } else if (opts.rh_count > 0) {
            if (preload_rt11_boot_block(opts.rh_path[0]) != 0) {
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

    if (opts.do_boottq) {
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

    if (opts.load_path) {
        FILE *fload = fopen(opts.load_path, "rb");
        if (!fload) {
            fprintf(stderr, "Cannot open file: %s\n", opts.load_path);
            r.fini(&r);
            return 1;
        }

        if (fseek(fload, 0, SEEK_END) != 0) {
            fprintf(stderr, "Seek failed: %s\n", opts.load_path);
            fclose(fload);
            r.fini(&r);
            return 1;
        }
        long fsize = ftell(fload);
        fseek(fload, 0, SEEK_SET);

        if (opts.load_addr < 0 || fsize < 0 ||
                !bus_range_is_ram((paddr_t)opts.load_addr, (size_t)fsize)) {
            fprintf(stderr, "Load address/size out of RAM bounds\n");
            fclose(fload);
            r.fini(&r);
            return 1;
        }

        {
            uint8_t *dst = bus_ram_ptr((paddr_t)opts.load_addr);
            if (!dst) {
                fprintf(stderr, "Load destination is outside RAM\n");
                fclose(fload);
                r.fini(&r);
                return 1;
            }
            if (fread(dst, 1, fsize, fload) != (size_t)fsize) {
                fprintf(stderr, "Read failed: %s\n", opts.load_path);
                fclose(fload);
                r.fini(&r);
                return 1;
            }
        }
        fclose(fload);
        fprintf(stderr, "Loaded %ld bytes from %s to octal %lo\n", fsize,
                opts.load_path, opts.load_addr);
    }

    if (opts.start_pc >= 0) {
        r.r[7] = (uint16_t)opts.start_pc;
        fprintf(stderr, "Set PC to octal %lo\n", opts.start_pc);
    }

    /* -------- main emulation loop --------
       Replace cpu_step(&r) with your core's actual stepping API. */
    long steps_done = 0;
    for (;;) {
        if (opts.max_steps == 0) {
            break;
        }
        /* Keep device service latency low so boot ROM wait loops do not stall. */
        int step_chunk = opts.trace ? 1 : 64;
        int steps_executed = 0;
        if (opts.max_steps > 0 && step_chunk > opts.max_steps) {
            step_chunk = (int)opts.max_steps;
        }
        for (int k = 0; k < step_chunk; k++) {
            bus_iowin_sync_from_cpu(&r, machine_kind);
            int trace_step = opts.trace &&
                             (opts.trace_after < 0 || steps_done >= opts.trace_after);
            if (trace_step) {
                char buf[128];
                word cur_pc = r.r[7];
                word tmp = cur_pc;
                disas(&r, &tmp, buf);
                fprintf(stderr, "%06o ", cur_pc);
                int i = 0;
                for (word a = cur_pc; a < tmp; a += 2) {
                    fprintf(stderr, "%06o ", r.load_word(&r, a));
                    i++;
                }
                while (i < 3) {
                    fprintf(stderr, "       ");
                    i++;
                }
                fprintf(stderr, "%s\n", buf);
                if (trace_loopvals && cur_pc == 0002032) {
                    word v177766 = r.load_word(&r, 0177766);
                    word v177776 = r.load_word(&r, 0177776);
                    word v60542 = r.load_word(&r, 060542);
                    word v60560 = r.load_word(&r, 060560);
                    fprintf(stderr,
                            "LOOPVALS 177766=%06o 177776=%06o 60542=%06o 60560=%06o\n",
                            v177766, v177776, v60542, v60560);
                }
                if (trace_loopvals && cur_pc == 0002214) {
                    word sp = r.r[6];
                    word stk_pc = r.load_word(&r, sp);
                    word stk_ps = r.load_word(&r, (word)(sp + 2));
                    fprintf(stderr, "LOOPSP  SP=%06o (SP)=%06o (SP+2)=%06o\n",
                            sp, stk_pc, stk_ps);
                }
                if (opts.trace_regs) {
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
            if (opts.max_steps > 0) {
                opts.max_steps--;
            }
            if (r.fAbort) {
                break;
            }
            if (opts.max_steps == 0) {
                break;
            }
        }

        ubmap_sync_from_cpu(&r, machine_kind);
        lsi11_poll_devices_steps((uint32_t)steps_executed);

        if (r.fAbort) {
            if (opts.exit_on_abort) {
                break;
            }
            /* RT-11 and some monitor code may use HALT/abort vector path. */
            r.fAbort = 0;
        }
    }

    r.fini(&r);
    tq11_close_image();
    rl11_close_image();
    xp_close_image();
    rh11_close_image();
    rk11_close_image();
    return 0;
}
