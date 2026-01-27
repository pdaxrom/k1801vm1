#include "irq.h"

#define MAX_IRQ 32

static irq_source_t g_irq[MAX_IRQ];
static int g_n = 0;

/* NOTE: You must match core’s DCJ11 encoding convention here. */
static uint16_t encode_vec(regs *r, uint16_t v, uint8_t pri)
{
    /* If your core uses v|((pri&7)<<9) for DCJ11, do it here for all devices. */
    extern int regs_model_is_dcj11(const regs *r); /* or use r->model if available */
    (void)r;

    /* Template: replace with actual model check used in your project. */
    /* Default: no encoding */
    (void)pri;
    return v;
}

int irq_register(const irq_source_t *s)
{
    if (!s || !s->pending || !s->ack) return -1;
    if (g_n >= MAX_IRQ) return -1;
    g_irq[g_n++] = *s;
    return 0;
}

int irq_poll(regs *r, uint16_t *vec_out)
{
    int best = -1;
    uint8_t best_pri = 0;

    for (int i = 0; i < g_n; i++) {
        if (!g_irq[i].pending()) continue;

        if (best < 0 || g_irq[i].priority > best_pri) {
            best = i;
            best_pri = g_irq[i].priority;
        }
    }

    if (best < 0) return 0;

    uint16_t v = g_irq[best].vector;
    uint8_t pri = g_irq[best].priority;
    *vec_out = encode_vec(r, v, pri);

    g_irq[best].ack(); /* grant-time ack */
    return 1;
}
