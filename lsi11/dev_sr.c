#include "dev_sr.h"
#include "devio.h"

/* SR address (octal) */
#define SR_ADDR 0177570

static uint16_t sr_value = 000000;

static uint8_t sr_read8(uint16_t a)
{
    if (a == SR_ADDR) {
        return (uint8_t)(sr_value & 000377);
    }
    if (a == (uint16_t)(SR_ADDR + 1)) {
        return (uint8_t)((sr_value >> 8) & 000377);
    }
    return 0;
}

static void sr_write8(uint16_t a, uint8_t v)
{
    (void)a;
    (void)v;
    /* read-only: ignore writes */
}

int sr_init(void)
{
    static const io_range_t r = { 0177570, 0177571, sr_read8, sr_write8, "SR" };
    if (devio_register(&r) != 0) {
        return -1;
    }
    sr_reset();
    return 0;
}

void sr_reset(void)
{
    sr_value = 000000;
}

void sr_set(uint16_t v)
{
    sr_value = v;
}
