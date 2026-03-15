#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emu_file.h"
#include "options.h"

#if defined(LSI11_TARGET_1184)
#define LSI11_TARGET_PDP1184 1
#endif

void options_init(lsi11_options_t *opts)
{
    memset(opts, 0, sizeof(*opts));
    opts->start_pc = -1;
    opts->sr_value = -1;
    opts->ram_kb_arg = -1;
    opts->sys_clock_mhz = -1;
    opts->force_dl11_alias = -1;
    opts->kw11_l_override = -1;
    opts->kw11_p_override = -1;
    opts->dz_port = -1;
    opts->trace_after = -1;
    opts->max_steps = -1;
    opts->display_enable = -1;
    opts->rh_mode = RH11_MODE_RH11;
    opts->cpu_model = DCJ11;
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

static int parse_rh_mode(const char *name, rh11_mode_t *mode_out)
{
    if (!name || !mode_out) {
        return -1;
    }

    if (!strcmp(name, "rh11")) {
        *mode_out = RH11_MODE_RH11;
        return 0;
    }
    if (!strcmp(name, "rh70")) {
        *mode_out = RH11_MODE_RH70;
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
    } else if (!strncmp(name, "xp", 2) || !strncmp(name, "rp", 2)) {
        kind = BOOT_DEV_XP;
        suffix = name + 2;
        max_units = XP_MAX_DRIVES;
    } else if (!strncmp(name, "rq", 2) || !strncmp(name, "ra", 2)) {
        kind = BOOT_DEV_RQ;
        suffix = name + 2;
        max_units = RQ11_MAX_UNITS;
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

const char *cpu_model_name(byte model)
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

void options_usage(const char *argv0)
{
#if defined(LSI11_TARGET_PDP1184)
    const char *target = "pdp1184";
#else
    const char *target = "lsi11";
#endif

    fprintf(stderr,
            "Usage:\n"
            "  %s [-rk <rk05.img>] [-rh <rk06rk07.img>] [-rq <mscp.img>] [-xp <rm05.img>] [-rl <rl.img>] "
            "[-tq <tk50.tap>] "
            "[-boot <dev>|-bootcopy|-bootrt11|-boottq] [-cpu <model>]\n"
            "  %s @<options.file>\n"
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
            "  -freq <mhz>     RP2040 sys clock in MHz (pico-lsi11 only)\n"
            "  -clock <mhz>    Alias for -freq\n"
            "  -rk <path>      Attach RK05 image (repeatable: rk0,rk1,...)\n"
            "  -rh <path>      Attach RH11 (RK06/RK07) image (repeatable: rh0,rh1,...)\n"
            "  -rq <path>      Attach RQ (MSCP) disk image (repeatable: rq0,rq1,...)\n"
            "  -xp <path>      Attach XP/RP RM05 image (repeatable: xp0,xp1,...)\n"
            "  -rp <path>      Alias for -xp\n"
            "  -rh-mode <m>    RH controller mode: rh11 (default) | rh70\n"
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
            "  -disable-rq     Disable RQ (MSCP)\n"
            "  -disable-xp     Disable XP/RP controller\n"
            "  -disable-rl     Disable RL11\n"
            "  -disable-tq     Disable TQ11/TMSCP tape controller\n"
            "  -disable-sr     Disable SR\n"
            "  -boot <dev>     Boot selected unit: rkN|rhN|hkN|xpN|rpN|rqN|raN|rlN|tqN "
            "(N defaults to 0)\n"
            "  -bootcopy       Copy first 010000 bytes from RK/RH/RL image into RAM at "
            "000000\n"
            "  -bootrt11       Run built-in RK/RH/RL bootstrap for the selected "
            "controller\n"
            "  -boottq         Run built-in TQ/TMSCP bootstrap at 016000 for unit 0\n"
            "  -trace          Trace each instruction\n"
            "  -trace-after N  Start tracing only after N executed instructions\n"
            "  -traceregs      With -trace, also dump registers\n"
            "  -traceirq       Trace delivered IRQ vectors\n"
            "  -tracenxm       Trace NXM traps\n",
            argv0, argv0, target);
    fprintf(stderr,
            "  -socket <path>  Use UNIX socket instead of host stdin/stdout (lsi11 host)\n"
            "  -tty7b          DL11 console 7-bit mode (default)\n"
            "  -tty8b          DL11 console 8-bit mode\n"
            "  -display        Enable mirrored terminal display (default on when supported)\n"
            "  -no-display     Disable mirrored terminal display after boot logo\n"
            "  -nl-to-cr       Map host newline (\\n) to CR (\\r) for input\n"
            "  -exit-on-abort  Exit emulator on HALT/abort\n"
            "  -steps N        Emulate N steps then exit\n"
            "  -check-config   Validate machine config and exit\n"
            "  -load <file>    Load binary file into RAM\n"
            "  -addr <oct>     Load address (octal) for -load (default 0)\n"
            "  -pc <oct>       Set initial PC (R7) to octal address\n"
            "  -sr <oct>       Set SR switch register (0177570) value\n"
#if defined(LSI11_TARGET_PDP1184)
            "  -ram <kb>       RAM size in KB (default 4096, must be multiple of 4)\n"
            "  --mem-kb <kb>   Same as -ram (pdp1184 only)\n"
            "  -dl11-alias     Enable DL11 alias 0176500..0176507\n"
            "  -no-dl11-alias  Disable DL11 alias 0176500..0176507 (default)\n"
#else
            "  -dl11-alias     Keep DL11 alias enabled (default)\n"
            "  -no-dl11-alias  Disable DL11 alias (non-standard for this target)\n"
#endif
            "  @<file>         Load options from file (one option per line, # comments)\n");
}

/*
 * Load command-line options from a text file.
 * Format: one option per line, # comments, empty lines ignored.
 * Tokens on the same line are split by whitespace.
 * The returned argv array and backing buffer must be freed by the caller
 * (free argv, then free argv[-1] which is the buffer pointer stashed there).
 * In practice, path strings referenced by opts survive until program exit.
 */
static int load_options_file(const char *path, int *out_argc, char ***out_argv)
{
    emu_file_t *f;
    long size;
    char *buf;
    size_t got;
    int capacity = 64;
    char **argv;
    int argc = 0;
    char *line;

    f = emu_fopen(path, "r");
    if (!f) {
        return -1;
    }

    if (emu_fseek(f, 0, EMU_SEEK_END) != 0) {
        emu_fclose(f);
        return -1;
    }
    size = emu_ftell(f);
    if (size < 0 || emu_fseek(f, 0, EMU_SEEK_SET) != 0) {
        emu_fclose(f);
        return -1;
    }

    if (size <= 0) {
        emu_fclose(f);
        *out_argc = 0;
        *out_argv = NULL;
        return 0;
    }

    buf = malloc((size_t)size + 1);
    if (!buf) {
        emu_fclose(f);
        return -1;
    }

    got = emu_fread(buf, 1, (size_t)size, f);
    emu_fclose(f);
    buf[got] = '\0';

    argv = malloc((size_t)capacity * sizeof(char *));
    if (!argv) {
        free(buf);
        return -1;
    }

    line = buf;
    while (*line) {
        char *eol = line;
        char *p;

        while (*eol && *eol != '\n') {
            eol++;
        }
        if (*eol == '\n') {
            *eol++ = '\0';
        }

        /* strip trailing \r for Windows line endings */
        {
            size_t len = strlen(line);
            if (len > 0 && line[len - 1] == '\r') {
                line[len - 1] = '\0';
            }
        }

        p = line;
        while (*p == ' ' || *p == '\t') {
            p++;
        }

        if (*p == '\0' || *p == '#') {
            line = eol;
            continue;
        }

        while (*p) {
            char *start;

            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (!*p) {
                break;
            }

            start = p;
            while (*p && *p != ' ' && *p != '\t') {
                p++;
            }
            if (*p) {
                *p++ = '\0';
            }

            if (argc >= capacity - 1) {
                char **new_argv;
                capacity *= 2;
                new_argv = realloc(argv, (size_t)capacity * sizeof(char *));
                if (!new_argv) {
                    free(argv);
                    /* buf intentionally leaked — strings may be referenced */
                    return -1;
                }
                argv = new_argv;
            }
            argv[argc++] = start;
        }

        line = eol;
    }

    argv[argc] = NULL;

    *out_argc = argc;
    *out_argv = argv;
    return 0;
}

static int parse_arg_list(lsi11_options_t *opts, int argc, char **argv,
                          const char *argv0)
{
    for (int i = 0; i < argc; i++) {
        if (argv[i][0] == '@') {
            int file_argc = 0;
            char **file_argv = NULL;
            int rc;

            if (argv[i][1] == '\0') {
                fprintf(stderr, "Missing filename after @\n");
                return -1;
            }
            if (load_options_file(argv[i] + 1, &file_argc, &file_argv) != 0) {
                fprintf(stderr, "Cannot open options file: %s\n", argv[i] + 1);
                return -1;
            }
            if (file_argc > 0) {
                rc = parse_arg_list(opts, file_argc, file_argv, argv0);
                free(file_argv);
                if (rc != 0) {
                    return rc;
                }
            }
        } else if (!strcmp(argv[i], "-rk") && i + 1 < argc) {
            if (opts->rk_count >= RK11_MAX_DRIVES) {
                fprintf(stderr, "Too many -rk images (max %d)\n", RK11_MAX_DRIVES);
                return -1;
            }
            opts->rk_path[opts->rk_count++] = argv[++i];
        } else if (!strcmp(argv[i], "-rh") && i + 1 < argc) {
            if (opts->rh_count >= RH11_MAX_DRIVES) {
                fprintf(stderr, "Too many -rh images (max %d)\n", RH11_MAX_DRIVES);
                return -1;
            }
            opts->rh_path[opts->rh_count++] = argv[++i];
        } else if (!strcmp(argv[i], "-rq") && i + 1 < argc) {
            if (opts->rq_count >= RQ11_MAX_UNITS) {
                fprintf(stderr, "Too many -rq images (max %d)\n", RQ11_MAX_UNITS);
                return -1;
            }
            opts->rq_path[opts->rq_count++] = argv[++i];
        } else if ((!strcmp(argv[i], "-xp") || !strcmp(argv[i], "-rp")) && i + 1 < argc) {
            if (opts->xp_count >= XP_MAX_DRIVES) {
                fprintf(stderr, "Too many -xp images (max %d)\n", XP_MAX_DRIVES);
                return -1;
            }
            opts->xp_path[opts->xp_count++] = argv[++i];
        } else if (!strcmp(argv[i], "-rh-mode") && i + 1 < argc) {
            if (parse_rh_mode(argv[++i], &opts->rh_mode) != 0) {
                fprintf(stderr, "Invalid -rh-mode: %s (expected rh11|rh70)\n", argv[i]);
                return -1;
            }
        } else if (!strcmp(argv[i], "-rl") && i + 1 < argc) {
            if (opts->rl_count >= RL11_MAX_DRIVES) {
                fprintf(stderr, "Too many -rl images (max %d)\n", RL11_MAX_DRIVES);
                return -1;
            }
            opts->rl_path[opts->rl_count].path = argv[++i];
            opts->rl_path[opts->rl_count].type = RL11_TYPE_AUTO;
            opts->rl_count++;
        } else if (!strcmp(argv[i], "-rl01") && i + 1 < argc) {
            if (opts->rl_count >= RL11_MAX_DRIVES) {
                fprintf(stderr, "Too many -rl images (max %d)\n", RL11_MAX_DRIVES);
                return -1;
            }
            opts->rl_path[opts->rl_count].path = argv[++i];
            opts->rl_path[opts->rl_count].type = RL11_TYPE_RL01;
            opts->rl_count++;
        } else if (!strcmp(argv[i], "-rl02") && i + 1 < argc) {
            if (opts->rl_count >= RL11_MAX_DRIVES) {
                fprintf(stderr, "Too many -rl images (max %d)\n", RL11_MAX_DRIVES);
                return -1;
            }
            opts->rl_path[opts->rl_count].path = argv[++i];
            opts->rl_path[opts->rl_count].type = RL11_TYPE_RL02;
            opts->rl_count++;
        } else if (!strcmp(argv[i], "-tq") && i + 1 < argc) {
            if (opts->tq_count >= TQ11_MAX_UNITS) {
                fprintf(stderr, "Too many -tq images (max %d)\n", TQ11_MAX_UNITS);
                return -1;
            }
            opts->tq_path[opts->tq_count++] = argv[++i];
        } else if (!strcmp(argv[i], "-dz") && i + 1 < argc) {
            opts->dz_port = strtol(argv[++i], NULL, 10);
            opts->dz_port_set = 1;
        } else if (!strcmp(argv[i], "-disable-dl")) {
            opts->disable_dl = 1;
        } else if (!strcmp(argv[i], "-disable-dz")) {
            opts->disable_dz = 1;
        } else if (!strcmp(argv[i], "-disable-kw")) {
            opts->disable_kw = 1;
        } else if (!strcmp(argv[i], "-enable-kw11-l")) {
            opts->kw11_l_override = 1;
        } else if (!strcmp(argv[i], "-disable-kw11-l")) {
            opts->kw11_l_override = 0;
        } else if (!strcmp(argv[i], "-enable-kw11-p")) {
            opts->kw11_p_override = 1;
        } else if (!strcmp(argv[i], "-disable-kw11-p")) {
            opts->kw11_p_override = 0;
        } else if (!strcmp(argv[i], "-disable-lp")) {
            opts->disable_lp = 1;
        } else if (!strcmp(argv[i], "-disable-rk")) {
            opts->disable_rk = 1;
        } else if (!strcmp(argv[i], "-disable-rh")) {
            opts->disable_rh = 1;
        } else if (!strcmp(argv[i], "-disable-rq")) {
            opts->disable_rq = 1;
        } else if (!strcmp(argv[i], "-disable-xp")) {
            opts->disable_xp = 1;
        } else if (!strcmp(argv[i], "-disable-rl")) {
            opts->disable_rl = 1;
        } else if (!strcmp(argv[i], "-disable-tq")) {
            opts->disable_tq = 1;
        } else if (!strcmp(argv[i], "-disable-sr")) {
            opts->disable_sr = 1;
        } else if (!strcmp(argv[i], "-bootcopy")) {
            opts->do_bootcopy = 1;
        } else if (!strcmp(argv[i], "-bootrt11")) {
            opts->do_bootrt11 = 1;
        } else if (!strcmp(argv[i], "-boottq")) {
            opts->do_boottq = 1;
        } else if (!strcmp(argv[i], "-boot") && i + 1 < argc) {
            if (parse_boot_device(argv[++i], &opts->boot_kind, &opts->boot_unit) != 0) {
                fprintf(stderr, "Invalid -boot device: %s\n", argv[i]);
                return -1;
            }
            opts->do_boot = 1;
        } else if (!strcmp(argv[i], "-traceirq")) {
            opts->trace_irq = 1;
        } else if (!strcmp(argv[i], "-tracenxm")) {
            opts->trace_nxm = 1;
        } else if (!strcmp(argv[i], "-trace")) {
            opts->trace = 1;
        } else if (!strcmp(argv[i], "-trace-after") && i + 1 < argc) {
            opts->trace_after = strtol(argv[++i], NULL, 10);
            opts->trace = 1;
        } else if (!strcmp(argv[i], "-socket") && i + 1 < argc) {
            opts->socket_path = argv[++i];
        } else if (!strcmp(argv[i], "-traceregs")) {
            opts->trace = 1;
            opts->trace_regs = 1;
        } else if (!strcmp(argv[i], "-tty7b")) {
            opts->dl11_8bit = 0;
        } else if (!strcmp(argv[i], "-tty8b")) {
            opts->dl11_8bit = 1;
        } else if (!strcmp(argv[i], "-display") ||
                   !strcmp(argv[i], "-enable-display")) {
            opts->display_enable = 1;
        } else if (!strcmp(argv[i], "-no-display") ||
                   !strcmp(argv[i], "-disable-display")) {
            opts->display_enable = 0;
        } else if (!strcmp(argv[i], "-nl-to-cr")) {
            opts->do_nl_to_cr = 1;
        } else if (!strcmp(argv[i], "-exit-on-abort")) {
            opts->exit_on_abort = 1;
        } else if (!strcmp(argv[i], "-check-config")) {
            opts->check_config_only = 1;
        } else if (!strcmp(argv[i], "-steps") && i + 1 < argc) {
            opts->max_steps = strtol(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "-load") && i + 1 < argc) {
            opts->load_path = argv[++i];
        } else if (!strcmp(argv[i], "-addr") && i + 1 < argc) {
            opts->load_addr = strtol(argv[++i], NULL, 8);
        } else if (!strcmp(argv[i], "-pc") && i + 1 < argc) {
            opts->start_pc = strtol(argv[++i], NULL, 8);
        } else if (!strcmp(argv[i], "-sr") && i + 1 < argc) {
            opts->sr_value = strtol(argv[++i], NULL, 8);
        } else if (!strcmp(argv[i], "-ram") && i + 1 < argc) {
            opts->ram_kb_arg = strtol(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--mem-kb") && i + 1 < argc) {
            opts->ram_kb_arg = strtol(argv[++i], NULL, 10);
        } else if ((!strcmp(argv[i], "-freq") || !strcmp(argv[i], "-clock")) &&
                   i + 1 < argc) {
            char *end = NULL;
            long mhz = strtol(argv[++i], &end, 10);

            if (!end || *end != '\0' || mhz <= 0) {
                fprintf(stderr, "Invalid clock frequency: %s\n", argv[i]);
                return -1;
            }
            opts->sys_clock_mhz = mhz;
        } else if (!strcmp(argv[i], "-dl11-alias")) {
            opts->force_dl11_alias = 1;
        } else if (!strcmp(argv[i], "-no-dl11-alias")) {
            opts->force_dl11_alias = 0;
        } else if (!strcmp(argv[i], "-cpu") && i + 1 < argc) {
            if (parse_cpu_model(argv[++i], &opts->cpu_model) != 0) {
                fprintf(stderr, "Unknown CPU model: %s\n", argv[i]);
                options_usage(argv0);
                return -1;
            }
        } else if (!strcmp(argv[i], "-force-fis")) {
            opts->force_fis = 1;
        } else if (!strcmp(argv[i], "-force-fp11")) {
            opts->force_fp11 = 1;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            options_usage(argv0);
            return -1;
        }
    }

    return 0;
}

int options_parse(lsi11_options_t *opts, int argc, char **argv)
{
    if (argc < 2) {
        options_usage(argv[0]);
        return -1;
    }

    return parse_arg_list(opts, argc - 1, argv + 1, argv[0]);
}
