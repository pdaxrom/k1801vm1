#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "../core/core.h"
#include "../core/disas.h"
#include "adapter_core.h"
#include "machine.h"
#include "rk11.h"
#include "bus.h"

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  --load <file>      Load binary into RAM\n"
        "  --addr <octal>     Load address (octal) – defaults to 0\n"
        "  --pc <octal>       Set PC (octal) after loading\n"
        "  --dump <addr> <n>  Dump N words from RAM at addr (octal)\n"
        "  --dump-hex         Use hex format for --dump output\n"
        "  --trace-emt        Print registers when EMT executes\n"
        "  --trace-regs       Print registers on each trace line\n"
        "  --trace-irq        Log IRQ delivery\n"
        "  --cpu <name>       CPU model: dcj11 (default), k1801vm1, k1801vm2\n"
        "  --rk11-sector1     Use 1-based sector numbering\n"
        "  --watch <addr>     Log writes to RAM address (octal)\n"
        "  --watch-silent     Log watch without PC/registers\n"
        "  --trace-pc <addr>  Log registers when PC hits addr (octal)\n"
        "  --trace-reg <n>    Log when register n changes (0-7)\n"
        "  --disas <addr> <n> Disassemble N instructions from addr (octal)\n"
        "  --trace            Enable instruction trace\n"
        "  --max-steps N      Run at most N steps (negative = unlimited)\n",
        prog);
}

int main(int argc, char **argv) {
    const char *load_path = NULL;
    unsigned int load_addr = 0;   /* default load address */
    const char *rk05_path = NULL;
    int do_boot = 0;              /* --boot rk flag */
    unsigned int pc = 0;
    int set_pc = 0;
    int trace = 0;
    int max_steps = -1;
    int dump_hex = 0;
    int trace_emt = 0;
    int trace_regs = 0;
    int trace_irq = 0;
    int cpu_model = DCJ11;
    int do_disas = 0;
    int watch_on = 0;
    unsigned int watch_addr = 0;
    int watch_silent = 0;
    int trace_pc_on = 0;
    unsigned int trace_pc_addr = 0;
    int trace_reg_on = 0;
    int trace_reg_index = -1;
    int rk11_sector1 = 0;
    struct dump_req { unsigned int addr; unsigned int words; };
    struct disas_req { unsigned int addr; unsigned int count; };
    struct dump_req dump_reqs[32];
    struct disas_req disas_reqs[32];
    int dump_count = 0;
    int disas_count = 0;

    /* manual long options parsing */
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--load") == 0 && i + 1 < argc) {
            load_path = argv[++i];
        } else if (strcmp(argv[i], "--addr") == 0 && i + 1 < argc) {
            load_addr = strtoul(argv[++i], NULL, 8);
        } else if (strcmp(argv[i], "--pc") == 0 && i + 1 < argc) {
            pc = strtoul(argv[++i], NULL, 8);
            set_pc = 1;
        } else if (strcmp(argv[i], "--trace") == 0) {
            trace = 1;
        } else if (strcmp(argv[i], "--max-steps") == 0 && i + 1 < argc) {
            max_steps = (int)strtol(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--dump") == 0 && i + 2 < argc) {
            if (dump_count < (int)(sizeof(dump_reqs) / sizeof(dump_reqs[0]))) {
                dump_reqs[dump_count].addr = strtoul(argv[++i], NULL, 8);
                dump_reqs[dump_count].words = strtoul(argv[++i], NULL, 8);
                dump_count++;
            } else {
                i += 2;
            }
        } else if (strcmp(argv[i], "--dump-hex") == 0) {
            dump_hex = 1;
        } else if (strcmp(argv[i], "--trace-emt") == 0) {
            trace_emt = 1;
        } else if (strcmp(argv[i], "--trace-regs") == 0) {
            trace_regs = 1;
        } else if (strcmp(argv[i], "--trace-irq") == 0) {
            trace_irq = 1;
        } else if (strcmp(argv[i], "--cpu") == 0 && i + 1 < argc) {
            const char *cpu = argv[++i];
            if (strcmp(cpu, "dcj11") == 0) cpu_model = DCJ11;
            else if (strcmp(cpu, "k1801vm1") == 0) cpu_model = K1801VM1;
            else if (strcmp(cpu, "k1801vm2") == 0) cpu_model = K1801VM2;
        } else if (strcmp(argv[i], "--rk11-sector1") == 0) {
            rk11_sector1 = 1;
        } else if (strcmp(argv[i], "--disas") == 0 && i + 2 < argc) {
            if (disas_count < (int)(sizeof(disas_reqs) / sizeof(disas_reqs[0]))) {
                disas_reqs[disas_count].addr = strtoul(argv[++i], NULL, 8);
                disas_reqs[disas_count].count = strtoul(argv[++i], NULL, 8);
                disas_count++;
                do_disas = 1;
            } else {
                i += 2;
            }
        } else if (strcmp(argv[i], "--watch") == 0 && i + 1 < argc) {
            watch_addr = strtoul(argv[++i], NULL, 8);
            watch_on = 1;
        } else if (strcmp(argv[i], "--watch-silent") == 0) {
            watch_silent = 1;
        } else if (strcmp(argv[i], "--trace-pc") == 0 && i + 1 < argc) {
            trace_pc_addr = strtoul(argv[++i], NULL, 8);
            trace_pc_on = 1;
        } else if (strcmp(argv[i], "--trace-reg") == 0 && i + 1 < argc) {
            trace_reg_index = (int)strtol(argv[++i], NULL, 0);
            if (trace_reg_index >= 0 && trace_reg_index <= 7) {
                trace_reg_on = 1;
            }
        } else if (strcmp(argv[i], "--rk05") == 0 && i + 1 < argc) {
            rk05_path = argv[++i];
        } else if (strcmp(argv[i], "--boot") == 0 && i + 1 < argc) {
            if (strcmp(argv[i+1], "rk") == 0) {
                do_boot = 1;
                i++; /* consume argument */
            }
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
    }

    regs r;
    machine_init(&r);
    r.model = (byte)cpu_model;
    terak_set_trace_irq(trace_irq);
    rk11_set_sector_base(rk11_sector1);
    if (watch_on) {
        terak_set_watch_addr((word)watch_addr, 1);
    }
    if (watch_silent) {
        terak_set_watch_silent(1);
    }
    if (trace_pc_on) {
        terak_set_trace_pc((word)trace_pc_addr, 1);
    }
    if (trace_reg_on) {
        terak_set_trace_reg(trace_reg_index, 1);
    }

    if (rk05_path) {
        if (rk11_open_image(rk05_path) != 0) {
            fprintf(stderr, "Failed to open RK05 image %s\n", rk05_path);
            return 1;
        }
    }

    if (load_path) {
        if (machine_load_bin(load_path, (uint16_t)load_addr) < 0) {
            fprintf(stderr, "Failed to load %s\n", load_path);
            return 1;
        }
        if (set_pc) r.r[7] = pc;
    }

    if (do_boot && rk05_path) {
        /* Simple host‑side bootstrap: copy first 4 KB of the disk image into RAM at 000000 */
        size_t copy_len = 4096;
        if (copy_len > sizeof(ram)) copy_len = sizeof(ram);
        if (rk11_boot_copy(ram, copy_len) != 0) {
            fprintf(stderr, "Failed to copy boot data from RK05 image\n");
            return 1;
        }
        r.r[7] = 0; /* start execution at address 0 */
    }

    machine_run(&r, max_steps, trace, trace_emt, trace_regs, trace_irq);

    if (do_disas && disas_count > 0) {
        for (int d = 0; d < disas_count; ++d) {
            word addr = (word)disas_reqs[d].addr;
            unsigned int count = disas_reqs[d].count;
            for (unsigned int i = 0; i < count; ++i) {
                char buf[128];
                word here = addr;
                word w = bus_read_word((paddr_t)here);
                disas(&r, &addr, buf);
                printf("%06o: %06o %s\n", here, w, buf);
            }
        }
    }
    /* Clean up via the machine API */
    machine_stop(&r);

    if (dump_count > 0) {
        for (int d = 0; d < dump_count; ++d) {
            unsigned int dump_addr = dump_reqs[d].addr;
            unsigned int words = dump_reqs[d].words;
            if (dump_addr + words * 2 > 0177777) {
                if (dump_addr >= 0177777) words = 0;
                else words = (0177777 - dump_addr) / 2;
            }
            for (unsigned int i = 0; i < words; ++i) {
                unsigned int addr = dump_addr + i * 2;
                if ((i % 8) == 0) {
                    if (i != 0) printf("\n");
                    if (dump_hex) printf("%04x:", addr & 0xFFFF);
                    else printf("%06o:", addr);
                }
                if (dump_hex) printf(" %04x", bus_read_word((paddr_t)addr));
                else printf(" %06o", bus_read_word((paddr_t)addr));
            }
            if (words > 0) printf("\n");
        }
    }
    return 0;
}
