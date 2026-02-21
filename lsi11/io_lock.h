#ifndef IO_LOCK_H_
#define IO_LOCK_H_

/*
 * Lightweight I/O spinlock for dual-core synchronization on RP2040.
 * Protects device register access (devio), IRQ polling, and device polling
 * between core 0 (peripherals) and core 1 (CPU emulation).
 *
 * On non-Pico builds, the lock is a no-op.
 */

#if defined(PICO_ON_DEVICE)

#include "hardware/sync.h"

/* Claimed at startup in main.c */
extern spin_lock_t *g_io_spinlock;

static inline uint32_t io_lock_acquire(void)
{
    return spin_lock_blocking(g_io_spinlock);
}

static inline void io_lock_release(uint32_t saved_irq)
{
    spin_unlock(g_io_spinlock, saved_irq);
}

#else /* host build: no-op */

#include <stdint.h>

static inline uint32_t io_lock_acquire(void)
{
    return 0;
}

static inline void io_lock_release(uint32_t saved_irq)
{
    (void)saved_irq;
}

#endif

#endif /* IO_LOCK_H_ */
