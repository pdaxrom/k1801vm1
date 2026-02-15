#include "adapter_core.h"

#include "bus.h"
#include "irq.h"

/* device modules */
#include "dev_dl11.h"
#include "dev_kw11.h"
#include "dev_lp11.h"
#include "dev_rl11.h"
#include "dev_rh11.h"
#include "dev_rk11.h"
#include "dev_sr.h"
#include "dev_vm1sel.h"
#include "dev_vm1sav.h"

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
    int sr;
    int vm1sel;
    int vm1sav;
} lsi11_device_mask_t;

static lsi11_device_mask_t device_mask = {1, 1, 1, 1, 1, 1, 1, 0, 0};

static int vm2_model(const regs *r)
{
    return (r->model == K1801VM2 || r->model == K1806VM2) ? 1 : 0;
}

static int vm2_halt_mode(const regs *r)
{
    return (vm2_model(r) && (r->psw & FLAG_H)) ? 1 : 0;
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
    device_mask.vm1sel = 1;
    device_mask.vm1sav = 1;
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
static void nxm_trap(regs *r, paddr_t addr)
{
    (void)addr;
    if (r->model == DCJ11) {
        int io_timeout = 0;
        if (addr <= 0177777) {
            io_timeout = (addr >= 0160000) ? 1 : 0;
        } else {
            io_timeout = (addr >= 017760000 && addr <= 017777777) ? 1 : 0;
        }
        if (io_timeout) {
            r->J11_REG177752_177766[6] |= 0000020; /* CPUE_TMO */
        } else {
            r->J11_REG177752_177766[6] |= 0000040; /* CPUE_NXM */
        }
        r->J11_REG177752_177766[6] &= 0000374;
    }

    /* Optional trace */
    if (trace_nxm_flag) {
        word pc = r->r[7];
        word disas_pc = (pc >= 0000002) ? (word)(pc - 0000002) : pc;
        char buf[128];
        word tmp = disas_pc;
        disas(r, &tmp, buf);
        fprintf(stderr, "NXM at %06o PC=%06o IR=%06o %s\n", (unsigned)addr,
                disas_pc, r->ir,
                buf);
        fprintf(stderr,
                "R0=%06o R1=%06o R2=%06o R3=%06o R4=%06o R5=%06o SP=%06o PS=%06o\n",
                r->r[0], r->r[1], r->r[2], r->r[3], r->r[4], r->r[5], r->r[6],
                r->psw);
    }

    /* Match core bus-error semantics: push current PC. */
    word fault_pc = r->r[7];
    /* push PSW then PC */
    r->r[6] -= 0000002;
    r->store_word(r, r->r[6], r->psw);
    r->r[6] -= 0000002;
    r->store_word(r, r->r[6], fault_pc);

    r->r[7] = r->load_word(r, 0000004);
    r->psw = r->load_word(r, 0000006);

    r->fAbort = 1;
}

static int j11_probe_shadow_hit(regs *r, paddr_t addr)
{
    if (machine_profile != LSI11_MACHINE_1184) {
        return 0;
    }
    if (r->model != DCJ11) {
        return 0;
    }
    /* Minimal probe aliases for 11/84-like RT-11 detection paths. */
    if (addr >= 017772000 && addr <= 017772001) {
        return 1;
    }
    return 0;
}

static byte j11_probe_shadow_read_byte(paddr_t addr)
{
    if (addr == 017772000) {
        return 000001;
    }
    return 000000;
}

static word j11_probe_shadow_read_word(paddr_t addr)
{
    if (addr == 017772000) {
        return 000001;
    }
    return 000000;
}

/* ---------- bus callbacks for core ---------- */

static byte core_load_byte(regs *r, word addr)
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

static void core_store_byte(regs *r, word addr, byte v)
{
    if (j11_probe_shadow_hit(r, (paddr_t)addr)) {
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

static word core_load_word(regs *r, word addr)
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

static void core_store_word(regs *r, word addr, word v)
{
    if (j11_probe_shadow_hit(r, (paddr_t)addr) &&
            j11_probe_shadow_hit(r, (paddr_t)(addr + 1))) {
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

static byte core_load_byte_pa(regs *r, dword addr)
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

static void core_store_byte_pa(regs *r, dword addr, byte v)
{
    if (j11_probe_shadow_hit(r, (paddr_t)addr)) {
        return;
    }
    if (bus_is_nxm((paddr_t)addr)) {
        nxm_trap(r, (paddr_t)addr);
        return;
    }
    bus_write8((paddr_t)addr, v);
}

static word core_load_word_pa(regs *r, dword addr)
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

static void core_store_word_pa(regs *r, dword addr, word v)
{
    if (j11_probe_shadow_hit(r, (paddr_t)addr) &&
            j11_probe_shadow_hit(r, (paddr_t)(addr + 1))) {
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
    (void)r;
    /* init bus RAM; devices register their I/O in *_init() */
    bus_init();
    dl11_set_alias(dl11_alias_on);

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
    if (r->model == DCJ11) {
        if (machine_profile == LSI11_MACHINE_1184) {
            /* Match PDP-11/84 identification path expected by RT-11 monitor. */
            r->J11_REG177750 = J11_CPU_ID_1184;
            /* 11/8x systems expose non-zero Hit/Miss register value. */
            r->J11_REG177752_177766[0] = 000010;
        } else {
            r->J11_REG177750 = 0;
            r->J11_REG177752_177766[0] = 0;
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
static int core_poll_irq(regs *r, word *vec)
{
    uint16_t v = 0;
    if (!irq_poll(r, &v)) {
        return 0;
    }

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
    if (device_mask.dl11) {
        dl11_poll();
    }
    if (device_mask.kw11) {
        kw11_poll();
    }
    if (device_mask.rl11) {
        rl11_poll();
    }
    if (device_mask.rk11) {
        rk11_poll();
    }
    if (device_mask.rh11) {
        rh11_poll();
    }
    if (device_mask.lp11) {
        lp11_poll();
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
