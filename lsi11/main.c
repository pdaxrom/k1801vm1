#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adapter_core.h"
#include "bus.h"
#include "dev_dl11.h"
#include "dev_rl11.h"
#include "dev_rh11.h"
#include "dev_rk11.h"
#include "dev_sr.h"
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
    if (!strcmp(name, "k1801vm1g") || !strcmp(name, "vm1g")) {
        *model = K1801VM1G;
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

static const char *cpu_model_name(byte model)
{
    switch (model) {
    case K1801VM1:
        return "k1801vm1";
    case K1801VM1G:
        return "k1801vm1g";
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
            "[-bootcopy|-bootrt11] [-cpu <model>]\n"
            "\n"
            "Target profile:\n"
            "  %s\n"
            "\n"
            "Options:\n"
#if defined(LSI11_TARGET_PDP1184)
            "  -cpu <model>    CPU model: dcj11 (default), 11/03, 11/84, 11/34, "
            "k1801vm1, k1801vm1g, k1801vm2, k1806vm2\n"
#else
            "  -cpu <model>    CPU model: dcj11 (default), 11/03, "
            "k1801vm1, k1801vm1g, k1801vm2, k1806vm2\n"
#endif
            "  -force-fis      Enable FIS for unsupported CPUs\n"
            "  -force-fp11     Enable FP11-A for unsupported CPUs\n"
            "  -rk <path>      Attach RK05 image\n"
            "  -rh <path>      Attach RH11 (RK06/RK07) image\n"
            "  -rl <path>      Attach RL image (auto detect RL01/RL02)\n"
            "  -rl01 <path>    Attach image as RL01\n"
            "  -rl02 <path>    Attach image as RL02\n"
            "  -disable-dl     Disable DL11\n"
            "  -disable-kw     Disable KW11\n"
            "  -disable-lp     Disable LP11\n"
            "  -disable-rk     Disable RK11\n"
            "  -disable-rh     Disable RH11\n"
            "  -disable-rl     Disable RL11\n"
            "  -disable-sr     Disable SR\n"
            "  -bootcopy       Copy first 010000 bytes from RK/RH/RL image into RAM at "
            "000000\n"
            "  -bootrt11       RK/RH: copy first 01000 bytes (or 2nd block if "
            "empty); RL: run RL bootstrap\n"
            "  -trace          Trace each instruction\n"
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
    const char *rk_path = NULL;
    const char *rh_path = NULL;
    const char *rl_path = NULL;
    int rl_type = RL11_TYPE_AUTO;
    const char *load_path = NULL;
    int do_bootcopy = 0;
    int do_bootrt11 = 0;
    long load_addr = 0;
    long start_pc = -1;
    long sr_value = -1;
    long ram_kb_arg = -1;
    int force_dl11_alias = -1;
    int disable_dl = 0;
    int disable_kw = 0;
    int disable_lp = 0;
    int disable_rk = 0;
    int disable_rh = 0;
    int disable_rl = 0;
    int disable_sr = 0;
#if defined(LSI11_TARGET_PDP1184)
    byte cpu_model = DCJ11;
#else
    byte cpu_model = DCJ11;
#endif
    int force_fis = 0;
    int force_fp11 = 0;
    int trace = 0;
    int trace_regs = 0;
    long max_steps = -1;
    int dl11_8bit = 0;
    int do_nl_to_cr = 0;
    int exit_on_abort = 0;
    int check_config_only = 0;
    char cfg_err[160] = {0};

#if defined(LSI11_TARGET_PDP1184)
    const lsi11_machine_t machine_kind = LSI11_MACHINE_1184;
#else
    const lsi11_machine_t machine_kind = LSI11_MACHINE_1104;
#endif

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-rk") && i + 1 < argc) {
            rk_path = argv[++i];
        } else if (!strcmp(argv[i], "-rh") && i + 1 < argc) {
            rh_path = argv[++i];
        } else if (!strcmp(argv[i], "-rl") && i + 1 < argc) {
            rl_path = argv[++i];
            rl_type = RL11_TYPE_AUTO;
        } else if (!strcmp(argv[i], "-rl01") && i + 1 < argc) {
            rl_path = argv[++i];
            rl_type = RL11_TYPE_RL01;
        } else if (!strcmp(argv[i], "-rl02") && i + 1 < argc) {
            rl_path = argv[++i];
            rl_type = RL11_TYPE_RL02;
        } else if (!strcmp(argv[i], "-disable-dl")) {
            disable_dl = 1;
        } else if (!strcmp(argv[i], "-disable-kw")) {
            disable_kw = 1;
        } else if (!strcmp(argv[i], "-disable-lp")) {
            disable_lp = 1;
        } else if (!strcmp(argv[i], "-disable-rk")) {
            disable_rk = 1;
        } else if (!strcmp(argv[i], "-disable-rh")) {
            disable_rh = 1;
        } else if (!strcmp(argv[i], "-disable-rl")) {
            disable_rl = 1;
        } else if (!strcmp(argv[i], "-disable-sr")) {
            disable_sr = 1;
        } else if (!strcmp(argv[i], "-bootcopy")) {
            do_bootcopy = 1;
        } else if (!strcmp(argv[i], "-bootrt11")) {
            do_bootrt11 = 1;
        } else if (!strcmp(argv[i], "-traceirq")) {
            lsi11_set_trace_irq(1);
        } else if (!strcmp(argv[i], "-tracenxm")) {
            lsi11_set_trace_nxm(1);
        } else if (!strcmp(argv[i], "-trace")) {
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

    if (cpu_model == K1801VM1 || cpu_model == K1801VM1G) {
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
    if (disable_sr &&
            lsi11_set_device_enabled("sr", 0, cfg_err, sizeof(cfg_err)) != 0) {
        fprintf(stderr, "Device configuration error: %s\n", cfg_err);
        return 2;
    }
    if (rk_path && !lsi11_device_enabled("rk11")) {
        fprintf(stderr, "-rk is not allowed with -disable-rk\n");
        return 2;
    }
    if (rh_path && !lsi11_device_enabled("rh11")) {
        fprintf(stderr, "-rh is not allowed with -disable-rh\n");
        return 2;
    }
    if (rl_path && !lsi11_device_enabled("rl11")) {
        fprintf(stderr, "-rl/-rl01/-rl02 is not allowed with -disable-rl\n");
        return 2;
    }

    if (check_config_only) {
        const char *m = (lsi11_machine_current() == LSI11_MACHINE_1184) ? "pdp1184"
                        : "lsi11";
        int rh11_on = lsi11_device_enabled("rh11");
        fprintf(stderr,
                "CONFIG machine=%s cpu=%s ram_kb=%u dl11_alias=%d rh11=%d "
                "dev_dl=%d dev_kw=%d dev_lp=%d dev_rk=%d dev_rh=%d dev_rl=%d "
                "dev_sr=%d dev_vm1sel=%d dev_vm1sav=%d\n",
                m, cpu_model_name(cpu_model), lsi11_machine_ram_kb(),
                lsi11_dl11_alias(), rh11_on, lsi11_device_enabled("dl11"),
                lsi11_device_enabled("kw11"), lsi11_device_enabled("lp11"),
                lsi11_device_enabled("rk11"), lsi11_device_enabled("rh11"),
                lsi11_device_enabled("rl11"), lsi11_device_enabled("sr"),
                lsi11_device_enabled("vm1sel"), lsi11_device_enabled("vm1sav"));
        return 0;
    }

    regs r;
    memset(&r, 0, sizeof(r));
    r.model = cpu_model;

    dl11_set_8bit(dl11_8bit);
    dl11_set_nl_to_cr(do_nl_to_cr);
    lsi11_hw_connect(&r);

    /* core init will init devices etc. */
    if (r.init(&r) != 0) {
        fprintf(stderr, "init failed\n");
        return 1;
    }

    /* Attach RK image if provided */
    if (rk_path) {
        if (rk11_open_image(rk_path) != 0) {
            fprintf(stderr, "rk11_open_image failed: %s\n", rk_path);
            r.fini(&r);
            return 1;
        }
    }
    if (rh_path) {
        if (rh11_open_image(rh_path) != 0) {
            fprintf(stderr, "rh11_open_image failed: %s\n", rh_path);
            rk11_close_image();
            r.fini(&r);
            return 1;
        }
    }
    if (rl_path) {
        if (rl11_open_image_typed(rl_path, rl_type) != 0) {
            fprintf(stderr, "rl11_open_image failed: %s\n", rl_path);
            rh11_close_image();
            rk11_close_image();
            r.fini(&r);
            return 1;
        }
    }

    core_init(&r);

    if (force_fis) {
        r.has_fis = 1;
    }
    if (force_fp11) {
        r.has_fpu = 1;
    }

    core_reset(&r);

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
        if (rk_path) {
            rc = rk11_boot_copy(ram0, n);
        } else if (rh_path) {
            rc = rh11_boot_copy(ram0, n);
        } else if (rl_path) {
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
        if (rl_path) {
            uint8_t *ram0 = bus_ram_ptr(RL_BOOT_ADDR);
            size_t n = sizeof(rl_bootstrap);
            if (!ram0 || !bus_range_is_ram(RL_BOOT_ADDR, n)) {
                fprintf(stderr, "bootrt11 destination is outside RAM\n");
                r.fini(&r);
                return 1;
            }
            for (size_t i = 0; i < (sizeof(rl_bootstrap) / sizeof(rl_bootstrap[0]));
                    i++) {
                uint16_t w = rl_bootstrap[i];
                ram0[i * 2 + 0] = (uint8_t)(w & 000377);
                ram0[i * 2 + 1] = (uint8_t)((w >> 8) & 000377);
            }
            r.r[7] = RL_BOOT_ENTRY;
        } else {
            const char *boot_path = rk_path ? rk_path : (rh_path ? rh_path : rl_path);
            if (!boot_path) {
                fprintf(stderr,
                        "-bootrt11 requires -rk <image>, -rh <image>, or -rl <image>\n");
                r.fini(&r);
                return 1;
            }
            FILE *f = fopen(boot_path, "rb");
            if (!f) {
                fprintf(stderr, "Cannot open boot image: %s\n", boot_path);
                r.fini(&r);
                return 1;
            }
            const size_t n = 01000;
            uint8_t buf[01000];
            size_t got = fread(buf, 1, n, f);
            int all_zero = 1;
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
                fprintf(stderr, "RT11 boot block not found in image\n");
                r.fini(&r);
                return 1;
            }
            {
                uint8_t *ram0 = bus_ram_ptr(0);
                if (!ram0 || !bus_range_is_ram(0, n)) {
                    fprintf(stderr, "bootrt11 destination is outside RAM\n");
                    r.fini(&r);
                    return 1;
                }
                memcpy(ram0, buf, n);
            }
            r.r[7] = 000000;
        }
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
    for (;;) {
        if (max_steps == 0) {
            break;
        }
        /* Keep device service latency low so boot ROM wait loops do not stall. */
        int step_chunk = trace ? 1 : 64;
        if (max_steps > 0 && step_chunk > max_steps) {
            step_chunk = (int)max_steps;
        }
        for (int k = 0; k < step_chunk; k++) {
            if (trace) {
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
                if (trace_regs) {
                    fprintf(stderr,
                            "R0=%06o R1=%06o R2=%06o R3=%06o R4=%06o R5=%06o SP=%06o PS=%06o\n",
                            r.r[0], r.r[1], r.r[2], r.r[3], r.r[4], r.r[5], r.r[6],
                            r.psw);
                }
            }
            /* TODO: replace with your core single-instruction executor */
            core_step(&r); /* must exist in your core */
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

        lsi11_poll_devices();

        if (r.fAbort) {
            if (exit_on_abort) {
                break;
            }
            /* RT-11 and some monitor code may use HALT/abort vector path. */
            r.fAbort = 0;
        }
    }

    r.fini(&r);
    rl11_close_image();
    rh11_close_image();
    rk11_close_image();
    return 0;
}
