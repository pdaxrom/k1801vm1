/*
 * core.h
 *
 *  Created on: 19.10.2016
 *      Author: Alexander Chukov <sash@pdaXrom.org>
 */

#ifndef _CORE_CORE_H_
#define _CORE_CORE_H_

#include <stdint.h>

// For DCJ-11 in PDP11/84 this disabled
//#define DCJ_REG_RSVD_ENABLED

#ifndef INLINE
#define INLINE inline
#endif

#ifndef WARN_UNUSED_RESULT
#if defined(__has_attribute)
#if __has_attribute(warn_unused_result)
#define WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#else
#define WARN_UNUSED_RESULT
#endif
#elif defined(__GNUC__)
#define WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#else
#define WARN_UNUSED_RESULT
#endif
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

#if defined(ENABLE_MMU) && (ENABLE_MMU)
typedef struct {
    uint8_t *host_base;
    uint16_t valid_min_flags;
    uint16_t valid_max;
} mmu_tlb_entry_t;
#endif

typedef struct _regs {
    byte model;

    word psw, r[8];
    word ir;
    word instr_pc;
    /* DCJ11 dual register set banks for R0..R5 (selected by PSW<11>). */
    word rset_bank[2][6];
    byte rset_bank_init;
    /* Banked stack pointers for DCJ11 protection modes: K/S/U. */
    word sp_mode[4];
    byte sp_mode_init;
    /* DCJ11 delayed stack trap state (yellow stack at end of instruction). */
    byte dcj11_yellow_pending;
    byte dcj11_stack_trap_active;
    /* Set when an instruction explicitly writes PSW via @#177776. */
    byte dcj11_explicit_psw_write;
    /* Set while trap/interrupt stack frame words are being pushed. */
    byte dcj11_vector_push_active;
    /* Old state to push for an active vector sequence (for red-stack fallback). */
    word dcj11_vector_old_pc;
    word dcj11_vector_old_psw;

    word cps, cpc; /* 0177676 0177674 */

    word SEL0;                    /* unaddressed SEL register */
    word J11_MEMERR;              /* 0177744: memory system error register */
    word J11_CCR;                 /* 0177746: cache control register */
    word J11_MAINT;               /* 0177750: maintenance register */
    word J11_HITMISS;             /* 0177752: cache hit/miss register */
#ifdef DCJ_REG_RSVD_ENABLED
    word J11_RSVD_177754;         /* 0177754: reserved/implementation-defined */
    word J11_RSVD_177756;         /* 0177756: reserved/implementation-defined */
    word J11_RSVD_177760;         /* 0177760: reserved/implementation-defined */
    word J11_RSVD_177762;         /* 0177762: reserved/implementation-defined */
    word J11_RSVD_177764;         /* 0177764: reserved/implementation-defined */
#endif
    word J11_CPUERR;              /* 0177766: CPU error register */
    word J11_RSVD_177770;         /* 0177770: reserved/implementation-defined */
    word J11_PIRQ;                /* 0177772: program interrupt request register */
    word J11_STKLIM;              /* 0177774: stack limit register */
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
    /* Per-instance transient FP11 instruction state. */
    byte fp11_reg_delta_mask;
    sbyte fp11_reg_delta[8];
    byte fp11_trap_pending;
    word fp11_trap_old_pc;
    word fp11_trap_old_psw;
    word fp11_backup_pc;

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

    mmu_tlb_entry_t mmu_tlb[4][2][8];
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
    /* Per-instance hwstub backing store; NULL for non-hwstub backends. */
    uint8_t *hwstub_mem;
    uint32_t hwstub_mem_size;
} regs;

WARN_UNUSED_RESULT int core_init(regs *r);
void core_reset(regs *r);
int core_step(regs *r);
void core_fini(regs *r);
void core_bus_error_trap(regs *r);

#endif
