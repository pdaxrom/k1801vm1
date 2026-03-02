#include "irq.h"

#ifdef PICO_ON_DEVICE
#include "pico/stdlib.h"
#endif

#include "../core/core.h"

#define MAX_IRQ 32

static irq_source_t g_irq[MAX_IRQ];
static int g_n = 0;

/* NOTE: You must match core’s DCJ11 encoding convention here. */
static inline uint16_t encode_vec(regs *r, uint16_t v, uint8_t pri)
{
    if (r && r->model == DCJ11) {
        return (uint16_t)(v | ((pri & 07) << 9));
    }
    return v;
}

int irq_register(const irq_source_t *s)
{
    if (!s || !s->pending || !s->ack) {
        return -1;
    }
    if (g_n >= MAX_IRQ) {
        return -1;
    }
    g_irq[g_n++] = *s;
    return 0;
}

#ifdef PICO_ON_DEVICE
int __not_in_flash_func(irq_poll)(regs *r, uint16_t *vec_out)
#else
int irq_poll(regs *r, uint16_t *vec_out)
#endif
{
    int best = -1;
    uint8_t best_pri = 0;
    int dcj11_psw_pri = 0;

    if (r && r->model == DCJ11) {
        dcj11_psw_pri = (r->psw >> 5) & 07;
    }

    for (int i = 0; i < g_n; i++) {
        if (!g_irq[i].pending()) {
            continue;
        }

        /*
         * Masked BR levels must not block delivery of lower unmasked
         * requests (important for KW11-L BR6 vs RH11/RK11/RL11 BR5).
         */
        if (r && r->model == DCJ11) {
            if (g_irq[i].priority && dcj11_psw_pri >= g_irq[i].priority) {
                continue;
            }
        }

        if (best < 0 || g_irq[i].priority > best_pri) {
            best = i;
            best_pri = g_irq[i].priority;
        }
    }

    if (best < 0) {
        return 0;
    }

    uint16_t v = g_irq[best].vector;
    uint8_t pri = g_irq[best].priority;
    if (r) {
        if (r->model == K1801VM1) {
            if (r->psw & 01000) {
                return 0;
            }
            if (v == 0160002) {
                if (r->psw & 02000) {
                    return 0;
                }
            } else {
                if (r->psw & 0200) {
                    return 0;
                }
            }
        } else if (r->model == K1801VM2 || r->model == K1806VM2) {
            if (r->psw & 0200) {
                return 0;
            }
        }
    }

    *vec_out = encode_vec(r, v, pri);
    g_irq[best].ack(); /* grant-time ack (only if acceptable) */
    return 1;
}
