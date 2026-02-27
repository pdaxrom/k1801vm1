#ifndef BUS_H_
#define BUS_H_

#include <stddef.h>
#include <stdint.h>

#ifdef PICO_ON_DEVICE
#include "pico/stdlib.h"
#endif

typedef uint32_t paddr_t;

/* PDP-11 fixed I/O page map (OCTAL) */
#define RAM_END 0157777
#define IO_PAGE_START 0160000
#define IO_PAGE_END 0177777

#define BUS_PAGE_SIZE 4096u
#define BUS_PAGE_SIZE_KB (BUS_PAGE_SIZE / 1024u)
#define BUS_RAM_GRANULARITY_KB BUS_PAGE_SIZE_KB

/* VM2 USER/HALT RAM bank defaults (0200000 = 64KB). */
#define BUS_VM2_DEFAULT_USER_RAM_BYTES 0200000u
#define BUS_VM2_DEFAULT_HALT_RAM_BYTES 0000000u
#define BUS_VM2_BANK_MAX_BYTES 0200000u

typedef enum {
    BUS_MACHINE_LSI11_1104 = 0,
    BUS_MACHINE_PDP1184 = 1,
} bus_machine_t;

/*
 * Configure bus/memory model.
 * - BUS_MACHINE_LSI11_1104: fixed 56KB RAM model (ram_kb argument ignored)
 * - BUS_MACHINE_PDP1184: default 4096KB when ram_kb==0, otherwise ram_kb must
 *   be a multiple of BUS_RAM_GRANULARITY_KB.
 */
int bus_configure(bus_machine_t machine, uint32_t ram_kb, char *err,
                  size_t err_len);

void bus_init(void);
void bus_reset_config(void);
void bus_vm2_reset_config(void);

int bus_vm2_configure(uint32_t user_ram_bytes, uint32_t halt_ram_bytes, char *err,
                      size_t err_len);
uint32_t bus_vm2_user_ram_bytes(void);
uint32_t bus_vm2_halt_ram_bytes(void);

int bus_is_nxm(paddr_t addr);
int bus_addr_is_ram(paddr_t addr);
int bus_range_is_ram(paddr_t addr, size_t len);

uint8_t bus_read8(paddr_t addr);
uint16_t bus_read16(paddr_t addr);

void bus_write8(paddr_t addr, uint8_t v);
void bus_write16(paddr_t addr, uint16_t v);

/* VM2 CPU-side accessors: bank RAM for 000000..157777, never bank 160000..177777. */
int bus_vm2_cpu_is_nxm(uint16_t addr, int halt_mode);
uint8_t bus_vm2_cpu_read8(uint16_t addr, int halt_mode);
void bus_vm2_cpu_write8(uint16_t addr, int halt_mode, uint8_t v);
uint16_t bus_vm2_cpu_read16(uint16_t addr, int halt_mode);
void bus_vm2_cpu_write16(uint16_t addr, int halt_mode, uint16_t v);

uint8_t *bus_ram_ptr(paddr_t addr);
uint32_t bus_ram_kb(void);
size_t bus_ram_bytes(void);
bus_machine_t bus_machine(void);

#endif
