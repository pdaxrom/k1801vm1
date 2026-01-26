/* Cross-model CPU diagnostic runner (no semantic checks yet). */
#include "cpu_diag.h"
#include <stdio.h>
#include <string.h>

static int run_model(byte model)
{
    cpu_diag_fixture fx;
    cpu_diag_result result;
    int ok;

    cpu_diag_fixture_init(&fx, model);
    result = cpu_diag_run_all(&fx);
    ok = result.passed != 0;
    printf("%s: %s\n", cpu_diag_model_name(model), ok ? "OK" : "FAIL");
    cpu_diag_fixture_fini(&fx);
    return ok;
}

int main(int argc, char **argv)
{
    int ok = 1;
    int i;
    int run_all = 1;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--all") == 0) {
            run_all = 1;
            continue;
        }
        run_all = 0;
        {
            byte model;
            if (!cpu_diag_parse_model(argv[i], &model)) {
                fprintf(stderr, "Unknown model: %s\n", argv[i]);
                return 2;
            }
            if (!run_model(model)) {
                ok = 0;
            }
        }
    }

    if (run_all) {
        ok &= run_model(K1801VM1);
        ok &= run_model(K1801VM1G);
        ok &= run_model(K1801VM2);
        ok &= run_model(DCJ11);
    }

    return ok ? 0 : 1;
}
