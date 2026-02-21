#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#include "dev_rh11.h"
#include "dev_rk11.h"
#include "dev_rl11.h"
#include "dev_sr.h"

#include "core.h"
#include "disas.h"

#include "term_display.h"

#define MAX_IMAGES 64
#define MAX_IMAGE_NAME 128
#define MAX_IMAGE_PATH 256
#define RT11_BOOT_BLOCK_BYTES 01000
#define RL_BOOT_ADDR 002000
#define RL_BOOT_ENTRY RL_BOOT_ADDR

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

typedef enum {
    IMAGE_TYPE_ANY = 0,
    IMAGE_TYPE_RK = 1,
    IMAGE_TYPE_RH = 2,
    IMAGE_TYPE_RL = 3,
} image_type_t;

static int prompt_number(const char *prompt, int min_value, int max_value);

static void prompt_and_set_clock(void)
{
    int max_idx = (int)(sizeof(freq_profiles) / sizeof(freq_profiles[0]));
    int choice;
    const freq_profile_t *p;
    bool ok;
    uint32_t actual_khz;

    printf("Select RP2040 frequency:\n");
    for (int i = 0; i < max_idx; i++) {
        printf("%d. %u MHz (VREG %s)\n", i + 1, freq_profiles[i].mhz,
               freq_profiles[i].vreg_name);
    }

    choice = prompt_number("Frequency number: ", 1, max_idx);
    p = &freq_profiles[choice - 1];

    vreg_set_voltage(p->vreg);
    sleep_ms(2);
    ok = set_sys_clock_khz((uint32_t)p->mhz * 1000u, true);
    actual_khz = clock_get_hz(clk_sys) / 1000u;

    if (ok) {
        printf("Clock set: %lu MHz (VREG %s)\n",
               (unsigned long)(actual_khz / 1000u), p->vreg_name);
    } else {
        printf("Failed to set %u MHz, current: %lu MHz\n", p->mhz,
               (unsigned long)(actual_khz / 1000u));
    }
}

static const char *image_type_name(image_type_t type)
{
    switch (type) {
    case IMAGE_TYPE_RK:
        return "RK";
    case IMAGE_TYPE_RH:
        return "RH";
    case IMAGE_TYPE_RL:
        return "RL";
    default:
        return "UNKNOWN";
    }
}

static image_type_t prompt_image_type(void)
{
    int choice;

    printf("Select image type:\n");
    printf("1. RK\n");
    printf("2. RH\n");
    printf("3. RL\n");
    choice = prompt_number("Image type number: ", 1, 3);

    switch (choice) {
    case 1:
        return IMAGE_TYPE_RK;
    case 2:
        return IMAGE_TYPE_RH;
    case 3:
    default:
        return IMAGE_TYPE_RL;
    }
}

static const uint16_t rl_bootstrap[] = {
    0012701, 0174400,
    0012761, 0000013, 0000004,
    0012711, 0000004,
    0105711,
    0100376,
    0005061, 0000002,
    0005061, 0000004,
    0012761, 0177400, 0000006,
    0012711, 0000014,
    0105711,
    0100376,
    0005007
};

static int prompt_trace_mode(void)
{
    printf("Boot trace:\n");
    printf("0. off\n");
    printf("1. on\n");
    return prompt_number("Trace mode number: ", 0, 1);
}

static const char *cpu_model_name(byte model)
{
    switch (model) {
    case K1801VM1:
        return "k1801vm1";
    case K1801VM1G:
        return "k1801vm1g";
    case K1801VM2:
        return "k1801vm2";
    case K1806VM2:
        return "k1806vm2";
    case DCJ11:
        return "dcj11";
    default:
        return "unknown";
    }
}

static byte prompt_cpu_model(int mode_choice)
{
    int choice;

    if (mode_choice == 2) {
        printf("Select CPU for pdp11/84:\n");
        printf("1. dcj11 (DEC 11/84)\n");
    } else {
        printf("Select CPU for lsi11:\n");
        printf("1. dcj11 (11/03-compatible)\n");
    }
    printf("2. k1801vm1\n");
    printf("3. k1801vm1g\n");
    printf("4. k1801vm2\n");
    printf("5. k1806vm2\n");

    choice = prompt_number("CPU number: ", 1, 5);
    switch (choice) {
    case 1:
        return DCJ11;
    case 2:
        return K1801VM1;
    case 3:
        return K1801VM1G;
    case 4:
        return K1801VM2;
    case 5:
    default:
        return K1806VM2;
    }
}

static void fatal_halt(const char *msg)
{
    printf("\nError: %s\n", msg);
    for (;;) {
        sleep_ms(1000);
    }
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

static int str_contains_casefold(const char *s, const char *needle)
{
    size_t ns;
    size_t nn;

    if (!s || !needle) {
        return 0;
    }
    ns = strlen(s);
    nn = strlen(needle);
    if (nn == 0 || ns < nn) {
        return 0;
    }

    for (size_t i = 0; i <= (ns - nn); i++) {
        size_t j = 0;
        while (j < nn && tolower((unsigned char)s[i + j]) ==
                tolower((unsigned char)needle[j])) {
            j++;
        }
        if (j == nn) {
            return 1;
        }
    }
    return 0;
}

static int has_image_extension(const char *name)
{
    static const char *exts[] = {"dsk",  "img",  "ima",  "iso", "rk05",
                                 "rk06", "rk07", "rl01", "rl02"
                                };
    const char *dot = strrchr(name, '.');
    size_t i;

    if (!dot || !dot[1]) {
        return 0;
    }
    dot++;

    for (i = 0; i < (sizeof(exts) / sizeof(exts[0])); i++) {
        if (str_eq_casefold(dot, exts[i])) {
            return 1;
        }
    }
    return 0;
}

static int image_matches_type(const char *name, image_type_t type)
{
    const char *dot;
    int is_rl = 0;
    int is_rh = 0;

    dot = strrchr(name, '.');
    if (dot && dot[1]) {
        const char *ext = dot + 1;
        if (str_eq_casefold(ext, "rl01") || str_eq_casefold(ext, "rl02")) {
            is_rl = 1;
        }
        if (str_eq_casefold(ext, "rk06") || str_eq_casefold(ext, "rk07")) {
            is_rh = 1;
        }
    }

    if (str_contains_casefold(name, "rl")) {
        is_rl = 1;
    }
    if (str_contains_casefold(name, "rk06") ||
            str_contains_casefold(name, "rk07") ||
            str_contains_casefold(name, "rh")) {
        is_rh = 1;
    }

    switch (type) {
    case IMAGE_TYPE_ANY:
        return 1;
    case IMAGE_TYPE_RK:
        return (!is_rl && !is_rh) ? 1 : 0;
    case IMAGE_TYPE_RH:
        /* RH images are often named as generic .dsk without rk06/rk07/rh marker. */
        return is_rl ? 0 : 1;
    case IMAGE_TYPE_RL:
        return is_rl ? 1 : 0;
    default:
        return 0;
    }
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
            tight_loop_contents();
            continue;
        }

        if (c == '\r' || c == '\n') {
            putchar('\n');
            break;
        }

        if ((c == 0x08 || c == 0x7f) && n > 0) {
            n--;
            printf("\b \b");
            continue;
        }

        if (c < 32 || c > 126) {
            continue;
        }

        if ((n + 1) < buf_len) {
            buf[n++] = (char)c;
            putchar(c);
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

static int list_images(image_entry_t *images, int max_images,
                       image_type_t type)
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
        if (fno.fname[0] == '_' && fno.fname[1] == 0) {
            continue;
        }
        if (fno.fname[0] == '.' && fno.fname[1] == '_') {
            continue;
        }
        if (!has_image_extension(fno.fname)) {
            continue;
        }
        if (!image_matches_type(fno.fname, type)) {
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

    if (count == 0) {
        return 0;
    }

    if (count > max_images) {
        printf("Found more than %d images, showing first %d.\n", max_images,
               max_images);
        count = max_images;
    }

    if (type == IMAGE_TYPE_ANY) {
        printf("Disk images on SD:\n");
    } else {
        printf("%s images on SD:\n", image_type_name(type));
    }
    for (int i = 0; i < count; i++) {
        printf("%d. %s\n", i + 1, images[i].name);
    }

    return count;
}

static int load_boot_block(const char *image_path, int trace_boot)
{
    FIL fil;
    uint8_t buf[RT11_BOOT_BLOCK_BYTES];
    uint8_t *ram0 = bus_ram_ptr(0);
    FRESULT fr;
    UINT br = 0;
    int all_zero = 1;
    int block_num = 0;

    if (!ram0 || !bus_range_is_ram(0, RT11_BOOT_BLOCK_BYTES)) {
        return -1;
    }

    fr = f_open(&fil, image_path, FA_READ);
    if (fr != FR_OK) {
        return -1;
    }

    fr = f_read(&fil, buf, RT11_BOOT_BLOCK_BYTES, &br);
    block_num = 1;
    if (fr == FR_OK && br == RT11_BOOT_BLOCK_BYTES) {
        all_zero = 1;
        for (size_t i = 0; i < RT11_BOOT_BLOCK_BYTES; i++) {
            if (buf[i] != 0) {
                all_zero = 0;
                break;
            }
        }
    }

    if (fr != FR_OK || br != RT11_BOOT_BLOCK_BYTES || all_zero) {
        fr = f_lseek(&fil, RT11_BOOT_BLOCK_BYTES);
        if (fr == FR_OK) {
            br = 0;
            fr = f_read(&fil, buf, RT11_BOOT_BLOCK_BYTES, &br);
            block_num = 2;
        }

        all_zero = 1;
        if (fr == FR_OK && br == RT11_BOOT_BLOCK_BYTES) {
            for (size_t i = 0; i < RT11_BOOT_BLOCK_BYTES; i++) {
                if (buf[i] != 0) {
                    all_zero = 0;
                    break;
                }
            }
        }
    }

    f_close(&fil);

    if (fr != FR_OK || br != RT11_BOOT_BLOCK_BYTES || all_zero) {
        return -1;
    }

    memcpy(ram0, buf, RT11_BOOT_BLOCK_BYTES);
    if (trace_boot) {
        printf("BOOT: block=%d bytes=%u first_words=%06o %06o %06o %06o\n",
               block_num, (unsigned)br,
               (unsigned)(buf[0] | ((unsigned)buf[1] << 8)),
               (unsigned)(buf[2] | ((unsigned)buf[3] << 8)),
               (unsigned)(buf[4] | ((unsigned)buf[5] << 8)),
               (unsigned)(buf[6] | ((unsigned)buf[7] << 8)));
    }
    return 0;
}

/* ---- Core 1: CPU emulation loop ---- */
static void __not_in_flash_func(core1_entry)(void)
{
    regs *r = (regs *)(uintptr_t)multicore_fifo_pop_blocking();
    int trace_boot = (int)multicore_fifo_pop_blocking();
    unsigned long steps = 0;

    for (;;) {
        int step_chunk = trace_boot ? 1 : 64;
        for (int i = 0; i < step_chunk; i++) {
            if (trace_boot && steps < 2000) {
                char dbuf[128];
                word pc = r->r[7];
                word tmp = pc;
                disas(r, &tmp, dbuf);
                printf("%06o %s\n", pc, dbuf);
            }
            core_step(r);
            steps++;
            if (r->fAbort) {
                break;
            }
        }
        if (r->fAbort) {
            r->fAbort = 0;
        }
    }
}

int main(void)
{
    regs r;
    image_entry_t images[MAX_IMAGES];
    char cfg_err[160] = {0};
    int mode_choice;
    byte cpu_model;
    image_type_t image_type;
    int trace_boot;
    int image_count;
    int image_choice;
    const char *mode_name;

    stdio_init_all();
    sleep_ms(2000);

    printf("\nPico LSI11 emulator\n");
    prompt_and_set_clock();
#if defined(LSI11_TARGET_1184)
    mode_choice = 2;
    printf("Fixed mode: pdp11/84\n");
#elif defined(LSI11_TARGET_1104)
    mode_choice = 1;
    printf("Fixed mode: lsi11\n");
#else
    printf("Select operating mode:\n");
    printf("1. lsi11\n");
    printf("2. pdp11/84\n");
    mode_choice = prompt_number("Mode number: ", 1, 2);
#endif

    if (mode_choice == 2) {
        if (lsi11_machine_configure(LSI11_MACHINE_1184, 128, cfg_err,
                                    sizeof(cfg_err)) != 0) {
            fatal_halt(cfg_err);
        }
        mode_name = "pdp11/84";
    } else {
        if (lsi11_machine_configure(LSI11_MACHINE_1104, 0, cfg_err,
                                    sizeof(cfg_err)) != 0) {
            fatal_halt(cfg_err);
        }
        mode_name = "lsi11";
    }

    cpu_model = prompt_cpu_model(mode_choice);
    if (cpu_model == K1801VM1 || cpu_model == K1801VM1G) {
        if (lsi11_set_device_enabled("vm1sel", 1, cfg_err, sizeof(cfg_err)) != 0 ||
                lsi11_set_device_enabled("vm1sav", 1, cfg_err, sizeof(cfg_err)) != 0) {
            fatal_halt(cfg_err);
        }
    } else {
        if (lsi11_set_device_enabled("vm1sel", 0, cfg_err, sizeof(cfg_err)) != 0 ||
                lsi11_set_device_enabled("vm1sav", 0, cfg_err, sizeof(cfg_err)) != 0) {
            fatal_halt(cfg_err);
        }
    }

    while (mount_sd_card() != 0) {
        char line[8];
        printf("SD is not initialized. Check power/wiring and press Enter to "
               "retry...\n");
        read_line(line, sizeof(line));
    }

    image_count = list_images(images, MAX_IMAGES, IMAGE_TYPE_ANY);
    if (image_count < 0) {
        fatal_halt("error reading SD directory");
    }
    if (image_count == 0) {
        fatal_halt("no disk images found on SD");
    }

    image_choice =
        prompt_number("Enter disk number to boot: ", 1, image_count) - 1;
    image_type = prompt_image_type();

    printf("Duplicate output to ST7565 display:\n");
    printf("0. no\n");
    printf("1. yes\n");
    if (prompt_number("Option [0/1]: ", 0, 1)) {
        term_display_init();
    }

    trace_boot = prompt_trace_mode();

    printf("Mode: %s\n", mode_name);
    printf("CPU: %s\n", cpu_model_name(cpu_model));
    printf("Image type: %s\n", image_type_name(image_type));
    printf("Disk: %s\n", images[image_choice].name);

    memset(&r, 0, sizeof(r));
    r.model = cpu_model;

    dl11_set_8bit(0);
    lsi11_hw_connect(&r);

    if (r.init(&r) != 0) {
        fatal_halt("device initialization failed");
    }
    lsi11_set_trace_irq(trace_boot ? 1 : 0);
    lsi11_set_trace_nxm(trace_boot ? 1 : 0);
    if (trace_boot) {
        //        rk11_set_debug(1);
        //        rh11_set_debug(1);
        //        rl11_set_debug(1);
    }

    if (image_type == IMAGE_TYPE_RK &&
            rk11_open_image(images[image_choice].path) != 0) {
        r.fini(&r);
        fatal_halt("failed to open selected RK image");
    }
    if (image_type == IMAGE_TYPE_RH &&
            rh11_open_image(images[image_choice].path) != 0) {
        r.fini(&r);
        fatal_halt("failed to open selected RH image");
    }
    if (image_type == IMAGE_TYPE_RL &&
            rl11_open_image_typed(images[image_choice].path, RL11_TYPE_AUTO) != 0) {
        r.fini(&r);
        fatal_halt("failed to open selected RL image");
    }

    r.ram_fast_size = (uint32_t)bus_ram_bytes();
    core_reset(&r);
    sr_set(0);

    if (image_type == IMAGE_TYPE_RL) {
        uint8_t *ram = bus_ram_ptr(RL_BOOT_ADDR);
        size_t n = sizeof(rl_bootstrap);
        if (!ram || !bus_range_is_ram(RL_BOOT_ADDR, n)) {
            rk11_close_image();
            rh11_close_image();
            rl11_close_image();
            r.fini(&r);
            fatal_halt("failed to load RL bootstrap");
        }
        for (size_t i = 0; i < (sizeof(rl_bootstrap) / sizeof(rl_bootstrap[0]));
                i++) {
            uint16_t w = rl_bootstrap[i];
            ram[i * 2 + 0] = (uint8_t)(w & 000377);
            ram[i * 2 + 1] = (uint8_t)((w >> 8) & 000377);
        }
        r.r[7] = RL_BOOT_ENTRY;
    } else {
        if (load_boot_block(images[image_choice].path, trace_boot) != 0) {
            uint8_t *ram0 = bus_ram_ptr(0);
            int boot_rc = -1;
            if (ram0 && bus_range_is_ram(0, RT11_BOOT_BLOCK_BYTES)) {
                if (image_type == IMAGE_TYPE_RK) {
                    boot_rc = rk11_boot_copy(ram0, RT11_BOOT_BLOCK_BYTES);
                } else if (image_type == IMAGE_TYPE_RH) {
                    boot_rc = rh11_boot_copy(ram0, RT11_BOOT_BLOCK_BYTES);
                }
            }
            if (boot_rc != 0) {
                rk11_close_image();
                rh11_close_image();
                rl11_close_image();
                r.fini(&r);
                fatal_halt("failed to load boot block");
            }
        }
        r.r[7] = 000000;
    }

    // FIXME: No-address register for initial configuration
    if (r.model == K1801VM2 || r.model == K1806VM2) {
        r.SEL0 = 0;
        r.SEL0 = 0200; // Disable FIS trap by default
    }

    {
        const char *m =
            (lsi11_machine_current() == LSI11_MACHINE_1184) ? "pdp1184" : "lsi11";
        int rh11_on = lsi11_device_enabled("rh11");
        printf("CONFIG machine=%s cpu=%s ram_kb=%u dl11_alias=%d rh11=%d "
               "dev_dl=%d dev_kw=%d dev_lp=%d dev_rk=%d dev_rh=%d dev_rl=%d "
               "dev_sr=%d dev_vm1sel=%d dev_vm1sav=%d\n",
               m, cpu_model_name(cpu_model), lsi11_machine_ram_kb(),
               lsi11_dl11_alias(), rh11_on, lsi11_device_enabled("dl11"),
               lsi11_device_enabled("kw11"), lsi11_device_enabled("lp11"),
               lsi11_device_enabled("rk11"), lsi11_device_enabled("rh11"),
               lsi11_device_enabled("rl11"), lsi11_device_enabled("sr"),
               lsi11_device_enabled("vm1sel"), lsi11_device_enabled("vm1sav"));
    }

    printf("Starting emulator...\n");

    /* Initialize the I/O spinlock before launching core 1 */
    g_io_spinlock = spin_lock_init(IO_SPINLOCK_NUM);

    /* Launch CPU emulation on core 1 */
    multicore_launch_core1(core1_entry);

    /* Pass regs pointer and trace flag to core 1 via FIFO */
    multicore_fifo_push_blocking((uint32_t)(uintptr_t)&r);
    multicore_fifo_push_blocking((uint32_t)trace_boot);

    /* Core 0: peripheral polling loop */
    for (;;) {
        lsi11_poll_devices();
        sleep_us(100);
    }
}
