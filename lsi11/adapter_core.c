#include "adapter_core.h"

#include "bus.h"
#include "irq.h"

/* device modules */
#include "dev_dl11.h"
#include "dev_kw11.h"
#include "dev_lp11.h"
#include "dev_rh11.h"
#include "dev_rk11.h"
#include "dev_rl11.h"
#include "dev_sr.h"
#include "tq11.h"
#include "dev_vm1sav.h"
#include "dev_vm1sel.h"

#include "io_lock.h"
#include "util_term.h"

#include <stdio.h>
#include <string.h>

/* ---- core integration ----
   You must include the correct core header(s) here.
   Typical: ../core/core.h or ../core/pdp11.h etc. */
#include "../core/core.h"  /* TODO: replace with your actual core header */
#include "../core/disas.h" /* optional if you want disassembly on NXM */

/* If your core defines word/byte types, use them; otherwise define here.
   TODO: remove these if core already defines them. */
#ifndef word
typedef uint16_t word;
#endif
#ifndef byte
typedef uint8_t byte;
#endif

#define J11_CPU_ID_1184 001045

static int trace_irq_flag = 0;
static int trace_nxm_flag = 0;
static lsi11_machine_t machine_profile = LSI11_MACHINE_1104;
static uint32_t machine_ram_kb = 56;
static int dl11_alias_on = 1;
static int term_raw_active = 0;

typedef struct {
    int dl11;
    int kw11;
    int lp11;
    int rl11;
    int rk11;
    int rh11;
    int tq11;
    int sr;
    int vm1sel;
    int vm1sav;
} lsi11_device_mask_t;

static lsi11_device_mask_t device_mask = {1, 1, 1, 1, 1, 1, 0, 1, 0, 0};
static uint16_t j11_probe_shadow_regs[10];

static inline int vm2_model(const regs *r)
{
    return (r->model == K1801VM2 || r->model == K1806VM2) ? 1 : 0;
}

static inline int vm1_model(const regs *r)
{
    return (r->model == K1801VM1) ? 1 : 0;
}

static inline int vm2_halt_mode(const regs *r)
{
    return (vm2_model(r) && (r->psw & FLAG_H)) ? 1 : 0;
}

static inline int dcj11_mode_norm(int mode)
{
    mode &= 03;
    return (mode == 02) ? 0 : mode;
}

static inline int dcj11_cur_mode(word psw)
{
    return dcj11_mode_norm((psw >> 14) & 03);
}

static inline int dcj11_regset(word psw)
{
    return (psw >> 11) & 01;
}

static inline word dcj11_set_cur_mode(word psw, int mode)
{
    int m = dcj11_mode_norm(mode);
    return (word)((psw & ~0140000) | ((word)(m & 03) << 14));
}

static inline word dcj11_set_prev_mode(word psw, int mode)
{
    int m = dcj11_mode_norm(mode);
    return (word)((psw & ~0030000) | ((word)(m & 03) << 12));
}

static inline void dcj11_sp_mode_init(regs *r)
{
    int mode;
    if (r->sp_mode_init) {
        return;
    }
    for (mode = 0; mode < 4; mode++) {
        r->sp_mode[mode] = r->r[6];
    }
    r->sp_mode_init = 1;
}

static inline void dcj11_regset_init(regs *r)
{
    int reg;
    if (r->rset_bank_init) {
        return;
    }
    for (reg = 0; reg < 6; reg++) {
        r->rset_bank[0][reg] = r->r[reg];
        r->rset_bank[1][reg] = r->r[reg];
    }
    r->rset_bank_init = 1;
}

static inline void dcj11_apply_psw(regs *r, word new_psw)
{
    word old_psw = r->psw;
    int old_mode = dcj11_cur_mode(old_psw);
    int new_mode = dcj11_cur_mode(new_psw);
    int old_set = dcj11_regset(old_psw);
    int new_set = dcj11_regset(new_psw);
    int reg;

    if (old_mode != new_mode) {
        dcj11_sp_mode_init(r);
        r->sp_mode[old_mode] = r->r[6];
        r->r[6] = r->sp_mode[new_mode];
    }

    if (old_set != new_set) {
        dcj11_regset_init(r);
        for (reg = 0; reg < 6; reg++) {
            r->rset_bank[old_set][reg] = r->r[reg];
            r->r[reg] = r->rset_bank[new_set][reg];
        }
    }

    r->psw = new_psw;
}

static void lsi11_reset_device_mask_for_profile(lsi11_machine_t machine)
{
    (void)machine;
    device_mask.dl11 = 1;
    device_mask.kw11 = 1;
    device_mask.lp11 = 1;
    device_mask.rl11 = 1;
    device_mask.rk11 = 1;
    device_mask.sr = 1;
    device_mask.rh11 = 1;
    device_mask.tq11 = 0;
    /* VM1 extension devices are CPU-specific and must be enabled explicitly. */
    device_mask.vm1sel = 0;
    device_mask.vm1sav = 0;
}

int lsi11_machine_configure(lsi11_machine_t machine, uint32_t ram_kb, char *err,
                            size_t err_len)
{
    int rc;

    if (machine == LSI11_MACHINE_1104) {
        rc = bus_configure(BUS_MACHINE_LSI11_1104, 0, err, err_len);
        if (rc != 0) {
            return rc;
        }
        machine_profile = LSI11_MACHINE_1104;
        machine_ram_kb = bus_ram_kb();
        dl11_alias_on = 1;
        lsi11_reset_device_mask_for_profile(machine_profile);
        return 0;
    }

    if (machine == LSI11_MACHINE_1184) {
        rc = bus_configure(BUS_MACHINE_PDP1184, ram_kb, err, err_len);
        if (rc != 0) {
            return rc;
        }
        machine_profile = LSI11_MACHINE_1184;
        machine_ram_kb = bus_ram_kb();
        dl11_alias_on = 0;
        lsi11_reset_device_mask_for_profile(machine_profile);
        return 0;
    }

    if (err && err_len) {
        snprintf(err, err_len, "Unknown machine profile");
    }
    return -1;
}

void lsi11_set_dl11_alias(int on)
{
    dl11_alias_on = on ? 1 : 0;
}

int lsi11_dl11_alias(void)
{
    return dl11_alias_on;
}

lsi11_machine_t lsi11_machine_current(void)
{
    return machine_profile;
}

uint32_t lsi11_machine_ram_kb(void)
{
    return machine_ram_kb;
}

int lsi11_set_device_enabled(const char *name, int on, char *err,
                             size_t err_len)
{
    int v = on ? 1 : 0;
    if (!name) {
        if (err && err_len) {
            snprintf(err, err_len, "Device name is required");
        }
        return -1;
    }

    if (!strcmp(name, "dl11")) {
        device_mask.dl11 = v;
        return 0;
    }
    if (!strcmp(name, "kw11")) {
        device_mask.kw11 = v;
        return 0;
    }
    if (!strcmp(name, "lp11")) {
        device_mask.lp11 = v;
        return 0;
    }
    if (!strcmp(name, "rl11")) {
        device_mask.rl11 = v;
        return 0;
    }
    if (!strcmp(name, "rk11")) {
        device_mask.rk11 = v;
        return 0;
    }
    if (!strcmp(name, "rh11")) {
        device_mask.rh11 = v;
        return 0;
    }
    if (!strcmp(name, "tq11")) {
        device_mask.tq11 = v;
        return 0;
    }
    if (!strcmp(name, "sr")) {
        device_mask.sr = v;
        return 0;
    }
    if (!strcmp(name, "vm1sel")) {
        device_mask.vm1sel = v;
        return 0;
    }
    if (!strcmp(name, "vm1sav")) {
        device_mask.vm1sav = v;
        return 0;
    }

    if (err && err_len) {
        snprintf(err, err_len, "Unknown device: %s", name);
    }
    return -1;
}

int lsi11_device_enabled(const char *name)
{
    if (!name) {
        return 0;
    }
    if (!strcmp(name, "dl11")) {
        return device_mask.dl11;
    }
    if (!strcmp(name, "kw11")) {
        return device_mask.kw11;
    }
    if (!strcmp(name, "lp11")) {
        return device_mask.lp11;
    }
    if (!strcmp(name, "rl11")) {
        return device_mask.rl11;
    }
    if (!strcmp(name, "rk11")) {
        return device_mask.rk11;
    }
    if (!strcmp(name, "rh11")) {
        return device_mask.rh11;
    }
    if (!strcmp(name, "tq11")) {
        return device_mask.tq11;
    }
    if (!strcmp(name, "sr")) {
        return device_mask.sr;
    }
    if (!strcmp(name, "vm1sel")) {
        return device_mask.vm1sel;
    }
    if (!strcmp(name, "vm1sav")) {
        return device_mask.vm1sav;
    }
    return 0;
}

/* ---------- NXM trap ----------
   This matches your previous approach: push PSW and PC, then vector through
   000004/000006. If your core already provides a bus error trap helper, use
   that instead. */
static inline void nxm_trap(regs *r, paddr_t addr)
{
    word old_psw = r->psw;
    word fault_pc = r->r[7];
    word vector_psw = 0;

    if (r->model == DCJ11) {
        int io_timeout = 0;
        if (addr <= 0177777) {
            io_timeout = (addr >= 0160000) ? 1 : 0;
        } else {
            io_timeout = (addr >= 017760000 && addr <= 017777777) ? 1 : 0;
        }
        if (io_timeout) {
            r->J11_CPUERR |= 0000020; /* CPUE_TMO */
        } else {
            r->J11_CPUERR |= 0000040; /* CPUE_NXM */
        }
        r->J11_CPUERR &= 0000374;
    }

    /* Optional trace */
    if (trace_nxm_flag) {
        word disas_pc = r->instr_pc;
        char buf[128];
        word tmp = disas_pc;
        char *dis_str;
#if defined(ENABLE_MMU) && (ENABLE_MMU)
        /*
         * disas() uses r->load_word (data-space reads). On J11 with split I/D,
         * that can decode garbage for the current instruction. Temporarily force
         * current mode to unified space so disassembly reads I-space words.
         */
        word saved_ssr3 = 0;
        word split_mask = 0;
        if (r->model == DCJ11 && (r->mmu_ssr0 & 0000001)) {
            int cm = (r->psw >> 14) & 03;
            if (cm == 02) {
                cm = 0;
            }
            if (cm == 0) {
                split_mask = 0000004; /* KDS */
            } else if (cm == 1) {
                split_mask = 0000002; /* SDS */
            } else if (cm == 3) {
                split_mask = 0000001; /* UDS */
            }
        }
        saved_ssr3 = r->mmu_ssr3;
        if (split_mask) {
            r->mmu_ssr3 = (word)(r->mmu_ssr3 & ~split_mask);
        }
        dis_str = disas(r, &tmp, buf);
#else
        dis_str = disas(r, &tmp, buf);
#endif
        fprintf(stderr, "NXM at %06o PC=%06o ", (unsigned)addr, disas_pc);
        int i = 0;
        for (word a = disas_pc; a < tmp; a += 2) {
            fprintf(stderr, "%06o ", r->load_word(r, a));
            i++;
        }
        while (i < 3) {
            fprintf(stderr, "       ");
            i++;
        }
        fprintf(stderr, "%s (IR=%06o)\n", dis_str, r->ir);
        fprintf(stderr,
                "R0=%06o R1=%06o R2=%06o R3=%06o R4=%06o R5=%06o SP=%06o PS=%06o\n",
                r->r[0], r->r[1], r->r[2], r->r[3], r->r[4], r->r[5], r->r[6],
                r->psw);
#if defined(ENABLE_MMU) && (ENABLE_MMU)
        if (split_mask) {
            r->mmu_ssr3 = saved_ssr3;
        }
#endif
    }

    if (r->model == DCJ11 && r->dcj11_vector_push_active) {
        word red_old_psw = r->dcj11_vector_old_psw;
        word red_old_pc = r->dcj11_vector_old_pc;
        int red_old_cm = dcj11_cur_mode(red_old_psw);

        /* Red-stack fallback for abort during trap/interrupt stack pushes. */
        r->dcj11_vector_push_active = 0;
        r->dcj11_yellow_pending = 0;
        r->J11_CPUERR = (word)((r->J11_CPUERR | 0000004) & 0000374);

        dcj11_apply_psw(r, red_old_psw);
        r->r[7] = red_old_pc;

        vector_psw = r->load_word(r, 0000006);
        vector_psw = dcj11_set_cur_mode(vector_psw, 0);
        vector_psw = dcj11_set_prev_mode(vector_psw, red_old_cm);
        dcj11_apply_psw(r, vector_psw);

        dcj11_sp_mode_init(r);
        r->sp_mode[0] = 0000004;
        r->r[6] = 0000004;

        r->r[6] -= 0000002;
        r->store_word(r, r->r[6], red_old_psw);
        r->r[6] -= 0000002;
        r->store_word(r, r->r[6], red_old_pc);
        r->r[7] = r->load_word(r, 0000004);
        r->fAbort = 1;
        return;
    }

    vector_psw = r->load_word(r, 0000006);
    if (r->model == DCJ11) {
        int old_cm = dcj11_cur_mode(old_psw);
        vector_psw = dcj11_set_cur_mode(vector_psw, 0);
        vector_psw = dcj11_set_prev_mode(vector_psw, old_cm);
        dcj11_apply_psw(r, vector_psw);
    } else {
        r->psw = vector_psw;
    }

    /*
     * Minimal VM2/HALT stack safety:
     * keep trap frame pushes in available HALT RAM window.
     */
    if (vm2_halt_mode(r)) {
        uint32_t halt_bytes = bus_vm2_halt_ram_bytes();
        word halt_top = (word)(halt_bytes & 0177776);
        if (halt_top >= 0000004) {
            if ((r->r[6] < 0000004) || (r->r[6] > halt_top)) {
                r->r[6] = halt_top;
            } else if (r->r[6] & 1) {
                r->r[6] &= 0177776;
            }
        }
    }

    /* Match core bus-error semantics: switch to vector PSW first, then push OLDPS/OLDPC. */
    r->r[6] -= 0000002;
    r->store_word(r, r->r[6], old_psw);
    r->r[6] -= 0000002;
    r->store_word(r, r->r[6], fault_pc);

    r->r[7] = r->load_word(r, 0000004);

    r->fAbort = 1;
}

static inline int j11_probe_shadow_to_a18(paddr_t addr, paddr_t *a18_out)
{
    if (!a18_out) {
        return 0;
    }

    if (addr >= 017760000 && addr <= 017777777) {
        *a18_out = (addr & 0777777);
        return 1;
    }
    if (addr >= 0760000 && addr <= 0777777) {
        *a18_out = addr;
        return 1;
    }
    if (addr >= 0160000 && addr <= 0177777) {
        *a18_out = (0600000 | (addr & 0177777));
        return 1;
    }
    return 0;
}

static inline int j11_probe_shadow_index18(paddr_t a18_even)
{
    switch (a18_even & 0777776) {
    case 0772000:
        return 0;
    case 0772474:
        return 1;
    case 0776350:
        return 2;
    case 0776450:
        return 3;
    case 0776750:
        return 4;
    case 0777754:
        return 5;
    case 0777756:
        return 6;
    case 0777760:
        return 7;
    case 0777762:
        return 8;
    case 0777764:
        return 9;
    default:
        return -1;
    }
}

static inline int j11_probe_shadow_lookup(paddr_t addr, int *idx, int *shift)
{
    paddr_t a18 = 0;
    int i;

    if (!j11_probe_shadow_to_a18(addr, &a18)) {
        return 0;
    }
    i = j11_probe_shadow_index18(a18);
    if (i < 0) {
        return 0;
    }
    if (idx) {
        *idx = i;
    }
    if (shift) {
        *shift = (a18 & 1) ? 8 : 0;
    }
    return 1;
}

static inline void j11_probe_shadow_reset(void)
{
    memset(j11_probe_shadow_regs, 0, sizeof(j11_probe_shadow_regs));
}

static inline int j11_probe_shadow_hit(regs *r, paddr_t addr)
{
    int idx;

    if (machine_profile != LSI11_MACHINE_1184) {
        return 0;
    }
    if (r->model != DCJ11) {
        return 0;
    }

    return j11_probe_shadow_lookup(addr, &idx, NULL);
}

static inline byte j11_probe_shadow_read_byte(paddr_t addr)
{
    int idx = -1;
    int shift = 0;

    if (j11_probe_shadow_lookup(addr, &idx, &shift)) {
        return (byte)((j11_probe_shadow_regs[idx] >> shift) & 000377);
    }
    return 000000;
}

static inline word j11_probe_shadow_read_word(paddr_t addr)
{
    int idx = -1;

    if (j11_probe_shadow_lookup(addr, &idx, NULL)) {
        return j11_probe_shadow_regs[idx];
    }
    return 000000;
}

static inline void j11_probe_shadow_write_byte(paddr_t addr, byte v)
{
    int idx = -1;
    int shift = 0;
    uint16_t mask;
    uint16_t cur;

    if (!j11_probe_shadow_lookup(addr, &idx, &shift)) {
        return;
    }

    mask = (uint16_t)(000377u << shift);
    cur = j11_probe_shadow_regs[idx];
    cur = (uint16_t)((cur & ~mask) | (((uint16_t)v << shift) & mask));
    j11_probe_shadow_regs[idx] = cur;
}

static inline void j11_probe_shadow_write_word(paddr_t addr, word v)
{
    int idx = -1;

    if (!j11_probe_shadow_lookup(addr, &idx, NULL)) {
        return;
    }
    j11_probe_shadow_regs[idx] = (uint16_t)v;
}

/* ---------- bus callbacks for core ---------- */

#ifdef PICO_ON_DEVICE
static byte __not_in_flash_func(core_load_byte)(regs *r, word addr)
#else
static byte core_load_byte(regs *r, word addr)
#endif
{
    if (j11_probe_shadow_hit(r, (paddr_t)addr)) {
        return j11_probe_shadow_read_byte((paddr_t)addr);
    }
    if (vm2_model(r)) {
        int halt_mode = vm2_halt_mode(r);
        if (bus_vm2_cpu_is_nxm((uint16_t)addr, halt_mode)) {
            nxm_trap(r, (paddr_t)addr);
            return 0;
        }
        return bus_vm2_cpu_read8((uint16_t)addr, halt_mode);
    }
    if (bus_is_nxm((paddr_t)addr)) {
        nxm_trap(r, (paddr_t)addr);
        return 0;
    }
    return bus_read8((paddr_t)addr);
}

#ifdef PICO_ON_DEVICE
static void __not_in_flash_func(core_store_byte)(regs *r, word addr, byte v)
#else
static void core_store_byte(regs *r, word addr, byte v)
#endif
{
    if (j11_probe_shadow_hit(r, (paddr_t)addr)) {
        j11_probe_shadow_write_byte((paddr_t)addr, v);
        return;
    }
    if (vm2_model(r)) {
        int halt_mode = vm2_halt_mode(r);
        if (bus_vm2_cpu_is_nxm((uint16_t)addr, halt_mode)) {
            nxm_trap(r, (paddr_t)addr);
            return;
        }
        bus_vm2_cpu_write8((uint16_t)addr, halt_mode, (uint8_t)v);
        return;
    }
    if (bus_is_nxm((paddr_t)addr)) {
        nxm_trap(r, (paddr_t)addr);
        return;
    }
    bus_write8((paddr_t)addr, v);
}

#ifdef PICO_ON_DEVICE
static word __not_in_flash_func(core_load_word)(regs *r, word addr)
#else
static word core_load_word(regs *r, word addr)
#endif
{
    if (j11_probe_shadow_hit(r, (paddr_t)addr) &&
            j11_probe_shadow_hit(r, (paddr_t)(addr + 1))) {
        return j11_probe_shadow_read_word((paddr_t)addr);
    }
    if (vm2_model(r)) {
        int halt_mode = vm2_halt_mode(r);
        uint16_t a1 = (uint16_t)(addr + 1);
        if (bus_vm2_cpu_is_nxm((uint16_t)addr, halt_mode) ||
                bus_vm2_cpu_is_nxm(a1, halt_mode)) {
            nxm_trap(r, (paddr_t)addr);
            return 0;
        }
        return (word)bus_vm2_cpu_read16((uint16_t)addr, halt_mode);
    }
    if (bus_is_nxm((paddr_t)addr) || bus_is_nxm((paddr_t)(addr + 1))) {
        nxm_trap(r, (paddr_t)addr);
        return 0;
    }
    return (word)bus_read16((paddr_t)addr);
}

#ifdef PICO_ON_DEVICE
static void __not_in_flash_func(core_store_word)(regs *r, word addr, word v)
#else
static void core_store_word(regs *r, word addr, word v)
#endif
{
    if (j11_probe_shadow_hit(r, (paddr_t)addr) &&
            j11_probe_shadow_hit(r, (paddr_t)(addr + 1))) {
        j11_probe_shadow_write_word((paddr_t)addr, v);
        return;
    }
    if (vm2_model(r)) {
        int halt_mode = vm2_halt_mode(r);
        uint16_t a1 = (uint16_t)(addr + 1);
        if (bus_vm2_cpu_is_nxm((uint16_t)addr, halt_mode) ||
                bus_vm2_cpu_is_nxm(a1, halt_mode)) {
            nxm_trap(r, (paddr_t)addr);
            return;
        }
        bus_vm2_cpu_write16((uint16_t)addr, halt_mode, (uint16_t)v);
        return;
    }
    if (bus_is_nxm((paddr_t)addr) || bus_is_nxm((paddr_t)(addr + 1))) {
        nxm_trap(r, (paddr_t)addr);
        return;
    }
    bus_write16((paddr_t)addr, (uint16_t)v);
}

#ifdef PICO_ON_DEVICE
static byte __not_in_flash_func(core_load_byte_pa)(regs *r, dword addr)
#else
static byte core_load_byte_pa(regs *r, dword addr)
#endif
{
    if (j11_probe_shadow_hit(r, (paddr_t)addr)) {
        return j11_probe_shadow_read_byte((paddr_t)addr);
    }
    if (bus_is_nxm((paddr_t)addr)) {
        nxm_trap(r, (paddr_t)addr);
        return 0;
    }
    return bus_read8((paddr_t)addr);
}

#ifdef PICO_ON_DEVICE
static void __not_in_flash_func(core_store_byte_pa)(regs *r, dword addr, byte v)
#else
static void core_store_byte_pa(regs *r, dword addr, byte v)
#endif
{
    if (j11_probe_shadow_hit(r, (paddr_t)addr)) {
        j11_probe_shadow_write_byte((paddr_t)addr, v);
        return;
    }
    if (bus_is_nxm((paddr_t)addr)) {
        nxm_trap(r, (paddr_t)addr);
        return;
    }
    bus_write8((paddr_t)addr, v);
}

#ifdef PICO_ON_DEVICE
static word __not_in_flash_func(core_load_word_pa)(regs *r, dword addr)
#else
static word core_load_word_pa(regs *r, dword addr)
#endif
{
    if (j11_probe_shadow_hit(r, (paddr_t)addr) &&
            j11_probe_shadow_hit(r, (paddr_t)(addr + 1))) {
        return j11_probe_shadow_read_word((paddr_t)addr);
    }
    if (bus_is_nxm((paddr_t)addr) || bus_is_nxm((paddr_t)(addr + 1))) {
        nxm_trap(r, (paddr_t)addr);
        return 0;
    }
    return (word)bus_read16((paddr_t)addr);
}

#ifdef PICO_ON_DEVICE
static void __not_in_flash_func(core_store_word_pa)(regs *r, dword addr, word v)
#else
static void core_store_word_pa(regs *r, dword addr, word v)
#endif
{
    if (j11_probe_shadow_hit(r, (paddr_t)addr) &&
            j11_probe_shadow_hit(r, (paddr_t)(addr + 1))) {
        j11_probe_shadow_write_word((paddr_t)addr, v);
        return;
    }
    if (bus_is_nxm((paddr_t)addr) || bus_is_nxm((paddr_t)(addr + 1))) {
        nxm_trap(r, (paddr_t)addr);
        return;
    }
    bus_write16((paddr_t)addr, (uint16_t)v);
}

/* core expects init/reset/fini */
static int impl_init(regs *r)
{
    int need_vm1_ext = vm1_model(r);

    /* init bus RAM; devices register their I/O in *_init() */
    bus_init();
    j11_probe_shadow_reset();
    dl11_set_alias(dl11_alias_on);

    /* Enforce VM1-only extension peripheral visibility by CPU model. */
    if (need_vm1_ext) {
        device_mask.vm1sel = 1;
        device_mask.vm1sav = 1;
    } else {
        device_mask.vm1sel = 0;
        device_mask.vm1sav = 0;
    }

    /* init host terminal */
    term_raw_active = 0;
    if (device_mask.dl11) {
        util_term_init_raw();
        term_raw_active = 1;
    }

    /* init device frameworks (devio/irq are implicit singletons) */
    if (device_mask.dl11 && dl11_init() != 0) {
        return -1;
    }
    if (device_mask.kw11 && kw11_init() != 0) {
        return -1;
    }
    if (device_mask.rl11 && rl11_init() != 0) {
        return -1;
    }
    if (device_mask.rk11 && rk11_init() != 0) {
        return -1;
    }
    if (device_mask.rh11 && rh11_init() != 0) {
        return -1;
    }
    if (device_mask.tq11 && tq11_init() != 0) {
        return -1;
    }
    if (device_mask.lp11 && lp11_init() != 0) {
        return -1;
    }
    if (device_mask.sr && sr_init() != 0) {
        return -1;
    }
    if (device_mask.vm1sel && vm1sel_init() != 0) {
        return -1;
    }
    if (device_mask.vm1sav && vm1sav_init() != 0) {
        return -1;
    }

    return 0;
}

static void impl_reset(regs *r)
{
    (void)r;
    j11_probe_shadow_reset();
    if (r->model == DCJ11) {
        if (machine_profile == LSI11_MACHINE_1184) {
            /* Match PDP-11/84 identification path expected by RT-11 monitor. */
            r->J11_MAINT = J11_CPU_ID_1184;
            /* 11/8x systems expose non-zero Hit/Miss register value. */
            r->J11_HITMISS = 000010;
        } else {
            r->J11_MAINT = 0;
            r->J11_HITMISS = 0;
        }
    }
    if (device_mask.dl11) {
        dl11_reset();
    }
    if (device_mask.kw11) {
        kw11_reset();
    }
    if (device_mask.rl11) {
        rl11_reset();
    }
    if (device_mask.rk11) {
        rk11_reset();
    }
    if (device_mask.rh11) {
        rh11_reset();
    }
    if (device_mask.tq11) {
        tq11_reset();
    }
    if (device_mask.lp11) {
        lp11_reset();
    }
    if (device_mask.sr) {
        sr_reset();
    }
    if (device_mask.vm1sel) {
        vm1sel_reset();
    }
    if (device_mask.vm1sav) {
        vm1sav_reset();
    }
}

static void impl_fini(regs *r)
{
    (void)r;
    if (term_raw_active) {
        util_term_restore();
        term_raw_active = 0;
    }
}

/* IRQ poll callback: delegates to irq_poll() */
#ifdef PICO_ON_DEVICE
int __not_in_flash_func(core_poll_irq)(regs *r, word *vec)
#else
static int core_poll_irq(regs *r, word *vec)
#endif
{
    uint16_t v = 0;
    uint32_t irqstate = io_lock_acquire();
    if (!irq_poll(r, &v)) {
        io_lock_release(irqstate);
        return 0;
    }
    io_lock_release(irqstate);

    *vec = (word)v;

    if (trace_irq_flag) {
        word vec_addr = (word)(*vec & 0000777);
        word handler = bus_read16(vec_addr);
        word psw = bus_read16((word)(vec_addr + 2));
        fprintf(stderr, "IRQ vec=%06o at %06o -> %06o PS=%06o\n", *vec, r->r[7],
                handler, psw);
    }

    return 1;
}

/* RAM pointer for core: core may use this for fast access.
   Only RAM 000000..0157777 is “real”, but backing array is 64K. */
static uint8_t *core_ramptr(regs *r, word offset)
{
    (void)r;
    return bus_ram_ptr((paddr_t)offset);
}

void lsi11_hw_connect(regs *r)
{
    r->load_byte = core_load_byte;
    r->store_byte = core_store_byte;
    r->load_word = core_load_word;
    r->store_word = core_store_word;
    r->load_byte_pa = core_load_byte_pa;
    r->store_byte_pa = core_store_byte_pa;
    r->load_word_pa = core_load_word_pa;
    r->store_word_pa = core_store_word_pa;

    r->init = impl_init;
    r->reset = impl_reset;
    r->fini = impl_fini;

    r->poll_irq = core_poll_irq;
    r->ramptr = core_ramptr;
}

void lsi11_poll_devices(void)
{
    /* Fast devices: poll under spinlock (no SD I/O) */
    uint32_t irqstate = io_lock_acquire();
    if (device_mask.dl11) {
        dl11_poll();
    }
    if (device_mask.kw11) {
        kw11_poll();
    }
    if (device_mask.lp11) {
        lp11_poll();
    }
    io_lock_release(irqstate);

    /* Disk controllers: poll outside spinlock.
       These execute disk commands that perform slow SD card SPI I/O.
       Holding the spinlock during SD I/O would block Core 1 CPU from
       accessing any device register, causing a hang. */
    if (device_mask.rl11) {
        rl11_poll();
    }
    if (device_mask.rk11) {
        rk11_poll();
    }
    if (device_mask.rh11) {
        rh11_poll();
    }
    if (device_mask.tq11) {
        tq11_poll();
    }
}

void lsi11_set_trace_irq(int on)
{
    trace_irq_flag = on ? 1 : 0;
}
void lsi11_set_trace_nxm(int on)
{
    trace_nxm_flag = on ? 1 : 0;
}
