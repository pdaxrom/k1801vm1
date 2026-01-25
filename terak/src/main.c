#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "../core/core.h"
#include "machine.h"
#include "rk11.h"
#include "bus.h"

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  --load <file>      Load binary into RAM\n"
        "  --addr <octal>     Load address (octal) – defaults to 0\n"
        "  --pc <octal>       Set PC (octal) after loading\n"
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

    machine_run(&r, max_steps, trace);
    /* Clean up via the machine API */
    machine_stop(&r);
    return 0;
}
