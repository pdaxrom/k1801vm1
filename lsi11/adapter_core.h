#ifndef ADAPTER_CORE_H_
#define ADAPTER_CORE_H_

/* Adapter between lsi11 machine and read-only core CPU library. */

#include <stdint.h>
#include "../core/core.h"

/* Connect machine callbacks to core regs struct. */
void lsi11_hw_connect(regs *r);

/* Machine poll (host-side): devices advance and host I/O. */
void lsi11_poll_devices(void);

/* Optional: tracing toggles (stubs; fill if needed) */
void lsi11_set_trace_irq(int on);
void lsi11_set_trace_nxm(int on);

#endif
