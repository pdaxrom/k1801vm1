#include <stdio.h>
#include <string.h>

#include "core/core.h"
#include "core/hardware.h"

#define TEST_BASE 01000
#define TEST_STACK 0400

typedef struct {
    regs r;
    byte *mem;
} cpu_fixture;

static const char *current_test;

static void fixture_setup_model(cpu_fixture *fx, byte model)
{
    memset(fx, 0, sizeof(*fx));
    fx->r.model = model;
    hwstub_connect(&fx->r);
    core_init(&fx->r);
    fx->mem = fx->r.ramptr(&fx->r, 0);
    memset(fx->mem, 0, 1 << 16);
    fx->r.SEL0 = 0;
    fx->r.SEL1 = 0;
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

static int test_irq_pending;
static int test_irq_priority;
static word test_last_vector;
static int test_irq_called;
static int test_irq_pending_pri[8];
static word test_irq_vector_pri[8];
static word test_irq_vector;

static INLINE int is_vm2_model(byte model);
static INLINE int is_vm1_model(byte model);

static int test_poll_irq(regs *r, word *vector)
{
    (void)r;
    if (test_irq_pending) {
        test_irq_pending = 0;
        if (vector) {
            *vector = (word)(000060 | ((test_irq_priority & 07) << 9));
            test_last_vector = *vector;
        }
        test_irq_called = 1;
        return 1;
    }
    return 0;
}

static int test_poll_irq_vector(regs *r, word *vector)
{
    (void)r;
    if (!test_irq_pending) {
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
            best = pri;
            break;
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

        snprintf(namebuf, sizeof(namebuf), "mfpd_illegal_%s", names[i]);
        rc += run_illegal_op_test_model(op_mfpd(operand(0, 0)), namebuf, models[i]);

        snprintf(namebuf, sizeof(namebuf), "mfpi_illegal_%s", names[i]);
        rc += run_illegal_op_test_model(op_mfpi(operand(0, 0)), namebuf, models[i]);

        snprintf(namebuf, sizeof(namebuf), "mtpi_illegal_%s", names[i]);
        rc += run_illegal_op_test_model(op_mtpi(operand(0, 0)), namebuf, models[i]);

        snprintf(namebuf, sizeof(namebuf), "mtpd_illegal_%s", names[i]);
        rc += run_illegal_op_test_model(op_mtpd(operand(0, 0)), namebuf, models[i]);

        snprintf(namebuf, sizeof(namebuf), "tstset_illegal_%s", names[i]);
        rc += run_illegal_op_test_model(op_tstset(operand(0, 1)), namebuf, models[i]);

        snprintf(namebuf, sizeof(namebuf), "wrtlck_illegal_%s", names[i]);
        rc += run_illegal_op_test_model(op_wrtlck(operand(0, 1)), namebuf, models[i]);
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
    expect_flags(&fx.r, 0, 0, 1, 1);
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after DIV by zero");
    fixture_teardown(&fx);
    active = 0;

    set_test_name(namebuf, sizeof(namebuf), "div_overflow", name);
    fixture_setup_model(&fx, model);
    active = 1;
    program[0] = op_div(0, operand(0, 2));
    load_program(&fx, TEST_BASE, program, 1);
    fx.r.r[0] = 0;
    fx.r.r[1] = 077777;
    fx.r.r[2] = 1;
    fx.r.psw = 0;
    ASSERT_EQ(core_step(&fx.r), 0, "DIV overflow should execute");
    expect_flags(&fx.r, 0, 0, 1, 0);
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance after DIV overflow");
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
    store_word(&fx, 0177716, 0200);
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
        ASSERT_EQ(fx.r.load_word(&fx.r, 0177716), 0210, "HALT should set SEL1 bit 010");
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
    store_word(&fx, 0177716, 0200);
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
        ASSERT_EQ(fx.r.load_word(&fx.r, 0177716), 0210, "HALT signal should set SEL1 bit 010");
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
    const word new_psw = 000200;
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

    set_test_name(namebuf, sizeof(namebuf), "mfps", name);
    fx.r.psw = 000345;
    write_op(&fx, op_mfps(operand(0, 0)));
    ASSERT_EQ(core_step(&fx.r), 0, "MFPS");
    ASSERT_EQ(fx.r.r[0] & 0377, 0345, "MFPS should move PSW");
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
        op_fis(0),
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

    fx.r.r[6] = 01000;
    fx.r.psw = 000000;
    fx.r.fFisError = 1;

    ASSERT_EQ(core_step(&fx.r), 0, "FIS error should trap to 0244");
    ASSERT_EQ(fx.r.r[7], handler, "FIS error should load vector");
    ASSERT_EQ(fx.r.psw, new_psw, "FIS error should load PSW");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE + 2, "Stack PC after FIS error");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 000000, "Stack PSW after FIS error");

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

    store_word(&fx, 0177706, limit);
    ASSERT_EQ(fx.r.load_word(&fx.r, 0177706), limit, "TVE_LIMIT read/write");

    store_word(&fx, 0177712, csr_value);
    ASSERT_EQ(fx.r.load_word(&fx.r, 0177710), limit, "TVE_COUNT loads from LIMIT");
    ASSERT_EQ(fx.r.load_word(&fx.r, 0177712), (word)(0177400 | csr_value), "TVE_CSR readback");
    ASSERT_EQ(fx.r.load_byte(&fx.r, 0177713), 0377, "TVE_CSR high byte is 0377");

    store_word(&fx, 0177710, 000000);
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
    ASSERT_EQ(fx.r.load_byte(&fx.r, 0177701), 0177, "RR high byte");
    ASSERT_EQ(fx.r.load_byte(&fx.r, 0177702), 0377, "RAP low byte");
    ASSERT_EQ(fx.r.load_byte(&fx.r, 0177703), 0177, "RAP high byte");
    ASSERT_EQ(fx.r.load_byte(&fx.r, 0177704), 0340, "ROSH low byte");
    ASSERT_EQ(fx.r.load_byte(&fx.r, 0177705), 0177, "ROSH high byte");

    fx.r.store_word(&fx.r, 0177702, 000000);
    ASSERT_EQ(fx.r.load_word(&fx.r, 0177702), 000000, "RAP clears on word write");
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
    store_word(&fx, 0177706, 000001);
    store_word(&fx, 0177712, 000024); /* RUN|MON */

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

static int test_dcj11_sel0_reset(void)
{
    cpu_fixture fx;
    int rc = 0;

    current_test = "dcj11_sel0_reset";
    fixture_setup_model(&fx, DCJ11);

    fx.r.SEL0 = 012340;
    fx.r.SEL1 = 077700;
    core_reset(&fx.r);

    ASSERT_EQ(fx.r.r[7], 012000, "Reset PC should use SEL0 high byte");
    ASSERT_EQ(fx.r.psw, 000340, "Reset PSW should be 0340");

cleanup:
    fixture_teardown(&fx);
    return rc;
}

static int test_dcj11_sel1_sel2_regs(void)
{
    cpu_fixture fx;
    int rc = 0;

    current_test = "dcj11_sel_regs";
    fixture_setup_model(&fx, DCJ11);

    fx.r.SEL1 = 012345;
    fx.r.SEL2 = 054321;
    ASSERT_EQ(fx.r.load_word(&fx.r, 0177716), 012345, "SEL1 word read");
    ASSERT_EQ(fx.r.load_word(&fx.r, 0177714), 054321, "SEL2 word read");
    ASSERT_EQ(fx.r.load_byte(&fx.r, 0177716), 00345, "SEL1 low byte read");
    ASSERT_EQ(fx.r.load_byte(&fx.r, 0177717), 00024, "SEL1 high byte read");
    ASSERT_EQ(fx.r.load_byte(&fx.r, 0177714), 00321, "SEL2 low byte read");
    ASSERT_EQ(fx.r.load_byte(&fx.r, 0177715), 00130, "SEL2 high byte read");

    store_word(&fx, 0177716, 065432);
    store_word(&fx, 0177714, 010765);
    ASSERT_EQ(fx.r.SEL1, 065432, "SEL1 word write");
    ASSERT_EQ(fx.r.SEL2, 010765, "SEL2 word write");

    fx.r.store_byte(&fx.r, 0177716, 00076);
    fx.r.store_byte(&fx.r, 0177717, 00123);
    fx.r.store_byte(&fx.r, 0177714, 00112);
    fx.r.store_byte(&fx.r, 0177715, 00007);
    ASSERT_EQ(fx.r.SEL1, 051476, "SEL1 byte write");
    ASSERT_EQ(fx.r.SEL2, 003512, "SEL2 byte write");

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

static int test_dcj11_wait_ignores_trace(void)
{
    cpu_fixture fx;
    int rc = 0;
    const word handler = 02000;
    const word new_psw = 000340;
    const word program[] = {
        op_wait(),
    };

    current_test = "dcj11_wait_ignore_t";
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
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "WAIT should not trace when T is set");
    ASSERT_EQ(fx.r.psw, (FLAG_T | 000003), "WAIT should not change PSW");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), 012345, "WAIT should not push PC");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00776), 065432, "WAIT should not push PSW");

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
    failed += test_branches();
    failed += test_jmp();
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
    failed += test_dcj11_special_ops();
    failed += test_bpt_vectors();
    failed += test_iot_vectors();
    failed += test_trace_vector();
    failed += test_rtt_restores_and_sets_ftrap();
    failed += test_rtt_skips_trace_once();
    failed += test_trace_rearms_after_handler();
    failed += test_trace_rearms_with_t_in_vector();
    failed += test_trace_stops_when_t_cleared();
    failed += test_rti_restores_state();
    failed += test_dcj11_rti_restores_state();
    failed += test_dcj11_rti_user_restricts_psw();
    failed += test_dcj11_mtps_user_restricts_psw();
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
    failed += test_dcj11_irq_vector_mask();
    failed += test_vm1_irq_masking_model(K1801VM1, "K1801VM1");
    failed += test_vm1_irq_masking_model(K1801VM1G, "K1801VM1G");
    failed += test_vm1_irq_priority_selection_model(K1801VM1, "K1801VM1");
    failed += test_vm1_irq_priority_selection_model(K1801VM1G, "K1801VM1G");
    failed += test_vm2_irq_masking_model(K1801VM2, "K1801VM2");
    failed += test_vm2_irq_masking_model(K1806VM2, "K1806VM2");
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
    failed += test_dcj11_sel0_reset();
    failed += test_dcj11_sel1_sel2_regs();
    failed += test_vm1_tstb_flags_model(K1801VM1, "K1801VM1");
    failed += test_vm1_tstb_flags_model(K1801VM1G, "K1801VM1G");
    failed += test_vm1_bpl_after_tstb_model(K1801VM1, "K1801VM1");
    failed += test_vm1_bpl_after_tstb_model(K1801VM1G, "K1801VM1G");
    failed += test_vm2_tstb_flags_model(K1801VM2, "K1801VM2");
    failed += test_vm2_tstb_flags_model(K1806VM2, "K1806VM2");
    failed += test_vm2_bpl_after_tstb_model(K1801VM2, "K1801VM2");
    failed += test_vm2_bpl_after_tstb_model(K1806VM2, "K1806VM2");
    failed += test_vm2_trap_stack_model(K1801VM2, "K1801VM2");
    failed += test_vm2_trap_stack_model(K1806VM2, "K1806VM2");
    failed += test_vm2_wait_ignores_trace_model(K1801VM2, "K1801VM2");
    failed += test_vm2_wait_ignores_trace_model(K1806VM2, "K1806VM2");
    failed += test_dcj11_wait_ignores_trace();
    failed += test_vm2_cpc_cpsw_update_model(K1801VM2, "K1801VM2");
    failed += test_vm2_cpc_cpsw_update_model(K1806VM2, "K1806VM2");
    failed += test_dcj11_tstb_flags();
    failed += test_dcj11_bpl_after_tstb();
    failed += test_dcj11_bpl_after_tstb_neg();
    failed += test_dcj11_bpt_stack_order();
    failed += test_dcj11_iot_stack_order();
    failed += test_dcj11_bus_error_stack_order();

    if (failed) {
        fprintf(stderr, "%d test(s) failed\n", failed);
        return 1;
    }

    printf("All core instruction tests passed\n");
    return 0;
}
