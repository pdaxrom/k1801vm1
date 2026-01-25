/* Machine abstraction – RAM, devices and execution loop */
#ifndef MACHINE_H_
#define MACHINE_H_

#include "../core/core.h"

/* Initialise the machine (CPU, bus, devices). */
void machine_init(regs *r);

/* Run the emulator.  Returns when max_steps reaches 0 or a halt occurs. */
int machine_run(regs *r, int max_steps, int trace);

/* Load a binary file into RAM at the given octal address. */
int machine_load_bin(const char *path, uint16_t load_addr);

/* Stop the machine and perform clean‑up via the API. */
void machine_stop(regs *r);

#endif /* MACHINE_H_ */
