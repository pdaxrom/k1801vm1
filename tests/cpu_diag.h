/* CPU diagnostic harness skeleton (no behavior yet). */
#ifndef CPU_DIAG_H_
#define CPU_DIAG_H_

#include <stddef.h>
#include "core/core.h"

typedef struct {
    regs r;
    byte *mem;
    byte *mem_owner;
    size_t mem_size;
} cpu_diag_fixture;

typedef struct {
    const char *name;
    int passed;
} cpu_diag_result;

void cpu_diag_fixture_init(cpu_diag_fixture *fx, byte model);
void cpu_diag_fixture_fini(cpu_diag_fixture *fx);

int cpu_diag_parse_model(const char *name, byte *model_out);
const char *cpu_diag_model_name(byte model);

cpu_diag_result cpu_diag_run_all(cpu_diag_fixture *fx);
void cpu_diag_trace_octal(cpu_diag_fixture *fx, const char *label);

#endif /* CPU_DIAG_H_ */
