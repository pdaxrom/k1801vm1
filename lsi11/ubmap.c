#include "ubmap.h"

#include "devio.h"

#include <stdint.h>
#include <string.h>

#define UBM_BASE 0170200
#define UBM_ENTRIES 32u
#define UBM_BYTES (UBM_ENTRIES * 4u)
#define UBM_LAST (uint16_t)(UBM_BASE + UBM_BYTES - 1u)
#define UBM_PAGE_SHIFT 13u
#define UBM_PAGE_MASK 0000037u
#define UBM_OFFSET_MASK 0017777u

#define PDP11_22BIT_IO_PAGE_START 017760000u
#define PDP11_22BIT_PHYS_MASK 017777777u

static uint32_t ubmap_regs[UBM_ENTRIES];
static int ubmap_registered;
static int ubmap_enabled_flag;

static uint8_t ubmap_read8(uint16_t addr)
{
    uint16_t rel = (uint16_t)(addr - UBM_BASE);
    uint16_t entry = (uint16_t)(rel >> 2);
    uint16_t lane = (uint16_t)(rel & 03u);
    uint32_t v = ubmap_regs[entry];

    switch (lane) {
    case 0:
        return (uint8_t)(v & 000376);
    case 1:
        return (uint8_t)((v >> 8) & 000377);
    case 2:
        return (uint8_t)((v >> 16) & 000077);
    default:
        return 0;
    }
}

static void ubmap_write8(uint16_t addr, uint8_t val)
{
    uint16_t rel = (uint16_t)(addr - UBM_BASE);
    uint16_t entry = (uint16_t)(rel >> 2);
    uint16_t lane = (uint16_t)(rel & 03u);
    uint32_t v = ubmap_regs[entry];

    switch (lane) {
    case 0:
        v = (v & ~000000377u) | ((uint32_t)(val & 000376));
        break;
    case 1:
        v = (v & ~000177400u) | ((uint32_t)val << 8);
        break;
    case 2:
        v = (v & ~017600000u) | (((uint32_t)val & 000077u) << 16);
        break;
    default:
        return;
    }
    ubmap_regs[entry] = v & PDP11_22BIT_PHYS_MASK & ~1u;
}

int ubmap_init(void)
{
    static const io_range_t io = {UBM_BASE, UBM_LAST, ubmap_read8, ubmap_write8,
                                  "UBMAP"
                                 };

    if (bus_machine() != BUS_MACHINE_PDP1184) {
        return 0;
    }

    if (!ubmap_registered) {
        if (devio_register(&io) != 0) {
            return -1;
        }
        ubmap_registered = 1;
    }

    ubmap_reset();
    return 0;
}

void ubmap_reset(void)
{
    memset(ubmap_regs, 0, sizeof(ubmap_regs));
    ubmap_enabled_flag = 0;
}

void ubmap_set_enabled(int on)
{
    ubmap_enabled_flag = on ? 1 : 0;
}

int ubmap_enabled(void)
{
    return ubmap_enabled_flag;
}

bus_paddr_t ubmap_map_addr(uint32_t unibus_addr)
{
    uint32_t page;
    uint32_t off;

    if (bus_machine() != BUS_MACHINE_PDP1184) {
        return (bus_paddr_t)(unibus_addr & 000777777u);
    }

    if (!ubmap_enabled_flag) {
        return (bus_paddr_t)(unibus_addr & 000777777u);
    }

    page = (unibus_addr >> UBM_PAGE_SHIFT) & UBM_PAGE_MASK;
    off = unibus_addr & UBM_OFFSET_MASK;

    if (page == UBM_PAGE_MASK) {
        return (bus_paddr_t)((PDP11_22BIT_IO_PAGE_START + off) & PDP11_22BIT_PHYS_MASK);
    }

    return (bus_paddr_t)((ubmap_regs[page] + off) & PDP11_22BIT_PHYS_MASK);
}
