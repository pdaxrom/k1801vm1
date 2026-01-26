/* Adapter between TERAK emulator and the read‑only core library */
#ifndef ADAPTER_CORE_H_
#define ADAPTER_CORE_H_

#include "core.h"

/* Initialise the CPU and install bus callbacks. */
void terak_hw_connect(regs *r);

/* Helper to poll for host keyboard input (called each step). */
void terak_poll_input(void);
void terak_set_trace_irq(int on);
void terak_set_watch_addr(word addr, int on);
void terak_set_watch_silent(int on);
void terak_set_trace_pc(word addr, int on);
int terak_trace_pc_enabled(void);
word terak_trace_pc_addr(void);
void terak_set_trace_reg(int reg, int on);
int terak_trace_reg_enabled(void);
int terak_trace_reg_index(void);

#endif /* ADAPTER_CORE_H_ */
