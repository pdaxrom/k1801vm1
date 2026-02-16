#ifndef MMU_TEST_COMMON_H
#define MMU_TEST_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/core.h"
#include "core/hardware.h"

#define MMU_TEST_BASE 01000
#define MMU_TEST_STACK 0400

typedef struct {
    regs r;
    byte *mem;
    byte *mem_owner;
    size_t mem_size;
} mmu_fixture;

static const char *mmu_current_test;

static INLINE void mmu_fixture_setup(mmu_fixture *fx)
{
    memset(fx, 0, sizeof(*fx));
    fx->mem_size = hwstub_required_memory_size();
    fx->mem_owner = (byte *)calloc(1, fx->mem_size);
    if (!fx->mem_owner) {
        fprintf(stderr, "FAIL: %s: hwstub memory allocation (%zu bytes)\n",
                mmu_current_test ? mmu_current_test : "mmu_fixture_setup", fx->mem_size);
        abort();
    }
    if (hwstub_set_memory(fx->mem_owner, fx->mem_size) != 0) {
        fprintf(stderr, "FAIL: %s: hwstub_set_memory\n",
                mmu_current_test ? mmu_current_test : "mmu_fixture_setup");
        free(fx->mem_owner);
        abort();
    }
    fx->r.model = DCJ11;
    hwstub_connect(&fx->r);
    core_init(&fx->r);
    fx->mem = fx->r.ramptr(&fx->r, 0);
    memset(fx->mem, 0, fx->mem_size);
    fx->r.SEL0 = 0;
    core_reset(&fx->r);
    fx->r.r[7] = MMU_TEST_BASE;
    fx->r.r[6] = MMU_TEST_STACK;
    fx->r.psw = 0;
}

static INLINE void mmu_fixture_teardown(mmu_fixture *fx)
{
    core_fini(&fx->r);
    hwstub_clear_memory_binding();
    free(fx->mem_owner);
    fx->mem_owner = NULL;
}

static INLINE void mmu_set_test(const char *name)
{
    mmu_current_test = name;
}

#define MMU_ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL: %s: %s\\n", mmu_current_test, (msg)); \
        return 1; \
    } \
} while (0)

#define MMU_ASSERT_EQ(actual, expected, msg) do { \
    word _a = (word)(actual); \
    word _e = (word)(expected); \
    if (_a != _e) { \
        printf("FAIL: %s: %s (got %06o expected %06o)\\n", mmu_current_test, (msg), _a, _e); \
        return 1; \
    } \
} while (0)

static INLINE void mmu_phys_write_word(mmu_fixture *fx, word pa, word value)
{
    fx->mem[pa] = (byte)(value & 0377);
    fx->mem[(word)(pa + 1)] = (byte)((value >> 8) & 0377);
}

static INLINE word mmu_phys_read_word(const mmu_fixture *fx, word pa)
{
    return (word)(fx->mem[pa] | ((word)fx->mem[(word)(pa + 1)] << 8));
}

static INLINE void mmu_phys22_write_word(mmu_fixture *fx, dword pa, word value)
{
    if (fx->r.store_word_pa) {
        fx->r.store_word_pa(&fx->r, pa, value);
        return;
    }
    mmu_phys_write_word(fx, (word)pa, value);
}

static INLINE word mmu_phys22_read_word(const mmu_fixture *fx, dword pa)
{
    if (fx->r.load_word_pa) {
        return fx->r.load_word_pa((regs *)&fx->r, pa);
    }
    return mmu_phys_read_word(fx, (word)pa);
}

static INLINE word mmu_operand(byte mode, byte reg)
{
    return (((word)mode) << 3) | (reg & 07);
}

static INLINE word mmu_op_mov(word src, word dst)
{
    return 0010000 | ((src & 077) << 6) | (dst & 077);
}

static INLINE word mmu_op_halt(void)
{
    return 000000;
}

static INLINE word mmu_op_bpt(void)
{
    return 0000003;
}

#endif
