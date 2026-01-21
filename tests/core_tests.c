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

static void fixture_setup(cpu_fixture *fx)
{
    memset(fx, 0, sizeof(*fx));
    fx->r.model = K1801VM1;
    hwstub_connect(&fx->r);
    core_init(&fx->r);
    fx->mem = fx->r.ramptr(&fx->r, 0);
    memset(fx->mem, 0, 1 << 16);
    fx->r.SEL1 = 0;
    core_reset(&fx->r);
    fx->r.r[7] = TEST_BASE;
    fx->r.r[6] = TEST_STACK;
    fx->r.psw = 0;
}

static void fixture_teardown(cpu_fixture *fx)
{
    core_fini(&fx->r);
}

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

static INLINE word op_add(word src, word dst)
{
    return 0060000 | ((src & 077) << 6) | (dst & 077);
}

static INLINE word op_sub(word src, word dst)
{
    return 0160000 | ((src & 077) << 6) | (dst & 077);
}

static INLINE word op_jsr(byte reg, word dst)
{
    return 0004000 | ((reg & 07) << 6) | (dst & 077);
}

static INLINE word op_beq(byte offset)
{
    return 0001400 | (offset & 0377);
}

static INLINE word op_clr(word dst)
{
    return 0005000 | (dst & 077);
}

static INLINE word op_inc(word dst)
{
    return 0005200 | (dst & 077);
}

static INLINE word op_nop(void)
{
    return 000240;
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

static INLINE word op_bpt(void)
{
    return 0000003;
}

static INLINE word op_iot(void)
{
    return 0000004;
}

static INLINE word op_rtt(void)
{
    return 0000006;
}

static INLINE word op_rti(void)
{
    return 0000002;
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
    const word new_psw = 000340;
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
    const word new_psw = 000340;
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
    ASSERT_EQ(fx.r.r[7], handler + 2, "PC should advance after handler");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should load trace vector");
    ASSERT_EQ(fx.r.load_word(&fx.r, handler + 2), op_nop(), "Handler+2 NOP after trace");
    ASSERT_EQ(fx.r.r[6], 00774, "SP should push two words");
    ASSERT_EQ(fx.r.load_word(&fx.r, 00774), TEST_BASE, "Stack PC incorrect");
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
    ASSERT_EQ(fx.r.r[7], handler + 2, "Trace should vector on following step");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should load trace vector");
    ASSERT_EQ(fx.r.load_word(&fx.r, 01200), TEST_BASE + 4, "Trace stack PC incorrect");
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
    ASSERT_EQ(fx.r.r[7], handler + 2, "PC should advance after handler");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should load trace vector");
    ASSERT_EQ(fx.r.fTrap, 0, "fTrap should remain clear");

    fx.r.psw |= FLAG_T;
    fx.r.r[7] = TEST_BASE;
    ASSERT_EQ(core_step(&fx.r), 0, "Trace should rearm on next step");
    ASSERT_EQ(fx.r.r[7], handler + 2, "PC should return to handler after trace");
    ASSERT_EQ(fx.r.load_word(&fx.r, 01170), TEST_BASE, "Second trace stack PC incorrect");
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
    ASSERT_EQ(fx.r.r[7], handler + 2, "PC should advance after handler");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should load trace vector");

    fx.r.r[7] = TEST_BASE;
    ASSERT_EQ(core_step(&fx.r), 0, "Trace should rearm with T in vector");
    ASSERT_EQ(fx.r.r[7], handler + 2, "PC should return to handler after trace");
    ASSERT_EQ(fx.r.load_word(&fx.r, handler + 2), op_nop(), "Handler+2 NOP after trace");
    ASSERT_EQ(fx.r.load_word(&fx.r, 01170), TEST_BASE, "Second trace stack PC incorrect");
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
    ASSERT_EQ(fx.r.r[7], handler + 2, "PC should advance after handler");
    ASSERT_EQ(fx.r.psw, new_psw, "PSW should load trace vector");

    fx.r.r[7] = TEST_BASE;
    ASSERT_EQ(core_step(&fx.r), 0, "Program NOP should execute");
    ASSERT_EQ(fx.r.r[7], TEST_BASE + 2, "PC should advance without trace");
    ASSERT_EQ(fx.r.load_word(&fx.r, 01174), TEST_BASE, "Trace stack PC incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, 01176), FLAG_T | 000003, "Trace stack PSW incorrect");
    ASSERT_EQ(fx.r.load_word(&fx.r, handler + 2), op_nop(), "Handler+2 NOP after trace");

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
    failed += test_bpt_vectors();
    failed += test_iot_vectors();
    failed += test_trace_vector();
    failed += test_rtt_restores_and_sets_ftrap();
    failed += test_rtt_skips_trace_once();
    failed += test_trace_rearms_after_handler();
    failed += test_trace_rearms_with_t_in_vector();
    failed += test_trace_stops_when_t_cleared();
    failed += test_rti_restores_state();

    if (failed) {
        fprintf(stderr, "%d test(s) failed\n", failed);
        return 1;
    }

    printf("All core instruction tests passed\n");
    return 0;
}
