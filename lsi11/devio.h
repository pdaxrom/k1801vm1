#ifndef DEVIO_H_
#define DEVIO_H_

#include <stdint.h>

#ifdef PICO_ON_DEVICE
#include "pico/stdlib.h"
#endif

typedef uint8_t (*io_read8_fn)(uint16_t addr);
typedef void    (*io_write8_fn)(uint16_t addr, uint8_t val);

typedef struct {
    uint16_t start;      /* inclusive */
    uint16_t end;        /* inclusive */
    io_read8_fn  read8;
    io_write8_fn write8;
    const char  *name;
} io_range_t;

int devio_register(const io_range_t *r);

int     devio_has(uint16_t addr);
uint8_t devio_read8(uint16_t addr);
void    devio_write8(uint16_t addr, uint8_t v);

#endif
