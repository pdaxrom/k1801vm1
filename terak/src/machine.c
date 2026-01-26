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

static void dump_emt_state(regs *r) {
    uint16_t op = r->ir;
    uint16_t vec = op & 0000377;
    printf("EMT %03o at %06o  R0=%06o R1=%06o R2=%06o R3=%06o R4=%06o R5=%06o SP=%06o PS=%06o\n",
           vec, r->r[7], r->r[0], r->r[1], r->r[2], r->r[3], r->r[4], r->r[5], r->r[6], r->psw);
    printf("EMT R0 block:");
    for (int i = 0; i < 8; ++i) {
        uint16_t addr = (uint16_t)(r->r[0] + i * 2);
        printf(" %06o", bus_read_word(addr));
    }
    printf("\n");
}

static void dump_regs_line(regs *r) {
    printf(" R0=%06o R1=%06o R2=%06o R3=%06o R4=%06o R5=%06o SP=%06o PS=%06o",
           r->r[0], r->r[1], r->r[2], r->r[3], r->r[4], r->r[5], r->r[6], r->psw);
}

int machine_run(regs *r, int max_steps, int trace, int trace_emt, int trace_regs, int trace_irq) {
    (void)trace_irq;
    int steps = 0;
    word last_regs[8];
    for (int i = 0; i < 8; ++i) last_regs[i] = r->r[i];
    while (max_steps < 0 || steps < max_steps) {
        if (trace) {
            /* Disassemble the instruction that is about to execute. */
            char buf[128];
            word addr = r->r[7]; /* address of the current instruction */
            disas(r, &addr, buf);
            printf("%06o: %06o %s", r->r[7], r->ir, buf);
            if (trace_regs) {
                dump_regs_line(r);
            }
            printf("\n");
        }
        if (terak_trace_pc_enabled() && r->r[7] == terak_trace_pc_addr()) {
            char buf[128];
            word addr = r->r[7];
            disas(r, &addr, buf);
            printf("TRACEPC %06o: %06o %s", r->r[7], r->ir, buf);
            dump_regs_line(r);
            printf("\n");
        }
        if (trace_emt) {
            if ((r->ir & 0177700) == 0104000) {
                dump_emt_state(r);
            }
        }
        int rc = core_step(r);
        if (rc != 0) break; /* halt or error */
        steps++;
        if (terak_trace_reg_enabled()) {
            int idx = terak_trace_reg_index();
            if (idx >= 0 && idx <= 7 && r->r[idx] != last_regs[idx]) {
                char buf[128];
                word addr = r->r[7];
                disas(r, &addr, buf);
                printf("TRACER R%d %06o->%06o at %06o: %06o %s\n",
                       idx, last_regs[idx], r->r[idx], r->r[7], r->ir, buf);
            }
        }
        for (int i = 0; i < 8; ++i) last_regs[i] = r->r[i];
        /* poll host input for DL11/RK11 */
        terak_poll_input();
    }
    return steps;
}

/* Stop the machine – invoke the registered fini callback and core cleanup. */
void machine_stop(regs *r) {
    core_fini(r);
}
