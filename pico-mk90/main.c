#include "pico_mk90_display.h"
#include "pico_mk90_media.h"

#include "bk_timing.h"
#include "core.h"
#include "f_util.h"
#include "ff.h"
#include "hw_config.h"
#include "mk90_machine.h"
#include "sd_card.h"

#include "pico/stdlib.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef PICO_MK90_STEPS_PER_FRAME
#define PICO_MK90_STEPS_PER_FRAME 2000
#endif

#ifndef PICO_MK90_FRAME_US
#define PICO_MK90_FRAME_US 16667
#endif

#ifndef PICO_MK90_KEY_HOLD_FRAMES
#define PICO_MK90_KEY_HOLD_FRAMES 2
#endif

#ifndef PICO_MK90_KEY_GAP_FRAMES
#define PICO_MK90_KEY_GAP_FRAMES 1
#endif

#define KEY_QUEUE_SIZE 32u

static const word mk90_keytab[64] = {
    000000u, 000000u, 000000u,
    000043u, 000103u, 000143u, 000203u, 000243u, 000303u, 000343u,
    000047u, 000107u, 000147u, 000207u, 000247u, 000307u, 000347u,
    000013u, 000053u, 000113u, 000153u, 000213u, 000253u, 000313u, 000353u,
    000017u, 000057u, 000117u, 000157u, 000217u, 000257u, 000317u, 000357u,
    000023u, 000063u, 000123u, 000163u, 000223u, 000263u, 000323u, 000363u,
    000027u, 000067u, 000127u, 000167u, 000227u, 000267u, 000327u, 000367u,
    000033u, 000073u, 000133u, 000173u, 000233u, 000273u, 000333u, 000373u,
    000037u, 000077u, 000137u, 000177u, 000277u, 000337u, 000377u
};

static const char mk90_letters[] =
    "12345:;67890/-ABWGDEVZIJKLMNOPRSTUFHC^[]XY_\\@Qaaa,.";

typedef struct {
    word queue[KEY_QUEUE_SIZE];
    unsigned head;
    unsigned count;
    word active_scan;
    int hold_frames;
    int gap_frames;
    int esc_state;
    int esc_param;
    int skip_lf;
} input_state_t;

static void fatal_halt(const char *message)
{
    printf("pico-mk90: fatal: %s\n", message ? message : "unknown error");
    for (;;) {
        sleep_ms(1000);
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
            printf("SD: mounted as %s\n", sd->pcName);
            return 0;
        }
        printf("SD: f_mount attempt %d/5 failed: %s (%d)\n", attempt,
               FRESULT_str(fr), fr);
        f_unmount(sd->pcName);
        sleep_ms(250);
    }

    return -1;
}

static word lookup_text_key(int key)
{
    char ch;

    if (key < 0 || key > 127) {
        return 0;
    }

    ch = (char)key;
    if (ch >= 'a' && ch <= 'z') {
        ch = (char)toupper((unsigned char)ch);
    }

    for (unsigned i = 0; i < sizeof(mk90_letters) - 1u; i++) {
        if (mk90_letters[i] == ch) {
            return mk90_keytab[i + 3u];
        }
    }
    return 0;
}

static word lookup_stdin_key(int ch)
{
    if (ch == '\r' || ch == '\n') {
        return mk90_keytab[56];
    }
    if (ch == '\t') {
        return mk90_keytab[49];
    }
    if (ch == ' ') {
        return mk90_keytab[60];
    }
    if (ch == '\b' || ch == 0x7F) {
        return mk90_keytab[55];
    }
    return lookup_text_key(ch);
}

static word translate_escape(input_state_t *input, int ch)
{
    if (input->esc_state == 0) {
        if (ch == '\r') {
            input->skip_lf = 1;
            return mk90_keytab[56];
        }
        if (ch == '\n' && input->skip_lf) {
            input->skip_lf = 0;
            return 0;
        }
        input->skip_lf = 0;
        if (ch == 0x1B) {
            input->esc_state = 1;
            input->esc_param = 0;
            return 0;
        }
        return lookup_stdin_key(ch);
    }

    if (input->esc_state == 1) {
        if (ch == '[') {
            input->esc_state = 2;
            return 0;
        }
        input->esc_state = 0;
        return 0;
    }

    if (input->esc_state == 2) {
        if (ch >= '0' && ch <= '9') {
            input->esc_state = 3;
            input->esc_param = ch - '0';
            return 0;
        }

        input->esc_state = 0;
        switch (ch) {
        case 'A':
            return mk90_keytab[50];
        case 'B':
            return mk90_keytab[58];
        case 'C':
            return mk90_keytab[54];
        case 'D':
            return mk90_keytab[51];
        case 'H':
            return mk90_keytab[57];
        case 'F':
            return mk90_keytab[59];
        default:
            return 0;
        }
    }

    if (input->esc_state == 3) {
        input->esc_state = 0;
        if (ch != '~') {
            return 0;
        }
        switch (input->esc_param) {
        case 1:
        case 7:
            return mk90_keytab[57];
        case 2:
        case 5:
            return mk90_keytab[61];
        case 3:
            return mk90_keytab[55];
        case 4:
        case 6:
        case 8:
            return mk90_keytab[59];
        default:
            return 0;
        }
    }

    input->esc_state = 0;
    return 0;
}

static void input_push(input_state_t *input, word scan_code)
{
    unsigned tail;

    if (scan_code == 0 || input->count >= KEY_QUEUE_SIZE) {
        return;
    }

    tail = (input->head + input->count) % KEY_QUEUE_SIZE;
    input->queue[tail] = scan_code;
    input->count++;
}

static void input_poll_stdin(input_state_t *input)
{
    for (int i = 0; i < 64; i++) {
        int ch = getchar_timeout_us(0);
        word scan_code;

        if (ch == PICO_ERROR_TIMEOUT) {
            break;
        }

        scan_code = translate_escape(input, ch);
        if (scan_code != 0) {
            input_push(input, scan_code);
        }
    }
}

static void input_frame(input_state_t *input)
{
    if (input->active_scan != 0) {
        input->hold_frames--;
        if (input->hold_frames <= 0) {
            mk90_machine_key_release();
            input->active_scan = 0;
            input->gap_frames = PICO_MK90_KEY_GAP_FRAMES;
        }
        return;
    }

    if (input->gap_frames > 0) {
        input->gap_frames--;
        return;
    }

    if (input->count != 0) {
        word scan_code = input->queue[input->head];

        input->head = (input->head + 1u) % KEY_QUEUE_SIZE;
        input->count--;
        input->active_scan = scan_code;
        input->hold_frames = PICO_MK90_KEY_HOLD_FRAMES;
        mk90_machine_key_press(scan_code);
    }
}

static void drain_audio_queue(void)
{
    word divider;
    uint32_t cycles;

    while (mk90_machine_pop_audio_segment(&divider, &cycles)) {
        (void)divider;
        (void)cycles;
    }
}

int main(void)
{
    regs r;
    input_state_t input;
    char err[160] = {0};
    uint64_t last_frame_us;

    stdio_init_all();
    sleep_ms(2000);

    printf("\npico-mk90\n");
    printf("SD: SPI0 GP16/17/18/19, ST7565: SPI1 GP9/10/11/13/14/15\n");
    printf("Keyboard: USB stdio stdin\n");

    if (!pico_mk90_display_init()) {
        fatal_halt("display initialization failed");
    }

    while (mount_sd_card() != 0) {
        printf("SD is not initialized. Check power/wiring/card and retrying...\n");
        sleep_ms(1000);
    }

    memset(&r, 0, sizeof(r));
    memset(&input, 0, sizeof(input));
    r.model = K1806VM2;

    mk90_machine_connect(&r);
    mk90_machine_set_trace(0);
    if (pico_mk90_load_sd_images(err, sizeof(err)) != 0) {
        fatal_halt(err);
    }
    if (core_init(&r) != 0) {
        fatal_halt("core_init failed");
    }
    core_reset(&r);

    printf("MK90 started: %d steps/frame, %d us/frame\n",
           PICO_MK90_STEPS_PER_FRAME, PICO_MK90_FRAME_US);

    last_frame_us = time_us_64();
    for (;;) {
        const uint64_t frame_start_us = time_us_64();
        uint32_t elapsed_ms = (uint32_t)((frame_start_us - last_frame_us) / 1000u);

        last_frame_us = frame_start_us;
        if (elapsed_ms == 0u) {
            elapsed_ms = 1u;
        } else if (elapsed_ms > 250u) {
            elapsed_ms = 250u;
        }

        input_poll_stdin(&input);
        input_frame(&input);

        for (int i = 0; i < PICO_MK90_STEPS_PER_FRAME; i++) {
            unsigned cycles;

            (void)core_step(&r);
            cycles = bk_timing_cycles(r.ir);
            if (cycles == 0u) {
                cycles = 1u;
            }
            mk90_machine_step(cycles);
        }

        mk90_machine_tick_ms(elapsed_ms);
        drain_audio_queue();
        pico_mk90_display_update_from_machine();

        {
            const uint64_t used_us = time_us_64() - frame_start_us;

            if (used_us < (uint64_t)PICO_MK90_FRAME_US) {
                sleep_us((uint32_t)((uint64_t)PICO_MK90_FRAME_US - used_us));
            }
        }
    }
}
