#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "hardware/vreg.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "f_util.h"
#include "ff.h"
#include "hw_config.h"
#include "sd_card.h"

#include "adapter_core.h"
#include "bus.h"
#include "dev_dl11.h"
#include "dev_dz11.h"
#include "dev_kw11.h"
#include "dev_rh11.h"
#include "dev_rk11.h"
#include "dev_rq11.h"
#include "dev_rl11.h"
#include "dev_sr.h"
#include "dev_tq11.h"
#include "dev_xp.h"
#include "emu_file.h"
#include "options.h"
#include "ubmap.h"

#include "core.h"
#include "disas.h"

#include "display_backend.h"

#if defined(LSI11_TARGET_1184)
#define LSI11_TARGET_PDP1184 1
#endif

#ifndef PICO_RP2350
#define PICO_RP2350 0
#endif

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define MAX_IMAGES 64
#define MAX_IMAGE_NAME 128
#define MAX_IMAGE_PATH 256
#define DEFAULT_OPTIONS_FILE "0:/default.conf"
#define RT11_BOOT_BLOCK_BYTES 01000u

#define VM2_MIN_HALT_RAM_BYTES 0004000u
#define VM2_BUSERR_VECTOR 0000004u
#define VM2_HALT_VECTOR 0000170u
#define VM2_HANDLER_ADDR 0000400u
#define VM2_VECTOR_PSW 0000400u

#define MMR3_BME 0000040u
#define MMR3_M22E 0000020u
#define MMR0_RELO_ENABLE 0000001u

#define RK_BOOT_ADDR 002000u
#define RK_BOOT_ENTRY (RK_BOOT_ADDR + 000002u)
#define RL_BOOT_ADDR 002000u
#define RL_BOOT_ENTRY RL_BOOT_ADDR
#define RH_BOOT_ADDR 002000u
#define RH_BOOT_ENTRY (RH_BOOT_ADDR + 000002u)
#define RQ_BOOT_ADDR 016000u
#define RQ_BOOT_ENTRY (RQ_BOOT_ADDR + 000002u)
#define RQ_BOOT_UNIT (RQ_BOOT_ADDR + 000010u)
#define RQ_BOOT_CSR (RQ_BOOT_ADDR + 000014u)
#define TQ_BOOT_ADDR 016000u
#define TQ_BOOT_ENTRY (TQ_BOOT_ADDR + 000002u)
#define TQ_BOOT_UNIT (TQ_BOOT_ADDR + 000010u)
#define TQ_BOOT_CSR (TQ_BOOT_ADDR + 000014u)
#define TQ_B_CMDINT (TQ_BOOT_ADDR - 001000u)
#define TQ_B_RSPINT (TQ_B_CMDINT + 000002u)
#define TQ_B_RING (TQ_B_RSPINT + 000002u)
#define TQ_B_RSPH (TQ_B_RING + 000010u)
#define TQ_B_TKRSP (TQ_B_RSPH + 000004u)
#define TQ_B_CMDH (TQ_B_TKRSP + 000060u)
#define TQ_B_TKCMD (TQ_B_CMDH + 000004u)

#define VM2_TX_IMM(ch) 0112711u, (uint16_t)(uint8_t)(ch)

/* I/O spinlock shared with devio.c and adapter_core.c via io_lock.h */
spin_lock_t *g_io_spinlock = NULL;
#define IO_SPINLOCK_NUM 0

typedef struct {
    char name[MAX_IMAGE_NAME];
    char path[MAX_IMAGE_PATH];
} image_entry_t;

typedef struct {
    uint16_t mhz;
    enum vreg_voltage vreg;
    const char *vreg_name;
} freq_profile_t;

typedef enum {
    IMAGE_FILTER_ANY = 0,
    IMAGE_FILTER_DISK = 1,
    IMAGE_FILTER_TAPE = 2,
} image_filter_t;

typedef struct {
    int trace;
    int trace_regs;
    long trace_after;
    long max_steps;
    long trace_limit;
    int exit_on_abort;
    volatile int halted;
} pico_run_config_t;

typedef int (*boot_copy_unit_fn)(unsigned unit, void *dest, size_t len);

static int preload_rt11_boot_block_bytes(const uint8_t *buf, size_t len);
static int preload_rt11_boot_block_attached(boot_copy_unit_fn copy_fn, unsigned unit,
                                            const char *fallback_path);

static pico_run_config_t g_run_config = {0};
static lsi11_machine_t g_machine_kind = LSI11_MACHINE_1104;

extern char __StackLimit;
extern void *_sbrk(int incr);

static uint32_t pico_pdp1184_ram_limit_kb(void)
{
#if PICO_RP2350
    return 384u;
#else
    return 128u;
#endif
}

static void print_startup_banner(void)
{
    const uintptr_t heap_end = (uintptr_t)_sbrk(0);
    const uintptr_t heap_limit = (uintptr_t)&__StackLimit;
    const uintptr_t free_ram_bytes = (heap_end < heap_limit) ? (heap_limit - heap_end) : 0u;

    printf("\nPico LSI11 emulator\n");
    printf("Free RAM: %lu KB\n", (unsigned long)(free_ram_bytes / 1024u));
}

static const freq_profile_t freq_profiles[] = {
    {125, VREG_VOLTAGE_1_10, "1.10V"},
    {133, VREG_VOLTAGE_1_10, "1.10V"},
    {150, VREG_VOLTAGE_1_10, "1.10V"},
    {166, VREG_VOLTAGE_1_10, "1.10V"},
    {180, VREG_VOLTAGE_1_15, "1.15V"},
    {200, VREG_VOLTAGE_1_15, "1.15V"},
    {225, VREG_VOLTAGE_1_20, "1.20V"},
    {250, VREG_VOLTAGE_1_25, "1.25V"},
    {266, VREG_VOLTAGE_1_25, "1.25V"},
    {280, VREG_VOLTAGE_1_30, "1.30V"},
    {300, VREG_VOLTAGE_1_30, "1.30V"},
};

static const freq_profile_t *find_freq_profile(long mhz)
{
    for (size_t i = 0; i < ARRAY_SIZE(freq_profiles); i++) {
        if ((long)freq_profiles[i].mhz == mhz) {
            return &freq_profiles[i];
        }
    }

    return NULL;
}

static int apply_freq_profile(const freq_profile_t *p)
{
    bool ok;
    uint32_t actual_khz;

    if (!p) {
        return -1;
    }

    vreg_set_voltage(p->vreg);
    sleep_ms(2);
    ok = set_sys_clock_khz((uint32_t)p->mhz * 1000u, true);
    actual_khz = clock_get_hz(clk_sys) / 1000u;

    if (ok) {
        printf("Clock set: %lu MHz (VREG %s)\n",
               (unsigned long)(actual_khz / 1000u), p->vreg_name);
        return 0;
    }

    printf("Failed to set %u MHz, current: %lu MHz\n", p->mhz,
           (unsigned long)(actual_khz / 1000u));
    return -1;
}

static int apply_configured_clock(const lsi11_options_t *opts)
{
    const freq_profile_t *p;

    if (!opts || opts->sys_clock_mhz <= 0) {
        return 0;
    }

    p = find_freq_profile(opts->sys_clock_mhz);
    if (!p) {
        printf("Unsupported RP2040 frequency: %ld MHz\n", opts->sys_clock_mhz);
        printf("Supported values:");
        for (size_t i = 0; i < ARRAY_SIZE(freq_profiles); i++) {
            printf(" %u", freq_profiles[i].mhz);
        }
        printf(" MHz\n");
        return -1;
    }

    return apply_freq_profile(p);
}

/*
 * VM2 HALT-bank runtime stub:
 *   MOV  #177566, R1
 *   MOVB #'<char>', (R1)
 *   ...
 *   BR   .
 */
static const uint16_t vm2_halt_stub_prog[] = {
    0012701u, 0177566u,

    VM2_TX_IMM('H'), VM2_TX_IMM('A'), VM2_TX_IMM('L'), VM2_TX_IMM('T'),
    VM2_TX_IMM(' '), VM2_TX_IMM('m'), VM2_TX_IMM('o'), VM2_TX_IMM('d'),
    VM2_TX_IMM('e'), VM2_TX_IMM(' '), VM2_TX_IMM('-'), VM2_TX_IMM(' '),
    VM2_TX_IMM('v'), VM2_TX_IMM('e'), VM2_TX_IMM('c'), VM2_TX_IMM('t'),
    VM2_TX_IMM('o'), VM2_TX_IMM('r'), VM2_TX_IMM(' '), VM2_TX_IMM('0'),
    VM2_TX_IMM('0'), VM2_TX_IMM('0'), VM2_TX_IMM('0'), VM2_TX_IMM('0'),
    VM2_TX_IMM('4'), VM2_TX_IMM('\r'), VM2_TX_IMM('\n'),

    0000777u
};

static const uint16_t rk_bootstrap[] = {
    0042113u,
    0012706u, RK_BOOT_ADDR,
    0012700u, 0000000u,
    0010003u,
    0000303u,
    0006303u,
    0006303u,
    0006303u,
    0006303u,
    0006303u,
    0012701u, 0177412u,
    0010311u,
    0005041u,
    0012741u, 0177000u,
    0012741u, 0000005u,
    0005002u,
    0005003u,
    0012704u, RK_BOOT_ADDR + 000020u,
    0005005u,
    0105711u,
    0100376u,
    0105011u,
    0005007u
};

static const uint16_t rl_bootstrap[] = {
    0012701u, 0174400u,
    0012761u, 0000013u, 0000004u,
    0012711u, 0000004u,
    0105711u,
    0100376u,
    0005061u, 0000002u,
    0005061u, 0000004u,
    0012761u, 0177400u, 0000006u,
    0012711u, 0000014u,
    0105711u,
    0100376u,
    0005007u
};

static const uint16_t rh_bootstrap[] = {
    0042115u,
    0012706u, RH_BOOT_ADDR,
    0012700u, 0000000u,
    0012701u, 0177440u,
    0012761u, 0000040u, 0000010u,
    0010061u, 0000010u,
    0016102u, 0000012u,
    0100375u,
    0042702u, 0177377u,
    0006302u,
    0006302u,
    0012703u, 0000003u,
    0050203u,
    0010311u,
    0105711u,
    0100376u,
    0012761u, 0177000u, 0000002u,
    0005061u, 0000004u,
    0005061u, 0000006u,
    0005061u, 0000020u,
    0012703u, 0000021u,
    0050203u,
    0010311u,
    0105711u,
    0100376u,
    0005002u,
    0005003u,
    0012704u, RH_BOOT_ADDR + 000020u,
    0005005u,
    0005007u
};

static const uint16_t rq_bootstrap[] = {
    0042125u,
    0012706u, 0016000u,
    0012700u, 0000000u,
    0012701u, 0172150u,
    0012704u, 0016162u,
    0012705u, 0004000u,
    0010102u,
    0005022u,
    0005712u,
    0100001u,
    0000000u,
    0030512u,
    0001773u,
    0012412u,
    0006305u,
    0100370u,
    0105714u,
    0001434u,
    0012702u, 0007000u,
    0005022u,
    0020227u, 0007204u,
    0103774u,
    0112437u, 0007100u,
    0110037u, 0007110u,
    0112437u, 0007114u,
    0112437u, 0007121u,
    0012722u, 0007004u,
    0010522u,
    0012722u, 0007104u,
    0010512u,
    0024242u,
    0005711u,
    0005712u,
    0100776u,
    0005737u, 0007016u,
    0001743u,
    0000000u,
    0005011u,
    0005003u,
    0012704u, RQ_BOOT_ADDR + 000020u,
    0005005u,
    0005007u,
    0100000u,
    0007204u,
    0000000u,
    0000001u,
    0004420u,
    0020000u,
    0001041u,
    0000000u
};

static const uint16_t tq_bootstrap[] = {
    0046525u,

    0012706u, TQ_BOOT_ADDR,
    0012700u, 0000000u,
    0012701u, 0174500u,
    0005021u,
    0012704u, 0004000u,
    0005002u,
    0005022u,
    0020237u, TQ_BOOT_ADDR - 000002u,
    0103774u,
    0012705u, TQ_BOOT_ADDR + 000312u,

    0005711u,
    0100001u,
    0000000u,
    0030411u,
    0001773u,
    0012511u,
    0006304u,
    0100370u,

    0012737u, 0000400u, TQ_B_CMDH + 000002u,
    0012737u, 0000044u, TQ_B_CMDH,
    0010037u, TQ_B_TKCMD + 000004u,
    0012737u, 0000011u, TQ_B_TKCMD + 000010u,
    0012737u, 0020000u, TQ_B_TKCMD + 000012u,
    0012702u, TQ_B_RING,
    0012722u, TQ_B_TKRSP,
    0010203u,
    0010423u,
    0012723u, TQ_B_TKCMD,
    0010423u,
    0005741u,
    0005712u,
    0100776u,
    0105737u, TQ_B_TKRSP + 000012u,
    0001401u,
    0000000u,
    0012703u, TQ_B_TKCMD + 000010u,
    0012723u, 0000045u,
    0012723u, 0020002u,
    0012723u, 0000001u,
    0005023u,
    0005023u,
    0005023u,
    0010412u,
    0010437u, TQ_B_RING + 000006u,
    0005711u,
    0005712u,
    0100776u,
    0105737u, TQ_B_TKRSP + 000012u,
    0001401u,
    0000000u,
    0012703u, TQ_B_TKCMD + 000010u,
    0012723u, 0000041u,
    0012723u, 0020000u,
    0012723u, 0001000u,
    0005023u,
    0005023u,
    0010412u,
    0010437u, TQ_B_RING + 000006u,
    0005711u,
    0005712u,
    0100776u,
    0105737u, TQ_B_TKRSP + 000012u,
    0001401u,
    0000000u,

    0005003u,
    0012704u, TQ_BOOT_ADDR + 000020u,
    0005005u,
    0005007u,

    0100000u,
    TQ_B_RING,
    0000000u,
    0000001u
};

static void fatal_halt(const char *msg)
{
    printf("\nError: %s\n", msg);
    for (;;) {
        display_backend_task();
        sleep_ms(1000);
    }
}

static void cleanup_media(void)
{
    tq11_close_image();
    rl11_close_image();
    xp_close_image();
    rq11_close_image();
    rh11_close_image();
    rk11_close_image();
}

static int cpu_is_vm2(byte model)
{
    return (model == K1801VM2 || model == K1806VM2) ? 1 : 0;
}

static int vm2_halt_store_word(uint16_t addr, uint16_t v, char *err, size_t err_len)
{
    if (bus_vm2_cpu_is_nxm(addr, 1) ||
            bus_vm2_cpu_is_nxm((uint16_t)(addr + 1u), 1)) {
        if (err && err_len) {
            snprintf(err, err_len, "VM2 HALT RAM NXM at %06o", (unsigned)addr);
        }
        return -1;
    }
    bus_vm2_cpu_write16(addr, 1, v);
    return 0;
}

static int vm2_install_halt_stub(char *err, size_t err_len)
{
    uint16_t pc = VM2_HANDLER_ADDR;
    uint16_t stop_pc = VM2_HANDLER_ADDR +
                       (uint16_t)((ARRAY_SIZE(vm2_halt_stub_prog) - 1u) * 2u);

    if (vm2_halt_store_word(VM2_BUSERR_VECTOR, VM2_HANDLER_ADDR, err, err_len) != 0) {
        return -1;
    }
    if (vm2_halt_store_word((uint16_t)(VM2_BUSERR_VECTOR + 2u), VM2_VECTOR_PSW,
                            err, err_len) != 0) {
        return -1;
    }

    for (size_t i = 0; i < ARRAY_SIZE(vm2_halt_stub_prog); i++) {
        if (vm2_halt_store_word(pc, vm2_halt_stub_prog[i], err, err_len) != 0) {
            return -1;
        }
        pc = (uint16_t)(pc + 2u);
    }

    if (vm2_halt_store_word(VM2_HALT_VECTOR, stop_pc, err, err_len) != 0) {
        return -1;
    }
    if (vm2_halt_store_word((uint16_t)(VM2_HALT_VECTOR + 2u), VM2_VECTOR_PSW,
                            err, err_len) != 0) {
        return -1;
    }

    return 0;
}

static void ubmap_sync_from_cpu(const regs *r, lsi11_machine_t machine)
{
    int enabled = 0;

    if (!r || machine != LSI11_MACHINE_1184 || r->model != DCJ11) {
        ubmap_set_enabled(0);
        return;
    }

#if defined(ENABLE_MMU) && (ENABLE_MMU)
    enabled = (r->mmu_ssr3 & MMR3_BME) ? 1 : 0;
#endif
    ubmap_set_enabled(enabled);
}

static void bus_iowin_sync_from_cpu(const regs *r, lsi11_machine_t machine)
{
    int io16 = 1;
    int m22e = 0;

#if defined(ENABLE_MMU) && (ENABLE_MMU)
    if (r && machine == LSI11_MACHINE_1184 && r->model == DCJ11) {
        if (r->mmu_ssr0 & MMR0_RELO_ENABLE) {
            io16 = 0;
            m22e = (r->mmu_ssr3 & MMR3_M22E) ? 1 : 0;
        }
    }
#else
    (void)r;
    (void)machine;
#endif

    bus_set_pdp1184_io_16bit(io16);
    bus_set_pdp1184_m22e(m22e);
}

static int install_bootstrap(uint16_t base, const uint16_t *words, size_t word_count)
{
    uint8_t *ram = bus_ram_ptr(base);
    size_t n = word_count * sizeof(uint16_t);

    if (!ram || !bus_range_is_ram(base, n)) {
        return -1;
    }

    for (size_t i = 0; i < word_count; i++) {
        uint16_t w = words[i];
        ram[i * 2 + 0] = (uint8_t)(w & 000377u);
        ram[i * 2 + 1] = (uint8_t)((w >> 8) & 000377u);
    }

    return 0;
}

static int preload_rt11_boot_block(const char *path)
{
    uint8_t buf[RT11_BOOT_BLOCK_BYTES * 2u];
    emu_file_t *f;
    size_t got;

    if (!path) {
        return -1;
    }

    f = emu_fopen(path, "rb");
    if (!f) {
        return -1;
    }

    got = emu_fread(buf, 1, sizeof(buf), f);
    emu_fclose(f);

    if (got < RT11_BOOT_BLOCK_BYTES) {
        return -1;
    }

    return preload_rt11_boot_block_bytes(buf, got);
}

static int preload_rt11_boot_block_bytes(const uint8_t *buf, size_t len)
{
    int block = -1;
    uint8_t *ram0;

    if (!buf || len < RT11_BOOT_BLOCK_BYTES) {
        return -1;
    }

    ram0 = bus_ram_ptr(0);
    if (!ram0 || !bus_range_is_ram(0, RT11_BOOT_BLOCK_BYTES)) {
        return -1;
    }

    for (size_t i = 0; i < RT11_BOOT_BLOCK_BYTES; i++) {
        if (buf[i]) {
            block = 0;
            break;
        }
    }
    if (block < 0 && len >= (RT11_BOOT_BLOCK_BYTES * 2u)) {
        for (size_t i = 0; i < RT11_BOOT_BLOCK_BYTES; i++) {
            if (buf[RT11_BOOT_BLOCK_BYTES + i]) {
                block = 1;
                break;
            }
        }
    }
    if (block < 0) {
        return -1;
    }

    memcpy(ram0, buf + (block * RT11_BOOT_BLOCK_BYTES), RT11_BOOT_BLOCK_BYTES);
    return 0;
}

static int preload_rt11_boot_block_attached(boot_copy_unit_fn copy_fn, unsigned unit,
                                            const char *fallback_path)
{
    uint8_t buf[RT11_BOOT_BLOCK_BYTES * 2u];

    if (copy_fn && copy_fn(unit, buf, sizeof(buf)) == 0 &&
            preload_rt11_boot_block_bytes(buf, sizeof(buf)) == 0) {
        return 0;
    }

    return preload_rt11_boot_block(fallback_path);
}

static int load_binary_into_ram(const char *path, long load_addr)
{
    emu_file_t *f;
    long fsize;
    uint8_t *dst;

    if (!path) {
        return -1;
    }

    f = emu_fopen(path, "rb");
    if (!f) {
        return -1;
    }
    if (emu_fseek(f, 0, EMU_SEEK_END) != 0) {
        emu_fclose(f);
        return -1;
    }
    fsize = emu_ftell(f);
    if (fsize < 0 || emu_fseek(f, 0, EMU_SEEK_SET) != 0) {
        emu_fclose(f);
        return -1;
    }
    if (load_addr < 0 ||
            !bus_range_is_ram((bus_paddr_t)load_addr, (size_t)fsize)) {
        emu_fclose(f);
        return -1;
    }

    dst = bus_ram_ptr((bus_paddr_t)load_addr);
    if (!dst) {
        emu_fclose(f);
        return -1;
    }
    if (emu_fread(dst, 1, (size_t)fsize, f) != (size_t)fsize) {
        emu_fclose(f);
        return -1;
    }

    emu_fclose(f);
    printf("Loaded %ld bytes from %s to octal %lo\n", fsize, path, load_addr);
    return 0;
}

static int str_eq_casefold(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0') ? 1 : 0;
}

static int has_image_extension(const char *name)
{
    static const char *exts[] = {
        "dsk", "img", "ima", "iso", "tap",
        "rk05", "rk06", "rk07",
        "rl01", "rl02",
        "rm03", "rm05", "rm80",
        "rp04", "rp05", "rp06"
    };
    const char *dot = strrchr(name, '.');

    if (!dot || !dot[1]) {
        return 0;
    }
    dot++;

    for (size_t i = 0; i < ARRAY_SIZE(exts); i++) {
        if (str_eq_casefold(dot, exts[i])) {
            return 1;
        }
    }
    return 0;
}

static int is_tape_image(const char *name)
{
    const char *dot = strrchr(name, '.');
    return (dot && dot[1] && str_eq_casefold(dot + 1, "tap")) ? 1 : 0;
}

static int read_line(char *buf, size_t buf_len)
{
    size_t n = 0;

    if (!buf || buf_len == 0) {
        return -1;
    }

    for (;;) {
        int c = getchar_timeout_us(100000);
        if (c == PICO_ERROR_TIMEOUT) {
            display_backend_task();
            tight_loop_contents();
            continue;
        }

        if (c == '\r' || c == '\n') {
            putchar('\n');
            display_backend_task();
            break;
        }

        if ((c == 0x08 || c == 0x7f) && n > 0) {
            n--;
            printf("\b \b");
            display_backend_task();
            continue;
        }

        if (c < 32 || c > 126) {
            continue;
        }

        if ((n + 1) < buf_len) {
            buf[n++] = (char)c;
            putchar(c);
            display_backend_task();
        }
    }

    buf[n] = '\0';
    return (int)n;
}

static int prompt_number(const char *prompt, int min_value, int max_value)
{
    char line[32];

    for (;;) {
        char *end = NULL;
        long v;

        printf("%s", prompt);
        if (read_line(line, sizeof(line)) < 0) {
            continue;
        }

        v = strtol(line, &end, 10);
        if (end && *end == '\0' && v >= min_value && v <= max_value) {
            return (int)v;
        }

        printf("Enter a number from %d to %d.\n", min_value, max_value);
    }
}

static void prompt_and_set_clock(lsi11_options_t *opts)
{
    int max_idx = (int)ARRAY_SIZE(freq_profiles);
    int choice;
    const freq_profile_t *p;

    printf("Select RP2040 frequency:\n");
    for (int i = 0; i < max_idx; i++) {
        printf("%d. %u MHz (VREG %s)\n", i + 1, freq_profiles[i].mhz,
               freq_profiles[i].vreg_name);
    }

    choice = prompt_number("Frequency number: ", 1, max_idx);
    p = &freq_profiles[choice - 1];
    if (opts) {
        opts->sys_clock_mhz = p->mhz;
    }
    apply_freq_profile(p);
}

static byte prompt_cpu_model(lsi11_machine_t machine_kind)
{
    int choice;

    if (machine_kind == LSI11_MACHINE_1184) {
        printf("Select CPU for pdp11/84:\n");
        printf("1. dcj11 (DEC 11/84)\n");
    } else {
        printf("Select CPU for lsi11:\n");
        printf("1. dcj11 (11/03-compatible)\n");
    }
    printf("2. k1801vm1\n");
    printf("3. k1801vm2\n");

    choice = prompt_number("CPU number: ", 1, 3);
    switch (choice) {
    case 2:
        return K1801VM1;
    case 3:
        return K1801VM2;
    default:
        return DCJ11;
    }
}

static int prompt_rl_type(void)
{
    printf("Select RL image type:\n");
    printf("0. auto\n");
    printf("1. rl01\n");
    printf("2. rl02\n");
    return prompt_number("RL type number: ", 0, 2);
}

static int prompt_force_override(const char *name)
{
    printf("%s override:\n", name);
    printf("0. skip\n");
    printf("1. force enable\n");
    printf("2. force disable\n");
    return prompt_number("Override number: ", 0, 2);
}

static int mount_sd_card(void)
{
    sd_card_t *sd = sd_get_by_num(0);
    FRESULT fr = FR_DISK_ERR;

    if (!sd) {
        printf("SD: device is not defined in hw_config.\n");
        return -1;
    }

    for (int attempt = 1; attempt <= 5; attempt++) {
        fr = f_mount(&sd->fatfs, sd->pcName, 1);
        if (fr == FR_OK) {
            return 0;
        }
        printf("SD: f_mount attempt %d/5 failed: %s (%d)\n", attempt,
               FRESULT_str(fr), fr);
        f_unmount(sd->pcName);
        sleep_ms(250);
    }

    return -1;
}

static int list_images(image_entry_t *images, int max_images, image_filter_t filter)
{
    DIR dir;
    FILINFO fno;
    FRESULT fr;
    int count = 0;

    fr = f_opendir(&dir, "0:/");
    if (fr != FR_OK) {
        printf("SD: f_opendir failed: %s (%d)\n", FRESULT_str(fr), fr);
        return -1;
    }

    for (;;) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK) {
            printf("SD: f_readdir failed: %s (%d)\n", FRESULT_str(fr), fr);
            f_closedir(&dir);
            return -1;
        }
        if (!fno.fname[0]) {
            break;
        }
        if (fno.fattrib & AM_DIR) {
            continue;
        }
        if (fno.fname[0] == '.') {
            continue;
        }
        if (fno.fname[0] == '_' && fno.fname[1] == '\0') {
            continue;
        }
        if (fno.fname[0] == '.' && fno.fname[1] == '_') {
            continue;
        }
        if (!has_image_extension(fno.fname)) {
            continue;
        }
        if (filter == IMAGE_FILTER_DISK && is_tape_image(fno.fname)) {
            continue;
        }
        if (filter == IMAGE_FILTER_TAPE && !is_tape_image(fno.fname)) {
            continue;
        }
        if (count < max_images) {
            snprintf(images[count].name, sizeof(images[count].name), "%s", fno.fname);
            snprintf(images[count].path, sizeof(images[count].path), "0:/%s",
                     fno.fname);
        }
        count++;
    }

    f_closedir(&dir);

    if (count > max_images) {
        printf("Found more than %d images, showing first %d.\n", max_images,
               max_images);
        count = max_images;
    }

    return count;
}

static int has_conf_extension(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot || !dot[1]) {
        return 0;
    }

    return (str_eq_casefold(dot + 1, "conf") ||
            str_eq_casefold(dot + 1, "con")) ? 1 : 0;
}

static int is_default_conf_name(const char *name)
{
    return str_eq_casefold(name, "default.conf") ||
           str_eq_casefold(name, "default.con");
}

static int list_conf_files(image_entry_t *files, int max_files)
{
    DIR dir;
    FILINFO fno;
    FRESULT fr;
    int count = 0;

    fr = f_opendir(&dir, "0:/");
    if (fr != FR_OK) {
        printf("SD: f_opendir failed: %s (%d)\n", FRESULT_str(fr), fr);
        return -1;
    }

    for (;;) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK) {
            printf("SD: f_readdir failed: %s (%d)\n", FRESULT_str(fr), fr);
            f_closedir(&dir);
            return -1;
        }
        if (!fno.fname[0]) {
            break;
        }
        if (fno.fattrib & AM_DIR) {
            continue;
        }
        if (!has_conf_extension(fno.fname)) {
            continue;
        }
        if (is_default_conf_name(fno.fname)) {
            continue;
        }
        if (count < max_files) {
            snprintf(files[count].name, sizeof(files[count].name), "%s", fno.fname);
            snprintf(files[count].path, sizeof(files[count].path), "0:/%s",
                     fno.fname);
        }
        count++;
    }

    f_closedir(&dir);

    if (count > max_files) {
        printf("Found more than %d config files, showing first %d.\n", max_files,
               max_files);
        count = max_files;
    }

    return count;
}

static void print_image_list(const char *title, const image_entry_t *images, int image_count)
{
    printf("%s\n", title);
    for (int i = 0; i < image_count; i++) {
        printf("%d. %s\n", i + 1, images[i].name);
    }
}

static const char *persist_menu_path(const char *path)
{
    size_t len;
    char *copy;

    if (!path) {
        return NULL;
    }

    len = strlen(path) + 1u;
    copy = (char *)malloc(len);
    if (!copy) {
        fatal_halt("out of memory while saving selected image path");
    }

    memcpy(copy, path, len);
    return copy;
}

static int select_image_index(const char *controller_name, int unit,
                              const image_entry_t *images, int image_count)
{
    char prompt[96];

    if (image_count <= 0) {
        return -1;
    }

    snprintf(prompt, sizeof(prompt), "Select %s image for unit %o: ",
             controller_name, unit);
    return prompt_number(prompt, 1, image_count) - 1;
}

static void prompt_boot_selection(lsi11_options_t *opts)
{
    struct boot_choice {
        int kind;
        int unit;
        const char *path;
    } choices[RK11_MAX_DRIVES + RH11_MAX_DRIVES + RQ11_MAX_UNITS +
              RL11_MAX_DRIVES + TQ11_MAX_UNITS];
    int choice_count = 0;
    int selection;

    printf("Boot device:\n");
    printf("0. none\n");

    for (int unit = 0; unit < opts->rk_count; unit++) {
        printf("%d. rk%o (%s)\n", choice_count + 1, unit, opts->rk_path[unit]);
        choices[choice_count].kind = BOOT_DEV_RK;
        choices[choice_count].unit = unit;
        choices[choice_count].path = opts->rk_path[unit];
        choice_count++;
    }
    for (int unit = 0; unit < opts->rh_count; unit++) {
        printf("%d. rh%o (%s)\n", choice_count + 1, unit, opts->rh_path[unit]);
        choices[choice_count].kind = BOOT_DEV_RH;
        choices[choice_count].unit = unit;
        choices[choice_count].path = opts->rh_path[unit];
        choice_count++;
    }
    for (int unit = 0; unit < opts->rq_count; unit++) {
        printf("%d. rq%o (%s)\n", choice_count + 1, unit, opts->rq_path[unit]);
        choices[choice_count].kind = BOOT_DEV_RQ;
        choices[choice_count].unit = unit;
        choices[choice_count].path = opts->rq_path[unit];
        choice_count++;
    }
    for (int unit = 0; unit < opts->rl_count; unit++) {
        printf("%d. rl%o (%s)\n", choice_count + 1, unit,
               opts->rl_path[unit].path);
        choices[choice_count].kind = BOOT_DEV_RL;
        choices[choice_count].unit = unit;
        choices[choice_count].path = opts->rl_path[unit].path;
        choice_count++;
    }
    for (int unit = 0; unit < opts->tq_count; unit++) {
        printf("%d. tq%o (%s)\n", choice_count + 1, unit, opts->tq_path[unit]);
        choices[choice_count].kind = BOOT_DEV_TQ;
        choices[choice_count].unit = unit;
        choices[choice_count].path = opts->tq_path[unit];
        choice_count++;
    }

    if (choice_count == 0) {
        printf("No bootable devices attached; emulator will start without auto boot.\n");
        return;
    }

    selection = prompt_number("Boot selection: ", 0, choice_count);
    if (selection == 0) {
        return;
    }

    opts->do_boot = 1;
    opts->boot_kind = choices[selection - 1].kind;
    opts->boot_unit = choices[selection - 1].unit;
}

static void collect_menu_options(lsi11_options_t *opts, lsi11_machine_t machine_kind)
{
    image_entry_t disk_images[MAX_IMAGES];
    image_entry_t tape_images[MAX_IMAGES];
    int disk_count;
    int tape_count;
    int attach_count;

    options_init(opts);
    prompt_and_set_clock(opts);

    if (machine_kind == LSI11_MACHINE_1184) {
        printf("Fixed mode: pdp11/84\n");
    } else {
        printf("Fixed mode: lsi11\n");
    }

    opts->cpu_model = prompt_cpu_model(machine_kind);
    switch (prompt_force_override("FIS")) {
    case 1:
        opts->force_fis = 1;
        opts->disable_fis = 0;
        break;
    case 2:
        opts->force_fis = 0;
        opts->disable_fis = 1;
        break;
    default:
        break;
    }
    switch (prompt_force_override("FP11")) {
    case 1:
        opts->force_fp11 = 1;
        opts->disable_fp11 = 0;
        break;
    case 2:
        opts->force_fp11 = 0;
        opts->disable_fp11 = 1;
        break;
    default:
        break;
    }
    if (display_backend_supported()) {
        opts->display_enable = 1;
        printf("Terminal display mirroring: enabled (%s)\n", display_backend_name());
    } else {
        opts->display_enable = 0;
        printf("Terminal display mirroring: unavailable in this firmware build\n");
    }

    disk_count = list_images(disk_images, MAX_IMAGES, IMAGE_FILTER_DISK);
    tape_count = list_images(tape_images, MAX_IMAGES, IMAGE_FILTER_TAPE);

    if (disk_count < 0 || tape_count < 0) {
        fatal_halt("error reading SD directory");
    }
    if (disk_count == 0 && tape_count == 0) {
        fatal_halt("no storage images found on SD");
    }

    if (disk_count > 0) {
        print_image_list("Disk images on SD:", disk_images, disk_count);
    } else {
        printf("No disk images found on SD.\n");
    }
    if (tape_count > 0) {
        print_image_list("Tape images on SD:", tape_images, tape_count);
    }

    if (disk_count > 0) {
        attach_count = prompt_number("Number of RK images to attach: ", 0,
                                     RK11_MAX_DRIVES);
        for (int unit = 0; unit < attach_count; unit++) {
            int idx = select_image_index("RK", unit, disk_images, disk_count);
            opts->rk_path[opts->rk_count++] = persist_menu_path(disk_images[idx].path);
        }

        attach_count = prompt_number("Number of RH images to attach: ", 0,
                                     RH11_MAX_DRIVES);
        for (int unit = 0; unit < attach_count; unit++) {
            int idx = select_image_index("RH", unit, disk_images, disk_count);
            opts->rh_path[opts->rh_count++] = persist_menu_path(disk_images[idx].path);
        }

        attach_count = prompt_number("Number of XP/RP images to attach: ", 0,
                                     XP_MAX_DRIVES);
        for (int unit = 0; unit < attach_count; unit++) {
            int idx = select_image_index("XP", unit, disk_images, disk_count);
            opts->xp_path[opts->xp_count++] = persist_menu_path(disk_images[idx].path);
        }

        attach_count = prompt_number("Number of RQ images to attach: ", 0,
                                     RQ11_MAX_UNITS);
        for (int unit = 0; unit < attach_count; unit++) {
            int idx = select_image_index("RQ", unit, disk_images, disk_count);
            opts->rq_path[opts->rq_count++] = persist_menu_path(disk_images[idx].path);
        }

        attach_count = prompt_number("Number of RL images to attach: ", 0,
                                     RL11_MAX_DRIVES);
        for (int unit = 0; unit < attach_count; unit++) {
            int idx = select_image_index("RL", unit, disk_images, disk_count);
            opts->rl_path[opts->rl_count].path = persist_menu_path(disk_images[idx].path);
            opts->rl_path[opts->rl_count].type = prompt_rl_type();
            opts->rl_count++;
        }
    }

    if (tape_count > 0) {
        attach_count = prompt_number("Number of TQ tape images to attach: ", 0,
                                     TQ11_MAX_UNITS);
        for (int unit = 0; unit < attach_count; unit++) {
            int idx = select_image_index("TQ", unit, tape_images, tape_count);
            opts->tq_path[opts->tq_count++] = persist_menu_path(tape_images[idx].path);
        }
    }

    prompt_boot_selection(opts);
}

static int emu_file_exists(const char *path)
{
    emu_file_t *f = emu_fopen(path, "r");
    if (!f) {
        return 0;
    }
    emu_fclose(f);
    return 1;
}

static int maybe_select_conf_file(lsi11_options_t *opts, int have_default_config)
{
    image_entry_t conf_files[MAX_IMAGES];
    int conf_count;
    int selection;

    conf_count = list_conf_files(conf_files, MAX_IMAGES);
    if (conf_count < 0) {
        return -1;
    }
    if (conf_count == 0) {
        return have_default_config;
    }

    printf("Other config files on SD:\n");
    if (have_default_config) {
        printf("0. continue with default.conf\n");
    } else {
        printf("0. skip config file and open interactive menu\n");
    }
    for (int i = 0; i < conf_count; i++) {
        printf("%d. %s\n", i + 1, conf_files[i].name);
    }

    selection = prompt_number("Config selection: ", 0, conf_count);
    if (selection == 0) {
        return have_default_config;
    }

    options_init(opts);
    {
        char file_arg[MAX_IMAGE_PATH + 4];
        char *argv[3];

        snprintf(file_arg, sizeof(file_arg), "@%s", conf_files[selection - 1].path);
        argv[0] = (char *)"pico-lsi11";
        argv[1] = file_arg;
        argv[2] = NULL;

        printf("Loading %s\n", conf_files[selection - 1].path);
        if (options_parse(opts, 2, argv) != 0) {
            return -1;
        }
    }

    return 1;
}

static int load_default_options(lsi11_options_t *opts)
{
    char file_arg[MAX_IMAGE_PATH + 4];
    char *argv[3];

    if (!emu_file_exists(DEFAULT_OPTIONS_FILE)) {
        return 0;
    }

    options_init(opts);
    snprintf(file_arg, sizeof(file_arg), "@%s", DEFAULT_OPTIONS_FILE);
    argv[0] = (char *)"pico-lsi11";
    argv[1] = file_arg;
    argv[2] = NULL;

    printf("Auto-loading %s\n", DEFAULT_OPTIONS_FILE);
    if (options_parse(opts, 2, argv) != 0) {
        return -1;
    }

    return 1;
}

static void resolve_display_option(lsi11_options_t *opts)
{
    if (!opts) {
        return;
    }
    if (opts->display_enable < 0) {
        opts->display_enable = display_backend_supported() ? 1 : 0;
    }
}

static int prepare_machine_options(lsi11_options_t *opts, lsi11_machine_t machine_kind,
                                   char *err, size_t err_len)
{
    if (opts->trace_irq) {
        lsi11_set_trace_irq(1);
    }
    if (opts->trace_nxm) {
        lsi11_set_trace_nxm(1);
    }

    if (rh11_set_mode(opts->rh_mode) != 0) {
        snprintf(err, err_len, "RH11 mode configuration error");
        return -1;
    }

    if (opts->dz_port_set) {
        snprintf(err, err_len, "DZ11 TCP listener is not available on pico-lsi11");
        return -1;
    }

    if (machine_kind == LSI11_MACHINE_1184) {
        uint32_t ram_limit_kb = pico_pdp1184_ram_limit_kb();

        if (opts->ram_kb_arg <= 0) {
            opts->ram_kb_arg = (long)ram_limit_kb;
        }
        if ((uint32_t)opts->ram_kb_arg > ram_limit_kb) {
            snprintf(err, err_len, "pico pdp11/84 is limited to %u KB RAM",
                     ram_limit_kb);
            return -1;
        }
    } else {
        if (opts->ram_kb_arg >= 0) {
            snprintf(err, err_len,
                     "This lsi11 target is fixed 56KB RAM; -ram is not supported.");
            return -1;
        }
        opts->ram_kb_arg = 0;
    }

    if (lsi11_machine_configure(machine_kind, (uint32_t)opts->ram_kb_arg, err,
                                err_len) != 0) {
        return -1;
    }

    if (cpu_is_vm2(opts->cpu_model) &&
            bus_vm2_configure(BUS_VM2_DEFAULT_USER_RAM_BYTES,
                              VM2_MIN_HALT_RAM_BYTES, err, err_len) != 0) {
        return -1;
    }

    if (opts->force_dl11_alias >= 0) {
        lsi11_set_dl11_alias(opts->force_dl11_alias);
    }

    {
        int kw11_l_on = 1;
        int kw11_p_on = 0;

        if (opts->kw11_l_override >= 0) {
            kw11_l_on = opts->kw11_l_override;
        }
        if (opts->kw11_p_override >= 0) {
            kw11_p_on = opts->kw11_p_override;
        }
        kw11_set_visibility(kw11_l_on, kw11_p_on);
    }

    if (opts->cpu_model == K1801VM1) {
        if (lsi11_set_device_enabled("vm1sel", 1, err, err_len) != 0 ||
                lsi11_set_device_enabled("vm1sav", 1, err, err_len) != 0) {
            return -1;
        }
    } else {
        if (lsi11_set_device_enabled("vm1sel", 0, err, err_len) != 0 ||
                lsi11_set_device_enabled("vm1sav", 0, err, err_len) != 0) {
            return -1;
        }
    }

    if (opts->disable_dl &&
            lsi11_set_device_enabled("dl11", 0, err, err_len) != 0) {
        return -1;
    }
    if (opts->disable_dz &&
            lsi11_set_device_enabled("dz11", 0, err, err_len) != 0) {
        return -1;
    }
    if (opts->disable_kw &&
            lsi11_set_device_enabled("kw11", 0, err, err_len) != 0) {
        return -1;
    }
    if (opts->disable_lp &&
            lsi11_set_device_enabled("lp11", 0, err, err_len) != 0) {
        return -1;
    }
    if (opts->disable_rk &&
            lsi11_set_device_enabled("rk11", 0, err, err_len) != 0) {
        return -1;
    }
    if (opts->disable_rh &&
            lsi11_set_device_enabled("rh11", 0, err, err_len) != 0) {
        return -1;
    }
    if (opts->disable_rq &&
            lsi11_set_device_enabled("rq11", 0, err, err_len) != 0) {
        return -1;
    }
    if (opts->disable_xp &&
            lsi11_set_device_enabled("xp11", 0, err, err_len) != 0) {
        return -1;
    }
    if (opts->disable_rl &&
            lsi11_set_device_enabled("rl11", 0, err, err_len) != 0) {
        return -1;
    }
    if (opts->disable_tq &&
            lsi11_set_device_enabled("tq11", 0, err, err_len) != 0) {
        return -1;
    }
    if (opts->disable_sr &&
            lsi11_set_device_enabled("sr", 0, err, err_len) != 0) {
        return -1;
    }
    if (opts->xp_count > 0 && !opts->disable_xp &&
            lsi11_set_device_enabled("xp11", 1, err, err_len) != 0) {
        return -1;
    }
    if (opts->rq_count > 0 && !opts->disable_rq &&
            lsi11_set_device_enabled("rq11", 1, err, err_len) != 0) {
        return -1;
    }
    if (opts->tq_count > 0 && !opts->disable_tq &&
            lsi11_set_device_enabled("tq11", 1, err, err_len) != 0) {
        return -1;
    }

    if (opts->rk_count > 0 && !lsi11_device_enabled("rk11")) {
        snprintf(err, err_len, "-rk is not allowed with -disable-rk");
        return -1;
    }
    if (opts->rh_count > 0 && !lsi11_device_enabled("rh11")) {
        snprintf(err, err_len, "-rh is not allowed with -disable-rh");
        return -1;
    }
    if (opts->rq_count > 0 && !lsi11_device_enabled("rq11")) {
        snprintf(err, err_len, "-rq is not allowed with -disable-rq");
        return -1;
    }
    if (opts->xp_count > 0 && !lsi11_device_enabled("xp11")) {
        snprintf(err, err_len, "-xp/-rp is not allowed with -disable-xp");
        return -1;
    }
    if (opts->rl_count > 0 && !lsi11_device_enabled("rl11")) {
        snprintf(err, err_len, "-rl/-rl01/-rl02 is not allowed with -disable-rl");
        return -1;
    }
    if (opts->tq_count > 0 && !lsi11_device_enabled("tq11")) {
        snprintf(err, err_len, "-tq is not allowed with -disable-tq");
        return -1;
    }
    if (opts->do_boottq && opts->tq_count == 0) {
        snprintf(err, err_len, "-boottq requires -tq <image>");
        return -1;
    }
    if (opts->do_boot && (opts->do_bootcopy || opts->do_bootrt11 || opts->do_boottq)) {
        snprintf(err, err_len,
                 "-boot cannot be combined with -bootcopy/-bootrt11/-boottq");
        return -1;
    }

    if (opts->do_boot) {
        switch (opts->boot_kind) {
        case BOOT_DEV_RK:
            if (!lsi11_device_enabled("rk11")) {
                snprintf(err, err_len, "-boot rk* requires RK11 enabled");
                return -1;
            }
            if (opts->boot_unit >= opts->rk_count || !opts->rk_path[opts->boot_unit]) {
                snprintf(err, err_len, "-boot rk%o requires matching -rk attachment",
                         opts->boot_unit);
                return -1;
            }
            break;

        case BOOT_DEV_RH:
            if (!lsi11_device_enabled("rh11")) {
                snprintf(err, err_len, "-boot rh* requires RH11 enabled");
                return -1;
            }
            if (opts->boot_unit >= opts->rh_count || !opts->rh_path[opts->boot_unit]) {
                snprintf(err, err_len, "-boot rh%o requires matching -rh attachment",
                         opts->boot_unit);
                return -1;
            }
            break;

        case BOOT_DEV_RQ:
            if (!lsi11_device_enabled("rq11")) {
                snprintf(err, err_len, "-boot rq* requires RQ enabled");
                return -1;
            }
            if (opts->boot_unit >= opts->rq_count || !opts->rq_path[opts->boot_unit]) {
                snprintf(err, err_len, "-boot rq%o requires matching -rq attachment",
                         opts->boot_unit);
                return -1;
            }
            break;

        case BOOT_DEV_RL:
            if (!lsi11_device_enabled("rl11")) {
                snprintf(err, err_len, "-boot rl* requires RL11 enabled");
                return -1;
            }
            if (opts->boot_unit >= opts->rl_count ||
                    !opts->rl_path[opts->boot_unit].path) {
                snprintf(err, err_len, "-boot rl%o requires matching -rl attachment",
                         opts->boot_unit);
                return -1;
            }
            break;

        case BOOT_DEV_TQ:
            if (!lsi11_device_enabled("tq11")) {
                snprintf(err, err_len, "-boot tq* requires TQ11 enabled");
                return -1;
            }
            if (opts->boot_unit >= opts->tq_count || !opts->tq_path[opts->boot_unit]) {
                snprintf(err, err_len, "-boot tq%o requires matching -tq attachment",
                         opts->boot_unit);
                return -1;
            }
            break;

        default:
            snprintf(err, err_len, "Unknown boot device");
            return -1;
        }
    }

    return 0;
}

static void print_config_summary(const lsi11_options_t *opts)
{
    const char *machine = (lsi11_machine_current() == LSI11_MACHINE_1184) ?
                          "pdp1184" : "lsi11";

    printf("CONFIG machine=%s cpu=%s ram_kb=%u dl11_alias=%d rh_mode=%s "
           "display=%d display_backend=%s dev_dl=%d dev_dz=%d dev_kw=%d "
           "dev_lp=%d dev_rk=%d dev_rh=%d dev_rq=%d dev_xp=%d dev_rl=%d "
           "dev_tq=%d dev_sr=%d "
           "dev_vm1sel=%d dev_vm1sav=%d\n",
           machine, cpu_model_name(opts->cpu_model), lsi11_machine_ram_kb(),
           lsi11_dl11_alias(), rh11_mode_name(opts->rh_mode),
           opts->display_enable ? 1 : 0, display_backend_name(),
           lsi11_device_enabled("dl11"), lsi11_device_enabled("dz11"),
           lsi11_device_enabled("kw11"), lsi11_device_enabled("lp11"),
           lsi11_device_enabled("rk11"), lsi11_device_enabled("rh11"),
           lsi11_device_enabled("rq11"), lsi11_device_enabled("xp11"),
           lsi11_device_enabled("rl11"),
           lsi11_device_enabled("tq11"), lsi11_device_enabled("sr"),
           lsi11_device_enabled("vm1sel"), lsi11_device_enabled("vm1sav"));

    for (int unit = 0; unit < opts->rk_count; unit++) {
        printf("ATTACH rk%o=%s\n", unit, opts->rk_path[unit]);
    }
    for (int unit = 0; unit < opts->rh_count; unit++) {
        printf("ATTACH rh%o=%s\n", unit, opts->rh_path[unit]);
    }
    for (int unit = 0; unit < opts->rq_count; unit++) {
        printf("ATTACH rq%o=%s\n", unit, opts->rq_path[unit]);
    }
    for (int unit = 0; unit < opts->xp_count; unit++) {
        printf("ATTACH xp%o=%s\n", unit, opts->xp_path[unit]);
    }
    for (int unit = 0; unit < opts->rl_count; unit++) {
        const char *rl_type = "auto";
        if (opts->rl_path[unit].type == RL11_TYPE_RL01) {
            rl_type = "rl01";
        } else if (opts->rl_path[unit].type == RL11_TYPE_RL02) {
            rl_type = "rl02";
        }
        printf("ATTACH rl%o=%s type=%s\n", unit, opts->rl_path[unit].path, rl_type);
    }
    for (int unit = 0; unit < opts->tq_count; unit++) {
        printf("ATTACH tq%o=%s\n", unit, opts->tq_path[unit]);
    }

    if (opts->do_boot) {
        const char *kind = "unknown";
        switch (opts->boot_kind) {
        case BOOT_DEV_RK:
            kind = "rk";
            break;
        case BOOT_DEV_RH:
            kind = "rh";
            break;
        case BOOT_DEV_RQ:
            kind = "rq";
            break;
        case BOOT_DEV_RL:
            kind = "rl";
            break;
        case BOOT_DEV_TQ:
            kind = "tq";
            break;
        default:
            break;
        }
        printf("BOOT %s%o\n", kind, opts->boot_unit);
    } else if (opts->do_bootcopy) {
        printf("BOOT bootcopy\n");
    } else if (opts->do_bootrt11) {
        printf("BOOT bootrt11\n");
    } else if (opts->do_boottq) {
        printf("BOOT boottq\n");
    }
}

static int attach_media(regs *r, const lsi11_options_t *opts, char *err, size_t err_len)
{
    (void)r;
    (void)err;
    (void)err_len;

    cleanup_media();

    for (int unit = 0; unit < opts->rk_count; unit++) {
        if (rk11_open_image_unit((unsigned)unit, opts->rk_path[unit]) != 0) {
            snprintf(err, err_len, "rk11_open_image failed: rk%o %s",
                     unit, opts->rk_path[unit]);
            cleanup_media();
            return -1;
        }
    }
    for (int unit = 0; unit < opts->rh_count; unit++) {
        if (rh11_open_image_unit((unsigned)unit, opts->rh_path[unit]) != 0) {
            snprintf(err, err_len, "rh11_open_image failed: rh%o %s",
                     unit, opts->rh_path[unit]);
            cleanup_media();
            return -1;
        }
    }
    for (int unit = 0; unit < opts->rq_count; unit++) {
        if (rq11_open_image_unit((unsigned)unit, opts->rq_path[unit]) != 0) {
            snprintf(err, err_len, "rq11_open_image failed: rq%o %s",
                     unit, opts->rq_path[unit]);
            cleanup_media();
            return -1;
        }
    }
    for (int unit = 0; unit < opts->xp_count; unit++) {
        if (xp_open_image_unit((unsigned)unit, opts->xp_path[unit]) != 0) {
            snprintf(err, err_len, "xp_open_image failed: xp%o %s",
                     unit, opts->xp_path[unit]);
            cleanup_media();
            return -1;
        }
    }
    for (int unit = 0; unit < opts->rl_count; unit++) {
        if (rl11_open_image_typed_unit((unsigned)unit, opts->rl_path[unit].path,
                                       opts->rl_path[unit].type) != 0) {
            snprintf(err, err_len, "rl11_open_image failed: rl%o %s",
                     unit, opts->rl_path[unit].path);
            cleanup_media();
            return -1;
        }
    }
    for (int unit = 0; unit < opts->tq_count; unit++) {
        if (tq11_open_image_unit((unsigned)unit, opts->tq_path[unit]) != 0) {
            snprintf(err, err_len, "tq11_open_image failed: tq%o %s",
                     unit, opts->tq_path[unit]);
            cleanup_media();
            return -1;
        }
    }

    return 0;
}

static int apply_boot_options(regs *r, const lsi11_options_t *opts, char *err,
                              size_t err_len)
{
    if (opts->do_boot) {
        switch (opts->boot_kind) {
        case BOOT_DEV_RK:
            if (preload_rt11_boot_block_attached(rk11_boot_copy_unit,
                                                 (unsigned)opts->boot_unit,
                                                 opts->rk_path[opts->boot_unit]) != 0) {
                snprintf(err, err_len, "RT11 boot block not found in rk%o image",
                         opts->boot_unit);
                return -1;
            }
            if (install_bootstrap(RK_BOOT_ADDR, rk_bootstrap,
                                  ARRAY_SIZE(rk_bootstrap)) != 0) {
                snprintf(err, err_len, "boot destination is outside RAM");
                return -1;
            }
            bus_write16((uint16_t)(RK_BOOT_ADDR + 000010u),
                        (uint16_t)opts->boot_unit);
            r->r[7] = RK_BOOT_ENTRY;
            break;

        case BOOT_DEV_RH:
            if (preload_rt11_boot_block_attached(rh11_boot_copy_unit,
                                                 (unsigned)opts->boot_unit,
                                                 opts->rh_path[opts->boot_unit]) != 0) {
                snprintf(err, err_len, "RT11 boot block not found in rh%o image",
                         opts->boot_unit);
                return -1;
            }
            if (install_bootstrap(RH_BOOT_ADDR, rh_bootstrap,
                                  ARRAY_SIZE(rh_bootstrap)) != 0) {
                snprintf(err, err_len, "boot destination is outside RAM");
                return -1;
            }
            bus_write16((uint16_t)(RH_BOOT_ADDR + 000010u),
                        (uint16_t)opts->boot_unit);
            r->r[7] = RH_BOOT_ENTRY;
            break;

        case BOOT_DEV_RQ:
            if (install_bootstrap(RQ_BOOT_ADDR, rq_bootstrap,
                                  ARRAY_SIZE(rq_bootstrap)) != 0) {
                snprintf(err, err_len, "boot destination is outside RAM");
                return -1;
            }
            bus_write16(RQ_BOOT_UNIT, (uint16_t)opts->boot_unit);
            bus_write16(RQ_BOOT_CSR, 0172150u);
            r->r[7] = RQ_BOOT_ENTRY;
            break;

        case BOOT_DEV_RL: {
            uint16_t ds = (uint16_t)((opts->boot_unit & 03) << 8);

            if (install_bootstrap(RL_BOOT_ADDR, rl_bootstrap,
                                  ARRAY_SIZE(rl_bootstrap)) != 0) {
                snprintf(err, err_len, "boot destination is outside RAM");
                return -1;
            }
            bus_write16((uint16_t)(RL_BOOT_ADDR + 000014u),
                        (uint16_t)(0000004u | ds));
            bus_write16((uint16_t)(RL_BOOT_ADDR + 000042u),
                        (uint16_t)(0000014u | ds));
            r->r[7] = RL_BOOT_ENTRY;
            break;
        }

        case BOOT_DEV_TQ:
            if (install_bootstrap(TQ_BOOT_ADDR, tq_bootstrap,
                                  ARRAY_SIZE(tq_bootstrap)) != 0) {
                snprintf(err, err_len, "boot destination is outside RAM");
                return -1;
            }
            bus_write16(TQ_BOOT_UNIT, (uint16_t)opts->boot_unit);
            bus_write16(TQ_BOOT_CSR, 0174500u);
            r->r[7] = TQ_BOOT_ENTRY;
            break;

        default:
            snprintf(err, err_len, "Unknown boot device");
            return -1;
        }
    }

    if (opts->do_bootcopy) {
        const size_t n = 010000u;
        int rc = -1;
        uint8_t *ram0 = bus_ram_ptr(0);

        if (!ram0 || !bus_range_is_ram(0, n)) {
            snprintf(err, err_len, "bootcopy destination is outside RAM");
            return -1;
        }
        if (opts->rk_count > 0) {
            rc = rk11_boot_copy(ram0, n);
        } else if (opts->rh_count > 0) {
            rc = rh11_boot_copy(ram0, n);
        } else if (opts->rl_count > 0) {
            rc = rl11_boot_copy(ram0, n);
        } else {
            snprintf(err, err_len,
                     "-bootcopy requires -rk <image>, -rh <image>, or -rl <image>");
            return -1;
        }
        if (rc != 0) {
            snprintf(err, err_len, "bootcopy failed (check -rk/-rh image)");
            return -1;
        }
        r->r[7] = 000000u;
    }

    if (opts->do_bootrt11) {
        if (opts->rl_count > 0) {
            if (install_bootstrap(RL_BOOT_ADDR, rl_bootstrap,
                                  ARRAY_SIZE(rl_bootstrap)) != 0) {
                snprintf(err, err_len, "bootrt11 destination is outside RAM");
                return -1;
            }
            r->r[7] = RL_BOOT_ENTRY;
        } else if (opts->rk_count > 0) {
            if (preload_rt11_boot_block_attached(rk11_boot_copy_unit, 0,
                                                 opts->rk_path[0]) != 0) {
                snprintf(err, err_len, "RT11 boot block not found in image");
                return -1;
            }
            if (install_bootstrap(RK_BOOT_ADDR, rk_bootstrap,
                                  ARRAY_SIZE(rk_bootstrap)) != 0) {
                snprintf(err, err_len, "bootrt11 destination is outside RAM");
                return -1;
            }
            r->r[7] = RK_BOOT_ENTRY;
        } else if (opts->rh_count > 0) {
            if (preload_rt11_boot_block_attached(rh11_boot_copy_unit, 0,
                                                 opts->rh_path[0]) != 0) {
                snprintf(err, err_len, "RT11 boot block not found in image");
                return -1;
            }
            if (install_bootstrap(RH_BOOT_ADDR, rh_bootstrap,
                                  ARRAY_SIZE(rh_bootstrap)) != 0) {
                snprintf(err, err_len, "bootrt11 destination is outside RAM");
                return -1;
            }
            r->r[7] = RH_BOOT_ENTRY;
        } else {
            snprintf(err, err_len,
                     "-bootrt11 requires -rk <image>, -rh <image>, or -rl <image>");
            return -1;
        }
    }

    if (opts->do_boottq) {
        if (install_bootstrap(TQ_BOOT_ADDR, tq_bootstrap,
                              ARRAY_SIZE(tq_bootstrap)) != 0) {
            snprintf(err, err_len, "boottq destination is outside RAM");
            return -1;
        }
        bus_write16(TQ_BOOT_UNIT, 0000000u);
        bus_write16(TQ_BOOT_CSR, 0174500u);
        r->r[7] = TQ_BOOT_ENTRY;
    }

    if (opts->load_path) {
        if (load_binary_into_ram(opts->load_path, opts->load_addr) != 0) {
            snprintf(err, err_len, "Cannot load file: %s", opts->load_path);
            return -1;
        }
    }

    if (opts->start_pc >= 0) {
        r->r[7] = (uint16_t)opts->start_pc;
        printf("Set PC to octal %lo\n", opts->start_pc);
    }

    return 0;
}

static void configure_run_settings(const lsi11_options_t *opts)
{
    memset(&g_run_config, 0, sizeof(g_run_config));
    g_run_config.trace = opts->trace ? 1 : 0;
    g_run_config.trace_regs = opts->trace_regs ? 1 : 0;
    g_run_config.trace_after = opts->trace_after;
    g_run_config.max_steps = opts->max_steps;
    g_run_config.exit_on_abort = opts->exit_on_abort ? 1 : 0;
    g_run_config.trace_limit =
        (opts->trace && opts->trace_after < 0) ? 2000 : -1;
}

static void trace_instruction(regs *r)
{
    char dbuf[128];
    word start_pc = r->r[7];
    word tmp = start_pc;
    char *dis_str = disas(r, &tmp, dbuf);
    int j = 0;

    printf("%06o ", start_pc);
    for (word a = start_pc; a < tmp; a = (word)(a + 2u)) {
        printf("%06o ", r->load_word(r, a));
        j++;
    }
    while (j < 3) {
        printf("       ");
        j++;
    }
    printf("%s\n", dis_str);

    if (g_run_config.trace_regs) {
        printf("R0=%06o R1=%06o R2=%06o R3=%06o R4=%06o R5=%06o SP=%06o PS=%06o\n",
               r->r[0], r->r[1], r->r[2], r->r[3], r->r[4], r->r[5], r->r[6],
               r->psw);
    }
}

static void park_core1(const char *reason, unsigned long steps_done)
{
    g_run_config.halted = 1;
    printf("%s after %lu steps\n", reason, steps_done);
    for (;;) {
        tight_loop_contents();
    }
}

/* ---- Core 1: CPU emulation loop ---- */
static void __not_in_flash_func(core1_entry)(void)
{
    regs *r = (regs *)(uintptr_t)multicore_fifo_pop_blocking();
    unsigned long steps_done = 0;

    for (;;) {
        int step_chunk;
        int steps_executed = 0;

        if (g_run_config.max_steps == 0) {
            park_core1("CPU stopped", steps_done);
        }

        step_chunk = g_run_config.trace ? 1 : 64;
        if (g_run_config.max_steps > 0 && step_chunk > g_run_config.max_steps) {
            step_chunk = (int)g_run_config.max_steps;
        }

        for (int i = 0; i < step_chunk; i++) {
            int trace_now = g_run_config.trace &&
                            (g_run_config.trace_after < 0 ||
                             steps_done >= (unsigned long)g_run_config.trace_after) &&
                            (g_run_config.trace_limit < 0 ||
                             steps_done < (unsigned long)g_run_config.trace_limit);

            bus_iowin_sync_from_cpu(r, g_machine_kind);
            if (trace_now) {
                trace_instruction(r);
            }

            core_step(r);
            steps_done++;
            steps_executed++;

            if (g_run_config.max_steps > 0) {
                g_run_config.max_steps--;
            }

            if (r->fAbort || g_run_config.max_steps == 0) {
                break;
            }
        }

        ubmap_sync_from_cpu(r, g_machine_kind);

        if (r->fAbort) {
            if (g_run_config.exit_on_abort) {
                r->fAbort = 0;
                park_core1("CPU stopped on abort", steps_done);
            }
            r->fAbort = 0;
        }

        if (g_run_config.max_steps == 0) {
            park_core1("CPU stopped", steps_done);
        }

        if (steps_executed == 0) {
            tight_loop_contents();
        }
    }
}

static int start_emulator(const lsi11_options_t *opts, char *err, size_t err_len)
{
    regs *r = NULL;
    static regs cpu_regs;

    memset(&cpu_regs, 0, sizeof(cpu_regs));
    cpu_regs.model = opts->cpu_model;
    r = &cpu_regs;

    dl11_set_8bit(opts->dl11_8bit);
    dl11_set_nl_to_cr(opts->do_nl_to_cr);
    dz11_set_8bit(opts->dl11_8bit);
    lsi11_hw_connect(r);

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

    if (r->init(r) != 0) {
        snprintf(err, err_len, "device initialization failed");
        return -1;
    }

    if (attach_media(r, opts, err, err_len) != 0) {
        r->fini(r);
        return -1;
    }

    if (opts->disable_fis) {
        r->has_fis = 0;
    } else if (opts->force_fis) {
        r->has_fis = 1;
    }
    if (opts->disable_fp11) {
        r->has_fpu = 0;
    } else if (opts->force_fp11) {
        r->has_fpu = 1;
    }

    r->ram_fast_size = (uint32_t)bus_ram_bytes();
    core_reset(r);
    bus_iowin_sync_from_cpu(r, g_machine_kind);
    ubmap_sync_from_cpu(r, g_machine_kind);

    if (opts->sr_value >= 0) {
        sr_set((uint16_t)opts->sr_value);
    } else {
        sr_set(0);
    }

    if (r->model == K1801VM2 || r->model == K1806VM2) {
        r->SEL0 = 0;
        r->SEL0 = 0200;
        if (vm2_install_halt_stub(err, err_len) != 0) {
            cleanup_media();
            r->fini(r);
            return -1;
        }
    }

    if (apply_boot_options(r, opts, err, err_len) != 0) {
        cleanup_media();
        r->fini(r);
        return -1;
    }

    configure_run_settings(opts);

    printf("Starting emulator...\n");

    g_io_spinlock = spin_lock_init(IO_SPINLOCK_NUM);
    multicore_launch_core1(core1_entry);
    multicore_fifo_push_blocking((uint32_t)(uintptr_t)r);

    int divisor = 0;
    for (;;) {
        if (g_run_config.halted) {
            display_backend_task();
            sleep_ms(10);
            continue;
        }
        lsi11_poll_devices();
        if (divisor % 500 == 0) {
            display_backend_task();
        }
        sleep_us(100);
    }
}

int main(void)
{
    lsi11_options_t opts;
    char cfg_err[160] = {0};
    int default_cfg_state;
    bool display_initialized = false;
    bool title_printed = false;

    stdio_init_all();
    sleep_ms(2000);
    if (display_backend_supported()) {
        if (!display_backend_init()) {
            fatal_halt("display initialization failed");
        }
        display_initialized = true;
        display_backend_set_output_enabled(true);
    }

    while (mount_sd_card() != 0) {
        char line[8];
        printf("SD is not initialized. Check power/wiring and press Enter to "
               "retry...\n");
        read_line(line, sizeof(line));
    }
    if (display_initialized) {
        (void)display_backend_show_boot_logo();
    }

#if defined(LSI11_TARGET_PDP1184)
    g_machine_kind = LSI11_MACHINE_1184;
#else
    g_machine_kind = LSI11_MACHINE_1104;
#endif

    default_cfg_state = load_default_options(&opts);
    if (default_cfg_state < 0) {
        fatal_halt("failed to parse default.conf");
    }
    default_cfg_state = maybe_select_conf_file(&opts, default_cfg_state);
    if (default_cfg_state < 0) {
        fatal_halt("failed to parse selected config file");
    }
    if (default_cfg_state == 0) {
        print_startup_banner();
        title_printed = true;
        collect_menu_options(&opts, g_machine_kind);
    }
    resolve_display_option(&opts);
    if (apply_configured_clock(&opts) != 0) {
        fatal_halt("failed to apply configured RP2040 frequency");
    }

    if (prepare_machine_options(&opts, g_machine_kind, cfg_err, sizeof(cfg_err)) != 0) {
        fatal_halt(cfg_err);
    }

    if (opts.display_enable > 0) {
        if (!display_backend_supported()) {
            fatal_halt("display mirroring requested, but this firmware was built without display support");
        }
        if (!display_initialized) {
            if (!display_backend_init()) {
                fatal_halt("display initialization failed");
            }
            display_initialized = true;
        }
        display_backend_set_output_enabled(true);
    } else if (display_initialized) {
        display_backend_set_output_enabled(false);
    }

    if (!title_printed) {
        print_startup_banner();
        title_printed = true;
    }

    print_config_summary(&opts);

    if (opts.check_config_only) {
        printf("Configuration check completed.\n");
        for (;;) {
            display_backend_task();
            sleep_ms(1000);
        }
    }

    if (start_emulator(&opts, cfg_err, sizeof(cfg_err)) != 0) {
        fatal_halt(cfg_err);
    }

    fatal_halt("emulator stopped unexpectedly");
    return 0;
}
