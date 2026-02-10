#include "bus.h"
#include "devio.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LSI11_FIXED_RAM_KB 56u
#define PDP1134_DEFAULT_RAM_KB 4096u
#define PDP11_18BIT_IO_PAGE_START 0760000
#define PDP11_18BIT_IO_PAGE_END   0777777
#define PDP11_22BIT_IO_PAGE_START 017760000
#define PDP11_22BIT_IO_PAGE_END   017777777

typedef struct {
  bus_machine_t machine;
  uint32_t ram_kb;
} bus_cfg_t;

static bus_cfg_t g_cfg = {BUS_MACHINE_LSI11_1104, LSI11_FIXED_RAM_KB};
static uint8_t *g_ram = NULL;
static size_t g_ram_bytes = 0;

static void set_err(char *err, size_t err_len, const char *fmt, ...) {
  va_list ap;

  if (!err || err_len == 0)
    return;

  va_start(ap, fmt);
  vsnprintf(err, err_len, fmt, ap);
  va_end(ap);
}

void bus_reset_config(void) {
  g_cfg.machine = BUS_MACHINE_LSI11_1104;
  g_cfg.ram_kb = LSI11_FIXED_RAM_KB;
}

int bus_configure(bus_machine_t machine, uint32_t ram_kb, char *err,
                  size_t err_len) {
  switch (machine) {
  case BUS_MACHINE_LSI11_1104:
    g_cfg.machine = BUS_MACHINE_LSI11_1104;
    g_cfg.ram_kb = LSI11_FIXED_RAM_KB;
    return 0;

  case BUS_MACHINE_PDP1134:
    if (ram_kb == 0)
      ram_kb = PDP1134_DEFAULT_RAM_KB;

    if ((ram_kb % BUS_RAM_GRANULARITY_KB) != 0) {
      set_err(err, err_len,
              "RAM size %u KB is invalid: must be multiple of %u KB",
              ram_kb, BUS_RAM_GRANULARITY_KB);
      return -1;
    }

    g_cfg.machine = BUS_MACHINE_PDP1134;
    g_cfg.ram_kb = ram_kb;
    return 0;

  default:
    set_err(err, err_len, "Unknown machine type");
    return -1;
  }
}

bus_machine_t bus_machine(void) { return g_cfg.machine; }

uint32_t bus_ram_kb(void) { return g_cfg.ram_kb; }

size_t bus_ram_bytes(void) { return g_ram_bytes; }

void bus_init(void) {
  size_t need_bytes = (size_t)g_cfg.ram_kb * 1024u;

  if (g_ram && g_ram_bytes != need_bytes) {
    free(g_ram);
    g_ram = NULL;
    g_ram_bytes = 0;
  }

  if (!g_ram) {
    if (posix_memalign((void **)&g_ram, BUS_PAGE_SIZE, need_bytes) != 0) {
      fprintf(stderr, "bus_init: memory allocation failed (%zu bytes)\n",
              need_bytes);
      abort();
    }
    g_ram_bytes = need_bytes;
  }

  memset(g_ram, 0, g_ram_bytes);
}

static int io_decode_addr(paddr_t addr, uint16_t *io_addr_out) {
  uint16_t a16;

  if (!io_addr_out)
    return 0;

  if (g_cfg.machine == BUS_MACHINE_LSI11_1104) {
    if (addr < IO_PAGE_START || addr > IO_PAGE_END)
      return 0;
    a16 = (uint16_t)addr;
    if (!devio_has(a16))
      return 0;
    *io_addr_out = a16;
    return 1;
  }

  if (addr <= 0177777) {
    a16 = (uint16_t)addr;
    if (devio_has(a16)) {
      *io_addr_out = a16;
      return 1;
    }
  }

  if (addr >= PDP11_18BIT_IO_PAGE_START && addr <= PDP11_18BIT_IO_PAGE_END) {
    a16 = (uint16_t)(IO_PAGE_START | (addr & 017777));
    if (devio_has(a16)) {
      *io_addr_out = a16;
      return 1;
    }
  }

  if (addr >= PDP11_22BIT_IO_PAGE_START && addr <= PDP11_22BIT_IO_PAGE_END) {
    a16 = (uint16_t)(IO_PAGE_START | (addr & 017777));
    if (devio_has(a16)) {
      *io_addr_out = a16;
      return 1;
    }
  }

  return 0;
}

static int io_is_decoded(paddr_t addr) {
  uint16_t io_addr = 0;
  return io_decode_addr(addr, &io_addr);
}

int bus_addr_is_ram(paddr_t addr) {
  if (io_is_decoded(addr))
    return 0;

  if (g_cfg.machine == BUS_MACHINE_LSI11_1104) {
    uint16_t a = (uint16_t)addr;
    return (a <= RAM_END) ? 1 : 0;
  }

  /* On PDP-11/34-like configuration, low 16-bit I/O page is never RAM. */
  if (addr >= IO_PAGE_START && addr <= IO_PAGE_END)
    return 0;

  return (addr < g_ram_bytes) ? 1 : 0;
}

int bus_range_is_ram(paddr_t addr, size_t len) {
  if (len == 0)
    return 1;

  for (size_t i = 0; i < len; i++) {
    if (!bus_addr_is_ram(addr + (paddr_t)i))
      return 0;
  }
  return 1;
}

int bus_is_nxm(paddr_t addr) {
  if (io_is_decoded(addr))
    return 0;

  if (bus_addr_is_ram(addr))
    return 0;

  return 1;
}

uint8_t bus_read8(paddr_t addr) {
  uint16_t io_addr = 0;

  if (io_decode_addr(addr, &io_addr))
    return devio_read8(io_addr);

  if (bus_addr_is_ram(addr))
    return g_ram[addr];

  return 0;
}

void bus_write8(paddr_t addr, uint8_t v) {
  uint16_t io_addr = 0;

  if (io_decode_addr(addr, &io_addr)) {
    devio_write8(io_addr, v);
    return;
  }

  if (bus_addr_is_ram(addr)) {
    g_ram[addr] = v;
    return;
  }
}

uint16_t bus_read16(paddr_t addr) {
  uint8_t lo = bus_read8(addr);
  uint8_t hi = bus_read8(addr + 1);
  return (uint16_t)(lo | ((uint16_t)hi << 8));
}

void bus_write16(paddr_t addr, uint16_t v) {
  bus_write8(addr, (uint8_t)(v & 000377));
  bus_write8(addr + 1, (uint8_t)((v >> 8) & 000377));
}

uint8_t *bus_ram_ptr(paddr_t addr) {
  if (!bus_addr_is_ram(addr))
    return NULL;
  return &g_ram[addr];
}
