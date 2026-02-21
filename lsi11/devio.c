#include "devio.h"
#include "io_lock.h"
#include <stddef.h>

#define IO_PAGE_START 0160000
#define IO_PAGE_END 0177777

#define MAX_RANGES 64

static io_range_t g_ranges[MAX_RANGES];
static int g_count = 0;

static int range_valid(const io_range_t *r)
{
    if (!r || !r->read8 || !r->write8) {
        return 0;
    }
    if (r->start > r->end) {
        return 0;
    }

    /* Allow aliases that fall outside IO page ONLY if you intentionally register
       them. For PDP-11/03-like, we still allow 0176500 alias (it is within IO
       page anyway). */
    return 1;
}

int devio_register(const io_range_t *r)
{
    if (!range_valid(r)) {
        return -1;
    }
    if (g_count >= MAX_RANGES) {
        return -1;
    }
    g_ranges[g_count++] = *r;
    return 0;
}

int devio_has(uint16_t addr)
{
    int i;
    for (i = 0; i < g_count; i++) {
        if (addr >= g_ranges[i].start && addr <= g_ranges[i].end) {
            return 1;
        }
    }
    return 0;
}

uint8_t devio_read8(uint16_t addr)
{
    int i;
    uint32_t irqstate = io_lock_acquire();
    for (i = 0; i < g_count; i++) {
        if (addr >= g_ranges[i].start && addr <= g_ranges[i].end) {
            uint8_t v = g_ranges[i].read8(addr);
            io_lock_release(irqstate);
            return v;
        }
    }
    io_lock_release(irqstate);
    return 0;
}

void devio_write8(uint16_t addr, uint8_t v)
{
    int i;
    uint32_t irqstate = io_lock_acquire();
    for (i = 0; i < g_count; i++) {
        if (addr >= g_ranges[i].start && addr <= g_ranges[i].end) {
            g_ranges[i].write8(addr, v);
            io_lock_release(irqstate);
            return;
        }
    }
    io_lock_release(irqstate);
}
