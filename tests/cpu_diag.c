/* CPU diagnostic harness skeleton (no semantic checks yet). */
#include "cpu_diag.h"
#include <string.h>

#include "core/hardware.h"

void cpu_diag_fixture_init(cpu_diag_fixture *fx, byte model)
{
    memset(fx, 0, sizeof(*fx));
    fx->r.model = model;
    hwstub_connect(&fx->r);
    core_init(&fx->r);
    fx->mem = fx->r.ramptr(&fx->r, 0);
    memset(fx->mem, 0, 1 << 16);
    fx->r.SEL0 = 0;
    fx->r.SEL1 = 0;
    core_reset(&fx->r);
}

void cpu_diag_fixture_fini(cpu_diag_fixture *fx)
{
    core_fini(&fx->r);
}

cpu_diag_result cpu_diag_run_all(cpu_diag_fixture *fx)
{
    (void)fx;
    cpu_diag_result result = { "cpu_diag", 1 };
    return result;
}

void cpu_diag_trace_octal(cpu_diag_fixture *fx, const char *label)
{
    if (label && label[0]) {
        printf("%s\n", label);
    }
    printf("PC=%06o PSW=%06o\n", fx->r.r[7], fx->r.psw);
    printf("R0=%06o R1=%06o R2=%06o R3=%06o R4=%06o R5=%06o R6=%06o R7=%06o\n",
           fx->r.r[0], fx->r.r[1], fx->r.r[2], fx->r.r[3],
           fx->r.r[4], fx->r.r[5], fx->r.r[6], fx->r.r[7]);
    printf("VEC 000000=%06o 000002=%06o 000004=%06o 000006=%06o\n",
           fx->mem[000000] | ((word)fx->mem[000001] << 8),
           fx->mem[000002] | ((word)fx->mem[000003] << 8),
           fx->mem[000004] | ((word)fx->mem[000005] << 8),
           fx->mem[000006] | ((word)fx->mem[000007] << 8));
    printf("VEC 000010=%06o 000012=%06o 000014=%06o 000016=%06o\n",
           fx->mem[000010] | ((word)fx->mem[000011] << 8),
           fx->mem[000012] | ((word)fx->mem[000013] << 8),
           fx->mem[000014] | ((word)fx->mem[000015] << 8),
           fx->mem[000016] | ((word)fx->mem[000017] << 8));
}
