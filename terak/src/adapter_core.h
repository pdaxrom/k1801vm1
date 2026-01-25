/* Adapter between TERAK emulator and the read‑only core library */
#ifndef ADAPTER_CORE_H_
#define ADAPTER_CORE_H_

#include "core.h"

/* Initialise the CPU and install bus callbacks. */
void terak_hw_connect(regs *r);

/* Helper to poll for host keyboard input (called each step). */
void terak_poll_input(void);

#endif /* ADAPTER_CORE_H_ */
