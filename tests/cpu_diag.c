/* CPU diagnostic harness skeleton (no semantic checks yet). */
#include "cpu_diag.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/hardware.h"

static int cpu_diag_str_eq(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b) {
        char ca = (char)tolower((unsigned char)*a);
        char cb = (char)tolower((unsigned char)*b);
        if (ca != cb) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

int cpu_diag_parse_model(const char *name, byte *model_out)
{
    if (!model_out) {
        return 0;
    }
    if (cpu_diag_str_eq(name, "dcj11")) {
        *model_out = DCJ11;
        return 1;
    }
    if (cpu_diag_str_eq(name, "vm1")) {
        *model_out = K1801VM1;
        return 1;
    }
    if (cpu_diag_str_eq(name, "vm2")) {
        *model_out = K1801VM2;
        return 1;
    }
    return 0;
}

const char *cpu_diag_model_name(byte model)
{
    switch (model) {
    case DCJ11:
        return "dcj11";
    case K1801VM2:
        return "vm2";
    case K1801VM1:
    default:
        return "vm1";
    }
}

void cpu_diag_fixture_init(cpu_diag_fixture *fx, byte model)
{
    size_t mem_size;
    byte *mem_owner;
    memset(fx, 0, sizeof(*fx));
    mem_size = hwstub_required_memory_size();
    mem_owner = (byte *)calloc(1, mem_size);
    if (!mem_owner) {
        fprintf(stderr, "cpu_diag: hwstub memory allocation failed (%zu bytes)\n",
                mem_size);
        abort();
    }
    if (hwstub_set_memory(mem_owner, mem_size) != 0) {
        fprintf(stderr, "cpu_diag: hwstub_set_memory failed\n");
        free(mem_owner);
        abort();
    }
    fx->r.model = model;
    hwstub_connect(&fx->r);
    if (core_init(&fx->r) != 0) {
        fprintf(stderr, "cpu_diag: core_init failed\n");
        free(mem_owner);
        abort();
    }
    fx->mem = fx->r.ramptr(&fx->r, 0);
    fx->mem_owner = mem_owner;
    fx->mem_size = mem_size;
    memset(fx->mem, 0, mem_size);
    fx->r.SEL0 = 0;
    core_reset(&fx->r);
}

void cpu_diag_fixture_fini(cpu_diag_fixture *fx)
{
    core_fini(&fx->r);
    hwstub_clear_memory_binding();
    free(fx->mem_owner);
    fx->mem_owner = NULL;
}

cpu_diag_result cpu_diag_run_all(cpu_diag_fixture *fx)
{
    (void)fx;
    cpu_diag_result result = {"cpu_diag", 1};
    return result;
}

void cpu_diag_trace_octal(cpu_diag_fixture *fx, const char *label)
{
    if (label && label[0]) {
        printf("%s\n", label);
    }
    printf("PC=%06o PSW=%06o\n", fx->r.r[7], fx->r.psw);
    printf("R0=%06o R1=%06o R2=%06o R3=%06o R4=%06o R5=%06o R6=%06o R7=%06o\n",
           fx->r.r[0], fx->r.r[1], fx->r.r[2], fx->r.r[3], fx->r.r[4], fx->r.r[5],
           fx->r.r[6], fx->r.r[7]);
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
