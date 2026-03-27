#include <SDL.h>

#include "../bk0010-01/bk_timing.h"
#include "../core/core.h"
#include "../core/disas.h"
#include "mk90_defs.h"
#include "mk90_machine.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

enum {
    MK90_MAX_TAP_EVENTS = 256,
    MK90_AUDIO_RATE     = 44100,
    MK90_AUDIO_BUFSIZE  = 512,
    MK90_AUDIO_QUEUE    = 256
};

typedef struct mk90_tap_event {
    word scan_code;
    int frame;
    int hold_frames;
    int pressed;
    int released;
} mk90_tap_event;

typedef struct mk90_audio_state {
    int sample_rate;
    int level;
    double sample_us;
    double half_period_us;
    double half_period_left_us;
    uint32_t half_cycles_left;
    word queued_divider[MK90_AUDIO_QUEUE];
    uint32_t queued_cycles[MK90_AUDIO_QUEUE];
    unsigned head;
    unsigned count;
} mk90_audio_state;

static void mk90_audio_push(mk90_audio_state *st, word divider, uint32_t cycles)
{
    unsigned tail;

    if (!st || divider == 0u || cycles == 0u) {
        return;
    }

    if (st->count != 0u) {
        tail = (st->head + st->count - 1u) % MK90_AUDIO_QUEUE;
        if (st->queued_divider[tail] == divider) {
            st->queued_cycles[tail] += cycles;
            return;
        }
    }

    if (st->count >= MK90_AUDIO_QUEUE) {
        tail = (st->head + st->count - 1u) % MK90_AUDIO_QUEUE;
        st->queued_cycles[tail] += cycles;
        return;
    }

    tail = (st->head + st->count) % MK90_AUDIO_QUEUE;
    st->queued_divider[tail] = divider;
    st->queued_cycles[tail] = cycles;
    st->count++;
}

static void mk90_audio_callback(void *userdata, Uint8 *stream, int len)
{
    mk90_audio_state *st = (mk90_audio_state *)userdata;
    int16_t *out = (int16_t *)stream;
    int samples = len / (int)sizeof(int16_t);
    double sample_us = st->sample_us;

    for (int i = 0; i < samples; i++) {
        double left = sample_us;
        double acc = 0.0;

        while (left > 0.0) {
            double span;

            if (st->half_cycles_left == 0u) {
                if (st->count == 0u) {
                    break;
                }
                st->half_period_us = (double)st->queued_divider[st->head] * 0.625;
                st->half_period_left_us = st->half_period_us;
                st->half_cycles_left = st->queued_cycles[st->head] * 2u;
                st->head = (st->head + 1u) % MK90_AUDIO_QUEUE;
                st->count--;
                st->level = 1;
            }

            span = st->half_period_left_us;
            if (span > left) {
                span = left;
            }
            acc += (st->level ? span : -span);
            left -= span;
            st->half_period_left_us -= span;

            if (st->half_period_left_us <= 1e-9) {
                st->level ^= 1;
                st->half_cycles_left--;
                if (st->half_cycles_left == 0u) {
                    st->level = 0;
                } else {
                    st->half_period_left_us = st->half_period_us;
                }
            }
        }

        out[i] = (int16_t)(8000.0 * (acc / sample_us));
    }
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [--rom <path>] [--romt <path>] [--smp0 <path>] [--smp1 <path>]\n"
            "          [--steps-per-frame <n>] [--frames <n>] [--scale <n>]\n"
            "          [--headless] [--trace] [--tick-ms <n>] [--dump-pgm <path>] [--dump-vram <path>]\n"
            "          [--tap-key <octal>] [--tap-frame <n>] [--tap-hold <n>] [--tap <frame>:<octal>[:<hold>]]\n"
            "          [--type <text>] [--type-frame <n>] [--type-step <n>] [--type-hold <n>]\n"
            "Defaults: roms/rom.bin, roms/romt.bin, media/smp0.bin, media/smp1.bin\n",
            prog);
}

static const char *resolve_default_path(const char *path_a, const char *path_b)
{
    if (path_a && access(path_a, R_OK) == 0) {
        return path_a;
    }
    if (path_b && access(path_b, R_OK) == 0) {
        return path_b;
    }
    return NULL;
}

static word mk90_lookup_text_key(SDL_Keycode key)
{
    unsigned i;
    char ch;

    if (key < 0 || key > 127) {
        return 0;
    }

    ch = (char)key;
    if (ch >= 'a' && ch <= 'z') {
        ch = (char)toupper((unsigned char)ch);
    }

    for (i = 0; i < sizeof(mk90_letters) - 1u; i++) {
        if (mk90_letters[i] == ch) {
            return mk90_keytab[i + 3u];
        }
    }
    return 0;
}

static word mk90_lookup_host_char(char ch)
{
    if (ch == '\n' || ch == '\r') {
        return mk90_keytab[56];
    }
    if (ch == ' ') {
        return mk90_keytab[60];
    }
    if (ch == '\b') {
        return mk90_keytab[55];
    }
    return mk90_lookup_text_key((SDL_Keycode)(unsigned char)ch);
}

static word mk90_translate_special_key(SDL_Scancode scancode)
{
    switch (scancode) {
    case SDL_SCANCODE_TAB:
    case SDL_SCANCODE_F6:
    case SDL_SCANCODE_LCTRL:
    case SDL_SCANCODE_RCTRL:
        return mk90_keytab[49];
    case SDL_SCANCODE_UP:
        return mk90_keytab[50];
    case SDL_SCANCODE_LEFT:
        return mk90_keytab[51];
    case SDL_SCANCODE_RIGHT:
        return mk90_keytab[54];
    case SDL_SCANCODE_BACKSPACE:
    case SDL_SCANCODE_DELETE:
        return mk90_keytab[55];
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_RETURN2:
    case SDL_SCANCODE_KP_ENTER:
        return mk90_keytab[56];
    case SDL_SCANCODE_HOME:
    case SDL_SCANCODE_F7:
        return mk90_keytab[57];
    case SDL_SCANCODE_DOWN:
        return mk90_keytab[58];
    case SDL_SCANCODE_END:
    case SDL_SCANCODE_PAGEDOWN:
        return mk90_keytab[59];
    case SDL_SCANCODE_SPACE:
        return mk90_keytab[60];
    case SDL_SCANCODE_INSERT:
    case SDL_SCANCODE_PAGEUP:
        return mk90_keytab[61];
    case SDL_SCANCODE_LALT:
    case SDL_SCANCODE_RALT:
    case SDL_SCANCODE_F8:
        return mk90_keytab[62];
    case SDL_SCANCODE_LSHIFT:
    case SDL_SCANCODE_RSHIFT:
    case SDL_SCANCODE_F9:
        return mk90_keytab[63];
    default:
        return 0;
    }
}

static word mk90_translate_numpad_key(SDL_Scancode scancode)
{
    switch (scancode) {
    case SDL_SCANCODE_KP_1:
        return mk90_keytab[3];
    case SDL_SCANCODE_KP_2:
        return mk90_keytab[4];
    case SDL_SCANCODE_KP_3:
        return mk90_keytab[5];
    case SDL_SCANCODE_KP_4:
        return mk90_keytab[6];
    case SDL_SCANCODE_KP_5:
        return mk90_keytab[7];
    case SDL_SCANCODE_KP_6:
        return mk90_keytab[10];
    case SDL_SCANCODE_KP_7:
        return mk90_keytab[11];
    case SDL_SCANCODE_KP_8:
        return mk90_keytab[12];
    case SDL_SCANCODE_KP_9:
        return mk90_keytab[13];
    case SDL_SCANCODE_KP_0:
        return mk90_keytab[14];
    case SDL_SCANCODE_KP_DIVIDE:
        return mk90_keytab[15];
    case SDL_SCANCODE_KP_MINUS:
        return mk90_keytab[16];
    case SDL_SCANCODE_KP_PERIOD:
        return mk90_keytab[53];
    default:
        return 0;
    }
}

static word mk90_translate_pc_printable_key(SDL_Scancode scancode)
{
    switch (scancode) {
    case SDL_SCANCODE_GRAVE:
        return mk90_keytab[47]; /* @ */
    case SDL_SCANCODE_1:
        return mk90_keytab[3];
    case SDL_SCANCODE_2:
        return mk90_keytab[4];
    case SDL_SCANCODE_3:
        return mk90_keytab[5];
    case SDL_SCANCODE_4:
        return mk90_keytab[6];
    case SDL_SCANCODE_5:
        return mk90_keytab[7];
    case SDL_SCANCODE_6:
        return mk90_keytab[10];
    case SDL_SCANCODE_7:
        return mk90_keytab[11];
    case SDL_SCANCODE_8:
        return mk90_keytab[12];
    case SDL_SCANCODE_9:
        return mk90_keytab[13];
    case SDL_SCANCODE_0:
        return mk90_keytab[14];
    case SDL_SCANCODE_MINUS:
        return mk90_keytab[16];
    case SDL_SCANCODE_EQUALS:
        return mk90_keytab[40]; /* ^ */
    case SDL_SCANCODE_Q:
        return mk90_keytab[48];
    case SDL_SCANCODE_W:
        return mk90_keytab[19];
    case SDL_SCANCODE_E:
        return mk90_keytab[22];
    case SDL_SCANCODE_R:
        return mk90_keytab[33];
    case SDL_SCANCODE_T:
        return mk90_keytab[35];
    case SDL_SCANCODE_Y:
        return mk90_keytab[44];
    case SDL_SCANCODE_U:
        return mk90_keytab[36];
    case SDL_SCANCODE_I:
        return mk90_keytab[25];
    case SDL_SCANCODE_O:
        return mk90_keytab[31];
    case SDL_SCANCODE_P:
        return mk90_keytab[32];
    case SDL_SCANCODE_LEFTBRACKET:
        return mk90_keytab[41];
    case SDL_SCANCODE_RIGHTBRACKET:
        return mk90_keytab[42];
    case SDL_SCANCODE_BACKSLASH:
        return mk90_keytab[46];
    case SDL_SCANCODE_A:
        return mk90_keytab[17];
    case SDL_SCANCODE_S:
        return mk90_keytab[34];
    case SDL_SCANCODE_D:
        return mk90_keytab[21];
    case SDL_SCANCODE_F:
        return mk90_keytab[37];
    case SDL_SCANCODE_G:
        return mk90_keytab[20];
    case SDL_SCANCODE_H:
        return mk90_keytab[38];
    case SDL_SCANCODE_J:
        return mk90_keytab[26];
    case SDL_SCANCODE_K:
        return mk90_keytab[27];
    case SDL_SCANCODE_L:
        return mk90_keytab[28];
    case SDL_SCANCODE_SEMICOLON:
        return mk90_keytab[8];
    case SDL_SCANCODE_APOSTROPHE:
        return mk90_keytab[9];
    case SDL_SCANCODE_Z:
        return mk90_keytab[24];
    case SDL_SCANCODE_X:
        return mk90_keytab[43];
    case SDL_SCANCODE_C:
        return mk90_keytab[39];
    case SDL_SCANCODE_V:
        return mk90_keytab[23];
    case SDL_SCANCODE_B:
        return mk90_keytab[18];
    case SDL_SCANCODE_N:
        return mk90_keytab[30];
    case SDL_SCANCODE_M:
        return mk90_keytab[29];
    case SDL_SCANCODE_COMMA:
        return mk90_keytab[52];
    case SDL_SCANCODE_PERIOD:
        return mk90_keytab[53];
    case SDL_SCANCODE_SLASH:
        return mk90_keytab[15];
    default:
        return 0;
    }
}

static word mk90_translate_key(SDL_Scancode scancode, SDL_Keycode key)
{
    word scan_code = mk90_translate_special_key(scancode);

    if (scan_code != 0u) {
        return scan_code;
    }

    scan_code = mk90_translate_numpad_key(scancode);
    if (scan_code != 0u) {
        return scan_code;
    }

    scan_code = mk90_translate_pc_printable_key(scancode);
    if (scan_code != 0u) {
        return scan_code;
    }

    return mk90_lookup_text_key(key);
}

static int mk90_save_pgm(const char *path, const uint32_t *pixels)
{
    FILE *fp;
    size_t count;

    if (!path || !pixels) {
        return -1;
    }

    fp = fopen(path, "wb");
    if (!fp) {
        return -1;
    }

    fprintf(fp, "P5\n%d %d\n255\n", MK90_SCREEN_WIDTH, MK90_SCREEN_HEIGHT);
    for (count = 0; count < (size_t)MK90_SCREEN_WIDTH * MK90_SCREEN_HEIGHT; count++) {
        byte value = (pixels[count] & 0x00FFFFFFu) == 0u ? 0u : 255u;

        if (fwrite(&value, 1, 1, fp) != 1) {
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);
    return 0;
}

static int mk90_save_vram(const char *path)
{
    FILE *fp;
    word base;
    size_t i;

    if (!path) {
        return -1;
    }

    fp = fopen(path, "wb");
    if (!fp) {
        return -1;
    }

    base = mk90_machine_lcd_base();
    for (i = 0; i < MK90_SCREEN_BYTES; i++) {
        byte value = mk90_machine_ram_peek((word)(base + i));

        if (fwrite(&value, 1, 1, fp) != 1) {
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);
    return 0;
}

static int parse_octal_word(const char *text, word *value)
{
    char *end = NULL;
    unsigned long parsed;

    if (!text || !value || text[0] == '\0') {
        return -1;
    }

    parsed = strtoul(text, &end, 8);
    if (!end || end[0] != '\0' || parsed > 0177777ul) {
        return -1;
    }

    *value = (word)parsed;
    return 0;
}

static int parse_tap_spec(const char *text, mk90_tap_event *event)
{
    const char *sep;
    const char *hold_sep;
    char frame_buf[32];
    char scan_buf[32];
    size_t frame_len;
    size_t scan_len;
    char *end = NULL;
    long frame_value;
    long hold_value = 1;

    if (!text || !event) {
        return -1;
    }

    sep = strchr(text, ':');
    if (!sep) {
        return -1;
    }
    hold_sep = strchr(sep + 1, ':');
    frame_len = (size_t)(sep - text);
    if (frame_len == 0 || frame_len >= sizeof(frame_buf)) {
        return -1;
    }

    memcpy(frame_buf, text, frame_len);
    frame_buf[frame_len] = '\0';
    frame_value = strtol(frame_buf, &end, 10);
    if (!end || end[0] != '\0' || frame_value < 0 || frame_value > 1000000L) {
        return -1;
    }

    scan_len = hold_sep ? (size_t)(hold_sep - (sep + 1)) : strlen(sep + 1);
    if (scan_len == 0 || scan_len >= sizeof(scan_buf)) {
        return -1;
    }
    memcpy(scan_buf, sep + 1, scan_len);
    scan_buf[scan_len] = '\0';
    if (parse_octal_word(scan_buf, &event->scan_code) != 0) {
        return -1;
    }

    if (hold_sep) {
        hold_value = strtol(hold_sep + 1, &end, 10);
        if (!end || end[0] != '\0' || hold_value < 1 || hold_value > 1000000L) {
            return -1;
        }
    }

    if (event->scan_code == 0u) {
        return -1;
    }

    event->frame = (int)frame_value;
    event->hold_frames = (int)hold_value;
    event->pressed = 0;
    event->released = 0;
    return 0;
}

static int append_tap_event(mk90_tap_event *events, int *count, int max_count,
                            int frame, int hold_frames, word scan_code)
{
    if (!events || !count || *count < 0 || *count >= max_count ||
        hold_frames < 1 || scan_code == 0u) {
        return -1;
    }

    events[*count].scan_code = scan_code;
    events[*count].frame = frame;
    events[*count].hold_frames = hold_frames;
    events[*count].pressed = 0;
    events[*count].released = 0;
    (*count)++;
    return 0;
}

static int append_type_text(mk90_tap_event *events, int *count, int max_count,
                            int *frame, int frame_step, int hold_frames,
                            const char *text)
{
    const unsigned char *p = (const unsigned char *)text;

    if (!events || !count || !frame || !text || frame_step < 1 || hold_frames < 1) {
        return -1;
    }

    while (*p) {
        word scan_code;
        unsigned char ch = *p++;

        if (ch == '\\' && *p) {
            switch (*p++) {
            case 'n':
            case 'r':
                ch = '\n';
                break;
            case 'b':
                ch = '\b';
                break;
            case '\\':
                ch = '\\';
                break;
            default:
                return -1;
            }
        }

        scan_code = mk90_lookup_host_char((char)ch);
        if (scan_code == 0u ||
            append_tap_event(events, count, max_count, *frame, hold_frames,
                             scan_code) != 0) {
            return -1;
        }
        *frame += frame_step;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    const char *rom_path = NULL;
    const char *romt_path = NULL;
    const char *smp0_path = NULL;
    const char *smp1_path = NULL;
    const char *default_rom;
    const char *default_romt;
    const char *default_smp0;
    const char *default_smp1;
    const char *dump_pgm_path = NULL;
    const char *dump_vram_path = NULL;
    mk90_tap_event tap_events[MK90_MAX_TAP_EVENTS];
    regs r;
    char err[256];
    word tap_scan_code = 0;
    int headless = 0;
    int trace = 0;
    int steps_per_frame = 2000;
    int frame_limit = -1;
    int scale = 4;
    int tick_ms = -1;
    int tap_frame = 120;
    int tap_hold = 1;
    int type_frame = 120;
    int type_step = 4;
    int type_hold = 1;
    int tap_count = 0;
    int frame_count = 0;
    uint32_t last_ticks;
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    uint32_t *pixels = NULL;
    SDL_Scancode active_host_key = SDL_SCANCODE_UNKNOWN;
    SDL_AudioDeviceID audio_dev = 0;
    mk90_audio_state audio_state = { MK90_AUDIO_RATE, 0,
                                     1000000.0 / (double)MK90_AUDIO_RATE,
                                     0.0, 0.0, 0u,
                                     { 0 }, { 0 }, 0u, 0u };
    int quit = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--rom") && i + 1 < argc) {
            rom_path = argv[++i];
        } else if (!strcmp(argv[i], "--romt") && i + 1 < argc) {
            romt_path = argv[++i];
        } else if (!strcmp(argv[i], "--smp0") && i + 1 < argc) {
            smp0_path = argv[++i];
        } else if (!strcmp(argv[i], "--smp1") && i + 1 < argc) {
            smp1_path = argv[++i];
        } else if (!strcmp(argv[i], "--steps-per-frame") && i + 1 < argc) {
            steps_per_frame = atoi(argv[++i]);
            if (steps_per_frame < 1) {
                fprintf(stderr, "mk90: --steps-per-frame must be >= 1\n");
                return 1;
            }
        } else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            frame_limit = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--scale") && i + 1 < argc) {
            scale = atoi(argv[++i]);
            if (scale < 1) {
                fprintf(stderr, "mk90: --scale must be >= 1\n");
                return 1;
            }
        } else if (!strcmp(argv[i], "--headless")) {
            headless = 1;
        } else if (!strcmp(argv[i], "--trace")) {
            trace = 1;
        } else if (!strcmp(argv[i], "--tick-ms") && i + 1 < argc) {
            tick_ms = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--dump-pgm") && i + 1 < argc) {
            dump_pgm_path = argv[++i];
        } else if (!strcmp(argv[i], "--dump-vram") && i + 1 < argc) {
            dump_vram_path = argv[++i];
        } else if (!strcmp(argv[i], "--tap-key") && i + 1 < argc) {
            if (parse_octal_word(argv[++i], &tap_scan_code) != 0) {
                fprintf(stderr, "mk90: invalid octal key scan code\n");
                return 1;
            }
        } else if (!strcmp(argv[i], "--tap-frame") && i + 1 < argc) {
            tap_frame = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--tap-hold") && i + 1 < argc) {
            tap_hold = atoi(argv[++i]);
            if (tap_hold < 1) {
                fprintf(stderr, "mk90: --tap-hold must be >= 1\n");
                return 1;
            }
        } else if (!strcmp(argv[i], "--tap") && i + 1 < argc) {
            if (tap_count >= (int)(sizeof(tap_events) / sizeof(tap_events[0])) ||
                parse_tap_spec(argv[++i], &tap_events[tap_count]) != 0) {
                fprintf(stderr, "mk90: invalid tap spec, expected <frame>:<octal>[:<hold>]\n");
                return 1;
            }
            tap_count++;
        } else if (!strcmp(argv[i], "--type-frame") && i + 1 < argc) {
            type_frame = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--type-step") && i + 1 < argc) {
            type_step = atoi(argv[++i]);
            if (type_step < 1) {
                fprintf(stderr, "mk90: --type-step must be >= 1\n");
                return 1;
            }
        } else if (!strcmp(argv[i], "--type-hold") && i + 1 < argc) {
            type_hold = atoi(argv[++i]);
            if (type_hold < 1) {
                fprintf(stderr, "mk90: --type-hold must be >= 1\n");
                return 1;
            }
        } else if (!strcmp(argv[i], "--type") && i + 1 < argc) {
            if (append_type_text(tap_events, &tap_count,
                                 (int)(sizeof(tap_events) / sizeof(tap_events[0])),
                                 &type_frame, type_step, type_hold,
                                 argv[++i]) != 0) {
                fprintf(stderr, "mk90: invalid --type text or too many tap events\n");
                return 1;
            }
        } else if (!strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    default_rom = resolve_default_path("roms/rom.bin",
                                       "mk90/roms/rom.bin");
    default_romt = resolve_default_path("roms/romt.bin",
                                        "mk90/roms/romt.bin");
    default_smp0 = resolve_default_path("media/smp0.bin",
                                        "mk90/media/smp0.bin");
    default_smp1 = resolve_default_path("media/smp1.bin",
                                        "mk90/media/smp1.bin");

    if (!rom_path) {
        rom_path = default_rom;
    }
    if (!romt_path) {
        romt_path = default_romt;
    }
    if (!smp0_path) {
        smp0_path = default_smp0;
    }
    if (!smp1_path) {
        smp1_path = default_smp1;
    }

    if (!rom_path) {
        fprintf(stderr,
                "mk90: no ROM image found; expected roms/rom.bin or use --rom <path>\n");
        return 1;
    }
    if (tap_scan_code != 0u) {
        if (append_tap_event(tap_events, &tap_count,
                             (int)(sizeof(tap_events) / sizeof(tap_events[0])),
                             tap_frame, tap_hold, tap_scan_code) != 0) {
            fprintf(stderr, "mk90: too many tap events\n");
            return 1;
        }
    }

    memset(&r, 0, sizeof(r));
    r.model = K1806VM2;

    mk90_machine_connect(&r);
    mk90_machine_set_trace(trace);
    if (mk90_machine_load_images(rom_path, romt_path, smp0_path, smp1_path,
                                 err, sizeof(err)) != 0) {
        fprintf(stderr, "mk90: %s\n", err);
        return 1;
    }
    if (core_init(&r) != 0) {
        fprintf(stderr, "mk90: core_init failed\n");
        return 1;
    }
    core_reset(&r);
    if (trace) {
        fprintf(stderr, "mk90: after reset PSW=%06o SEL0=%06o PC=%06o\n",
                r.psw, r.SEL0, r.r[7]);
    }

    if (!headless || dump_pgm_path) {
        pixels = (uint32_t *)calloc((size_t)MK90_SCREEN_WIDTH * MK90_SCREEN_HEIGHT,
                                    sizeof(uint32_t));
        if (!pixels) {
            fprintf(stderr, "mk90: framebuffer allocation failed\n");
            core_fini(&r);
            return 1;
        }
    }

    if (!headless) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO) != 0) {
            fprintf(stderr, "mk90: SDL_Init failed: %s\n", SDL_GetError());
            free(pixels);
            core_fini(&r);
            return 1;
        }

        window = SDL_CreateWindow("MK-90",
                                  SDL_WINDOWPOS_CENTERED,
                                  SDL_WINDOWPOS_CENTERED,
                                  MK90_SCREEN_WIDTH * scale,
                                  MK90_SCREEN_HEIGHT * scale,
                                  SDL_WINDOW_SHOWN);
        if (!window) {
            fprintf(stderr, "mk90: SDL_CreateWindow failed: %s\n", SDL_GetError());
            free(pixels);
            core_fini(&r);
            SDL_Quit();
            return 1;
        }

        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        texture = SDL_CreateTexture(renderer,
                                    SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STREAMING,
                                    MK90_SCREEN_WIDTH,
                                    MK90_SCREEN_HEIGHT);
        if (!renderer || !texture) {
            fprintf(stderr, "mk90: SDL renderer setup failed\n");
            free(pixels);
            SDL_DestroyTexture(texture);
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            core_fini(&r);
            SDL_Quit();
            return 1;
        }

        {
            SDL_AudioSpec want;
            SDL_AudioSpec have;
            SDL_zero(want);
            want.freq     = MK90_AUDIO_RATE;
            want.format   = AUDIO_S16SYS;
            want.channels = 1;
            want.samples  = MK90_AUDIO_BUFSIZE;
            want.callback = mk90_audio_callback;
            want.userdata = &audio_state;
            audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
            if (audio_dev != 0) {
                audio_state.sample_rate = have.freq;
                audio_state.sample_us = 1000000.0 / (double)audio_state.sample_rate;
                SDL_PauseAudioDevice(audio_dev, 0);
            }
        }
    }

    last_ticks = SDL_GetTicks();
    while (!quit) {
        uint32_t frame_start = SDL_GetTicks();
        uint32_t elapsed_ms = frame_start - last_ticks;

        last_ticks = frame_start;
        if (tick_ms >= 0) {
            elapsed_ms = (uint32_t)tick_ms;
        } else if (elapsed_ms == 0u) {
            elapsed_ms = 1u;
        }

        for (int tap_index = 0; tap_index < tap_count; tap_index++) {
            if (tap_events[tap_index].pressed &&
                !tap_events[tap_index].released &&
                frame_count == tap_events[tap_index].frame +
                               tap_events[tap_index].hold_frames) {
                mk90_machine_key_release();
                tap_events[tap_index].released = 1;
            }
        }
        for (int tap_index = 0; tap_index < tap_count; tap_index++) {
            if (!tap_events[tap_index].pressed &&
                frame_count == tap_events[tap_index].frame) {
                mk90_machine_key_press(tap_events[tap_index].scan_code);
                tap_events[tap_index].pressed = 1;
            }
        }

        if (!headless) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) {
                    quit = 1;
                } else if (event.type == SDL_WINDOWEVENT &&
                           event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                    if (active_host_key != SDL_SCANCODE_UNKNOWN) {
                        mk90_machine_key_release();
                        active_host_key = SDL_SCANCODE_UNKNOWN;
                    }
                } else if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                    word scan_code = mk90_translate_key(event.key.keysym.scancode,
                                                        event.key.keysym.sym);
                    if (event.key.keysym.sym == SDLK_ESCAPE) {
                        quit = 1;
                    } else if (scan_code != 0u) {
                        if (active_host_key != SDL_SCANCODE_UNKNOWN &&
                            active_host_key != event.key.keysym.scancode) {
                            mk90_machine_key_release();
                        }
                        mk90_machine_key_press(scan_code);
                        active_host_key = event.key.keysym.scancode;
                    }
                } else if (event.type == SDL_KEYUP && !event.key.repeat &&
                           event.key.keysym.scancode == active_host_key) {
                    mk90_machine_key_release();
                    active_host_key = SDL_SCANCODE_UNKNOWN;
                }
            }
        }

        {
            for (int i = 0; i < steps_per_frame; i++) {
                unsigned cycles;

                if (trace) {
                    char text[256];
                    word pc = r.r[7];
                    word dis_pc = pc;

                    disas(&r, &dis_pc, text);
                    fprintf(stderr,
                            "%06o  %06o  %06o  %06o  %06o  %06o  %06o  %06o  %06o  %s\n",
                            r.psw,
                            r.r[0], r.r[1], r.r[2], r.r[3],
                            r.r[4], r.r[5], r.r[6], pc, text);
                }
                (void)core_step(&r);
                cycles = bk_timing_cycles(r.ir);
                if (cycles == 0u) {
                    cycles = 1u;
                }
                mk90_machine_step(cycles);
            }

            if (audio_dev != 0) {
                word divider;
                uint32_t cycles;

                SDL_LockAudioDevice(audio_dev);
                while (mk90_machine_pop_audio_segment(&divider, &cycles)) {
                    mk90_audio_push(&audio_state, divider, cycles);
                }
                SDL_UnlockAudioDevice(audio_dev);
            }
        }
        mk90_machine_tick_ms(elapsed_ms);

        if (!headless) {
            mk90_machine_render(pixels, MK90_SCREEN_WIDTH);
            SDL_UpdateTexture(texture, NULL, pixels,
                              MK90_SCREEN_WIDTH * (int)sizeof(uint32_t));
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, NULL, NULL);
            SDL_RenderPresent(renderer);
        } else if (dump_pgm_path) {
            mk90_machine_render(pixels, MK90_SCREEN_WIDTH);
        }

        frame_count++;
        if (frame_limit >= 0 && frame_count >= frame_limit) {
            quit = 1;
        }

        if (!headless) {
            uint32_t frame_ms = SDL_GetTicks() - frame_start;
            if (frame_ms < 16u) {
                SDL_Delay(16u - frame_ms);
            }
        }
    }

    if (dump_pgm_path) {
        mk90_machine_render(pixels, MK90_SCREEN_WIDTH);
        if (mk90_save_pgm(dump_pgm_path, pixels) != 0) {
            fprintf(stderr, "mk90: failed to save %s\n", dump_pgm_path);
        }
    }
    if (dump_vram_path && mk90_save_vram(dump_vram_path) != 0) {
        fprintf(stderr, "mk90: failed to save %s\n", dump_vram_path);
    }
    if (audio_dev != 0) {
        SDL_CloseAudioDevice(audio_dev);
    }
    free(pixels);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    if (!headless) {
        SDL_Quit();
    }
    core_fini(&r);
    return 0;
}
