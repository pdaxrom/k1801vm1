/*
 * core.c
 *
 *  Created on: 19.10.2016
 *      Author: Alexander Chukov <sash@pdaXrom.org>
 */

#include "core.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef PICO_ON_DEVICE
#include "pico/stdlib.h"
#endif

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

/*
 * Batch PSW flag helpers: compute multiple flags in one PSW write.
 * Avoids 3-4 separate read-modify-write cycles per instruction.
 */
#define PSW_SET_NZV_WORD(result)                                               \
  do {                                                                         \
    word _f = 0;                                                               \
    if ((result) & SIGN)                                                       \
      _f |= FLAG_N;                                                            \
    if ((word)(result) == 0)                                                   \
      _f |= FLAG_Z;                                                            \
    r->psw = (r->psw & ~(FLAG_N | FLAG_Z | FLAG_V)) | _f;                      \
  } while (0)

#define PSW_SET_NZV_BYTE(result)                                               \
  do {                                                                         \
    word _f = 0;                                                               \
    if ((result) & SIGN_B)                                                     \
      _f |= FLAG_N;                                                            \
    if (((result) & 0377) == 0)                                                \
      _f |= FLAG_Z;                                                            \
    r->psw = (r->psw & ~(FLAG_N | FLAG_Z | FLAG_V)) | _f;                      \
  } while (0)

static INLINE dword arith_rshift32(dword value, word count)
{
    if (count == 0) {
        return value;
    }
    if (value & 0x80000000u) {
        return (value >> count) | (~0u << (32 - count));
    }
    return value >> count;
}

#define is_vm2(r) ((r)->model == K1801VM2 || (r)->model == K1806VM2)
#define has_prev_space_ops(r) (is_vm2(r) || (r)->model == DCJ11)
#define PSW_CM_MASK 0140000
#define PSW_PM_MASK 0030000

static INLINE int psw_mode_normalize(int mode)
{
    mode &= 03;
    return (mode == 02) ? 0 : mode;
}

static INLINE int dcj11_kernel_psw(word psw)
{
    return psw_mode_normalize((psw >> 14) & 03) == 0;
}

static INLINE int dcj11_psw_cur_mode(word psw)
{
    return psw_mode_normalize((psw >> 14) & 03);
}

static INLINE int dcj11_psw_prev_mode_mxpi(word psw)
{
    int pm = (psw >> 12) & 03;
    /*
     * J-11 MxPI special case: PM=2 selects user stack bank semantics.
     * Keep global mode normalization unchanged and scope this override to
     * MFPI/MFPD/MTPI/MTPD only.
     */
    if (pm == 02) {
        return 03;
    }
    return psw_mode_normalize(pm);
}

static INLINE word dcj11_psw_set_cur_mode(word psw, int mode)
{
    int m = psw_mode_normalize(mode);
    return (word)((psw & ~PSW_CM_MASK) | ((word)(m & 03) << 14));
}

static INLINE word dcj11_psw_set_prev_mode(word psw, int mode)
{
    int m = psw_mode_normalize(mode);
    return (word)((psw & ~PSW_PM_MASK) | ((word)(m & 03) << 12));
}

static INLINE int dcj11_psw_regset(word psw)
{
    return (psw >> 11) & 01;
}

static INLINE bool vm1_reg_block_load_word(regs *r, word offset,
        word *value_out)
{
    switch (offset) {
    case 0177700:
        *value_out = 017777;
        return true;
    case 0177702:
        *value_out = r->VM1_RAP_PRESENT ? 017777 : 0;
        return true;
    case 0177704:
        *value_out = 0177340;
        return true;
    case 0177706:
        *value_out = r->TVE_LIMIT;
        return true;
    case 0177710:
        *value_out = r->TVE_COUNT;
        return true;
    case 0177712:
        *value_out = r->TVE_CSR;
        return true;
    default:
        return false;
    }
}

static INLINE bool vm1_reg_block_store_word(regs *r, word offset, word value)
{
    switch (offset) {
    case 0177700:
        return true;
    case 0177702:
        r->VM1_RAP_PRESENT = 0;
        return true;
    case 0177704:
        return true;
    case 0177706:
        r->TVE_LIMIT = value;
        return true;
    case 0177710:
        return true;
    case 0177712:
        r->TVE_CSR = (word)(0177400 | (value & 0177));
        r->TVE_COUNT = r->TVE_LIMIT;
        return true;
    default:
        return false;
    }
}

static INLINE bool vm1_reg_block_load_byte(regs *r, word offset, byte *val8)
{
    word value;
    if (!vm1_reg_block_load_word(r, offset, &value)) {
        return false;
    }
    *val8 = (byte)((offset & 1) ? ((value >> 8) & 0377) : (value & 0377));
    return true;
}

static INLINE bool vm1_reg_block_store_byte(regs *r, word offset, byte value)
{
    word regv;
    if (!vm1_reg_block_load_word(r, offset, &regv)) {
        return false;
    }
    if (offset & 1) {
        regv = (word)((regv & 000377) | (((word)value & 0377) << 8));
    } else {
        regv = (word)((regv & 0177400) | ((word)value & 0377));
    }
    return vm1_reg_block_store_word(r, offset, regv);
}

#define DCJ11_REG_MEMERR 0177744
#define DCJ11_REG_CCR 0177746
#define DCJ11_REG_MAINT 0177750
#define DCJ11_REG_HITMISS 0177752
#ifdef DCJ_REG_RSVD_ENABLED
#define DCJ11_REG_RSVD_177754 0177754
#define DCJ11_REG_RSVD_177756 0177756
#define DCJ11_REG_RSVD_177760 0177760
#define DCJ11_REG_RSVD_177762 0177762
#define DCJ11_REG_RSVD_177764 0177764
#endif
#define DCJ11_REG_CPUERR 0177766
#define DCJ11_REG_RSVD_177770 0177770
#define DCJ11_REG_PIRQ 0177772
#define DCJ11_REG_PSW 0177776

#define DCJ11_CPUERR_RED 0000004
#define DCJ11_CPUERR_YEL 0000010
#define DCJ11_CPUERR_TMO 0000020
#define DCJ11_CPUERR_NXM 0000040
#define DCJ11_CPUERR_ADR 0000100
#define DCJ11_CPUERR_HALT 0000200
#define DCJ11_CPUERR_MASK 0000374

#define DCJ11_PIRQ_RW 0177000
#define DCJ11_PIRQ_IMP 0177356
#define DCJ11_STACK_YEL_LIMIT 0000400

static INLINE void dcj11_set_cpuerr(regs *r, word bits)
{
    if (r->model != DCJ11) {
        return;
    }
    r->J11_CPUERR = (word)((r->J11_CPUERR | bits) & DCJ11_CPUERR_MASK);
}

static INLINE int dcj11_pirq_highest_level(word pirq)
{
    word req = (word)(pirq & DCJ11_PIRQ_RW);
    if (req & 0100000) {
        return 7;
    }
    if (req & 0040000) {
        return 6;
    }
    if (req & 0020000) {
        return 5;
    }
    if (req & 0010000) {
        return 4;
    }
    if (req & 0004000) {
        return 3;
    }
    if (req & 0002000) {
        return 2;
    }
    if (req & 0001000) {
        return 1;
    }
    return 0;
}

static INLINE word dcj11_pirq_pri_field(int level)
{
    switch (level) {
    case 1:
        return 0000042;
    case 2:
        return 0000104;
    case 3:
        return 0000146;
    case 4:
        return 0000210;
    case 5:
        return 0000252;
    case 6:
        return 0000314;
    case 7:
        return 0000356;
    default:
        return 0;
    }
}

static INLINE word dcj11_pirq_visible(word pirq)
{
    int level = dcj11_pirq_highest_level(pirq);
    word req = (word)(pirq & DCJ11_PIRQ_RW);
    return (word)((req | dcj11_pirq_pri_field(level)) & DCJ11_PIRQ_IMP);
}

static INLINE void dcj11_set_psw(regs *r, word new_psw);
static INLINE word dcj11_explicit_psw_write(regs *r, word new_psw);

static INLINE bool dcj11_reg_block_load_word(regs *r, word offset,
        word *value)
{
    switch (offset & 0177776) {
    case DCJ11_REG_MEMERR:
        *value = r->J11_MEMERR;
        return true;
    case DCJ11_REG_CCR:
        *value = r->J11_CCR;
        return true;
    case DCJ11_REG_MAINT:
        *value = r->J11_MAINT;
        return true;
    case DCJ11_REG_HITMISS:
        *value = r->J11_HITMISS;
        return true;
#ifdef DCJ_REG_RSVD_ENABLED
    case DCJ11_REG_RSVD_177754:
        *value = r->J11_RSVD_177754;
        return true;
    case DCJ11_REG_RSVD_177756:
        *value = r->J11_RSVD_177756;
        return true;
    case DCJ11_REG_RSVD_177760:
        *value = r->J11_RSVD_177760;
        return true;
    case DCJ11_REG_RSVD_177762:
        *value = r->J11_RSVD_177762;
        return true;
    case DCJ11_REG_RSVD_177764:
        *value = r->J11_RSVD_177764;
        return true;
#endif
    case DCJ11_REG_CPUERR:
        *value = (word)(r->J11_CPUERR & DCJ11_CPUERR_MASK);
        return true;
    case DCJ11_REG_RSVD_177770:
        *value = r->J11_RSVD_177770;
        return true;
    case DCJ11_REG_PIRQ:
        *value = dcj11_pirq_visible(r->J11_PIRQ);
        return true;
    case DCJ11_REG_PSW:
        *value = r->psw;
        return true;
    default:
        return false;
    }
}

static INLINE bool dcj11_reg_block_store_word(regs *r, word offset,
        word value)
{
    switch (offset & 0177776) {
    case DCJ11_REG_MEMERR:
        /* Memory system error register: cleared by any write. */
        r->J11_MEMERR = 0;
        return true;
    case DCJ11_REG_CCR:
        r->J11_CCR = value;
        return true;
    case DCJ11_REG_MAINT:
        /* Maintenance register is read-only in normal mode. */
        return true;
    case DCJ11_REG_HITMISS:
        /* Hit/miss register is read-only for software. */
        return true;
#ifdef DCJ_REG_RSVD_ENABLED
    case DCJ11_REG_RSVD_177754:
        r->J11_RSVD_177754 = value;
        return true;
    case DCJ11_REG_RSVD_177756:
        r->J11_RSVD_177756 = value;
        return true;
    case DCJ11_REG_RSVD_177760:
        r->J11_RSVD_177760 = value;
        return true;
    case DCJ11_REG_RSVD_177762:
        r->J11_RSVD_177762 = value;
        return true;
    case DCJ11_REG_RSVD_177764:
        r->J11_RSVD_177764 = value;
        return true;
#endif
    case DCJ11_REG_CPUERR:
        /* CPU error register clears on write. */
        r->J11_CPUERR = 0;
        return true;
    case DCJ11_REG_RSVD_177770:
        r->J11_RSVD_177770 = value;
        return true;
    case DCJ11_REG_PIRQ:
        r->J11_PIRQ = (word)(value & DCJ11_PIRQ_RW);
        return true;
    case DCJ11_REG_PSW:
        dcj11_set_psw(r, dcj11_explicit_psw_write(r, value));
        return true;
    default:
        return false;
    }
}

static INLINE bool dcj11_reg_block_load_byte(regs *r, word offset, byte *val8)
{
    word value;
    if (!dcj11_reg_block_load_word(r, offset, &value)) {
        return false;
    }
    *val8 = (byte)((offset & 1) ? ((value >> 8) & 0377) : (value & 0377));
    return true;
}

static INLINE bool dcj11_reg_block_store_byte(regs *r, word offset,
        byte value)
{
    if ((offset & 0177776) == DCJ11_REG_PIRQ) {
        if ((offset & 1) == 0) {
            /* Byte writes to PIRQ low byte are ignored. */
            return true;
        }
        /* Byte writes to PIRQ high byte update request bits. */
        return dcj11_reg_block_store_word(r, DCJ11_REG_PIRQ, (word)(value << 8));
    }

    if (offset == DCJ11_REG_MEMERR || offset == DCJ11_REG_HITMISS ||
            offset == DCJ11_REG_CPUERR || offset == DCJ11_REG_MAINT) {
        return dcj11_reg_block_store_word(r, offset, 0);
    }

    word regv;
    if (!dcj11_reg_block_load_word(r, offset, &regv)) {
        return false;
    }
    if (offset & 1) {
        regv = (word)((regv & 000377) | (((word)value & 0377) << 8));
    } else {
        regv = (word)((regv & 0177400) | ((word)value & 0377));
    }
    return dcj11_reg_block_store_word(r, offset, regv);
}

static INLINE word dcj11_explicit_psw_write(regs *r, word new_psw)
{
    if (r->model == DCJ11) {
        /* 11/84-class behavior: explicit PSW references do not alter T-bit. */
        new_psw = (word)((new_psw & ~FLAG_T) | (r->psw & FLAG_T));
    }
    return new_psw;
}

static INLINE word trap_psw(regs *r, word old_psw, word vec_psw)
{
    if (is_vm2(r)) {
        if (old_psw & FLAG_H) {
            /* HALT entry: vector may load PSW[8:0] (0000777). */
            return (word)((old_psw & ~0000777) | (vec_psw & 0000777));
        }
        /* USER entry: load PSW[7:0] and force H/U (0000400) to 0. */
        return (word)((old_psw & ~0000777) | (vec_psw & 0000377));
    } else if (r->model == DCJ11) {
        int old_cm = dcj11_psw_cur_mode(old_psw);
        vec_psw = dcj11_psw_set_cur_mode(vec_psw, 0);
        vec_psw = dcj11_psw_set_prev_mode(vec_psw, old_cm);
    }
    return vec_psw;
}

static INLINE word dcj11_rti_rtt_protect_psw(word old_psw, word new_psw)
{
    const word set_only_mask = 0174000; /* PS<15:11>: may only be set outside kernel */
    const word keep_ipl_mask = 000340;  /* PS<7:5>: unchanged outside kernel */

    new_psw = (word)((new_psw & ~set_only_mask) |
                     ((new_psw | old_psw) & set_only_mask));
    new_psw = (word)((new_psw & ~keep_ipl_mask) | (old_psw & keep_ipl_mask));
    return new_psw;
}

static INLINE void dcj11_sp_mode_init(regs *r)
{
    int mode;

    if (r->model != DCJ11) {
        return;
    }
    if (r->sp_mode_init) {
        return;
    }
    for (mode = 0; mode < 4; mode++) {
        r->sp_mode[mode] = r->r[6];
    }
    r->sp_mode_init = 1;
}

static INLINE void dcj11_switch_stack_mode(regs *r, word old_psw,
        word new_psw)
{
    int old_mode;
    int new_mode;

    if (r->model != DCJ11) {
        return;
    }
    old_mode = dcj11_psw_cur_mode(old_psw);
    new_mode = dcj11_psw_cur_mode(new_psw);
    if (old_mode == new_mode) {
        return;
    }

    dcj11_sp_mode_init(r);
    r->sp_mode[old_mode] = r->r[6];
    r->r[6] = r->sp_mode[new_mode];
}

static INLINE void dcj11_regset_init(regs *r)
{
    int reg;

    if (r->model != DCJ11) {
        return;
    }
    if (r->rset_bank_init) {
        return;
    }
    for (reg = 0; reg < 6; reg++) {
        r->rset_bank[0][reg] = r->r[reg];
        r->rset_bank[1][reg] = r->r[reg];
    }
    r->rset_bank_init = 1;
}

static INLINE void dcj11_switch_regset(regs *r, word old_psw, word new_psw)
{
    int old_sel;
    int new_sel;
    int reg;

    if (r->model != DCJ11) {
        return;
    }
    old_sel = dcj11_psw_regset(old_psw);
    new_sel = dcj11_psw_regset(new_psw);
    if (old_sel == new_sel) {
        return;
    }

    dcj11_regset_init(r);
    for (reg = 0; reg < 6; reg++) {
        r->rset_bank[old_sel][reg] = r->r[reg];
        r->r[reg] = r->rset_bank[new_sel][reg];
    }
}

static INLINE void dcj11_set_psw(regs *r, word new_psw)
{
    word old_psw = r->psw;
    if (r->model == DCJ11) {
        dcj11_switch_stack_mode(r, old_psw, new_psw);
        dcj11_switch_regset(r, old_psw, new_psw);
    }
    r->psw = new_psw;
}

static INLINE void dcj11_note_stack_reference(regs *r, word addr)
{
    if (r->model != DCJ11) {
        return;
    }
    if (r->dcj11_stack_trap_active) {
        return;
    }
    if (!dcj11_kernel_psw(r->psw)) {
        return;
    }
    if (addr < DCJ11_STACK_YEL_LIMIT) {
        dcj11_set_cpuerr(r, DCJ11_CPUERR_YEL);
        r->dcj11_yellow_pending = 1;
    }
}

static INLINE word dcj11_read_mode_reg(regs *r, int mode, word reg)
{
    if (reg != 6 || r->model != DCJ11) {
        return r->r[reg & 07];
    }
    dcj11_sp_mode_init(r);
    return r->sp_mode[psw_mode_normalize(mode)];
}

static INLINE void dcj11_write_mode_reg(regs *r, int mode, word reg,
                                        word value)
{
    if (reg != 6 || r->model != DCJ11) {
        r->r[reg & 07] = value;
        return;
    }
    dcj11_sp_mode_init(r);
    r->sp_mode[psw_mode_normalize(mode)] = value;
    if (dcj11_psw_cur_mode(r->psw) == psw_mode_normalize(mode)) {
        r->r[6] = value;
    }
}

#define pushw(v)                                                               \
  {                                                                            \
    r->r[6] -= 2;                                                              \
    dcj11_note_stack_reference(r, r->r[6]);                                    \
    store_word(r, r->r[6], v);                                                 \
  }

#define pullw(v)                                                               \
  {                                                                            \
    v = load_word(r, r->r[6]);                                                 \
    r->r[6] += 2;                                                              \
  }

static INLINE int irq_accept(regs *r, word irq_vector, word *vec_out)
{
    word vec = (r->model == K1801VM1)
               ? irq_vector
               : (irq_vector & 0777);

    int pri = (irq_vector >> 9) & 07;
    int psw_pri = (r->psw >> 5) & 07;
    if (r->model == K1801VM1) {
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
    } else if (is_vm2(r)) {
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

static INLINE int dcj11_pirq_poll(regs *r, word *irq_vector)
{
    int level;
    int psw_pri;

    if (r->model != DCJ11) {
        return 0;
    }
    level = dcj11_pirq_highest_level(r->J11_PIRQ);
    if (level == 0) {
        return 0;
    }
    psw_pri = (r->psw >> 5) & 07;
    if (psw_pri >= level) {
        /* Masked PIRQ level must not block polling of external IRQ sources. */
        return 0;
    }
    *irq_vector = (word)(0000240 | ((level & 07) << 9));
    return 1;
}

static INLINE int core_poll_irq_any(regs *r, word *irq_vector)
{
    if (dcj11_pirq_poll(r, irq_vector)) {
        return 1;
    }
    if (r->poll_irq && r->poll_irq(r, irq_vector)) {
        return 1;
    }
    return 0;
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

/* J11 MMR0 (SSR0) subset compatible with SIMH's MM0_J mask. */
#define MMU_SSR0_ENABLE 0000001
#define MMU_SSR0_PAGE_SHIFT 1
#define MMU_SSR0_PAGE_MASK 0000176
#define MMU_SSR0_PROTECT 0020000
#define MMU_SSR0_LENGTH 0040000
#define MMU_SSR0_NONRES 0100000
#define MMU_SSR0_FREEZE 0160000
#define MMU_SSR0_J_MASK 0160177
#define MMU_SSR0_J_WR_MASK 0171401

#define MMU_SSR3_UD 0000001
#define MMU_SSR3_SD 0000002
#define MMU_SSR3_KD 0000004
#define MMU_SSR3_CSM 0000010
#define MMU_SSR3_M22E 0000020
#define MMU_SSR3_BME 0000040 /* latched, but UB map translation is not modeled */
#define MMU_SSR3_J_MASK 0000077

#define MMU_PAR_J_MASK 0177777
#define MMU_PDR_J_MASK 0177516
#define MMU_PDR_W 0000100
#define MMU_PDR_A 0000200

#define MMU_PA18_MASK 000777777
#define MMU_PA22_MASK 017777777
#define MMU_PA18_IOPAGE_BASE 000760000
#define MMU_PA22_IOPAGE_PREFIX 017000000

#define MMU_TRAP_VECTOR 0000250

#if defined(ENABLE_MMU) && (ENABLE_MMU)
static void INLINE mmu_tlb_update(regs *r, int mode, int space, int seg);
static void INLINE mmu_tlb_flush_all(regs *r);
static INLINE int mmu_split_enabled(const regs *r, int mode);
#endif

static INLINE void bus_error_trap(regs *r);
static INLINE void mmu_fault_trap(regs *r, word va, word pc, int fault,
                                  int mode, int space, int seg);
static INLINE int dcj11_take_red_stack_abort(regs *r, const char *cause);
#if defined(ENABLE_MMU) && (ENABLE_MMU)
static INLINE dword mmu_phys_finalize(dword pa, word ssr3);
static INLINE int mmu_acf_read_ok(word pdr);
static INLINE int mmu_acf_write_ok(word pdr);
static INLINE int mmu_acf_write_is_protect(word pdr);
#endif
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

#if defined(ENABLE_MMU) && (ENABLE_MMU)
static INLINE word mmu_ssr1_visible(regs *r)
{
    /*
     * J-11 suppresses #/@# (PC autoincrement, encoded as 027) entries when
     * MMR1 is read. Keep internal raw tracking and apply cleanup on readback.
     */
    word mmr1 = r->mmu_ssr1;
    if (r->model != DCJ11) {
        return mmr1;
    }
    if ((mmr1 >> 8) == 027) {
        mmr1 = (word)(mmr1 & 0377);
    }
    if ((mmr1 & 0377) == 027) {
        mmr1 = (word)(mmr1 >> 8);
    }
    return mmr1;
}
#endif

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
        *value_out = mmu_ssr1_visible(r);
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
        *value_out = (word)(r->mmu_ssr3 & MMU_SSR3_J_MASK);
#else
        *value_out = 0;
#endif
        return 1;
    }
    if (mmu_decode_parpdr(a, &mode, &space, &is_par, &seg)) {
#if defined(ENABLE_MMU) && (ENABLE_MMU)
        *value_out =
            is_par ? (r->mmu_par[mode][space][seg] & MMU_PAR_J_MASK)
                   : (r->mmu_pdr[mode][space][seg] & MMU_PDR_J_MASK);
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
        word old_ssr0 = r->mmu_ssr0;
        word data = (word)(value & MMU_SSR0_J_MASK);
        r->mmu_ssr0 =
            (word)((r->mmu_ssr0 & ~MMU_SSR0_J_WR_MASK) | (data & MMU_SSR0_J_WR_MASK));
        if ((old_ssr0 ^ value) & MMU_SSR0_ENABLE) {
            mmu_tlb_flush_all(r);
        }
#else
        (void)value;
#endif
        return 1;
    }
    if (a == MMU_SSR1) {
#if defined(ENABLE_MMU) && (ENABLE_MMU)
        (void)value; /* MMR1 is read-only */
#else
        (void)value;
#endif
        return 1;
    }
    if (a == MMU_SSR2) {
#if defined(ENABLE_MMU) && (ENABLE_MMU)
        (void)value; /* MMR2 is read-only */
#else
        (void)value;
#endif
        return 1;
    }
    if (a == MMU_SSR3 || a == MMU_SSR3_ALT) {
#if defined(ENABLE_MMU) && (ENABLE_MMU)
        r->mmu_ssr3 = (word)(value & MMU_SSR3_J_MASK);
        mmu_tlb_flush_all(r);
#else
        (void)value;
#endif
        return 1;
    }
    if (mmu_decode_parpdr(a, &mode, &space, &is_par, &seg)) {
#if defined(ENABLE_MMU) && (ENABLE_MMU)
        if (is_par) {
            r->mmu_par[mode][space][seg] = (word)(value & MMU_PAR_J_MASK);
            /* Any APR write clears status bits in the paired PDR. */
            r->mmu_pdr[mode][space][seg] &= (word)~(MMU_PDR_A | MMU_PDR_W);
        } else {
            /* J11 masks unimplemented bits; PDR A/W are read-only and clear. */
            r->mmu_pdr[mode][space][seg] =
                (word)((value & MMU_PDR_J_MASK) & ~(MMU_PDR_A | MMU_PDR_W));
        }
        mmu_tlb_update(r, mode, space, seg);
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
static void INLINE mmu_tlb_update(regs *r, int mode, int space, int seg)
{
    word pdr = (word)(r->mmu_pdr[mode][space][seg] & MMU_PDR_J_MASK);
    word par = (word)(r->mmu_par[mode][space][seg] & MMU_PAR_J_MASK);
    int acf = pdr & 07;
    int ed = (pdr >> 3) & 01;
    int len = (pdr >> 8) & 0177;

    r->mmu_tlb[mode][space][seg].host_read_base = NULL;
    r->mmu_tlb[mode][space][seg].host_write_base = NULL;
    r->mmu_tlb[mode][space][seg].valid_min = 0;
    r->mmu_tlb[mode][space][seg].valid_max = 0;

    if (acf == 0) {
        return; /* Non-resident */
    }

    if (ed) {
        /* Expand Down: valid from (len*64) to 8191 */
        r->mmu_tlb[mode][space][seg].valid_min = (uint16_t)(len << 6);
        r->mmu_tlb[mode][space][seg].valid_max = 0x1FFF;
    } else {
        /* Expand Up: valid from 0 to (len*64 + 63) */
        r->mmu_tlb[mode][space][seg].valid_min = 0;
        r->mmu_tlb[mode][space][seg].valid_max = (uint16_t)((len << 6) | 0x3F);
    }

    if (r->ram_fast) {
        dword pa_base = (dword)par << 6;
        dword va_base = (dword)seg << 13;

        /* Fast bypass only for space mapped strictly into RAM */
        if ((pa_base + 0x2000) <= r->ram_fast_size) {
            uint8_t *base_ptr = r->ram_fast + pa_base - va_base;
            r->mmu_tlb[mode][space][seg].host_read_base = base_ptr;
            /* Writes bypassed only if segment already has W bit set */
            if ((pdr & 0000100) && (acf == 2 || acf == 3 || acf == 6 || acf == 7)) {
                r->mmu_tlb[mode][space][seg].host_write_base = base_ptr;
            }
        }
    }
}

static void INLINE mmu_tlb_flush_all(regs *r)
{
    int mode, space, seg;
    for (mode = 0; mode < 4; mode++) {
        for (space = 0; space < 2; space++) {
            for (seg = 0; seg < 8; seg++) {
                mmu_tlb_update(r, mode, space, seg);
            }
        }
    }
}

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
    return (psw >> 14) & 03;
}

static INLINE void mmu_note_write_pdrw(regs *r, int mode, int space, int seg)
{
#if defined(ENABLE_MMU) && (ENABLE_MMU)
    if (r->model != DCJ11) {
        return;
    }
    if ((r->mmu_ssr0 & MMU_SSR0_ENABLE) == 0) {
        return;
    }
    mode &= 03;
    seg &= 07;
    if (space && !mmu_split_enabled(r, mode)) {
        space = 0;
    }
    r->mmu_pdr[mode][space ? 1 : 0][seg] |= MMU_PDR_W;
#else
    (void)r;
    (void)mode;
    (void)space;
    (void)seg;
#endif
}

static INLINE void mmu_note_internal_reg_write(regs *r, word va,
                                               int force_kernel_d)
{
#if defined(ENABLE_MMU) && (ENABLE_MMU)
    int mode;
    int space;

    if (r->model != DCJ11) {
        return;
    }
    if ((r->mmu_ssr0 & MMU_SSR0_ENABLE) == 0) {
        return;
    }

    mode = force_kernel_d ? 0 : mmu_mode_from_psw(r->psw);
    space = force_kernel_d ? (mmu_split_enabled(r, 0) ? 1 : 0)
                           : (mmu_split_enabled(r, mode) ? 1 : 0);
    mmu_note_write_pdrw(r, mode, space, (va >> 13) & 07);
#else
    (void)r;
    (void)va;
    (void)force_kernel_d;
#endif
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

static INLINE dword mmu_phys_finalize(dword pa, word ssr3)
{
    /*
     * J11 CPU-side translation uses only 18/22-bit select (M22E).
     * BME (UB map enable) is currently kept as a readable/writable bit but
     * does not alter CPU address translation in this emulator.
     */
    if (ssr3 & MMU_SSR3_M22E) {
        return pa & MMU_PA22_MASK;
    }
    pa &= MMU_PA18_MASK;
    if (pa >= MMU_PA18_IOPAGE_BASE) {
        pa = MMU_PA22_IOPAGE_PREFIX | pa;
    }
    return pa;
}

static INLINE int mmu_acf_read_ok(word pdr)
{
    /* J11 PDR uses a 2-bit access code in bits <2:1> (bit 0 not implemented). */
    int acf = pdr & 06;
    return (acf == 02 || acf == 06);
}

static INLINE int mmu_acf_write_ok(word pdr)
{
    int acf = pdr & 06;
    return (acf == 06);
}

static INLINE int mmu_acf_write_is_protect(word pdr)
{
    int acf = pdr & 06;
    return (acf == 02);
}
#endif

#if !defined(ENABLE_MMU) || !(ENABLE_MMU)
static INLINE void mmu_note_write_pdrw(regs *r, int mode, int space, int seg)
{
    (void)r;
    (void)mode;
    (void)space;
    (void)seg;
}

static INLINE void mmu_note_internal_reg_write(regs *r, word va,
                                               int force_kernel_d)
{
    (void)r;
    (void)va;
    (void)force_kernel_d;
}
#endif

static INLINE void mmu_mmr1_instruction_start(regs *r)
{
#if defined(ENABLE_MMU) && (ENABLE_MMU)
    if (r->model != DCJ11) {
        return;
    }
    if (r->mmu_ssr0 & MMU_SSR0_FREEZE) {
        return;
    }
    /*
     * J-11 loads MMR2 with instruction VA at the start of each fetch cycle,
     * independent of relocation enable, unless MMR0<15:13> freeze is active.
     */
    r->mmu_ssr2 = r->r[7];
    r->mmu_ssr1 = 0;
#else
    (void)r;
#endif
}

static INLINE void mmu_mmr1_record_delta(regs *r, int reg, int delta)
{
#if defined(ENABLE_MMU) && (ENABLE_MMU)
    word entry;

    if (r->model != DCJ11) {
        return;
    }
    if (r->mmu_ssr0 & MMU_SSR0_FREEZE) {
        return;
    }

    entry = (word)((((word)delta & 037) << 3) | (reg & 07));
    if ((r->mmu_ssr1 & 0000377) == 0) {
        r->mmu_ssr1 = entry;
        return;
    }
    if ((r->mmu_ssr1 & 0177400) == 0) {
        r->mmu_ssr1 = (word)(r->mmu_ssr1 | (entry << 8));
    }
#else
    (void)r;
    (void)reg;
    (void)delta;
#endif
}

static INLINE int translate_va_ex(regs *r, word va, int is_write, int is_ifetch,
                                  int force_kernel_d, dword *pa_out,
                                  int *fault_code_out, int *mode_out,
                                  int *space_out, int *seg_out)
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
    if (space_out) {
        *space_out = 0;
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
        word pdr = (word)(r->mmu_pdr[mode][space][seg] & MMU_PDR_J_MASK);
        word par = (word)(r->mmu_par[mode][space][seg] & MMU_PAR_J_MASK);
        int ed = (pdr >> 3) & 01;
        int len = (pdr >> 8) & 0177;

        if (mode_out) {
            *mode_out = mode;
        }
        if (space_out) {
            *space_out = space;
        }
        if (seg_out) {
            *seg_out = seg;
        }

        if (mode == 2) {
            /*
             * J-11 PSW current mode 2 is illegal for MMU translation paths.
             * Model this as an immediate non-resident abort rather than
             * normalizing to kernel mode.
             */
            if (is_write) {
                mmu_note_write_pdrw(r, mode, space, seg);
            }
            if (fault_code_out) {
                *fault_code_out = MMU_FAULT_NONRES;
            }
            return -1;
        }

        if ((!ed && block > len) || (ed && block < len)) {
            if (is_write) {
                mmu_note_write_pdrw(r, mode, space, seg);
            }
            if (fault_code_out) {
                *fault_code_out = MMU_FAULT_LENGTH;
            }
            return -1;
        }

        if (is_write) {
            if (!mmu_acf_write_ok(pdr)) {
                mmu_note_write_pdrw(r, mode, space, seg);
                if (fault_code_out) {
                    *fault_code_out =
                        mmu_acf_write_is_protect(pdr) ? MMU_FAULT_PROTECT
                                                     : MMU_FAULT_NONRES;
                }
                return -1;
            }
        } else if (!mmu_acf_read_ok(pdr)) {
            if (fault_code_out) {
                *fault_code_out = MMU_FAULT_NONRES;
            }
            return -1;
        }

        if (is_write) {
            /* Write implies access; keep PDR<A/W> status bits updated. */
            r->mmu_pdr[mode][space][seg] |= (MMU_PDR_A | MMU_PDR_W);
        } else {
            r->mmu_pdr[mode][space][seg] |= MMU_PDR_A;
        }

        if (pa_out) {
            dword pa_block = (dword)((par + block) & MMU_PAR_J_MASK);
            *pa_out = mmu_phys_finalize((pa_block << 6) | (va & 077), r->mmu_ssr3);
        }
    }
#else
    (void)r;
    (void)is_write;
    (void)is_ifetch;
    (void)force_kernel_d;
    (void)fault_code_out;
    (void)mode_out;
    (void)space_out;
    (void)seg_out;
#endif

    return 0;
}

/*
 * Required centralized translation API.
 * is_ifetch: 1=instruction fetch (I-space), 0=data access (I/D per SSR3)
 */
static INLINE int translate_va(regs *r, word va, int is_write, int is_ifetch,
                               dword *pa_out, int *fault_code_out,
                               int *mode_out, int *space_out, int *seg_out)
{
    return translate_va_ex(r, va, is_write, is_ifetch, 0, pa_out, fault_code_out,
                           mode_out, space_out, seg_out);
}

static INLINE int translate_va_mode_space(regs *r, word va, int is_write,
        int mode_in, int space_in,
        dword *pa_out, int *fault_code_out,
        int *space_out, int *seg_out)
{
    if (pa_out) {
        *pa_out = va;
    }
    if (fault_code_out) {
        *fault_code_out = MMU_FAULT_NONE;
    }
    if (space_out) {
        *space_out = space_in ? 1 : 0;
    }
    if (seg_out) {
        *seg_out = (va >> 13) & 07;
    }

#if defined(ENABLE_MMU) && (ENABLE_MMU)
    if (r->model != DCJ11) {
        return 0;
    }

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
        int mode = psw_mode_normalize(mode_in);
        int space = space_in ? 1 : 0;
        int seg = (va >> 13) & 07;
        int block = (va >> 6) & 0177;
        word pdr;
        word par;
        int ed;
        int len;

        if (space && !mmu_split_enabled(r, mode)) {
            space = 0;
        }
        if (space_out) {
            *space_out = space;
        }
        if (seg_out) {
            *seg_out = seg;
        }

        pdr = (word)(r->mmu_pdr[mode][space][seg] & MMU_PDR_J_MASK);
        par = (word)(r->mmu_par[mode][space][seg] & MMU_PAR_J_MASK);
        ed = (pdr >> 3) & 01;
        len = (pdr >> 8) & 0177;

        if ((!ed && block > len) || (ed && block < len)) {
            if (is_write) {
                mmu_note_write_pdrw(r, mode, space, seg);
            }
            if (fault_code_out) {
                *fault_code_out = MMU_FAULT_LENGTH;
            }
            return -1;
        }

        if (is_write) {
            if (!mmu_acf_write_ok(pdr)) {
                mmu_note_write_pdrw(r, mode, space, seg);
                if (fault_code_out) {
                    *fault_code_out =
                        mmu_acf_write_is_protect(pdr) ? MMU_FAULT_PROTECT
                                                     : MMU_FAULT_NONRES;
                }
                return -1;
            }
        } else if (!mmu_acf_read_ok(pdr)) {
            if (fault_code_out) {
                *fault_code_out = MMU_FAULT_NONRES;
            }
            return -1;
        }

        if (is_write) {
            r->mmu_pdr[mode][space][seg] |= (MMU_PDR_A | MMU_PDR_W);
        } else {
            r->mmu_pdr[mode][space][seg] |= MMU_PDR_A;
        }

        if (pa_out) {
            dword pa_block = (dword)((par + block) & MMU_PAR_J_MASK);
            *pa_out = mmu_phys_finalize((pa_block << 6) | (va & 077), r->mmu_ssr3);
        }
    }
#else
    (void)r;
    (void)is_write;
    (void)mode_in;
    (void)space_in;
    (void)fault_code_out;
    (void)space_out;
    (void)seg_out;
#endif

    return 0;
}

static INLINE void mmu_record_fault(regs *r, word va, word pc, int fault,
                                    int mode, int space, int seg)
{
#if defined(ENABLE_MMU) && (ENABLE_MMU)
    if (r->model != DCJ11) {
        return;
    }
    /* First-fault latch semantics (MMR0<15:13> freeze updates). */
    if (r->mmu_ssr0 & MMU_SSR0_FREEZE) {
        return;
    }
    r->mmu_ssr0 = (word)(((r->mmu_ssr0 & MMU_SSR0_ENABLE) |
                          ((((word)(((mode & 03) << 4) | ((space & 01) << 3) |
                                     (seg & 07))) << MMU_SSR0_PAGE_SHIFT) &
                           MMU_SSR0_PAGE_MASK) |
                          mmu_fault_to_ssr0_bits(fault)) &
                         MMU_SSR0_J_MASK);
    if (r->model == DCJ11) {
        /*
         * J11 MMR2 latches the faulting instruction PC, not the post-incremented
         * resume PC used for the trap stack frame.
         */
        r->mmu_ssr2 = r->instr_pc;
    } else {
        r->mmu_ssr2 = pc;
    }
    (void)va;
#else
    (void)r;
    (void)va;
    (void)pc;
    (void)fault;
    (void)mode;
    (void)space;
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
    int space = 0;
    int seg = 0;
    int rc;

#if (!defined(ENABLE_MMU) || !(ENABLE_MMU))
    /* Fast path: RAM access bypasses entire callback chain. */
    if (r->ram_fast && offset < r->ram_fast_size) {
        return r->ram_fast[offset];
    }
#else
    if (r->model == DCJ11 && (r->mmu_ssr0 & MMU_SSR0_ENABLE)) {
        mode = mmu_mode_from_psw(r->psw);
        seg = offset >> 13;
        uint16_t po = offset & 0x1FFF;
        int space = (is_ifetch || !mmu_split_enabled(r, mode)) ? 0 : 1;
        if (po >= r->mmu_tlb[mode][space][seg].valid_min &&
                po <= r->mmu_tlb[mode][space][seg].valid_max &&
                r->mmu_tlb[mode][space][seg].host_read_base != NULL) {
            r->mmu_pdr[mode][space][seg] |= MMU_PDR_A;
            return r->mmu_tlb[mode][space][seg].host_read_base[offset];
        }
    }
#endif

    if (r->model == DCJ11) {
        if (dcj11_reg_block_load_byte(r, offset, &value)) {
            return value;
        }
    } else if (r->model == K1801VM1) {
        if (vm1_reg_block_load_byte(r, offset, &value)) {
            return value;
        }
    }

    if (mmu_io_read_byte(r, offset, &value)) {
        return value;
    }

    if (force_kernel_d) {
        rc = translate_va_ex(r, offset, 0, is_ifetch, 1, &pa, &fault, &mode, &space,
                             &seg);
    } else {
        rc = translate_va(r, offset, 0, is_ifetch, &pa, &fault, &mode, &space, &seg);
#if defined(ENABLE_MMU) && (ENABLE_MMU)
        if (rc < 0 && r->model == DCJ11 && (r->mmu_ssr0 & MMU_SSR0_ENABLE)) {
            mode = mmu_mode_from_psw(r->psw);
            space = (is_ifetch || !mmu_split_enabled(r, mode)) ? 0 : 1;
            seg = (offset >> 13) & 07;
        }
#endif
    }
    if (rc < 0) {
        mmu_fault_trap(r, offset, r->r[7], fault, mode, space, seg);
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
    int space = 0;
    int seg = 0;
    int rc;

#if (!defined(ENABLE_MMU) || !(ENABLE_MMU))
    /* Fast path: RAM access bypasses entire callback chain. */
    if (r->ram_fast && offset < r->ram_fast_size) {
        r->ram_fast[offset] = value;
        return;
    }
#else
    if (r->model == DCJ11 && (r->mmu_ssr0 & MMU_SSR0_ENABLE)) {
        mode = mmu_mode_from_psw(r->psw);
        seg = offset >> 13;
        uint16_t po = offset & 0x1FFF;
        int space = (!mmu_split_enabled(r, mode)) ? 0 : 1;
        if (po >= r->mmu_tlb[mode][space][seg].valid_min &&
                po <= r->mmu_tlb[mode][space][seg].valid_max &&
                r->mmu_tlb[mode][space][seg].host_write_base != NULL) {
            r->mmu_pdr[mode][space][seg] |= (MMU_PDR_A | MMU_PDR_W);
            r->mmu_tlb[mode][space][seg].host_write_base[offset] = value;
            return;
        }
    }
#endif

    if (r->model == DCJ11) {
        if (dcj11_reg_block_store_byte(r, offset, value)) {
            mmu_note_internal_reg_write(r, offset, force_kernel_d);
            return;
        }
    } else if (r->model == K1801VM1) {
        if (vm1_reg_block_store_byte(r, offset, value)) {
            return;
        }
    }

    if (mmu_io_write_byte(r, offset, value)) {
        mmu_note_internal_reg_write(r, offset, force_kernel_d);
        return;
    }

    if (force_kernel_d) {
        rc = translate_va_ex(r, offset, 1, 0, 1, &pa, &fault, &mode, &space, &seg);
    } else {
        rc = translate_va(r, offset, 1, 0, &pa, &fault, &mode, &space, &seg);
#if defined(ENABLE_MMU) && (ENABLE_MMU)
        if (rc < 0 && r->model == DCJ11 && (r->mmu_ssr0 & MMU_SSR0_ENABLE)) {
            mode = mmu_mode_from_psw(r->psw);
            space = (!mmu_split_enabled(r, mode)) ? 0 : 1;
            seg = (offset >> 13) & 07;
        }
#endif
    }
    if (rc < 0) {
        mmu_fault_trap(r, offset, r->r[7], fault, mode, space, seg);
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
    int space = 0;
    int seg = 0;
    int rc;

#if (!defined(ENABLE_MMU) || !(ENABLE_MMU))
    /* Fast path: RAM word access bypasses entire callback chain. */
    if (r->ram_fast && (offset + 1) < r->ram_fast_size) {
        return (word)(r->ram_fast[offset] | ((word)r->ram_fast[offset + 1] << 8));
    }
#else
    if (r->model == DCJ11 && (r->mmu_ssr0 & MMU_SSR0_ENABLE)) {
        mode = mmu_mode_from_psw(r->psw);
        seg = offset >> 13;
        uint16_t po = offset & 0x1FFF;
        int space = (is_ifetch || !mmu_split_enabled(r, mode)) ? 0 : 1;
        if (po >= r->mmu_tlb[mode][space][seg].valid_min &&
                po <= r->mmu_tlb[mode][space][seg].valid_max &&
                r->mmu_tlb[mode][space][seg].host_read_base != NULL) {
            r->mmu_pdr[mode][space][seg] |= MMU_PDR_A;
            uint8_t *p = r->mmu_tlb[mode][space][seg].host_read_base + offset;
            return (word)(p[0] | ((word)p[1] << 8));
        }
    }
#endif

    if (r->model == DCJ11) {
        if (offset & 1) {
            dcj11_set_cpuerr(r, DCJ11_CPUERR_ADR);
            bus_error_trap(r);
            return 0;
        }

        word regv;
        if (dcj11_reg_block_load_word(r, offset, &regv)) {
            if (is_ifetch) {
                dcj11_set_cpuerr(r, DCJ11_CPUERR_ADR);
                bus_error_trap(r);
                return 0;
            }
            return regv;
        }
    } else if (r->model == K1801VM1) {
        word value;
        if (vm1_reg_block_load_word(r, offset, &value)) {
            return value;
        }
    }

    if (mmu_io_read_word(r, offset, &value)) {
        if (r->model == DCJ11 && is_ifetch) {
            /*
             * J-11 treats instruction fetch from internal MMU register block
             * as address error (CPUERR.ADR), trapping through vector 4.
             */
            dcj11_set_cpuerr(r, DCJ11_CPUERR_ADR);
            bus_error_trap(r);
            return 0;
        }
        return value;
    }

    if (force_kernel_d) {
        rc = translate_va_ex(r, offset, 0, is_ifetch, 1, &pa, &fault, &mode, &space,
                             &seg);
    } else {
        rc = translate_va(r, offset, 0, is_ifetch, &pa, &fault, &mode, &space, &seg);
#if defined(ENABLE_MMU) && (ENABLE_MMU)
        if (rc < 0 && r->model == DCJ11 && (r->mmu_ssr0 & MMU_SSR0_ENABLE)) {
            mode = mmu_mode_from_psw(r->psw);
            space = (is_ifetch || !mmu_split_enabled(r, mode)) ? 0 : 1;
            seg = (offset >> 13) & 07;
        }
#endif
    }
    if (rc < 0) {
        mmu_fault_trap(r, offset, r->r[7], fault, mode, space, seg);
        return 0;
    }
    value = raw_load_word_phys(r, pa);
    return value;
}

static INLINE void core_store_word_ex(regs *r, word offset, word value,
                                      int force_kernel_d)
{
    dword pa = 0;
    int fault = MMU_FAULT_NONE;
    int mode = 0;
    int space = 0;
    int seg = 0;
    int rc;

#if (!defined(ENABLE_MMU) || !(ENABLE_MMU))
    /* Fast path: RAM word access bypasses entire callback chain. */
    if (r->ram_fast && (offset + 1) < r->ram_fast_size) {
        r->ram_fast[offset] = (uint8_t)(value & 000377);
        r->ram_fast[offset + 1] = (uint8_t)((value >> 8) & 000377);
        return;
    }
#else
    if (r->model == DCJ11 && (r->mmu_ssr0 & MMU_SSR0_ENABLE)) {
        mode = mmu_mode_from_psw(r->psw);
        seg = offset >> 13;
        uint16_t po = offset & 0x1FFF;
        int space = (!mmu_split_enabled(r, mode)) ? 0 : 1;
        if (po >= r->mmu_tlb[mode][space][seg].valid_min &&
                po <= r->mmu_tlb[mode][space][seg].valid_max &&
                r->mmu_tlb[mode][space][seg].host_write_base != NULL) {
            r->mmu_pdr[mode][space][seg] |= (MMU_PDR_A | MMU_PDR_W);
            uint8_t *p = r->mmu_tlb[mode][space][seg].host_write_base + offset;
            p[0] = (uint8_t)(value & 000377);
            p[1] = (uint8_t)((value >> 8) & 000377);
            return;
        }
    }
#endif

    if (r->model == DCJ11) {
        if (offset & 1) {
            dcj11_set_cpuerr(r, DCJ11_CPUERR_ADR);
            bus_error_trap(r);
            return;
        }

        if (dcj11_reg_block_store_word(r, offset, value)) {
            mmu_note_internal_reg_write(r, offset, force_kernel_d);
            return;
        }
    } else if (r->model == K1801VM1) {
        if (vm1_reg_block_store_word(r, offset, value)) {
            return;
        }
    }

    if (mmu_io_write_word(r, offset, value)) {
        mmu_note_internal_reg_write(r, offset, force_kernel_d);
        return;
    }

    if (force_kernel_d) {
        rc = translate_va_ex(r, offset, 1, 0, 1, &pa, &fault, &mode, &space, &seg);
    } else {
        rc = translate_va(r, offset, 1, 0, &pa, &fault, &mode, &space, &seg);
#if defined(ENABLE_MMU) && (ENABLE_MMU)
        if (rc < 0 && r->model == DCJ11 && (r->mmu_ssr0 & MMU_SSR0_ENABLE)) {
            mode = mmu_mode_from_psw(r->psw);
            space = (!mmu_split_enabled(r, mode)) ? 0 : 1;
            seg = (offset >> 13) & 07;
        }
#endif
    }
    if (rc < 0) {
        mmu_fault_trap(r, offset, r->r[7], fault, mode, space, seg);
        return;
    }
    raw_store_word_phys(r, pa, value);
}

static INLINE word core_load_word_mode_space(regs *r, word offset, int mode,
        int data_space)
{
    dword pa = 0;
    int fault = MMU_FAULT_NONE;
    int space = 0;
    int seg = 0;
    int rc;
    word value;

#if defined(ENABLE_MMU) && (ENABLE_MMU)
    if (r->model == DCJ11 && (r->mmu_ssr0 & MMU_SSR0_ENABLE)) {
        mode = psw_mode_normalize(mode);
        seg = offset >> 13;
        uint16_t po = offset & 0x1FFF;
        int space = (data_space && mmu_split_enabled(r, mode)) ? 1 : 0;
        if (po >= r->mmu_tlb[mode][space][seg].valid_min &&
                po <= r->mmu_tlb[mode][space][seg].valid_max &&
                r->mmu_tlb[mode][space][seg].host_read_base != NULL) {
            r->mmu_pdr[mode][space][seg] |= MMU_PDR_A;
            uint8_t *p = r->mmu_tlb[mode][space][seg].host_read_base + offset;
            return (word)(p[0] | ((word)p[1] << 8));
        }
    }
#endif

    if (r->model == DCJ11) {
        if (offset & 1) {
            dcj11_set_cpuerr(r, DCJ11_CPUERR_ADR);
            bus_error_trap(r);
            return 0;
        }

        word value;
        if (dcj11_reg_block_load_word(r, offset, &value)) {
            return value;
        }
    } else if (r->model == K1801VM1) {
        word value;
        if (vm1_reg_block_load_word(r, offset, &value)) {
            return value;
        }
    }

    if (mmu_io_read_word(r, offset, &value)) {
        return value;
    }

    rc = translate_va_mode_space(r, offset, 0, mode, data_space, &pa, &fault,
                                 &space, &seg);
    if (rc < 0) {
        mmu_fault_trap(r, offset, r->r[7], fault, psw_mode_normalize(mode), space,
                       seg);
        return 0;
    }
    return raw_load_word_phys(r, pa);
}

static INLINE void core_store_word_mode_space(regs *r, word offset, word value,
        int mode, int data_space)
{
    dword pa = 0;
    int fault = MMU_FAULT_NONE;
    int space = 0;
    int seg = 0;
    int rc;

#if defined(ENABLE_MMU) && (ENABLE_MMU)
    if (r->model == DCJ11 && (r->mmu_ssr0 & MMU_SSR0_ENABLE)) {
        mode = psw_mode_normalize(mode);
        seg = offset >> 13;
        uint16_t po = offset & 0x1FFF;
        int space = (data_space && mmu_split_enabled(r, mode)) ? 1 : 0;
        if (po >= r->mmu_tlb[mode][space][seg].valid_min &&
                po <= r->mmu_tlb[mode][space][seg].valid_max &&
                r->mmu_tlb[mode][space][seg].host_write_base != NULL) {
            r->mmu_pdr[mode][space][seg] |= (MMU_PDR_A | MMU_PDR_W);
            uint8_t *p = r->mmu_tlb[mode][space][seg].host_write_base + offset;
            p[0] = (uint8_t)(value & 000377);
            p[1] = (uint8_t)((value >> 8) & 000377);
            return;
        }
    }
#endif

    if (r->model == DCJ11) {
        if (offset & 1) {
            dcj11_set_cpuerr(r, DCJ11_CPUERR_ADR);
            bus_error_trap(r);
            return;
        }

        if (dcj11_reg_block_store_word(r, offset, value)) {
            mmu_note_write_pdrw(r, mode, data_space ? 1 : 0, (offset >> 13) & 07);
            return;
        }
    } else if (r->model == K1801VM1) {
        if (vm1_reg_block_store_word(r, offset, value)) {
            return;
        }
    }

    if (mmu_io_write_word(r, offset, value)) {
        mmu_note_write_pdrw(r, mode, data_space ? 1 : 0, (offset >> 13) & 07);
        return;
    }

    rc = translate_va_mode_space(r, offset, 1, mode, data_space, &pa, &fault,
                                 &space, &seg);
    if (rc < 0) {
        mmu_fault_trap(r, offset, r->r[7], fault, psw_mode_normalize(mode), space,
                       seg);
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
    word a = (word)(offset & 0177776);
#if defined(ENABLE_MMU) && (ENABLE_MMU)
    if (r->model == DCJ11) {
        /* PDP-11 traps fetch vectors via kernel D-space when MMU is present. */
        return core_load_word_mode_space(r, a, 0, 1);
    }
#endif
    return raw_load_word_phys(r, a);
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

void core_init(regs *r)
{
    if (r->model == K1806VM2) {
        r->model = K1801VM2;
    }
    if (r->model == DCJ11) {
        r->has_fis = 0;
        r->has_fpu = 1;
    } else {
        r->has_fis = 0;
        r->has_fpu = 0;
    }
    r->init(r);
}

void core_reset(regs *r)
{
    int mode;
    int bank;
    int reg;

    for (mode = 0; mode < 4; mode++) {
        r->sp_mode[mode] = 0;
    }
    r->sp_mode_init = 0;
    for (bank = 0; bank < 2; bank++) {
        for (reg = 0; reg < 6; reg++) {
            r->rset_bank[bank][reg] = 0;
        }
    }
    r->rset_bank_init = 0;
    r->dcj11_yellow_pending = 0;
    r->dcj11_stack_trap_active = 0;
    r->dcj11_vector_push_active = 0;
    r->dcj11_vector_old_pc = 0;
    r->dcj11_vector_old_psw = 0;
    r->ir = 0;
    r->J11_MEMERR = 0;
    r->J11_CCR = 0;
    r->J11_MAINT = 0;
    r->J11_HITMISS = 0;
#ifdef DCJ_REG_RSVD_ENABLED
    r->J11_RSVD_177754 = 0;
    r->J11_RSVD_177756 = 0;
    r->J11_RSVD_177760 = 0;
    r->J11_RSVD_177762 = 0;
    r->J11_RSVD_177764 = 0;
#endif
    r->J11_CPUERR = 0;
    r->J11_RSVD_177770 = 0;
    r->J11_PIRQ = 0;
    r->fWait = 0;
    r->fTrap = 0;
    r->fAbort = 0;
    r->fHaltSignal = 0;
    r->fStepDeferHalt = 0;
#if defined(ENABLE_MMU) && (ENABLE_MMU)
    r->mmu_ssr0 = 0;
    r->mmu_ssr1 = 0;
    r->mmu_ssr2 = 0;
    r->mmu_ssr3 = 0;
    memset(r->mmu_par, 0, sizeof(r->mmu_par));
    memset(r->mmu_pdr, 0, sizeof(r->mmu_pdr));
#endif

    r->reset(r);

    if (r->model == K1801VM1) {
        r->TVE_CSR = 0177400;
        r->TVE_LIMIT = 0;
        r->TVE_COUNT = 0;
        r->TVE_PENDING = 0;
        r->VM1_RAP_PRESENT = 1;
        r->r[7] = load_word(r, 0177716) & 0177400;
    } else if (is_vm2(r)) {
        r->r[7] = r->SEL0 & 0177400;
    } else {
        r->r[7] = 0;
    }
    r->psw = 0340;

    /* Cache direct RAM pointer for fast-path access in core_step. */
    if (r->ramptr) {
        r->ram_fast = r->ramptr(r, 0);
        /* ram_fast_size set externally or defaults to bus_ram_bytes() */
    } else {
        r->ram_fast = NULL;
    }
}

void core_fini(regs *r)
{
    if (r->fini) {
        r->fini(r);
    }
}

static INLINE void core_take_vector(regs *r, word vec, word old_pc,
                                    word old_psw, const char *kind)
{
    word new_pc = 0;
    word fetched_psw = 0;
    word new_psw = 0;
    (void)kind;

    fetched_psw = load_word_vector(r, (word)(vec + 2));
    if (r->fAbort) {
        return;
    }
    new_pc = load_word_vector(r, vec);
    if (r->fAbort) {
        return;
    }

    new_psw = trap_psw(r, old_psw, fetched_psw);
    dcj11_set_psw(r, new_psw);

    if (r->model == DCJ11) {
        r->dcj11_vector_old_pc = old_pc;
        r->dcj11_vector_old_psw = old_psw;
        r->dcj11_vector_push_active = 1;
    }

    pushw(old_psw);
    if (r->fAbort) {
        r->dcj11_vector_push_active = 0;
        return;
    }
    pushw(old_pc);
    if (r->fAbort) {
        r->dcj11_vector_push_active = 0;
        return;
    }
    r->dcj11_vector_push_active = 0;
    r->r[7] = new_pc;
}

static INLINE int dcj11_service_stack_trap(regs *r)
{
    word old_psw;

    if (r->model != DCJ11) {
        return 0;
    }
    if (!r->dcj11_yellow_pending || r->dcj11_stack_trap_active) {
        return 0;
    }

    old_psw = r->psw;
    r->dcj11_yellow_pending = 0;
    r->dcj11_stack_trap_active = 1;
    core_take_vector(r, 000004, r->r[7], old_psw, "STK");
    r->dcj11_stack_trap_active = 0;
    return 1;
}

static INLINE int dcj11_take_red_stack_abort(regs *r, const char *cause)
{
    word old_psw;
    word old_pc;
    word vector_psw;
    word new_psw;
    word new_pc;
    (void)cause;

    if (r->model != DCJ11 || !r->dcj11_vector_push_active) {
        return 0;
    }

    old_psw = r->dcj11_vector_old_psw;
    old_pc = r->dcj11_vector_old_pc;
    r->dcj11_vector_push_active = 0;
    r->dcj11_yellow_pending = 0;

    /*
     * Abort during trap/interrupt stack push: restore pre-trap state and
     * take red stack trap using emergency kernel stack at locations 2 and 0.
     */
    dcj11_set_psw(r, old_psw);
    r->r[7] = old_pc;
    dcj11_set_cpuerr(r, DCJ11_CPUERR_RED);

    vector_psw = load_word_vector(r, 000006);
    if (r->fAbort) {
        return 1;
    }
    new_psw = trap_psw(r, old_psw, vector_psw);
    dcj11_set_psw(r, new_psw);

    dcj11_sp_mode_init(r);
    r->sp_mode[0] = 000004;
    if (dcj11_psw_cur_mode(r->psw) == 0) {
        r->r[6] = 000004;
    }

    r->r[6] -= 2;
    store_word(r, r->r[6], old_psw);
    if (r->fAbort) {
        return 1;
    }
    r->r[6] -= 2;
    store_word(r, r->r[6], old_pc);
    if (r->fAbort) {
        return 1;
    }

    new_pc = load_word_vector(r, 000004);
    if (!r->fAbort) {
        r->r[7] = new_pc;
    }
    r->fAbort = 1;
    return 1;
}

static INLINE void dcj11_reset_instruction_state(regs *r)
{
    if (r->model != DCJ11) {
        return;
    }
    /* J-11 RESET clears PIRQ; CPUERR is explicitly unaffected. */
    r->J11_PIRQ = 0;
#if defined(ENABLE_MMU) && (ENABLE_MMU)
    /*
     * J-11 RESET clears MMR0<15:13,0> and MMR3.
     * MMR1/MMR2 are not explicitly reset by RESET.
     */
    r->mmu_ssr0 = 0;
    r->mmu_ssr3 = 0;
    mmu_tlb_flush_all(r);
#endif
}

static INLINE void illegal_trap(regs *r)
{
    word old_psw = r->psw;
    core_take_vector(r, 010, r->r[7], old_psw, "ILL");
}

static INLINE void trap_vector4(regs *r, const char *kind)
{
    word old_psw = r->psw;
    core_take_vector(r, 000004, r->r[7], old_psw, kind);
}

static INLINE void bus_error_trap(regs *r)
{
    word old_psw = r->psw;
    static int trace_init = 0;
    static int trace_on = 0;
    if (dcj11_take_red_stack_abort(r, "BUSERR")) {
        return;
    }
    if (!trace_init) {
        trace_on = (getenv("CORE_TRACE_BUSERR") != NULL) ? 1 : 0;
        trace_init = 1;
    }
    if (trace_on) {
        fprintf(stderr, "BUSERR pc=%06o fault=%06o sp=%06o ps=%06o ir=%06o\n",
                r->r[7], r->r[7], r->r[6], r->psw, r->ir);
    }
    core_take_vector(r, 000004, r->r[7], old_psw, "BUSERR");
    r->fAbort = 1;
}

static INLINE void mmu_fault_trap(regs *r, word va, word pc, int fault,
                                  int mode, int space, int seg)
{
    word old_psw = r->psw;
    if (dcj11_take_red_stack_abort(r, "MMU")) {
        return;
    }
    mmu_record_fault(r, va, pc, fault, mode, space, seg);
    core_take_vector(r, MMU_TRAP_VECTOR, pc, old_psw, "MMU");
    r->fAbort = 1;
}

static INLINE void handle_halt(regs *r)
{
    word vec;
    if (r->model == K1801VM1) {
        store_word(r, 0177676, r->psw);
        store_word(r, 0177674, r->r[7]);
        store_word(r, 0177716, load_word(r, 0177716) | 010);
        vec = (load_word(r, 0177716) & 0177400) | 002;
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
            // vec |= (r->SEL0 & 0177400);
            //  In kernel mode must start mini ODT console
        }
        r->r[7] = load_word_vector(r, vec) & 0177776;
        dcj11_set_psw(r, 0340);
    } else {
        store_word(r, 0177676, r->psw);
        store_word(r, 0177674, r->r[7]);
        store_word(r, 0177716, load_word(r, 0177716) | 010);
        vec = load_word(r, 0177716) & 0177400;
        r->r[7] = load_word_vector(r, vec + 2) & 0177776;
        r->psw = load_word_vector(r, vec + 4);
    }
}

static INLINE float f11_to_float32(uint32_t f11)
{
    uint32_t sign = (f11 >> 31) & 1;
    uint32_t exp11 = (f11 >> 23) & 0xFF;
    uint32_t frac = f11 & 0x007FFFFF;

    if (exp11 == 0) {
        return sign ? -0.0f : 0.0f;
    }

    int32_t exp32 = (int32_t)exp11 - 2;
    uint32_t ieee = 0;

    if (exp32 <= 0) {
        ieee = (sign << 31);
    } else if (exp32 >= 255) {
        ieee = (sign << 31) | (255 << 23);
    } else {
        ieee = (sign << 31) | ((uint32_t)exp32 << 23) | frac;
    }

    union {
        uint32_t u;
        float f;
    } res;
    res.u = ieee;
    return res.f;
}

static INLINE uint32_t float32_to_f11(float f, int *error)
{
    union {
        float f;
        uint32_t u;
    } val;
    val.f = f;

    uint32_t sign = (val.u >> 31) & 1;
    uint32_t exp32 = (val.u >> 23) & 0xFF;
    uint32_t frac = val.u & 0x007FFFFF;

    if (error) {
        *error = 0;
    }

    if (exp32 == 0) {
        return 0;
    }

    int32_t exp11 = (int32_t)exp32 + 2;
    if (exp11 >= 256) {
        if (error) {
            *error = 02;
        }
        return (sign << 31) | (255 << 23) | frac;
    } else if (exp11 <= 0) {
        if (error) {
            *error = 012;
        }
        return 0;
    }

    return (sign << 31) | ((uint32_t)exp11 << 23) | frac;
}

static INLINE void handle_fis(regs *r)

{
    word vec;
    if (!is_vm2(r) || r->SEL0 & 0200) {
        illegal_trap(r);
        return;
    }
    r->cps = r->psw;
    r->cpc = r->r[7];
    vec = (word)((r->SEL0 & 0177400) | 010);
    r->r[7] = load_word_vector(r, vec) & 0177776;
    r->psw = load_word_vector(r, vec + 2);
}

static INLINE void handle_fis_error(regs *r, word error)
{
    pushw(error);
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

    /*
     * Step is 2 for word ops, SP, PC-relative modes, and modes 3/5.
     * Only byte ops on R0-R5 with modes 0/1/2/4 use step=1.
     */
    if (data_type == TYPE_BYTE && reg != 6 && reg != 7 && mode != 3 &&
            mode != 5 && mode != 6 && mode != 7) {
        step = 1;
    } else {
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
        mmu_mmr1_record_delta(r, reg, step);
        if (reg == 7) {
            return TYPE_IFETCH;
        }
        return TYPE_MEM;
    case 3: /* @(Rn)+ */
        *offset = r->r[reg];
        r->r[reg] += step;
        mmu_mmr1_record_delta(r, reg, step);
        if (reg == 7) {
            *offset = load_word_ifetch(r, *offset);
        } else {
            *offset = load_word(r, *offset);
        }
        if (r->fAbort) {
            return TYPE_ERROR;
        }
        return TYPE_MEM;
    case 4: /* -(Rn) */
        r->r[reg] -= step;
        mmu_mmr1_record_delta(r, reg, -step);
        if (reg == 6) {
            dcj11_note_stack_reference(r, r->r[reg]);
        }
        *offset = r->r[reg];
        return TYPE_MEM;
    case 5: /* @-(Rn) */
        r->r[reg] -= step;
        mmu_mmr1_record_delta(r, reg, -step);
        if (reg == 6) {
            dcj11_note_stack_reference(r, r->r[reg]);
        }
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
        mmu_mmr1_record_delta(r, 7, 2);
        *offset = r->r[reg] + tmp;
    }
    return TYPE_MEM;
    case 7: { /* @X(Rn) */
        word tmp = load_word_ifetch(r, r->r[7]);
        if (r->fAbort) {
            return TYPE_ERROR;
        }
        r->r[7] += 2;
        mmu_mmr1_record_delta(r, 7, 2);
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

static int fp11(regs *r, word IR);

#ifdef PICO_ON_DEVICE
int __not_in_flash_func(core_step)(regs *r)
#else
int core_step(regs *r)
#endif
{
    word src_offset;
    byte src_type;
    word dst_offset;
    byte dst_type;
    word irq_vector;
    word psw_before = r->psw;
    int do_trace = 0;
    int trace_override = 0;
    int trace_override_value = 0;
    int defer_halt = 0;
    int skip_irq = 0;
    int skip_trace = r->fTrap ? 1 : 0;
    r->fTrap = 0;

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
        if (r->model == DCJ11 && (r->psw & FLAG_T)) {
            word old_psw = r->psw;
            r->fWait = 0;
            core_take_vector(r, 014, r->r[7], old_psw, "TRACE");
            return 0;
        }
        if (core_poll_irq_any(r, &irq_vector)) {
            word vec;
            if (irq_accept(r, irq_vector, &vec)) {
                word old_psw = r->psw;
                r->fWait = 0;
                core_take_vector(r, vec, r->r[7], old_psw, "IRQ");
            }
            return 0;
        }
        return 0;
    }

    /* IRQs are checked after instruction execution (real PDP-11 behavior). */

    // load instruction
    r->instr_pc = r->r[7];
    mmu_mmr1_instruction_start(r);

    word fetch_pc = r->r[7];
    if (r->model != DCJ11) {
        /*
         * VM* compatibility policy (11/03-style row): instruction stream
         * fetch via PC advances PC even when the fetch aborts.
         */
        r->r[7] += 2;
    }

    word op = load_word_ifetch(r, fetch_pc);
    if (r->fAbort) {
        r->fAbort = 0;
        return 0;
    }

    r->ir = op;
    if (r->model == DCJ11) {
        r->r[7] += 2;
    }

    if ((op & 0177740) == 000240) { /* Condition Code Operators */
        if (op & 000020) {
            set_flag(op & 000017);
        } else {
            clear_flag(op & 000017);
        }
        goto step_end;
    }

    //
    // No operands instructions
    //
    switch (op) {
    case 000000: { /* HALT */
        if (r->model == DCJ11 && !dcj11_kernel_psw(psw_before)) {
            if (r->model == DCJ11) {
                dcj11_set_cpuerr(r, DCJ11_CPUERR_HALT);
            }
            word old_psw = r->psw;
            core_take_vector(r, 000004, r->r[7], old_psw, "HALT");
            goto step_end;
        }
        handle_halt(r);
        goto step_end;
    }

    case 000001: /* WAIT */
        r->fWait = 1;
        if (r->model == K1801VM1 || is_vm2(r) ||
                r->model == DCJ11) {
            skip_trace = 1;
        }
        goto step_end;

    case 000002: { /* RTI */
        word popped_pc;
        word popped_psw;
        pullw(popped_pc);
        pullw(popped_psw);
        r->r[7] = popped_pc;
        if (is_vm2(r)) {
            if (r->r[7] < 0160000) {
                popped_psw = (word)((popped_psw & ~FLAG_H) | (psw_before & FLAG_H));
            }
        }
        if (r->model == DCJ11 && !dcj11_kernel_psw(psw_before)) {
            popped_psw = dcj11_rti_rtt_protect_psw(psw_before, popped_psw);
        }
        dcj11_set_psw(r, popped_psw);
        if (r->model == DCJ11) {
            trace_override = 1;
            trace_override_value = (r->psw & FLAG_T) ? 1 : 0;
        }
    }
    goto step_end;

    case 000003: { /* BPT */
        word old_psw = r->psw;
        core_take_vector(r, 014, r->r[7], old_psw, "BPT");
    }
    goto step_end;

    case 000004: { /* IOT */
        word old_psw = r->psw;
        core_take_vector(r, 020, r->r[7], old_psw, "IOT");
    }
    goto step_end;

    case 000005: /* RESET */
        if (r->model == DCJ11 && !dcj11_kernel_psw(psw_before)) {
            goto step_end;
        }
        if (r->model == DCJ11) {
            dcj11_reset_instruction_state(r);
        }
        r->fWait = 0;
        r->fHaltSignal = 0;
        if (r->reset) {
            r->reset(r);
        }
        goto step_end;

    case 000006: { /* RTT */
        word popped_pc;
        word popped_psw;
        pullw(popped_pc);
        pullw(popped_psw);
        r->r[7] = popped_pc;
        if (is_vm2(r)) {
            if (r->r[7] < 0160000) {
                popped_psw = (word)((popped_psw & ~FLAG_H) | (psw_before & FLAG_H));
            }
        } else if (r->model == DCJ11) {
            if (!dcj11_kernel_psw(psw_before)) {
                popped_psw = dcj11_rti_rtt_protect_psw(psw_before, popped_psw);
            }
        }
        dcj11_set_psw(r, popped_psw);
        skip_trace = 1;
        if (r->model == DCJ11) {
            trace_override = 1;
            trace_override_value = 0;
        } else {
            r->fTrap = 1;
        }
    }
    goto step_end;

    case 000007: /* MFPT */
        if (r->model != DCJ11) {
            illegal_trap(r);
            goto step_end;
        }
        r->r[0] = 5;
        goto step_end;


    }

    if ((op & 0177770) == 0000230) { /* SPL */
        if (r->model != DCJ11) {
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
            {
                word vec;
                word irq_vector;
                if (core_poll_irq_any(r, &irq_vector)) {
                    if (irq_accept(r, irq_vector, &vec)) {
                        word old_psw = r->psw;
                        core_take_vector(r, vec, r->r[7], old_psw, "IRQ");
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
            if (!is_vm2(r) || !flag_is_set(FLAG_H)) {
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
        core_take_vector(r, vec, r->r[7], old_psw, (op & 0400) ? "TRAP" : "EMT");
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
        r->r[6] = r->r[7] + (nn << 1);
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
            trap_vector4(r, "ILL");
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

    case 00070: { /* CSM */
        word old_sp;
        word new_sp;
        word old_psw_stack;
        word new_psw;
        int old_mode;
        int csm_enabled = 0;

        if (r->model != DCJ11) {
            illegal_trap(r);
            goto step_end;
        }
#if defined(ENABLE_MMU) && (ENABLE_MMU)
        csm_enabled = (r->mmu_ssr3 & MMU_SSR3_CSM) ? 1 : 0;
#endif
        old_mode = dcj11_psw_cur_mode(psw_before);
        if (!csm_enabled || old_mode == 0) {
            illegal_trap(r);
            goto step_end;
        }

        DECODE_DST();
        GET_WORD(csm_arg);

        old_sp = r->r[6];
        old_psw_stack =
            (word)(r->psw & ~(FLAG_N | FLAG_Z | FLAG_V | FLAG_C));

        /*
         * Match SIMH/J-11 CSM frame order on supervisor D-space stack:
         *   [SP-2] = PSW (CC cleared), [SP-4] = PC, [SP-6] = operand.
         */
        core_store_word_mode_space(r, (word)(old_sp - 2), old_psw_stack, 1, 1);
        if (r->fAbort) {
            goto step_end;
        }
        core_store_word_mode_space(r, (word)(old_sp - 4), r->r[7], 1, 1);
        if (r->fAbort) {
            goto step_end;
        }
        core_store_word_mode_space(r, (word)(old_sp - 6), csm_arg, 1, 1);
        if (r->fAbort) {
            goto step_end;
        }

        new_sp = (word)(old_sp - 6);
        dcj11_sp_mode_init(r);
        r->sp_mode[old_mode] = old_sp;
        r->sp_mode[1] = new_sp;

        new_psw = r->psw;
        new_psw = dcj11_psw_set_prev_mode(new_psw, old_mode);
        new_psw = dcj11_psw_set_cur_mode(new_psw, 1);
        new_psw = (word)(new_psw & ~FLAG_T);
        dcj11_set_psw(r, new_psw);
        if (old_mode == 1) {
            r->r[6] = new_sp;
        }
        r->sp_mode[1] = new_sp;

        r->r[7] = core_load_word_mode_space(r, 000010, 1, 0);
        if (r->fAbort) {
            goto step_end;
        }
        goto step_end;
    }

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
        word mfp_tmp = 0;
        if (!has_prev_space_ops(r)) {
            illegal_trap(r);
            goto step_end;
        }
        if (r->model != DCJ11) {
            DECODE_DST();
            mfp_tmp = get_data_word(r, dst_type, dst_offset);
            if (r->fAbort) {
                goto step_end;
            }
        } else {
            int prev_mode = dcj11_psw_prev_mode_mxpi(psw_before);
            int cur_mode = dcj11_psw_cur_mode(psw_before);
            word saved_sp = r->r[6];
            int swapped_sp = 0;

            if (prev_mode != cur_mode) {
                dcj11_sp_mode_init(r);
                r->r[6] = r->sp_mode[prev_mode];
                swapped_sp = 1;
            }
            dst_type = decode_data(r, op & 00077, TYPE_WORD, &dst_offset);
            if (swapped_sp) {
                r->sp_mode[prev_mode] = r->r[6];
                r->r[6] = saved_sp;
            }
            if (dst_type == TYPE_ERROR) {
                goto step_end;
            }
            if (dst_type == TYPE_REG) {
                mfp_tmp = dcj11_read_mode_reg(r, prev_mode, dst_offset);
            } else {
                mfp_tmp = core_load_word_mode_space(r, dst_offset, prev_mode, 1);
            }
            if (r->fAbort) {
                goto step_end;
            }
        }
        r->r[6] -= 2;
        mmu_mmr1_record_delta(r, 6, -2);
        dcj11_note_stack_reference(r, r->r[6]);
        store_word(r, r->r[6], mfp_tmp);
        if (r->fAbort) {
            goto step_end;
        }
        set_flag_if(mfp_tmp & SIGN, FLAG_N);
        set_flag_if(mfp_tmp == 0, FLAG_Z);
        clear_flag(FLAG_V);
        goto step_end;
    }
    case 01065: { /* MFPI */
        word mfp_tmp = 0;
        if (!has_prev_space_ops(r)) {
            illegal_trap(r);
            goto step_end;
        }
        if (r->model != DCJ11) {
            DECODE_DST();
            mfp_tmp = get_data_word(r, dst_type, dst_offset);
            if (r->fAbort) {
                goto step_end;
            }
        } else {
            int prev_mode = dcj11_psw_prev_mode_mxpi(psw_before);
            int cur_mode = dcj11_psw_cur_mode(psw_before);
            word saved_sp = r->r[6];
            int swapped_sp = 0;

            if (prev_mode != cur_mode) {
                dcj11_sp_mode_init(r);
                r->r[6] = r->sp_mode[prev_mode];
                swapped_sp = 1;
            }
            dst_type = decode_data(r, op & 00077, TYPE_WORD, &dst_offset);
            if (swapped_sp) {
                r->sp_mode[prev_mode] = r->r[6];
                r->r[6] = saved_sp;
            }
            if (dst_type == TYPE_ERROR) {
                goto step_end;
            }
            if (dst_type == TYPE_REG) {
                mfp_tmp = dcj11_read_mode_reg(r, prev_mode, dst_offset);
            } else {
                /*
                 * Match SIMH: MFPI in user->user context uses D-space, otherwise I-space.
                 * PC-relative/immediate modes must still access the previous space, not current I.
                 */
                int prev_is_d = (prev_mode == cur_mode && prev_mode == 3) ? 1 : 0;
                mfp_tmp = core_load_word_mode_space(r, dst_offset, prev_mode, prev_is_d);
            }
            if (r->fAbort) {
                goto step_end;
            }
        }
        r->r[6] -= 2;
        mmu_mmr1_record_delta(r, 6, -2);
        dcj11_note_stack_reference(r, r->r[6]);
        store_word(r, r->r[6], mfp_tmp);
        if (r->fAbort) {
            goto step_end;
        }
        set_flag_if(mfp_tmp & SIGN, FLAG_N);
        set_flag_if(mfp_tmp == 0, FLAG_Z);
        clear_flag(FLAG_V);
        goto step_end;
    }
    case 00066: { /* MTPI */
        if (!has_prev_space_ops(r)) {
            illegal_trap(r);
            goto step_end;
        }
        if (r->model != DCJ11) {
            DECODE_DST();
        } else {
            int prev_mode = dcj11_psw_prev_mode_mxpi(psw_before);
            int cur_mode = dcj11_psw_cur_mode(psw_before);
            word saved_sp = r->r[6];
            int swapped_sp = 0;

            if (prev_mode != cur_mode) {
                dcj11_sp_mode_init(r);
                r->r[6] = r->sp_mode[prev_mode];
                swapped_sp = 1;
            }
            dst_type = decode_data(r, op & 00077, TYPE_WORD, &dst_offset);
            if (swapped_sp) {
                r->sp_mode[prev_mode] = r->r[6];
                r->r[6] = saved_sp;
            }
            if (dst_type == TYPE_ERROR) {
                goto step_end;
            }
        }
        word tmp = load_word(r, r->r[6]);
        if (r->fAbort) {
            goto step_end;
        }
        r->r[6] += 2;
        mmu_mmr1_record_delta(r, 6, 2);
        if (r->model != DCJ11) {
            PUT_WORD(tmp);
        } else {
            int prev_mode = dcj11_psw_prev_mode_mxpi(psw_before);
            if (dst_type == TYPE_REG) {
                dcj11_write_mode_reg(r, prev_mode, dst_offset, tmp);
            } else {
                core_store_word_mode_space(r, dst_offset, tmp, prev_mode, 0);
            }
        }
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
        if (r->model != DCJ11) {
            DECODE_DST();
        } else {
            int prev_mode = dcj11_psw_prev_mode_mxpi(psw_before);
            int cur_mode = dcj11_psw_cur_mode(psw_before);
            word saved_sp = r->r[6];
            int swapped_sp = 0;

            if (prev_mode != cur_mode) {
                dcj11_sp_mode_init(r);
                r->r[6] = r->sp_mode[prev_mode];
                swapped_sp = 1;
            }
            dst_type = decode_data(r, op & 00077, TYPE_WORD, &dst_offset);
            if (swapped_sp) {
                r->sp_mode[prev_mode] = r->r[6];
                r->r[6] = saved_sp;
            }
            if (dst_type == TYPE_ERROR) {
                goto step_end;
            }
        }
        word tmp = load_word(r, r->r[6]);
        if (r->fAbort) {
            goto step_end;
        }
        r->r[6] += 2;
        mmu_mmr1_record_delta(r, 6, 2);
        if (r->model != DCJ11) {
            PUT_WORD(tmp);
        } else {
            int prev_mode = dcj11_psw_prev_mode_mxpi(psw_before);
            if (dst_type == TYPE_REG) {
                dcj11_write_mode_reg(r, prev_mode, dst_offset, tmp);
            } else {
                core_store_word_mode_space(r, dst_offset, tmp, prev_mode, 1);
            }
        }
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
        if (dst_type == TYPE_REG) {
            if (r->model == DCJ11) {
                illegal_trap(r);
            } else {
                trap_vector4(r, "ILL");
            }
            goto step_end;
        }
        r->r[6] -= 2;
        mmu_mmr1_record_delta(r, 6, -2);
        dcj11_note_stack_reference(r, r->r[6]);
        store_word(r, r->r[6], r->r[reg]);
        if (r->fAbort) {
            goto step_end;
        }
        r->r[reg] = r->r[7];
        r->r[7] = dst_offset;
        goto step_end;
    }

    case 0070: { /* MUL */
        if (r->model == K1801VM1) {
            illegal_trap(r);
            goto step_end;
        }
        sdword src;
        sdword src2;
        sdword prod;
        RA_REG(reg);
        src = (sdword)(sword)r->r[reg];
        DECODE_DST();
        src2 = (sdword)(sword)get_data_word(r, dst_type, dst_offset);
        prod = src * src2;

        r->r[reg] = (word)(((dword)prod >> 16) & 0177777);
        r->r[reg | 1] = (word)(prod & 0177777);

        set_flag_if(prod == 0, FLAG_Z);
        set_flag_if(prod < 0, FLAG_N);
        set_flag_if((prod > 077777) || (prod < -0100000), FLAG_C);
        clear_flag(FLAG_V);
        goto step_end;
    }

    case 0071: { /* DIV */
        if (r->model == K1801VM1) {
            illegal_trap(r);
            goto step_end;
        }
        sdword dividend;
        sdword divisor;
        int64_t q64;
        sdword quotient;
        sdword remainder;
        RA_REG(reg);
        dividend = (sdword)(((dword)r->r[reg] << 16) | r->r[reg | 1]);
        DECODE_DST();
        divisor = (sdword)(sword)get_data_word(r, dst_type, dst_offset);

        if (divisor == 0) {
            clear_flag(FLAG_N);
            set_flag(FLAG_Z);
            set_flag(FLAG_V);
            set_flag(FLAG_C);
            goto step_end;
        }

        if (dividend == (sdword)0x80000000u && divisor == -1) {
            clear_flag(FLAG_N);
            clear_flag(FLAG_Z);
            set_flag(FLAG_V);
            clear_flag(FLAG_C);
            goto step_end;
        }

        q64 = ((int64_t)dividend) / ((int64_t)divisor);
        set_flag_if(q64 < 0, FLAG_N);
        if (q64 > 077777 || q64 < -0100000) {
            clear_flag(FLAG_Z);
            set_flag(FLAG_V);
            clear_flag(FLAG_C);
            goto step_end;
        }

        quotient = (sdword)q64;
        remainder = dividend - (divisor * quotient);

        r->r[reg] = (word)(quotient & 0177777);
        r->r[reg | 1] = (word)(remainder & 0177777);

        set_flag_if(quotient < 0, FLAG_N);
        set_flag_if(quotient == 0, FLAG_Z);
        clear_flag(FLAG_V);
        clear_flag(FLAG_C);

        goto step_end;
    }

    case 0072: { /* ASH */
        if (r->model == K1801VM1) {
            illegal_trap(r);
            goto step_end;
        }
        word src16;
        word count;
        int sign;
        dword src;
        dword dst;
        dword spill;
        RA_REG(reg);
        src16 = r->r[reg];
        DECODE_DST();
        GET_WORD(shift);
        count = shift & 077;
        sign = (src16 & SIGN) ? 1 : 0;
        src = (dword)(sdword)(sword)src16;

        if (count == 0) {
            dst = src;
            clear_flag(FLAG_V);
            clear_flag(FLAG_C);
        } else if (count <= 15) {
            dst = src << count;
            spill = (src >> (16 - count)) & 0177777;
            set_flag_if(spill != ((dst & SIGN) ? 0177777 : 0), FLAG_V);
            set_flag_if(spill & 1, FLAG_C);
        } else if (count <= 31) {
            dst = 0;
            set_flag_if(src != 0, FLAG_V);
            set_flag_if((src << (count - 16)) & 1, FLAG_C);
        } else if (count == 32) {
            dst = sign ? 0xFFFFFFFFu : 0;
            clear_flag(FLAG_V);
            set_flag_if(sign, FLAG_C);
        } else {
            dst = arith_rshift32(src, (word)(64 - count));
            clear_flag(FLAG_V);
            set_flag_if((src >> (63 - count)) & 1, FLAG_C);
        }

        r->r[reg] = (word)(dst & 0177777);
        set_flag_if(r->r[reg] & SIGN, FLAG_N);
        set_flag_if(r->r[reg] == 0, FLAG_Z);

        goto step_end;
    }

    case 0073: { /* ASHC */
        if (r->model == K1801VM1) {
            illegal_trap(r);
            goto step_end;
        }
        word count;
        int sign;
        dword src;
        dword dst;
        dword spill;
        RA_REG(reg);
        src = ((dword)r->r[reg] << 16) | r->r[reg | 1];
        DECODE_DST();
        GET_WORD(shift);
        count = shift & 077;
        sign = (r->r[reg] & SIGN) ? 1 : 0;

        if (count == 0) {
            dst = src;
            clear_flag(FLAG_V);
            clear_flag(FLAG_C);
        } else if (count <= 31) {
            dst = src << count;
            spill = (src >> (32 - count)) | (sign ? (~0u << count) : 0);
            set_flag_if(spill != ((dst & 0x80000000u) ? 0xFFFFFFFFu : 0), FLAG_V);
            set_flag_if(spill & 1, FLAG_C);
        } else if (count == 32) {
            dst = sign ? 0xFFFFFFFFu : 0;
            clear_flag(FLAG_V);
            set_flag_if(sign, FLAG_C);
        } else {
            dst = arith_rshift32(src, (word)(64 - count));
            clear_flag(FLAG_V);
            set_flag_if((src >> (63 - count)) & 1, FLAG_C);
        }

        r->r[reg] = (word)((dst >> 16) & 0177777);
        r->r[reg | 1] = (word)(dst & 0177777);

        set_flag_if(r->r[reg] & SIGN, FLAG_N);
        set_flag_if((r->r[reg] | r->r[reg | 1]) == 0, FLAG_Z);

        goto step_end;
    }

    case 0074: { /* XOR */
        word src_reg;
        RA_REG(reg);
        src_reg = r->r[reg];
        DECODE_DST();
        GET_WORD(tmp);
        tmp ^= (r->model == DCJ11) ? r->r[reg] : src_reg;
        PUT_WORD(tmp);
        set_flag_if(tmp & SIGN, FLAG_N);
        set_flag_if(tmp == 0, FLAG_Z);
        clear_flag(FLAG_V);
        goto step_end;
    }
    case 0075: {
        if ((op & 0177740) == 0075000) {
            if (r->has_fis) {
                word reg = op & 7;
                word mop = (op >> 3) & 3;
                word addr_b = r->r[reg];
                word addr_a = (addr_b + 4) & 0177777;

                word b_w0 = load_word(r, addr_b);
                word b_w1 = load_word(r, (addr_b + 2) & 0177777);
                if (r->fAbort) {
                    return 0;
                }

                word a_w0 = load_word(r, addr_a);
                word a_w1 = load_word(r, (addr_a + 2) & 0177777);
                if (r->fAbort) {
                    return 0;
                }

                uint32_t b_f11 = ((uint32_t)b_w0 << 16) | b_w1;
                uint32_t a_f11 = ((uint32_t)a_w0 << 16) | a_w1;

                float b_f32 = f11_to_float32(b_f11);
                float a_f32 = f11_to_float32(a_f11);

                if (mop == 3 && b_f32 == 0.0f) {
                    handle_fis_error(r, 013);
                    goto step_end;
                }

                float res_f32 = 0.0f;
                switch (mop) {
                case 0:
                    res_f32 = a_f32 + b_f32;
                    break; // FADD
                case 1:
                    res_f32 = a_f32 - b_f32;
                    break; // FSUB
                case 2:
                    res_f32 = a_f32 * b_f32;
                    break; // FMUL
                case 3:
                    res_f32 = a_f32 / b_f32;
                    break; // FDIV
                }

                int fis_err = 0;
                uint32_t res_f11 = float32_to_f11(res_f32, &fis_err);

                if (fis_err) {
                    handle_fis_error(r, fis_err);
                    goto step_end;
                }

                word res_w0 = (res_f11 >> 16) & 0xFFFF;
                word res_w1 = res_f11 & 0xFFFF;

                store_word(r, addr_a, res_w0);
                if (r->fAbort) {
                    return 0;
                }
                store_word(r, (addr_a + 2) & 0177777, res_w1);
                if (r->fAbort) {
                    return 0;
                }

                r->r[reg] = addr_a;

                if (res_f11 & 0x80000000) {
                    set_flag(FLAG_N);
                } else {
                    clear_flag(FLAG_N);
                }

                if ((res_f11 & 0x7FFFFFFF) == 0) {
                    set_flag(FLAG_Z);
                } else {
                    clear_flag(FLAG_Z);
                }

                clear_flag(FLAG_V);
                clear_flag(FLAG_C);
            } else {
                handle_fis(r);
            }
            goto step_end;
        }
        break;
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
        word tmp;
        if (r->model == DCJ11 && (((op >> 6) & 070) == 0) && (op & 070)) {
            word sreg = (op >> 6) & 07;
            DECODE_DST();
            tmp = r->r[sreg];
        } else {
            DECODE_SRC();
            tmp = get_data_word(r, src_type, src_offset);
            if (r->fAbort) {
                return 0;
            }
            DECODE_DST();
        }
        PUT_WORD(tmp);
        PSW_SET_NZV_WORD(tmp);
        goto step_end;
    }
    case 011: { /* MOVB */
        word tmp;
        if (r->model == DCJ11 && (((op >> 6) & 070) == 0) && (op & 070)) {
            word sreg = (op >> 6) & 07;
            DECODE_DSTB();
            tmp = r->r[sreg] & 0377;
        } else {
            DECODE_SRCB();
            tmp = get_data_byte(r, src_type, src_offset);
            if (r->fAbort) {
                return 0;
            }
            DECODE_DSTB();
        }
        PUT_BYTE_MOVB(tmp);
        PSW_SET_NZV_BYTE(tmp);
        goto step_end;
    }

    case 002: { /* CMP */
        word tmp;
        word tmp1;
        if (r->model == DCJ11 && (((op >> 6) & 070) == 0) && (op & 070)) {
            word sreg = (op >> 6) & 07;
            DECODE_DST();
            tmp1 = get_data_word(r, dst_type, dst_offset);
            if (r->fAbort) {
                return 0;
            }
            tmp = r->r[sreg];
        } else {
            DECODE_SRC();
            tmp = get_data_word(r, src_type, src_offset);
            if (r->fAbort) {
                return 0;
            }
            DECODE_DST();
            tmp1 = get_data_word(r, dst_type, dst_offset);
            if (r->fAbort) {
                return 0;
            }
        }
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
        word tmp;
        word tmp1;
        if (r->model == DCJ11 && (((op >> 6) & 070) == 0) && (op & 070)) {
            word sreg = (op >> 6) & 07;
            DECODE_DSTB();
            tmp1 = get_data_byte(r, dst_type, dst_offset);
            if (r->fAbort) {
                return 0;
            }
            tmp = r->r[sreg] & 0377;
        } else {
            DECODE_SRCB();
            tmp = get_data_byte(r, src_type, src_offset);
            if (r->fAbort) {
                return 0;
            }
            DECODE_DSTB();
            tmp1 = get_data_byte(r, dst_type, dst_offset);
            if (r->fAbort) {
                return 0;
            }
        }
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
        word tmp;
        word tmp1;
        if (r->model == DCJ11 && (((op >> 6) & 070) == 0) && (op & 070)) {
            word sreg = (op >> 6) & 07;
            DECODE_DST();
            tmp1 = get_data_word(r, dst_type, dst_offset);
            if (r->fAbort) {
                return 0;
            }
            tmp = r->r[sreg];
        } else {
            DECODE_SRC();
            tmp = get_data_word(r, src_type, src_offset);
            if (r->fAbort) {
                return 0;
            }
            DECODE_DST();
            tmp1 = get_data_word(r, dst_type, dst_offset);
            if (r->fAbort) {
                return 0;
            }
        }
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
        word tmp;
        word tmp1;
        if (r->model == DCJ11 && (((op >> 6) & 070) == 0) && (op & 070)) {
            word sreg = (op >> 6) & 07;
            DECODE_DST();
            tmp1 = get_data_word(r, dst_type, dst_offset);
            if (r->fAbort) {
                return 0;
            }
            tmp = r->r[sreg];
        } else {
            DECODE_SRC();
            tmp = get_data_word(r, src_type, src_offset);
            if (r->fAbort) {
                return 0;
            }
            DECODE_DST();
            tmp1 = get_data_word(r, dst_type, dst_offset);
            if (r->fAbort) {
                return 0;
            }
        }
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
        word tmp;
        word tmp1;
        if (r->model == DCJ11 && (((op >> 6) & 070) == 0) && (op & 070)) {
            word sreg = (op >> 6) & 07;
            DECODE_DST();
            tmp1 = get_data_word(r, dst_type, dst_offset);
            if (r->fAbort) {
                return 0;
            }
            tmp = r->r[sreg];
        } else {
            DECODE_SRC();
            tmp = get_data_word(r, src_type, src_offset);
            if (r->fAbort) {
                return 0;
            }
            DECODE_DST();
            tmp1 = get_data_word(r, dst_type, dst_offset);
            if (r->fAbort) {
                return 0;
            }
        }
        word tmp2 = tmp & tmp1;
        PSW_SET_NZV_WORD(tmp2);
        goto step_end;
    }
    case 013: { /* BITB */
        word tmp;
        word tmp1;
        if (r->model == DCJ11 && (((op >> 6) & 070) == 0) && (op & 070)) {
            word sreg = (op >> 6) & 07;
            DECODE_DSTB();
            tmp1 = get_data_byte(r, dst_type, dst_offset);
            if (r->fAbort) {
                return 0;
            }
            tmp = r->r[sreg] & 0377;
        } else {
            DECODE_SRCB();
            tmp = get_data_byte(r, src_type, src_offset);
            if (r->fAbort) {
                return 0;
            }
            DECODE_DSTB();
            tmp1 = get_data_byte(r, dst_type, dst_offset);
            if (r->fAbort) {
                return 0;
            }
        }
        byte tmp2 = tmp & tmp1;
        PSW_SET_NZV_BYTE(tmp2);
        goto step_end;
    }

    case 004: { /* BIC */
        word tmp;
        word tmp1;
        if (r->model == DCJ11 && (((op >> 6) & 070) == 0) && (op & 070)) {
            word sreg = (op >> 6) & 07;
            DECODE_DST();
            tmp1 = get_data_word(r, dst_type, dst_offset);
            if (r->fAbort) {
                return 0;
            }
            tmp = r->r[sreg];
        } else {
            DECODE_SRC();
            tmp = get_data_word(r, src_type, src_offset);
            if (r->fAbort) {
                return 0;
            }
            DECODE_DST();
            tmp1 = get_data_word(r, dst_type, dst_offset);
            if (r->fAbort) {
                return 0;
            }
        }
        tmp1 = (~tmp) & tmp1;
        PUT_WORD(tmp1);
        PSW_SET_NZV_WORD(tmp1);
        goto step_end;
    }
    case 014: { /* BICB */
        word tmp;
        word tmp1;
        if (r->model == DCJ11 && (((op >> 6) & 070) == 0) && (op & 070)) {
            word sreg = (op >> 6) & 07;
            DECODE_DSTB();
            tmp1 = get_data_byte(r, dst_type, dst_offset);
            if (r->fAbort) {
                return 0;
            }
            tmp = r->r[sreg] & 0377;
        } else {
            DECODE_SRCB();
            tmp = get_data_byte(r, src_type, src_offset);
            if (r->fAbort) {
                return 0;
            }
            DECODE_DSTB();
            tmp1 = get_data_byte(r, dst_type, dst_offset);
            if (r->fAbort) {
                return 0;
            }
        }
        tmp1 = (~tmp) & tmp1;
        PUT_BYTE(tmp1);
        PSW_SET_NZV_BYTE(tmp1);
        goto step_end;
    }

    case 005: { /* BIS */
        word tmp;
        word tmp1;
        if (r->model == DCJ11 && (((op >> 6) & 070) == 0) && (op & 070)) {
            word sreg = (op >> 6) & 07;
            DECODE_DST();
            tmp1 = get_data_word(r, dst_type, dst_offset);
            if (r->fAbort) {
                return 0;
            }
            tmp = r->r[sreg];
        } else {
            DECODE_SRC();
            tmp = get_data_word(r, src_type, src_offset);
            if (r->fAbort) {
                return 0;
            }
            DECODE_DST();
            tmp1 = get_data_word(r, dst_type, dst_offset);
            if (r->fAbort) {
                return 0;
            }
        }
        tmp1 = tmp | tmp1;
        PUT_WORD(tmp1);
        PSW_SET_NZV_WORD(tmp1);
        goto step_end;
    }
    case 015: { /* BISB */
        word tmp;
        word tmp1;
        if (r->model == DCJ11 && (((op >> 6) & 070) == 0) && (op & 070)) {
            word sreg = (op >> 6) & 07;
            DECODE_DSTB();
            tmp1 = get_data_byte(r, dst_type, dst_offset);
            if (r->fAbort) {
                return 0;
            }
            tmp = r->r[sreg] & 0377;
        } else {
            DECODE_SRCB();
            tmp = get_data_byte(r, src_type, src_offset);
            if (r->fAbort) {
                return 0;
            }
            DECODE_DSTB();
            tmp1 = get_data_byte(r, dst_type, dst_offset);
            if (r->fAbort) {
                return 0;
            }
        }
        tmp1 = tmp | tmp1;
        PUT_BYTE(tmp1);
        PSW_SET_NZV_BYTE(tmp1);
        goto step_end;
    }
    case 017: {
        if (r->has_fpu) {
            fp11(r, op);
            goto step_end;
        }
    }
    }

    illegal_trap(r);
    goto step_end;
step_end:
    if ((r->model == K1801VM1) &&
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
    if (trace_override) {
        do_trace = trace_override_value;
    } else if (!skip_trace && (psw_before & FLAG_T)) {
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
        core_take_vector(r, 014, r->r[7], old_psw, "TRACE");
    }
    if (dcj11_service_stack_trap(r)) {
        return 0;
    }
    if (!do_trace) {
        if (!skip_irq) {
            if (r->model == DCJ11) {
                while (core_poll_irq_any(r, &irq_vector)) {
                    word vec;
                    if (!irq_accept(r, irq_vector, &vec)) {
                        break;
                    }
                    word old_psw = r->psw;
                    core_take_vector(r, vec, r->r[7], old_psw, "IRQ");
                    if (r->fAbort) {
                        break;
                    }
                }
            } else {
                if (core_poll_irq_any(r, &irq_vector)) {
                    word vec;
                    if (irq_accept(r, irq_vector, &vec)) {
                        word old_psw = r->psw;
                        core_take_vector(r, vec, r->r[7], old_psw, "IRQ");
                    }
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

#include "pdp11_fp.c"
