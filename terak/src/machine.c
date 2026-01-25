#include "machine.h"
#include "adapter_core.h"
#include "bus.h"
#include <stdio.h>
#include <string.h>
#include "../core/disas.h"

void machine_init(regs *r) {
    /* Select DCJ11 variant */
    r->model = DCJ11;
    terak_hw_connect(r);
    core_init(r);
    core_reset(r);
}

int machine_load_bin(const char *path, uint16_t load_addr) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    size_t n = fread(&ram[load_addr], 1, 65536 - load_addr, fp);
    fclose(fp);
    return (int)n;
}

int machine_run(regs *r, int max_steps, int trace) {
    int steps = 0;
    while (max_steps < 0 || steps < max_steps) {
        if (trace) {
            /* Disassemble the instruction that is about to execute. */
            char buf[128];
            word addr = r->r[7]; /* address of the current instruction */
            disas(r, &addr, buf);
            printf("%06o: %06o %s\n", r->r[7], r->ir, buf);
        }
        int rc = core_step(r);
        if (rc != 0) break; /* halt or error */
        steps++;
        /* poll host input for DL11/RK11 */
        terak_poll_input();
    }
    return steps;
}

/* Stop the machine – invoke the registered fini callback and core cleanup. */
void machine_stop(regs *r) {
    core_fini(r);
}
