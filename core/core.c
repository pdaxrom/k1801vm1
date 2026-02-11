/*
 * core.c
 *
 *  Created on: 19.10.2016
 *      Author: Alexander Chukov <sash@pdaXrom.org>
 */

#include "core.h"
#include "hardware.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef MMU_STUB_REGS_WHEN_DISABLED
#define MMU_STUB_REGS_WHEN_DISABLED 1
#endif

#define TYPE_WORD 0
#define TYPE_BYTE 1

#define TYPE_REG 0
#define TYPE_MEM 1
#define TYPE_ERROR 2
#define TYPE_IFETCH 3

#define MPI 0077777   /* most positive integer */
#define MNI 0100000   /* most negative integer */
#define NEG_1 0177777 /* negative one */
#define SIGN 0100000  /* sign bit */
#define CARRY 0200000 /* set if carry out */

#define MPI_B 0177   /* most positive integer (byte) */
#define MNI_B 0200   /* most negative integer (byte) */
#define NEG_1_B 0377 /* negative one (byte) */
#define SIGN_B 0200  /* sign bit (byte) */
#define CARRY_B 0400 /* set if carry out (byte) */

#define unimpl()                                                               \
  {                                                                            \
    fprintf(stderr, "Unimplemented %o at %o\n", op, r->r[7]);                  \
    break;                                                                     \
  }

#define clear_flag(f)                                                          \
  {                                                                            \
    r->psw &= ~(f);                                                            \
  }

#define set_flag(f)                                                            \
  {                                                                            \
    r->psw |= (f);                                                             \
  }

#define flag_is_set(f) ((r->psw & (f)) == (f))
#define flag_is_clear(f) ((r->psw & (f)) == 0)

#define set_flag_if(a, b)                                                      \
  {                                                                            \
    if (a) {                                                                   \
      set_flag(b);                                                             \
    } else {                                                                   \
      clear_flag(b);                                                           \
    }                                                                          \
  }

#define is_vm2(r) ((r)->model == K1801VM2 || (r)->model == K1806VM2)
#define has_prev_space_ops(r) (is_vm2(r) || (r)->model == DCJ11)
static INLINE int dcj11_kernel_psw(word psw)
{
    return ((psw >> 14) & 03) == 0;
}

enum {
    DCJ11_REG_NONE = 0,
    DCJ11_REG_MEMERR_177744,
    DCJ11_REG_CCR_177746,
    DCJ11_REG_MAINT_177750,
    DCJ11_REG_HITMISS_177752,
    DCJ11_REG_CPUERR_177766
};

static INLINE int dcj11_reg_select(word offset)
{
    switch (offset & 0177776) {
    case 0177744:
        return DCJ11_REG_MEMERR_177744;
    case 0177746:
        return DCJ11_REG_CCR_177746;
    case 0177750:
        return DCJ11_REG_MAINT_177750;
    case 0177752:
        return DCJ11_REG_HITMISS_177752;
    case 0177766:
        return DCJ11_REG_CPUERR_177766;
    default:
        return DCJ11_REG_NONE;
    }
}

static INLINE int dcj11_has_reg_block_177744(regs *r, word offset)
{
    return (r->model == DCJ11) && (dcj11_reg_select(offset) != DCJ11_REG_NONE);
}

static INLINE word dcj11_reg_block_load_word(regs *r, word offset)
{
    switch (dcj11_reg_select(offset)) {
    case DCJ11_REG_MEMERR_177744:
        return r->J11_REG177744;
    case DCJ11_REG_CCR_177746:
        return r->J11_REG177746;
    case DCJ11_REG_MAINT_177750:
        return r->J11_REG177750;
    case DCJ11_REG_HITMISS_177752:
        return r->J11_REG177752_177766[0];
    case DCJ11_REG_CPUERR_177766:
        return (word)(r->J11_REG177752_177766[6] & 0000374);
    default:
        return 0;
    }
}

static INLINE void dcj11_reg_block_store_word(regs *r, word offset,
        word value)
{
    switch (dcj11_reg_select(offset)) {
    case DCJ11_REG_MEMERR_177744:
        /* Memory system error register: cleared by any write. */
        r->J11_REG177744 = 0;
        return;
    case DCJ11_REG_CCR_177746:
        r->J11_REG177746 = value;
        return;
    case DCJ11_REG_MAINT_177750:
        /* Maintenance register is read-only in normal mode. */
        return;
    case DCJ11_REG_HITMISS_177752:
        /* Hit/miss register clears on write. */
        r->J11_REG177752_177766[0] = 0;
        return;
    case DCJ11_REG_CPUERR_177766:
        /* CPU error register clears on write. */
        r->J11_REG177752_177766[6] = 0;
        return;
    default:
        return;
    }
}

static INLINE int cpu_has_reg_177776(word offset)
{
    return ((offset & 0177776) == 0177776);
}

static INLINE byte dcj11_reg_177750_load_byte(regs *r, word offset)
{
    word value = dcj11_reg_block_load_word(r, offset);
    return (byte)((offset & 1) ? ((value >> 8) & 0377) : (value & 0377));
}

static INLINE void dcj11_reg_177750_store_byte(regs *r, word offset,
        byte value)
{
    int reg = dcj11_reg_select(offset);
    if (reg == DCJ11_REG_MEMERR_177744 || reg == DCJ11_REG_HITMISS_177752 ||
            reg == DCJ11_REG_CPUERR_177766 || reg == DCJ11_REG_MAINT_177750) {
        dcj11_reg_block_store_word(r, offset, 0);
        return;
    }

    word regv = dcj11_reg_block_load_word(r, offset);
    if (offset & 1) {
        regv = (word)((regv & 000377) | (((word)value & 0377) << 8));
    } else {
        regv = (word)((regv & 0177400) | ((word)value & 0377));
    }
    dcj11_reg_block_store_word(r, offset, regv);
}

static INLINE byte cpu_reg_177776_load_byte(regs *r, word offset)
{
    return (byte)((offset & 1) ? ((r->psw >> 8) & 0377) : (r->psw & 0377));
}

static INLINE void cpu_reg_177776_store_byte(regs *r, word offset, byte value)
{
    if (offset & 1) {
        r->psw = (word)((r->psw & 000377) | (((word)value & 0377) << 8));
    } else {
        r->psw = (word)((r->psw & 0177400) | ((word)value & 0377));
    }
}

static INLINE word trap_psw(regs *r, word old_psw, word vec_psw)
{
    if (is_vm2(r)) {
        word old_mode = (old_psw >> 14) & 03;
        /* Trap always enters kernel mode; previous mode keeps old current mode. */
        vec_psw = (word)((vec_psw & ~0170000) | (old_mode << 12));
    }
    return vec_psw;
}

#define pushw(v)                                                               \
  {                                                                            \
    r->r[6] -= 2;                                                              \
    store_word(r, r->r[6], v);                                                 \
  }

#define pullw(v)                                                               \
  {                                                                            \
    v = load_word(r, r->r[6]);                                                 \
    r->r[6] += 2;                                                              \
  }

static INLINE int irq_accept(regs *r, word irq_vector, word *vec_out)
{
    word vec = (r->model == K1801VM1 || r->model == K1801VM1G)
               ? irq_vector
               : (irq_vector & 0777);
    int pri = (irq_vector >> 9) & 07;
    int psw_pri = (r->psw >> 5) & 07;
    if (r->model == K1801VM1 || r->model == K1801VM1G) {
        const word psw10 = 01000;
        const word psw11 = 02000;
        if (r->psw & psw10) {
            return 0;
        }
        if ((vec == 0160002) && (r->psw & psw11)) {
            return 0;
        }
        if ((vec != 0160002) && flag_is_set(FLAG_P)) {
            return 0;
        }
        *vec_out = vec & 0177776;
        return 1;
    }
    if (is_vm2(r)) {
        vec = irq_vector;
        if (r->psw & FLAG_P) {
            return 0;
        }
        *vec_out = vec & 0177776;
        return 1;
    }
    if (pri && psw_pri >= pri) {
        return 0;
    }
    *vec_out = vec & 017776;
    return 1;
}

void core_init(regs *r)
{
    if (r->model == K1806VM2) {
        r->model = K1801VM2;
    }
    r->init(r);
}

void core_reset(regs *r)
{
    r->r[7] = r->SEL0 & 0177400;
    r->psw = 0340;
    r->ir = 0;
    r->J11_REG177744 = 0;
    r->J11_REG177746 = 0;
    r->J11_REG177750 = 0;
    memset(r->J11_REG177752_177766, 0, sizeof(r->J11_REG177752_177766));
    r->fWait = 0;
    r->fTrap = 0;
    r->fAbort = 0;
    r->fHaltSignal = 0;
    r->fStepDeferHalt = 0;
    r->fFisError = 0;
#if defined(ENABLE_MMU) && (ENABLE_MMU)
    r->mmu_ssr0 = 0;
    r->mmu_ssr1 = 0;
    r->mmu_ssr2 = 0;
    r->mmu_ssr3 = 0;
    memset(r->mmu_par, 0, sizeof(r->mmu_par));
    memset(r->mmu_pdr, 0, sizeof(r->mmu_pdr));
#endif

    r->reset(r);
}

void core_fini(regs *r)
{
    if (r->fini) {
        r->fini(r);
    }
}

#define raw_load_byte(a, b) (((a)->load_byte)((a), (b)))
#define raw_store_byte(a, b, c) (((a)->store_byte)((a), (b), (c)))
#define raw_load_word(a, b) (((a)->load_word)((a), (b)))
#define raw_store_word(a, b, c) (((a)->store_word)((a), (b), (c)))

static INLINE byte raw_load_byte_phys(regs *r, dword pa)
{
    /*
     * Keep legacy 16-bit path for low addresses so existing
     * memory-mapped register decode remains intact.
     */
    if (pa <= 0177777) {
        return raw_load_byte(r, (word)pa);
    }
    if (r->load_byte_pa) {
        return r->load_byte_pa(r, pa);
    }
    return raw_load_byte(r, (word)pa);
}

static INLINE void raw_store_byte_phys(regs *r, dword pa, byte value)
{
    if (pa <= 0177777) {
        raw_store_byte(r, (word)pa, value);
        return;
    }
    if (r->store_byte_pa) {
        r->store_byte_pa(r, pa, value);
        return;
    }
    raw_store_byte(r, (word)pa, value);
}

static INLINE word raw_load_word_phys(regs *r, dword pa)
{
    if (pa <= 0177777) {
        return raw_load_word(r, (word)pa);
    }
    if (r->load_word_pa) {
        return r->load_word_pa(r, pa);
    }
    return raw_load_word(r, (word)pa);
}

static INLINE void raw_store_word_phys(regs *r, dword pa, word value)
{
    if (pa <= 0177777) {
        raw_store_word(r, (word)pa, value);
        return;
    }
    if (r->store_word_pa) {
        r->store_word_pa(r, pa, value);
        return;
    }
    raw_store_word(r, (word)pa, value);
}

enum {
    MMU_FAULT_NONE = 0,
    MMU_FAULT_NONRES = 1,
    MMU_FAULT_LENGTH = 2,
    MMU_FAULT_PROTECT = 3,
};

/* MMR/SSR and PAR/PDR map (octal) */
#define MMU_SSR0 0177572
#define MMU_SSR1 0177574
#define MMU_SSR2 0177576
/*
 * MMR3/SSR3 is 0172516 on classic 11/34-style systems.
 * Keep 0177516 as a compatibility alias for existing DCJ11-oriented code.
 */
#define MMU_SSR3 0172516
#define MMU_SSR3_ALT 0177516

#define MMU_SUP_I_PDR_BASE 0172200
#define MMU_SUP_D_PDR_BASE 0172220
#define MMU_SUP_I_PAR_BASE 0172240
#define MMU_SUP_D_PAR_BASE 0172260

#define MMU_KER_I_PDR_BASE 0172300
#define MMU_KER_D_PDR_BASE 0172320
#define MMU_KER_I_PAR_BASE 0172340
#define MMU_KER_D_PAR_BASE 0172360

#define MMU_USR_I_PDR_BASE 0177600
#define MMU_USR_D_PDR_BASE 0177620
#define MMU_USR_I_PAR_BASE 0177640
#define MMU_USR_D_PAR_BASE 0177660

#define MMU_SSR0_ENABLE 0000001
#define MMU_SSR0_SEG_SHIFT 1
#define MMU_SSR0_MODE_SHIFT 5
#define MMU_SSR0_FAULT 0100000
#define MMU_SSR0_NONRES 0004000
#define MMU_SSR0_LENGTH 0002000
#define MMU_SSR0_PROTECT 0001000

#define MMU_SSR3_KD 0000001
#define MMU_SSR3_SD 0000002
#define MMU_SSR3_UD 0000004

#define MMU_TRAP_VECTOR 0000250

static INLINE void bus_error_trap(regs *r);
static INLINE void mmu_fault_trap(regs *r, word va, word pc, int fault,
                                  int mode, int seg);
static int core_trace_mmu_boot = -1;

static INLINE int mmu_decode_parpdr(word addr, int *mode, int *space,
                                    int *is_par, int *seg)
{
    word a = (word)(addr & 0177776);

#define MMU_DEC(base, m, s, p)                                                 \
  do {                                                                         \
    if (a >= (base) && a <= (word)((base) + 016) &&                            \
        (((a) - (base)) & 1) == 0) {                                           \
      if (mode)                                                                \
        *mode = (m);                                                           \
      if (space)                                                               \
        *space = (s);                                                          \
      if (is_par)                                                              \
        *is_par = (p);                                                         \
      if (seg)                                                                 \
        *seg = (int)(((a) - (base)) >> 1);                                     \
      return 1;                                                                \
    }                                                                          \
  } while (0)

    MMU_DEC(MMU_SUP_I_PDR_BASE, 1, 0, 0);
    MMU_DEC(MMU_SUP_D_PDR_BASE, 1, 1, 0);
    MMU_DEC(MMU_SUP_I_PAR_BASE, 1, 0, 1);
    MMU_DEC(MMU_SUP_D_PAR_BASE, 1, 1, 1);

    MMU_DEC(MMU_KER_I_PDR_BASE, 0, 0, 0);
    MMU_DEC(MMU_KER_D_PDR_BASE, 0, 1, 0);
    MMU_DEC(MMU_KER_I_PAR_BASE, 0, 0, 1);
    MMU_DEC(MMU_KER_D_PAR_BASE, 0, 1, 1);

    MMU_DEC(MMU_USR_I_PDR_BASE, 3, 0, 0);
    MMU_DEC(MMU_USR_D_PDR_BASE, 3, 1, 0);
    MMU_DEC(MMU_USR_I_PAR_BASE, 3, 0, 1);
    MMU_DEC(MMU_USR_D_PAR_BASE, 3, 1, 1);

#undef MMU_DEC
    return 0;
}

static INLINE int mmu_is_reg_address(word addr)
{
    int mode, space, is_par, seg;
    word a = (word)(addr & 0177776);

#if (!defined(ENABLE_MMU) || !(ENABLE_MMU))
#if !(MMU_STUB_REGS_WHEN_DISABLED)
    return 0;
#endif
#endif

    if (a == MMU_SSR0 || a == MMU_SSR1 || a == MMU_SSR2 || a == MMU_SSR3 ||
            a == MMU_SSR3_ALT) {
        return 1;
    }
    return mmu_decode_parpdr(a, &mode, &space, &is_par, &seg);
}

static INLINE int mmu_io_read_word(regs *r, word addr, word *value_out)
{
    int mode, space, is_par, seg;
    word a = (word)(addr & 0177776);

#if (!defined(ENABLE_MMU) || !(ENABLE_MMU))
#if !(MMU_STUB_REGS_WHEN_DISABLED)
    (void)r;
    (void)addr;
    (void)value_out;
    return 0;
#endif
#endif

    if (r->model != DCJ11) {
        return 0;
    }

    if (a == MMU_SSR0) {
#if defined(ENABLE_MMU) && (ENABLE_MMU)
        *value_out = r->mmu_ssr0;
#else
        *value_out = 0;
#endif
        return 1;
    }
    if (a == MMU_SSR1) {
#if defined(ENABLE_MMU) && (ENABLE_MMU)
        *value_out = r->mmu_ssr1;
#else
        *value_out = 0;
#endif
        return 1;
    }
    if (a == MMU_SSR2) {
#if defined(ENABLE_MMU) && (ENABLE_MMU)
        *value_out = r->mmu_ssr2;
#else
        *value_out = 0;
#endif
        return 1;
    }
    if (a == MMU_SSR3 || a == MMU_SSR3_ALT) {
#if defined(ENABLE_MMU) && (ENABLE_MMU)
        *value_out = r->mmu_ssr3;
#else
        *value_out = 0;
#endif
        return 1;
    }
    if (mmu_decode_parpdr(a, &mode, &space, &is_par, &seg)) {
#if defined(ENABLE_MMU) && (ENABLE_MMU)
        *value_out =
            is_par ? r->mmu_par[mode][space][seg] : r->mmu_pdr[mode][space][seg];
#else
        *value_out = 0;
#endif
        return 1;
    }

    return 0;
}

static INLINE int mmu_io_write_word(regs *r, word addr, word value)
{
    int mode, space, is_par, seg;
    word a = (word)(addr & 0177776);

#if (!defined(ENABLE_MMU) || !(ENABLE_MMU))
#if !(MMU_STUB_REGS_WHEN_DISABLED)
    (void)r;
    (void)addr;
    (void)value;
    return 0;
#endif
#endif

    if (r->model != DCJ11) {
        return 0;
    }

    if (a == MMU_SSR0) {
#if defined(ENABLE_MMU) && (ENABLE_MMU)
        r->mmu_ssr0 = value;
        if (core_trace_mmu_boot > 0) {
            fprintf(stderr, "MMU SSR0 <= %06o\n", value);
        }
#else
        (void)value;
#endif
        return 1;
    }
    if (a == MMU_SSR1) {
#if defined(ENABLE_MMU) && (ENABLE_MMU)
        r->mmu_ssr1 = value;
#else
        (void)value;
#endif
        return 1;
    }
    if (a == MMU_SSR2) {
#if defined(ENABLE_MMU) && (ENABLE_MMU)
        r->mmu_ssr2 = value;
#else
        (void)value;
#endif
        return 1;
    }
    if (a == MMU_SSR3 || a == MMU_SSR3_ALT) {
#if defined(ENABLE_MMU) && (ENABLE_MMU)
        r->mmu_ssr3 = value;
        if (core_trace_mmu_boot > 0) {
            fprintf(stderr, "MMU SSR3 <= %06o (addr=%06o)\n", value, a);
        }
#else
        (void)value;
#endif
        return 1;
    }
    if (mmu_decode_parpdr(a, &mode, &space, &is_par, &seg)) {
#if defined(ENABLE_MMU) && (ENABLE_MMU)
        if (is_par) {
            r->mmu_par[mode][space][seg] = value;
        } else {
            r->mmu_pdr[mode][space][seg] = value;
        }
#else
        (void)value;
#endif
        return 1;
    }

    return 0;
}

static INLINE int mmu_io_read_byte(regs *r, word addr, byte *value_out)
{
    word w;
    word wa = (word)(addr & 0177776);
    if (!mmu_io_read_word(r, wa, &w)) {
        return 0;
    }
    *value_out = (addr & 1) ? (byte)((w >> 8) & 0377) : (byte)(w & 0377);
    return 1;
}

static INLINE int mmu_io_write_byte(regs *r, word addr, byte value)
{
    word w;
    word wa = (word)(addr & 0177776);

    if (!mmu_is_reg_address(wa) || r->model != DCJ11) {
        return 0;
    }

    if (!mmu_io_read_word(r, wa, &w)) {
        w = 0;
    }
    if (addr & 1) {
        w = (word)((w & 0377) | ((word)value << 8));
    } else {
        w = (word)((w & 0177400) | value);
    }
    return mmu_io_write_word(r, wa, w);
}

#if defined(ENABLE_MMU) && (ENABLE_MMU)
static INLINE int mmu_split_enabled(const regs *r, int mode)
{
    switch (mode) {
    case 0:
        return (r->mmu_ssr3 & MMU_SSR3_KD) ? 1 : 0;
    case 1:
        return (r->mmu_ssr3 & MMU_SSR3_SD) ? 1 : 0;
    case 3:
        return (r->mmu_ssr3 & MMU_SSR3_UD) ? 1 : 0;
    default:
        return 0;
    }
}

static INLINE int mmu_mode_from_psw(word psw)
{
    int mode = (psw >> 14) & 03;
    /* Reserved mode 2 is treated as kernel in this core. */
    return (mode == 2) ? 0 : mode;
}

static INLINE int mmu_fault_write_allowed(word pdr)
{
    int acf = pdr & 07;
    return (acf == 2 || acf == 3 || acf == 6 || acf == 7);
}

static INLINE word mmu_fault_to_ssr0_bits(int fault)
{
    switch (fault) {
    case MMU_FAULT_NONRES:
        return MMU_SSR0_NONRES;
    case MMU_FAULT_LENGTH:
        return MMU_SSR0_LENGTH;
    case MMU_FAULT_PROTECT:
        return MMU_SSR0_PROTECT;
    default:
        return 0;
    }
}
#endif

static INLINE int translate_va_ex(regs *r, word va, int is_write, int is_ifetch,
                                  int force_kernel_d, dword *pa_out,
                                  int *fault_code_out, int *mode_out,
                                  int *seg_out)
{
    if (pa_out) {
        *pa_out = va;
    }
    if (fault_code_out) {
        *fault_code_out = MMU_FAULT_NONE;
    }
    if (mode_out) {
        *mode_out = 0;
    }
    if (seg_out) {
        *seg_out = (va >> 13) & 07;
    }

#if defined(ENABLE_MMU) && (ENABLE_MMU)
    if (r->model != DCJ11) {
        return 0;
    }

    /*
     * Top virtual 8KB is the I/O page on DCJ11 regardless of MMU enable.
     * Under MMU it is still fixed to physical I/O page (18-bit or 22-bit view).
     */
    if (va >= 0160000) {
        if (pa_out) {
            *pa_out = (dword)(017760000 | (va & 017777));
        }
        return 0;
    }

    if ((r->mmu_ssr0 & MMU_SSR0_ENABLE) == 0) {
        return 0;
    }

    {
        int mode = force_kernel_d ? 0 : mmu_mode_from_psw(r->psw);
        int space = force_kernel_d
                    ? (mmu_split_enabled(r, 0) ? 1 : 0)
                    : (is_ifetch ? 0 : (mmu_split_enabled(r, mode) ? 1 : 0));
        int seg = (va >> 13) & 07;
        int block = (va >> 6) & 0177;
        word pdr = r->mmu_pdr[mode][space][seg];
        word par = r->mmu_par[mode][space][seg];
        int acf = pdr & 07;
        int ed = (pdr >> 3) & 01;
        int len = (pdr >> 8) & 0177;

        if (mode_out) {
            *mode_out = mode;
        }
        if (seg_out) {
            *seg_out = seg;
        }

        if (acf == 0) {
            if (fault_code_out) {
                *fault_code_out = MMU_FAULT_NONRES;
            }
            return -1;
        }

        if ((!ed && block > len) || (ed && block < len)) {
            if (fault_code_out) {
                *fault_code_out = MMU_FAULT_LENGTH;
            }
            return -1;
        }

        if (is_write && !mmu_fault_write_allowed(pdr)) {
            if (fault_code_out) {
                *fault_code_out = MMU_FAULT_PROTECT;
            }
            return -1;
        }

        if (is_write) {
            /* Mark segment written (heuristic W-bit model). */
            r->mmu_pdr[mode][space][seg] |= 0000100;
        }

        if (pa_out) {
            dword pa_block = (dword)((par + block) & 0777777);
            *pa_out = (pa_block << 6) | (va & 077);
        }
    }
#else
    (void)r;
    (void)is_write;
    (void)is_ifetch;
    (void)force_kernel_d;
    (void)fault_code_out;
    (void)mode_out;
    (void)seg_out;
#endif

    return 0;
}

/*
 * Required centralized translation API.
 * is_ifetch: 1=instruction fetch (I-space), 0=data access (I/D per SSR3)
 */
static INLINE int translate_va(regs *r, word va, int is_write, int is_ifetch,
                               dword *pa_out, int *fault_code_out)
{
    return translate_va_ex(r, va, is_write, is_ifetch, 0, pa_out, fault_code_out,
                           NULL, NULL);
}

static INLINE void mmu_record_fault(regs *r, word va, word pc, int fault,
                                    int mode, int seg)
{
#if defined(ENABLE_MMU) && (ENABLE_MMU)
    if (r->model != DCJ11) {
        return;
    }
    /* First-fault latch semantics. */
    if (r->mmu_ssr0 & MMU_SSR0_FAULT) {
        return;
    }
    r->mmu_ssr0 = (word)((r->mmu_ssr0 & MMU_SSR0_ENABLE) | MMU_SSR0_FAULT |
                         (((word)seg & 07) << MMU_SSR0_SEG_SHIFT) |
                         (((word)mode & 03) << MMU_SSR0_MODE_SHIFT) |
                         mmu_fault_to_ssr0_bits(fault));
    r->mmu_ssr2 = pc;
    r->mmu_ssr1 = va;
#else
    (void)r;
    (void)va;
    (void)pc;
    (void)fault;
    (void)mode;
    (void)seg;
#endif
}

static INLINE byte core_load_byte_ex(regs *r, word offset, int is_ifetch,
                                     int force_kernel_d)
{
    byte value;
    dword pa = 0;
    int fault = MMU_FAULT_NONE;
    int mode = 0;
    int seg = 0;
    int rc;

    if (dcj11_has_reg_block_177744(r, offset)) {
        return dcj11_reg_177750_load_byte(r, offset);
    }
    if (cpu_has_reg_177776(offset)) {
        return cpu_reg_177776_load_byte(r, offset);
    }

    if (mmu_io_read_byte(r, offset, &value)) {
        return value;
    }

    if (force_kernel_d) {
        rc = translate_va_ex(r, offset, 0, is_ifetch, 1, &pa, &fault, &mode, &seg);
    } else {
        rc = translate_va(r, offset, 0, is_ifetch, &pa, &fault);
#if defined(ENABLE_MMU) && (ENABLE_MMU)
        if (rc < 0 && r->model == DCJ11 && (r->mmu_ssr0 & MMU_SSR0_ENABLE)) {
            mode = mmu_mode_from_psw(r->psw);
            seg = (offset >> 13) & 07;
        }
#endif
    }
    if (rc < 0) {
        mmu_fault_trap(r, offset, r->r[7], fault, mode, seg);
        return 0;
    }

    return raw_load_byte_phys(r, pa);
}

static INLINE void core_store_byte_ex(regs *r, word offset, byte value,
                                      int force_kernel_d)
{
    dword pa = 0;
    int fault = MMU_FAULT_NONE;
    int mode = 0;
    int seg = 0;
    int rc;

    if (dcj11_has_reg_block_177744(r, offset)) {
        dcj11_reg_177750_store_byte(r, offset, value);
        return;
    }
    if (cpu_has_reg_177776(offset)) {
        cpu_reg_177776_store_byte(r, offset, value);
        return;
    }

    if (mmu_io_write_byte(r, offset, value)) {
        return;
    }

    if (force_kernel_d) {
        rc = translate_va_ex(r, offset, 1, 0, 1, &pa, &fault, &mode, &seg);
    } else {
        rc = translate_va(r, offset, 1, 0, &pa, &fault);
#if defined(ENABLE_MMU) && (ENABLE_MMU)
        if (rc < 0 && r->model == DCJ11 && (r->mmu_ssr0 & MMU_SSR0_ENABLE)) {
            mode = mmu_mode_from_psw(r->psw);
            seg = (offset >> 13) & 07;
        }
#endif
    }
    if (rc < 0) {
        mmu_fault_trap(r, offset, r->r[7], fault, mode, seg);
        return;
    }

    raw_store_byte_phys(r, pa, value);
}

static INLINE word core_load_word_ex(regs *r, word offset, int is_ifetch,
                                     int force_kernel_d)
{
    word value;
    dword pa = 0;
    int fault = MMU_FAULT_NONE;
    int mode = 0;
    int seg = 0;
    int rc;

    if ((r->model == DCJ11) && (offset & 1)) {
        bus_error_trap(r);
        return 0;
    }

    if (dcj11_has_reg_block_177744(r, offset)) {
        return dcj11_reg_block_load_word(r, offset);
    }
    if (offset == 0177776) {
        return r->psw;
    }

    if (mmu_io_read_word(r, offset, &value)) {
        return value;
    }

    if (force_kernel_d) {
        rc = translate_va_ex(r, offset, 0, is_ifetch, 1, &pa, &fault, &mode, &seg);
    } else {
        rc = translate_va(r, offset, 0, is_ifetch, &pa, &fault);
#if defined(ENABLE_MMU) && (ENABLE_MMU)
        if (rc < 0 && r->model == DCJ11 && (r->mmu_ssr0 & MMU_SSR0_ENABLE)) {
            mode = mmu_mode_from_psw(r->psw);
            seg = (offset >> 13) & 07;
        }
#endif
    }
    if (rc < 0) {
        mmu_fault_trap(r, offset, r->r[7], fault, mode, seg);
        return 0;
    }

    return raw_load_word_phys(r, pa);
}

static INLINE void core_store_word_ex(regs *r, word offset, word value,
                                      int force_kernel_d)
{
    dword pa = 0;
    int fault = MMU_FAULT_NONE;
    int mode = 0;
    int seg = 0;
    int rc;

    if ((r->model == DCJ11) && (offset & 1)) {
        bus_error_trap(r);
        return;
    }

    if (dcj11_has_reg_block_177744(r, offset)) {
        dcj11_reg_block_store_word(r, offset, value);
        return;
    }
    if (offset == 0177776) {
        r->psw = value;
        return;
    }

    if (mmu_io_write_word(r, offset, value)) {
        return;
    }

    if (force_kernel_d) {
        rc = translate_va_ex(r, offset, 1, 0, 1, &pa, &fault, &mode, &seg);
    } else {
        rc = translate_va(r, offset, 1, 0, &pa, &fault);
#if defined(ENABLE_MMU) && (ENABLE_MMU)
        if (rc < 0 && r->model == DCJ11 && (r->mmu_ssr0 & MMU_SSR0_ENABLE)) {
            mode = mmu_mode_from_psw(r->psw);
            seg = (offset >> 13) & 07;
        }
#endif
    }
    if (rc < 0) {
        mmu_fault_trap(r, offset, r->r[7], fault, mode, seg);
        return;
    }

    raw_store_word_phys(r, pa, value);
}

static INLINE byte core_load_byte(regs *r, word offset)
{
    return core_load_byte_ex(r, offset, 0, 0);
}

static INLINE void core_store_byte(regs *r, word offset, byte value)
{
    core_store_byte_ex(r, offset, value, 0);
}

static INLINE word core_load_word(regs *r, word offset)
{
    return core_load_word_ex(r, offset, 0, 0);
}

static INLINE word core_load_word_ifetch(regs *r, word offset)
{
    return core_load_word_ex(r, offset, 1, 0);
}

static INLINE word core_load_word_vector(regs *r, word offset)
{
    return core_load_word_ex(r, offset, 0, 1);
}

static INLINE void core_store_word(regs *r, word offset, word value)
{
    core_store_word_ex(r, offset, value, 0);
}

#define load_byte(a, b) core_load_byte((a), (b))
#define store_byte(a, b, c) core_store_byte((a), (b), (c))
#define load_word(a, b) core_load_word((a), (b))
#define load_word_ifetch(a, b) core_load_word_ifetch((a), (b))
#define load_word_vector(a, b) core_load_word_vector((a), (b))
#define store_word(a, b, c) core_store_word((a), (b), (c))

static INLINE void illegal_trap(regs *r)
{
    word old_psw = r->psw;
    pushw(r->psw);
    pushw(r->r[7]);
    r->r[7] = load_word_vector(r, 010);
    r->psw = trap_psw(r, old_psw, load_word_vector(r, 012));
}

static INLINE void bus_error_trap(regs *r)
{
    word old_psw = r->psw;
    static int trace_init = 0;
    static int trace_on = 0;
    if (!trace_init) {
        trace_on = (getenv("CORE_TRACE_BUSERR") != NULL) ? 1 : 0;
        trace_init = 1;
    }
    if (trace_on) {
        fprintf(stderr, "BUSERR pc=%06o fault=%06o sp=%06o ps=%06o ir=%06o\n",
                r->r[7], r->r[7], r->r[6], r->psw, r->ir);
    }
    r->r[6] -= 2;
    raw_store_word(r, r->r[6], r->psw);
    r->r[6] -= 2;
    raw_store_word(r, r->r[6], r->r[7]);
    r->r[7] = load_word_vector(r, 000004);
    r->psw = trap_psw(r, old_psw, load_word_vector(r, 000006));
    r->fAbort = 1;
}

static INLINE void mmu_fault_trap(regs *r, word va, word pc, int fault,
                                  int mode, int seg)
{
    word old_psw = r->psw;
    mmu_record_fault(r, va, pc, fault, mode, seg);
    r->r[6] -= 2;
    raw_store_word(r, r->r[6], r->psw);
    r->r[6] -= 2;
    raw_store_word(r, r->r[6], pc);
    r->r[7] = load_word_vector(r, MMU_TRAP_VECTOR);
    r->psw =
        trap_psw(r, old_psw, load_word_vector(r, (word)(MMU_TRAP_VECTOR + 2)));
    r->fAbort = 1;
}

static INLINE void handle_halt(regs *r)
{
    word vec;
    if (r->model == K1801VM1 || r->model == K1801VM1G) {
        store_word(r, 0177676, r->psw);
        store_word(r, 0177674, r->r[7]);
        store_word(r, 0177716, load_word(r, 0177716) | 010);
        vec = 0160002;
        r->r[7] = load_word_vector(r, vec);
        r->psw = load_word_vector(r, (word)(vec + 2));
    } else if (is_vm2(r)) {
        r->cps = r->psw;
        r->cpc = r->r[7];
        vec = (word)((r->SEL0 & 0177400) | 0170);
        r->r[7] = load_word_vector(r, vec) & 0177776;
        r->psw = load_word_vector(r, vec + 2);
    } else if (r->model == DCJ11) {
        pushw(r->psw);
        pushw(r->r[7]);
        vec = 4;
        if (flag_is_set(FLAG_H)) {
            vec |= (r->SEL0 & 0177400);
        }
        r->r[7] = load_word_vector(r, vec) & 0177776;
        r->psw = 0340;
    } else {
        store_word(r, 0177676, r->psw);
        store_word(r, 0177674, r->r[7]);
        store_word(r, 0177716, load_word(r, 0177716) | 010);
        vec = (r->SEL0 & 0177400);
        r->r[7] = load_word_vector(r, vec + 2) & 0177776;
        r->psw = load_word_vector(r, vec + 4);
    }
}

static INLINE void handle_fis(regs *r)
{
    word vec;
    if (r->SEL0 & 0200) {
        illegal_trap(r);
        return;
    }
    r->cps = r->psw;
    r->cpc = r->r[7];
    vec = (word)((r->SEL0 & 0177400) | 010);
    r->r[7] = load_word_vector(r, vec) & 0177776;
    r->psw = load_word_vector(r, vec + 2);
}

static INLINE void handle_fis_error(regs *r)
{
    pushw(r->psw);
    pushw(r->r[7]);
    r->r[7] = load_word_vector(r, 0244);
    r->psw = load_word_vector(r, 0246);
}

static INLINE byte get_data_byte(regs *r, byte type, word offset)
{
    if (type == TYPE_REG) {
        return r->r[offset] & 0377;
    } else if (type == TYPE_IFETCH) {
        return core_load_byte_ex(r, offset, 1, 0);
    } else {
        return load_byte(r, offset);
    }
}

static INLINE void put_data_byte(regs *r, byte type, word offset, byte value)
{
    if (type == TYPE_REG) {
        r->r[offset] = (r->r[offset] & 0177400) | value;
    } else {
        store_byte(r, offset, value);
    }
}

static INLINE void put_data_byte_movb(regs *r, byte type, word offset,
                                      byte value)
{
    if (type == TYPE_REG) {
        r->r[offset] = (value & SIGN_B) ? (0177400 | value) : (value & 0377);
    } else {
        store_byte(r, offset, value);
    }
}

static INLINE word get_data_word(regs *r, byte type, word offset)
{
    if (type == TYPE_REG) {
        return r->r[offset];
    } else if (type == TYPE_IFETCH) {
        return load_word_ifetch(r, offset);
    } else {
        return load_word(r, offset);
    }
}

static INLINE void put_data_word(regs *r, byte type, word offset, word value)
{
    if (type == TYPE_REG) {
        r->r[offset] = value;
    } else {
        store_word(r, offset, value);
    }
}

static INLINE byte decode_data(regs *r, byte data, byte data_type,
                               word *offset)
{
    byte reg = data & 0007;
    byte mode = (data & 0070) >> 3;
    byte step;

    if (r->fAbort) {
        return TYPE_ERROR;
    }

    if (data_type == TYPE_WORD) {
        step = 2;
    } else {
        step = 1;
    }

    // modes #n, @#A, A or X(PC), @A or @X(PC)
    // increase PC to 2
    if (data == 0027 || data == 0037 || data == 0067 || data == 77) {
        step = 2;
    }

    // The increment/decrement is always 2 bytes for modes 3 and 5,
    // or if the register being used is R6 (the stack pointer SP).
    if (reg == 6 || mode == 3 || mode == 5) {
        step = 2;
    }

    switch (mode) {
    case 0: /* Rn */
        *offset = reg;
        return TYPE_REG;
    case 1: /* (Rn) */
        *offset = r->r[reg];
        return TYPE_MEM;
    case 2: /* (Rn)+ */
        *offset = r->r[reg];
        r->r[reg] += step;
        if (reg == 7) {
            return TYPE_IFETCH;
        }
        return TYPE_MEM;
    case 3: /* @(Rn)+ */
        if (reg == 7) {
            *offset = load_word_ifetch(r, r->r[reg]);
        } else {
            *offset = load_word(r, r->r[reg]);
        }
        if (r->fAbort) {
            return TYPE_ERROR;
        }
        r->r[reg] += step;
        return TYPE_MEM;
    case 4: /* -(Rn) */
        r->r[reg] -= step;
        *offset = r->r[reg];
        return TYPE_MEM;
    case 5: /* @-(Rn) */
        r->r[reg] -= step;
        *offset = load_word(r, r->r[reg]);
        if (r->fAbort) {
            return TYPE_ERROR;
        }
        return TYPE_MEM;
    case 6: { /* X(Rn) */
        word tmp = load_word_ifetch(r, r->r[7]);
        if (r->fAbort) {
            return TYPE_ERROR;
        }
        r->r[7] += 2;
        *offset = r->r[reg] + tmp;
    }
    return TYPE_MEM;
    case 7: { /* @X(Rn) */
        word tmp = load_word_ifetch(r, r->r[7]);
        if (r->fAbort) {
            return TYPE_ERROR;
        }
        r->r[7] += 2;
        *offset = load_word(r, r->r[reg] + tmp);
        if (r->fAbort) {
            return TYPE_ERROR;
        }
    }
    return TYPE_MEM;
    }

    // Never return here.
    return TYPE_ERROR;
}

int core_step(regs *r)
{
    word src_offset;
    byte src_type;
    word dst_offset;
    byte dst_type;
    word irq_vector;
    word psw_before = r->psw;
    int do_trace = 0;
    int defer_halt = 0;
    int skip_irq = 0;
    int skip_trace = r->fTrap ? 1 : 0;
    r->fTrap = 0;

    if (core_trace_mmu_boot < 0) {
        core_trace_mmu_boot = (getenv("CORE_TRACE_MMU_BOOT") != NULL) ? 1 : 0;
    }

    if (r->fAbort) {
        r->fAbort = 0;
        return 0;
    }

    if (r->fHaltSignal) {
        if (is_vm2(r) && r->fStepDeferHalt) {
            r->fStepDeferHalt = 0;
            defer_halt = 1;
        } else {
            r->fHaltSignal = 0;
            if (is_vm2(r) && flag_is_set(FLAG_H)) {
                r->fWait = 0;
                return 0;
            }
            r->fWait = 0;
            handle_halt(r);
            return 0;
        }
    }

    if (r->fWait) {
        if (r->poll_irq && r->poll_irq(r, &irq_vector)) {
            word vec;
            if (irq_accept(r, irq_vector, &vec)) {
                word old_psw = r->psw;
                r->fWait = 0;
                pushw(r->psw);
                pushw(r->r[7]);
                r->r[7] = load_word_vector(r, vec);
                r->psw = trap_psw(r, old_psw, load_word_vector(r, (word)(vec + 2)));
            }
            return 0;
        }
        return 0;
    }

    /* IRQs are checked after instruction execution (real PDP-11 behavior). */

    // load instruction

    word op = load_word_ifetch(r, r->r[7]);
    if (r->fAbort) {
        r->fAbort = 0;
        return 0;
    }

    if (core_trace_mmu_boot > 0 && r->r[7] == 005202) {
        dword pa_r = 0, pa_w = 0;
        int f_r = 0, f_w = 0;
        int m_r = 0, m_w = 0;
        int s_r = 0, s_w = 0;
        int rc_r = translate_va_ex(r, 0140000, 0, 0, 0, &pa_r, &f_r, &m_r, &s_r);
        int rc_w = translate_va_ex(r, 0140000, 1, 0, 0, &pa_w, &f_w, &m_w, &s_w);
#if defined(ENABLE_MMU) && (ENABLE_MMU)
        fprintf(stderr,
                "MMU BOOT PC=%06o SSR0=%06o SSR3=%06o KIPDR6=%06o KDPDR6=%06o "
                "KIPAR6=%06o KDPAR6=%06o rcR=%d paR=%08o fR=%d mR=%d sR=%d rcW=%d "
                "paW=%08o fW=%d mW=%d sW=%d\n",
                r->r[7], r->mmu_ssr0, r->mmu_ssr3, r->mmu_pdr[0][0][6],
                r->mmu_pdr[0][1][6], r->mmu_par[0][0][6], r->mmu_par[0][1][6], rc_r,
                (unsigned)pa_r, f_r, m_r, s_r, rc_w, (unsigned)pa_w, f_w, m_w, s_w);
#else
        fprintf(stderr,
                "MMU BOOT PC=%06o rcR=%d paR=%08o fR=%d mR=%d sR=%d rcW=%d "
                "paW=%08o fW=%d mW=%d sW=%d\n",
                r->r[7], rc_r, (unsigned)pa_r, f_r, m_r, s_r, rc_w, (unsigned)pa_w,
                f_w, m_w, s_w);
#endif
    }

    r->ir = op;
    r->r[7] += 2;
    if (is_vm2(r) && ((op & 0177700) == 0075000)) {
        if (r->fFisError) {
            r->fFisError = 0;
            handle_fis_error(r);
        } else {
            handle_fis(r);
        }
        goto step_end;
    }

    //
    // No operands instructions
    //
    switch (op) {
    case 000000: { /* HALT */
        if ((r->model == DCJ11 || is_vm2(r)) && !dcj11_kernel_psw(psw_before)) {
            word old_psw = r->psw;
            pushw(r->psw);
            pushw(r->r[7]);
            r->r[7] = load_word_vector(r, 000004);
            r->psw = trap_psw(r, old_psw, load_word_vector(r, 000006));
            goto step_end;
        }
        handle_halt(r);
        goto step_end;
    }

    case 000001: /* WAIT */
        r->fWait = 1;
        if (r->model == K1801VM1 || r->model == K1801VM1G || is_vm2(r) ||
                r->model == DCJ11) {
            skip_trace = 1;
        }
        goto step_end;

    case 000002: /* RTI */
        pullw(r->r[7]);
        pullw(r->psw);
        if (is_vm2(r)) {
            if (r->r[7] < 0160000) {
                r->psw = (word)((r->psw & ~FLAG_H) | (psw_before & FLAG_H));
            }
        }
        if (r->model == DCJ11 && !dcj11_kernel_psw(psw_before)) {
            word mask = 0170000 | 000340;
            r->psw = (word)((r->psw & ~mask) | (psw_before & mask));
        }
        goto step_end;

    case 000003: { /* BPT */
        word old_psw = r->psw;
        pushw(r->psw);
        pushw(r->r[7]);
        r->r[7] = load_word_vector(r, 014);
        r->psw = trap_psw(r, old_psw, load_word_vector(r, 016));
    }
    goto step_end;

    case 000004: { /* IOT */
        word old_psw = r->psw;
        pushw(r->psw);
        pushw(r->r[7]);
        r->r[7] = load_word_vector(r, 020);
        r->psw = trap_psw(r, old_psw, load_word_vector(r, 022));
    }
    goto step_end;

    case 000005: /* RESET */
        if (r->model == DCJ11 && !dcj11_kernel_psw(psw_before)) {
            goto step_end;
        }
        r->fWait = 0;
        r->fHaltSignal = 0;
        if (r->reset) {
            r->reset(r);
        }
        goto step_end;

    case 000006: /* RTT */
        pullw(r->r[7]);
        pullw(r->psw);
        if (is_vm2(r)) {
            if (r->r[7] < 0160000) {
                r->psw = (word)((r->psw & ~FLAG_H) | (psw_before & FLAG_H));
            }
        }
        if (r->model == DCJ11 && !dcj11_kernel_psw(psw_before)) {
            word mask = 0170000 | 000340;
            r->psw = (word)((r->psw & ~mask) | (psw_before & mask));
        }
        r->fTrap = 1;
        skip_trace = 1;
        goto step_end;

    case 000007: /* MFPT */
        if (r->model != DCJ11) {
            illegal_trap(r);
            goto step_end;
        }
        r->r[0] = 5;
        goto step_end;

    case 000240: /* NOP */
        goto step_end;

    case 000241: /* CLC */
        clear_flag(FLAG_C);
        goto step_end;

    case 000242: /* CLV */
        clear_flag(FLAG_V);
        goto step_end;

    case 000244: /* CLZ */
        clear_flag(FLAG_Z);
        goto step_end;

    case 000250: /* CLN */
        clear_flag(FLAG_N);
        goto step_end;

    case 000257: /* CCC */
        clear_flag(FLAG_C | FLAG_V | FLAG_Z | FLAG_N);
        goto step_end;

    case 000261: /* SEC */
        set_flag(FLAG_C);
        goto step_end;

    case 000262: /* SEV */
        set_flag(FLAG_V);
        goto step_end;

    case 000264: /* SEZ */
        set_flag(FLAG_Z);
        goto step_end;

    case 000270: /* SEN */
        set_flag(FLAG_N);
        goto step_end;

    case 000277: /* SCC */
        set_flag(FLAG_C | FLAG_V | FLAG_Z | FLAG_N);
        goto step_end;
    }

    if ((op & 0177770) == 0000230) { /* SPL */
        if (r->model != DCJ11 && !is_vm2(r)) {
            illegal_trap(r);
            goto step_end;
        }
        if (dcj11_kernel_psw(psw_before)) {
            r->psw = (word)((r->psw & ~000340) | ((op & 07) << 5));
        }
        goto step_end;
    }

    if (is_vm2(r)) {
        switch (op) {
        case 0000012: /* START */
            if (!flag_is_set(FLAG_H) || !flag_is_set(FLAG_P)) {
                illegal_trap(r);
                goto step_end;
            }
            r->r[7] = r->cpc;
            r->psw = r->cps;
            if (r->poll_irq) {
                word vec;
                word irq_vector;
                if (r->poll_irq(r, &irq_vector)) {
                    if (irq_accept(r, irq_vector, &vec)) {
                        pushw(r->psw);
                        pushw(r->r[7]);
                        r->r[7] = load_word_vector(r, vec);
                        r->psw = load_word_vector(r, (word)(vec + 2));
                    }
                }
            }
            goto step_end;

        case 0000016: /* STEP */
            if (!flag_is_set(FLAG_H) || !flag_is_set(FLAG_P)) {
                illegal_trap(r);
                goto step_end;
            }
            r->r[7] = r->cpc;
            r->psw = r->cps;
            skip_irq = 1;
            r->fStepDeferHalt = 1;
            goto step_end;

        case 0000020: /* RSEL */
            if (!flag_is_set(FLAG_H)) {
                illegal_trap(r);
                goto step_end;
            }
            r->r[0] = r->SEL0;
            goto step_end;

        case 0000021: /* MFUS */
            if (!flag_is_set(FLAG_H)) {
                illegal_trap(r);
                goto step_end;
            }
            {
                word psw_saved = r->psw;
                r->psw = (word)(r->psw & ~FLAG_H);
                r->r[0] = load_word(r, r->r[5]);
                r->r[5] += 2;
                r->psw = psw_saved;
            }
            goto step_end;

        case 0000022: /* RCPC */
            if (!flag_is_set(FLAG_H) || !flag_is_set(FLAG_P)) {
                illegal_trap(r);
                goto step_end;
            }
            r->r[0] = r->cpc;
            goto step_end;

        case 0000024: /* RCPS */
            if (!flag_is_set(FLAG_H) || !flag_is_set(FLAG_P)) {
                illegal_trap(r);
                goto step_end;
            }
            r->r[0] = r->cps;
            goto step_end;

        case 0000031: /* MTUS */
            if (!flag_is_set(FLAG_H)) {
                illegal_trap(r);
                goto step_end;
            }
            {
                word psw_saved = r->psw;
                r->psw = (word)(r->psw & ~FLAG_H);
                r->r[5] -= 2;
                store_word(r, r->r[5], r->r[0]);
                r->psw = psw_saved;
            }
            goto step_end;

        case 0000032: /* WCPC */
            if (!flag_is_set(FLAG_H) || !flag_is_set(FLAG_P)) {
                illegal_trap(r);
                goto step_end;
            }
            r->cpc = r->r[0];
            goto step_end;

        case 0000034: /* WCPS */
            if (!flag_is_set(FLAG_H) || !flag_is_set(FLAG_P)) {
                illegal_trap(r);
                goto step_end;
            }
            r->cps = r->r[0];
            goto step_end;
        }
    }

#define BR_OFFSET()                                                            \
  word offset = op & 0377;                                                     \
  if (offset & SIGN_B) {                                                       \
    offset += 0177400;                                                         \
  }

    switch (op & 0177400) {
    case 0000400: { /* BR */
        BR_OFFSET();
        r->r[7] += (offset * 2);
        goto step_end;
    }

    case 0001000: /* BNE */
        if (flag_is_clear(FLAG_Z)) {
            BR_OFFSET();
            r->r[7] += (offset * 2);
        }
        goto step_end;

    case 0001400: /* BEQ */
        if (flag_is_set(FLAG_Z)) {
            BR_OFFSET();
            r->r[7] += (offset * 2);
        }
        goto step_end;

    case 0100000: /* BPL */
        if (flag_is_clear(FLAG_N)) {
            BR_OFFSET();
            r->r[7] += (offset * 2);
        }
        goto step_end;

    case 0100400: /* BMI */
        if (flag_is_set(FLAG_N)) {
            BR_OFFSET();
            r->r[7] += (offset * 2);
        }
        goto step_end;

    case 0102000: /* BVC */
        if (flag_is_clear(FLAG_V)) {
            BR_OFFSET();
            r->r[7] += (offset * 2);
        }
        goto step_end;

    case 0102400: /* BVS */
        if (flag_is_set(FLAG_V)) {
            BR_OFFSET();
            r->r[7] += (offset * 2);
        }
        goto step_end;

    case 0103000: /* BCC or BHIS */
        if (flag_is_clear(FLAG_C)) {
            BR_OFFSET();
            r->r[7] += (offset * 2);
        }
        goto step_end;

    case 0103400: /* BCS or BLO */
        if (flag_is_set(FLAG_C)) {
            BR_OFFSET();
            r->r[7] += (offset * 2);
        }
        goto step_end;

    case 0002000: /* BGE */
        if ((flag_is_set(FLAG_N) ^ flag_is_set(FLAG_V)) == 0) {
            BR_OFFSET();
            r->r[7] += (offset * 2);
        }
        goto step_end;

    case 0002400: /* BLT */
        if ((flag_is_set(FLAG_N) ^ flag_is_set(FLAG_V)) == 1) {
            BR_OFFSET();
            r->r[7] += (offset * 2);
        }
        goto step_end;

    case 0003000: /* BGT */
        if (((flag_is_set(FLAG_N) ^ flag_is_set(FLAG_V)) | flag_is_set(FLAG_Z)) ==
                0) {
            BR_OFFSET();
            r->r[7] += (offset * 2);
        }
        goto step_end;

    case 0003400: /* BLE */
        if (((flag_is_set(FLAG_N) ^ flag_is_set(FLAG_V)) | flag_is_set(FLAG_Z)) ==
                1) {
            BR_OFFSET();
            r->r[7] += (offset * 2);
        }
        goto step_end;

    case 0101000: /* BHI */
        if (flag_is_clear(FLAG_C | FLAG_Z)) {
            BR_OFFSET();
            r->r[7] += (offset * 2);
        }
        goto step_end;

    case 0101400: /* BLOS */
        if (!flag_is_clear(FLAG_C | FLAG_Z)) {
            BR_OFFSET();
            r->r[7] += (offset * 2);
        }
        goto step_end;

    /* EMT */
    case 0104000:
    /* TRAP */
    case 0104400: {
        word old_psw = r->psw;
        word vec = (op & 0400) ? 000034 : 000030;
        pushw(r->psw);
        pushw(r->r[7]);
        r->r[7] = load_word_vector(r, vec);
        r->psw = trap_psw(r, old_psw, load_word_vector(r, (word)(vec + 2)));
    }
    goto step_end;
    }

    // RTS
    if ((op & 0177770) == 000200) {
        byte reg = op & 07;
        r->r[7] = r->r[reg];
        pullw(r->r[reg]);
        goto step_end;
    }

    // MARKNN instruction

    if ((op & 0177700) == 0006400) {
        word nn = op & 077;
        r->r[6] += (nn << 1);
        r->r[7] = r->r[5];
        pullw(r->r[5]);
        goto step_end;
    }

    // SOB instruction

    if ((op & 0177000) == 0077000) {
        word nn = op & 077;
        word reg = (op >> 6) & 07;
        r->r[reg]--;
        if (r->r[reg]) {
            r->r[7] -= (nn << 1);
        }
        goto step_end;
    }

    //
    // single operand instructions
    //

#define DECODE_DST()                                                           \
  do {                                                                         \
    dst_type = decode_data(r, op & 00077, TYPE_WORD, &dst_offset);             \
    if (dst_type == TYPE_ERROR)                                                \
      return 0;                                                                \
  } while (0)
#define DECODE_DSTB()                                                          \
  do {                                                                         \
    dst_type = decode_data(r, op & 00077, TYPE_BYTE, &dst_offset);             \
    if (dst_type == TYPE_ERROR)                                                \
      return 0;                                                                \
  } while (0)

#define GET_WORD(a)                                                            \
  word a = get_data_word(r, dst_type, dst_offset);                             \
  if (r->fAbort)                                                               \
    return 0;
#define GET_BYTE(a)                                                            \
  word a = get_data_byte(r, dst_type, dst_offset);                             \
  if (r->fAbort)                                                               \
    return 0;

#define PUT_WORD(a)                                                            \
  put_data_word(r, dst_type, dst_offset, a);                                   \
  if (r->fAbort)                                                               \
    return 0;
#define PUT_BYTE(a)                                                            \
  put_data_byte(r, dst_type, dst_offset, a);                                   \
  if (r->fAbort)                                                               \
    return 0;
#define PUT_BYTE_MOVB(a)                                                       \
  put_data_byte_movb(r, dst_type, dst_offset, a);                              \
  if (r->fAbort)                                                               \
    return 0;

    switch ((op & 0177700) >> 6) {
    case 00001: /* JMP */
        if ((op & 070) == 0) {
            illegal_trap(r);
            goto step_end;
        }
        DECODE_DST();
        r->r[7] = dst_offset;
        goto step_end;

    case 00050: /* CLR */
        DECODE_DST();
        put_data_word(r, dst_type, dst_offset, 0);
        clear_flag(FLAG_N | FLAG_V | FLAG_C);
        set_flag(FLAG_Z);
        goto step_end;
    case 01050: /* CLRB */
        DECODE_DSTB();
        put_data_byte(r, dst_type, dst_offset, 0);
        clear_flag(FLAG_N | FLAG_V | FLAG_C);
        set_flag(FLAG_Z);
        goto step_end;

    case 00051: { /* COM */
        DECODE_DST();
        GET_WORD(tmp);
        tmp = ~tmp;
        PUT_WORD(tmp);
        set_flag_if(tmp == 0, FLAG_Z);
        set_flag_if(tmp & SIGN, FLAG_N);
        clear_flag(FLAG_V);
        set_flag(FLAG_C);
        goto step_end;
    }
    case 01051: { /* COMB */
        DECODE_DSTB();
        GET_BYTE(tmp);
        tmp = (~tmp) & 0377;
        PUT_BYTE(tmp);
        set_flag_if(tmp == 0, FLAG_Z);
        set_flag_if(tmp & SIGN_B, FLAG_N);
        clear_flag(FLAG_V);
        set_flag(FLAG_C);
        goto step_end;
    }

    case 00052: { /* INC */
        DECODE_DST();
        GET_WORD(tmp);
        set_flag_if(tmp == MPI, FLAG_V);
        tmp++;
        PUT_WORD(tmp);
        set_flag_if(tmp == 0, FLAG_Z);
        set_flag_if(tmp & SIGN, FLAG_N);
        goto step_end;
    }
    case 01052: { /* INCB */
        DECODE_DSTB();
        GET_BYTE(tmp);
        set_flag_if(tmp == MPI_B, FLAG_V);
        tmp = (tmp + 1) & 0377;
        PUT_BYTE(tmp);
        set_flag_if(tmp == 0, FLAG_Z);
        set_flag_if(tmp & SIGN_B, FLAG_N);
        goto step_end;
    }

    case 00053: { /* DEC */
        DECODE_DST();
        GET_WORD(tmp);
        set_flag_if(tmp == MNI, FLAG_V);
        tmp--;
        PUT_WORD(tmp);
        set_flag_if(tmp == 0, FLAG_Z);
        set_flag_if(tmp & SIGN, FLAG_N);
        goto step_end;
    }
    case 01053: { /* DECB */
        DECODE_DSTB();
        GET_BYTE(tmp);
        set_flag_if(tmp == MNI_B, FLAG_V);
        tmp = (tmp - 1) & 0377;
        PUT_BYTE(tmp);
        set_flag_if(tmp == 0, FLAG_Z);
        set_flag_if(tmp & SIGN_B, FLAG_N);
        goto step_end;
    }

    case 00054: { /* NEG */
        DECODE_DST();
        GET_WORD(tmp);
        tmp = (NEG_1 - tmp) + 1;
        PUT_WORD(tmp);
        set_flag_if(tmp == 0, FLAG_Z);
        set_flag_if(tmp & SIGN, FLAG_N);
        set_flag_if(tmp == MNI, FLAG_V);
        set_flag_if(tmp != 0, FLAG_C);
        goto step_end;
    }
    case 01054: { /* NEGB */
        DECODE_DSTB();
        GET_BYTE(tmp);
        tmp = ((NEG_1_B - tmp) + 1) & 0377;
        PUT_BYTE(tmp);
        set_flag_if(tmp == 0, FLAG_Z);
        set_flag_if(tmp & SIGN_B, FLAG_N);
        set_flag_if(tmp == MNI_B, FLAG_V);
        set_flag_if(tmp != 0, FLAG_C);
        goto step_end;
    }

    case 00057: { /* TST */
        DECODE_DST();
        GET_WORD(tmp);
        set_flag_if(tmp == 0, FLAG_Z);
        set_flag_if(tmp & SIGN, FLAG_N);
        clear_flag(FLAG_V | FLAG_C);
        goto step_end;
    }
    case 01057: { /* TSTB */
        DECODE_DSTB();
        GET_BYTE(tmp);
        set_flag_if(tmp == 0, FLAG_Z);
        set_flag_if(tmp & SIGN_B, FLAG_N);
        clear_flag(FLAG_V | FLAG_C);
        goto step_end;
    }

    case 00062: { /* ASR */
        DECODE_DST();
        GET_WORD(tmp);
        set_flag_if(tmp & 1, FLAG_C);
        tmp = (tmp & SIGN) | (tmp >> 1);
        PUT_WORD(tmp);
        set_flag_if(tmp & SIGN, FLAG_N);
        set_flag_if(tmp == 0, FLAG_Z);
        set_flag_if(flag_is_set(FLAG_N) ^ flag_is_set(FLAG_C), FLAG_V);
        goto step_end;
    }
    case 01062: { /* ASRB */
        DECODE_DSTB();
        GET_BYTE(tmp);
        set_flag_if(tmp & 1, FLAG_C);
        tmp = (tmp & SIGN_B) | (tmp >> 1);
        PUT_BYTE(tmp);
        set_flag_if(tmp & SIGN_B, FLAG_N);
        set_flag_if(tmp == 0, FLAG_Z);
        set_flag_if(flag_is_set(FLAG_N) ^ flag_is_set(FLAG_C), FLAG_V);
        goto step_end;
    }

    case 00063: { /* ASL */
        DECODE_DST();
        GET_WORD(tmp);
        set_flag_if(tmp & SIGN, FLAG_C);
        tmp = tmp << 1;
        PUT_WORD(tmp);
        set_flag_if(tmp & SIGN, FLAG_N);
        set_flag_if(tmp == 0, FLAG_Z);
        set_flag_if(flag_is_set(FLAG_N) ^ flag_is_set(FLAG_C), FLAG_V);
        goto step_end;
    }
    case 01063: { /* ASLB */
        DECODE_DSTB();
        GET_BYTE(tmp);
        set_flag_if(tmp & SIGN_B, FLAG_C);
        tmp = (tmp << 1) & 0377;
        PUT_BYTE(tmp);
        set_flag_if(tmp & SIGN_B, FLAG_N);
        set_flag_if(tmp == 0, FLAG_Z);
        set_flag_if(flag_is_set(FLAG_N) ^ flag_is_set(FLAG_C), FLAG_V);
        goto step_end;
    }

    case 00060: { /* ROR */
        DECODE_DST();
        GET_WORD(tmp);
        word tmp_c = tmp & 1;
        tmp = tmp >> 1;
        if (flag_is_set(FLAG_C)) {
            tmp |= SIGN;
        }
        PUT_WORD(tmp);
        set_flag_if(tmp_c, FLAG_C);
        set_flag_if(tmp & SIGN, FLAG_N);
        set_flag_if(tmp == 0, FLAG_Z);
        set_flag_if(flag_is_set(FLAG_N) ^ flag_is_set(FLAG_C), FLAG_V);
        goto step_end;
    }
    case 01060: { /* RORB */
        DECODE_DSTB();
        GET_BYTE(tmp);
        byte tmp_c = tmp & 1;
        tmp = tmp >> 1;
        if (flag_is_set(FLAG_C)) {
            tmp |= SIGN_B;
        }
        PUT_BYTE(tmp);
        set_flag_if(tmp_c, FLAG_C);
        set_flag_if(tmp & SIGN_B, FLAG_N);
        set_flag_if(tmp == 0, FLAG_Z);
        set_flag_if(flag_is_set(FLAG_N) ^ flag_is_set(FLAG_C), FLAG_V);
        goto step_end;
    }

    case 00061: { /* ROL */
        DECODE_DST();
        GET_WORD(tmp);
        word tmp_c = tmp & SIGN;
        tmp = tmp << 1;
        if (flag_is_set(FLAG_C)) {
            tmp |= 1;
        }
        PUT_WORD(tmp);
        set_flag_if(tmp_c, FLAG_C);
        set_flag_if(tmp & SIGN, FLAG_N);
        set_flag_if(tmp == 0, FLAG_Z);
        set_flag_if(flag_is_set(FLAG_N) ^ flag_is_set(FLAG_C), FLAG_V);
        goto step_end;
    }
    case 01061: { /* ROLB */
        DECODE_DSTB();
        GET_BYTE(tmp);
        byte tmp_c = tmp & SIGN_B;
        tmp = (tmp << 1) & 0377;
        if (flag_is_set(FLAG_C)) {
            tmp |= 1;
        }
        tmp &= 0377;
        PUT_BYTE(tmp);
        set_flag_if(tmp_c, FLAG_C);
        set_flag_if(tmp & SIGN_B, FLAG_N);
        set_flag_if(tmp == 0, FLAG_Z);
        set_flag_if(flag_is_set(FLAG_N) ^ flag_is_set(FLAG_C), FLAG_V);
        goto step_end;
    }

    case 00055: { /* ADC */
        DECODE_DST();
        GET_WORD(tmp);
        set_flag_if((tmp == MPI) && flag_is_set(FLAG_C), FLAG_V);
        byte tmp_c = (tmp == NEG_1) && flag_is_set(FLAG_C);
        if (flag_is_set(FLAG_C)) {
            tmp = tmp + 1;
        }
        PUT_WORD(tmp);
        set_flag_if(tmp_c, FLAG_C);
        set_flag_if(tmp & SIGN, FLAG_N);
        set_flag_if(tmp == 0, FLAG_Z);
        goto step_end;
    }
    case 01055: { /* ADCB */
        DECODE_DSTB();
        GET_BYTE(tmp);
        set_flag_if((tmp == MPI_B) && flag_is_set(FLAG_C), FLAG_V);
        byte tmp_c = (tmp == NEG_1_B) && flag_is_set(FLAG_C);
        if (flag_is_set(FLAG_C)) {
            tmp = (tmp + 1) & 0377;
        }
        PUT_BYTE(tmp);
        set_flag_if(tmp_c, FLAG_C);
        set_flag_if(tmp & SIGN_B, FLAG_N);
        set_flag_if(tmp == 0, FLAG_Z);
        goto step_end;
    }

    case 00056: { /* SBC */
        DECODE_DST();
        GET_WORD(tmp);
        set_flag_if(tmp == MNI, FLAG_V);
        byte flag_c = flag_is_set(FLAG_C);
        byte tmp_c = flag_c && (tmp == 0);
        if (flag_c) {
            tmp = tmp - 1;
        }
        PUT_WORD(tmp);
        set_flag_if(tmp_c, FLAG_C);
        set_flag_if(tmp & SIGN, FLAG_N);
        set_flag_if(tmp == 0, FLAG_Z);
        goto step_end;
    }
    case 01056: { /* SBCB */
        DECODE_DSTB();
        GET_BYTE(tmp);
        set_flag_if(tmp == MNI_B, FLAG_V);
        byte flag_c = flag_is_set(FLAG_C);
        byte tmp_c = flag_c && (tmp == 0);
        if (flag_c) {
            tmp = (tmp - 1) & 0377;
        }
        PUT_BYTE(tmp);
        set_flag_if(tmp_c, FLAG_C);
        set_flag_if(tmp & SIGN_B, FLAG_N);
        set_flag_if(tmp == 0, FLAG_Z);
        goto step_end;
    }

    case 00003: { /* SWAB */
        DECODE_DST();
        GET_WORD(tmp);
        tmp = (tmp >> 8) | (tmp << 8);
        PUT_WORD(tmp);
        tmp &= 0377;
        clear_flag(FLAG_V | FLAG_C);
        set_flag_if(tmp & SIGN_B, FLAG_N);
        set_flag_if(tmp == 0, FLAG_Z);
    }
    goto step_end;

    case 00067: /* SXT */
        DECODE_DST();
        if (flag_is_set(FLAG_N)) {
            put_data_word(r, dst_type, dst_offset, NEG_1);
            clear_flag(FLAG_Z);
        } else {
            put_data_word(r, dst_type, dst_offset, 0);
            set_flag(FLAG_Z);
        }
        clear_flag(FLAG_V);
        goto step_end;

    case 01067: { /* MFPS */
        DECODE_DSTB();
        word tmp = r->psw & 0377;
        PUT_BYTE_MOVB(tmp);
        set_flag_if(tmp & SIGN_B, FLAG_N);
        set_flag_if(tmp == 0, FLAG_Z);
        clear_flag(FLAG_V);
    }
    goto step_end;

    case 01064: /* MTPS */
        DECODE_DSTB();
        GET_BYTE(tmp);
        {
            word low = tmp & 0357;
            if (r->model == DCJ11 && !dcj11_kernel_psw(psw_before)) {
                low = (word)((low & 000037) | (r->psw & 000340));
            }
            r->psw = (word)((r->psw & 0177000) | (r->psw & (FLAG_T | FLAG_H)) | low);
        }
        goto step_end;

    case 00072: { /* TSTSET */
        if (r->model != DCJ11) {
            illegal_trap(r);
            goto step_end;
        }
        if ((op & 070) == 0) {
            illegal_trap(r);
            goto step_end;
        }
        DECODE_DST();
        GET_WORD(tmp);
        r->r[0] = tmp;
        PUT_WORD(tmp | 1);
        set_flag_if(r->r[0] & SIGN, FLAG_N);
        set_flag_if(r->r[0] == 0, FLAG_Z);
        clear_flag(FLAG_V);
        set_flag_if(tmp & 1, FLAG_C);
        goto step_end;
    }

    case 00073: { /* WRTLCK */
        if (r->model != DCJ11) {
            illegal_trap(r);
            goto step_end;
        }
        if ((op & 070) == 0) {
            illegal_trap(r);
            goto step_end;
        }
        DECODE_DST();
        word tmp = r->r[0];
        PUT_WORD(tmp);
        set_flag_if(tmp & SIGN, FLAG_N);
        set_flag_if(tmp == 0, FLAG_Z);
        clear_flag(FLAG_V);
        goto step_end;
    }

    case 00065: { /* MFPD */
        if (!has_prev_space_ops(r)) {
            illegal_trap(r);
            goto step_end;
        }
        DECODE_DST();
        GET_WORD(tmp);
        pushw(tmp);
        set_flag_if(tmp & SIGN, FLAG_N);
        set_flag_if(tmp == 0, FLAG_Z);
        clear_flag(FLAG_V);
        goto step_end;
    }
    case 01065: { /* MFPI */
        if (!has_prev_space_ops(r)) {
            illegal_trap(r);
            goto step_end;
        }
        DECODE_DST();
        GET_WORD(tmp);
        pushw(tmp);
        set_flag_if(tmp & SIGN, FLAG_N);
        set_flag_if(tmp == 0, FLAG_Z);
        clear_flag(FLAG_V);
        goto step_end;
    }
    case 00066: { /* MTPI */
        if (!has_prev_space_ops(r)) {
            illegal_trap(r);
            goto step_end;
        }
        DECODE_DST();
        word tmp;
        pullw(tmp);
        PUT_WORD(tmp);
        set_flag_if(tmp & SIGN, FLAG_N);
        set_flag_if(tmp == 0, FLAG_Z);
        clear_flag(FLAG_V);
        goto step_end;
    }
    case 01066: { /* MTPD */
        if (!has_prev_space_ops(r)) {
            illegal_trap(r);
            goto step_end;
        }
        DECODE_DST();
        word tmp;
        pullw(tmp);
        PUT_WORD(tmp);
        set_flag_if(tmp & SIGN, FLAG_N);
        set_flag_if(tmp == 0, FLAG_Z);
        clear_flag(FLAG_V);
        goto step_end;
    }
    }

    //
    // Double operand instructions - register and address
    //

#define RA_REG(r) byte r = (op >> 6) & 07;

    switch ((op & 0177000) >> 9) {
    case 0004: { /* JSR */
        RA_REG(reg);
        DECODE_DST();
        pushw(r->r[reg]);
        r->r[reg] = r->r[7];
        r->r[7] = dst_offset;
        goto step_end;
    }

    case 0070: { /* MUL */
        if (r->model == K1801VM1) {
            illegal_trap(r);
            goto step_end;
        }
        union u_word data1;
        union u_word data2;
        union u_dword tmp;
        RA_REG(reg);
        data1.u = r->r[reg];
        DECODE_DST();
        data2.u = get_data_word(r, dst_type, dst_offset);

        tmp.s = ((sdword)data1.s) * ((sdword)data2.s);

        r->r[reg] = tmp.u >> 16;
        r->r[reg | 1] = tmp.u & 0177777;

        set_flag_if(tmp.u == 0, FLAG_Z);
        set_flag_if(tmp.u & 0x80000000, FLAG_N);
        set_flag_if((tmp.s < -0100000) || (tmp.s >= 077777), FLAG_C);
        clear_flag(FLAG_V);
        goto step_end;
    }

    case 0071: { /* DIV */
        if (r->model == K1801VM1) {
            illegal_trap(r);
            goto step_end;
        }
        union u_word data1;
        union u_word data2;
        union u_dword tmp;
        RA_REG(reg);
        data1.u = (r->r[reg] << 16) | r->r[reg | 1];
        DECODE_DST();
        data2.u = get_data_word(r, dst_type, dst_offset);
        if (data2.u == 0) {
            set_flag(FLAG_C);
            set_flag(FLAG_V);
            clear_flag(FLAG_Z);
            clear_flag(FLAG_N);
            goto step_end;
        }
        clear_flag(FLAG_C);

        tmp.s = data1.s / data2.s;

        set_flag_if(tmp.u == 0, FLAG_Z);
        set_flag_if(tmp.s < 0, FLAG_N);
        if ((tmp.s < -0100000) || (tmp.s >= 077777)) {
            clear_flag(FLAG_C);
            set_flag(FLAG_V);
            clear_flag(FLAG_Z);
            clear_flag(FLAG_N);
            goto step_end;
        }

        r->r[reg | 1] = (data1.s % data2.s) & 0177777;
        r->r[reg] = tmp.u & 0177777;

        goto step_end;
    }

    case 0072: { /* ASH */
        if (r->model == K1801VM1) {
            illegal_trap(r);
            goto step_end;
        }
        RA_REG(reg);
        word tmp = r->r[reg];
        word old = tmp;
        DECODE_DST();
        GET_WORD(shift);

        if ((shift & 077) != 0) {
            if (shift & 040) {
                word count = 0100 - (shift & 077);
                while (count--) {
                    set_flag_if(tmp & 1, FLAG_C);
                    if (tmp & SIGN) {
                        tmp = (tmp >> 1) | SIGN;
                    } else {
                        tmp >>= 1;
                    }
                }
            } else {
                word count = shift & 037;
                while (count--) {
                    set_flag_if(tmp & SIGN, FLAG_C);
                    tmp <<= 1;
                }
            }
            set_flag_if((old & SIGN) != (tmp & SIGN), FLAG_V);
            r->r[reg] = tmp;
        } else {
            clear_flag(FLAG_V);
        }
        set_flag_if(tmp & SIGN, FLAG_N);
        set_flag_if(tmp == 0, FLAG_Z);

        goto step_end;
    }

    case 0073: { /* ASHC */
        if (r->model == K1801VM1) {
            illegal_trap(r);
            goto step_end;
        }
        RA_REG(reg);
        dword tmp = (r->r[reg] << 16) | r->r[reg | 1];
        dword old = tmp;
        DECODE_DST();
        GET_WORD(shift);

        if ((shift & 077) != 0) {
            if (shift & 040) {
                word count = 0100 - (shift & 077);
                while (count--) {
                    set_flag_if(tmp & 1, FLAG_C);
                    if (tmp & 0x80000000) {
                        tmp = (tmp >> 1) | 0x80000000;
                    } else {
                        tmp >>= 1;
                    }
                }
            } else {
                word count = shift & 037;
                while (count--) {
                    set_flag_if(tmp & 0x80000000, FLAG_C);
                    tmp <<= 1;
                }
            }
            set_flag_if((old & 0x80000000) != (tmp & 0x80000000), FLAG_V);
            r->r[reg] = tmp >> 16;
            r->r[reg | 1] = tmp & 0177777;
        } else {
            clear_flag(FLAG_V);
        }

        set_flag_if(tmp & 0x80000000, FLAG_N);
        set_flag_if(tmp == 0, FLAG_Z);

        goto step_end;
    }

    case 0074: { /* XOR */
        RA_REG(reg);
        DECODE_DST();
        GET_WORD(tmp);
        tmp ^= r->r[reg];
        PUT_WORD(tmp);
        set_flag_if(tmp & SIGN, FLAG_N);
        set_flag_if(tmp == 0, FLAG_Z);
        clear_flag(FLAG_V);
        goto step_end;
    }
    }

    //
    // Double operand instructions
    //

#define DECODE_SRC()                                                           \
  do {                                                                         \
    src_type = decode_data(r, (op >> 6) & 077, TYPE_WORD, &src_offset);        \
    if (src_type == TYPE_ERROR)                                                \
      return 0;                                                                \
  } while (0)
#define DECODE_SRCB()                                                          \
  do {                                                                         \
    src_type = decode_data(r, (op >> 6) & 077, TYPE_BYTE, &src_offset);        \
    if (src_type == TYPE_ERROR)                                                \
      return 0;                                                                \
  } while (0)

#define GET_SWORD(a)                                                           \
  word a = get_data_word(r, src_type, src_offset);                             \
  if (r->fAbort)                                                               \
    return 0;
#define GET_SBYTE(a)                                                           \
  word a = get_data_byte(r, src_type, src_offset);                             \
  if (r->fAbort)                                                               \
    return 0;

    switch ((op & 0170000) >> 12) {
    case 001: { /* MOV */
        DECODE_SRC();
        GET_SWORD(tmp);
        DECODE_DST();
        PUT_WORD(tmp);
        set_flag_if(tmp & SIGN, FLAG_N);
        set_flag_if(tmp == 0, FLAG_Z);
        clear_flag(FLAG_V);
        goto step_end;
    }
    case 011: { /* MOVB */
        DECODE_SRCB();
        GET_SBYTE(tmp);
        DECODE_DSTB();
        PUT_BYTE_MOVB(tmp);
        set_flag_if(tmp & SIGN_B, FLAG_N);
        set_flag_if(tmp == 0, FLAG_Z);
        clear_flag(FLAG_V);
        goto step_end;
    }

    case 002: { /* CMP */
        DECODE_SRC();
        GET_SWORD(tmp);
        DECODE_DST();
        GET_WORD(tmp1);
        word tmp2 = ~tmp1;
        dword tmp3 = ((dword)tmp) + ((dword)tmp2) + 1;
        tmp2 = tmp3 & 0177777;
        set_flag_if(tmp2 & SIGN, FLAG_N);
        set_flag_if(tmp2 == 0, FLAG_Z);
        set_flag_if(((tmp & SIGN) != (tmp1 & SIGN)) &&
                    ((tmp1 & SIGN) == (tmp2 & SIGN)),
                    FLAG_V);
        set_flag_if(!(tmp3 & CARRY), FLAG_C);
        goto step_end;
    }
    case 012: { /* CMPB */
        DECODE_SRCB();
        GET_SBYTE(tmp);
        DECODE_DSTB();
        GET_BYTE(tmp1);
        byte tmp2 = ~tmp1;
        dword tmp3 = ((dword)tmp) + ((dword)tmp2) + 1;
        tmp2 = tmp3 & 0377;
        set_flag_if(tmp2 & SIGN_B, FLAG_N);
        set_flag_if(tmp2 == 0, FLAG_Z);
        set_flag_if(((tmp & SIGN_B) != (tmp1 & SIGN_B)) &&
                    ((tmp1 & SIGN_B) == (tmp2 & SIGN_B)),
                    FLAG_V);
        set_flag_if(!(tmp3 & CARRY_B), FLAG_C);
        goto step_end;
    }

    case 006: { /* ADD */
        DECODE_SRC();
        GET_SWORD(tmp);
        DECODE_DST();
        GET_WORD(tmp1);
        dword tmp3 = ((dword)tmp) + ((dword)tmp1);
        word tmp2 = tmp3 & 0177777;
        PUT_WORD(tmp2);
        set_flag_if(tmp2 & SIGN, FLAG_N);
        set_flag_if(tmp2 == 0, FLAG_Z);
        set_flag_if(((tmp & SIGN) == (tmp1 & SIGN)) &&
                    ((tmp & SIGN) != (tmp2 & SIGN)),
                    FLAG_V);
        set_flag_if(tmp3 & CARRY, FLAG_C);
        goto step_end;
    }

    case 016: { /* SUB */
        DECODE_SRC();
        GET_SWORD(tmp);
        DECODE_DST();
        GET_WORD(tmp1);
        word tmp2 = ~tmp;
        dword tmp3 = ((dword)tmp1) + ((dword)tmp2) + 1;
        tmp2 = tmp3 & 0177777;
        PUT_WORD(tmp2);
        set_flag_if(tmp2 & SIGN, FLAG_N);
        set_flag_if(tmp2 == 0, FLAG_Z);
        set_flag_if(((tmp & SIGN) != (tmp1 & SIGN)) &&
                    ((tmp & SIGN) == (tmp2 & SIGN)),
                    FLAG_V);
        set_flag_if(!(tmp3 & CARRY), FLAG_C);
        goto step_end;
    }

    case 003: { /* BIT */
        DECODE_SRC();
        GET_SWORD(tmp);
        DECODE_DST();
        GET_WORD(tmp1);
        word tmp2 = tmp & tmp1;
        set_flag_if(tmp2 & SIGN, FLAG_N);
        set_flag_if(tmp2 == 0, FLAG_Z);
        clear_flag(FLAG_V);
        goto step_end;
    }
    case 013: { /* BITB */
        DECODE_SRCB();
        GET_SBYTE(tmp);
        DECODE_DSTB();
        GET_BYTE(tmp1);
        byte tmp2 = tmp & tmp1;
        set_flag_if(tmp2 & SIGN_B, FLAG_N);
        set_flag_if(tmp2 == 0, FLAG_Z);
        clear_flag(FLAG_V);
        goto step_end;
    }

    case 004: { /* BIC */
        DECODE_SRC();
        GET_SWORD(tmp);
        DECODE_DST();
        GET_WORD(tmp1);
        tmp1 = (~tmp) & tmp1;
        PUT_WORD(tmp1);
        set_flag_if(tmp1 & SIGN, FLAG_N);
        set_flag_if(tmp1 == 0, FLAG_Z);
        clear_flag(FLAG_V);
        goto step_end;
    }
    case 014: { /* BICB */
        DECODE_SRCB();
        GET_SBYTE(tmp);
        DECODE_DSTB();
        GET_BYTE(tmp1);
        tmp1 = (~tmp) & tmp1;
        PUT_BYTE(tmp1);
        set_flag_if(tmp1 & SIGN_B, FLAG_N);
        set_flag_if(tmp1 == 0, FLAG_Z);
        clear_flag(FLAG_V);
        goto step_end;
    }

    case 005: { /* BIS */
        DECODE_SRC();
        GET_SWORD(tmp);
        DECODE_DST();
        GET_WORD(tmp1);
        tmp1 = tmp | tmp1;
        PUT_WORD(tmp1);
        set_flag_if(tmp1 & SIGN, FLAG_N);
        set_flag_if(tmp1 == 0, FLAG_Z);
        clear_flag(FLAG_V);
        goto step_end;
    }
    case 015: { /* BISB */
        DECODE_SRCB();
        GET_SBYTE(tmp);
        DECODE_DSTB();
        GET_BYTE(tmp1);
        tmp1 = tmp | tmp1;
        PUT_BYTE(tmp1);
        set_flag_if(tmp1 & SIGN_B, FLAG_N);
        set_flag_if(tmp1 == 0, FLAG_Z);
        clear_flag(FLAG_V);
        goto step_end;
    }
    }

    illegal_trap(r);
    goto step_end;
step_end:
    if ((r->model == K1801VM1 || r->model == K1801VM1G) &&
            (r->TVE_CSR & 000020)) {
        if (r->TVE_COUNT != 0) {
            r->TVE_COUNT--;
        }
        if (r->TVE_COUNT == 0) {
            if (r->TVE_CSR & 000004) {
                if (r->TVE_PENDING) {
                    r->TVE_CSR |= 000200;
                } else {
                    r->TVE_PENDING = 1;
                }
            }
            if (!(r->TVE_CSR & 000002)) {
                if (r->TVE_CSR & 000010) {
                    r->TVE_CSR &= ~000020;
                }
                if (r->TVE_LIMIT == 0) {
                    r->TVE_COUNT = 0177777;
                } else {
                    r->TVE_COUNT = r->TVE_LIMIT;
                }
            }
        }
    }
    if (!skip_trace && (psw_before & FLAG_T)) {
        do_trace = 1;
    }
    if (defer_halt && r->fHaltSignal) {
        r->fHaltSignal = 0;
        if (is_vm2(r) && flag_is_set(FLAG_H)) {
            r->fWait = 0;
            return 0;
        }
        r->fWait = 0;
        handle_halt(r);
        return 0;
    }
    if (do_trace) {
        word old_psw = r->psw;
        pushw(r->psw);
        pushw(r->r[7]);
        r->r[7] = load_word_vector(r, 014);
        r->psw = trap_psw(r, old_psw, load_word_vector(r, 016));
    }
    if (!do_trace) {
        if (r->model == K1801VM1G && r->TVE_PENDING && (r->TVE_CSR & 000004)) {
            if ((r->psw & 01000) == 0 && (r->psw & FLAG_P) == 0) {
                word old_psw = r->psw;
                r->TVE_PENDING = 0;
                pushw(r->psw);
                pushw(r->r[7]);
                r->r[7] = load_word_vector(r, 000270);
                r->psw = trap_psw(r, old_psw, load_word_vector(r, 000272));
                return 0;
            }
        }
        if (!skip_irq) {
            if (r->poll_irq && r->poll_irq(r, &irq_vector)) {
                word vec;
                if (irq_accept(r, irq_vector, &vec)) {
                    word old_psw = r->psw;
                    pushw(r->psw);
                    pushw(r->r[7]);
                    r->r[7] = load_word_vector(r, vec);
                    r->psw = trap_psw(r, old_psw, load_word_vector(r, (word)(vec + 2)));
                }
            }
        }
    }
    if (is_vm2(r)) {
        if ((r->psw & (FLAG_P | FLAG_H)) != (FLAG_P | FLAG_H)) {
            r->cps = r->psw;
            r->cpc = r->r[7];
        }
    }
    return 0;
}
