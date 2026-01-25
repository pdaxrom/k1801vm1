#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "../core/core.h"
#include "machine.h"

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
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
    }

    regs r;
    machine_init(&r);

    if (load_path) {
        if (machine_load_bin(load_path, (uint16_t)load_addr) < 0) {
            fprintf(stderr, "Failed to load %s\n", load_path);
            return 1;
        }
        if (set_pc) r.r[7] = pc;
    }

    machine_run(&r, max_steps, trace);
    /* Clean up via the machine API */
    machine_stop(&r);
    return 0;
}
