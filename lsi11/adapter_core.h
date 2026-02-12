#ifndef ADAPTER_CORE_H_
#define ADAPTER_CORE_H_

/* Adapter between lsi11 machine and read-only core CPU library. */

#include <stdint.h>
#include <stddef.h>
#include "../core/core.h"

typedef enum {
    LSI11_MACHINE_1104 = 0,
    LSI11_MACHINE_1184 = 1,
    LSI11_MACHINE_1134 = LSI11_MACHINE_1184,
} lsi11_machine_t;

/*
 * Configure machine profile before lsi11_hw_connect()/r.init().
 * For 11/04-like profile, RAM is fixed to 56KB (ram_kb ignored).
 * For 11/84-like profile, default is 4096KB when ram_kb==0 and ram_kb must be
 * a multiple of 4KB.
 */
int lsi11_machine_configure(lsi11_machine_t machine, uint32_t ram_kb, char *err,
                            size_t err_len);

void lsi11_set_dl11_alias(int on);
int lsi11_dl11_alias(void);
lsi11_machine_t lsi11_machine_current(void);
uint32_t lsi11_machine_ram_kb(void);
int lsi11_set_device_enabled(const char *name, int on, char *err,
                             size_t err_len);
int lsi11_device_enabled(const char *name);

/* Connect machine callbacks to core regs struct. */
void lsi11_hw_connect(regs *r);

/* Machine poll (host-side): devices advance and host I/O. */
void lsi11_poll_devices(void);

/* Optional: tracing toggles (stubs; fill if needed) */
void lsi11_set_trace_irq(int on);
void lsi11_set_trace_nxm(int on);

#endif
