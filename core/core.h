/*
 * core.h
 *
 *  Created on: 19.10.2016
 *      Author: Alexander Chukov <sash@pdaXrom.org>
 */

#ifndef _CORE_CORE_H_
#define _CORE_CORE_H_

#include <stdint.h>

#ifndef INLINE
#define INLINE inline
#endif

#ifndef ENABLE_MMU
#define ENABLE_MMU 0
#endif

#ifndef byte
#define byte uint8_t
#endif
#ifndef word
#define word uint16_t
#endif
#ifndef dword
#define dword uint32_t
#endif

#ifndef sbyte
#define sbyte int8_t
#endif
#ifndef sword
#define sword int16_t
#endif
#ifndef sdword
#define sdword int32_t
#endif

enum {
    K1801VM1 = 0,
    K1801VM1G,
    K1801VM2,
    K1806VM2,
    DCJ11,
};

#define SET_BIT(a) (1 << a)

#define BIT_H 8
#define BIT_P 7
#define BIT_T 4
#define BIT_N 3
#define BIT_Z 2
#define BIT_V 1
#define BIT_C 0

#define FLAG_H SET_BIT(BIT_H)
#define FLAG_P SET_BIT(BIT_P)
#define FLAG_T SET_BIT(BIT_T)
#define FLAG_N SET_BIT(BIT_N)
#define FLAG_Z SET_BIT(BIT_Z)
#define FLAG_V SET_BIT(BIT_V)
#define FLAG_C SET_BIT(BIT_C)

union u_word {
    word u;
    sword s;
};

union u_dword {
    dword u;
    sdword s;
};

/* SIMH PDP-11 FPU definitions used by pdp11_fp.c */
typedef struct {
    uint32_t l;
    uint32_t h;
} fpac_t;

typedef struct _regs {
    byte model;

    word psw, r[8];
    word ir;
    /* Banked stack pointers for DCJ11 protection modes: K/S/U. */
    word sp_mode[4];
    byte sp_mode_init;

    word cps, cpc; /* 0177676 0177674 */

    word SEL0;                    /* unaddressed SEL register */
    word J11_REG177744;           /* DCJ11 CPU register at 0177744 */
    word J11_REG177746;           /* DCJ11 CPU register at 0177746 */
    word J11_REG177750;           /* DCJ11 CPU register at 0177750 */
    word J11_REG177752_177766[8]; /* DCJ11 CPU registers at 0177752..0177766 */
    word TVE_LIMIT;               /* 0177706 */
    word TVE_COUNT;               /* 0177710 */
    word TVE_CSR;                 /* 0177712 */
    byte TVE_PENDING;
    byte VM1_RAP_PRESENT;

    word fTrap;

    word fWait;
    word fAbort;
    word fHaltSignal;
    word fStepDeferHalt;

    /* FIS */
    byte has_fis;
    /* FPU state */
    byte has_fpu;
    fpac_t fpu_fr[6];
    uint32_t fpu_fps;
    uint32_t fpu_fea;
    uint32_t fpu_fec;

#if defined(ENABLE_MMU) && (ENABLE_MMU)
    /*
     * DCJ11 MMU state (PDP-11 MMR/PAR/PDR model).
     * Indexed as [mode][space][segment]:
     *   mode: 0=kernel, 1=supervisor, 3=user (2 reserved)
     *   space: 0=I-space, 1=D-space
     *   segment: 0..7
     */
    word mmu_ssr0;
    word mmu_ssr1;
    word mmu_ssr2;
    word mmu_ssr3;
    word mmu_par[4][2][8];
    word mmu_pdr[4][2][8];

    struct {
        uint8_t *host_read_base;
        uint8_t *host_write_base;
        uint16_t valid_min;
        uint16_t valid_max;
    } mmu_tlb[4][2][8];
#endif

    byte (*load_byte)(struct _regs *r, word offset);
    void (*store_byte)(struct _regs *r, word offset, byte value);
    word (*load_word)(struct _regs *r, word offset);
    void (*store_word)(struct _regs *r, word offset, word value);

    /*
     * Optional physical-address callbacks (22-bit capable).
     * When NULL, core falls back to 16-bit callbacks.
     */
    byte (*load_byte_pa)(struct _regs *r, dword offset);
    void (*store_byte_pa)(struct _regs *r, dword offset, byte value);
    word (*load_word_pa)(struct _regs *r, dword offset);
    void (*store_word_pa)(struct _regs *r, dword offset, word value);

    int (*init)(struct _regs *r);
    void (*reset)(struct _regs *r);
    void (*fini)(struct _regs *r);

    int (*poll_irq)(struct _regs *r, word *vector);

    byte *(*ramptr)(struct _regs *r, word offset);

    /*
     * Cached direct pointer to base of RAM, set once at init.
     * Used by core_step fast-path to bypass callback chain for RAM accesses.
     * NULL if not available. Size in bytes stored alongside.
     */
    uint8_t *ram_fast;
    uint32_t ram_fast_size;
} regs;

void core_init(regs *r);
void core_reset(regs *r);
int core_step(regs *r);
void core_fini(regs *r);

#endif
