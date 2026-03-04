#include "bus.h"
#include "devio.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LSI11_FIXED_RAM_KB 56u
#define PDP1184_DEFAULT_RAM_KB 4096u
#define PDP11_18BIT_IO_PAGE_START 0760000
#define PDP11_18BIT_IO_PAGE_END 0777777
#define PDP11_22BIT_IO_PAGE_START 017760000
#define PDP11_22BIT_IO_PAGE_END 017777777

typedef struct {
    bus_machine_t machine;
    uint32_t ram_kb;
} bus_cfg_t;

typedef struct {
    uint32_t user_ram_bytes;
    uint32_t halt_ram_bytes;
} vm2_ram_cfg_t;

static bus_cfg_t g_cfg = {BUS_MACHINE_LSI11_1104, LSI11_FIXED_RAM_KB};
static uint8_t *g_ram = NULL;
static size_t g_ram_bytes = 0;
static vm2_ram_cfg_t g_vm2_cfg = {BUS_VM2_DEFAULT_USER_RAM_BYTES,
                                  BUS_VM2_DEFAULT_HALT_RAM_BYTES
                                 };
static uint8_t *g_vm2_halt_ram = NULL;
static size_t g_vm2_halt_ram_bytes = 0;
static int g_pdp1184_io_16bit = 1;
static int g_pdp1184_m22e = 0;
static int g_watch_init;
static int g_watch_on;
static paddr_t g_watch_pa;

static void bus_watch_init(void)
{
    const char *env;
    char *endp = NULL;
    unsigned long v;

    if (g_watch_init) {
        return;
    }
    g_watch_init = 1;
    env = getenv("LSI11_WATCH_PA");
    if (!env || !*env) {
        return;
    }
    v = strtoul(env, &endp, 8);
    if (endp == env || *endp != '\0' || v > PDP11_22BIT_IO_PAGE_END) {
        return;
    }
    g_watch_pa = (paddr_t)v;
    g_watch_on = 1;
}

static int bus_watch_hit_byte(paddr_t addr)
{
    bus_watch_init();
    return (g_watch_on && addr == g_watch_pa) ? 1 : 0;
}

static int bus_watch_hit_word(paddr_t addr)
{
    bus_watch_init();
    if (!g_watch_on) {
        return 0;
    }
    return (addr == g_watch_pa || (addr + 1u) == g_watch_pa) ? 1 : 0;
}

static inline void *bus_alloc(size_t size)
{
#if defined(PICO_ON_DEVICE)
    return malloc(size);
#else
    void *p = NULL;
    if (posix_memalign(&p, BUS_PAGE_SIZE, size) != 0) {
        return NULL;
    }
    return p;
#endif
}

static void set_err(char *err, size_t err_len, const char *fmt, ...)
{
    va_list ap;

    if (!err || err_len == 0) {
        return;
    }

    va_start(ap, fmt);
    vsnprintf(err, err_len, fmt, ap);
    va_end(ap);
}

void bus_reset_config(void)
{
    g_cfg.machine = BUS_MACHINE_LSI11_1104;
    g_cfg.ram_kb = LSI11_FIXED_RAM_KB;
    g_pdp1184_io_16bit = 1;
    g_pdp1184_m22e = 0;
    bus_vm2_reset_config();
}

void bus_vm2_reset_config(void)
{
    g_vm2_cfg.user_ram_bytes = BUS_VM2_DEFAULT_USER_RAM_BYTES;
    g_vm2_cfg.halt_ram_bytes = BUS_VM2_DEFAULT_HALT_RAM_BYTES;
}

int bus_configure(bus_machine_t machine, uint32_t ram_kb, char *err,
                  size_t err_len)
{
    switch (machine) {
    case BUS_MACHINE_LSI11_1104:
        g_cfg.machine = BUS_MACHINE_LSI11_1104;
        g_cfg.ram_kb = LSI11_FIXED_RAM_KB;
        g_pdp1184_io_16bit = 1;
        return 0;

    case BUS_MACHINE_PDP1184:
        if (ram_kb == 0) {
            ram_kb = PDP1184_DEFAULT_RAM_KB;
        }

        if ((ram_kb % BUS_RAM_GRANULARITY_KB) != 0) {
            set_err(err, err_len,
                    "RAM size %u KB is invalid: must be multiple of %u KB", ram_kb,
                    BUS_RAM_GRANULARITY_KB);
            return -1;
        }

        g_cfg.machine = BUS_MACHINE_PDP1184;
        g_cfg.ram_kb = ram_kb;
        g_pdp1184_io_16bit = 1;
        g_pdp1184_m22e = 0;
        return 0;

    default:
        set_err(err, err_len, "Unknown machine type");
        return -1;
    }
}

bus_machine_t bus_machine(void)
{
    return g_cfg.machine;
}

uint32_t bus_ram_kb(void)
{
    return g_cfg.ram_kb;
}

size_t bus_ram_bytes(void)
{
    return g_ram_bytes;
}

void bus_set_pdp1184_io_16bit(int on)
{
    g_pdp1184_io_16bit = on ? 1 : 0;
}

int bus_pdp1184_io_16bit(void)
{
    return g_pdp1184_io_16bit;
}

void bus_set_pdp1184_m22e(int on)
{
    g_pdp1184_m22e = on ? 1 : 0;
}

int bus_pdp1184_m22e(void)
{
    return g_pdp1184_m22e;
}

int bus_vm2_configure(uint32_t user_ram_bytes, uint32_t halt_ram_bytes,
                      char *err, size_t err_len)
{
    if (user_ram_bytes == 0) {
        user_ram_bytes = BUS_VM2_DEFAULT_USER_RAM_BYTES;
    }
    if (user_ram_bytes > BUS_VM2_BANK_MAX_BYTES) {
        set_err(err, err_len, "VM2 USER RAM size %07o exceeds 0200000 bytes",
                (unsigned)user_ram_bytes);
        return -1;
    }
    if (halt_ram_bytes > BUS_VM2_BANK_MAX_BYTES) {
        set_err(err, err_len, "VM2 HALT RAM size %07o exceeds 0200000 bytes",
                (unsigned)halt_ram_bytes);
        return -1;
    }

    g_vm2_cfg.user_ram_bytes = user_ram_bytes;
    g_vm2_cfg.halt_ram_bytes = halt_ram_bytes;
    return 0;
}

uint32_t bus_vm2_user_ram_bytes(void)
{
    return g_vm2_cfg.user_ram_bytes;
}

uint32_t bus_vm2_halt_ram_bytes(void)
{
    return g_vm2_cfg.halt_ram_bytes;
}

void bus_init(void)
{
    size_t need_bytes = (size_t)g_cfg.ram_kb * 1024u;
    size_t halt_need = (size_t)g_vm2_cfg.halt_ram_bytes;

    if (g_ram && g_ram_bytes != need_bytes) {
        free(g_ram);
        g_ram = NULL;
        g_ram_bytes = 0;
    }

    if (!g_ram) {
        g_ram = (uint8_t *)bus_alloc(need_bytes);
        if (!g_ram) {
            fprintf(stderr, "bus_init: memory allocation failed (%zu bytes)\n",
                    need_bytes);
            abort();
        }
        g_ram_bytes = need_bytes;
    }

    memset(g_ram, 0, g_ram_bytes);

    if (g_vm2_halt_ram && g_vm2_halt_ram_bytes != halt_need) {
        free(g_vm2_halt_ram);
        g_vm2_halt_ram = NULL;
        g_vm2_halt_ram_bytes = 0;
    }

    if (halt_need == 0) {
        if (g_vm2_halt_ram) {
            free(g_vm2_halt_ram);
            g_vm2_halt_ram = NULL;
            g_vm2_halt_ram_bytes = 0;
        }
        return;
    }

    if (!g_vm2_halt_ram) {
        g_vm2_halt_ram = (uint8_t *)bus_alloc(halt_need);
        if (!g_vm2_halt_ram) {
            fprintf(stderr, "bus_init: VM2 HALT RAM allocation failed (%zu bytes)\n",
                    halt_need);
            abort();
        }
        g_vm2_halt_ram_bytes = halt_need;
    }

    memset(g_vm2_halt_ram, 0, g_vm2_halt_ram_bytes);
}

static inline int io_decode_addr(paddr_t addr, uint16_t *io_addr_out)
{
    uint16_t a16;

    if (!io_addr_out) {
        return 0;
    }

    if (addr <= 0177777) {
        a16 = (uint16_t)addr;
        if (a16 < IO_PAGE_START || a16 > IO_PAGE_END) {
            return 0;
        }
        if (g_cfg.machine == BUS_MACHINE_LSI11_1104) {
            if (devio_has(a16)) {
                *io_addr_out = a16;
                return 1;
            }
        } else if (g_cfg.machine == BUS_MACHINE_PDP1184 &&
                   g_pdp1184_io_16bit) {
            if (devio_has(a16)) {
                *io_addr_out = a16;
                return 1;
            }
        }
    }

    if (g_cfg.machine == BUS_MACHINE_PDP1184) {
        if (!g_pdp1184_m22e &&
                addr >= PDP11_18BIT_IO_PAGE_START &&
                addr <= PDP11_18BIT_IO_PAGE_END) {
            a16 = (uint16_t)(IO_PAGE_START | (addr & 017777));
            if (devio_has(a16)) {
                *io_addr_out = a16;
                return 1;
            }
        }
        if (g_pdp1184_m22e &&
                addr >= PDP11_22BIT_IO_PAGE_START &&
                addr <= PDP11_22BIT_IO_PAGE_END) {
            a16 = (uint16_t)(IO_PAGE_START | (addr & 017777));
            if (devio_has(a16)) {
                *io_addr_out = a16;
                return 1;
            }
        }
    }

    return 0;
}

static inline int io_is_decoded(paddr_t addr)
{
    uint16_t io_addr = 0;
    return io_decode_addr(addr, &io_addr);
}

#ifdef PICO_ON_DEVICE
int __not_in_flash_func(bus_addr_is_ram)(paddr_t addr)
#else
int bus_addr_is_ram(paddr_t addr)
#endif
{
    if (io_is_decoded(addr)) {
        return 0;
    }

    if (g_cfg.machine == BUS_MACHINE_LSI11_1104) {
        uint16_t a = (uint16_t)addr;
        return (a <= RAM_END) ? 1 : 0;
    }

    /*
     * PDP-11/84:
     * - 16-bit compatibility mode:
     *   reserve low 16-bit I/O page 0160000..0177777 and full 18-bit
     *   compatibility alias 0760000..0777777.
     * - 18-bit MMU mode: reserve 0760000..0777777 only.
     * - 22-bit mode: reserve 017760000..017777777 only.
     */
    if (g_pdp1184_io_16bit) {
        if (addr >= IO_PAGE_START && addr <= IO_PAGE_END) {
            return 0;
        }
        if (addr >= PDP11_18BIT_IO_PAGE_START && addr <= PDP11_18BIT_IO_PAGE_END) {
            return 0;
        }
    } else if (g_pdp1184_m22e) {
        if (addr >= PDP11_22BIT_IO_PAGE_START && addr <= PDP11_22BIT_IO_PAGE_END) {
            return 0;
        }
    } else {
        if (addr >= PDP11_18BIT_IO_PAGE_START && addr <= PDP11_18BIT_IO_PAGE_END) {
            return 0;
        }
    }

    return (addr < g_ram_bytes) ? 1 : 0;
}

#ifdef PICO_ON_DEVICE
int __not_in_flash_func(bus_range_is_ram)(paddr_t addr, size_t len)
#else
int bus_range_is_ram(paddr_t addr, size_t len)
#endif
{
    if (len == 0) {
        return 1;
    }

    for (size_t i = 0; i < len; i++) {
        if (!bus_addr_is_ram(addr + (paddr_t)i)) {
            return 0;
        }
    }
    return 1;
}

#ifdef PICO_ON_DEVICE
int __not_in_flash_func(bus_is_nxm)(paddr_t addr)
#else
int bus_is_nxm(paddr_t addr)
#endif
{
    if (io_is_decoded(addr)) {
        return 0;
    }

    if (bus_addr_is_ram(addr)) {
        return 0;
    }

    return 1;
}

#ifdef PICO_ON_DEVICE
uint8_t __not_in_flash_func(bus_read8)(paddr_t addr)
#else
uint8_t bus_read8(paddr_t addr)
#endif
{
    uint16_t io_addr = 0;

    if (io_decode_addr(addr, &io_addr)) {
        return devio_read8(io_addr);
    }

    if (bus_addr_is_ram(addr)) {
        return g_ram[addr];
    }

    return 0;
}

#ifdef PICO_ON_DEVICE
void __not_in_flash_func(bus_write8)(paddr_t addr, uint8_t v)
#else
void bus_write8(paddr_t addr, uint8_t v)
#endif
{
    uint16_t io_addr = 0;

    if (bus_watch_hit_byte(addr)) {
        fprintf(stderr, "BUSWATCH W8  pa=%08o v=%03o\n", (unsigned)addr,
                (unsigned)v);
    }

    if (io_decode_addr(addr, &io_addr)) {
        devio_write8(io_addr, v);
        return;
    }

    if (bus_addr_is_ram(addr)) {
        g_ram[addr] = v;
        return;
    }
}

#ifdef PICO_ON_DEVICE
uint16_t __not_in_flash_func(bus_read16)(paddr_t addr)
#else
uint16_t bus_read16(paddr_t addr)
#endif
{
    uint16_t io_addr = 0;

    if (io_decode_addr(addr, &io_addr)) {
        uint8_t lo = devio_read8(io_addr);
        uint8_t hi = devio_read8((uint16_t)(io_addr + 1));
        return (uint16_t)(lo | ((uint16_t)hi << 8));
    }

    if (bus_addr_is_ram(addr)) {
        return (uint16_t)(g_ram[addr] | ((uint16_t)g_ram[addr + 1] << 8));
    }

    return 0;
}

#ifdef PICO_ON_DEVICE
void __not_in_flash_func(bus_write16)(paddr_t addr, uint16_t v)
#else
void bus_write16(paddr_t addr, uint16_t v)
#endif
{
    uint16_t io_addr = 0;

    if (bus_watch_hit_word(addr)) {
        fprintf(stderr, "BUSWATCH W16 pa=%08o v=%06o\n", (unsigned)addr,
                (unsigned)v);
    }

    if (io_decode_addr(addr, &io_addr)) {
        devio_write8(io_addr, (uint8_t)(v & 000377));
        devio_write8((uint16_t)(io_addr + 1), (uint8_t)((v >> 8) & 000377));
        return;
    }

    if (bus_addr_is_ram(addr)) {
        g_ram[addr] = (uint8_t)(v & 000377);
        g_ram[addr + 1] = (uint8_t)((v >> 8) & 000377);
        return;
    }
}

static int inline vm2_bankable_ram_addr(uint16_t addr)
{
    /* CPU-side banking applies only to RAM window 000000..157777. */
    return (addr < IO_PAGE_START) ? 1 : 0;
}

static int inline vm2_cpu_is_nxm_byte(uint16_t addr, int halt_mode)
{
    uint16_t io_addr = 0;

    if (io_decode_addr((paddr_t)addr, &io_addr)) {
        return 0;
    }
    if (!bus_addr_is_ram((paddr_t)addr)) {
        return 1;
    }
    if (!vm2_bankable_ram_addr(addr)) {
        return 0;
    }
    if (halt_mode) {
        if (g_vm2_cfg.halt_ram_bytes == 0) {
            return 1;
        }
        return ((uint32_t)addr >= g_vm2_cfg.halt_ram_bytes) ? 1 : 0;
    }
    return ((uint32_t)addr >= g_vm2_cfg.user_ram_bytes) ? 1 : 0;
}

#ifdef PICO_ON_DEVICE
int __not_in_flash_func(bus_vm2_cpu_is_nxm)(uint16_t addr, int halt_mode)
#else
int bus_vm2_cpu_is_nxm(uint16_t addr, int halt_mode)
#endif
{
    return vm2_cpu_is_nxm_byte(addr, halt_mode ? 1 : 0);
}

#ifdef PICO_ON_DEVICE
uint8_t __not_in_flash_func(bus_vm2_cpu_read8)(uint16_t addr, int halt_mode)
#else
uint8_t bus_vm2_cpu_read8(uint16_t addr, int halt_mode)
#endif
{
    uint16_t io_addr = 0;

    if (io_decode_addr((paddr_t)addr, &io_addr)) {
        return devio_read8(io_addr);
    }
    if (vm2_cpu_is_nxm_byte(addr, halt_mode ? 1 : 0)) {
        return 0;
    }
    if (halt_mode && vm2_bankable_ram_addr(addr) && g_vm2_halt_ram) {
        return g_vm2_halt_ram[addr];
    }
    return g_ram[addr];
}

#ifdef PICO_ON_DEVICE
void __not_in_flash_func(bus_vm2_cpu_write8)(uint16_t addr, int halt_mode, uint8_t v)
#else
void bus_vm2_cpu_write8(uint16_t addr, int halt_mode, uint8_t v)
#endif
{
    uint16_t io_addr = 0;

    if (io_decode_addr((paddr_t)addr, &io_addr)) {
        devio_write8(io_addr, v);
        return;
    }
    if (vm2_cpu_is_nxm_byte(addr, halt_mode ? 1 : 0)) {
        return;
    }
    if (halt_mode && vm2_bankable_ram_addr(addr) && g_vm2_halt_ram) {
        g_vm2_halt_ram[addr] = v;
        return;
    }
    g_ram[addr] = v;
}

#ifdef PICO_ON_DEVICE
uint16_t __not_in_flash_func(bus_vm2_cpu_read16)(uint16_t addr, int halt_mode)
#else
uint16_t bus_vm2_cpu_read16(uint16_t addr, int halt_mode)
#endif
{
    uint8_t lo = bus_vm2_cpu_read8(addr, halt_mode);
    uint8_t hi = bus_vm2_cpu_read8((uint16_t)(addr + 1), halt_mode);
    return (uint16_t)(lo | ((uint16_t)hi << 8));
}

#ifdef PICO_ON_DEVICE
void __not_in_flash_func(bus_vm2_cpu_write16)(uint16_t addr, int halt_mode, uint16_t v)
#else
void bus_vm2_cpu_write16(uint16_t addr, int halt_mode, uint16_t v)
#endif
{
    bus_vm2_cpu_write8(addr, halt_mode, (uint8_t)(v & 000377));
    bus_vm2_cpu_write8((uint16_t)(addr + 1), halt_mode,
                       (uint8_t)((v >> 8) & 000377));
}

uint8_t *bus_ram_ptr(paddr_t addr)
{
    if (!bus_addr_is_ram(addr)) {
        return NULL;
    }
    return &g_ram[addr];
}
