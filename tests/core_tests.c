#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/core.h"
#include "core/hardware.h"

#define TEST_BASE 01000
#define TEST_STACK 0400
#define DCJ11_CM(mode) ((word)(((mode) & 03) << 14))
#define DCJ11_PM(mode) ((word)(((mode) & 03) << 12))

typedef struct {
    regs r;
    byte *mem;
    byte *mem_owner;
    size_t mem_size;
} cpu_fixture;

static const char *current_test;
static int test_buserr_fetch_enable = 0;
static word test_buserr_fetch_addr = 0;
static int test_buserr_fetch_guard = 0;
static void test_dispatch_store_word(regs *r, word offset, word value);

static word test_dcj11_pirq_visible(word pirq)
{
    word req = pirq & 0177000;
    word pl = 0;
    if (req & 0100000) {
        pl = 0000356;
    } else if (req & 0040000) {
        pl = 0000314;
    } else if (req & 0020000) {
        pl = 0000252;
    } else if (req & 0010000) {
        pl = 0000210;
    } else if (req & 0004000) {
        pl = 0000146;
    } else if (req & 0002000) {
        pl = 0000104;
    } else if (req & 0001000) {
        pl = 0000042;
    }
    return (word)((req | pl) & 0177356);
}

/* Dispatchers to core internal registers replicated from core.c */
static word test_dispatch_load_word(regs *r, word offset)
{
    if (r->model == DCJ11) {
        switch (offset & 0177776) {
        case 0177744:
            return r->J11_MEMERR;
        case 0177746:
            return r->J11_CCR;
        case 0177750:
            return r->J11_MAINT;
        case 0177752:
            return r->J11_HITMISS;
        case 0177754:
            return r->J11_RSVD_177754;
        case 0177756:
            return r->J11_RSVD_177756;
        case 0177760:
            return r->J11_RSVD_177760;
        case 0177762:
            return r->J11_RSVD_177762;
        case 0177764:
            return r->J11_RSVD_177764;
        case 0177766:
            return (word)(r->J11_CPUERR & 0000374);
        case 0177770:
            return r->J11_RSVD_177770;
        case 0177772:
            return test_dcj11_pirq_visible(r->J11_PIRQ);
        }
    } else if (r->model == K1801VM1 || r->model == K1801VM1G) {
        switch (offset) {
        case 0177700:
            return 017777;
        case 0177702:
            return r->VM1_RAP_PRESENT ? 017777 : 0;
        case 0177704:
            return 0177340;
        case 0177706:
            return r->TVE_LIMIT;
        case 0177710:
            return r->TVE_COUNT;
        case 0177712:
            return r->TVE_CSR;
        }
    }
    return r->load_byte(r, offset) | (r->load_byte(r, offset + 1) << 8);
}

static void test_inject_buserr_trap(regs *r)
{
    word old_psw = r->psw;
    word fault_pc = r->r[7];
    word vector_psw = test_dispatch_load_word(r, 000006);

    r->psw = vector_psw;
    r->r[6] -= 2;
    test_dispatch_store_word(r, r->r[6], old_psw);
    r->r[6] -= 2;
    test_dispatch_store_word(r, r->r[6], fault_pc);
    r->r[7] = test_dispatch_load_word(r, 000004);
    r->fAbort = 1;
}

static word test_dispatch_load_word_fetch_fault(regs *r, word offset)
{
    if (test_buserr_fetch_enable && !test_buserr_fetch_guard &&
            offset == test_buserr_fetch_addr) {
        test_buserr_fetch_guard = 1;
        test_inject_buserr_trap(r);
        test_buserr_fetch_guard = 0;
        return 0;
    }
    return test_dispatch_load_word(r, offset);
}

static void test_dispatch_store_word(regs *r, word offset, word value)
{
    if (r->model == DCJ11) {
        switch (offset & 0177776) {
        case 0177744:
            r->J11_MEMERR = 0;
            return;
        case 0177746:
            r->J11_CCR = value;
            return;
        case 0177750:
            return;
        case 0177752:
            return;
        case 0177754:
            r->J11_RSVD_177754 = value;
            return;
        case 0177756:
            r->J11_RSVD_177756 = value;
            return;
        case 0177760:
            r->J11_RSVD_177760 = value;
            return;
        case 0177762:
            r->J11_RSVD_177762 = value;
            return;
        case 0177764:
            r->J11_RSVD_177764 = value;
            return;
        case 0177772:
            r->J11_PIRQ = value & 0177000;
            return;
        case 0177766:
            r->J11_CPUERR = 0;
            return;
        case 0177770:
            r->J11_RSVD_177770 = value;
            return;
        }
    } else if (r->model == K1801VM1 || r->model == K1801VM1G) {
        switch (offset) {
        case 0177700:
            return;
        case 0177702:
            r->VM1_RAP_PRESENT = 0;
            return;
        case 0177704:
            return;
        case 0177706:
            r->TVE_LIMIT = value;
            return;
        case 0177710:
            return;
        case 0177712:
            r->TVE_CSR = (word)(0177400 | (value & 0177));
            r->TVE_COUNT = r->TVE_LIMIT;
            return;
        }
    }
    r->store_byte(r, offset, (byte)(value & 0377));
    r->store_byte(r, offset + 1, (byte)(value >> 8));
}

static byte test_dispatch_load_byte(regs *r, word offset)
{
    word val16;
    if (r->model == K1801VM1 || r->model == K1801VM1G) {
        if (offset >= 0177700 && offset <= 0177713) {
            val16 = test_dispatch_load_word(r, offset & 0177776);
            return (byte)((offset & 1) ? (val16 >> 8) : (val16 & 0377));
        }
    } else if (r->model == DCJ11) {
        if (offset >= 0177744 && offset <= 0177773) {
            val16 = test_dispatch_load_word(r, offset & 0177776);
            return (byte)((offset & 1) ? (val16 >> 8) : (val16 & 0377));
        }
    }
    /* Fallback to hwstub via original ptr if we could save it,
       but here we know it's hardware_load_byte which we can call indirectly if we link it.
       Wait, hardware_load_byte is static in hardware.c!
       So we use fx->mem directly.
       We need access to fx->mem. Let's use r->ramptr. */
    byte *m = r->ramptr(r, offset);
    return m ? *m : 0;
}

static void test_dispatch_store_byte(regs *r, word offset, byte value)
{
    word val16;
    if (r->model == K1801VM1 || r->model == K1801VM1G) {
        if (offset >= 0177700 && offset <= 0177713) {
            val16 = test_dispatch_load_word(r, offset & 0177776);
            if (offset & 1) {
                val16 = (word)((val16 & 000377) | (value << 8));
            } else {
                val16 = (word)((val16 & 0177400) | value);
            }
            test_dispatch_store_word(r, offset & 0177776, val16);
            return;
        }
    } else if (r->model == DCJ11) {
        if (offset >= 0177744 && offset <= 0177773) {
            if ((offset & 0177776) == 0177772) {
                if (offset & 1) {
                    test_dispatch_store_word(r, 0177772, (word)(value << 8));
                }
                return;
            }
            if ((offset & 0177776) == 0177744 || (offset & 0177776) == 0177750 ||
                    (offset & 0177776) == 0177752 || (offset & 0177776) == 0177766) {
                test_dispatch_store_word(r, offset & 0177776, 0);
                return;
            }
            val16 = test_dispatch_load_word(r, offset & 0177776);
            if (offset & 1) {
                val16 = (word)((val16 & 000377) | ((word)value << 8));
            } else {
                val16 = (word)((val16 & 0177400) | (word)value);
            }
            test_dispatch_store_word(r, offset & 0177776, val16);
            return;
        }
    }
    byte *m = r->ramptr(r, offset);
    if (m) {
        *m = value;
    }
}

static void fixture_setup_model(cpu_fixture *fx, byte model)
{
    memset(fx, 0, sizeof(*fx));
    fx->mem_size = hwstub_required_memory_size();
    fx->mem_owner = (byte *)calloc(1, fx->mem_size);
    if (!fx->mem_owner) {
        fprintf(stderr, "FAIL: hwstub memory allocation (%zu bytes)\n", fx->mem_size);
        exit(1);
    }
    if (hwstub_set_memory(fx->mem_owner, fx->mem_size) != 0) {
        fprintf(stderr, "FAIL: hwstub_set_memory\n");
        free(fx->mem_owner);
        exit(1);
    }
    fx->r.model = model;
    hwstub_connect(&fx->r);
    core_init(&fx->r);
    fx->mem = fx->r.ramptr(&fx->r, 0);
    memset(fx->mem, 0, fx->mem_size);

    /* Wrap callbacks with our dispatchers */
    fx->r.load_word = test_dispatch_load_word;
    fx->r.store_word = test_dispatch_store_word;
    fx->r.load_byte = test_dispatch_load_byte;
    fx->r.store_byte = test_dispatch_store_byte;

    fx->r.SEL0 = 0;
    core_reset(&fx->r);
    fx->r.r[7] = TEST_BASE;
    fx->r.r[6] = TEST_STACK;
    fx->r.psw = 0;
}

static void fixture_setup(cpu_fixture *fx)
{
    fixture_setup_model(fx, K1801VM1);
}

static void fixture_teardown(cpu_fixture *fx)
{
    core_fini(&fx->r);
    hwstub_clear_memory_binding();
    free(fx->mem_owner);
    fx->mem_owner = NULL;
}

static INLINE int is_vm1_model(byte model);

static INLINE word operand(byte mode, byte reg)
{
    return (((word) mode) << 3) | (reg & 07);
}

static INLINE word op_mov(word src, word dst)
{
    return 0010000 | ((src & 077) << 6) | (dst & 077);
}

static INLINE word op_movb(word src, word dst)
{
    return 0110000 | ((src & 077) << 6) | (dst & 077);
}

static INLINE word op_cmp(word src, word dst)
{
    return 0020000 | ((src & 077) << 6) | (dst & 077);
}

static INLINE word op_cmpb(word src, word dst)
{
    return 0120000 | ((src & 077) << 6) | (dst & 077);
}

static INLINE word op_bit(word src, word dst)
{
    return 0030000 | ((src & 077) << 6) | (dst & 077);
}

static INLINE word op_bitb(word src, word dst)
{
    return 0130000 | ((src & 077) << 6) | (dst & 077);
}

static INLINE word op_bic(word src, word dst)
{
    return 0040000 | ((src & 077) << 6) | (dst & 077);
}

static INLINE word op_bicb(word src, word dst)
{
    return 0140000 | ((src & 077) << 6) | (dst & 077);
}

static INLINE word op_bis(word src, word dst)
{
    return 0050000 | ((src & 077) << 6) | (dst & 077);
}

static INLINE word op_bisb(word src, word dst)
{
    return 0150000 | ((src & 077) << 6) | (dst & 077);
}

static INLINE word op_add(word src, word dst)
{
    return 0060000 | ((src & 077) << 6) | (dst & 077);
}

static INLINE word op_sub(word src, word dst)
{
    return 0160000 | ((src & 077) << 6) | (dst & 077);
}

static INLINE word op_xor(byte reg, word dst)
{
    return 0074000 | ((reg & 07) << 6) | (dst & 077);
}

static INLINE word op_jsr(byte reg, word dst)
{
    return 0004000 | ((reg & 07) << 6) | (dst & 077);
}

static INLINE word op_mul(byte reg, word dst)
{
    return 0070000 | ((reg & 07) << 6) | (dst & 077);
}

static INLINE word op_div(byte reg, word dst)
{
    return 0071000 | ((reg & 07) << 6) | (dst & 077);
}

static INLINE word op_ash(byte reg, word dst)
{
    return 0072000 | ((reg & 07) << 6) | (dst & 077);
}

static INLINE word op_ashc(byte reg, word dst)
{
    return 0073000 | ((reg & 07) << 6) | (dst & 077);
}

static INLINE word op_clr(word dst)
{
    return 0005000 | (dst & 077);
}

static INLINE word op_clrb(word dst)
{
    return 0105000 | (dst & 077);
}

static INLINE word op_com(word dst)
{
    return 0005100 | (dst & 077);
}

static INLINE word op_comb(word dst)
{
    return 0105100 | (dst & 077);
}

static INLINE word op_inc(word dst)
{
    return 0005200 | (dst & 077);
}

static INLINE word op_incb(word dst)
{
    return 0105200 | (dst & 077);
}

static INLINE word op_dec(word dst)
{
    return 0005300 | (dst & 077);
}

static INLINE word op_decb(word dst)
{
    return 0105300 | (dst & 077);
}

static INLINE word op_neg(word dst)
{
    return 0005400 | (dst & 077);
}

static INLINE word op_negb(word dst)
{
    return 0105400 | (dst & 077);
}

static INLINE word op_tst(word dst)
{
    return 0005700 | (dst & 077);
}

static INLINE word op_tstb(word dst)
{
    return 0105700 | (dst & 077);
}

static INLINE word op_asr(word dst)
{
    return 0006200 | (dst & 077);
}

static INLINE word op_asrb(word dst)
{
    return 0106200 | (dst & 077);
}

static INLINE word op_asl(word dst)
{
    return 0006300 | (dst & 077);
}

static INLINE word op_aslb(word dst)
{
    return 0106300 | (dst & 077);
}

static INLINE word op_ror(word dst)
{
    return 0006000 | (dst & 077);
}

static INLINE word op_rorb(word dst)
{
    return 0106000 | (dst & 077);
}

static INLINE word op_rol(word dst)
{
    return 0006100 | (dst & 077);
}

static INLINE word op_rolb(word dst)
{
    return 0106100 | (dst & 077);
}

static INLINE word op_adc(word dst)
{
    return 0005500 | (dst & 077);
}

static INLINE word op_adcb(word dst)
{
    return 0105500 | (dst & 077);
}

static INLINE word op_sbc(word dst)
{
    return 0005600 | (dst & 077);
}

static INLINE word op_sbcb(word dst)
{
    return 0105600 | (dst & 077);
}

static INLINE word op_swab(word dst)
{
    return 0000300 | (dst & 077);
}

static INLINE word op_sxt(word dst)
{
    return 0006700 | (dst & 077);
}

static INLINE word op_csm(word dst)
{
    return 0007000 | (dst & 077);
}

static INLINE word op_mfps(word dst)
{
    return 0106700 | (dst & 077);
}

static INLINE word op_mtps(word dst)
{
    return 0106400 | (dst & 077);
}

static INLINE word op_jmp(word dst)
{
    return 0000100 | (dst & 077);
}

static INLINE word op_nop(void)
{
    return 000240;
}

static INLINE word op_halt(void)
{
    return 000000;
}

static INLINE word op_wait(void)
{
    return 000001;
}

static INLINE word op_rti(void)
{
    return 0000002;
}

static INLINE word op_bpt(void)
{
    return 0000003;
}

static INLINE word op_iot(void)
{
    return 0000004;
}

static INLINE word op_reset(void)
{
    return 0000005;
}

static INLINE word op_spl(byte level)
{
    return 0000230 | (level & 07);
}

static INLINE word op_rtt(void)
{
    return 0000006;
}

static INLINE word op_clc(void)
{
    return 000241;
}

static INLINE word op_clv(void)
{
    return 000242;
}

static INLINE word op_clz(void)
{
    return 000244;
}

static INLINE word op_cln(void)
{
    return 000250;
}

static INLINE word op_ccc(void)
{
    return 000257;
}

static INLINE word op_sec(void)
{
    return 000261;
}

static INLINE word op_sev(void)
{
    return 000262;
}

static INLINE word op_sez(void)
{
    return 000264;
}

static INLINE word op_sen(void)
{
    return 000270;
}

static INLINE word op_scc(void)
{
    return 000277;
}

static INLINE word op_br(byte offset)
{
    return 0000400 | (offset & 0377);
}

static INLINE word op_bne(byte offset)
{
    return 0001000 | (offset & 0377);
}

static INLINE word op_beq(byte offset)
{
    return 0001400 | (offset & 0377);
}

static INLINE word op_bpl(byte offset)
{
    return 0100000 | (offset & 0377);
}

static INLINE word op_bmi(byte offset)
{
    return 0100400 | (offset & 0377);
}

static INLINE word op_bvc(byte offset)
{
    return 0102000 | (offset & 0377);
}

static INLINE word op_bvs(byte offset)
{
    return 0102400 | (offset & 0377);
}

static INLINE word op_bcc(byte offset)
{
    return 0103000 | (offset & 0377);
}

static INLINE word op_bcs(byte offset)
{
    return 0103400 | (offset & 0377);
}

static INLINE word op_bge(byte offset)
{
    return 0002000 | (offset & 0377);
}

static INLINE word op_blt(byte offset)
{
    return 0002400 | (offset & 0377);
}

static INLINE word op_bgt(byte offset)
{
    return 0003000 | (offset & 0377);
}

static INLINE word op_ble(byte offset)
{
    return 0003400 | (offset & 0377);
}

static INLINE word op_bhi(byte offset)
{
    return 0101000 | (offset & 0377);
}

static INLINE word op_blos(byte offset)
{
    return 0101400 | (offset & 0377);
}

static INLINE word op_rts(byte reg)
{
    return 000200 | (reg & 07);
}

static INLINE word op_sob(byte reg, byte offset)
{
    return 0077000 | ((reg & 07) << 6) | (offset & 077);
}

static INLINE word op_emt(byte code)
{
    return 0104000 | (code & 0377);
}

static INLINE word op_trap(byte code)
{
    return 0104400 | (code & 0377);
}

static INLINE word op_mfpt(void)
{
    return 0000007;
}

static INLINE word op_mfpd(word src)
{
    return 0006500 | (src & 077);
}

static INLINE word op_mfpi(word src)
{
    return 0106500 | (src & 077);
}

static INLINE word op_mtpi(word dst)
{
    return 0006600 | (dst & 077);
}

static INLINE word op_mtpd(word dst)
{
    return 0106600 | (dst & 077);
}

static INLINE word op_tstset(word dst)
{
    return 0007200 | (dst & 077);
}

static INLINE word op_wrtlck(word dst)
{
    return 0007300 | (dst & 077);
}

static INLINE word op_fis(word sub)
{
    return 0075000 | (sub & 077);
}

static INLINE word op_fp11(word op11_8, byte ac, word dst)
{
    return 0170000 | ((op11_8 & 017) << 8) | ((ac & 03) << 6) | (dst & 077);
}

static int test_irq_pending;
static int test_irq_priority;
static word test_last_vector;
static int test_irq_called;
static int test_irq_pending_pri[8];
static word test_irq_vector_pri[8];
static word test_irq_vector;
static int test_irq_seq_len;
static int test_irq_seq_idx;
static word test_irq_seq_vector[8];
static int test_irq_seq_priority[8];

static INLINE int is_vm2_model(byte model);
static INLINE int is_vm1_model(byte model);

static int is_irq_masked(regs *r, word vector, int priority)
{
    if (is_vm1_model(r->model)) {
        if (r->psw & 01000) {
            return 1;
        }
        if (vector == 0160002) {
            if (r->psw & 02000) {
                return 1;
            }
        } else {
            if (r->psw & 0200) {
                return 1;
            }
        }
    } else if (is_vm2_model(r->model)) {
        if (r->psw & 0200) {
            return 1;
        }
    } else {
        int psw_pri = (r->psw >> 5) & 07;
        if (priority && psw_pri >= priority) {
            return 1;
        }
    }
    return 0;
}

static int test_poll_irq(regs *r, word *vector)
{
    if (!test_irq_pending) {
        return 0;
    }
    test_irq_called = 1;
    word v = (word)(000060 | ((test_irq_priority & 07) << 9));
    test_last_vector = v;
    if (is_irq_masked(r, 0, test_irq_priority)) {
        return 0;
    }

    if (vector) {
        *vector = v;
    }
    test_irq_pending = 0;
    return 1;
}

static int test_poll_irq_vector(regs *r, word *vector)
{
    if (!test_irq_pending) {
        return 0;
    }
    if (is_irq_masked(r, test_irq_vector, 4)) {
        return 0;
    }

    test_irq_pending = 0;
    if (vector) {
        *vector = test_irq_vector;
        test_last_vector = *vector;
    }
    return 1;
}

static int test_poll_irq_highest(regs *r, word *vector)
{
    int best = -1;
    for (int pri = 7; pri >= 0; pri--) {
        if (test_irq_pending_pri[pri]) {
            if (!is_irq_masked(r, test_irq_vector_pri[pri], pri)) {
                best = pri;
                break;
            }
        }
    }
    if (best >= 0) {
        test_irq_pending_pri[best] = 0;
        if (vector) {
            if (is_vm1_model(r->model) || is_vm2_model(r->model)) {
                *vector = (word)(test_irq_vector_pri[best] & 0177776);
            } else {
                *vector = (word)((test_irq_vector_pri[best] & 0777) | ((best & 07) << 9));
            }
            test_last_vector = *vector;
        }
        test_irq_called = 1;
        return 1;
    }
    return 0;
}

static int test_poll_irq_sequence(regs *r, word *vector)
{
    int idx;
    int pri;
    word v;

    if (test_irq_seq_idx >= test_irq_seq_len) {
        return 0;
    }
    idx = test_irq_seq_idx;
    pri = test_irq_seq_priority[idx];
    v = test_irq_seq_vector[idx];

    if (is_irq_masked(r, v, pri)) {
        return 0;
    }

    if (vector) {
        if (is_vm1_model(r->model) || is_vm2_model(r->model)) {
            *vector = (word)(v & 0177776);
        } else {
            *vector = (word)((v & 0777) | ((pri & 07) << 9));
        }
        test_last_vector = *vector;
    }
    test_irq_seq_idx++;
    test_irq_called = 1;
    return 1;
}

static INLINE void store_word(cpu_fixture *fx, word addr, word value)
{
    fx->r.store_word(&fx->r, addr, value);
}

static void load_program(cpu_fixture *fx, word addr, const word *program, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        store_word(fx, addr + (i * 2), program[i]);
    }
    fx->r.r[7] = addr;
}

static INLINE int is_flag_set(const regs *r, word flag)
{
    return (r->psw & flag) == flag;
}

static INLINE int is_vm2_model(byte model)
{
    return model == K1801VM2 || model == K1806VM2;
}

static INLINE int is_vm1_model(byte model)
{
    return model == K1801VM1 || model == K1801VM1G;
}

typedef struct {
    byte model;
    const char *name;
} model_case;

static const model_case model_cases[] = {
    { K1801VM1,  "K1801VM1" },
    { K1801VM1G, "K1801VM1G" },
    { K1801VM2,  "K1801VM2" },
    { K1806VM2,  "K1806VM2" },
    { DCJ11,     "DCJ11" },
};

static INLINE void set_test_name(char *buf, size_t len, const char *base, const char *model)
{
    snprintf(buf, len, "%s_%s", base, model);
    current_test = buf;
}

static int run_for_models(int (*fn)(byte model, const char *name))
{
    int rc = 0;
    for (size_t i = 0; i < sizeof(model_cases) / sizeof(model_cases[0]); i++) {
        rc += fn(model_cases[i].model, model_cases[i].name);
    }
    return rc;
}

#define ASSERT_EQ(actual, expected, msg)                         \
    do {                                                         \
        if ((actual) != (expected)) {                            \
            fprintf(stderr, "%s: %s (expected %06o, got %06o)\n",\
                    current_test, msg,                           \
                    (word) (expected), (word) (actual));         \
            rc = 1;                                              \
            goto cleanup;                                        \
        }                                                        \
    } while (0)

#define ASSERT_TRUE(cond, msg)                                   \
    do {                                                         \
        if (!(cond)) {                                           \
            fprintf(stderr, "%s: %s\n", current_test, msg);      \
            rc = 1;                                              \
            goto cleanup;                                        \
        }                                                        \
    } while (0)

#define expect_flags(r, n, z, v, c)                              \
    do {                                                         \
        if ((n) >= 0) {                                          \
            ASSERT_EQ(is_flag_set((r), FLAG_N), (n), "N flag mismatch"); \
        }                                                        \
        if ((z) >= 0) {                                          \
            ASSERT_EQ(is_flag_set((r), FLAG_Z), (z), "Z flag mismatch"); \
        }                                                        \
        if ((v) >= 0) {                                          \
            ASSERT_EQ(is_flag_set((r), FLAG_V), (v), "V flag mismatch"); \
        }                                                        \
        if ((c) >= 0) {                                          \
            ASSERT_EQ(is_flag_set((r), FLAG_C), (c), "C flag mismatch"); \
        }                                                        \
    } while (0)

static INLINE void write_op(cpu_fixture *fx, word op)
{
    store_word(fx, TEST_BASE, op);
    fx->r.r[7] = TEST_BASE;
}

static int test_mov_updates_flags(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word program[] = {
        op_mov(operand(2, 7), operand(0, 0)), /* MOV #value, R0 */
        01234,
    };

    current_test = "mov_basic";
    fixture_setup(&fx);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));
    fx.r.psw = FLAG_C | FLAG_V;

    ASSERT_EQ(core_step(&fx.r), 0, "core_step should succeed");
    ASSERT_EQ(fx.r.r[0], 01234, "MOV result incorrect");
    ASSERT_TRUE(!is_flag_set(&fx.r, FLAG_N), "N should be clear");
    ASSERT_TRUE(!is_flag_set(&fx.r, FLAG_Z), "Z should be clear");
    ASSERT_TRUE(!is_flag_set(&fx.r, FLAG_V), "V should be clear");
    ASSERT_TRUE(is_flag_set(&fx.r, FLAG_C), "C must be preserved");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_movb_sign_extends_register_dest(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word program[] = {
        op_movb(operand(2, 7), operand(0, 1)), /* MOVB #value, R1 */
        0377,
    };

    current_test = "movb_sign_extend";
    fixture_setup(&fx);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    ASSERT_EQ(core_step(&fx.r), 0, "core_step should succeed");
    ASSERT_EQ(fx.r.r[1], 0177777, "MOVB should sign extend into registers");
    ASSERT_TRUE(is_flag_set(&fx.r, FLAG_N), "N should be set");
    ASSERT_TRUE(!is_flag_set(&fx.r, FLAG_Z), "Z should be clear");
    ASSERT_TRUE(!is_flag_set(&fx.r, FLAG_V), "V should be clear");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_add_sets_expected_flags(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word program[] = {
        op_add(operand(2, 7), operand(0, 0)), /* ADD #value, R0 */
        000001,
    };

    current_test = "add_flags";
    fixture_setup(&fx);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));
    fx.r.r[0] = 077777; /* 0x7FFF */

    ASSERT_EQ(core_step(&fx.r), 0, "core_step should succeed");
    ASSERT_EQ(fx.r.r[0], 0100000, "ADD result incorrect");
    ASSERT_TRUE(is_flag_set(&fx.r, FLAG_N), "N should be set");
    ASSERT_TRUE(!is_flag_set(&fx.r, FLAG_Z), "Z should be clear");
    ASSERT_TRUE(is_flag_set(&fx.r, FLAG_V), "V should be set");
    ASSERT_TRUE(!is_flag_set(&fx.r, FLAG_C), "C should be clear");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_subtract_with_borrow(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word program[] = {
        op_sub(operand(2, 7), operand(0, 2)), /* SUB #value, R2 */
        000001,
    };

    current_test = "sub_borrow";
    fixture_setup(&fx);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));
    fx.r.r[2] = 0;

    ASSERT_EQ(core_step(&fx.r), 0, "core_step should succeed");
    ASSERT_EQ(fx.r.r[2], 0177777, "SUB result incorrect");
    ASSERT_TRUE(is_flag_set(&fx.r, FLAG_N), "N should be set");
    ASSERT_TRUE(!is_flag_set(&fx.r, FLAG_Z), "Z should be clear");
    ASSERT_TRUE(!is_flag_set(&fx.r, FLAG_V), "V should be clear");
    ASSERT_TRUE(is_flag_set(&fx.r, FLAG_C), "C should be set when borrow occurs");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_branch_on_equal_skips_instruction(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word program[] = {
        op_clr(operand(0, 0)),               /* CLR R0 -> sets Z */
        op_beq(0002),                        /* skip next word pair */
        op_mov(operand(2, 7), operand(0, 1)),/* MOV #1, R1 */
        000001,
        op_mov(operand(2, 7), operand(0, 1)),/* MOV #2, R1 */
        000002,
    };

    current_test = "beq_skip";
    fixture_setup(&fx);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    ASSERT_EQ(core_step(&fx.r), 0, "CLR should succeed");
    ASSERT_EQ(core_step(&fx.r), 0, "BEQ should succeed");
    ASSERT_EQ(core_step(&fx.r), 0, "MOV should succeed");
    ASSERT_EQ(fx.r.r[1], 000002, "Branch should skip MOV #1");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 12, "PC should point past MOV #2");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_jsr_and_rts_restore_context(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word subroutine_addr = TEST_BASE + 012;
    const word program[] = {
        op_jsr(5, operand(3, 7)),  /* JSR R5,@#SUB */
        subroutine_addr,
        op_mov(operand(2, 7), operand(0, 0)), /* MOV #1, R0 (after return) */
        000001,
        op_nop(),
        op_mov(operand(2, 7), operand(0, 1)), /* SUBR: MOV #1234, R1 */
        01234,
        op_rts(5),
    };

    current_test = "jsr_rts";
    fixture_setup(&fx);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));
    fx.r.r[5] = 050000;

    ASSERT_EQ(core_step(&fx.r), 0, "JSR should succeed");
    ASSERT_EQ(fx.r.r[7], subroutine_addr, "PC did not jump to subroutine");
    ASSERT_EQ(core_step(&fx.r), 0, "Subroutine MOV should succeed");
    ASSERT_EQ(fx.r.r[1], 01234, "Subroutine result incorrect");
    ASSERT_EQ(core_step(&fx.r), 0, "RTS should succeed");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 4, "Return address incorrect");
    ASSERT_EQ(fx.r.r[5], 050000, "R5 should be restored");
    ASSERT_EQ(fx.r.r[6], TEST_STACK, "Stack pointer should be restored");
    ASSERT_EQ(core_step(&fx.r), 0, "Post-return MOV should succeed");
    ASSERT_EQ(fx.r.r[0], 000001, "Mainline MOV result incorrect");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_sob_loops_expected_count(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word program[] = {
        op_inc(operand(0, 1)),          /* INC R1 */
        op_sob(0, 0002),                /* branch back two words */
        op_nop(),
    };

    current_test = "sob_loop";
    fixture_setup(&fx);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));
    fx.r.r[0] = 3;
    fx.r.r[1] = 0;

    ASSERT_EQ(core_step(&fx.r), 0, "INC #1");
    ASSERT_EQ(core_step(&fx.r), 0, "SOB #1");
    ASSERT_EQ(core_step(&fx.r), 0, "INC #2");
    ASSERT_EQ(core_step(&fx.r), 0, "SOB #2");
    ASSERT_EQ(core_step(&fx.r), 0, "INC #3");
    ASSERT_EQ(core_step(&fx.r), 0, "SOB #3");
    ASSERT_EQ(fx.r.r[1], 3, "Loop body should run three times");
    ASSERT_EQ(fx.r.r[0], 0, "Loop counter should reach zero");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 4, "PC should fall through to NOP");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_emt_pushes_and_vectors(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 02000;
    const word new_psw = 000000;
    const word program[] = {
        op_emt(0),
    };

    current_test = "emt_vector";
    fixture_setup(&fx);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 030, handler);
    store_word(&fx, 032, new_psw);
    ASSERT_EQ(fx.r.load_word(&fx.r, 030), handler, "Vector fetch sanity");
    ASSERT_EQ(fx.r.load_word(&fx.r, 032), new_psw, "Vector PSW sanity");
    fx.r.r[6] = 01000;
    fx.r.psw = 000003;

    ASSERT_EQ(core_step(&fx.r), 0, "EMT should succeed");
    ASSERT_EQ(fx.r.r[7], handler, "PC should load EMT vector");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should load EMT vector");
    ASSERT_EQ(fx.r.r[6], 00774, "SP should push two words");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE + 2, "Stack PC incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 000003, "Stack PSW incorrect");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_trap_pushes_and_vectors(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 03000;
    const word new_psw = 000000;
    const word program[] = {
        op_trap(0),
    };

    current_test = "trap_vector";
    fixture_setup(&fx);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 034, handler);
    store_word(&fx, 036, new_psw);
    fx.r.r[6] = 01000;
    fx.r.psw = 000003;

    ASSERT_EQ(core_step(&fx.r), 0, "TRAP should succeed");
    ASSERT_EQ(fx.r.r[7], handler, "PC should load TRAP vector");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should load TRAP vector");
    ASSERT_EQ(fx.r.r[6], 00774, "SP should push two words");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE + 2, "Stack PC incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 000003, "Stack PSW incorrect");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_vm1_trap_vectors_ignore_code(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler_emt = 02400;
    const word handler_trap = 02600;
    const word new_psw_emt = 000120;
    const word new_psw_trap = 000140;
    const byte emt_code = 000012;
    const byte trap_code = 000003;
    const word emt_vec = 000030;
    const word trap_vec = 000034;
    const word emt_program[] = { op_emt(emt_code) };
    const word trap_program[] = { op_trap(trap_code) };

    current_test = "vm1_emt_vector_ignore_code";
    fixture_setup_model(&fx, K1801VM1);
    load_program(&fx, TEST_BASE, emt_program, sizeof(emt_program) / sizeof(emt_program[0]));
    store_word(&fx, emt_vec, handler_emt);
    store_word(&fx, emt_vec + 2, new_psw_emt);
    fx.r.r[6] = 01000;
    fx.r.psw = 000003;
    ASSERT_EQ(core_step(&fx.r), 0, "EMT should vector with code");
    ASSERT_EQ(fx.r.r[7], handler_emt, "EMT PC should load vector");
    ASSERT_EQ(fx.r.psw, new_psw_emt, "EMT PSW should load vector");
    ASSERT_EQ(fx.r.r[6], 00774, "EMT SP should push two words");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE + 2, "EMT stack PC incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 000003, "EMT stack PSW incorrect");
    fixture_teardown(&fx);

    current_test = "vm1_trap_vector_ignore_code";
    fixture_setup_model(&fx, K1801VM1);
    load_program(&fx, TEST_BASE, trap_program, sizeof(trap_program) / sizeof(trap_program[0]));
    store_word(&fx, trap_vec, handler_trap);
    store_word(&fx, trap_vec + 2, new_psw_trap);
    fx.r.r[6] = 01000;
    fx.r.psw = 000003;
    ASSERT_EQ(core_step(&fx.r), 0, "TRAP should vector with code");
    ASSERT_EQ(fx.r.r[7], handler_trap, "TRAP PC should load vector");
    ASSERT_EQ(fx.r.psw, new_psw_trap, "TRAP PSW should load vector");
    ASSERT_EQ(fx.r.r[6], 00774, "TRAP SP should push two words");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE + 2, "TRAP stack PC incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 000003, "TRAP stack PSW incorrect");
    fixture_teardown(&fx);

cleanup:
    return rc;
}

static int test_emt_trap_ignore_code_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word handler_emt = 02400;
    const word handler_trap = 02600;
    const word new_psw_emt = 000120;
    const word new_psw_trap = 000140;
    const byte emt_code = 000012;
    const byte trap_code = 000003;
    const word emt_vec = 000030;
    const word trap_vec = 000034;
    const word emt_program[] = { op_emt(emt_code) };
    const word trap_program[] = { op_trap(trap_code) };

    set_test_name(namebuf, sizeof(namebuf), "emt_ignore_code", name);
    fixture_setup_model(&fx, model);
    load_program(&fx, TEST_BASE, emt_program, sizeof(emt_program) / sizeof(emt_program[0]));
    store_word(&fx, emt_vec, handler_emt);
    store_word(&fx, emt_vec + 2, new_psw_emt);
    fx.r.r[6] = 01000;
    fx.r.psw = 000003;
    ASSERT_EQ(core_step(&fx.r), 0, "EMT should vector to 000030");
    ASSERT_EQ(fx.r.r[7], handler_emt, "EMT PC should load vector");
    ASSERT_EQ(fx.r.psw, new_psw_emt, "EMT PSW should load vector");
    ASSERT_EQ(fx.r.r[6], 00774, "EMT SP should push two words");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE + 2, "EMT stack PC incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 000003, "EMT stack PSW incorrect");
    fixture_teardown(&fx);

    set_test_name(namebuf, sizeof(namebuf), "trap_ignore_code", name);
    fixture_setup_model(&fx, model);
    load_program(&fx, TEST_BASE, trap_program, sizeof(trap_program) / sizeof(trap_program[0]));
    store_word(&fx, trap_vec, handler_trap);
    store_word(&fx, trap_vec + 2, new_psw_trap);
    fx.r.r[6] = 01000;
    fx.r.psw = 000003;
    ASSERT_EQ(core_step(&fx.r), 0, "TRAP should vector to 000034");
    ASSERT_EQ(fx.r.r[7], handler_trap, "TRAP PC should load vector");
    ASSERT_EQ(fx.r.psw, new_psw_trap, "TRAP PSW should load vector");
    ASSERT_EQ(fx.r.r[6], 00774, "TRAP SP should push two words");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE + 2, "TRAP stack PC incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 000003, "TRAP stack PSW incorrect");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int run_illegal_op_test(word op, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 05000;
    const word new_psw = 000000;
    const word program[] = {
        op,
    };

    current_test = name;
    fixture_setup(&fx);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 010, handler);
    store_word(&fx, 012, new_psw);
    fx.r.r[6] = 01000;
    fx.r.psw = 000003;

    ASSERT_EQ(core_step(&fx.r), 0, "Illegal instruction should trap");
    ASSERT_EQ(fx.r.r[7], handler, "PC should load illegal vector");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should load illegal vector");
    ASSERT_EQ(fx.r.r[6], 00774, "SP should push two words");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE + 2, "Stack PC incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 000003, "Stack PSW incorrect");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int run_illegal_op_test_model(word op, const char *name, byte model)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 05000;
    const word new_psw = 000340;
    const word program[] = {
        op,
    };

    current_test = name;
    fixture_setup_model(&fx, model);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 010, handler);
    store_word(&fx, 012, new_psw);
    fx.r.r[6] = 01000;
    fx.r.psw = 000003;

    ASSERT_EQ(core_step(&fx.r), 0, "Illegal instruction should trap");
    ASSERT_EQ(fx.r.r[7], handler, "PC should load illegal vector");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should load illegal vector");
    ASSERT_EQ(fx.r.r[6], 00774, "SP should push two words");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE + 2, "Stack PC incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 000003, "Stack PSW incorrect");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_vm1_illegal_instructions_trap(void)
{
    int rc = 0;

    rc += run_illegal_op_test(op_mul(0, operand(0, 1)), "illegal_mul_vm1");
    rc += run_illegal_op_test(op_div(0, operand(0, 1)), "illegal_div_vm1");
    rc += run_illegal_op_test(op_ash(0, operand(0, 1)), "illegal_ash_vm1");
    rc += run_illegal_op_test(op_ashc(0, operand(0, 1)), "illegal_ashc_vm1");

    return rc;
}

static int test_dcj11_special_ops_illegal_on_other_models(void)
{
    int rc = 0;
    const byte models[] = { K1801VM1, K1801VM1G, K1801VM2, K1806VM2 };
    const char *names[] = { "K1801VM1", "K1801VM1G", "K1801VM2", "K1806VM2" };
    char namebuf[64];

    for (size_t i = 0; i < sizeof(models) / sizeof(models[0]); i++) {
        snprintf(namebuf, sizeof(namebuf), "mfpt_illegal_%s", names[i]);
        rc += run_illegal_op_test_model(op_mfpt(), namebuf, models[i]);

        if (models[i] == K1801VM1 || models[i] == K1801VM1G) {
            snprintf(namebuf, sizeof(namebuf), "mfpd_illegal_%s", names[i]);
            rc += run_illegal_op_test_model(op_mfpd(operand(0, 0)), namebuf, models[i]);

            snprintf(namebuf, sizeof(namebuf), "mfpi_illegal_%s", names[i]);
            rc += run_illegal_op_test_model(op_mfpi(operand(0, 0)), namebuf, models[i]);

            snprintf(namebuf, sizeof(namebuf), "mtpi_illegal_%s", names[i]);
            rc += run_illegal_op_test_model(op_mtpi(operand(0, 0)), namebuf, models[i]);

            snprintf(namebuf, sizeof(namebuf), "mtpd_illegal_%s", names[i]);
            rc += run_illegal_op_test_model(op_mtpd(operand(0, 0)), namebuf, models[i]);
        }

        snprintf(namebuf, sizeof(namebuf), "tstset_illegal_%s", names[i]);
        rc += run_illegal_op_test_model(op_tstset(operand(0, 1)), namebuf, models[i]);

        snprintf(namebuf, sizeof(namebuf), "wrtlck_illegal_%s", names[i]);
        rc += run_illegal_op_test_model(op_wrtlck(operand(0, 1)), namebuf, models[i]);

        snprintf(namebuf, sizeof(namebuf), "csm_illegal_%s", names[i]);
        rc += run_illegal_op_test_model(op_csm(operand(0, 0)), namebuf, models[i]);
    }

    return rc;
}

static int test_dcj11_tstset_wrtlck_mode0_illegal(void)
{
    int rc = 0;

    rc += run_illegal_op_test_model(op_tstset(operand(0, 0)), "tstset_mode0_dcj11", DCJ11);
    rc += run_illegal_op_test_model(op_wrtlck(operand(0, 0)), "wrtlck_mode0_dcj11", DCJ11);

    return rc;
}

static int test_dcj11_alignment_trap(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 06000;
    const word new_psw = 000340;
    const word program[] = {
        op_mov(operand(1, 1), operand(0, 0)),
    };

    current_test = "dcj11_alignment_trap";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000004, handler);
    store_word(&fx, 000006, new_psw);
    fx.r.r[1] = 000001;
    fx.r.r[6] = 01000;
    fx.r.psw = 000003;

    ASSERT_EQ(core_step(&fx.r), 0, "Odd word access should trap");
    ASSERT_EQ(fx.r.r[7], handler, "PC should load bus error vector");
    ASSERT_EQ(fx.r.psw & 0177760, new_psw & 0177760, "PSW should load bus error vector");
    ASSERT_EQ(fx.r.r[6], 00774, "SP should push two words");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE + 2, "Stack PC incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 000003, "Stack PSW incorrect");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_alignment_trap_store(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 06000;
    const word new_psw = 000340;
    const word program[] = {
        op_mov(operand(0, 0), operand(1, 1)),
    };

    current_test = "dcj11_alignment_trap_store";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000004, handler);
    store_word(&fx, 000006, new_psw);
    fx.r.r[0] = 01234;
    fx.r.r[1] = 000001;
    fx.r.r[6] = 01000;
    fx.r.psw = 000003;

    ASSERT_EQ(core_step(&fx.r), 0, "Odd word store should trap");
    ASSERT_EQ(fx.r.r[7], handler, "PC should load bus error vector");
    ASSERT_EQ(fx.r.psw & 0177760, new_psw & 0177760, "PSW should load bus error vector");
    ASSERT_EQ(fx.r.r[6], 00774, "SP should push two words");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE + 2, "Stack PC incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 000003, "Stack PSW incorrect");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_alignment_trap_fetch(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 06000;
    const word new_psw = 000340;

    current_test = "dcj11_alignment_trap_fetch";
    fixture_setup_model(&fx, DCJ11);

    store_word(&fx, 000004, handler);
    store_word(&fx, 000006, new_psw);
    fx.r.r[7] = TEST_BASE + 1;
    fx.r.r[6] = 01000;
    fx.r.psw = 000003;

    ASSERT_EQ(core_step(&fx.r), 0, "Odd instruction fetch should trap");
    ASSERT_EQ(fx.r.r[7], handler, "PC should load bus error vector");
    ASSERT_EQ(fx.r.psw & 0177760, new_psw & 0177760, "PSW should load bus error vector");
    ASSERT_EQ(fx.r.r[6], 00774, "SP should push two words");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE + 1, "Stack PC incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 000003, "Stack PSW incorrect");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_vm_ifetch_bus_error_increments_pc_model(byte model,
        const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word handler = 06000;
    const word new_psw = 000340;

    set_test_name(namebuf, sizeof(namebuf), "ifetch_bus_pc_inc", name);
    fixture_setup_model(&fx, model);
    current_test = namebuf;

    store_word(&fx, 000004, handler);
    store_word(&fx, 000006, new_psw);
    fx.r.load_word = test_dispatch_load_word_fetch_fault;
    fx.r.ram_fast = NULL;
    fx.r.r[7] = TEST_BASE;
    fx.r.r[6] = 01000;
    fx.r.psw = 000003;
    test_buserr_fetch_enable = 1;
    test_buserr_fetch_addr = TEST_BASE;

    ASSERT_EQ(core_step(&fx.r), 0, "Instruction fetch bus error should trap");
    ASSERT_EQ(fx.r.r[7], handler, "PC should load bus error vector");
    ASSERT_EQ(fx.r.psw & 0177760, new_psw & 0177760,
              "PSW should load bus error vector");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE + 2,
              "VM* bus-error fetch should stack incremented PC");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 000003, "Stack PSW incorrect");

cleanup:
    test_buserr_fetch_enable = 0;
    test_buserr_fetch_addr = 0;
    test_buserr_fetch_guard = 0;
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_ifetch_bus_error_preserves_pc(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 06000;
    const word new_psw = 000340;

    current_test = "dcj11_ifetch_bus_pc_preserve";
    fixture_setup_model(&fx, DCJ11);

    store_word(&fx, 000004, handler);
    store_word(&fx, 000006, new_psw);
    fx.r.load_word = test_dispatch_load_word_fetch_fault;
    fx.r.ram_fast = NULL;
    fx.r.r[7] = TEST_BASE;
    fx.r.r[6] = 01000;
    fx.r.psw = 000003;
    test_buserr_fetch_enable = 1;
    test_buserr_fetch_addr = TEST_BASE;

    ASSERT_EQ(core_step(&fx.r), 0, "Instruction fetch bus error should trap");
    ASSERT_EQ(fx.r.r[7], handler, "PC should load bus error vector");
    ASSERT_EQ(fx.r.psw & 0177760, new_psw & 0177760,
              "PSW should load bus error vector");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE,
              "DCJ11 bus-error fetch should stack non-incremented PC");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 000003, "Stack PSW incorrect");

cleanup:
    test_buserr_fetch_enable = 0;
    test_buserr_fetch_addr = 0;
    test_buserr_fetch_guard = 0;
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_ifetch_internal_mmu_reg_traps_addr(word fetch_addr,
        const char *tag)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word handler = 06000;
    const word new_psw = 000340;

    set_test_name(namebuf, sizeof(namebuf), "dcj11_ifetch_mmu_reg", tag);
    current_test = namebuf;
    fixture_setup_model(&fx, DCJ11);

    store_word(&fx, 000004, handler);
    store_word(&fx, 000006, new_psw);
    fx.r.r[7] = fetch_addr;
    fx.r.r[6] = 01000;
    fx.r.psw = 000003;
    fx.r.J11_CPUERR = 0;

    ASSERT_EQ(core_step(&fx.r), 0, "Internal MMU-reg ifetch should trap");
    ASSERT_EQ(fx.r.r[7], handler, "PC should load bus error vector");
    ASSERT_EQ(fx.r.psw & 0177760, new_psw & 0177760,
              "PSW should load bus error vector");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), fetch_addr,
              "DCJ11 internal-reg ifetch should stack non-incremented PC");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 000003, "Stack PSW incorrect");
    ASSERT_EQ(fx.r.J11_CPUERR & 0000100, 0000100, "CPUERR.ADR should be set");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_vm_sp_bus_error_uses_vector4_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word handler = 06200;
    const word new_psw = 000340;
    const word stack_base = 01200;
    const word program[] = {
        op_mov(operand(1, 6), operand(0, 0)), /* MOV (SP), R0 */
    };

    set_test_name(namebuf, sizeof(namebuf), "sp_buserr_vec4", name);
    fixture_setup_model(&fx, model);
    current_test = namebuf;
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000004, handler);
    store_word(&fx, 000006, new_psw);
    fx.r.load_word = test_dispatch_load_word_fetch_fault;
    fx.r.ram_fast = NULL;
    fx.r.r[7] = TEST_BASE;
    fx.r.r[6] = stack_base;
    fx.r.psw = 000003;
    test_buserr_fetch_enable = 1;
    test_buserr_fetch_addr = stack_base;

    ASSERT_EQ(core_step(&fx.r), 0, "(SP) data bus error should trap");
    ASSERT_EQ(fx.r.r[7], handler, "VM* should use vector-4 path, not HALT path");
    ASSERT_EQ(fx.r.psw & 0177760, new_psw & 0177760,
              "PSW should load bus-error vector");
    ASSERT_EQ(fx.r.r[6], stack_base - 4, "SP should include trap frame push");
    ASSERT_EQ(fx.r.load_word(&fx.r, stack_base - 4), TEST_BASE + 2,
              "Stack PC should point past the faulting instruction");
    ASSERT_EQ(fx.r.load_word(&fx.r, stack_base - 2), 000003, "Stack PSW incorrect");

cleanup:
    test_buserr_fetch_enable = 0;
    test_buserr_fetch_addr = 0;
    test_buserr_fetch_guard = 0;
    fixture_teardown(&fx);
    return rc;
}

static int test_extended_ops_supported_models(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    word program[2];
    int active = 0;
    char namebuf[64];

    set_test_name(namebuf, sizeof(namebuf), "mul_supported", name);
    fixture_setup_model(&fx, model);
    active = 1;
    program[0] = op_mul(0, operand(0, 1));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 2;
    fx.r.r[1] = 3;
    fx.r.psw = 0;
    ASSERT_EQ(core_step(&fx.r), 0, "MUL should execute on VM1G");
    ASSERT_EQ(fx.r.r[0], 0, "MUL high word");
    ASSERT_EQ(fx.r.r[1], 6, "MUL low word");
    expect_flags(&fx.r, 0, 0, 0, 0);
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after MUL");
    fixture_teardown(&fx);
    active = 0;

    set_test_name(namebuf, sizeof(namebuf), "mul_c_boundary", name);
    fixture_setup_model(&fx, model);
    active = 1;
    program[0] = op_mul(0, operand(0, 1));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 1;
    fx.r.r[1] = 077777;
    fx.r.psw = 0;
    ASSERT_EQ(core_step(&fx.r), 0, "MUL C boundary should execute");
    ASSERT_EQ(fx.r.r[0], 0, "MUL C boundary high word");
    ASSERT_EQ(fx.r.r[1], 077777, "MUL C boundary low word");
    expect_flags(&fx.r, 0, 0, 0, 0);
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after MUL C boundary");
    fixture_teardown(&fx);
    active = 0;

    set_test_name(namebuf, sizeof(namebuf), "mul_negative", name);
    fixture_setup_model(&fx, model);
    active = 1;
    program[0] = op_mul(0, operand(0, 1));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 0177777;
    fx.r.r[1] = 2;
    fx.r.psw = 0;
    ASSERT_EQ(core_step(&fx.r), 0, "MUL negative should execute");
    ASSERT_EQ(fx.r.r[0], 0177777, "MUL negative high word");
    ASSERT_EQ(fx.r.r[1], 0177776, "MUL negative low word");
    expect_flags(&fx.r, 1, 0, 0, 0);
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after MUL negative");
    fixture_teardown(&fx);
    active = 0;

    set_test_name(namebuf, sizeof(namebuf), "div_supported", name);
    fixture_setup_model(&fx, model);
    active = 1;
    program[0] = op_div(0, operand(0, 2));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 0;
    fx.r.r[1] = 6;
    fx.r.r[2] = 3;
    fx.r.psw = 0;
    ASSERT_EQ(core_step(&fx.r), 0, "DIV should execute on VM1G");
    ASSERT_EQ(fx.r.r[0], 2, "DIV quotient");
    ASSERT_EQ(fx.r.r[1], 0, "DIV remainder");
    expect_flags(&fx.r, 0, 0, 0, 0);
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after DIV");
    fixture_teardown(&fx);
    active = 0;

    set_test_name(namebuf, sizeof(namebuf), "div_remainder", name);
    fixture_setup_model(&fx, model);
    active = 1;
    program[0] = op_div(0, operand(0, 2));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 0;
    fx.r.r[1] = 7;
    fx.r.r[2] = 3;
    fx.r.psw = 0;
    ASSERT_EQ(core_step(&fx.r), 0, "DIV remainder should execute");
    ASSERT_EQ(fx.r.r[0], 2, "DIV remainder quotient");
    ASSERT_EQ(fx.r.r[1], 1, "DIV remainder remainder");
    expect_flags(&fx.r, 0, 0, 0, 0);
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after DIV remainder");
    fixture_teardown(&fx);
    active = 0;

    set_test_name(namebuf, sizeof(namebuf), "div_by_zero", name);
    fixture_setup_model(&fx, model);
    active = 1;
    program[0] = op_div(0, operand(0, 2));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 0;
    fx.r.r[1] = 6;
    fx.r.r[2] = 0;
    fx.r.psw = 0;
    ASSERT_EQ(core_step(&fx.r), 0, "DIV by zero should execute");
    expect_flags(&fx.r, 0, 1, 1, 1);
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after DIV by zero");
    fixture_teardown(&fx);
    active = 0;

    set_test_name(namebuf, sizeof(namebuf), "div_overflow", name);
    fixture_setup_model(&fx, model);
    active = 1;
    program[0] = op_div(0, operand(0, 2));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 0;
    fx.r.r[1] = 0100000;
    fx.r.r[2] = 1;
    fx.r.psw = 0;
    ASSERT_EQ(core_step(&fx.r), 0, "DIV overflow should execute");
    ASSERT_EQ(fx.r.r[0], 0, "DIV overflow should not update quotient");
    ASSERT_EQ(fx.r.r[1], 0100000, "DIV overflow should not update remainder");
    expect_flags(&fx.r, 0, 0, 1, 0);
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after DIV overflow");
    fixture_teardown(&fx);
    active = 0;

    set_test_name(namebuf, sizeof(namebuf), "div_minint_by_neg1", name);
    fixture_setup_model(&fx, model);
    active = 1;
    program[0] = op_div(0, operand(0, 2));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 0100000;
    fx.r.r[1] = 0;
    fx.r.r[2] = 0177777;
    fx.r.psw = 0;
    ASSERT_EQ(core_step(&fx.r), 0, "DIV minint/-1 should execute");
    ASSERT_EQ(fx.r.r[0], 0100000, "DIV minint/-1 should not update quotient");
    ASSERT_EQ(fx.r.r[1], 0, "DIV minint/-1 should not update remainder");
    expect_flags(&fx.r, 0, 0, 1, 0);
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after DIV minint/-1");
    fixture_teardown(&fx);
    active = 0;

    set_test_name(namebuf, sizeof(namebuf), "ash_supported", name);
    fixture_setup_model(&fx, model);
    active = 1;
    program[0] = op_ash(0, operand(0, 1));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 1;
    fx.r.r[1] = 1;
    fx.r.psw = 0;
    ASSERT_EQ(core_step(&fx.r), 0, "ASH should execute on VM1G");
    ASSERT_EQ(fx.r.r[0], 2, "ASH result");
    expect_flags(&fx.r, 0, 0, 0, 0);
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after ASH");
    fixture_teardown(&fx);
    active = 0;

    set_test_name(namebuf, sizeof(namebuf), "ash_zero", name);
    fixture_setup_model(&fx, model);
    active = 1;
    program[0] = op_ash(0, operand(0, 1));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 01234;
    fx.r.r[1] = 0;
    fx.r.psw = FLAG_V;
    ASSERT_EQ(core_step(&fx.r), 0, "ASH zero should execute");
    ASSERT_EQ(fx.r.r[0], 01234, "ASH zero should not change value");
    expect_flags(&fx.r, 0, 0, 0, -1);
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after ASH zero");
    fixture_teardown(&fx);
    active = 0;

    set_test_name(namebuf, sizeof(namebuf), "ash_large", name);
    fixture_setup_model(&fx, model);
    active = 1;
    program[0] = op_ash(0, operand(0, 1));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 1;
    fx.r.r[1] = 0000041;
    fx.r.psw = 0;
    ASSERT_EQ(core_step(&fx.r), 0, "ASH large should execute");
    ASSERT_EQ(fx.r.r[0], 0, "ASH large should shift right");
    expect_flags(&fx.r, 0, 1, 0, 0);
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after ASH large");
    fixture_teardown(&fx);
    active = 0;

    set_test_name(namebuf, sizeof(namebuf), "ash_v_flag", name);
    fixture_setup_model(&fx, model);
    active = 1;
    program[0] = op_ash(0, operand(0, 1));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 0040000;
    fx.r.r[1] = 1;
    fx.r.psw = 0;
    ASSERT_EQ(core_step(&fx.r), 0, "ASH should execute");
    ASSERT_EQ(fx.r.r[0], 0100000, "ASH should shift into sign");
    expect_flags(&fx.r, 1, 0, 1, 0);
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after ASH");
    fixture_teardown(&fx);
    active = 0;

    set_test_name(namebuf, sizeof(namebuf), "ash_v_nosignchange", name);
    fixture_setup_model(&fx, model);
    active = 1;
    program[0] = op_ash(0, operand(0, 1));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 0120000;
    fx.r.r[1] = 2;
    fx.r.psw = 0;
    ASSERT_EQ(core_step(&fx.r), 0, "ASH no-sign-change overflow should execute");
    ASSERT_EQ(fx.r.r[0], 0100000, "ASH no-sign-change overflow result");
    expect_flags(&fx.r, 1, 0, 1, 0);
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after ASH no-sign-change overflow");
    fixture_teardown(&fx);
    active = 0;

    set_test_name(namebuf, sizeof(namebuf), "ash_right", name);
    fixture_setup_model(&fx, model);
    active = 1;
    program[0] = op_ash(0, operand(0, 1));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 0002;
    fx.r.r[1] = 0100077;
    fx.r.psw = 0;
    ASSERT_EQ(core_step(&fx.r), 0, "ASH right should execute");
    ASSERT_EQ(fx.r.r[0], 1, "ASH right result");
    expect_flags(&fx.r, 0, 0, 0, 0);
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after ASH right");
    fixture_teardown(&fx);
    active = 0;

    set_test_name(namebuf, sizeof(namebuf), "ash_right_carry", name);
    fixture_setup_model(&fx, model);
    active = 1;
    program[0] = op_ash(0, operand(0, 1));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 0003;
    fx.r.r[1] = 0100076;
    fx.r.psw = 0;
    ASSERT_EQ(core_step(&fx.r), 0, "ASH right should execute");
    ASSERT_EQ(fx.r.r[0], 0, "ASH right carry result");
    expect_flags(&fx.r, 0, 1, 0, 1);
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after ASH right carry");
    fixture_teardown(&fx);
    active = 0;

    set_test_name(namebuf, sizeof(namebuf), "ashc_supported", name);
    fixture_setup_model(&fx, model);
    active = 1;
    program[0] = op_ashc(0, operand(0, 2));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 0;
    fx.r.r[1] = 1;
    fx.r.r[2] = 1;
    fx.r.psw = 0;
    ASSERT_EQ(core_step(&fx.r), 0, "ASHC should execute on VM1G");
    ASSERT_EQ(fx.r.r[0], 0, "ASHC high word");
    ASSERT_EQ(fx.r.r[1], 2, "ASHC low word");
    expect_flags(&fx.r, 0, 0, 0, 0);
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after ASHC");
    fixture_teardown(&fx);
    active = 0;

    set_test_name(namebuf, sizeof(namebuf), "ashc_zero", name);
    fixture_setup_model(&fx, model);
    active = 1;
    program[0] = op_ashc(0, operand(0, 2));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 01234;
    fx.r.r[1] = 05670;
    fx.r.r[2] = 0;
    fx.r.psw = FLAG_V;
    ASSERT_EQ(core_step(&fx.r), 0, "ASHC zero should execute");
    ASSERT_EQ(fx.r.r[0], 01234, "ASHC zero high");
    ASSERT_EQ(fx.r.r[1], 05670, "ASHC zero low");
    expect_flags(&fx.r, 0, 0, 0, -1);
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after ASHC zero");
    fixture_teardown(&fx);
    active = 0;

    set_test_name(namebuf, sizeof(namebuf), "ashc_large", name);
    fixture_setup_model(&fx, model);
    active = 1;
    program[0] = op_ashc(0, operand(0, 2));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 0;
    fx.r.r[1] = 1;
    fx.r.r[2] = 0000041;
    fx.r.psw = 0;
    ASSERT_EQ(core_step(&fx.r), 0, "ASHC large should execute");
    ASSERT_EQ(fx.r.r[0], 0, "ASHC large high");
    ASSERT_EQ(fx.r.r[1], 0, "ASHC large low");
    expect_flags(&fx.r, 0, 1, 0, 0);
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after ASHC large");
    fixture_teardown(&fx);
    active = 0;

    set_test_name(namebuf, sizeof(namebuf), "ashc_v_flag", name);
    fixture_setup_model(&fx, model);
    active = 1;
    program[0] = op_ashc(0, operand(0, 2));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 0040000;
    fx.r.r[1] = 0;
    fx.r.r[2] = 1;
    fx.r.psw = 0;
    ASSERT_EQ(core_step(&fx.r), 0, "ASHC should execute");
    ASSERT_EQ(fx.r.r[0], 0100000, "ASHC should shift into sign");
    ASSERT_EQ(fx.r.r[1], 0, "ASHC low word");
    expect_flags(&fx.r, 1, 0, 1, 0);
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after ASHC");
    fixture_teardown(&fx);
    active = 0;

    set_test_name(namebuf, sizeof(namebuf), "ashc_v_nosignchange", name);
    fixture_setup_model(&fx, model);
    active = 1;
    program[0] = op_ashc(0, operand(0, 2));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 0120000;
    fx.r.r[1] = 0;
    fx.r.r[2] = 2;
    fx.r.psw = 0;
    ASSERT_EQ(core_step(&fx.r), 0, "ASHC no-sign-change overflow should execute");
    ASSERT_EQ(fx.r.r[0], 0100000, "ASHC no-sign-change high word");
    ASSERT_EQ(fx.r.r[1], 0, "ASHC no-sign-change low word");
    expect_flags(&fx.r, 1, 0, 1, 0);
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after ASHC no-sign-change overflow");
    fixture_teardown(&fx);
    active = 0;

    set_test_name(namebuf, sizeof(namebuf), "ashc_right", name);
    fixture_setup_model(&fx, model);
    active = 1;
    program[0] = op_ashc(0, operand(0, 2));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 0;
    fx.r.r[1] = 2;
    fx.r.r[2] = 0100077;
    fx.r.psw = 0;
    ASSERT_EQ(core_step(&fx.r), 0, "ASHC right should execute");
    ASSERT_EQ(fx.r.r[0], 0, "ASHC right high word");
    ASSERT_EQ(fx.r.r[1], 1, "ASHC right low word");
    expect_flags(&fx.r, 0, 0, 0, 0);
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after ASHC right");
    fixture_teardown(&fx);
    active = 0;

    set_test_name(namebuf, sizeof(namebuf), "ashc_right_carry", name);
    fixture_setup_model(&fx, model);
    active = 1;
    program[0] = op_ashc(0, operand(0, 2));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 0;
    fx.r.r[1] = 0003;
    fx.r.r[2] = 0100076;
    fx.r.psw = 0;
    ASSERT_EQ(core_step(&fx.r), 0, "ASHC right should execute");
    ASSERT_EQ(fx.r.r[0], 0, "ASHC right carry high");
    ASSERT_EQ(fx.r.r[1], 0, "ASHC right carry low");
    expect_flags(&fx.r, 0, 1, 0, 1);
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after ASHC right carry");
    fixture_teardown(&fx);
    active = 0;

    set_test_name(namebuf, sizeof(namebuf), "mul_overflow", name);
    fixture_setup_model(&fx, model);
    active = 1;
    program[0] = op_mul(0, operand(0, 1));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 077777;
    fx.r.r[1] = 2;
    fx.r.psw = 0;
    ASSERT_EQ(core_step(&fx.r), 0, "MUL overflow should execute");
    ASSERT_EQ(fx.r.r[0], 0, "MUL overflow high word");
    ASSERT_EQ(fx.r.r[1], 0177776, "MUL overflow low word");
    expect_flags(&fx.r, 0, 0, 0, 1);
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after MUL overflow");
    fixture_teardown(&fx);
    active = 0;

    set_test_name(namebuf, sizeof(namebuf), "div_negative", name);
    fixture_setup_model(&fx, model);
    active = 1;
    program[0] = op_div(0, operand(0, 2));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 0177777;
    fx.r.r[1] = 0177776;
    fx.r.r[2] = 2;
    fx.r.psw = 0;
    ASSERT_EQ(core_step(&fx.r), 0, "DIV negative should execute");
    ASSERT_EQ(fx.r.r[0], 0177777, "DIV negative quotient");
    ASSERT_EQ(fx.r.r[1], 0, "DIV negative remainder");
    expect_flags(&fx.r, 1, 0, 0, 0);
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after DIV negative");
    fixture_teardown(&fx);
    active = 0;

    set_test_name(namebuf, sizeof(namebuf), "div_negative_divisor", name);
    fixture_setup_model(&fx, model);
    active = 1;
    program[0] = op_div(0, operand(0, 2));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 0;
    fx.r.r[1] = 7;
    fx.r.r[2] = 0177777;
    fx.r.psw = 0;
    ASSERT_EQ(core_step(&fx.r), 0, "DIV negative divisor should execute");
    ASSERT_EQ(fx.r.r[0], 0177771, "DIV negative divisor quotient");
    ASSERT_EQ(fx.r.r[1], 0, "DIV negative divisor remainder");
    expect_flags(&fx.r, 1, 0, 0, 0);
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after DIV negative divisor");
    fixture_teardown(&fx);
    active = 0;

    set_test_name(namebuf, sizeof(namebuf), "mul_max_positive", name);
    fixture_setup_model(&fx, model);
    active = 1;
    program[0] = op_mul(0, operand(0, 1));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 077777;
    fx.r.r[1] = 077777;
    fx.r.psw = 0;
    ASSERT_EQ(core_step(&fx.r), 0, "MUL max should execute");
    ASSERT_EQ(fx.r.r[0], 037777, "MUL max high word");
    ASSERT_EQ(fx.r.r[1], 0000001, "MUL max low word");
    expect_flags(&fx.r, 0, 0, 0, 1);
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after MUL max");
    fixture_teardown(&fx);
    active = 0;

    set_test_name(namebuf, sizeof(namebuf), "ash_neg_right", name);
    fixture_setup_model(&fx, model);
    active = 1;
    program[0] = op_ash(0, operand(0, 1));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 0177777;
    fx.r.r[1] = 0100077;
    fx.r.psw = 0;
    ASSERT_EQ(core_step(&fx.r), 0, "ASH negative right should execute");
    ASSERT_EQ(fx.r.r[0], 0177777, "ASH negative right result");
    expect_flags(&fx.r, 1, 0, 0, 1);
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after ASH negative right");
    fixture_teardown(&fx);
    active = 0;

    set_test_name(namebuf, sizeof(namebuf), "ashc_neg_right", name);
    fixture_setup_model(&fx, model);
    active = 1;
    program[0] = op_ashc(0, operand(0, 2));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 0177777;
    fx.r.r[1] = 0177777;
    fx.r.r[2] = 0100077;
    fx.r.psw = 0;
    ASSERT_EQ(core_step(&fx.r), 0, "ASHC negative right should execute");
    ASSERT_EQ(fx.r.r[0], 0177777, "ASHC negative right high");
    ASSERT_EQ(fx.r.r[1], 0177777, "ASHC negative right low");
    expect_flags(&fx.r, 1, 0, 0, 1);
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after ASHC negative right");
    fixture_teardown(&fx);
    active = 0;

cleanup:
    if (active) {
        fixture_teardown(&fx);
    }
    return rc;
}

static int test_extended_ops_supported(void)
{
    int rc = 0;

    rc += test_extended_ops_supported_models(K1801VM1G, "K1801VM1G");
    rc += test_extended_ops_supported_models(K1801VM2, "K1801VM2");
    rc += test_extended_ops_supported_models(K1806VM2, "K1806VM2");
    rc += test_extended_ops_supported_models(DCJ11, "DCJ11");

    return rc;
}

static int test_vm1_eis_illegal(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 02000;
    const word new_psw = 000340;
    const word ops[] = {
        op_mul(0, operand(0, 1)),
        op_div(0, operand(0, 1)),
        op_ash(0, operand(0, 1)),
        op_ashc(0, operand(0, 1)),
    };
    const char *names[] = { "MUL", "DIV", "ASH", "ASHC" };

    for (size_t i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
        current_test = names[i];
        fixture_setup_model(&fx, K1801VM1);
        write_op(&fx, ops[i]);
        store_word(&fx, 000010, handler);
        store_word(&fx, 000012, new_psw);
        fx.r.r[6] = 01000;
        fx.r.psw = 000003;
        ASSERT_EQ(core_step(&fx.r), 0, "EIS should trap on VM1");
        ASSERT_EQ(fx.r.r[7], handler, "Illegal instruction vector");
        ASSERT_EQ(fx.r.psw, new_psw, "Illegal instruction PSW");
        ASSERT_EQ(fx.r.r[6], 00774, "SP should push two words");
        ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE + 2, "Stack PC incorrect");
        ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 000003, "Stack PSW incorrect");
        fixture_teardown(&fx);
    }

cleanup:
    return rc;
}

static int test_condition_codes_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];

    fixture_setup_model(&fx, model);

    set_test_name(namebuf, sizeof(namebuf), "nop", name);
    fx.r.psw = FLAG_N | FLAG_Z | FLAG_V | FLAG_C;
    fx.r.r[0] = 01234;
    write_op(&fx, op_nop());
    ASSERT_EQ(core_step(&fx.r), 0, "NOP should execute");
    ASSERT_EQ(fx.r.r[0], 01234, "NOP should not change registers");
    ASSERT_EQ(fx.r.psw, (FLAG_N | FLAG_Z | FLAG_V | FLAG_C), "NOP should not change PSW");

    set_test_name(namebuf, sizeof(namebuf), "clc", name);
    fx.r.psw = FLAG_N | FLAG_Z | FLAG_V | FLAG_C;
    write_op(&fx, op_clc());
    ASSERT_EQ(core_step(&fx.r), 0, "CLC should execute");
    expect_flags(&fx.r, 1, 1, 1, 0);

    set_test_name(namebuf, sizeof(namebuf), "clv", name);
    fx.r.psw = FLAG_N | FLAG_Z | FLAG_V | FLAG_C;
    write_op(&fx, op_clv());
    ASSERT_EQ(core_step(&fx.r), 0, "CLV should execute");
    expect_flags(&fx.r, 1, 1, 0, 1);

    set_test_name(namebuf, sizeof(namebuf), "clz", name);
    fx.r.psw = FLAG_N | FLAG_Z | FLAG_V | FLAG_C;
    write_op(&fx, op_clz());
    ASSERT_EQ(core_step(&fx.r), 0, "CLZ should execute");
    expect_flags(&fx.r, 1, 0, 1, 1);

    set_test_name(namebuf, sizeof(namebuf), "cln", name);
    fx.r.psw = FLAG_N | FLAG_Z | FLAG_V | FLAG_C;
    write_op(&fx, op_cln());
    ASSERT_EQ(core_step(&fx.r), 0, "CLN should execute");
    expect_flags(&fx.r, 0, 1, 1, 1);

    set_test_name(namebuf, sizeof(namebuf), "ccc", name);
    fx.r.psw = FLAG_N | FLAG_Z | FLAG_V | FLAG_C;
    write_op(&fx, op_ccc());
    ASSERT_EQ(core_step(&fx.r), 0, "CCC should execute");
    expect_flags(&fx.r, 0, 0, 0, 0);

    set_test_name(namebuf, sizeof(namebuf), "sec", name);
    fx.r.psw = 0;
    write_op(&fx, op_sec());
    ASSERT_EQ(core_step(&fx.r), 0, "SEC should execute");
    expect_flags(&fx.r, 0, 0, 0, 1);

    set_test_name(namebuf, sizeof(namebuf), "sev", name);
    fx.r.psw = 0;
    write_op(&fx, op_sev());
    ASSERT_EQ(core_step(&fx.r), 0, "SEV should execute");
    expect_flags(&fx.r, 0, 0, 1, 0);

    set_test_name(namebuf, sizeof(namebuf), "sez", name);
    fx.r.psw = 0;
    write_op(&fx, op_sez());
    ASSERT_EQ(core_step(&fx.r), 0, "SEZ should execute");
    expect_flags(&fx.r, 0, 1, 0, 0);

    set_test_name(namebuf, sizeof(namebuf), "sen", name);
    fx.r.psw = 0;
    write_op(&fx, op_sen());
    ASSERT_EQ(core_step(&fx.r), 0, "SEN should execute");
    expect_flags(&fx.r, 1, 0, 0, 0);

    set_test_name(namebuf, sizeof(namebuf), "scc", name);
    fx.r.psw = 0;
    write_op(&fx, op_scc());
    ASSERT_EQ(core_step(&fx.r), 0, "SCC should execute");
    expect_flags(&fx.r, 1, 1, 1, 1);

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_condition_codes(void)
{
    return run_for_models(test_condition_codes_model);
}

static int test_wait_and_reset_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];

    fixture_setup_model(&fx, model);

    set_test_name(namebuf, sizeof(namebuf), "wait", name);
    write_op(&fx, op_wait());
    ASSERT_EQ(core_step(&fx.r), 0, "WAIT should execute");
    ASSERT_EQ(fx.r.fWait, 1, "WAIT should set fWait");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after WAIT");

    ASSERT_EQ(core_step(&fx.r), 0, "WAIT should stop execution");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should stay while waiting");

    fx.r.fWait = 0;
    set_test_name(namebuf, sizeof(namebuf), "reset", name);
    write_op(&fx, op_reset());
    fx.r.psw = 000003;
    fx.r.r[0] = 0777;
    ASSERT_EQ(core_step(&fx.r), 0, "RESET should execute");
    ASSERT_EQ(fx.r.psw, 000003, "RESET should not change PSW");
    ASSERT_EQ(fx.r.r[0], 0777, "RESET should not change registers");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after RESET");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_wait_and_reset(void)
{
    return run_for_models(test_wait_and_reset_model);
}

static int test_dcj11_wait_resume_on_irq(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 02000;
    const word new_psw = 000340;
    const word program[] = {
        op_wait(),
        op_nop(),
    };

    current_test = "dcj11_wait_irq";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000060, handler);
    store_word(&fx, 000062, new_psw);
    store_word(&fx, handler, op_nop());
    ASSERT_EQ(fx.r.load_word(&fx.r, 000060), handler, "IRQ vector sanity");
    ASSERT_EQ(fx.r.load_word(&fx.r, 000062), new_psw, "IRQ PSW sanity");
    fx.r.r[6] = 01000;
    fx.r.psw = 000003;
    fx.r.poll_irq = test_poll_irq;

    ASSERT_EQ(core_step(&fx.r), 0, "WAIT should execute");
    ASSERT_EQ(fx.r.fWait, 1, "WAIT should set fWait");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after WAIT");

    test_irq_pending = 1;
    ASSERT_EQ(core_step(&fx.r), 0, "IRQ should resume WAIT");
    ASSERT_EQ(fx.r.fWait, 0, "IRQ should clear fWait");
    ASSERT_EQ(fx.r.r[7], handler, "PC should enter handler");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should load IRQ vector");
    ASSERT_EQ(fx.r.r[6], 00774, "SP should push two words");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE + 2, "Stack PC incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 000003, "Stack PSW incorrect");

cleanup:
    fixture_teardown(&fx);
    test_irq_pending = 0;
    test_irq_priority = 0;
    return rc;
}

static int test_vm1_wait_ignores_trace_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word handler = 02000;
    const word new_psw = 000340;
    const word program[] = {
        op_wait(),
    };

    if (!is_vm1_model(model)) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "vm1_wait_ignore_t", name);
    fixture_setup_model(&fx, model);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 014, handler);
    store_word(&fx, 016, new_psw);
    fx.r.r[6] = 01000;
    store_word(&fx, 00774, 012345);
    store_word(&fx, 00776, 065432);
    fx.r.psw = FLAG_T | 000003;

    ASSERT_EQ(core_step(&fx.r), 0, "WAIT should execute");
    ASSERT_EQ(fx.r.fWait, 1, "WAIT should set fWait");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "WAIT should not trace when T is set");
    ASSERT_EQ(fx.r.psw, (FLAG_T | 000003), "WAIT should not change PSW");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), 012345, "WAIT should not push PC");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 065432, "WAIT should not push PSW");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_halt_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    word handler = 01234;
    word new_psw = 00440;

    fixture_setup_model(&fx, model);
    set_test_name(namebuf, sizeof(namebuf), "halt", name);

    fx.r.r[6] = 01000;
    fx.r.psw = 000003;
    fx.r.SEL0 = 0400;
    if (is_vm1_model(model)) {
        store_word(&fx, 0177716, 0160000);
    } else {
        store_word(&fx, 0177716, 0200);
    }
    store_word(&fx, 0170, handler);
    store_word(&fx, 0172, new_psw);
    store_word(&fx, 0570, handler);
    store_word(&fx, 0572, new_psw);
    if (is_vm2_model(model)) {
        store_word(&fx, 0572, (word)(FLAG_P | FLAG_H));
    }
    store_word(&fx, 000004, handler);
    store_word(&fx, 000006, 000000);
    store_word(&fx, 0160002, handler);
    store_word(&fx, 0160004, new_psw);

    write_op(&fx, op_halt());
    ASSERT_EQ(core_step(&fx.r), 0, "HALT should execute");

    if (is_vm2_model(model)) {
        ASSERT_EQ(fx.r.r[7], handler & 0177776, "HALT should load vector");
        ASSERT_EQ(fx.r.psw, (word)(FLAG_P | FLAG_H), "HALT should load PSW");
        ASSERT_EQ(fx.r.cpc, TEST_BASE + 2, "HALT should save CPC");
        ASSERT_EQ(fx.r.cps, 000003, "HALT should save CPS");
    } else if (is_vm1_model(model)) {
        ASSERT_EQ(fx.r.load_word(&fx.r, 0177674), TEST_BASE + 2, "HALT should store PC");
        ASSERT_EQ(fx.r.load_word(&fx.r, 0177676), 000003, "HALT should store PSW");
        ASSERT_EQ(fx.r.load_word(&fx.r, 0177716), 0160010, "HALT should set SEL1 bit 010");
        ASSERT_EQ(fx.r.r[7], handler, "HALT should load vector");
        ASSERT_EQ(fx.r.psw, new_psw, "HALT should load PSW");
    } else if (model == DCJ11) {
        ASSERT_EQ(fx.r.r[6], 00774, "HALT should push two words");
        ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE + 2, "HALT stack PC");
        ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 000003, "HALT stack PSW");
        ASSERT_EQ(fx.r.r[7], handler & 0177776, "HALT should load vector");
        ASSERT_EQ(fx.r.psw, 0340, "HALT should set PSW to 0340");
    } else {
        ASSERT_EQ(fx.r.load_word(&fx.r, 0177674), TEST_BASE + 2, "HALT should store PC");
        ASSERT_EQ(fx.r.load_word(&fx.r, 0177676), 000003, "HALT should store PSW");
        ASSERT_EQ(fx.r.load_word(&fx.r, 0177716), 0210, "HALT should set SEL1 bit 010");
        ASSERT_EQ(fx.r.r[7], handler & 0177776, "HALT should load vector");
        ASSERT_EQ(fx.r.psw, new_psw, "HALT should load PSW");
    }

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_halt(void)
{
    return run_for_models(test_halt_model);
}

static int test_external_halt_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    word handler = 01234;
    word new_psw = 00440;

    fixture_setup_model(&fx, model);
    set_test_name(namebuf, sizeof(namebuf), "halt_signal", name);

    fx.r.r[6] = 01000;
    fx.r.psw = 000003;
    fx.r.SEL0 = 0400;
    if (is_vm1_model(model)) {
        store_word(&fx, 0177716, 0160000);
    } else {
        store_word(&fx, 0177716, 0200);
    }
    store_word(&fx, 0170, handler);
    store_word(&fx, 0172, new_psw);
    store_word(&fx, 0570, handler);
    store_word(&fx, 0572, new_psw);
    if (is_vm2_model(model)) {
        store_word(&fx, 0572, (word)(FLAG_P | FLAG_H));
    }
    store_word(&fx, 000004, handler);
    store_word(&fx, 000006, 000000);
    store_word(&fx, 0160002, handler);
    store_word(&fx, 0160004, new_psw);

    fx.r.fHaltSignal = 1;
    ASSERT_EQ(core_step(&fx.r), 0, "HALT signal should execute");

    if (is_vm2_model(model)) {
        ASSERT_EQ(fx.r.r[7], handler & 0177776, "HALT signal should load vector");
        ASSERT_EQ(fx.r.psw, (word)(FLAG_P | FLAG_H), "HALT signal should load PSW");
        ASSERT_EQ(fx.r.cpc, TEST_BASE, "HALT signal should save CPC");
        ASSERT_EQ(fx.r.cps, 000003, "HALT signal should save CPS");
    } else if (is_vm1_model(model)) {
        ASSERT_EQ(fx.r.load_word(&fx.r, 0177674), TEST_BASE, "HALT signal should store PC");
        ASSERT_EQ(fx.r.load_word(&fx.r, 0177676), 000003, "HALT signal should store PSW");
        ASSERT_EQ(fx.r.load_word(&fx.r, 0177716), 0160010, "HALT signal should set SEL1 bit 010");
        ASSERT_EQ(fx.r.r[7], handler, "HALT signal should load vector");
        ASSERT_EQ(fx.r.psw, new_psw, "HALT signal should load PSW");
    } else if (model == DCJ11) {
        ASSERT_EQ(fx.r.r[6], 00774, "HALT signal should push two words");
        ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE, "HALT signal stack PC");
        ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 000003, "HALT signal stack PSW");
        ASSERT_EQ(fx.r.r[7], handler & 0177776, "HALT signal should load vector");
        ASSERT_EQ(fx.r.psw, 0340, "HALT signal should set PSW to 0340");
    } else {
        ASSERT_EQ(fx.r.load_word(&fx.r, 0177674), TEST_BASE, "HALT signal should store PC");
        ASSERT_EQ(fx.r.load_word(&fx.r, 0177676), 000003, "HALT signal should store PSW");
        ASSERT_EQ(fx.r.load_word(&fx.r, 0177716), 0210, "HALT signal should set SEL1 bit 010");
        ASSERT_EQ(fx.r.r[7], handler & 0177776, "HALT signal should load vector");
        ASSERT_EQ(fx.r.psw, new_psw, "HALT signal should load PSW");
    }

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_vm2_external_halt_masked(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word program[] = {
        op_nop(),
    };

    current_test = "vm2_halt_mask";
    fixture_setup_model(&fx, K1801VM2);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    fx.r.psw = FLAG_H;
    fx.r.fHaltSignal = 1;

    ASSERT_EQ(core_step(&fx.r), 0, "HALT signal should be masked by H");
    ASSERT_EQ(fx.r.r[7], TEST_BASE, "PC should remain unchanged");
    ASSERT_EQ(fx.r.psw, FLAG_H, "PSW should remain unchanged");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_external_halt(void)
{
    return run_for_models(test_external_halt_model);
}

static int test_dcj11_halt_user_traps(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 02600;
    const word new_psw = DCJ11_CM(0) | DCJ11_PM(3) | 000200;
    const word program[] = {
        op_halt(),
    };

    current_test = "dcj11_halt_user";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000004, handler);
    store_word(&fx, 000006, new_psw);
    fx.r.r[6] = 01000;
    fx.r.psw = 0140000;

    ASSERT_EQ(core_step(&fx.r), 0, "HALT should trap in user mode");
    ASSERT_EQ(fx.r.r[7], handler, "HALT should vector through 000004");
    ASSERT_EQ(fx.r.psw, new_psw, "HALT should load PSW from vector");
    ASSERT_EQ(fx.r.r[6], 00774, "HALT should push two words");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE + 2, "HALT stack PC");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 0140000, "HALT stack PSW");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_reset_user_is_nop(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word program[] = {
        op_reset(),
    };

    current_test = "dcj11_reset_user";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    fx.r.r[0] = 01234;
    fx.r.psw = 0140000;
    ASSERT_EQ(core_step(&fx.r), 0, "RESET should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "RESET should act as NOP in user mode");
    ASSERT_EQ(fx.r.r[0], 01234, "RESET should not modify registers");
    ASSERT_EQ(fx.r.psw, 0140000, "RESET should not modify PSW");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_reset_kernel_clears_pirq_preserves_cpuerr_mmr1_mmr2(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word program[] = {
        op_reset(),
    };

    current_test = "dcj11_reset_kernel";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    fx.r.psw = 0000000;
    fx.r.J11_PIRQ = 0177000;
    fx.r.J11_CPUERR = 0000160;
#if defined(ENABLE_MMU) && (ENABLE_MMU)
    fx.r.mmu_ssr0 = 0160176;
    fx.r.mmu_ssr1 = 0007777;
    fx.r.mmu_ssr2 = 0001234;
    fx.r.mmu_ssr3 = 0000027;
#endif

    ASSERT_EQ(core_step(&fx.r), 0, "RESET should execute in kernel mode");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after RESET");
    ASSERT_EQ(fx.r.J11_PIRQ, 0000000, "RESET should clear PIRQ");
    ASSERT_EQ(fx.r.J11_CPUERR, 0000160, "RESET should not clear CPUERR");
#if defined(ENABLE_MMU) && (ENABLE_MMU)
    ASSERT_EQ(fx.r.mmu_ssr0, 0000000, "RESET should clear MMR0 control/status");
    ASSERT_EQ(fx.r.mmu_ssr1, 0007777, "RESET should preserve MMR1");
    ASSERT_EQ(fx.r.mmu_ssr2, 0001234, "RESET should preserve MMR2");
    ASSERT_EQ(fx.r.mmu_ssr3, 0000000, "RESET should clear MMR3");
#endif

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_branches_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];

    fixture_setup_model(&fx, model);

    {
        word psw = FLAG_N | FLAG_Z | FLAG_V | FLAG_C;
        set_test_name(namebuf, sizeof(namebuf), "br_forward", name);
        fx.r.psw = psw;
        write_op(&fx, op_br(0001));
        ASSERT_EQ(core_step(&fx.r), 0, "BR should execute");
        ASSERT_EQ(fx.r.r[7], TEST_BASE + 4, "BR should branch forward");
        ASSERT_EQ(fx.r.psw, psw, "BR should not change PSW");
    }

    {
        set_test_name(namebuf, sizeof(namebuf), "br_backward", name);
        fx.r.psw = 0;
        write_op(&fx, op_br(0377));
        ASSERT_EQ(core_step(&fx.r), 0, "BR should execute");
        ASSERT_EQ(fx.r.r[7], TEST_BASE, "BR should branch backward");
    }

    set_test_name(namebuf, sizeof(namebuf), "bne_taken", name);
    fx.r.psw = 0;
    write_op(&fx, op_bne(0001));
    ASSERT_EQ(core_step(&fx.r), 0, "BNE should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 4, "BNE should branch when Z=0");

    set_test_name(namebuf, sizeof(namebuf), "bne_not", name);
    fx.r.psw = FLAG_Z;
    write_op(&fx, op_bne(0001));
    ASSERT_EQ(core_step(&fx.r), 0, "BNE should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "BNE should not branch when Z=1");

    set_test_name(namebuf, sizeof(namebuf), "beq_taken", name);
    fx.r.psw = FLAG_Z;
    write_op(&fx, op_beq(0001));
    ASSERT_EQ(core_step(&fx.r), 0, "BEQ should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 4, "BEQ should branch when Z=1");

    set_test_name(namebuf, sizeof(namebuf), "beq_not", name);
    fx.r.psw = 0;
    write_op(&fx, op_beq(0001));
    ASSERT_EQ(core_step(&fx.r), 0, "BEQ should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "BEQ should not branch when Z=0");

    set_test_name(namebuf, sizeof(namebuf), "bpl_taken", name);
    fx.r.psw = 0;
    write_op(&fx, op_bpl(0001));
    ASSERT_EQ(core_step(&fx.r), 0, "BPL should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 4, "BPL should branch when N=0");

    set_test_name(namebuf, sizeof(namebuf), "bpl_not", name);
    fx.r.psw = FLAG_N;
    write_op(&fx, op_bpl(0001));
    ASSERT_EQ(core_step(&fx.r), 0, "BPL should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "BPL should not branch when N=1");

    set_test_name(namebuf, sizeof(namebuf), "bmi_taken", name);
    fx.r.psw = FLAG_N;
    write_op(&fx, op_bmi(0001));
    ASSERT_EQ(core_step(&fx.r), 0, "BMI should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 4, "BMI should branch when N=1");

    set_test_name(namebuf, sizeof(namebuf), "bmi_not", name);
    fx.r.psw = 0;
    write_op(&fx, op_bmi(0001));
    ASSERT_EQ(core_step(&fx.r), 0, "BMI should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "BMI should not branch when N=0");

    set_test_name(namebuf, sizeof(namebuf), "bvc_taken", name);
    fx.r.psw = 0;
    write_op(&fx, op_bvc(0001));
    ASSERT_EQ(core_step(&fx.r), 0, "BVC should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 4, "BVC should branch when V=0");

    set_test_name(namebuf, sizeof(namebuf), "bvc_not", name);
    fx.r.psw = FLAG_V;
    write_op(&fx, op_bvc(0001));
    ASSERT_EQ(core_step(&fx.r), 0, "BVC should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "BVC should not branch when V=1");

    set_test_name(namebuf, sizeof(namebuf), "bvs_taken", name);
    fx.r.psw = FLAG_V;
    write_op(&fx, op_bvs(0001));
    ASSERT_EQ(core_step(&fx.r), 0, "BVS should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 4, "BVS should branch when V=1");

    set_test_name(namebuf, sizeof(namebuf), "bvs_not", name);
    fx.r.psw = 0;
    write_op(&fx, op_bvs(0001));
    ASSERT_EQ(core_step(&fx.r), 0, "BVS should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "BVS should not branch when V=0");

    set_test_name(namebuf, sizeof(namebuf), "bcc_taken", name);
    fx.r.psw = 0;
    write_op(&fx, op_bcc(0001));
    ASSERT_EQ(core_step(&fx.r), 0, "BCC should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 4, "BCC should branch when C=0");

    set_test_name(namebuf, sizeof(namebuf), "bcc_not", name);
    fx.r.psw = FLAG_C;
    write_op(&fx, op_bcc(0001));
    ASSERT_EQ(core_step(&fx.r), 0, "BCC should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "BCC should not branch when C=1");

    set_test_name(namebuf, sizeof(namebuf), "bcs_taken", name);
    fx.r.psw = FLAG_C;
    write_op(&fx, op_bcs(0001));
    ASSERT_EQ(core_step(&fx.r), 0, "BCS should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 4, "BCS should branch when C=1");

    set_test_name(namebuf, sizeof(namebuf), "bcs_not", name);
    fx.r.psw = 0;
    write_op(&fx, op_bcs(0001));
    ASSERT_EQ(core_step(&fx.r), 0, "BCS should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "BCS should not branch when C=0");

    set_test_name(namebuf, sizeof(namebuf), "bge_taken", name);
    fx.r.psw = 0;
    write_op(&fx, op_bge(0001));
    ASSERT_EQ(core_step(&fx.r), 0, "BGE should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 4, "BGE should branch when N=V");

    set_test_name(namebuf, sizeof(namebuf), "bge_not", name);
    fx.r.psw = FLAG_N;
    write_op(&fx, op_bge(0001));
    ASSERT_EQ(core_step(&fx.r), 0, "BGE should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "BGE should not branch when N!=V");

    set_test_name(namebuf, sizeof(namebuf), "blt_taken", name);
    fx.r.psw = FLAG_N;
    write_op(&fx, op_blt(0001));
    ASSERT_EQ(core_step(&fx.r), 0, "BLT should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 4, "BLT should branch when N!=V");

    set_test_name(namebuf, sizeof(namebuf), "blt_not", name);
    fx.r.psw = 0;
    write_op(&fx, op_blt(0001));
    ASSERT_EQ(core_step(&fx.r), 0, "BLT should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "BLT should not branch when N=V");

    set_test_name(namebuf, sizeof(namebuf), "bgt_taken", name);
    fx.r.psw = 0;
    write_op(&fx, op_bgt(0001));
    ASSERT_EQ(core_step(&fx.r), 0, "BGT should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 4, "BGT should branch when !Z && N=V");

    set_test_name(namebuf, sizeof(namebuf), "bgt_not", name);
    fx.r.psw = FLAG_Z;
    write_op(&fx, op_bgt(0001));
    ASSERT_EQ(core_step(&fx.r), 0, "BGT should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "BGT should not branch when Z=1");

    set_test_name(namebuf, sizeof(namebuf), "ble_taken", name);
    fx.r.psw = FLAG_Z;
    write_op(&fx, op_ble(0001));
    ASSERT_EQ(core_step(&fx.r), 0, "BLE should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 4, "BLE should branch when Z=1");

    set_test_name(namebuf, sizeof(namebuf), "ble_not", name);
    fx.r.psw = 0;
    write_op(&fx, op_ble(0001));
    ASSERT_EQ(core_step(&fx.r), 0, "BLE should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "BLE should not branch when Z=0 and N=V");

    set_test_name(namebuf, sizeof(namebuf), "bhi_taken", name);
    fx.r.psw = 0;
    write_op(&fx, op_bhi(0001));
    ASSERT_EQ(core_step(&fx.r), 0, "BHI should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 4, "BHI should branch when C=0 && Z=0");

    set_test_name(namebuf, sizeof(namebuf), "bhi_not", name);
    fx.r.psw = FLAG_C;
    write_op(&fx, op_bhi(0001));
    ASSERT_EQ(core_step(&fx.r), 0, "BHI should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "BHI should not branch when C=1");

    set_test_name(namebuf, sizeof(namebuf), "blos_taken", name);
    fx.r.psw = FLAG_C;
    write_op(&fx, op_blos(0001));
    ASSERT_EQ(core_step(&fx.r), 0, "BLOS should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 4, "BLOS should branch when C=1");

    set_test_name(namebuf, sizeof(namebuf), "blos_not", name);
    fx.r.psw = 0;
    write_op(&fx, op_blos(0001));
    ASSERT_EQ(core_step(&fx.r), 0, "BLOS should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "BLOS should not branch when C=0 && Z=0");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_branches(void)
{
    return run_for_models(test_branches_model);
}

static int test_jmp_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    word target = 01234;

    fixture_setup_model(&fx, model);
    set_test_name(namebuf, sizeof(namebuf), "jmp", name);

    fx.r.r[1] = target;
    write_op(&fx, op_jmp(operand(1, 1)));
    ASSERT_EQ(core_step(&fx.r), 0, "JMP should execute");
    ASSERT_EQ(fx.r.r[7], target, "JMP should load target address");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_jmp(void)
{
    return run_for_models(test_jmp_model);
}

static int test_jmp_jsr_autoinc_mode2_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word initial_sp = 01000;
    int setup_active = 0;

    set_test_name(namebuf, sizeof(namebuf), "jmp_autoinc_mode2", name);
    fixture_setup_model(&fx, model);
    setup_active = 1;
    fx.r.r[1] = 02000;
    write_op(&fx, op_jmp(operand(2, 1))); /* JMP (R1)+ */
    ASSERT_EQ(core_step(&fx.r), 0, "JMP (R)+ should execute");
    ASSERT_EQ(fx.r.r[7], 02000, "JMP (R)+ should load PC from original R");
    ASSERT_EQ(fx.r.r[1], 02002, "JMP (R)+ should autoincrement register by 2");
    fixture_teardown(&fx);
    setup_active = 0;

    set_test_name(namebuf, sizeof(namebuf), "jsr_autoinc_mode2", name);
    fixture_setup_model(&fx, model);
    setup_active = 1;
    fx.r.r[2] = 03000;
    fx.r.r[5] = 012345;
    fx.r.r[6] = initial_sp;
    write_op(&fx, op_jsr(5, operand(2, 2))); /* JSR R5,(R2)+ */
    ASSERT_EQ(core_step(&fx.r), 0, "JSR reg,(R)+ should execute");
    ASSERT_EQ(fx.r.r[7], 03000, "JSR reg,(R)+ should load PC from original R");
    ASSERT_EQ(fx.r.r[2], 03002, "JSR reg,(R)+ should autoincrement EA register");
    ASSERT_EQ(fx.r.r[5], TEST_BASE + 2, "JSR should save return PC into source register");
    ASSERT_EQ(fx.r.r[6], initial_sp - 2, "JSR should push source register value");
    ASSERT_EQ(fx.r.load_word(&fx.r, initial_sp - 2), 012345,
              "JSR should push original source register value");

cleanup:
    if (setup_active) {
        fixture_teardown(&fx);
    }
    return rc;
}

static int test_jmp_jsr_autoinc_mode2(void)
{
    return run_for_models(test_jmp_jsr_autoinc_mode2_model);
}

static int test_jmp_jsr_mode0_trap_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word handler = 02600;
    const word new_psw = 000340;
    const word jmp_trap_vec = 000004;
    const word jsr_trap_vec = (model == DCJ11) ? 000010 : 000004;
    const word initial_sp = 01000;

    set_test_name(namebuf, sizeof(namebuf), "jmp_mode0_trap", name);
    fixture_setup_model(&fx, model);

    store_word(&fx, jmp_trap_vec, handler);
    store_word(&fx, jmp_trap_vec + 2, new_psw);
    fx.r.r[6] = initial_sp;
    fx.r.psw = 000000;
    write_op(&fx, op_jmp(operand(0, 1))); /* JMP R1 (mode 0) */

    ASSERT_EQ(core_step(&fx.r), 0, "JMP mode0 should trap");
    ASSERT_EQ(fx.r.r[7], handler, "JMP mode0 should vector to expected trap");
    ASSERT_EQ(fx.r.psw, new_psw, "JMP mode0 should load trap PSW");
    ASSERT_EQ(fx.r.r[6], initial_sp - 4, "Trap frame should push PC+PSW");
    ASSERT_EQ(fx.r.load_word(&fx.r, initial_sp - 4), TEST_BASE + 2,
              "Trap frame PC should point to next instruction");
    ASSERT_EQ(fx.r.load_word(&fx.r, initial_sp - 2), 000000,
              "Trap frame PSW should preserve old PSW");
    fixture_teardown(&fx);

    set_test_name(namebuf, sizeof(namebuf), "jsr_mode0_trap", name);
    fixture_setup_model(&fx, model);

    store_word(&fx, jsr_trap_vec, handler);
    store_word(&fx, jsr_trap_vec + 2, new_psw);
    fx.r.r[2] = 012345;
    fx.r.r[5] = 065432;
    fx.r.r[6] = initial_sp;
    fx.r.psw = 000000;
    write_op(&fx, op_jsr(5, operand(0, 2))); /* JSR R5,R2 (mode 0) */

    ASSERT_EQ(core_step(&fx.r), 0, "JSR mode0 should trap");
    ASSERT_EQ(fx.r.r[7], handler, "JSR mode0 should vector to expected trap");
    ASSERT_EQ(fx.r.psw, new_psw, "JSR mode0 should load trap PSW");
    ASSERT_EQ(fx.r.r[6], initial_sp - 4, "Trap frame should push PC+PSW");
    ASSERT_EQ(fx.r.load_word(&fx.r, initial_sp - 4), TEST_BASE + 2,
              "Trap frame PC should point to next instruction");
    ASSERT_EQ(fx.r.load_word(&fx.r, initial_sp - 2), 000000,
              "Trap frame PSW should preserve old PSW");
    ASSERT_EQ(fx.r.r[5], 065432, "JSR mode0 trap must not modify source register");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_jmp_jsr_mode0_trap(void)
{
    return run_for_models(test_jmp_jsr_mode0_trap_model);
}

static int test_reg_source_order_split_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word initial_sp = 01000;

    set_test_name(namebuf, sizeof(namebuf), "mov_same_reg_autoinc", name);
    fixture_setup_model(&fx, model);
    fx.r.r[1] = 02000;
    write_op(&fx, op_mov(operand(0, 1), operand(2, 1))); /* MOV R1,(R1)+ */
    ASSERT_EQ(core_step(&fx.r), 0, "MOV should execute");
    ASSERT_EQ(fx.r.r[1], 02002, "Destination autoincrement should apply");
    ASSERT_EQ(fx.r.load_word(&fx.r, 02000),
              (model == DCJ11) ? 02002 : 02000,
              "MOV source register sampling must be model-specific");
    fixture_teardown(&fx);

    set_test_name(namebuf, sizeof(namebuf), "mov_same_reg_autoinc_def", name);
    fixture_setup_model(&fx, model);
    fx.r.r[1] = 02000;
    store_word(&fx, 02000, 03000);
    write_op(&fx, op_mov(operand(0, 1), operand(3, 1))); /* MOV R1,@(R1)+ */
    ASSERT_EQ(core_step(&fx.r), 0, "MOV should execute");
    ASSERT_EQ(fx.r.r[1], 02002, "Destination autoincrement should apply");
    ASSERT_EQ(fx.r.load_word(&fx.r, 03000),
              (model == DCJ11) ? 02002 : 02000,
              "MOV deferred source sampling must be model-specific");
    fixture_teardown(&fx);

    set_test_name(namebuf, sizeof(namebuf), "mov_same_reg_autodec", name);
    fixture_setup_model(&fx, model);
    fx.r.r[1] = 02000;
    write_op(&fx, op_mov(operand(0, 1), operand(4, 1))); /* MOV R1,-(R1) */
    ASSERT_EQ(core_step(&fx.r), 0, "MOV should execute");
    ASSERT_EQ(fx.r.r[1], 01776, "Destination autodecrement should apply");
    ASSERT_EQ(fx.r.load_word(&fx.r, 01776),
              (model == DCJ11) ? 01776 : 02000,
              "MOV autodecrement source sampling must be model-specific");
    fixture_teardown(&fx);

    set_test_name(namebuf, sizeof(namebuf), "mov_same_reg_autodec_def", name);
    fixture_setup_model(&fx, model);
    fx.r.r[1] = 02000;
    store_word(&fx, 01776, 03000);
    write_op(&fx, op_mov(operand(0, 1), operand(5, 1))); /* MOV R1,@-(R1) */
    ASSERT_EQ(core_step(&fx.r), 0, "MOV should execute");
    ASSERT_EQ(fx.r.r[1], 01776, "Destination autodecrement should apply");
    ASSERT_EQ(fx.r.load_word(&fx.r, 03000),
              (model == DCJ11) ? 01776 : 02000,
              "MOV deferred autodecrement source sampling must be model-specific");
    fixture_teardown(&fx);

    set_test_name(namebuf, sizeof(namebuf), "mov_pc_index_src", name);
    fixture_setup_model(&fx, model);
    fx.r.r[0] = 03000;
    write_op(&fx, op_mov(operand(0, 7), operand(6, 0))); /* MOV PC,4(R0) */
    store_word(&fx, TEST_BASE + 2, 000004);
    ASSERT_EQ(core_step(&fx.r), 0, "MOV PC,X(R) should execute");
    ASSERT_EQ(fx.r.load_word(&fx.r, 03004),
              (model == DCJ11) ? (TEST_BASE + 4) : (TEST_BASE + 2),
              "MOV PC source value must be model-specific");
    fixture_teardown(&fx);

    set_test_name(namebuf, sizeof(namebuf), "jsr_same_reg_autoinc_src", name);
    fixture_setup_model(&fx, model);
    fx.r.r[2] = 02000;
    fx.r.r[6] = initial_sp;
    store_word(&fx, 02000, 03000);
    write_op(&fx, op_jsr(2, operand(3, 2))); /* JSR R2,@(R2)+ */
    ASSERT_EQ(core_step(&fx.r), 0, "JSR should execute");
    ASSERT_EQ(fx.r.r[7], 03000, "JSR should jump through deferred destination");
    ASSERT_EQ(fx.r.r[6], initial_sp - 2, "JSR should push one word");
    ASSERT_EQ(fx.r.load_word(&fx.r, initial_sp - 2), 02002,
              "JSR source register value should follow destination EA timing");
    fixture_teardown(&fx);

    set_test_name(namebuf, sizeof(namebuf), "xor_same_reg_autoinc_src", name);
    fixture_setup_model(&fx, model);
    fx.r.r[1] = 02000;
    store_word(&fx, 02000, 012345);
    write_op(&fx, op_xor(1, operand(2, 1))); /* XOR R1,(R1)+ */
    ASSERT_EQ(core_step(&fx.r), 0, "XOR should execute");
    ASSERT_EQ(fx.r.r[1], 02002, "XOR destination autoincrement should apply");
    ASSERT_EQ(fx.r.load_word(&fx.r, 02000),
              (word)(012345 ^ ((model == DCJ11) ? 02002 : 02000)),
              "XOR source register sampling must be model-specific");
    fixture_teardown(&fx);

    set_test_name(namebuf, sizeof(namebuf), "jsr_pc_index_src", name);
    fixture_setup_model(&fx, model);
    fx.r.r[0] = 03000;
    fx.r.r[6] = initial_sp;
    write_op(&fx, op_jsr(7, operand(6, 0))); /* JSR PC,4(R0) */
    store_word(&fx, TEST_BASE + 2, 000004);
    ASSERT_EQ(core_step(&fx.r), 0, "JSR PC,X(R) should execute");
    ASSERT_EQ(fx.r.r[7], 03004, "JSR PC,X(R) should jump to indexed destination");
    ASSERT_EQ(fx.r.r[6], initial_sp - 2, "JSR should push one word");
    ASSERT_EQ(fx.r.load_word(&fx.r, initial_sp - 2), TEST_BASE + 4,
              "JSR PC source value should point past extension word");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_reg_source_order_split(void)
{
    return run_for_models(test_reg_source_order_split_model);
}

static int test_addressing_modes_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];

    fixture_setup_model(&fx, model);

    set_test_name(namebuf, sizeof(namebuf), "addr_rn", name);
    fx.r.r[1] = 01234;
    write_op(&fx, op_mov(operand(0, 1), operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "MOV R1,R0");
    ASSERT_EQ(fx.r.r[0], 01234, "Rn mode should read register");

    set_test_name(namebuf, sizeof(namebuf), "addr_indirect", name);
    fx.r.r[1] = 02000;
    store_word(&fx, 02000, 04567);
    write_op(&fx, op_mov(operand(1, 1), operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "MOV (R1),R0");
    ASSERT_EQ(fx.r.r[0], 04567, "(Rn) should read memory");

    set_test_name(namebuf, sizeof(namebuf), "addr_autoinc", name);
    fx.r.r[1] = 02000;
    store_word(&fx, 02000, 01111);
    write_op(&fx, op_mov(operand(2, 1), operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "MOV (R1)+,R0");
    ASSERT_EQ(fx.r.r[0], 01111, "(Rn)+ should read memory");
    ASSERT_EQ(fx.r.r[1], 02002, "(Rn)+ should increment by 2");

    set_test_name(namebuf, sizeof(namebuf), "addr_autoinc_def", name);
    fx.r.r[1] = 02000;
    store_word(&fx, 02000, 03000);
    store_word(&fx, 03000, 02222);
    write_op(&fx, op_mov(operand(3, 1), operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "MOV @(R1)+,R0");
    ASSERT_EQ(fx.r.r[0], 02222, "@(Rn)+ should read indirect");
    ASSERT_EQ(fx.r.r[1], 02002, "@(Rn)+ should increment by 2");

    set_test_name(namebuf, sizeof(namebuf), "addr_autodec", name);
    fx.r.r[1] = 02002;
    store_word(&fx, 02000, 03333);
    write_op(&fx, op_mov(operand(4, 1), operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "MOV -(R1),R0");
    ASSERT_EQ(fx.r.r[0], 03333, "-(Rn) should read memory");
    ASSERT_EQ(fx.r.r[1], 02000, "-(Rn) should decrement by 2");

    set_test_name(namebuf, sizeof(namebuf), "addr_autodec_def", name);
    fx.r.r[1] = 02002;
    store_word(&fx, 02000, 03000);
    store_word(&fx, 03000, 04444);
    write_op(&fx, op_mov(operand(5, 1), operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "MOV @-(R1),R0");
    ASSERT_EQ(fx.r.r[0], 04444, "@-(Rn) should read indirect");
    ASSERT_EQ(fx.r.r[1], 02000, "@-(Rn) should decrement by 2");

    set_test_name(namebuf, sizeof(namebuf), "addr_index", name);
    fx.r.r[1] = 02000;
    store_word(&fx, 02004, 05555);
    store_word(&fx, TEST_BASE + 2, 0004);
    write_op(&fx, op_mov(operand(6, 1), operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "MOV X(R1),R0");
    ASSERT_EQ(fx.r.r[0], 05555, "X(Rn) should read with displacement");

    set_test_name(namebuf, sizeof(namebuf), "addr_index_def", name);
    fx.r.r[1] = 02000;
    store_word(&fx, 02004, 03000);
    store_word(&fx, 03000, 06666);
    store_word(&fx, TEST_BASE + 2, 0004);
    write_op(&fx, op_mov(operand(7, 1), operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "MOV @X(R1),R0");
    ASSERT_EQ(fx.r.r[0], 06666, "@X(Rn) should read indirect");

    set_test_name(namebuf, sizeof(namebuf), "addr_byte_autoinc", name);
    fx.r.r[1] = 02000;
    store_word(&fx, 02000, 01234);
    write_op(&fx, op_movb(operand(2, 1), operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "MOVB (R1)+,R0");
    ASSERT_EQ(fx.r.r[0] & 0377, 0234, "MOVB should read byte");
    ASSERT_EQ(fx.r.r[1], 02001, "Byte (Rn)+ should increment by 1");

    set_test_name(namebuf, sizeof(namebuf), "addr_byte_sp_inc", name);
    fx.r.r[6] = 03000;
    store_word(&fx, 03000, 077);
    write_op(&fx, op_movb(operand(2, 6), operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "MOVB (SP)+,R0");
    ASSERT_EQ(fx.r.r[6], 03002, "Byte (SP)+ should increment by 2");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_addressing_modes(void)
{
    return run_for_models(test_addressing_modes_model);
}

static int test_single_operand_word_ops_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];

    fixture_setup_model(&fx, model);

    set_test_name(namebuf, sizeof(namebuf), "clr", name);
    fx.r.r[0] = 01234;
    fx.r.psw = FLAG_N | FLAG_V | FLAG_C;
    write_op(&fx, op_clr(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "CLR");
    ASSERT_EQ(fx.r.r[0], 0, "CLR should clear register");
    expect_flags(&fx.r, 0, 1, 0, 0);

    set_test_name(namebuf, sizeof(namebuf), "com", name);
    fx.r.r[0] = 0;
    write_op(&fx, op_com(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "COM");
    ASSERT_EQ(fx.r.r[0], 0177777, "COM should invert");
    expect_flags(&fx.r, 1, 0, 0, 1);

    set_test_name(namebuf, sizeof(namebuf), "inc", name);
    fx.r.r[0] = 077777;
    write_op(&fx, op_inc(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "INC");
    ASSERT_EQ(fx.r.r[0], 0100000, "INC should increment");
    expect_flags(&fx.r, 1, 0, 1, -1);

    set_test_name(namebuf, sizeof(namebuf), "dec", name);
    fx.r.r[0] = 0100000;
    write_op(&fx, op_dec(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "DEC");
    ASSERT_EQ(fx.r.r[0], 077777, "DEC should decrement");
    expect_flags(&fx.r, 0, 0, 1, -1);

    set_test_name(namebuf, sizeof(namebuf), "neg", name);
    fx.r.r[0] = 1;
    write_op(&fx, op_neg(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "NEG");
    ASSERT_EQ(fx.r.r[0], 0177777, "NEG should negate");
    expect_flags(&fx.r, 1, 0, 0, 1);

    set_test_name(namebuf, sizeof(namebuf), "tst", name);
    fx.r.r[0] = 0;
    fx.r.psw = FLAG_C | FLAG_V;
    write_op(&fx, op_tst(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "TST");
    ASSERT_EQ(fx.r.r[0], 0, "TST should not modify");
    expect_flags(&fx.r, 0, 1, 0, -1);

    set_test_name(namebuf, sizeof(namebuf), "asr", name);
    fx.r.r[0] = 1;
    fx.r.psw = 0;
    write_op(&fx, op_asr(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "ASR");
    ASSERT_EQ(fx.r.r[0], 0, "ASR should shift");
    expect_flags(&fx.r, 0, 1, 1, 1);

    set_test_name(namebuf, sizeof(namebuf), "asl", name);
    fx.r.r[0] = 0100000;
    fx.r.psw = 0;
    write_op(&fx, op_asl(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "ASL");
    ASSERT_EQ(fx.r.r[0], 0, "ASL should shift");
    expect_flags(&fx.r, 0, 1, 1, 1);

    set_test_name(namebuf, sizeof(namebuf), "ror", name);
    fx.r.r[0] = 1;
    fx.r.psw = FLAG_C;
    write_op(&fx, op_ror(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "ROR");
    ASSERT_EQ(fx.r.r[0], 0100000, "ROR should rotate");
    expect_flags(&fx.r, 1, 0, 0, 1);

    set_test_name(namebuf, sizeof(namebuf), "rol", name);
    fx.r.r[0] = 0100000;
    fx.r.psw = 1;
    write_op(&fx, op_rol(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "ROL");
    ASSERT_EQ(fx.r.r[0], 1, "ROL should rotate");
    expect_flags(&fx.r, 0, 0, 1, 1);

    set_test_name(namebuf, sizeof(namebuf), "adc", name);
    fx.r.r[0] = 0177777;
    fx.r.psw = FLAG_C;
    write_op(&fx, op_adc(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "ADC");
    ASSERT_EQ(fx.r.r[0], 0, "ADC should add carry");
    expect_flags(&fx.r, 0, 1, 0, 1);

    set_test_name(namebuf, sizeof(namebuf), "sbc", name);
    fx.r.r[0] = 0;
    fx.r.psw = FLAG_C;
    write_op(&fx, op_sbc(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "SBC");
    ASSERT_EQ(fx.r.r[0], 0177777, "SBC should subtract carry");
    expect_flags(&fx.r, 1, 0, 0, 1);

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_single_operand_word_ops(void)
{
    return run_for_models(test_single_operand_word_ops_model);
}

static int test_single_operand_byte_ops_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];

    fixture_setup_model(&fx, model);

    set_test_name(namebuf, sizeof(namebuf), "clrb", name);
    fx.r.r[0] = 0123400;
    fx.r.psw = FLAG_N | FLAG_V | FLAG_C;
    write_op(&fx, op_clrb(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "CLRB");
    ASSERT_EQ(fx.r.r[0], 0123400, "CLRB should clear low byte");
    expect_flags(&fx.r, 0, 1, 0, 0);

    set_test_name(namebuf, sizeof(namebuf), "comb", name);
    fx.r.r[0] = 0123400;
    write_op(&fx, op_comb(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "COMB");
    ASSERT_EQ(fx.r.r[0], 0123777, "COMB should invert low byte");
    expect_flags(&fx.r, 1, 0, 0, 1);

    set_test_name(namebuf, sizeof(namebuf), "incb", name);
    fx.r.r[0] = 0123777;
    write_op(&fx, op_incb(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "INCB");
    ASSERT_EQ(fx.r.r[0], 0123400, "INCB should increment low byte");
    expect_flags(&fx.r, 0, 1, 0, -1);

    set_test_name(namebuf, sizeof(namebuf), "decb", name);
    fx.r.r[0] = 0124200;
    write_op(&fx, op_decb(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "DECB");
    ASSERT_EQ(fx.r.r[0], 0124177, "DECB should decrement low byte");
    expect_flags(&fx.r, 0, 0, 1, -1);

    set_test_name(namebuf, sizeof(namebuf), "negb", name);
    fx.r.r[0] = 0000001;
    write_op(&fx, op_negb(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "NEGB");
    ASSERT_EQ(fx.r.r[0], 0000377, "NEGB should negate low byte");
    expect_flags(&fx.r, 1, 0, 0, 1);

    set_test_name(namebuf, sizeof(namebuf), "tstb", name);
    fx.r.r[0] = 0000000;
    fx.r.psw = FLAG_C | FLAG_V;
    write_op(&fx, op_tstb(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "TSTB");
    ASSERT_EQ(fx.r.r[0], 0000000, "TSTB should not modify");
    expect_flags(&fx.r, 0, 1, 0, -1);

    set_test_name(namebuf, sizeof(namebuf), "asrb", name);
    fx.r.r[0] = 0000001;
    fx.r.psw = 0;
    write_op(&fx, op_asrb(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "ASRB");
    ASSERT_EQ(fx.r.r[0] & 0377, 0, "ASRB should shift");
    expect_flags(&fx.r, 0, 1, 1, 1);

    set_test_name(namebuf, sizeof(namebuf), "aslb", name);
    fx.r.r[0] = 0000200;
    fx.r.psw = 0;
    write_op(&fx, op_aslb(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "ASLB");
    ASSERT_EQ(fx.r.r[0] & 0377, 0, "ASLB should shift");
    expect_flags(&fx.r, 0, 1, 1, 1);

    set_test_name(namebuf, sizeof(namebuf), "rorb", name);
    fx.r.r[0] = 0000001;
    fx.r.psw = FLAG_C;
    write_op(&fx, op_rorb(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "RORB");
    ASSERT_EQ(fx.r.r[0] & 0377, 0200, "RORB should rotate");
    expect_flags(&fx.r, 1, 0, 0, 1);

    set_test_name(namebuf, sizeof(namebuf), "rolb", name);
    fx.r.r[0] = 0000200;
    fx.r.psw = 1;
    write_op(&fx, op_rolb(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "ROLB");
    ASSERT_EQ(fx.r.r[0] & 0377, 1, "ROLB should rotate");
    expect_flags(&fx.r, 0, 0, 1, 1);

    set_test_name(namebuf, sizeof(namebuf), "adcb", name);
    fx.r.r[0] = 0000377;
    fx.r.psw = FLAG_C;
    write_op(&fx, op_adcb(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "ADCB");
    ASSERT_EQ(fx.r.r[0] & 0377, 0, "ADCB should add carry");
    expect_flags(&fx.r, 0, 1, 0, 1);

    set_test_name(namebuf, sizeof(namebuf), "sbcb", name);
    fx.r.r[0] = 0000000;
    fx.r.psw = FLAG_C;
    write_op(&fx, op_sbcb(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "SBCB");
    ASSERT_EQ(fx.r.r[0] & 0377, 0377, "SBCB should subtract carry");
    expect_flags(&fx.r, 1, 0, 0, 1);

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_single_operand_byte_ops(void)
{
    return run_for_models(test_single_operand_byte_ops_model);
}

static int test_misc_ops_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];

    fixture_setup_model(&fx, model);

    set_test_name(namebuf, sizeof(namebuf), "swab", name);
    fx.r.r[0] = 0123405;
    write_op(&fx, op_swab(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "SWAB");
    ASSERT_EQ(fx.r.r[0], 02647, "SWAB should swap bytes");
    expect_flags(&fx.r, 1, 0, 0, -1);

    set_test_name(namebuf, sizeof(namebuf), "sxt_set", name);
    fx.r.psw = FLAG_N;
    fx.r.r[0] = 0;
    write_op(&fx, op_sxt(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "SXT");
    ASSERT_EQ(fx.r.r[0], 0177777, "SXT should sign extend");
    expect_flags(&fx.r, 1, 0, 0, -1);

    set_test_name(namebuf, sizeof(namebuf), "sxt_clear", name);
    fx.r.psw = 0;
    fx.r.r[0] = 0177777;
    write_op(&fx, op_sxt(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "SXT");
    ASSERT_EQ(fx.r.r[0], 0, "SXT should clear");
    expect_flags(&fx.r, 0, 1, 0, -1);

    set_test_name(namebuf, sizeof(namebuf), "mfps_reg", name);
    fx.r.psw = 000345;
    write_op(&fx, op_mfps(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "MFPS");
    ASSERT_EQ(fx.r.r[0], 0177745, "MFPS should sign-extend into register");
    expect_flags(&fx.r, 1, 0, 0, -1);

    set_test_name(namebuf, sizeof(namebuf), "mfps_mem_byte", name);
    store_word(&fx, 02200, 012345);
    fx.r.psw = 000141;
    fx.r.r[1] = 02200;
    write_op(&fx, op_mfps(operand(1, 1)));
    ASSERT_EQ(core_step(&fx.r), 0, "MFPS");
    ASSERT_EQ(fx.r.load_byte(&fx.r, 02200), 0141, "MFPS should store low PSW byte");
    ASSERT_EQ(fx.r.load_byte(&fx.r, 02201), 024, "MFPS should keep high byte");
    expect_flags(&fx.r, 0, 0, 0, -1);

    set_test_name(namebuf, sizeof(namebuf), "mfps_autoinc_byte", name);
    store_word(&fx, 02220, 0);
    fx.r.psw = 000201;
    fx.r.r[2] = 02220;
    write_op(&fx, op_mfps(operand(2, 2)));
    ASSERT_EQ(core_step(&fx.r), 0, "MFPS");
    ASSERT_EQ(fx.r.r[2], 02221, "MFPS autoincrement must step by byte");
    ASSERT_EQ(fx.r.load_byte(&fx.r, 02220), 0201, "MFPS autoincrement value mismatch");
    expect_flags(&fx.r, 1, 0, 0, -1);

    set_test_name(namebuf, sizeof(namebuf), "mtps", name);
    fx.r.psw = 012340;
    fx.r.r[0] = 000007;
    write_op(&fx, op_mtps(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "MTPS");
    ASSERT_EQ(fx.r.psw & 0007, 0007, "MTPS should update low bits");
    ASSERT_EQ(fx.r.psw & (FLAG_T | FLAG_H), 012340 & (FLAG_T | FLAG_H),
              "MTPS should keep T/H");

    set_test_name(namebuf, sizeof(namebuf), "mtps_set_p", name);
    fx.r.psw = 012340;
    fx.r.r[0] = 0200;
    write_op(&fx, op_mtps(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "MTPS");
    ASSERT_EQ(fx.r.psw & FLAG_P, FLAG_P, "MTPS should set P");

    set_test_name(namebuf, sizeof(namebuf), "mtps_clear_p", name);
    fx.r.psw = 012340 | FLAG_P;
    fx.r.r[0] = 0000;
    write_op(&fx, op_mtps(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "MTPS");
    ASSERT_EQ(fx.r.psw & FLAG_P, 0, "MTPS should clear P");

    set_test_name(namebuf, sizeof(namebuf), "mtps_imm_set_p", name);
    fx.r.psw = 012340;
    store_word(&fx, TEST_BASE, op_mtps(operand(2, 7)));
    store_word(&fx, TEST_BASE + 2, 0200);
    fx.r.r[7] = TEST_BASE;
    ASSERT_EQ(core_step(&fx.r), 0, "MTPS");
    ASSERT_EQ(fx.r.psw & FLAG_P, FLAG_P, "MTPS immediate should set P");

    set_test_name(namebuf, sizeof(namebuf), "mtps_imm_clear_p", name);
    fx.r.psw = 012340 | FLAG_P;
    store_word(&fx, TEST_BASE, op_mtps(operand(2, 7)));
    store_word(&fx, TEST_BASE + 2, 0000);
    fx.r.r[7] = TEST_BASE;
    ASSERT_EQ(core_step(&fx.r), 0, "MTPS");
    ASSERT_EQ(fx.r.psw & FLAG_P, 0, "MTPS immediate should clear P");

    set_test_name(namebuf, sizeof(namebuf), "mtps_imm_keep_th", name);
    fx.r.psw = FLAG_T | FLAG_H | FLAG_P;
    store_word(&fx, TEST_BASE, op_mtps(operand(2, 7)));
    store_word(&fx, TEST_BASE + 2, 0000);
    fx.r.r[7] = TEST_BASE;
    fx.r.fTrap = 1;
    ASSERT_EQ(core_step(&fx.r), 0, "MTPS");
    ASSERT_EQ(fx.r.psw & (FLAG_T | FLAG_H), FLAG_T | FLAG_H,
              "MTPS immediate should keep T/H");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_misc_ops(void)
{
    return run_for_models(test_misc_ops_model);
}

static int test_double_operand_word_ops_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];

    fixture_setup_model(&fx, model);

    set_test_name(namebuf, sizeof(namebuf), "mov", name);
    fx.r.r[1] = 0100000;
    write_op(&fx, op_mov(operand(0, 1), operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "MOV");
    ASSERT_EQ(fx.r.r[0], 0100000, "MOV should copy");
    expect_flags(&fx.r, 1, 0, 0, -1);

    set_test_name(namebuf, sizeof(namebuf), "cmp", name);
    fx.r.r[0] = 3;
    fx.r.r[1] = 5;
    write_op(&fx, op_cmp(operand(0, 0), operand(0, 1)));
    ASSERT_EQ(core_step(&fx.r), 0, "CMP");
    expect_flags(&fx.r, 1, 0, 0, 1);

    set_test_name(namebuf, sizeof(namebuf), "bit", name);
    fx.r.psw = FLAG_C;
    fx.r.r[0] = 0040;
    fx.r.r[1] = 0060;
    write_op(&fx, op_bit(operand(0, 0), operand(0, 1)));
    ASSERT_EQ(core_step(&fx.r), 0, "BIT");
    expect_flags(&fx.r, 0, 0, 0, -1);

    set_test_name(namebuf, sizeof(namebuf), "bic", name);
    fx.r.r[0] = 0007;
    fx.r.r[1] = 0005;
    write_op(&fx, op_bic(operand(0, 0), operand(0, 1)));
    ASSERT_EQ(core_step(&fx.r), 0, "BIC");
    ASSERT_EQ(fx.r.r[1], 0000, "BIC should clear bits");
    expect_flags(&fx.r, 0, 1, 0, -1);

    set_test_name(namebuf, sizeof(namebuf), "bis", name);
    fx.r.r[0] = 0007;
    fx.r.r[1] = 0010;
    write_op(&fx, op_bis(operand(0, 0), operand(0, 1)));
    ASSERT_EQ(core_step(&fx.r), 0, "BIS");
    ASSERT_EQ(fx.r.r[1], 0017, "BIS should set bits");
    expect_flags(&fx.r, 0, 0, 0, -1);

    set_test_name(namebuf, sizeof(namebuf), "xor", name);
    fx.r.r[0] = 0003;
    fx.r.r[1] = 0005;
    write_op(&fx, op_xor(0, operand(0, 1)));
    ASSERT_EQ(core_step(&fx.r), 0, "XOR");
    ASSERT_EQ(fx.r.r[1], 0006, "XOR should apply reg");
    expect_flags(&fx.r, 0, 0, 0, -1);

    set_test_name(namebuf, sizeof(namebuf), "add", name);
    fx.r.r[0] = 1;
    fx.r.r[1] = 2;
    write_op(&fx, op_add(operand(0, 0), operand(0, 1)));
    ASSERT_EQ(core_step(&fx.r), 0, "ADD");
    ASSERT_EQ(fx.r.r[1], 3, "ADD should add");
    expect_flags(&fx.r, 0, 0, 0, 0);

    set_test_name(namebuf, sizeof(namebuf), "sub", name);
    fx.r.r[0] = 2;
    fx.r.r[1] = 1;
    write_op(&fx, op_sub(operand(0, 0), operand(0, 1)));
    ASSERT_EQ(core_step(&fx.r), 0, "SUB");
    ASSERT_EQ(fx.r.r[1], 0177777, "SUB should subtract");
    expect_flags(&fx.r, 1, 0, 0, 1);

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_double_operand_word_ops(void)
{
    return run_for_models(test_double_operand_word_ops_model);
}

static int test_double_operand_byte_ops_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];

    fixture_setup_model(&fx, model);

    set_test_name(namebuf, sizeof(namebuf), "movb", name);
    fx.r.r[1] = 0000200;
    write_op(&fx, op_movb(operand(0, 1), operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "MOVB");
    ASSERT_EQ(fx.r.r[0], 0177600, "MOVB should sign extend");
    expect_flags(&fx.r, 1, 0, 0, -1);

    set_test_name(namebuf, sizeof(namebuf), "cmpb", name);
    fx.r.r[0] = 0000001;
    fx.r.r[1] = 0000002;
    write_op(&fx, op_cmpb(operand(0, 0), operand(0, 1)));
    ASSERT_EQ(core_step(&fx.r), 0, "CMPB");
    expect_flags(&fx.r, 1, 0, 0, 1);

    set_test_name(namebuf, sizeof(namebuf), "bitb", name);
    fx.r.psw = FLAG_C;
    fx.r.r[0] = 0000001;
    fx.r.r[1] = 0000003;
    write_op(&fx, op_bitb(operand(0, 0), operand(0, 1)));
    ASSERT_EQ(core_step(&fx.r), 0, "BITB");
    expect_flags(&fx.r, 0, 0, 0, -1);

    set_test_name(namebuf, sizeof(namebuf), "bicb", name);
    fx.r.r[0] = 0000001;
    fx.r.r[1] = 0123403;
    write_op(&fx, op_bicb(operand(0, 0), operand(0, 1)));
    ASSERT_EQ(core_step(&fx.r), 0, "BICB");
    ASSERT_EQ(fx.r.r[1], 0123402, "BICB should clear low bits");
    expect_flags(&fx.r, 0, 0, 0, -1);

    set_test_name(namebuf, sizeof(namebuf), "bisb", name);
    fx.r.r[0] = 0000001;
    fx.r.r[1] = 0123402;
    write_op(&fx, op_bisb(operand(0, 0), operand(0, 1)));
    ASSERT_EQ(core_step(&fx.r), 0, "BISB");
    ASSERT_EQ(fx.r.r[1], 0123403, "BISB should set low bits");
    expect_flags(&fx.r, 0, 0, 0, -1);

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_double_operand_byte_ops(void)
{
    return run_for_models(test_double_operand_byte_ops_model);
}

static int test_vm2_privileged_ops_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];

    set_test_name(namebuf, sizeof(namebuf), "vm2_privileged", name);
    fixture_setup_model(&fx, model);

    fx.r.cpc = 01234;
    fx.r.cps = 00440 | FLAG_H;
    fx.r.psw = FLAG_P | FLAG_H;
    write_op(&fx, 0000012);
    ASSERT_EQ(core_step(&fx.r), 0, "START");
    ASSERT_EQ(fx.r.r[7], 01234, "START should load CPC");
    ASSERT_EQ(fx.r.psw, (word)(00440 | FLAG_H), "START should load CPS");

    fx.r.cpc = 02000;
    fx.r.cps = 000200 | FLAG_H;
    fx.r.psw = FLAG_P | FLAG_H;
    write_op(&fx, 0000016);
    ASSERT_EQ(core_step(&fx.r), 0, "STEP");
    ASSERT_EQ(fx.r.r[7], 02000, "STEP should load CPC");
    ASSERT_EQ(fx.r.psw, (word)(000200 | FLAG_H), "STEP should load CPS");

    fx.r.SEL0 = 0777;
    fx.r.psw = FLAG_H;
    write_op(&fx, 0000020);
    ASSERT_EQ(core_step(&fx.r), 0, "RSEL");
    ASSERT_EQ(fx.r.r[0], 0777, "RSEL should read SEL0");

    fx.r.r[5] = 03000;
    store_word(&fx, 03000, 01234);
    fx.r.psw = FLAG_H;
    write_op(&fx, 0000021);
    ASSERT_EQ(core_step(&fx.r), 0, "MFUS");
    ASSERT_EQ(fx.r.r[0], 01234, "MFUS should load via R5");
    ASSERT_EQ(fx.r.r[5], 03002, "MFUS should increment R5");
    ASSERT_EQ(fx.r.psw, FLAG_H, "MFUS should preserve PSW");

    fx.r.cpc = 05555;
    fx.r.psw = FLAG_P | FLAG_H;
    write_op(&fx, 0000022);
    ASSERT_EQ(core_step(&fx.r), 0, "RCPC");
    ASSERT_EQ(fx.r.r[0], 05555, "RCPC should read CPC");

    fx.r.cps = 00666;
    fx.r.psw = FLAG_P | FLAG_H;
    write_op(&fx, 0000024);
    ASSERT_EQ(core_step(&fx.r), 0, "RCPS");
    ASSERT_EQ(fx.r.r[0], 00666, "RCPS should read CPS");

    fx.r.r[5] = 04002;
    fx.r.r[0] = 07777;
    fx.r.psw = FLAG_H;
    write_op(&fx, 0000031);
    ASSERT_EQ(core_step(&fx.r), 0, "MTUS");
    ASSERT_EQ(fx.r.r[5], 04000, "MTUS should decrement R5");
    ASSERT_EQ(fx.r.load_word(&fx.r, 04000), 07777, "MTUS should store via R5");
    ASSERT_EQ(fx.r.psw, FLAG_H, "MTUS should preserve PSW");

    fx.r.psw = FLAG_P | FLAG_H;
    fx.r.r[0] = 01111;
    write_op(&fx, 0000032);
    ASSERT_EQ(core_step(&fx.r), 0, "WCPC");
    ASSERT_EQ(fx.r.cpc, 01111, "WCPC should write CPC");

    fx.r.psw = FLAG_P | FLAG_H;
    fx.r.r[0] = 02222;
    write_op(&fx, 0000034);
    ASSERT_EQ(core_step(&fx.r), 0, "WCPS");
    ASSERT_EQ(fx.r.cps, 02222, "WCPS should write CPS");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_vm2_privileged_ops(void)
{
    int rc = 0;

    rc += test_vm2_privileged_ops_model(K1801VM2, "K1801VM2");
    rc += test_vm2_privileged_ops_model(K1806VM2, "K1806VM2");

    return rc;
}

static int test_vm2_halt_ops_illegal_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word handler = 02600;
    const word new_psw = 000340;

    if (!is_vm2_model(model)) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "vm2_halt_ops_illegal", name);
    fixture_setup_model(&fx, model);

    store_word(&fx, 000010, handler);
    store_word(&fx, 000012, new_psw);
    fx.r.r[6] = 01000;
    fx.r.psw = 000000;
    write_op(&fx, 0000020); /* RSEL */

    ASSERT_EQ(core_step(&fx.r), 0, "RSEL in USER should trap");
    ASSERT_EQ(fx.r.r[7], handler, "PC should load illegal vector");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should load illegal vector");
    ASSERT_EQ(fx.r.r[6], 00774, "SP should push two words");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE + 2, "Stack PC incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 000000, "Stack PSW incorrect");
    fixture_teardown(&fx);

    set_test_name(namebuf, sizeof(namebuf), "vm2_halt_ops_pmask", name);
    fixture_setup_model(&fx, model);
    store_word(&fx, 000010, handler);
    store_word(&fx, 000012, new_psw);
    fx.r.r[6] = 01000;
    fx.r.psw = FLAG_H;
    write_op(&fx, 0000012); /* START */
    ASSERT_EQ(core_step(&fx.r), 0, "START without P should trap");
    ASSERT_EQ(fx.r.r[7], handler, "PC should load illegal vector");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should load illegal vector");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_vm2_start_irq_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word handler = 02400;
    const word new_psw = 000200;
    const word program[] = {
        0000012, /* START */
    };

    if (!is_vm2_model(model)) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "vm2_start_irq", name);
    fixture_setup_model(&fx, model);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000060, handler);
    store_word(&fx, 000062, new_psw);
    store_word(&fx, handler, op_nop());

    fx.r.r[6] = 01000;
    fx.r.cpc = 02000;
    fx.r.cps = 000000;
    fx.r.psw = FLAG_P | FLAG_H;
    fx.r.poll_irq = test_poll_irq_vector;
    test_irq_vector = 000060;
    test_irq_pending = 1;

    ASSERT_EQ(core_step(&fx.r), 0, "START should execute");
    ASSERT_EQ(fx.r.r[7], handler, "START should enter IRQ before first instruction");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should load IRQ vector");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), 02000, "Stack PC should be CPC");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 000000, "Stack PSW should be CPSW");

cleanup:
    fixture_teardown(&fx);
    test_irq_pending = 0;
    test_irq_vector = 0;
    return rc;
}

static int test_vm2_step_no_irq_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word handler = 02400;
    const word new_psw = 000200;
    const word program[] = {
        0000016, /* STEP */
    };

    if (!is_vm2_model(model)) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "vm2_step_no_irq", name);
    fixture_setup_model(&fx, model);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000060, handler);
    store_word(&fx, 000062, new_psw);
    store_word(&fx, handler, op_nop());

    fx.r.r[6] = 01000;
    fx.r.cpc = 02000;
    fx.r.cps = 000000;
    fx.r.psw = FLAG_P | FLAG_H;
    fx.r.poll_irq = test_poll_irq_vector;
    test_irq_vector = 000060;
    test_irq_pending = 1;

    ASSERT_EQ(core_step(&fx.r), 0, "STEP should execute");
    ASSERT_EQ(fx.r.r[7], 02000, "STEP should load CPC");
    ASSERT_EQ(fx.r.psw, 000000, "STEP should load CPSW");
    ASSERT_EQ(test_irq_pending, 1, "STEP should not consume IRQ");

cleanup:
    fixture_teardown(&fx);
    test_irq_pending = 0;
    test_irq_vector = 0;
    return rc;
}

static int test_vm2_step_halt_signal_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word handler = 02400;
    const word new_psw = 000200;
    const word vec = 0570;
    const word program[] = {
        0000016, /* STEP */
    };

    if (!is_vm2_model(model)) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "vm2_step_halt", name);
    fixture_setup_model(&fx, model);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    fx.r.SEL0 = 0400;
    store_word(&fx, vec, handler);
    store_word(&fx, vec + 2, new_psw);
    store_word(&fx, 02000, op_nop());

    fx.r.r[6] = 01000;
    fx.r.cpc = 02000;
    fx.r.cps = 000000;
    fx.r.psw = FLAG_P | FLAG_H;

    ASSERT_EQ(core_step(&fx.r), 0, "STEP should execute");
    ASSERT_EQ(fx.r.r[7], 02000, "STEP should load CPC");
    ASSERT_EQ(fx.r.psw, 000000, "STEP should load CPSW");

    fx.r.fHaltSignal = 1;
    ASSERT_EQ(core_step(&fx.r), 0, "Instruction after STEP should execute");
    ASSERT_EQ(fx.r.cpc, 02002, "HALT should capture CPC after first instruction");
    ASSERT_EQ(fx.r.cps, 000000, "HALT should capture CPSW after first instruction");
    ASSERT_EQ(fx.r.r[7], handler, "HALT signal should vector after first instruction");
    ASSERT_EQ(fx.r.psw, new_psw, "HALT signal should load PSW");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_vm2_step_halt_masked_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word program[] = {
        0000016, /* STEP */
    };

    if (!is_vm2_model(model)) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "vm2_step_halt_masked", name);
    fixture_setup_model(&fx, model);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));
    store_word(&fx, 02000, op_nop());

    fx.r.r[6] = 01000;
    fx.r.cpc = 02000;
    fx.r.cps = FLAG_H;
    fx.r.psw = FLAG_P | FLAG_H;

    ASSERT_EQ(core_step(&fx.r), 0, "STEP should execute");
    ASSERT_EQ(fx.r.r[7], 02000, "STEP should load CPC");
    ASSERT_EQ(fx.r.psw, FLAG_H, "STEP should load CPSW");

    fx.r.fHaltSignal = 1;
    ASSERT_EQ(core_step(&fx.r), 0, "Instruction after STEP should execute");
    ASSERT_EQ(fx.r.r[7], 02002, "HALT should be masked when H=1");
    ASSERT_EQ(fx.r.fHaltSignal, 0, "HALT signal should be consumed");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_vm2_step_halt_unmasked_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word handler = 02400;
    const word new_psw = 000200;
    const word vec = 0570;
    const word program[] = {
        0000016, /* STEP */
    };

    if (!is_vm2_model(model)) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "vm2_step_halt_unmasked", name);
    fixture_setup_model(&fx, model);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    fx.r.SEL0 = 0400;
    store_word(&fx, vec, handler);
    store_word(&fx, vec + 2, new_psw);
    store_word(&fx, 02000, op_nop());

    fx.r.r[6] = 01000;
    fx.r.cpc = 02000;
    fx.r.cps = FLAG_P;
    fx.r.psw = FLAG_P | FLAG_H;

    ASSERT_EQ(core_step(&fx.r), 0, "STEP should execute");
    ASSERT_EQ(fx.r.r[7], 02000, "STEP should load CPC");
    ASSERT_EQ(fx.r.psw, FLAG_P, "STEP should load CPSW");

    fx.r.fHaltSignal = 1;
    ASSERT_EQ(core_step(&fx.r), 0, "Instruction after STEP should execute");
    ASSERT_EQ(fx.r.r[7], handler, "HALT should vector when H=0");
    ASSERT_EQ(fx.r.psw, new_psw, "HALT should load PSW");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_vm2_step_halt_unmasked_p0_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word handler = 02400;
    const word new_psw = 000200;
    const word vec = 0570;
    const word program[] = {
        0000016, /* STEP */
    };

    if (!is_vm2_model(model)) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "vm2_step_halt_unmasked_p0", name);
    fixture_setup_model(&fx, model);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    fx.r.SEL0 = 0400;
    store_word(&fx, vec, handler);
    store_word(&fx, vec + 2, new_psw);
    store_word(&fx, 02000, op_nop());

    fx.r.r[6] = 01000;
    fx.r.cpc = 02000;
    fx.r.cps = 000000;
    fx.r.psw = FLAG_P | FLAG_H;

    ASSERT_EQ(core_step(&fx.r), 0, "STEP should execute");
    ASSERT_EQ(fx.r.r[7], 02000, "STEP should load CPC");
    ASSERT_EQ(fx.r.psw, 000000, "STEP should load CPSW");

    fx.r.fHaltSignal = 1;
    ASSERT_EQ(core_step(&fx.r), 0, "Instruction after STEP should execute");
    ASSERT_EQ(fx.r.r[7], handler, "HALT should vector when H=0,P=0");
    ASSERT_EQ(fx.r.psw, new_psw, "HALT should load PSW");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_vm2_fis_trap_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word handler = 02400;
    const word new_psw = FLAG_P | FLAG_H;
    const word vec = 0410;
    const word program[] = {
        op_fis(0),
    };

    if (!is_vm2_model(model)) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "vm2_fis_trap", name);
    fixture_setup_model(&fx, model);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    fx.r.SEL0 = 0400;
    store_word(&fx, vec, handler);
    store_word(&fx, vec + 2, new_psw);
    store_word(&fx, handler, op_nop());

    fx.r.r[6] = 01000;
    fx.r.psw = FLAG_H;

    ASSERT_EQ(core_step(&fx.r), 0, "FIS should trap to SEL010");
    ASSERT_EQ(fx.r.r[7], handler, "FIS should vector to SEL010");
    ASSERT_EQ(fx.r.psw, new_psw, "FIS should load PSW from vector");
    ASSERT_EQ(fx.r.cpc, TEST_BASE + 2, "FIS should save CPC");
    ASSERT_EQ(fx.r.cps, FLAG_H, "FIS should save CPSW");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_vm2_fis_missing_traps_illegal_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word handler = 02400;
    const word new_psw = 000340;
    const word program[] = {
        op_fis(0),
    };

    if (!is_vm2_model(model)) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "vm2_fis_missing", name);
    fixture_setup_model(&fx, model);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000010, handler);
    store_word(&fx, 000012, new_psw);
    fx.r.SEL0 = 0200;
    fx.r.r[6] = 01000;
    fx.r.psw = 000000;

    ASSERT_EQ(core_step(&fx.r), 0, "FIS missing should trap illegal");
    ASSERT_EQ(fx.r.r[7], handler, "Illegal vector should load");
    ASSERT_EQ(fx.r.psw, new_psw, "Illegal PSW should load");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_vm2_fis_error_trap_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word handler = 02400;
    const word new_psw = 000200;
    const word program[] = {
        op_fis(030), /* FDIV R0 */
    };

    if (!is_vm2_model(model)) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "vm2_fis_error", name);
    fixture_setup_model(&fx, model);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 0244, handler);
    store_word(&fx, 0246, new_psw);
    store_word(&fx, handler, op_nop());

    /* Настройка R0 для FDIV: a=0.0 (делитель), b=0.0 (делимое/код ошибки) */
    fx.r.r[0] = 01200;
    store_word(&fx, 01200, 0); /* a_w0 */
    store_word(&fx, 01202, 0); /* a_w1 */
    store_word(&fx, 01204, 0); /* b_w0 (код ошибки) */
    store_word(&fx, 01206, 0); /* b_w1 */

    fx.r.r[6] = 01000;
    fx.r.psw = 000000;
    fx.r.has_fis = 1;

    ASSERT_EQ(core_step(&fx.r), 0, "FIS error should trap to 0244");
    ASSERT_EQ(fx.r.r[7], handler, "FIS error should load vector");
    ASSERT_EQ(fx.r.psw, new_psw, "FIS error should load PSW");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE + 2, "Stack PC after FIS error");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 000013, "Stack error code after FIS error");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_fis_odd_register_allowed_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word base = 01200;
    const word program[] = {
        op_fis(001), /* FADD R1 */
    };

    if (is_vm1_model(model)) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "fis_odd_register", name);
    fixture_setup_model(&fx, model);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    fx.r.has_fis = 1;
    fx.r.r[1] = base;
    fx.r.psw = FLAG_N | FLAG_V | FLAG_C;
    store_word(&fx, base + 0, 0);
    store_word(&fx, base + 2, 0);
    store_word(&fx, base + 4, 0);
    store_word(&fx, base + 6, 0);

    ASSERT_EQ(core_step(&fx.r), 0, "FIS with odd register should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "FIS with odd register should not trap");
    ASSERT_EQ(fx.r.r[1], base + 4, "FIS should post-increment register by 4");
    expect_flags(&fx.r, 0, 1, 0, 0);

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_fp11_divf_divzero_trap(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 02400;
    const word new_psw = 000340;
    const word program[] = {
        op_fp11(011, 0, operand(0, 0)), /* DIVF F0,F0 */
    };

    current_test = "fp11_divf_divzero_trap";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 0244, handler);
    store_word(&fx, 0246, new_psw);

    fx.r.has_fpu = 1;
    fx.r.r[6] = 01000;
    fx.r.psw = 000003;
    fx.r.fpu_fr[0].h = 0;
    fx.r.fpu_fr[0].l = 0;

    ASSERT_EQ(core_step(&fx.r), 0, "FP11 DIVF by zero should trap");
    ASSERT_EQ(fx.r.r[7], handler, "FP11 DIVF should vector to 0244");
    ASSERT_EQ(fx.r.psw, new_psw, "FP11 DIVF should load PSW from 0246");
    ASSERT_EQ(fx.r.r[6], 00774, "FP11 trap should push two words");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE + 2, "FP11 trap frame PC incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 000003, "FP11 trap frame PSW incorrect");
    ASSERT_EQ(fx.r.fpu_fec, 000004, "FEC should record divide-by-zero");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_special_ops(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word src_addr = 02000;
    const word dst_addr = 02100;

    current_test = "dcj11_special_ops";
    fixture_setup_model(&fx, DCJ11);

    fx.r.psw = FLAG_N | FLAG_Z | FLAG_V | FLAG_C;
    write_op(&fx, op_mfpt());
    ASSERT_EQ(core_step(&fx.r), 0, "MFPT should execute");
    ASSERT_EQ(fx.r.r[0], 5, "MFPT should set R0 to 5");
    ASSERT_EQ(fx.r.psw & (FLAG_N | FLAG_Z | FLAG_V | FLAG_C),
              (FLAG_N | FLAG_Z | FLAG_V | FLAG_C),
              "MFPT should not change CC");

    store_word(&fx, src_addr, 01234);
    write_op(&fx, op_mfpd(operand(1, 1)));
    fx.r.r[1] = src_addr;
    fx.r.r[6] = 01000;
    ASSERT_EQ(core_step(&fx.r), 0, "MFPD should execute");
    ASSERT_EQ(fx.r.r[6], 00776, "MFPD should push word");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 01234, "MFPD stack value");
    ASSERT_EQ(is_flag_set(&fx.r, FLAG_N), 0, "MFPD should clear N");
    ASSERT_EQ(is_flag_set(&fx.r, FLAG_Z), 0, "MFPD should clear Z");
    ASSERT_EQ(is_flag_set(&fx.r, FLAG_V), 0, "MFPD should clear V");

    store_word(&fx, src_addr, 04567);
    write_op(&fx, op_mfpi(operand(1, 1)));
    fx.r.r[1] = src_addr;
    fx.r.r[6] = 01000;
    ASSERT_EQ(core_step(&fx.r), 0, "MFPI should execute");
    ASSERT_EQ(fx.r.r[6], 00776, "MFPI should push word");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 04567, "MFPI stack value");
    ASSERT_EQ(is_flag_set(&fx.r, FLAG_N), 0, "MFPI should clear N");
    ASSERT_EQ(is_flag_set(&fx.r, FLAG_Z), 0, "MFPI should clear Z");
    ASSERT_EQ(is_flag_set(&fx.r, FLAG_V), 0, "MFPI should clear V");

    store_word(&fx, TEST_STACK, 01234);
    write_op(&fx, op_mtpi(operand(1, 2)));
    fx.r.r[2] = dst_addr;
    fx.r.r[6] = TEST_STACK;
    ASSERT_EQ(core_step(&fx.r), 0, "MTPI should execute");
    ASSERT_EQ(fx.r.r[6], TEST_STACK + 2, "MTPI should pop word");
    ASSERT_EQ(fx.r.load_word(&fx.r, dst_addr), 01234, "MTPI should store word");
    ASSERT_EQ(is_flag_set(&fx.r, FLAG_N), 0, "MTPI should clear N");
    ASSERT_EQ(is_flag_set(&fx.r, FLAG_Z), 0, "MTPI should clear Z");
    ASSERT_EQ(is_flag_set(&fx.r, FLAG_V), 0, "MTPI should clear V");

    store_word(&fx, TEST_STACK, 04567);
    write_op(&fx, op_mtpd(operand(1, 2)));
    fx.r.r[2] = dst_addr;
    fx.r.r[6] = TEST_STACK;
    ASSERT_EQ(core_step(&fx.r), 0, "MTPD should execute");
    ASSERT_EQ(fx.r.r[6], TEST_STACK + 2, "MTPD should pop word");
    ASSERT_EQ(fx.r.load_word(&fx.r, dst_addr), 04567, "MTPD should store word");
    ASSERT_EQ(is_flag_set(&fx.r, FLAG_N), 0, "MTPD should clear N");
    ASSERT_EQ(is_flag_set(&fx.r, FLAG_Z), 0, "MTPD should clear Z");
    ASSERT_EQ(is_flag_set(&fx.r, FLAG_V), 0, "MTPD should clear V");

    store_word(&fx, dst_addr, 000000);
    write_op(&fx, op_tstset(operand(1, 2)));
    fx.r.r[2] = dst_addr;
    ASSERT_EQ(core_step(&fx.r), 0, "TSTSET should execute");
    ASSERT_EQ(fx.r.r[0], 0, "TSTSET should load R0");
    ASSERT_EQ(fx.r.load_word(&fx.r, dst_addr), 000001, "TSTSET should set low bit");
    ASSERT_EQ(is_flag_set(&fx.r, FLAG_C), 0, "TSTSET should clear C when bit0 was 0");
    ASSERT_EQ(is_flag_set(&fx.r, FLAG_N), 0, "TSTSET should clear N when R0 >= 0");
    ASSERT_EQ(is_flag_set(&fx.r, FLAG_Z), 1, "TSTSET should set Z when R0 == 0");
    ASSERT_EQ(is_flag_set(&fx.r, FLAG_V), 0, "TSTSET should clear V");

    store_word(&fx, dst_addr, 000001);
    write_op(&fx, op_tstset(operand(1, 2)));
    fx.r.r[2] = dst_addr;
    ASSERT_EQ(core_step(&fx.r), 0, "TSTSET should execute");
    ASSERT_EQ(fx.r.r[0], 1, "TSTSET should load R0");
    ASSERT_EQ(fx.r.load_word(&fx.r, dst_addr), 000001, "TSTSET should keep bit0 set");
    ASSERT_EQ(is_flag_set(&fx.r, FLAG_C), 1, "TSTSET should set C when bit0 was 1");
    ASSERT_EQ(is_flag_set(&fx.r, FLAG_N), 0, "TSTSET should clear N when R0 >= 0");
    ASSERT_EQ(is_flag_set(&fx.r, FLAG_Z), 0, "TSTSET should clear Z when R0 != 0");
    ASSERT_EQ(is_flag_set(&fx.r, FLAG_V), 0, "TSTSET should clear V");

    fx.r.r[0] = 01234;
    write_op(&fx, op_wrtlck(operand(1, 2)));
    fx.r.r[2] = dst_addr;
    ASSERT_EQ(core_step(&fx.r), 0, "WRTLCK should execute");
    ASSERT_EQ(fx.r.load_word(&fx.r, dst_addr), 01234, "WRTLCK should store R0");
    ASSERT_EQ(is_flag_set(&fx.r, FLAG_N), 0, "WRTLCK should clear N when R0 >= 0");
    ASSERT_EQ(is_flag_set(&fx.r, FLAG_Z), 0, "WRTLCK should clear Z when R0 != 0");
    ASSERT_EQ(is_flag_set(&fx.r, FLAG_V), 0, "WRTLCK should clear V");

    fx.r.r[0] = 0100000;
    write_op(&fx, op_wrtlck(operand(1, 2)));
    fx.r.r[2] = dst_addr;
    ASSERT_EQ(core_step(&fx.r), 0, "WRTLCK should execute");
    ASSERT_EQ(fx.r.load_word(&fx.r, dst_addr), 0100000, "WRTLCK should store negative R0");
    ASSERT_EQ(is_flag_set(&fx.r, FLAG_N), 1, "WRTLCK should set N when R0 < 0");
    ASSERT_EQ(is_flag_set(&fx.r, FLAG_Z), 0, "WRTLCK should clear Z when R0 != 0");
    ASSERT_EQ(is_flag_set(&fx.r, FLAG_V), 0, "WRTLCK should clear V");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_mxpi_prev_mode2_uses_user_sp(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word kernel_sp = 01400;
    const word user_sp = 01234;
    const word psw_pm2 = 0020000; /* PM=2, CM=0 */

    current_test = "dcj11_mxpi_pm2_user_sp";
    fixture_setup_model(&fx, DCJ11);

    fx.r.sp_mode_init = 1;
    fx.r.sp_mode[0] = kernel_sp;
    fx.r.sp_mode[1] = 01100;
    fx.r.sp_mode[3] = user_sp;
    fx.r.r[6] = kernel_sp;
    fx.r.psw = psw_pm2;

    write_op(&fx, op_mfpi(operand(0, 6))); /* MFPI R6 */
    ASSERT_EQ(core_step(&fx.r), 0, "MFPI R6 should execute");
    ASSERT_EQ(fx.r.r[6], kernel_sp - 2, "MFPI should push on current kernel stack");
    ASSERT_EQ(fx.r.load_word(&fx.r, kernel_sp - 2), user_sp,
              "MFPI PM=2 should read user stack pointer");
    ASSERT_EQ(fx.r.sp_mode[3], user_sp, "MFPI should not modify user SP bank");

    fx.r.r[6] = kernel_sp;
    fx.r.sp_mode[0] = kernel_sp;
    fx.r.sp_mode[3] = user_sp;
    fx.r.psw = psw_pm2;
    write_op(&fx, op_mfpd(operand(0, 6))); /* MFPD R6 */
    ASSERT_EQ(core_step(&fx.r), 0, "MFPD R6 should execute");
    ASSERT_EQ(fx.r.r[6], kernel_sp - 2, "MFPD should push on current kernel stack");
    ASSERT_EQ(fx.r.load_word(&fx.r, kernel_sp - 2), user_sp,
              "MFPD PM=2 should read user stack pointer");
    ASSERT_EQ(fx.r.sp_mode[3], user_sp, "MFPD should not modify user SP bank");

    fx.r.r[6] = kernel_sp;
    fx.r.sp_mode[0] = kernel_sp;
    fx.r.sp_mode[3] = user_sp;
    fx.r.psw = psw_pm2;
    store_word(&fx, kernel_sp, 06543);
    write_op(&fx, op_mtpi(operand(0, 6))); /* MTPI R6 */
    ASSERT_EQ(core_step(&fx.r), 0, "MTPI R6 should execute");
    ASSERT_EQ(fx.r.r[6], kernel_sp + 2, "MTPI should pop from current kernel stack");
    ASSERT_EQ(fx.r.sp_mode[3], 06543, "MTPI PM=2 should write user SP bank");

    fx.r.r[6] = kernel_sp;
    fx.r.sp_mode[0] = kernel_sp;
    fx.r.sp_mode[3] = user_sp;
    fx.r.psw = psw_pm2;
    store_word(&fx, kernel_sp, 07654);
    write_op(&fx, op_mtpd(operand(0, 6))); /* MTPD R6 */
    ASSERT_EQ(core_step(&fx.r), 0, "MTPD R6 should execute");
    ASSERT_EQ(fx.r.r[6], kernel_sp + 2, "MTPD should pop from current kernel stack");
    ASSERT_EQ(fx.r.sp_mode[3], 07654, "MTPD PM=2 should write user SP bank");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_csm_disabled_illegal(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 04000;
    const word vec_psw = 000340;
    const word program[] = {
        op_csm(operand(0, 2)),
    };

    current_test = "dcj11_csm_disabled_illegal";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000010, handler);
    store_word(&fx, 000012, vec_psw);

    fx.r.psw = 0140000;
    fx.r.r[2] = 012345;
    fx.r.r[6] = 01000;

    ASSERT_EQ(core_step(&fx.r), 0, "CSM should trap when MMR3<3> is clear");
    ASSERT_EQ(fx.r.r[7], handler, "CSM disabled should take illegal instruction vector");
    ASSERT_EQ((fx.r.psw >> 14) & 03, 0, "illegal trap should enter kernel mode");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_csm_user_to_supervisor(void)
{
#if !defined(ENABLE_MMU) || !(ENABLE_MMU)
    return 0;
#else
    cpu_fixture fx;
    int rc = 0;
    const word handler = 05000;
    const word program[] = {
        op_csm(operand(0, 2)),
    };
    const word old_psw = 0140017;

    current_test = "dcj11_csm_user_to_supervisor";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000010, handler);
    store_word(&fx, 000012, 000000);

    fx.r.mmu_ssr3 = 0000010; /* MMR3<3>: CSM enable */
    fx.r.psw = old_psw;
    fx.r.r[2] = 012345;
    fx.r.r[6] = 01000;

    ASSERT_EQ(core_step(&fx.r), 0, "CSM should execute in user mode when enabled");
    ASSERT_EQ(fx.r.r[7], handler, "CSM should load PC from vector 010");
    ASSERT_EQ((fx.r.psw >> 14) & 03, 1, "CSM should enter supervisor mode");
    ASSERT_EQ((fx.r.psw >> 12) & 03, 3, "CSM should record previous mode as user");
    ASSERT_EQ(fx.r.psw & FLAG_T, 0, "CSM should clear T bit");
    ASSERT_EQ(fx.r.psw & (FLAG_N | FLAG_Z | FLAG_V | FLAG_C),
              old_psw & (FLAG_N | FLAG_Z | FLAG_V | FLAG_C),
              "CSM should preserve condition codes");

    ASSERT_EQ(fx.r.r[6], 00772, "CSM should push 3-word frame on supervisor stack");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00772), 012345, "CSM frame word 0 should be operand");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE + 2,
              "CSM frame word 1 should be return PC");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776),
              old_psw & ~(FLAG_N | FLAG_Z | FLAG_V | FLAG_C),
              "CSM frame word 2 should be PSW with CC cleared");

cleanup:
    fixture_teardown(&fx);
    return rc;
#endif
}

static int test_dcj11_mmr1_jsr_records_sp_delta(void)
{
#if !defined(ENABLE_MMU) || !(ENABLE_MMU)
    return 0;
#else
    cpu_fixture fx;
    int rc = 0;
    const word target = 02400;
    const word program[] = {
        op_jsr(5, operand(1, 2)), /* JSR R5,(R2) */
    };

    current_test = "dcj11_mmr1_jsr_sp_delta";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    fx.r.psw = 000000;
    fx.r.r[2] = target;
    fx.r.r[5] = 012345;
    fx.r.r[6] = 01000;

    ASSERT_EQ(core_step(&fx.r), 0, "JSR should execute");
    ASSERT_EQ(fx.r.r[7], target, "JSR should jump to destination");
    ASSERT_EQ(fx.r.r[5], TEST_BASE + 2, "JSR should save return PC in source register");
    ASSERT_EQ(fx.r.r[6], 00776, "JSR should push one word to stack");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 012345, "JSR should push source register value");
    ASSERT_EQ(fx.r.mmu_ssr1, 000366, "MMR1 should record SP autodecrement for JSR");

cleanup:
    fixture_teardown(&fx);
    return rc;
#endif
}

static int test_dcj11_mmr1_mfpi_records_sp_delta(void)
{
#if !defined(ENABLE_MMU) || !(ENABLE_MMU)
    return 0;
#else
    cpu_fixture fx;
    int rc = 0;
    const word program[] = {
        op_mfpi(operand(0, 1)), /* MFPI R1 */
    };

    current_test = "dcj11_mmr1_mfpi_sp_delta";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    fx.r.psw = 000000;
    fx.r.r[1] = 065432;
    fx.r.r[6] = 01000;

    ASSERT_EQ(core_step(&fx.r), 0, "MFPI should execute");
    ASSERT_EQ(fx.r.r[6], 00776, "MFPI should push one word to stack");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 065432, "MFPI should push source value");
    ASSERT_EQ(fx.r.mmu_ssr1, 000366, "MMR1 should record SP autodecrement for MFPI");

cleanup:
    fixture_teardown(&fx);
    return rc;
#endif
}

static int test_dcj11_mmr1_mxpi_pop_records_sp_delta(void)
{
#if !defined(ENABLE_MMU) || !(ENABLE_MMU)
    return 0;
#else
    cpu_fixture fx;
    int rc = 0;
    const word stack_addr = 02000;
    const word program_mtpi[] = {
        op_mtpi(operand(0, 2)), /* MTPI R2 */
    };
    const word program_mtpd[] = {
        op_mtpd(operand(0, 3)), /* MTPD R3 */
    };

    current_test = "dcj11_mmr1_mxpi_pop_sp_delta";
    fixture_setup_model(&fx, DCJ11);

    load_program(&fx, TEST_BASE, program_mtpi, sizeof(program_mtpi) / sizeof(program_mtpi[0]));
    fx.r.psw = 000000;
    fx.r.r[2] = 0;
    fx.r.r[6] = stack_addr;
    store_word(&fx, stack_addr, 012345);
    ASSERT_EQ(core_step(&fx.r), 0, "MTPI should execute");
    ASSERT_EQ(fx.r.r[2], 012345, "MTPI should pop into destination register");
    ASSERT_EQ(fx.r.r[6], stack_addr + 2, "MTPI should pop one word from stack");
    ASSERT_EQ(fx.r.mmu_ssr1, 000026, "MMR1 should record SP autoincrement for MTPI");

    load_program(&fx, TEST_BASE, program_mtpd, sizeof(program_mtpd) / sizeof(program_mtpd[0]));
    fx.r.r[3] = 0;
    fx.r.r[6] = stack_addr;
    store_word(&fx, stack_addr, 076543);
    ASSERT_EQ(core_step(&fx.r), 0, "MTPD should execute");
    ASSERT_EQ(fx.r.r[3], 076543, "MTPD should pop into destination register");
    ASSERT_EQ(fx.r.r[6], stack_addr + 2, "MTPD should pop one word from stack");
    ASSERT_EQ(fx.r.mmu_ssr1, 000026, "MMR1 should record SP autoincrement for MTPD");

cleanup:
    fixture_teardown(&fx);
    return rc;
#endif
}

static int test_dcj11_mmr1_autoinc_deferred_records_before_fault(void)
{
#if !defined(ENABLE_MMU) || !(ENABLE_MMU)
    return 0;
#else
    cpu_fixture fx;
    int rc = 0;
    const word handler = 04000;
    const word vec_psw = 000340;
    const word program[] = {
        op_mov(operand(3, 0), operand(0, 1)), /* MOV @(R0)+,R1 */
    };

    current_test = "dcj11_mmr1_mode3_fault_order";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000004, handler);
    store_word(&fx, 000006, vec_psw);

    fx.r.psw = 000003;
    fx.r.r[0] = 000001; /* odd -> fault on deferred pointer read */
    fx.r.r[1] = 012345;
    fx.r.r[6] = 01000;

    ASSERT_EQ(core_step(&fx.r), 0, "MOV @(R0)+ should fault on odd pointer read");
    ASSERT_EQ(fx.r.r[7], handler, "Fault should vector to bus error handler");
    ASSERT_EQ(fx.r.r[0], 000003, "Autoincrement must be visible even when deferred read faults");
    ASSERT_EQ(fx.r.r[1], 012345, "Destination register should remain unchanged on abort");
    ASSERT_EQ(fx.r.mmu_ssr1, 000020, "MMR1 should record source autoincrement delta before fault");

cleanup:
    fixture_teardown(&fx);
    return rc;
#endif
}

static int test_dcj11_mmu_illegal_mode2_aborts(void)
{
#if !defined(ENABLE_MMU) || !(ENABLE_MMU)
    return 0;
#else
    cpu_fixture fx;
    int rc = 0;
    const word handler = 05200;
    const word vec_psw = 000340;
    const word program[] = {
        op_nop(),
    };
    word ssr0_page;

    current_test = "dcj11_mmu_illegal_mode2_aborts";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000250, handler);
    store_word(&fx, 000252, vec_psw);

    fx.r.psw = 0100000;     /* PSW<15:14> = 2 (illegal MMU mode). */
    fx.r.r[6] = 01000;
    fx.r.mmu_ssr0 = 0000001; /* MMU enable */
    /* Keep kernel seg0 mapped so MMU trap vector fetch can complete. */
    fx.r.mmu_par[0][0][0] = 0000000;
    fx.r.mmu_pdr[0][0][0] = 0177006;

    ASSERT_EQ(core_step(&fx.r), 0, "Illegal mode 2 should abort via MMU trap");
    ASSERT_EQ(fx.r.r[7], handler, "Illegal mode 2 should vector to MMU trap handler");
    ASSERT_EQ(fx.r.mmu_ssr2, TEST_BASE, "MMR2 should latch faulting instruction PC");
    ASSERT_EQ(fx.r.mmu_ssr0 & 0100000, 0100000, "MMR0 should report non-resident abort");

    ssr0_page = (word)((fx.r.mmu_ssr0 >> 1) & 077);
    ASSERT_EQ((ssr0_page >> 4) & 03, 000002, "MMR0 page field should record mode 2");
    ASSERT_EQ((ssr0_page >> 3) & 01, 000000, "MMR0 page field should record I-space");
    ASSERT_EQ(ssr0_page & 07, 000000, "MMR0 page field should record seg0 at TEST_BASE");

cleanup:
    fixture_teardown(&fx);
    return rc;
#endif
}

static int test_dcj11_pdrw_set_on_internal_reg_write(void)
{
#if !defined(ENABLE_MMU) || !(ENABLE_MMU)
    return 0;
#else
    cpu_fixture fx;
    int rc = 0;
    const word program[] = {
        op_mov(operand(2, 7), operand(3, 7)), /* MOV #1,@#1777572 */
        000001,
        0177572,
    };

    current_test = "dcj11_pdrw_internal_reg_write";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    fx.r.psw = 000000;
    fx.r.r[6] = 01000;
    fx.r.mmu_ssr0 = 0000001; /* MMU enable */
    fx.r.mmu_ssr3 = 0000004; /* kernel split I/D */

    /* Ensure instruction fetch and data references are resident. */
    fx.r.mmu_par[0][0][0] = 0000000;
    fx.r.mmu_pdr[0][0][0] = 0177006;
    fx.r.mmu_par[0][1][0] = 0000000;
    fx.r.mmu_pdr[0][1][0] = 0177006;
    fx.r.mmu_par[0][1][7] = 0000000;
    fx.r.mmu_pdr[0][1][7] = 0177006;

    ASSERT_EQ(fx.r.mmu_pdr[0][1][7] & 0000100, 0000000, "PDR.W starts clear");
    ASSERT_EQ(core_step(&fx.r), 0, "MOV to internal MMU register should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 6, "MOV #,@# should consume three words");
    ASSERT_EQ(fx.r.mmu_pdr[0][1][7] & 0000100, 0000100,
              "Internal register write should set PDR.W");

cleanup:
    fixture_teardown(&fx);
    return rc;
#endif
}

static int test_bpt_vectors(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 04000;
    const word new_psw = 000200;
    const word program[] = {
        op_bpt(),
    };

    current_test = "bpt_vector";
    fixture_setup(&fx);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 014, handler);
    store_word(&fx, 016, new_psw);
    fx.r.r[6] = 01000;
    fx.r.psw = 000003;

    ASSERT_EQ(core_step(&fx.r), 0, "BPT should succeed");
    ASSERT_EQ(fx.r.r[7], handler, "PC should load BPT vector");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should load BPT vector");
    ASSERT_EQ(fx.r.r[6], 00774, "SP should push two words");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE + 2, "Stack PC incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 000003, "Stack PSW incorrect");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_iot_vectors(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 05000;
    const word new_psw = 000220;
    const word program[] = {
        op_iot(),
    };

    current_test = "iot_vector";
    fixture_setup(&fx);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 020, handler);
    store_word(&fx, 022, new_psw);
    fx.r.r[6] = 01000;
    fx.r.psw = 000003;

    ASSERT_EQ(core_step(&fx.r), 0, "IOT should succeed");
    ASSERT_EQ(fx.r.r[7], handler, "PC should load IOT vector");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should load IOT vector");
    ASSERT_EQ(fx.r.r[6], 00774, "SP should push two words");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE + 2, "Stack PC incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 000003, "Stack PSW incorrect");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_trace_vector(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 06000;
    const word new_psw = 000240;
    const word program[] = {
        op_nop(),
        op_nop(),
    };

    current_test = "trace_vector";
    fixture_setup(&fx);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 014, handler);
    store_word(&fx, 016, new_psw);
    store_word(&fx, handler, op_nop());
    store_word(&fx, handler + 2, op_nop());
    store_word(&fx, handler + 2, op_nop());
    ASSERT_EQ(fx.r.load_word(&fx.r, handler), op_nop(), "Handler NOP sanity");
    ASSERT_EQ(fx.r.load_word(&fx.r, handler + 2), op_nop(), "Handler+2 NOP sanity");
    ASSERT_EQ(fx.r.load_word(&fx.r, 016), new_psw, "Vector PSW sanity");
    fx.r.r[6] = 01000;
    fx.r.psw = FLAG_T | 000003;

    ASSERT_EQ(core_step(&fx.r), 0, "Trace should succeed");
    ASSERT_EQ(fx.r.r[7], handler, "PC should enter trace handler");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should load trace vector");
    ASSERT_EQ(fx.r.load_word(&fx.r, handler), op_nop(), "Handler NOP after trace");
    ASSERT_EQ(fx.r.r[6], 00774, "SP should push two words");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE + 2, "Stack PC incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), FLAG_T | 000003, "Stack PSW incorrect");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_rtt_restores_and_sets_ftrap(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word program[] = {
        op_rtt(),
    };

    current_test = "rtt_restore";
    fixture_setup(&fx);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    fx.r.r[6] = 01200;
    store_word(&fx, 01200, 01234);
    store_word(&fx, 01202, 000111);
    fx.r.psw = 000003;
    fx.r.fTrap = 0;

    ASSERT_EQ(core_step(&fx.r), 0, "RTT should succeed");
    ASSERT_EQ(fx.r.r[7], 01234, "PC should restore from stack");
    ASSERT_EQ(fx.r.psw, 000111, "PSW should restore from stack");
    ASSERT_EQ(fx.r.r[6], 01204, "SP should restore after pulls");
    ASSERT_EQ(fx.r.fTrap, 1, "RTT should set fTrap");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_rtt_skips_trace_once(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 07000;
    const word new_psw = FLAG_T | 000003;
    const word program[] = {
        op_rtt(),
        op_nop(),
        op_nop(),
    };

    current_test = "rtt_trace_skip";
    fixture_setup(&fx);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 014, handler);
    store_word(&fx, 016, new_psw);
    store_word(&fx, handler, op_nop());
    store_word(&fx, handler + 2, op_nop());

    fx.r.r[6] = 01200;
    store_word(&fx, 01200, TEST_BASE + 2);
    store_word(&fx, 01202, FLAG_T | 000003);
    fx.r.psw = 000003;

    ASSERT_EQ(core_step(&fx.r), 0, "RTT should succeed");
    ASSERT_EQ(fx.r.fTrap, 1, "RTT should set fTrap");

    ASSERT_EQ(core_step(&fx.r), 0, "NOP should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 4, "PC should advance without trace trap");
    ASSERT_EQ(fx.r.fTrap, 0, "fTrap should clear after next step");
    ASSERT_EQ(fx.r.psw, FLAG_T | 000003, "PSW should preserve T bit");

    ASSERT_EQ(core_step(&fx.r), 0, "Next instruction should execute under trace");
    ASSERT_EQ(fx.r.r[7], handler, "Trace should vector on following step");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should load trace vector");
    ASSERT_EQ(fx.r.load_word(&fx.r, 01200), TEST_BASE + 6, "Trace stack PC incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, 01202), FLAG_T | 000003, "Trace stack PSW incorrect");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_trace_rearms_after_handler(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 07000;
    const word new_psw = 000240;
    const word program[] = {
        op_nop(),
        op_nop(),
    };

    current_test = "trace_rearm";
    fixture_setup(&fx);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 014, handler);
    store_word(&fx, 016, new_psw);
    store_word(&fx, handler, op_nop());
    store_word(&fx, handler + 2, op_nop());

    fx.r.r[6] = 01200;
    fx.r.psw = FLAG_T | 000003;

    ASSERT_EQ(core_step(&fx.r), 0, "Trace should vector on first step");
    ASSERT_EQ(fx.r.r[7], handler, "PC should enter trace handler");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should load trace vector");
    ASSERT_EQ(fx.r.fTrap, 0, "fTrap should remain clear");

    fx.r.psw |= FLAG_T;
    fx.r.r[7] = TEST_BASE;
    ASSERT_EQ(core_step(&fx.r), 0, "Trace should rearm on next step");
    ASSERT_EQ(fx.r.r[7], handler, "PC should return to handler after trace");
    ASSERT_EQ(fx.r.load_word(&fx.r, 01170), TEST_BASE + 2, "Second trace stack PC incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, 01172), (word)(new_psw | FLAG_T), "Second trace stack PSW incorrect");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_trace_rearms_with_t_in_vector(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 07200;
    const word new_psw = (word)(FLAG_T | 000003);
    const word program[] = {
        op_nop(),
        op_nop(),
    };

    current_test = "trace_rearm_vector_t";
    fixture_setup(&fx);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 014, handler);
    store_word(&fx, 016, new_psw);
    store_word(&fx, handler, op_nop());
    store_word(&fx, handler + 2, op_nop());

    fx.r.r[6] = 01200;
    fx.r.psw = FLAG_T | 000003;

    ASSERT_EQ(core_step(&fx.r), 0, "Trace should vector on first step");
    ASSERT_EQ(fx.r.r[7], handler, "PC should enter trace handler");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should load trace vector");

    fx.r.r[7] = TEST_BASE;
    ASSERT_EQ(core_step(&fx.r), 0, "Trace should rearm with T in vector");
    ASSERT_EQ(fx.r.r[7], handler, "PC should return to handler after trace");
    ASSERT_EQ(fx.r.load_word(&fx.r, handler), op_nop(), "Handler NOP after trace");
    ASSERT_EQ(fx.r.load_word(&fx.r, 01170), TEST_BASE + 2, "Second trace stack PC incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, 01172), new_psw, "Second trace stack PSW incorrect");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_trace_stops_when_t_cleared(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 07400;
    const word new_psw = 000003;
    const word program[] = {
        op_nop(),
        op_nop(),
    };

    current_test = "trace_stop_clear_t";
    fixture_setup(&fx);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 014, handler);
    store_word(&fx, 016, new_psw);
    store_word(&fx, handler, op_nop());
    store_word(&fx, handler + 2, op_nop());

    fx.r.r[6] = 01200;
    fx.r.psw = FLAG_T | 000003;

    ASSERT_EQ(core_step(&fx.r), 0, "Trace should vector on first step");
    ASSERT_EQ(fx.r.r[7], handler, "PC should enter trace handler");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should load trace vector");

    fx.r.r[7] = TEST_BASE;
    ASSERT_EQ(core_step(&fx.r), 0, "Program NOP should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance without trace");
    ASSERT_EQ(fx.r.load_word(&fx.r, 01174), TEST_BASE + 2, "Trace stack PC incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, 01176), FLAG_T | 000003, "Trace stack PSW incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, handler), op_nop(), "Handler NOP after trace");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_rti_restores_state(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word program[] = {
        op_rti(),
    };

    current_test = "rti_restore";
    fixture_setup(&fx);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    fx.r.r[6] = 01200;
    store_word(&fx, 01200, 01234);
    store_word(&fx, 01202, 000111);
    fx.r.psw = 000003;

    ASSERT_EQ(core_step(&fx.r), 0, "RTI should succeed");
    ASSERT_EQ(fx.r.r[7], 01234, "PC should restore from stack");
    ASSERT_EQ(fx.r.psw, 000111, "PSW should restore from stack");
    ASSERT_EQ(fx.r.r[6], 01204, "SP should restore after pulls");
    ASSERT_EQ(fx.r.fTrap, 0, "RTI should not set fTrap");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_rti_traces_immediately_when_t_restored(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 07200;
    const word vector_psw = 000340;
    const word program[] = {
        op_rti(),
        op_nop(),
    };

    current_test = "dcj11_rti_trace_immediate";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 014, handler);
    store_word(&fx, 016, vector_psw);
    store_word(&fx, handler, op_nop());
    fx.r.r[6] = 01200;
    store_word(&fx, 01200, TEST_BASE + 2);
    store_word(&fx, 01202, FLAG_T | 000003);
    fx.r.psw = 000003;

    ASSERT_EQ(core_step(&fx.r), 0, "RTI should return and immediately take TRACE");
    ASSERT_EQ(fx.r.r[7], handler, "RTI should vector to trace handler");
    ASSERT_EQ(fx.r.psw, vector_psw, "TRACE should load vector PSW");
    ASSERT_EQ(fx.r.r[6], 01200, "SP should pop RTI frame and push TRACE frame");
    ASSERT_EQ(fx.r.load_word(&fx.r, 01200), TEST_BASE + 2, "TRACE frame PC should be return PC");
    ASSERT_EQ(fx.r.load_word(&fx.r, 01202), FLAG_T | 000003,
              "TRACE frame PSW should preserve restored T-bit");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_rtt_traces_after_one_instruction(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 07240;
    const word vector_psw = 000340;
    const word program[] = {
        op_rtt(),
        op_nop(),
        op_nop(),
    };

    current_test = "dcj11_rtt_trace_one_inst";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 014, handler);
    store_word(&fx, 016, vector_psw);
    store_word(&fx, handler, op_nop());
    fx.r.r[6] = 01200;
    store_word(&fx, 01200, TEST_BASE + 2);
    store_word(&fx, 01202, FLAG_T | 000003);
    fx.r.psw = 000003;

    ASSERT_EQ(core_step(&fx.r), 0, "RTT should restore PC/PSW");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "RTT should return to next instruction");
    ASSERT_EQ(fx.r.psw, FLAG_T | 000003, "RTT should restore T-bit");
    ASSERT_EQ(fx.r.fTrap, 0, "DCJ11 RTT should not use fTrap skip flag");

    ASSERT_EQ(core_step(&fx.r), 0, "One instruction should execute before TRACE");
    ASSERT_EQ(fx.r.r[7], handler, "TRACE should occur after exactly one instruction");
    ASSERT_EQ(fx.r.psw, vector_psw, "TRACE should load vector PSW");
    ASSERT_EQ(fx.r.r[6], 01200, "SP should contain TRACE frame after one instruction");
    ASSERT_EQ(fx.r.load_word(&fx.r, 01200), TEST_BASE + 4, "TRACE frame PC should follow one instruction");
    ASSERT_EQ(fx.r.load_word(&fx.r, 01202), FLAG_T | 000003,
              "TRACE frame PSW should keep RTT-restored T-bit");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_explicit_psw_write_preserves_t(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word program[] = {
        op_mov(operand(2, 7), operand(3, 7)), /* MOV #T,@#177776 */
        FLAG_T,
        0177776,
        op_mov(operand(2, 7), operand(3, 7)), /* MOV #0,@#177776 */
        0,
        0177776,
    };

    current_test = "dcj11_psw_explicit_t_protected";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    fx.r.psw = 000003;
    ASSERT_EQ(core_step(&fx.r), 0, "Explicit PSW write should execute");
    ASSERT_EQ(fx.r.psw & FLAG_T, 0, "Explicit PSW write must not set T on DCJ11");

    fx.r.psw = FLAG_T | 000003;
    fx.r.fTrap = 1; /* avoid trace side effect while validating explicit write policy */
    ASSERT_EQ(core_step(&fx.r), 0, "Explicit PSW write should execute");
    ASSERT_EQ(fx.r.psw & FLAG_T, FLAG_T, "Explicit PSW write must not clear T on DCJ11");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_vm1_rti_restores_flags_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word program[] = {
        op_rti(),
    };
    const word stack_psw = (FLAG_H | FLAG_P | FLAG_T | FLAG_N | FLAG_Z | FLAG_C);

    if (!is_vm1_model(model)) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "vm1_rti_flags", name);
    fixture_setup_model(&fx, model);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    fx.r.r[6] = 01200;
    store_word(&fx, 01200, 01234);
    store_word(&fx, 01202, stack_psw);
    fx.r.psw = 000003;

    ASSERT_EQ(core_step(&fx.r), 0, "RTI should succeed");
    ASSERT_EQ(fx.r.r[7], 01234, "PC should restore from stack");
    ASSERT_EQ(fx.r.psw, stack_psw, "PSW should restore from stack");
    ASSERT_EQ(fx.r.r[6], 01204, "SP should restore after pulls");
    ASSERT_EQ(fx.r.fTrap, 0, "RTI should not set fTrap");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_rti_restores_state(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word program[] = {
        op_rti(),
    };

    current_test = "dcj11_rti_restore";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    fx.r.r[6] = 01200;
    store_word(&fx, 01200, 01234);
    store_word(&fx, 01202, 000111);
    fx.r.psw = 000003;

    ASSERT_EQ(core_step(&fx.r), 0, "RTI should succeed");
    ASSERT_EQ(fx.r.r[7], 01234, "PC should restore from stack");
    ASSERT_EQ(fx.r.psw, 000111, "PSW should restore from stack");
    ASSERT_EQ(fx.r.r[6], 01204, "SP should restore after pulls");
    ASSERT_EQ(fx.r.fTrap, 0, "RTI should not set fTrap");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_vm2_rti_restores_state_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word program[] = {
        op_rti(),
    };

    if (!is_vm2_model(model)) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "vm2_rti_restore", name);
    fixture_setup_model(&fx, model);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    fx.r.r[6] = 01200;
    store_word(&fx, 01200, 01234);
    store_word(&fx, 01202, 000111);
    fx.r.psw = 000003;

    ASSERT_EQ(core_step(&fx.r), 0, "RTI should succeed");
    ASSERT_EQ(fx.r.r[7], 01234, "PC should restore from stack");
    ASSERT_EQ(fx.r.psw, 000111, "PSW should restore from stack");
    ASSERT_EQ(fx.r.r[6], 01204, "SP should restore after pulls");
    ASSERT_EQ(fx.r.fTrap, 0, "RTI should not set fTrap");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_vm2_rti_hu_restore_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word program[] = {
        op_rti(),
    };

    if (!is_vm2_model(model)) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "vm2_rti_hu_keep", name);
    fixture_setup_model(&fx, model);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));
    fx.r.r[6] = 01200;
    store_word(&fx, 01200, 0100000);
    store_word(&fx, 01202, 000000);
    fx.r.psw = FLAG_H;
    ASSERT_EQ(core_step(&fx.r), 0, "RTI should execute");
    ASSERT_EQ((fx.r.psw & FLAG_H), FLAG_H, "RTI should keep H when PC < 160000");
    fixture_teardown(&fx);

    set_test_name(namebuf, sizeof(namebuf), "vm2_rti_hu_load", name);
    fixture_setup_model(&fx, model);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));
    fx.r.r[6] = 01200;
    store_word(&fx, 01200, 0160000);
    store_word(&fx, 01202, FLAG_H);
    fx.r.psw = 000000;
    ASSERT_EQ(core_step(&fx.r), 0, "RTI should execute");
    ASSERT_EQ((fx.r.psw & FLAG_H), FLAG_H, "RTI should load H when PC >= 160000");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_rti_user_restricts_psw(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word program[] = {
        op_rti(),
    };
    const word psw_before = 0140240;
    const word psw_stack = 000000;

    current_test = "dcj11_rti_user_psw";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    fx.r.r[6] = 01200;
    store_word(&fx, 01200, 01234);
    store_word(&fx, 01202, psw_stack);
    fx.r.psw = psw_before;

    ASSERT_EQ(core_step(&fx.r), 0, "RTI should succeed");
    ASSERT_EQ(fx.r.r[7], 01234, "PC should restore from stack");
    ASSERT_EQ(fx.r.psw, psw_before, "RTI should preserve mode/priority bits");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_rti_user_sets_high_psw_bits(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word program[] = {
        op_rti(),
    };
    const word psw_before = DCJ11_CM(1) | 000240 | 000001;
    const word psw_stack = DCJ11_CM(3) | DCJ11_PM(3) | 004000 | 000340 | 000016;
    const word expected_psw = (word)((psw_stack & ~(0174000 | 000340)) |
                                     ((psw_stack | psw_before) & 0174000) |
                                     (psw_before & 000340));

    current_test = "dcj11_rti_user_psw_set_only";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    fx.r.r[6] = 01200;
    store_word(&fx, 01200, 01234);
    store_word(&fx, 01202, psw_stack);
    fx.r.psw = psw_before;

    ASSERT_EQ(core_step(&fx.r), 0, "RTI should succeed");
    ASSERT_EQ(fx.r.r[7], 01234, "PC should restore from stack");
    ASSERT_EQ(fx.r.psw, expected_psw, "RTI must OR-protect PS<15:11> and keep PS<7:5>");
    ASSERT_EQ(fx.r.psw & 004000, 004000, "RTI should allow setting PS<11> outside kernel");
    ASSERT_EQ(fx.r.psw & 000340, psw_before & 000340, "RTI should preserve PS<7:5> outside kernel");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_mode_stack_banking(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word program[] = {
        op_rti(),
        000003, /* BPT */
        op_rti(),
        000003, /* BPT */
        op_nop(),
    };
    const word psw_kernel = DCJ11_CM(0) | 000003;
    const word psw_super = DCJ11_CM(1) | 000003;
    const word psw_user = DCJ11_CM(3) | 000003;
    const word ksp0 = 010000;
    const word ssp0 = 012000;
    const word usp0 = 014000;

    current_test = "dcj11_mode_stack_banking";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    fx.r.sp_mode_init = 1;
    fx.r.sp_mode[0] = ksp0;
    fx.r.sp_mode[1] = ssp0;
    fx.r.sp_mode[3] = usp0;
    fx.r.r[6] = ksp0;
    fx.r.psw = psw_kernel;

    store_word(&fx, ksp0, TEST_BASE + 2);
    store_word(&fx, ksp0 + 2, psw_user);
    store_word(&fx, 000014, TEST_BASE + 4);
    store_word(&fx, 000016, 000340);
    ASSERT_EQ(core_step(&fx.r), 0, "RTI K->U should succeed");
    ASSERT_EQ(fx.r.psw, psw_user, "RTI should switch to user mode");
    ASSERT_EQ(fx.r.r[6], usp0, "R6 should load user stack bank");
    ASSERT_EQ(fx.r.sp_mode[0], ksp0 + 4, "Kernel stack bank should be advanced");

    fx.r.r[6] = usp0 + 010;
    ASSERT_EQ(core_step(&fx.r), 0, "BPT U->K should succeed");
    ASSERT_EQ((fx.r.psw >> 14) & 03, 0, "BPT should enter kernel mode");
    ASSERT_EQ(fx.r.r[6], ksp0, "BPT should switch to kernel stack");
    ASSERT_EQ(fx.r.sp_mode[3], usp0 + 010, "User stack bank should retain updates");

    store_word(&fx, ksp0, TEST_BASE + 6);
    store_word(&fx, ksp0 + 2, psw_super);
    ASSERT_EQ(core_step(&fx.r), 0, "RTI K->S should succeed");
    ASSERT_EQ(fx.r.psw, psw_super, "RTI should switch to supervisor mode");
    ASSERT_EQ(fx.r.r[6], ssp0, "R6 should load supervisor stack bank");

    fx.r.r[6] = ssp0 + 010;
    store_word(&fx, 000014, TEST_BASE + 10);
    store_word(&fx, 000016, 000340);
    ASSERT_EQ(core_step(&fx.r), 0, "BPT S->K should succeed");
    ASSERT_EQ((fx.r.psw >> 14) & 03, 0, "BPT should enter kernel mode");
    ASSERT_EQ(fx.r.r[6], ksp0, "Kernel stack should be selected again");
    ASSERT_EQ(fx.r.sp_mode[1], ssp0 + 010, "Supervisor stack bank should retain updates");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_register_set_banking(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word program[] = {
        op_mov(operand(2, 7), operand(0, 0)), /* R0 <- 000111 (set 0) */
        000111,
        op_mov(operand(2, 7), operand(0, 1)), /* R1 <- PSW addr */
        0177776,
        op_mov(operand(2, 7), operand(1, 1)), /* PS<11>=1 (switch to set 1) */
        004000,
        op_mov(operand(2, 7), operand(0, 0)), /* R0 <- 000222 (set 1) */
        000222,
        op_mov(operand(2, 7), operand(1, 1)), /* PS<11>=0 (back to set 0) */
        000000,
        op_mov(operand(2, 7), operand(1, 1)), /* PS<11>=1 (set 1 again) */
        004000,
        op_mov(operand(2, 7), operand(1, 1)), /* PS<11>=0 (set 0 again) */
        000000,
    };

    current_test = "dcj11_register_set_banking";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));
    fx.r.psw = 000000;

    ASSERT_EQ(core_step(&fx.r), 0, "MOV #111,R0");
    ASSERT_EQ(fx.r.r[0], 000111, "R0 in set 0");
    ASSERT_EQ(core_step(&fx.r), 0, "MOV #177776,R1");
    ASSERT_EQ(fx.r.r[1], 0177776, "R1 in set 0");

    ASSERT_EQ(core_step(&fx.r), 0, "MOV #4000,@(R1)");
    ASSERT_EQ((fx.r.psw & 004000), 004000, "PS<11> should be set");
    ASSERT_EQ(fx.r.r[0], 000111, "Initial set 1 should mirror set 0");

    ASSERT_EQ(core_step(&fx.r), 0, "MOV #222,R0");
    ASSERT_EQ(fx.r.r[0], 000222, "R0 in set 1");

    ASSERT_EQ(core_step(&fx.r), 0, "MOV #0,@(R1)");
    ASSERT_EQ((fx.r.psw & 004000), 000000, "PS<11> should be clear");
    ASSERT_EQ(fx.r.r[0], 000111, "R0 restored from set 0");

    ASSERT_EQ(core_step(&fx.r), 0, "MOV #4000,@(R1) again");
    ASSERT_EQ((fx.r.psw & 004000), 004000, "PS<11> should be set again");
    ASSERT_EQ(fx.r.r[0], 000222, "R0 restored from set 1");

    ASSERT_EQ(core_step(&fx.r), 0, "MOV #0,@(R1) again");
    ASSERT_EQ((fx.r.psw & 004000), 000000, "PS<11> should be clear again");
    ASSERT_EQ(fx.r.r[0], 000111, "R0 restored from set 0 again");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_irq_entry_frame_user(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 02000;
    const word vector_psw = 000340;
    const word user_psw = DCJ11_CM(3) | 000003;
    const word kernel_sp = 01000;
    const word user_sp = 01200;
    const word expected_psw = DCJ11_CM(0) | DCJ11_PM(3) | 000340;
    const word program[] = {
        op_nop(),
    };

    current_test = "dcj11_irq_entry_user";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000060, handler);
    store_word(&fx, 000062, vector_psw);
    store_word(&fx, handler, op_rti());

    fx.r.sp_mode_init = 1;
    fx.r.sp_mode[0] = kernel_sp;
    fx.r.sp_mode[1] = kernel_sp;
    fx.r.sp_mode[3] = user_sp;
    fx.r.r[6] = user_sp;
    fx.r.psw = user_psw;
    fx.r.poll_irq = test_poll_irq_vector;
    test_irq_vector = 000060;
    test_irq_pending = 1;

    ASSERT_EQ(core_step(&fx.r), 0, "IRQ should be accepted");
    ASSERT_EQ(fx.r.r[7], handler, "PC should enter IRQ handler");
    ASSERT_EQ(fx.r.psw, expected_psw, "PSW should set CM=K and PM=U");
    ASSERT_EQ((fx.r.psw >> 14) & 03, 0, "Current mode should be kernel");
    ASSERT_EQ((fx.r.psw >> 12) & 03, 3, "Previous mode should be user");
    ASSERT_EQ(fx.r.r[6], (word)(kernel_sp - 4), "IRQ should push frame on kernel stack");
    ASSERT_EQ(fx.r.load_word(&fx.r, (word)(kernel_sp - 4)), TEST_BASE + 2,
              "Kernel stack PC incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, (word)(kernel_sp - 2)), user_psw,
              "Kernel stack PSW incorrect");
    ASSERT_EQ(fx.r.sp_mode[3], user_sp, "User stack bank should stay unchanged");

cleanup:
    fixture_teardown(&fx);
    test_irq_pending = 0;
    test_irq_vector = 0;
    return rc;
}

static int test_dcj11_rti_restore_user_mode_stack(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 02200;
    const word vector_psw = 000340;
    const word user_psw = DCJ11_CM(3) | 000003;
    const word kernel_sp = 01000;
    const word user_sp = 01200;
    const word program[] = {
        op_nop(),
        op_nop(),
    };

    current_test = "dcj11_rti_restore_user";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000060, handler);
    store_word(&fx, 000062, vector_psw);
    store_word(&fx, handler, op_rti());

    fx.r.sp_mode_init = 1;
    fx.r.sp_mode[0] = kernel_sp;
    fx.r.sp_mode[1] = kernel_sp;
    fx.r.sp_mode[3] = user_sp;
    fx.r.r[6] = user_sp;
    fx.r.psw = user_psw;
    fx.r.poll_irq = test_poll_irq_vector;
    test_irq_vector = 000060;
    test_irq_pending = 1;

    ASSERT_EQ(core_step(&fx.r), 0, "IRQ entry should succeed");
    ASSERT_EQ(core_step(&fx.r), 0, "RTI should return from handler");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "RTI should restore user PC");
    ASSERT_EQ(fx.r.psw, user_psw, "RTI should restore user PSW");
    ASSERT_EQ(fx.r.r[6], user_sp, "RTI should restore user stack");
    ASSERT_EQ(fx.r.sp_mode[0], kernel_sp, "Kernel stack bank should preserve return SP");

cleanup:
    fixture_teardown(&fx);
    test_irq_pending = 0;
    test_irq_vector = 0;
    return rc;
}

static int test_dcj11_irq_rti_supervisor_mode(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 02400;
    const word vector_psw = 000340;
    const word super_psw = DCJ11_CM(1) | 000003;
    const word kernel_sp = 01000;
    const word super_sp = 01200;
    const word expected_psw = DCJ11_CM(0) | DCJ11_PM(1) | 000340;
    const word program[] = {
        op_nop(),
        op_nop(),
    };

    current_test = "dcj11_irq_rti_supervisor";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000060, handler);
    store_word(&fx, 000062, vector_psw);
    store_word(&fx, handler, op_rti());

    fx.r.sp_mode_init = 1;
    fx.r.sp_mode[0] = kernel_sp;
    fx.r.sp_mode[1] = super_sp;
    fx.r.sp_mode[3] = 01400;
    fx.r.r[6] = super_sp;
    fx.r.psw = super_psw;
    fx.r.poll_irq = test_poll_irq_vector;
    test_irq_vector = 000060;
    test_irq_pending = 1;

    ASSERT_EQ(core_step(&fx.r), 0, "IRQ should be accepted from supervisor mode");
    ASSERT_EQ(fx.r.psw, expected_psw, "IRQ PSW should set PM=supervisor");
    ASSERT_EQ(fx.r.r[6], (word)(kernel_sp - 4), "IRQ frame should use kernel stack");
    ASSERT_EQ(fx.r.load_word(&fx.r, (word)(kernel_sp - 4)), TEST_BASE + 2,
              "Kernel stack PC incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, (word)(kernel_sp - 2)), super_psw,
              "Kernel stack PSW incorrect");
    ASSERT_EQ(core_step(&fx.r), 0, "RTI should return to supervisor context");
    ASSERT_EQ(fx.r.psw, super_psw, "RTI should restore supervisor PSW");
    ASSERT_EQ(fx.r.r[6], super_sp, "RTI should restore supervisor stack");

cleanup:
    fixture_teardown(&fx);
    test_irq_pending = 0;
    test_irq_vector = 0;
    return rc;
}

static int test_dcj11_irq_rti_mode_stack_switch(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 02000;
    const word kernel_psw = DCJ11_CM(0) | DCJ11_PM(3) | 000340;
    const word user_psw = 0140003;
    const word kernel_sp = 01000;
    const word user_sp = 01200;
    const word program[] = {
        op_nop(),
        op_nop(),
    };

    current_test = "dcj11_irq_rti_mode_stack";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000060, handler);
    store_word(&fx, 000062, kernel_psw);
    store_word(&fx, handler, op_rti());

    fx.r.sp_mode_init = 1;
    fx.r.sp_mode[0] = kernel_sp;
    fx.r.sp_mode[1] = kernel_sp;
    fx.r.sp_mode[3] = user_sp;
    fx.r.r[6] = user_sp;
    fx.r.psw = user_psw;
    fx.r.poll_irq = test_poll_irq_vector;
    test_irq_vector = 000060;
    test_irq_pending = 1;

    ASSERT_EQ(core_step(&fx.r), 0, "IRQ should be accepted after NOP");
    ASSERT_EQ(fx.r.r[7], handler, "PC should enter IRQ handler");
    ASSERT_EQ(fx.r.psw, kernel_psw, "PSW should load kernel IRQ vector");
    ASSERT_EQ(fx.r.r[6], (word)(kernel_sp - 4), "IRQ should push frame on kernel stack");
    ASSERT_EQ(fx.r.load_word(&fx.r, (word)(kernel_sp - 4)), TEST_BASE + 2,
              "Kernel stack PC incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, (word)(kernel_sp - 2)), user_psw,
              "Kernel stack PSW incorrect");
    ASSERT_EQ(fx.r.sp_mode[3], user_sp, "User stack bank should be preserved");

    ASSERT_EQ(core_step(&fx.r), 0, "RTI should return to user context");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "RTI should restore user PC");
    ASSERT_EQ(fx.r.psw, user_psw, "RTI should restore user PSW");
    ASSERT_EQ(fx.r.r[6], user_sp, "RTI should switch back to user stack");
    ASSERT_EQ(fx.r.sp_mode[0], kernel_sp, "Kernel stack bank should preserve post-RTI SP");

cleanup:
    fixture_teardown(&fx);
    test_irq_pending = 0;
    test_irq_vector = 0;
    return rc;
}

static int test_dcj11_mtps_user_restricts_psw(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word program[] = {
        op_mtps(operand(0, 0)),
    };

    current_test = "dcj11_mtps_user";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    fx.r.r[0] = 000340;
    fx.r.psw = 0140000 | 000200;
    ASSERT_EQ(core_step(&fx.r), 0, "MTPS should execute");
    ASSERT_EQ((fx.r.psw & 000340), 000200, "MTPS should not change PSW priority");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_spl_kernel_sets_priority(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word program[] = {
        op_spl(5),
        op_spl(2),
    };
    const word cc_before = FLAG_N | FLAG_C;

    current_test = "dcj11_spl_kernel";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    fx.r.psw = cc_before;
    ASSERT_EQ(core_step(&fx.r), 0, "SPL 5 should execute");
    ASSERT_EQ(fx.r.psw & 000340, 000240, "SPL should set kernel priority");
    ASSERT_EQ(fx.r.psw & 000017, cc_before, "SPL should preserve condition codes");

    ASSERT_EQ(core_step(&fx.r), 0, "SPL 2 should execute");
    ASSERT_EQ(fx.r.psw & 000340, 000100, "SPL should update kernel priority");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_spl_user_is_nop(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word program[] = {
        op_spl(7),
    };
    const word psw_before = 0140205;

    current_test = "dcj11_spl_user_nop";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    fx.r.psw = psw_before;
    ASSERT_EQ(core_step(&fx.r), 0, "SPL in user mode should execute as NOP");
    ASSERT_EQ(fx.r.psw, psw_before, "SPL in user mode should not modify PSW");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "SPL in user mode should advance PC");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_spl_illegal_on_other_models_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    const word program[] = {
        op_spl(7),
    };
    const word handler = 07000;
    const word new_psw = 000340;
    char namebuf[64];

    if (model == DCJ11 || model == K1801VM2 || model == K1806VM2) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "spl_illegal", name);
    fixture_setup_model(&fx, model);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 010, handler);
    store_word(&fx, 012, new_psw);
    fx.r.r[6] = 01000;
    fx.r.psw = 000000;

    ASSERT_EQ(core_step(&fx.r), 0, "SPL should trap on non-DCJ11 models");
    ASSERT_EQ(fx.r.r[7], handler, "SPL should load illegal vector");
    ASSERT_EQ(fx.r.psw, new_psw, "SPL should load illegal vector PSW");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_spl_illegal_on_other_models(void)
{
    return run_for_models(test_spl_illegal_on_other_models_model);
}

static int test_keyboard_irq_vector(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 02000;
    const word new_psw = 000340;
    const word program[] = {
        op_nop(),
        op_nop(),
    };

    current_test = "kbd_irq_vector";
    fixture_setup(&fx);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000060, handler);
    store_word(&fx, 000062, new_psw);
    store_word(&fx, handler, op_nop());

    fx.r.r[6] = 01000;
    fx.r.psw = 000003;
    fx.r.poll_irq = test_poll_irq;
    test_irq_pending = 1;

    ASSERT_EQ(core_step(&fx.r), 0, "IRQ should succeed");
    ASSERT_EQ(fx.r.r[7], handler, "PC should enter handler");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should load IRQ vector");
    ASSERT_EQ(fx.r.r[6], 00774, "SP should push two words");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE + 2, "Stack PC incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 000003, "Stack PSW incorrect");

cleanup:
    fixture_teardown(&fx);
    test_irq_pending = 0;
    test_irq_priority = 0;
    return rc;
}

static int test_irq_mask_blocks_interrupt(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 02400;
    const word new_psw = 000340;
    const word program[] = {
        op_nop(),
        op_nop(),
    };

    current_test = "irq_mask_block";
    fixture_setup(&fx);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000060, handler);
    store_word(&fx, 000062, new_psw);
    store_word(&fx, handler, op_nop());

    fx.r.r[6] = 01000;
    fx.r.psw = 000003;

    fx.r.poll_irq = NULL;
    test_irq_pending = 1;

    ASSERT_EQ(core_step(&fx.r), 0, "NOP should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance without IRQ");
    ASSERT_EQ(fx.r.psw, 000003, "PSW should remain unchanged");
    ASSERT_EQ(fx.r.r[6], 01000, "SP should remain unchanged");

cleanup:
    fixture_teardown(&fx);
    test_irq_pending = 0;
    test_irq_priority = 0;
    return rc;
}

static int test_dcj11_irq_priority_mask(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 02400;
    const word new_psw = 000340;
    const word program[] = {
        op_nop(),
        op_nop(),
    };

    current_test = "dcj11_irq_pri_mask";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000060, handler);
    store_word(&fx, 000062, new_psw);
    store_word(&fx, handler, op_nop());

    fx.r.r[6] = 01000;
    fx.r.psw = 000200; /* priority 4 */
    fx.r.poll_irq = test_poll_irq;

    test_irq_priority = 4;
    test_irq_pending = 1;
    test_irq_called = 0;
    test_last_vector = 0;
    ASSERT_EQ(core_step(&fx.r), 0, "IRQ at same priority should be masked");
    ASSERT_EQ(test_irq_called, 1, "IRQ poll should run");
    ASSERT_EQ(test_last_vector, (word)(000060 | ((test_irq_priority & 07) << 9)), "IRQ poll should provide vector");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance without IRQ");

    test_irq_priority = 5;
    test_irq_pending = 1;
    test_irq_called = 0;
    test_last_vector = 0;
    ASSERT_EQ(core_step(&fx.r), 0, "IRQ at higher priority should be accepted");
    ASSERT_EQ(test_irq_called, 1, "IRQ poll should run");
    ASSERT_EQ(test_last_vector, (word)(000060 | ((test_irq_priority & 07) << 9)), "IRQ poll should provide vector");
    ASSERT_EQ(fx.r.r[6], 00774, "SP should push two words");
    ASSERT_EQ(fx.r.load_word(&fx.r, 000060), handler, "IRQ vector should remain intact");
    ASSERT_EQ(fx.r.r[7], handler, "PC should enter handler");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should load IRQ vector");

cleanup:
    fixture_teardown(&fx);
    test_irq_pending = 0;
    test_irq_priority = 0;
    return rc;
}

static int test_dcj11_irq_highest_priority(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler_lo = 02000;
    const word handler_hi = 04000;
    const word new_psw = 000340;
    const word program[] = {
        op_nop(),
    };

    current_test = "dcj11_irq_highest";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000060, handler_lo);
    store_word(&fx, 000062, new_psw);
    store_word(&fx, 000070, handler_hi);
    store_word(&fx, 000072, new_psw);
    store_word(&fx, handler_lo, op_nop());
    store_word(&fx, handler_hi, op_rti());
    ASSERT_EQ(fx.r.load_word(&fx.r, handler_hi), op_rti(), "IRQ handler should start with RTI");

    memset(test_irq_pending_pri, 0, sizeof(test_irq_pending_pri));
    memset(test_irq_vector_pri, 0, sizeof(test_irq_vector_pri));
    test_irq_pending_pri[3] = 1;
    test_irq_vector_pri[3] = 000060;
    test_irq_pending_pri[6] = 1;
    test_irq_vector_pri[6] = 000070;

    fx.r.r[6] = 01000;
    fx.r.psw = 000000;
    fx.r.poll_irq = test_poll_irq_highest;

    ASSERT_EQ(core_step(&fx.r), 0, "IRQ should be accepted");
    ASSERT_EQ(fx.r.r[7], handler_hi, "Highest priority IRQ should be selected");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should load IRQ vector");

cleanup:
    fixture_teardown(&fx);
    memset(test_irq_pending_pri, 0, sizeof(test_irq_pending_pri));
    memset(test_irq_vector_pri, 0, sizeof(test_irq_vector_pri));
    return rc;
}

static int test_dcj11_irq_preempt_before_first_isr_instruction(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler_lo = 02000;
    const word handler_hi = 04000;
    const word psw_lo = 000140; /* priority 3 */
    const word psw_hi = 000340; /* priority 7 */
    const word program[] = {
        op_nop(),
    };

    current_test = "dcj11_irq_preempt_entry";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000060, handler_lo);
    store_word(&fx, 000062, psw_lo);
    store_word(&fx, 000070, handler_hi);
    store_word(&fx, 000072, psw_hi);
    store_word(&fx, handler_lo, op_inc(operand(0, 0)));
    store_word(&fx, handler_hi, op_nop());

    memset(test_irq_seq_vector, 0, sizeof(test_irq_seq_vector));
    memset(test_irq_seq_priority, 0, sizeof(test_irq_seq_priority));
    test_irq_seq_len = 2;
    test_irq_seq_idx = 0;
    test_irq_seq_vector[0] = 000060;
    test_irq_seq_priority[0] = 3;
    test_irq_seq_vector[1] = 000070;
    test_irq_seq_priority[1] = 6;

    fx.r.r[0] = 0;
    fx.r.r[6] = 01000;
    fx.r.psw = 000000;
    fx.r.poll_irq = test_poll_irq_sequence;

    ASSERT_EQ(core_step(&fx.r), 0, "IRQ should be accepted");
    ASSERT_EQ(fx.r.r[7], handler_hi,
              "Higher-priority IRQ must preempt before low ISR first instruction");
    ASSERT_EQ(fx.r.psw, psw_hi, "PSW should load high-priority vector PSW");
    ASSERT_EQ(fx.r.r[0], 0, "Low-priority ISR first opcode must not execute");
    ASSERT_EQ(fx.r.r[6], 00770, "Two IRQ entries should push two frames");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00770), handler_lo,
              "Top frame PC should be low-priority handler entry");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00772), psw_lo,
              "Top frame PSW should be low-priority handler PSW");

cleanup:
    fixture_teardown(&fx);
    test_irq_seq_len = 0;
    test_irq_seq_idx = 0;
    memset(test_irq_seq_vector, 0, sizeof(test_irq_seq_vector));
    memset(test_irq_seq_priority, 0, sizeof(test_irq_seq_priority));
    return rc;
}

static int test_dcj11_irq_vector_mask(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler_masked = 02000;
    const word handler_full = 03000;
    const word new_psw_masked = 000340;
    const word new_psw_full = 000200;
    const word program[] = {
        op_nop(),
    };

    current_test = "dcj11_irq_vector_mask";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000300, handler_masked);
    store_word(&fx, 000302, new_psw_masked);
    store_word(&fx, 004300, handler_full);
    store_word(&fx, 004302, new_psw_full);
    store_word(&fx, handler_masked, op_nop());
    store_word(&fx, handler_full, op_nop());

    fx.r.r[6] = 01000;
    fx.r.psw = 000000;
    fx.r.poll_irq = test_poll_irq_vector;
    test_irq_vector = 004300;
    test_irq_pending = 1;
    ASSERT_EQ(core_step(&fx.r), 0, "IRQ should execute");
    ASSERT_EQ(fx.r.r[7], handler_masked, "IRQ should mask vector to 8-bit");
    ASSERT_EQ(fx.r.psw, new_psw_masked, "PSW should load masked vector");

cleanup:
    fixture_teardown(&fx);
    test_irq_pending = 0;
    test_irq_vector = 0;
    return rc;
}

static int test_vm1_irq_masking_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word handler = 02200;
    const word new_psw = 000340;
    const word program[] = {
        op_nop(),
        op_nop(),
    };

    if (!is_vm1_model(model)) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "vm1_irq_mask_psw7", name);
    fixture_setup_model(&fx, model);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));
    store_word(&fx, 000100, handler);
    store_word(&fx, 000102, new_psw);

    fx.r.r[6] = 01000;
    fx.r.psw = FLAG_P;
    fx.r.poll_irq = test_poll_irq_vector;
    test_irq_vector = 000100;
    test_irq_pending = 1;
    ASSERT_EQ(core_step(&fx.r), 0, "IRQ2 should be masked by PSW7");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance without IRQ");
    ASSERT_EQ(fx.r.r[6], 01000, "SP should remain unchanged");

    fx.r.psw = 000000;
    test_irq_vector = 000100;
    test_irq_pending = 1;
    ASSERT_EQ(core_step(&fx.r), 0, "IRQ2 should be accepted when PSW7=0");
    ASSERT_EQ(fx.r.r[7], handler, "PC should enter handler");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should load IRQ vector");
    ASSERT_EQ(fx.r.r[6], 00774, "SP should push two words");
    fixture_teardown(&fx);

    set_test_name(namebuf, sizeof(namebuf), "vm1_irq_mask_psw10", name);
    fixture_setup_model(&fx, model);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));
    store_word(&fx, 000060, handler);
    store_word(&fx, 000062, new_psw);

    fx.r.r[6] = 01000;
    fx.r.psw = 01000;
    fx.r.poll_irq = test_poll_irq_vector;
    test_irq_vector = 000060;
    test_irq_pending = 1;
    ASSERT_EQ(core_step(&fx.r), 0, "VIRQ should be masked by PSW10");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance without IRQ");
    ASSERT_EQ(fx.r.r[6], 01000, "SP should remain unchanged");

    fx.r.psw = 000000;
    test_irq_vector = 000060;
    test_irq_pending = 1;
    ASSERT_EQ(core_step(&fx.r), 0, "VIRQ should be accepted when PSW10=0");
    ASSERT_EQ(fx.r.r[7], handler, "PC should enter handler");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should load IRQ vector");
    ASSERT_EQ(fx.r.r[6], 00774, "SP should push two words");

cleanup:
    fixture_teardown(&fx);
    test_irq_pending = 0;
    test_irq_vector = 0;
    test_last_vector = 0;
    return rc;
}

static int test_vm1_irq_priority_selection_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word handler_1 = 02100;
    const word handler_2 = 02200;
    const word new_psw = 000340;
    const word program[] = {
        op_nop(),
    };

    if (!is_vm1_model(model)) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "vm1_irq_priority_irq1", name);
    fixture_setup_model(&fx, model);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));
    store_word(&fx, 0160002, handler_1);
    store_word(&fx, 0160004, new_psw);
    ASSERT_EQ(fx.r.load_word(&fx.r, 0160002), handler_1, "VM1 IRQ1 vector readback");
    store_word(&fx, 000100, handler_2);
    store_word(&fx, 000102, new_psw);

    fx.r.r[6] = 01000;
    fx.r.psw = 000000;
    fx.r.poll_irq = test_poll_irq_highest;
    memset(test_irq_pending_pri, 0, sizeof(test_irq_pending_pri));
    memset(test_irq_vector_pri, 0, sizeof(test_irq_vector_pri));
    test_irq_pending_pri[3] = 1;
    test_irq_vector_pri[3] = 000100;
    test_irq_pending_pri[6] = 1;
    test_irq_vector_pri[6] = 0160002;

    ASSERT_EQ(core_step(&fx.r), 0, "IRQ should be accepted");
    ASSERT_EQ(fx.r.r[7], handler_1, "IRQ1 should win over IRQ2");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should load IRQ vector");
    ASSERT_EQ(fx.r.r[6], 00774, "SP should push two words");
    fixture_teardown(&fx);

cleanup:
    memset(test_irq_pending_pri, 0, sizeof(test_irq_pending_pri));
    memset(test_irq_vector_pri, 0, sizeof(test_irq_vector_pri));
    return rc;
}

static int test_vm1_timer_registers_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word limit = 012345;
    const word csr_value = 000024;

    if (!is_vm1_model(model)) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "vm1_timer_regs", name);
    fixture_setup_model(&fx, model);

    fx.r.TVE_LIMIT = limit;
    ASSERT_EQ(fx.r.load_word(&fx.r, 0177706), limit, "TVE_LIMIT read/write");

    /* Writing to TVE_CSR triggers LIMIT -> COUNT copy in core.
       Since we can't easily trigger core's store logic without code,
       we'll simulate it by setting fields. */
    fx.r.TVE_CSR = (word)(0177400 | csr_value);
    fx.r.TVE_COUNT = fx.r.TVE_LIMIT;

    ASSERT_EQ(fx.r.load_word(&fx.r, 0177710), limit, "TVE_COUNT loads from LIMIT");
    ASSERT_EQ(fx.r.load_word(&fx.r, 0177712), (word)(0177400 | csr_value), "TVE_CSR readback");
    ASSERT_EQ(fx.r.load_byte(&fx.r, 0177713), 0377, "TVE_CSR high byte is 0377");

    /* TVE_COUNT is read-only in core. */
    ASSERT_EQ(fx.r.load_word(&fx.r, 0177710), limit, "TVE_COUNT is read-only");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_vm1_rr_rap_rosh_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];

    if (!is_vm1_model(model)) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "vm1_rr_rap_rosh", name);
    fixture_setup_model(&fx, model);

    ASSERT_EQ(fx.r.load_word(&fx.r, 0177700), 017777, "RR word readback");
    ASSERT_EQ(fx.r.load_word(&fx.r, 0177702), 017777, "RAP word readback");
    ASSERT_EQ(fx.r.load_word(&fx.r, 0177704), 0177340, "ROSH word readback");
    ASSERT_EQ(fx.r.load_byte(&fx.r, 0177700), 0377, "RR low byte");
    ASSERT_EQ(fx.r.load_byte(&fx.r, 0177701), 0037, "RR high byte");
    ASSERT_EQ(fx.r.load_byte(&fx.r, 0177702), 0377, "RAP low byte");
    ASSERT_EQ(fx.r.load_byte(&fx.r, 0177703), 0037, "RAP high byte");
    ASSERT_EQ(fx.r.load_byte(&fx.r, 0177704), 0340, "ROSH low byte");
    ASSERT_EQ(fx.r.load_byte(&fx.r, 0177705), 0376, "ROSH high byte");

    fx.r.VM1_RAP_PRESENT = 0;
    ASSERT_EQ(fx.r.load_word(&fx.r, 0177702), 000000, "RAP clears on field write");
    ASSERT_EQ(fx.r.load_byte(&fx.r, 0177702), 000000, "RAP low byte clears");
    ASSERT_EQ(fx.r.load_byte(&fx.r, 0177703), 000000, "RAP high byte clears");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_vm1_rr_rap_rosh_non_vm1_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];

    if (is_vm1_model(model)) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "vm1_rr_rap_rosh_non", name);
    fixture_setup_model(&fx, model);

    store_word(&fx, 0177700, 012345);
    store_word(&fx, 0177702, 023456);
    store_word(&fx, 0177704, 034567);

    ASSERT_EQ(fx.r.load_word(&fx.r, 0177700), 012345, "RR should be normal RAM");
    ASSERT_EQ(fx.r.load_word(&fx.r, 0177702), 023456, "RAP should be normal RAM");
    ASSERT_EQ(fx.r.load_word(&fx.r, 0177704), 034567, "ROSH should be normal RAM");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_vm1g_timer_irq(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 02400;
    const word new_psw = 000340;
    const word program[] = {
        op_nop(),
    };

    current_test = "vm1g_timer_irq";
    fixture_setup_model(&fx, K1801VM1G);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000270, handler);
    store_word(&fx, 000272, new_psw);
    fx.r.TVE_LIMIT = 000001;
    fx.r.TVE_CSR = 000024; /* RUN|MON */
    fx.r.TVE_COUNT = fx.r.TVE_LIMIT;

    fx.r.r[6] = 01000;
    fx.r.psw = 000000;

    ASSERT_EQ(core_step(&fx.r), 0, "Timer should execute");
    ASSERT_EQ(fx.r.r[7], handler, "Timer IRQ should vector to 000270");
    ASSERT_EQ(fx.r.psw, new_psw, "Timer IRQ should load PSW");
    ASSERT_EQ(fx.r.r[6], 00774, "Timer IRQ should push two words");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_vm1g_eis_diagnostics(void)
{
    cpu_fixture fx;
    int rc = 0;
    word program[1];

    current_test = "vm1g_eis_diag";
    fixture_setup_model(&fx, K1801VM1G);

    program[0] = op_mul(0, operand(0, 1));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 2;
    fx.r.r[1] = 3;
    fx.r.psw = 0;
    ASSERT_EQ(core_step(&fx.r), 0, "MUL should execute");
    ASSERT_EQ(fx.r.r[0], 0, "MUL high word");
    ASSERT_EQ(fx.r.r[1], 6, "MUL low word");

    program[0] = op_div(0, operand(0, 2));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 0;
    fx.r.r[1] = 6;
    fx.r.r[2] = 3;
    fx.r.psw = 0;
    ASSERT_EQ(core_step(&fx.r), 0, "DIV should execute");
    ASSERT_EQ(fx.r.r[0], 2, "DIV quotient");
    ASSERT_EQ(fx.r.r[1], 0, "DIV remainder");

    program[0] = op_ash(0, operand(0, 1));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 1;
    fx.r.r[1] = 1;
    fx.r.psw = 0;
    ASSERT_EQ(core_step(&fx.r), 0, "ASH should execute");
    ASSERT_EQ(fx.r.r[0], 2, "ASH result");

    program[0] = op_ashc(0, operand(0, 2));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 0;
    fx.r.r[1] = 1;
    fx.r.r[2] = 1;
    fx.r.psw = 0;
    ASSERT_EQ(core_step(&fx.r), 0, "ASHC should execute");
    ASSERT_EQ(fx.r.r[0], 0, "ASHC high word");
    ASSERT_EQ(fx.r.r[1], 2, "ASHC low word");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_vm1g_eis_not_illegal(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 05000;
    const word new_psw = 000340;
    word program[1];

    current_test = "vm1g_eis_not_illegal";
    fixture_setup_model(&fx, K1801VM1G);

    store_word(&fx, 000010, handler);
    store_word(&fx, 000012, new_psw);

    program[0] = op_mul(0, operand(0, 1));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 2;
    fx.r.r[1] = 3;
    fx.r.psw = 000003;
    ASSERT_EQ(core_step(&fx.r), 0, "MUL should execute on VM1G");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "MUL should advance PC");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_vm2_sel0_reset(void)
{
    cpu_fixture fx;
    int rc = 0;

    current_test = "vm2_sel0_reset";
    fixture_setup_model(&fx, K1801VM2);

    fx.r.SEL0 = 012340;
    core_reset(&fx.r);

    ASSERT_EQ(fx.r.r[7], 012000, "Reset PC should use SEL0 high byte");
    ASSERT_EQ(fx.r.psw, 000340, "Reset PSW should be 0340");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_vm1_sel1_sel2_regs(void)
{
    cpu_fixture fx;
    int rc = 0;

    current_test = "vm1_sel_regs";
    fixture_setup_model(&fx, K1801VM1);

    store_word(&fx, 0177716, 012345);
    store_word(&fx, 0177714, 054321);
    ASSERT_EQ(fx.r.load_word(&fx.r, 0177716), 012345, "SEL1 word read");
    ASSERT_EQ(fx.r.load_word(&fx.r, 0177714), 054321, "SEL2 word read");
    ASSERT_EQ(fx.r.load_byte(&fx.r, 0177716), 00345, "SEL1 low byte read");
    ASSERT_EQ(fx.r.load_byte(&fx.r, 0177717), 00024, "SEL1 high byte read");
    ASSERT_EQ(fx.r.load_byte(&fx.r, 0177714), 00321, "SEL2 low byte read");
    ASSERT_EQ(fx.r.load_byte(&fx.r, 0177715), 00130, "SEL2 high byte read");

    store_word(&fx, 0177716, 065432);
    store_word(&fx, 0177714, 010765);
    ASSERT_EQ(fx.r.load_word(&fx.r, 0177716), 065432, "SEL1 word write");
    ASSERT_EQ(fx.r.load_word(&fx.r, 0177714), 010765, "SEL2 word write");

    fx.r.store_byte(&fx.r, 0177716, 00076);
    fx.r.store_byte(&fx.r, 0177717, 00123);
    fx.r.store_byte(&fx.r, 0177714, 00112);
    fx.r.store_byte(&fx.r, 0177715, 00007);
    ASSERT_EQ(fx.r.load_word(&fx.r, 0177716), 051476, "SEL1 byte write");
    ASSERT_EQ(fx.r.load_word(&fx.r, 0177714), 003512, "SEL2 byte write");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_reg177750_core_owned(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word program[] = {
        op_mov(operand(3, 7), operand(0, 0)),
        0177750,
        op_mov(operand(2, 7), operand(3, 7)),
        065432,
        0177750,
        op_mov(operand(3, 7), operand(0, 1)),
        0177750,
        op_movb(operand(2, 7), operand(3, 7)),
        000345,
        0177750,
        op_movb(operand(2, 7), operand(3, 7)),
        000024,
        0177751,
        op_mov(operand(3, 7), operand(0, 2)),
        0177750,
    };

    current_test = "dcj11_reg177750_core_owned";
    fixture_setup_model(&fx, DCJ11);

    /* Ensure this register is CPU-owned, not regular RAM-backed storage. */
    fx.mem[0177750] = 000001;
    fx.mem[0177751] = 000002;
    fx.r.J11_MAINT = 001045;
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    ASSERT_EQ(core_step(&fx.r), 0, "MOV @#177750,R0");
    ASSERT_EQ(fx.r.r[0], 001045, "0177750 reset value");

    ASSERT_EQ(core_step(&fx.r), 0, "MOV #065432,@#177750");
    ASSERT_EQ(core_step(&fx.r), 0, "MOV @#177750,R1");
    ASSERT_EQ(fx.r.r[1], 001045, "0177750 is read-only");

    ASSERT_EQ(core_step(&fx.r), 0, "MOVB #345,@#177750");
    ASSERT_EQ(core_step(&fx.r), 0, "MOVB #24,@#177751");
    ASSERT_EQ(core_step(&fx.r), 0, "MOV @#177750,R2");
    ASSERT_EQ(fx.r.r[2], 001045, "0177750 byte writes ignored");

    ASSERT_EQ(fx.mem[0177750], 000001, "0177750 should not modify RAM low byte");
    ASSERT_EQ(fx.mem[0177751], 000002, "0177750 should not modify RAM high byte");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_regblock_177744_177746_core_owned(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word program[] = {
        op_mov(operand(3, 7), operand(0, 0)),
        0177744,
        op_mov(operand(2, 7), operand(3, 7)),
        012345,
        0177744,
        op_mov(operand(3, 7), operand(0, 1)),
        0177744,
        op_mov(operand(3, 7), operand(0, 2)),
        0177746,
        op_mov(operand(2, 7), operand(3, 7)),
        076543,
        0177746,
        op_mov(operand(3, 7), operand(0, 3)),
        0177746,
    };

    current_test = "dcj11_regblock_177744_177746_core_owned";
    fixture_setup_model(&fx, DCJ11);

    fx.mem[0177744] = 000011;
    fx.mem[0177745] = 000022;
    fx.mem[0177746] = 000033;
    fx.mem[0177747] = 000044;
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    ASSERT_EQ(core_step(&fx.r), 0, "MOV @#177744,R0");
    ASSERT_EQ(fx.r.r[0], 000000, "0177744 reset value");

    ASSERT_EQ(core_step(&fx.r), 0, "MOV #012345,@#177744");
    ASSERT_EQ(core_step(&fx.r), 0, "MOV @#177744,R1");
    ASSERT_EQ(fx.r.r[1], 000000, "0177744 write clears MEMERR");

    ASSERT_EQ(core_step(&fx.r), 0, "MOV @#177746,R2");
    ASSERT_EQ(fx.r.r[2], 000000, "0177746 reset value");

    ASSERT_EQ(core_step(&fx.r), 0, "MOV #076543,@#177746");
    ASSERT_EQ(core_step(&fx.r), 0, "MOV @#177746,R3");
    ASSERT_EQ(fx.r.r[3], 076543, "0177746 word write/read");

    ASSERT_EQ(fx.mem[0177744], 000011, "0177744 should not modify RAM low byte");
    ASSERT_EQ(fx.mem[0177745], 000022, "0177744 should not modify RAM high byte");
    ASSERT_EQ(fx.mem[0177746], 000033, "0177746 should not modify RAM low byte");
    ASSERT_EQ(fx.mem[0177747], 000044, "0177746 should not modify RAM high byte");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_regblock_177752_177766_core_owned(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word program[] = {
        op_mov(operand(3, 7), operand(0, 0)),
        0177752,
        op_mov(operand(2, 7), operand(3, 7)),
        012345,
        0177752,
        op_mov(operand(3, 7), operand(0, 1)),
        0177752,
        op_movb(operand(2, 7), operand(3, 7)),
        000067,
        0177753,
        op_mov(operand(3, 7), operand(0, 2)),
        0177752,
        op_mov(operand(2, 7), operand(3, 7)),
        054321,
        0177766,
        op_mov(operand(3, 7), operand(0, 3)),
        0177766,
    };

    current_test = "dcj11_regblock_177752_177766_core_owned";
    fixture_setup_model(&fx, DCJ11);

    /* Ensure these CPU-owned words are not backed by RAM bytes. */
    fx.mem[0177752] = 000011;
    fx.mem[0177753] = 000022;
    fx.mem[0177766] = 000033;
    fx.mem[0177767] = 000044;
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    ASSERT_EQ(core_step(&fx.r), 0, "MOV @#177752,R0");
    ASSERT_EQ(fx.r.r[0], 000000, "0177752 reset value");

    ASSERT_EQ(core_step(&fx.r), 0, "MOV #012345,@#177752");
    ASSERT_EQ(core_step(&fx.r), 0, "MOV @#177752,R1");
    ASSERT_EQ(fx.r.r[1], 000000, "0177752 write clears HITMISS");

    ASSERT_EQ(core_step(&fx.r), 0, "MOVB #067,@#177753");
    ASSERT_EQ(core_step(&fx.r), 0, "MOV @#177752,R2");
    ASSERT_EQ(fx.r.r[2], 000000, "0177752 byte write clears HITMISS");

    ASSERT_EQ(core_step(&fx.r), 0, "MOV #054321,@#1777766");
    ASSERT_EQ(core_step(&fx.r), 0, "MOV @#1777766,R3");
    ASSERT_EQ(fx.r.r[3], 000000, "0177766 write clears CPUERR");

    ASSERT_EQ(fx.mem[0177752], 000011, "0177752 should not modify RAM low byte");
    ASSERT_EQ(fx.mem[0177753], 000022, "0177752 should not modify RAM high byte");
    ASSERT_EQ(fx.mem[0177766], 000033, "0177766 should not modify RAM low byte");
    ASSERT_EQ(fx.mem[0177767], 000044, "0177766 should not modify RAM high byte");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_regblock_177754_177770_core_owned(void)
{
    cpu_fixture fx;
    int rc = 0;

    current_test = "dcj11_regblock_177754_177770_core_owned";
    fixture_setup_model(&fx, DCJ11);

    /* Ensure these CPU-owned words are not backed by RAM bytes. */
    fx.mem[0177754] = 000011;
    fx.mem[0177755] = 000022;
    fx.mem[0177756] = 000033;
    fx.mem[0177757] = 000044;
    fx.mem[0177760] = 000055;
    fx.mem[0177761] = 000066;
    fx.mem[0177762] = 000077;
    fx.mem[0177763] = 000101;
    fx.mem[0177764] = 000111;
    fx.mem[0177765] = 000122;
    fx.mem[0177770] = 000133;
    fx.mem[0177771] = 000144;

    fx.r.J11_RSVD_177754 = 001111;
    fx.r.J11_RSVD_177756 = 002222;
    fx.r.J11_RSVD_177760 = 003333;
    fx.r.J11_RSVD_177762 = 004444;
    fx.r.J11_RSVD_177764 = 005555;
    fx.r.J11_RSVD_177770 = 006666;

    ASSERT_EQ(fx.r.load_word(&fx.r, 0177754), 001111, "0177754 readback");
    ASSERT_EQ(fx.r.load_word(&fx.r, 0177756), 002222, "0177756 readback");
    ASSERT_EQ(fx.r.load_word(&fx.r, 0177760), 003333, "0177760 readback");
    ASSERT_EQ(fx.r.load_word(&fx.r, 0177762), 004444, "0177762 readback");
    ASSERT_EQ(fx.r.load_word(&fx.r, 0177764), 005555, "0177764 readback");
    ASSERT_EQ(fx.r.load_word(&fx.r, 0177770), 006666, "0177770 readback");

    store_word(&fx, 0177754, 012345);
    store_word(&fx, 0177756, 023456);
    store_word(&fx, 0177760, 034567);
    store_word(&fx, 0177762, 045670);
    store_word(&fx, 0177764, 056701);
    store_word(&fx, 0177770, 067012);

    ASSERT_EQ(fx.r.load_word(&fx.r, 0177754), 012345, "0177754 word write");
    ASSERT_EQ(fx.r.load_word(&fx.r, 0177756), 023456, "0177756 word write");
    ASSERT_EQ(fx.r.load_word(&fx.r, 0177760), 034567, "0177760 word write");
    ASSERT_EQ(fx.r.load_word(&fx.r, 0177762), 045670, "0177762 word write");
    ASSERT_EQ(fx.r.load_word(&fx.r, 0177764), 056701, "0177764 word write");
    ASSERT_EQ(fx.r.load_word(&fx.r, 0177770), 067012, "0177770 word write");

    fx.r.store_byte(&fx.r, 0177760, 000076);
    fx.r.store_byte(&fx.r, 0177761, 000123);
    ASSERT_EQ(fx.r.load_word(&fx.r, 0177760),
              (word)((((word)000123) << 8) | (word)000076),
              "0177760 byte write merge");

    ASSERT_EQ(fx.mem[0177754], 000011, "0177754 should not modify RAM low byte");
    ASSERT_EQ(fx.mem[0177755], 000022, "0177754 should not modify RAM high byte");
    ASSERT_EQ(fx.mem[0177756], 000033, "0177756 should not modify RAM low byte");
    ASSERT_EQ(fx.mem[0177757], 000044, "0177756 should not modify RAM high byte");
    ASSERT_EQ(fx.mem[0177760], 000055, "0177760 should not modify RAM low byte");
    ASSERT_EQ(fx.mem[0177761], 000066, "0177760 should not modify RAM high byte");
    ASSERT_EQ(fx.mem[0177762], 000077, "0177762 should not modify RAM low byte");
    ASSERT_EQ(fx.mem[0177763], 000101, "0177762 should not modify RAM high byte");
    ASSERT_EQ(fx.mem[0177764], 000111, "0177764 should not modify RAM low byte");
    ASSERT_EQ(fx.mem[0177765], 000122, "0177764 should not modify RAM high byte");
    ASSERT_EQ(fx.mem[0177770], 000133, "0177770 should not modify RAM low byte");
    ASSERT_EQ(fx.mem[0177771], 000144, "0177770 should not modify RAM high byte");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_non_dcj11_reg177750_is_ram(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word program[] = {
        op_mov(operand(3, 7), operand(0, 0)),
        0177750,
        op_mov(operand(2, 7), operand(3, 7)),
        012345,
        0177750,
        op_mov(operand(3, 7), operand(0, 1)),
        0177750,
    };

    current_test = "non_dcj11_reg177750_is_ram";
    fixture_setup_model(&fx, K1801VM2);

    fx.mem[0177750] = 000011;
    fx.mem[0177751] = 000022;
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    ASSERT_EQ(core_step(&fx.r), 0, "MOV @#177750,R0");
    ASSERT_EQ(fx.r.r[0], 011011, "0177750 should be RAM on VM2");
    ASSERT_EQ(core_step(&fx.r), 0, "MOV #012345,@#177750");
    ASSERT_EQ(core_step(&fx.r), 0, "MOV @#177750,R1");
    ASSERT_EQ(fx.r.r[1], 012345, "VM2 RAM word readback");
    ASSERT_EQ(fx.mem[0177750], 000345, "VM2 RAM low byte");
    ASSERT_EQ(fx.mem[0177751], 000024, "VM2 RAM high byte");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_vm1_tstb_flags_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];

    if (!is_vm1_model(model)) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "vm1_tstb_zero", name);
    fixture_setup_model(&fx, model);
    fx.r.r[0] = 0000000;
    fx.r.psw = FLAG_C | FLAG_V;
    write_op(&fx, op_tstb(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "TSTB");
    ASSERT_EQ(fx.r.r[0], 0000000, "TSTB should not modify");
    expect_flags(&fx.r, 0, 1, 0, 0);

    set_test_name(namebuf, sizeof(namebuf), "vm1_tstb_neg", name);
    fx.r.r[0] = 0000200;
    fx.r.psw = FLAG_C | FLAG_V;
    write_op(&fx, op_tstb(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "TSTB");
    ASSERT_EQ(fx.r.r[0], 0000200, "TSTB should not modify");
    expect_flags(&fx.r, 1, 0, 0, 0);

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_vm1_bpl_after_tstb_model(byte model, const char *name)
{
    cpu_fixture fx_pos;
    cpu_fixture fx_neg;
    int pos_init = 0;
    int neg_init = 0;
    int rc = 0;
    char namebuf[64];
    const word program[] = {
        op_tstb(operand(0, 0)),      /* 0 */
        op_bpl(0003),                /* 2: branch offset is in words */
        op_mov(operand(2, 7), operand(0, 1)), /* 4 */
        000001,                      /* 6 */
        op_br(0002),                 /* 10 */
        op_mov(operand(2, 7), operand(0, 1)), /* 12 */
        000002,                      /* 14 */
        op_nop(),                    /* 16 */
    };

    if (!is_vm1_model(model)) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "vm1_bpl_pos", name);
    fixture_setup_model(&fx_pos, model);
    pos_init = 1;
    load_program(&fx_pos, TEST_BASE, program, sizeof(program) / sizeof(program[0]));
    fx_pos.r.r[0] = 0000001;
    ASSERT_EQ(core_step(&fx_pos.r), 0, "TSTB");
    ASSERT_EQ(core_step(&fx_pos.r), 0, "BPL");
    ASSERT_EQ(fx_pos.r.r[7], TEST_BASE + 0012, "BPL should branch to MOV #2");
    ASSERT_EQ(core_step(&fx_pos.r), 0, "MOV #2");
    ASSERT_EQ(fx_pos.r.r[1], 000002, "BPL should branch when N=0");

    set_test_name(namebuf, sizeof(namebuf), "vm1_bpl_neg", name);
    fixture_setup_model(&fx_neg, model);
    neg_init = 1;
    load_program(&fx_neg, TEST_BASE, program, sizeof(program) / sizeof(program[0]));
    fx_neg.r.r[0] = 0000200;
    ASSERT_EQ(core_step(&fx_neg.r), 0, "TSTB");
    ASSERT_EQ(core_step(&fx_neg.r), 0, "BPL");
    ASSERT_EQ(fx_neg.r.r[7], TEST_BASE + 0004, "BPL should fall through to MOV #1");
    ASSERT_EQ(core_step(&fx_neg.r), 0, "MOV #1");
    ASSERT_EQ(core_step(&fx_neg.r), 0, "BR");
    ASSERT_EQ(fx_neg.r.r[1], 000001, "BPL should not branch when N=1");

cleanup:
    if (pos_init) {
        fixture_teardown(&fx_pos);
    }
    if (neg_init) {
        fixture_teardown(&fx_neg);
    }
    return rc;
}

static int test_vm2_tstb_flags_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];

    if (!is_vm2_model(model)) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "vm2_tstb_zero", name);
    fixture_setup_model(&fx, model);
    fx.r.r[0] = 0000000;
    fx.r.psw = FLAG_C | FLAG_V;
    write_op(&fx, op_tstb(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "TSTB");
    ASSERT_EQ(fx.r.r[0], 0000000, "TSTB should not modify");
    expect_flags(&fx.r, 0, 1, 0, 0);

    set_test_name(namebuf, sizeof(namebuf), "vm2_tstb_neg", name);
    fx.r.r[0] = 0000200;
    fx.r.psw = FLAG_C | FLAG_V;
    write_op(&fx, op_tstb(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "TSTB");
    ASSERT_EQ(fx.r.r[0], 0000200, "TSTB should not modify");
    expect_flags(&fx.r, 1, 0, 0, 0);

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_vm2_bpl_after_tstb_model(byte model, const char *name)
{
    cpu_fixture fx_pos;
    cpu_fixture fx_neg;
    int pos_init = 0;
    int neg_init = 0;
    int rc = 0;
    char namebuf[64];
    const word program[] = {
        op_tstb(operand(0, 0)),      /* 0 */
        op_bpl(0003),                /* 2: branch offset is in words */
        op_mov(operand(2, 7), operand(0, 1)), /* 4 */
        000001,                      /* 6 */
        op_br(0002),                 /* 10 */
        op_mov(operand(2, 7), operand(0, 1)), /* 12 */
        000002,                      /* 14 */
        op_nop(),                    /* 16 */
    };

    if (!is_vm2_model(model)) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "vm2_bpl_pos", name);
    fixture_setup_model(&fx_pos, model);
    pos_init = 1;
    load_program(&fx_pos, TEST_BASE, program, sizeof(program) / sizeof(program[0]));
    fx_pos.r.r[0] = 0000001;
    ASSERT_EQ(core_step(&fx_pos.r), 0, "TSTB");
    ASSERT_EQ(core_step(&fx_pos.r), 0, "BPL");
    ASSERT_EQ(fx_pos.r.r[7], TEST_BASE + 0012, "BPL should branch to MOV #2");
    ASSERT_EQ(core_step(&fx_pos.r), 0, "MOV #2");
    ASSERT_EQ(fx_pos.r.r[1], 000002, "BPL should branch when N=0");

    set_test_name(namebuf, sizeof(namebuf), "vm2_bpl_neg", name);
    fixture_setup_model(&fx_neg, model);
    neg_init = 1;
    load_program(&fx_neg, TEST_BASE, program, sizeof(program) / sizeof(program[0]));
    fx_neg.r.r[0] = 0000200;
    ASSERT_EQ(core_step(&fx_neg.r), 0, "TSTB");
    ASSERT_EQ(core_step(&fx_neg.r), 0, "BPL");
    ASSERT_EQ(fx_neg.r.r[7], TEST_BASE + 0004, "BPL should fall through to MOV #1");
    ASSERT_EQ(core_step(&fx_neg.r), 0, "MOV #1");
    ASSERT_EQ(core_step(&fx_neg.r), 0, "BR");
    ASSERT_EQ(fx_neg.r.r[1], 000001, "BPL should not branch when N=1");

cleanup:
    if (pos_init) {
        fixture_teardown(&fx_pos);
    }
    if (neg_init) {
        fixture_teardown(&fx_neg);
    }
    return rc;
}

static int test_vm2_mtps_preserves_hu_t_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word old_psw = 0000420;
    const word src = 0000347;

    if (!is_vm2_model(model)) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "vm2_mtps_hu_t", name);
    fixture_setup_model(&fx, model);
    fx.r.psw = old_psw;
    fx.r.r[0] = src;
    fx.r.fTrap = 1; /* suppress trace side-effect when T=1 */
    write_op(&fx, op_mtps(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "MTPS should execute");
    ASSERT_EQ(fx.r.psw & 0000400, old_psw & 0000400, "MTPS must preserve H/U");
    ASSERT_EQ(fx.r.psw & 0000020, old_psw & 0000020, "MTPS must preserve T");
    ASSERT_EQ(fx.r.psw & 0000340, src & 0000340, "MTPS must load bits 7:5");
    ASSERT_EQ(fx.r.psw & 0000017, src & 0000017, "MTPS must load bits 3:0");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_vm2_user_vector_forces_hu_zero_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word handler = 02000;
    const word vec_psw = 0000437;
    const word program[] = {
        op_bpt(),
    };

    if (!is_vm2_model(model)) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "vm2_vec_user_hu0", name);
    fixture_setup_model(&fx, model);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));
    store_word(&fx, 000014, handler);
    store_word(&fx, 000016, vec_psw);
    fx.r.psw = 0000000; /* USER: H/U=0 */
    fx.r.r[6] = TEST_STACK;
    ASSERT_EQ(core_step(&fx.r), 0, "BPT should execute");
    ASSERT_EQ(fx.r.psw & 0000400, 0000000, "USER vector must force H/U=0");
    ASSERT_EQ(fx.r.psw & 0000377, vec_psw & 0000377,
              "USER vector must load PSW bits 7:0");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_vm2_halt_vector_allows_hu_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word handler = 02200;
    const word vec_psw = 0000437;
    const word program[] = {
        op_bpt(),
    };

    if (!is_vm2_model(model)) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "vm2_vec_halt_hu", name);
    fixture_setup_model(&fx, model);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));
    store_word(&fx, 000014, handler);
    store_word(&fx, 000016, vec_psw);
    fx.r.psw = 0000400; /* HALT: H/U=1 */
    fx.r.r[6] = TEST_STACK;
    ASSERT_EQ(core_step(&fx.r), 0, "BPT should execute");
    ASSERT_EQ(fx.r.psw & 0000400, 0000400, "HALT vector may keep H/U=1");
    ASSERT_EQ(fx.r.psw & 0000377, vec_psw & 0000377,
              "HALT vector must load PSW bits 7:0");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_vm2_trap_stack_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word handler = 06000;
    const word new_psw = 000340;
    word program[1];

    if (!is_vm2_model(model)) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "vm2_bpt_stack", name);
    fixture_setup_model(&fx, model);
    program[0] = op_bpt();
    load_program(&fx, TEST_BASE, program, 1);
    store_word(&fx, 000014, handler);
    store_word(&fx, 000016, new_psw);
    fx.r.r[6] = 01000;
    fx.r.psw = 000003;
    ASSERT_EQ(core_step(&fx.r), 0, "BPT should execute");
    ASSERT_EQ(fx.r.r[7], handler, "BPT should load vector");
    ASSERT_EQ(fx.r.psw, new_psw, "BPT should load PSW");
    ASSERT_EQ(fx.r.r[6], 00774, "BPT should push two words");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE + 2, "BPT stack PC");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 000003, "BPT stack PSW");
    fixture_teardown(&fx);

    set_test_name(namebuf, sizeof(namebuf), "vm2_iot_stack", name);
    fixture_setup_model(&fx, model);
    program[0] = op_iot();
    load_program(&fx, TEST_BASE, program, 1);
    store_word(&fx, 000020, handler);
    store_word(&fx, 000022, new_psw);
    fx.r.r[6] = 01000;
    fx.r.psw = 000003;
    ASSERT_EQ(core_step(&fx.r), 0, "IOT should execute");
    ASSERT_EQ(fx.r.r[7], handler, "IOT should load vector");
    ASSERT_EQ(fx.r.psw, new_psw, "IOT should load PSW");
    ASSERT_EQ(fx.r.r[6], 00774, "IOT should push two words");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE + 2, "IOT stack PC");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 000003, "IOT stack PSW");
    fixture_teardown(&fx);

    set_test_name(namebuf, sizeof(namebuf), "vm2_emt_stack", name);
    fixture_setup_model(&fx, model);
    program[0] = op_emt(0);
    load_program(&fx, TEST_BASE, program, 1);
    store_word(&fx, 000030, handler);
    store_word(&fx, 000032, new_psw);
    fx.r.r[6] = 01000;
    fx.r.psw = 000003;
    ASSERT_EQ(core_step(&fx.r), 0, "EMT should execute");
    ASSERT_EQ(fx.r.r[7], handler, "EMT should load vector");
    ASSERT_EQ(fx.r.psw, new_psw, "EMT should load PSW");
    ASSERT_EQ(fx.r.r[6], 00774, "EMT should push two words");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE + 2, "EMT stack PC");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 000003, "EMT stack PSW");
    fixture_teardown(&fx);

    set_test_name(namebuf, sizeof(namebuf), "vm2_trap_stack", name);
    fixture_setup_model(&fx, model);
    program[0] = op_trap(0);
    load_program(&fx, TEST_BASE, program, 1);
    store_word(&fx, 000034, handler);
    store_word(&fx, 000036, new_psw);
    fx.r.r[6] = 01000;
    fx.r.psw = 000003;
    ASSERT_EQ(core_step(&fx.r), 0, "TRAP should execute");
    ASSERT_EQ(fx.r.r[7], handler, "TRAP should load vector");
    ASSERT_EQ(fx.r.psw, new_psw, "TRAP should load PSW");
    ASSERT_EQ(fx.r.r[6], 00774, "TRAP should push two words");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE + 2, "TRAP stack PC");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 000003, "TRAP stack PSW");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_vm2_wait_ignores_trace_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word handler = 02000;
    const word new_psw = 000340;
    const word program[] = {
        op_wait(),
    };

    if (!is_vm2_model(model)) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "vm2_wait_ignore_t", name);
    fixture_setup_model(&fx, model);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000014, handler);
    store_word(&fx, 000016, new_psw);
    fx.r.r[6] = 01000;
    store_word(&fx, 00774, 012345);
    store_word(&fx, 00776, 065432);
    fx.r.psw = FLAG_T | 000003;

    ASSERT_EQ(core_step(&fx.r), 0, "WAIT should execute");
    ASSERT_EQ(fx.r.fWait, 1, "WAIT should set fWait");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "WAIT should not trace when T is set");
    ASSERT_EQ(fx.r.psw, (FLAG_T | 000003), "WAIT should not change PSW");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), 012345, "WAIT should not push PC");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 065432, "WAIT should not push PSW");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_vm2_cpc_cpsw_update_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word program[] = {
        op_nop(),
    };

    if (!is_vm2_model(model)) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "vm2_cpc_lock", name);
    fixture_setup_model(&fx, model);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));
    fx.r.cpc = 01234;
    fx.r.cps = 05670;
    fx.r.psw = FLAG_P | FLAG_H;
    ASSERT_EQ(core_step(&fx.r), 0, "NOP should execute");
    ASSERT_EQ(fx.r.cpc, 01234, "CPC should remain locked");
    ASSERT_EQ(fx.r.cps, 05670, "CPSW should remain locked");
    fixture_teardown(&fx);

    set_test_name(namebuf, sizeof(namebuf), "vm2_cpc_update", name);
    fixture_setup_model(&fx, model);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));
    fx.r.cpc = 01234;
    fx.r.cps = 05670;
    fx.r.psw = FLAG_H;
    ASSERT_EQ(core_step(&fx.r), 0, "NOP should execute");
    ASSERT_EQ(fx.r.cpc, TEST_BASE + 2, "CPC should follow PC");
    ASSERT_EQ(fx.r.cps, fx.r.psw, "CPSW should follow PSW");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_vm2_irq_masking_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word handler = 02400;
    const word new_psw = 000340;
    const word program[] = {
        op_nop(),
        op_nop(),
    };

    if (!is_vm2_model(model)) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "vm2_irq_pmask", name);
    fixture_setup_model(&fx, model);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));
    store_word(&fx, 000060, handler);
    store_word(&fx, 000062, new_psw);
    store_word(&fx, handler, op_nop());
    fx.r.r[6] = 01000;
    fx.r.psw = FLAG_P;
    fx.r.poll_irq = test_poll_irq_vector;
    test_irq_vector = 000060;
    test_irq_pending = 1;
    ASSERT_EQ(core_step(&fx.r), 0, "IRQ should be masked by P");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance without IRQ");
    fixture_teardown(&fx);

    set_test_name(namebuf, sizeof(namebuf), "vm2_irq_pri_ignore", name);
    fixture_setup_model(&fx, model);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));
    store_word(&fx, 001060, handler);
    store_word(&fx, 001062, new_psw);
    store_word(&fx, handler, op_nop());
    fx.r.r[6] = 01000;
    fx.r.psw = 000140; /* priority bits set, P clear */
    fx.r.poll_irq = test_poll_irq_vector;
    test_irq_vector = 001060;
    test_irq_pending = 1;
    ASSERT_EQ(core_step(&fx.r), 0, "IRQ should ignore priority bits on VM2");
    ASSERT_EQ(fx.r.r[7], handler, "PC should enter handler");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should load IRQ vector");
    fixture_teardown(&fx);

    set_test_name(namebuf, sizeof(namebuf), "vm2_irq_full_vector", name);
    fixture_setup_model(&fx, model);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));
    store_word(&fx, 0161234, handler);
    store_word(&fx, 0161236, new_psw);
    store_word(&fx, 0001234, 012345);
    store_word(&fx, 0001236, 000200);
    fx.r.r[6] = 01000;
    fx.r.psw = 000000;
    fx.r.poll_irq = test_poll_irq_vector;
    test_irq_vector = 0161234;
    test_irq_pending = 1;
    ASSERT_EQ(core_step(&fx.r), 0, "IRQ should accept full vector");
    ASSERT_EQ(fx.r.r[7], handler, "PC should use full vector address");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should load from full vector");

cleanup:
    fixture_teardown(&fx);
    test_irq_pending = 0;
    test_irq_vector = 0;
    return rc;
}

static int test_vm2_irq_mask_holds_pending_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word handler = 02400;
    const word new_psw = 000340;
    const word program[] = {
        op_nop(),
        op_nop(),
    };

    if (!is_vm2_model(model)) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "vm2_irq_mask_hold", name);
    fixture_setup_model(&fx, model);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));
    store_word(&fx, 000060, handler);
    store_word(&fx, 000062, new_psw);
    store_word(&fx, handler, op_nop());
    fx.r.r[6] = 01000;
    fx.r.psw = FLAG_P;
    fx.r.poll_irq = test_poll_irq_vector;
    test_irq_vector = 000060;
    test_irq_pending = 1;

    ASSERT_EQ(core_step(&fx.r), 0, "Masked IRQ step should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "Masked IRQ should not vector");
    ASSERT_EQ(test_irq_pending, 1, "Masked VM2 IRQ must remain pending");

    fx.r.psw = 000000;
    ASSERT_EQ(core_step(&fx.r), 0, "Unmasked IRQ step should execute");
    ASSERT_EQ(fx.r.r[7], handler, "Pending VM2 IRQ should vector after unmask");
    ASSERT_EQ(fx.r.psw, new_psw, "Vector PSW should load on accepted IRQ");

cleanup:
    fixture_teardown(&fx);
    test_irq_pending = 0;
    test_irq_vector = 0;
    return rc;
}

static int test_vm2_irq_highest_priority_model(byte model, const char *name)
{
    cpu_fixture fx;
    int rc = 0;
    char namebuf[64];
    const word handler_lo = 02000;
    const word handler_hi = 04000;
    const word new_psw = 000340;
    const word program[] = {
        op_nop(),
    };

    if (!is_vm2_model(model)) {
        return 0;
    }

    set_test_name(namebuf, sizeof(namebuf), "vm2_irq_highest", name);
    fixture_setup_model(&fx, model);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000060, handler_lo);
    store_word(&fx, 000062, new_psw);
    store_word(&fx, 003000, handler_hi);
    store_word(&fx, 003002, new_psw);
    store_word(&fx, handler_lo, op_nop());
    store_word(&fx, handler_hi, op_nop());
    ASSERT_EQ(fx.r.load_word(&fx.r, 003000), handler_hi, "Vector should point to handler");

    memset(test_irq_pending_pri, 0, sizeof(test_irq_pending_pri));
    memset(test_irq_vector_pri, 0, sizeof(test_irq_vector_pri));
    test_irq_pending_pri[3] = 1;
    test_irq_vector_pri[3] = 000060;
    test_irq_pending_pri[6] = 1;
    test_irq_vector_pri[6] = 003000;

    fx.r.r[6] = 01000;
    fx.r.psw = 000000;
    fx.r.poll_irq = test_poll_irq_highest;

    ASSERT_EQ(core_step(&fx.r), 0, "IRQ should be accepted");
    ASSERT_EQ(fx.r.r[7], handler_hi, "Highest priority IRQ should be selected");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should load IRQ vector");

cleanup:
    fixture_teardown(&fx);
    memset(test_irq_pending_pri, 0, sizeof(test_irq_pending_pri));
    memset(test_irq_vector_pri, 0, sizeof(test_irq_vector_pri));
    return rc;
}

static int test_dcj11_wait_traces_on_tbit(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 02000;
    const word new_psw = 000340;
    const word program[] = {
        op_wait(),
    };

    current_test = "dcj11_wait_trace_t";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000014, handler);
    store_word(&fx, 000016, new_psw);
    fx.r.r[6] = 01000;
    store_word(&fx, 00774, 012345);
    store_word(&fx, 00776, 065432);
    fx.r.psw = FLAG_T | 000003;

    ASSERT_EQ(core_step(&fx.r), 0, "WAIT should execute");
    ASSERT_EQ(fx.r.fWait, 1, "WAIT should set fWait");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "WAIT should advance PC");
    ASSERT_EQ(fx.r.psw, (FLAG_T | 000003), "WAIT should not change PSW");

    ASSERT_EQ(core_step(&fx.r), 0, "Pending T-bit should end WAIT with TRACE");
    ASSERT_EQ(fx.r.fWait, 0, "TRACE should clear WAIT state");
    ASSERT_EQ(fx.r.r[7], handler, "TRACE should vector from WAIT");
    ASSERT_EQ(fx.r.psw, new_psw, "TRACE should load vector PSW");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE + 2, "TRACE frame PC should be WAIT next PC");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), FLAG_T | 000003, "TRACE frame PSW should preserve T");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_tstb_flags(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word addr = 01200;
    const word program[] = {
        op_tstb(operand(1, 0)),
        op_tstb(operand(1, 0)),
    };

    current_test = "dcj11_tstb_flags";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));
    fx.r.r[0] = addr;

    fx.r.store_byte(&fx.r, addr, 0200);
    ASSERT_EQ(core_step(&fx.r), 0, "TSTB should execute");
    expect_flags(&fx.r, 1, 0, 0, 0);

    fx.r.store_byte(&fx.r, addr, 0000);
    ASSERT_EQ(core_step(&fx.r), 0, "TSTB should execute");
    expect_flags(&fx.r, 0, 1, 0, 0);

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_bpl_after_tstb(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word addr = 01200;
    const word program[] = {
        op_tstb(operand(1, 0)),      /* 0 */
        op_bpl(0002),                /* 2: branch offset is in words, skips MOV #imm (2 words) */
        op_mov(operand(2, 7), operand(0, 1)), /* 4 */
        000001,                      /* 6 */
        op_mov(operand(2, 7), operand(0, 1)), /* 10 */
        000002,                      /* 12 */
    };

    current_test = "dcj11_bpl_after_tstb_pos";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));
    fx.r.r[0] = addr;
    fx.r.store_byte(&fx.r, addr, 000001);

    ASSERT_EQ(core_step(&fx.r), 0, "TSTB should execute");
    ASSERT_EQ(core_step(&fx.r), 0, "BPL should execute");
    ASSERT_EQ(core_step(&fx.r), 0, "MOV #2 should execute");
    ASSERT_EQ(fx.r.r[1], 000002, "BPL should skip MOV #1");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_bpl_after_tstb_neg(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word addr = 01200;
    const word program[] = {
        op_tstb(operand(1, 0)),      /* 0 */
        op_bpl(0002),                /* 2: branch offset is in words, skips MOV #imm (2 words) */
        op_mov(operand(2, 7), operand(0, 1)), /* 4 */
        000001,                      /* 6 */
        op_mov(operand(2, 7), operand(0, 1)), /* 10 */
        000002,                      /* 12 */
    };

    current_test = "dcj11_bpl_after_tstb_neg";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));
    fx.r.r[0] = addr;
    fx.r.store_byte(&fx.r, addr, 0200);

    ASSERT_EQ(core_step(&fx.r), 0, "TSTB should execute");
    ASSERT_EQ(core_step(&fx.r), 0, "BPL should execute");
    ASSERT_EQ(core_step(&fx.r), 0, "MOV #1 should execute");
    ASSERT_EQ(fx.r.r[1], 000001, "BPL should not branch on N=1");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_bpt_stack_order(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 02000;
    const word new_psw = 000340;
    const word program[] = {
        000003, /* BPT */
    };

    current_test = "dcj11_bpt_stack";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000014, handler);
    store_word(&fx, 000016, new_psw);
    fx.r.r[6] = 01000;
    fx.r.psw = 000003;

    ASSERT_EQ(core_step(&fx.r), 0, "BPT should succeed");
    ASSERT_EQ(fx.r.r[7], handler, "PC should load BPT vector");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should load BPT vector");
    ASSERT_EQ(fx.r.r[6], 00774, "SP should push two words");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE + 2, "Stack PC incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 000003, "Stack PSW incorrect");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_iot_stack_order(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 02400;
    const word new_psw = 000340;
    const word program[] = {
        000004, /* IOT */
    };

    current_test = "dcj11_iot_stack";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000020, handler);
    store_word(&fx, 000022, new_psw);
    fx.r.r[6] = 01000;
    fx.r.psw = 000003;

    ASSERT_EQ(core_step(&fx.r), 0, "IOT should succeed");
    ASSERT_EQ(fx.r.r[7], handler, "PC should load IOT vector");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should load IOT vector");
    ASSERT_EQ(fx.r.r[6], 00774, "SP should push two words");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE + 2, "Stack PC incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 000003, "Stack PSW incorrect");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_bus_error_stack_order(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 02600;
    const word new_psw = 000340;
    const word program[] = {
        op_mov(operand(1, 0), operand(0, 1)), /* MOV (R0), R1 */
    };

    current_test = "dcj11_bus_error_stack";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000004, handler);
    store_word(&fx, 000006, new_psw);
    fx.r.r[0] = 000001; /* odd address triggers bus error on word fetch */
    fx.r.r[6] = 01000;
    fx.r.psw = 000003;

    ASSERT_EQ(core_step(&fx.r), 0, "Bus error should trap");
    ASSERT_EQ(fx.r.r[7], handler, "PC should load bus error vector");
    ASSERT_EQ(fx.r.psw & 0177760, new_psw & 0177760, "PSW should load bus error vector");
    ASSERT_EQ(fx.r.r[6], 00774, "SP should push two words");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE + 2, "Stack PC incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 000003, "Stack PSW incorrect");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_yellow_stack_trap_autodec_sp(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 02600;
    const word new_psw = 000340;
    const word program[] = {
        op_mov(operand(2, 7), operand(4, 6)), /* MOV #1234, -(SP) */
        01234,
    };

    current_test = "dcj11_yellow_stack_autodec";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000004, handler);
    store_word(&fx, 000006, new_psw);
    fx.r.r[6] = 000400;
    fx.r.psw = 000003;

    ASSERT_EQ(core_step(&fx.r), 0, "MOV #- (SP) should execute then trap");
    ASSERT_EQ(fx.r.r[7], handler, "PC should load stack trap vector");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should load stack trap vector");
    ASSERT_EQ((fx.r.J11_CPUERR & 0000010), 0000010, "CPUERR.YEL should be set");
    ASSERT_EQ(fx.r.r[6], 000372, "SP should include MOV push and trap frame");
    ASSERT_EQ(fx.r.load_word(&fx.r, 000376), 01234, "MOV destination should be written before trap");
    ASSERT_EQ(fx.r.load_word(&fx.r, 000372), TEST_BASE + 4, "Stack trap frame PC incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, 000374), 000001, "Stack trap frame PSW incorrect");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_yellow_stack_trap_on_bpt_push(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word bpt_handler = 02000;
    const word stk_handler = 02400;
    const word new_psw = 000340;
    const word program[] = {
        op_bpt(),
    };

    current_test = "dcj11_yellow_stack_bpt_push";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000014, bpt_handler);
    store_word(&fx, 000016, new_psw);
    store_word(&fx, 000004, stk_handler);
    store_word(&fx, 000006, new_psw);
    fx.r.r[6] = 000400;
    fx.r.psw = 000003;

    ASSERT_EQ(core_step(&fx.r), 0, "BPT should trigger follow-up yellow stack trap");
    ASSERT_EQ(fx.r.r[7], stk_handler, "PC should end in stack trap handler");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should match stack trap vector");
    ASSERT_EQ((fx.r.J11_CPUERR & 0000010), 0000010, "CPUERR.YEL should be set");
    ASSERT_EQ(fx.r.r[6], 000370, "SP should include BPT and stack-trap frames");
    ASSERT_EQ(fx.r.load_word(&fx.r, 000370), bpt_handler, "Yellow trap frame PC incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, 000372), new_psw, "Yellow trap frame PSW incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, 000374), TEST_BASE + 2, "BPT frame PC incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, 000376), 000003, "BPT frame PSW incorrect");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_stack_limit_boundary_no_trap(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 02600;
    const word new_psw = 000340;
    const word program[] = {
        op_mov(operand(2, 7), operand(4, 6)), /* MOV #777, -(SP) */
        000777,
    };

    current_test = "dcj11_stack_limit_boundary";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000004, handler);
    store_word(&fx, 000006, new_psw);
    fx.r.r[6] = 000402; /* post-decrement address = 0400 (limit boundary) */
    fx.r.psw = 000003;

    ASSERT_EQ(core_step(&fx.r), 0, "MOV #- (SP) at boundary should not trap");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 4, "PC should advance normally");
    ASSERT_EQ(fx.r.r[6], 000400, "SP should decrement to boundary");
    ASSERT_EQ((fx.r.J11_CPUERR & 0000010), 0000000, "CPUERR.YEL should remain clear");
    ASSERT_EQ(fx.r.load_word(&fx.r, 000400), 000777, "Destination write should complete");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_red_stack_trap_on_vector_push_abort(void)
{
#if defined(ENABLE_MMU) && (ENABLE_MMU)
    cpu_fixture fx;
    int rc = 0;
    const word bpt_handler = 02000;
    const word red_handler = 03000;
    const word new_psw = 000340;
    const word program[] = {
        op_bpt(),
    };

    current_test = "dcj11_red_stack_push_abort";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000014, bpt_handler);
    store_word(&fx, 000016, new_psw);
    store_word(&fx, 000004, red_handler);
    store_word(&fx, 000006, new_psw);

    fx.r.psw = 000003;
    fx.r.r[6] = 020004;

    /* MMU on + kernel split I/D: vectors in KD seg0, stack push faults in KD seg1. */
    fx.r.mmu_ssr0 = 0000001;
    fx.r.mmu_ssr3 = 0000004;
    fx.r.mmu_par[0][0][0] = 0000000;
    fx.r.mmu_pdr[0][0][0] = 0177006;
    fx.r.mmu_par[0][1][0] = 0000000;
    fx.r.mmu_pdr[0][1][0] = 0177006;
    fx.r.mmu_par[0][1][1] = 0000000;
    fx.r.mmu_pdr[0][1][1] = 0000000;

    ASSERT_EQ(core_step(&fx.r), 0, "Red stack fallback should complete trap handling");
    ASSERT_EQ(fx.r.r[7], red_handler, "PC should load red stack vector");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should load red stack vector");
    ASSERT_EQ(fx.r.r[6], 000000, "Emergency stack should end at 0");
    ASSERT_EQ(fx.r.load_word(&fx.r, 000000), TEST_BASE + 2, "Emergency stack PC incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, 000002), 000003, "Emergency stack PSW incorrect");
    ASSERT_EQ((fx.r.J11_CPUERR & 0000004), 0000004, "CPUERR.RED should be set");

cleanup:
    fixture_teardown(&fx);
    return rc;
#else
    current_test = "dcj11_red_stack_push_abort";
    return 0;
#endif
}

static int test_dcj11_trace_priority_over_yellow_stack(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word trace_handler = 02000;
    const word stack_handler = 02400;
    const word vector_psw = 000340;
    const word program[] = {
        op_mov(operand(2, 7), operand(4, 6)), /* MOV #1, -(SP) */
        000001,
    };

    current_test = "dcj11_trace_priority_over_yellow";
    fixture_setup_model(&fx, DCJ11);
    load_program(&fx, TEST_BASE, program, sizeof(program) / sizeof(program[0]));

    store_word(&fx, 000014, trace_handler);
    store_word(&fx, 000016, vector_psw);
    store_word(&fx, 000004, stack_handler);
    store_word(&fx, 000006, vector_psw);

    fx.r.psw = 000020; /* T-bit set */
    fx.r.r[6] = 000400;

    ASSERT_EQ(core_step(&fx.r), 0, "MOV should trigger trace then yellow");
    ASSERT_EQ(fx.r.r[7], stack_handler, "Yellow stack trap should run after trace");
    ASSERT_EQ(fx.r.psw, vector_psw, "PSW should match stack trap vector");
    ASSERT_EQ((fx.r.J11_CPUERR & 0000010), 0000010, "CPUERR.YEL should be set");
    ASSERT_EQ(fx.r.r[6], 000366, "SP should include MOV + trace + stack trap frames");
    ASSERT_EQ(fx.r.load_word(&fx.r, 000366), trace_handler, "Yellow frame PC should be trace handler");
    ASSERT_EQ(fx.r.load_word(&fx.r, 000370), vector_psw, "Yellow frame PSW should be trace PSW");
    ASSERT_EQ(fx.r.load_word(&fx.r, 000372), TEST_BASE + 4, "Trace frame PC should resume after MOV");
    ASSERT_EQ(fx.r.load_word(&fx.r, 000374), 000020, "Trace frame PSW should keep original T-bit");
    ASSERT_EQ(fx.r.load_word(&fx.r, 000376), 000001, "MOV destination write should complete");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

int main(void)
{
    int failed = 0;

    failed += test_mov_updates_flags();
    failed += test_movb_sign_extends_register_dest();
    failed += test_add_sets_expected_flags();
    failed += test_subtract_with_borrow();
    failed += test_branch_on_equal_skips_instruction();
    failed += test_jsr_and_rts_restore_context();
    failed += test_sob_loops_expected_count();
    failed += test_emt_pushes_and_vectors();
    failed += test_trap_pushes_and_vectors();
    failed += test_vm1_trap_vectors_ignore_code();
    failed += test_emt_trap_ignore_code_model(K1801VM2, "K1801VM2");
    failed += test_emt_trap_ignore_code_model(K1806VM2, "K1806VM2");
    failed += test_emt_trap_ignore_code_model(DCJ11, "DCJ11");
    failed += test_vm1_illegal_instructions_trap();
    failed += test_dcj11_special_ops_illegal_on_other_models();
    failed += test_dcj11_tstset_wrtlck_mode0_illegal();
    failed += test_dcj11_alignment_trap();
    failed += test_dcj11_alignment_trap_store();
    failed += test_dcj11_alignment_trap_fetch();
    failed += test_vm_ifetch_bus_error_increments_pc_model(K1801VM1, "K1801VM1");
    failed += test_vm_ifetch_bus_error_increments_pc_model(K1801VM1G, "K1801VM1G");
    failed += test_vm_ifetch_bus_error_increments_pc_model(K1801VM2, "K1801VM2");
    failed += test_vm_ifetch_bus_error_increments_pc_model(K1806VM2, "K1806VM2");
    failed += test_dcj11_ifetch_bus_error_preserves_pc();
    failed += test_dcj11_ifetch_internal_mmu_reg_traps_addr(0177572, "mmr0");
    failed += test_dcj11_ifetch_internal_mmu_reg_traps_addr(0177660, "usr_d_par0");
    failed += test_vm_sp_bus_error_uses_vector4_model(K1801VM1, "K1801VM1");
    failed += test_vm_sp_bus_error_uses_vector4_model(K1801VM1G, "K1801VM1G");
    failed += test_vm_sp_bus_error_uses_vector4_model(K1801VM2, "K1801VM2");
    failed += test_vm_sp_bus_error_uses_vector4_model(K1806VM2, "K1806VM2");
    failed += test_extended_ops_supported();
    failed += test_vm1_eis_illegal();
    failed += test_condition_codes();
    failed += test_wait_and_reset();
    failed += test_dcj11_wait_resume_on_irq();
    failed += test_vm1_wait_ignores_trace_model(K1801VM1, "K1801VM1");
    failed += test_vm1_wait_ignores_trace_model(K1801VM1G, "K1801VM1G");
    failed += test_halt();
    failed += test_external_halt();
    failed += test_vm2_external_halt_masked();
    failed += test_dcj11_halt_user_traps();
    failed += test_dcj11_reset_user_is_nop();
    failed += test_dcj11_reset_kernel_clears_pirq_preserves_cpuerr_mmr1_mmr2();
    failed += test_branches();
    failed += test_jmp();
    failed += test_jmp_jsr_autoinc_mode2();
    failed += test_jmp_jsr_mode0_trap();
    failed += test_reg_source_order_split();
    failed += test_addressing_modes();
    failed += test_single_operand_word_ops();
    failed += test_single_operand_byte_ops();
    failed += test_misc_ops();
    failed += test_double_operand_word_ops();
    failed += test_double_operand_byte_ops();
    failed += test_vm2_privileged_ops();
    failed += test_vm2_halt_ops_illegal_model(K1801VM2, "K1801VM2");
    failed += test_vm2_halt_ops_illegal_model(K1806VM2, "K1806VM2");
    failed += test_vm2_start_irq_model(K1801VM2, "K1801VM2");
    failed += test_vm2_start_irq_model(K1806VM2, "K1806VM2");
    failed += test_vm2_step_no_irq_model(K1801VM2, "K1801VM2");
    failed += test_vm2_step_no_irq_model(K1806VM2, "K1806VM2");
    failed += test_vm2_step_halt_signal_model(K1801VM2, "K1801VM2");
    failed += test_vm2_step_halt_signal_model(K1806VM2, "K1806VM2");
    failed += test_vm2_step_halt_masked_model(K1801VM2, "K1801VM2");
    failed += test_vm2_step_halt_masked_model(K1806VM2, "K1806VM2");
    failed += test_vm2_step_halt_unmasked_model(K1801VM2, "K1801VM2");
    failed += test_vm2_step_halt_unmasked_model(K1806VM2, "K1806VM2");
    failed += test_vm2_step_halt_unmasked_p0_model(K1801VM2, "K1801VM2");
    failed += test_vm2_step_halt_unmasked_p0_model(K1806VM2, "K1806VM2");
    failed += test_vm2_fis_trap_model(K1801VM2, "K1801VM2");
    failed += test_vm2_fis_trap_model(K1806VM2, "K1806VM2");
    failed += test_vm2_fis_missing_traps_illegal_model(K1801VM2, "K1801VM2");
    failed += test_vm2_fis_missing_traps_illegal_model(K1806VM2, "K1806VM2");
    failed += test_vm2_fis_error_trap_model(K1801VM2, "K1801VM2");
    failed += test_vm2_fis_error_trap_model(K1806VM2, "K1806VM2");
    failed += test_fis_odd_register_allowed_model(K1801VM2, "K1801VM2");
    failed += test_fis_odd_register_allowed_model(K1806VM2, "K1806VM2");
    failed += test_fis_odd_register_allowed_model(DCJ11, "DCJ11");
    failed += test_fp11_divf_divzero_trap();
    failed += test_dcj11_special_ops();
    failed += test_dcj11_mxpi_prev_mode2_uses_user_sp();
    failed += test_dcj11_csm_disabled_illegal();
    failed += test_dcj11_csm_user_to_supervisor();
    failed += test_dcj11_mmr1_jsr_records_sp_delta();
    failed += test_dcj11_mmr1_mfpi_records_sp_delta();
    failed += test_dcj11_mmr1_mxpi_pop_records_sp_delta();
    failed += test_dcj11_mmr1_autoinc_deferred_records_before_fault();
    failed += test_dcj11_mmu_illegal_mode2_aborts();
    failed += test_dcj11_pdrw_set_on_internal_reg_write();
    failed += test_bpt_vectors();
    failed += test_iot_vectors();
    failed += test_trace_vector();
    failed += test_rtt_restores_and_sets_ftrap();
    failed += test_rtt_skips_trace_once();
    failed += test_trace_rearms_after_handler();
    failed += test_trace_rearms_with_t_in_vector();
    failed += test_trace_stops_when_t_cleared();
    failed += test_rti_restores_state();
    failed += test_dcj11_rti_traces_immediately_when_t_restored();
    failed += test_dcj11_rtt_traces_after_one_instruction();
    failed += test_dcj11_rti_restores_state();
    failed += test_dcj11_explicit_psw_write_preserves_t();
    failed += test_dcj11_rti_user_restricts_psw();
    failed += test_dcj11_rti_user_sets_high_psw_bits();
    failed += test_dcj11_mode_stack_banking();
    failed += test_dcj11_register_set_banking();
    failed += test_dcj11_irq_entry_frame_user();
    failed += test_dcj11_rti_restore_user_mode_stack();
    failed += test_dcj11_irq_rti_supervisor_mode();
    failed += test_dcj11_irq_rti_mode_stack_switch();
    failed += test_dcj11_mtps_user_restricts_psw();
    failed += test_dcj11_spl_kernel_sets_priority();
    failed += test_dcj11_spl_user_is_nop();
    failed += test_spl_illegal_on_other_models();
    failed += test_vm2_rti_restores_state_model(K1801VM2, "K1801VM2");
    failed += test_vm2_rti_restores_state_model(K1806VM2, "K1806VM2");
    failed += test_vm2_rti_hu_restore_model(K1801VM2, "K1801VM2");
    failed += test_vm2_rti_hu_restore_model(K1806VM2, "K1806VM2");
    failed += test_vm1_rti_restores_flags_model(K1801VM1, "K1801VM1");
    failed += test_vm1_rti_restores_flags_model(K1801VM1G, "K1801VM1G");
    failed += test_keyboard_irq_vector();
    failed += test_irq_mask_blocks_interrupt();
    failed += test_dcj11_irq_priority_mask();
    failed += test_dcj11_irq_highest_priority();
    failed += test_dcj11_irq_preempt_before_first_isr_instruction();
    failed += test_dcj11_irq_vector_mask();
    failed += test_vm1_irq_masking_model(K1801VM1, "K1801VM1");
    failed += test_vm1_irq_masking_model(K1801VM1G, "K1801VM1G");
    failed += test_vm1_irq_priority_selection_model(K1801VM1, "K1801VM1");
    failed += test_vm1_irq_priority_selection_model(K1801VM1G, "K1801VM1G");
    failed += test_vm2_irq_masking_model(K1801VM2, "K1801VM2");
    failed += test_vm2_irq_masking_model(K1806VM2, "K1806VM2");
    failed += test_vm2_irq_mask_holds_pending_model(K1801VM2, "K1801VM2");
    failed += test_vm2_irq_mask_holds_pending_model(K1806VM2, "K1806VM2");
    failed += test_vm2_irq_highest_priority_model(K1801VM2, "K1801VM2");
    failed += test_vm2_irq_highest_priority_model(K1806VM2, "K1806VM2");
    failed += test_vm1_timer_registers_model(K1801VM1, "K1801VM1");
    failed += test_vm1_timer_registers_model(K1801VM1G, "K1801VM1G");
    failed += test_vm1_rr_rap_rosh_model(K1801VM1, "K1801VM1");
    failed += test_vm1_rr_rap_rosh_model(K1801VM1G, "K1801VM1G");
    failed += test_vm1_rr_rap_rosh_non_vm1_model(K1801VM2, "K1801VM2");
    failed += test_vm1_rr_rap_rosh_non_vm1_model(K1806VM2, "K1806VM2");
    failed += test_vm1_rr_rap_rosh_non_vm1_model(DCJ11, "DCJ11");
    failed += test_vm1g_timer_irq();
    failed += test_vm1g_eis_diagnostics();
    failed += test_vm1g_eis_not_illegal();
    failed += test_vm2_sel0_reset();
    failed += test_vm1_sel1_sel2_regs();
    failed += test_dcj11_regblock_177744_177746_core_owned();
    failed += test_dcj11_reg177750_core_owned();
    failed += test_dcj11_regblock_177752_177766_core_owned();
    failed += test_dcj11_regblock_177754_177770_core_owned();
    failed += test_non_dcj11_reg177750_is_ram();
    failed += test_vm1_tstb_flags_model(K1801VM1, "K1801VM1");
    failed += test_vm1_tstb_flags_model(K1801VM1G, "K1801VM1G");
    failed += test_vm1_bpl_after_tstb_model(K1801VM1, "K1801VM1");
    failed += test_vm1_bpl_after_tstb_model(K1801VM1G, "K1801VM1G");
    failed += test_vm2_tstb_flags_model(K1801VM2, "K1801VM2");
    failed += test_vm2_tstb_flags_model(K1806VM2, "K1806VM2");
    failed += test_vm2_bpl_after_tstb_model(K1801VM2, "K1801VM2");
    failed += test_vm2_bpl_after_tstb_model(K1806VM2, "K1806VM2");
    failed += test_vm2_mtps_preserves_hu_t_model(K1801VM2, "K1801VM2");
    failed += test_vm2_mtps_preserves_hu_t_model(K1806VM2, "K1806VM2");
    failed += test_vm2_user_vector_forces_hu_zero_model(K1801VM2, "K1801VM2");
    failed += test_vm2_user_vector_forces_hu_zero_model(K1806VM2, "K1806VM2");
    failed += test_vm2_halt_vector_allows_hu_model(K1801VM2, "K1801VM2");
    failed += test_vm2_halt_vector_allows_hu_model(K1806VM2, "K1806VM2");
    failed += test_vm2_trap_stack_model(K1801VM2, "K1801VM2");
    failed += test_vm2_trap_stack_model(K1806VM2, "K1806VM2");
    failed += test_vm2_wait_ignores_trace_model(K1801VM2, "K1801VM2");
    failed += test_vm2_wait_ignores_trace_model(K1806VM2, "K1806VM2");
    failed += test_dcj11_wait_traces_on_tbit();
    failed += test_vm2_cpc_cpsw_update_model(K1801VM2, "K1801VM2");
    failed += test_vm2_cpc_cpsw_update_model(K1806VM2, "K1806VM2");
    failed += test_dcj11_tstb_flags();
    failed += test_dcj11_bpl_after_tstb();
    failed += test_dcj11_bpl_after_tstb_neg();
    failed += test_dcj11_bpt_stack_order();
    failed += test_dcj11_iot_stack_order();
    failed += test_dcj11_bus_error_stack_order();
    failed += test_dcj11_yellow_stack_trap_autodec_sp();
    failed += test_dcj11_yellow_stack_trap_on_bpt_push();
    failed += test_dcj11_stack_limit_boundary_no_trap();
    failed += test_dcj11_red_stack_trap_on_vector_push_abort();
    failed += test_dcj11_trace_priority_over_yellow_stack();

    if (failed) {
        fprintf(stderr, "%d test(s) failed\n", failed);
        return 1;
    }

    printf("All core instruction tests passed\n");
    return 0;
}
