#ifndef IRQ_LATCH_H_
#define IRQ_LATCH_H_

#include <stdint.h>

typedef struct {
    uint8_t done;       /* mirror of DONE bit */
    uint8_t ie;         /* mirror of IE bit */
    uint8_t irq_req;    /* latched request; cleared on CPU ack */
    uint8_t irq_armed;  /* 1 only when DONE==0 */
} irq_latch_t;

static inline void irq_latch_reset(irq_latch_t *l)
{
    l->done = 0;
    l->ie = 0;
    l->irq_req = 0;
    l->irq_armed = 1;
}

/* Device event => sets DONE once. No repeat while DONE==1. */
static inline void irq_latch_event_set_done(irq_latch_t *l)
{
    if (l->done) return;
    l->done = 1;
    if (l->ie && l->irq_armed) {
        l->irq_req = 1;
        l->irq_armed = 0;
    }
}

/* CPU acknowledge => clears only irq_req. DONE unchanged. */
static inline void irq_latch_ack(irq_latch_t *l)
{
    l->irq_req = 0;
}

/* Software-defined operation clears DONE => re-arm + clear irq_req. */
static inline void irq_latch_sw_clear_done(irq_latch_t *l)
{
    l->done = 0;
    l->irq_req = 0;
    l->irq_armed = 1;
}

/* Set IE. IMPORTANT: per policy, DO NOT raise irq_req on IE toggle if DONE==1. */
static inline void irq_latch_set_ie(irq_latch_t *l, int ie)
{
    l->ie = ie ? 1 : 0;
}

#endif
