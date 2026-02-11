#ifndef IRQ_H_
#define IRQ_H_

#include <stdint.h>
#include "../core/core.h"

typedef struct {
    const char *name;
    uint16_t vector;  /* e.g. 000060 */
    uint8_t priority; /* 0..7 */
    int (*pending)(void);
    void (*ack)(void);
} irq_source_t;

int irq_register(const irq_source_t *s);

/* Returns 1 and sets *vec_out when an IRQ is delivered; else 0. */
int irq_poll(regs *r, uint16_t *vec_out);

#endif
