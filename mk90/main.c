#include <SDL.h>

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

typedef struct mk90_tap_event {
    word scan_code;
    int frame;
    int pressed;
    int released;
} mk90_tap_event;

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [--rom <path>] [--romt <path>] [--smp0 <path>] [--smp1 <path>]\n"
            "          [--steps-per-frame <n>] [--frames <n>] [--scale <n>]\n"
            "          [--headless] [--trace] [--tick-ms <n>] [--dump-pgm <path>]\n"
            "          [--tap-key <octal>] [--tap-frame <n>] [--tap <frame>:<octal>]\n"
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

static word mk90_translate_key(SDL_Keycode key)
{
    switch (key) {
    case SDLK_UP:
        return mk90_keytab[50];
    case SDLK_LEFT:
        return mk90_keytab[51];
    case SDLK_RIGHT:
        return mk90_keytab[54];
    case SDLK_BACKSPACE:
        return mk90_keytab[55];
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return mk90_keytab[56];
    case SDLK_F7:
        return mk90_keytab[57];
    case SDLK_DOWN:
        return mk90_keytab[58];
    case SDLK_PAGEDOWN:
        return mk90_keytab[59];
    case SDLK_SPACE:
        return mk90_keytab[60];
    case SDLK_PAGEUP:
        return mk90_keytab[61];
    case SDLK_F8:
        return mk90_keytab[62];
    case SDLK_F9:
        return mk90_keytab[63];
    case SDLK_F6:
        return mk90_keytab[49];
    default:
        return mk90_lookup_text_key(key);
    }
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
    char frame_buf[32];
    size_t frame_len;
    char *end = NULL;
    long frame_value;

    if (!text || !event) {
        return -1;
    }

    sep = strchr(text, ':');
    if (!sep) {
        return -1;
    }
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
    if (parse_octal_word(sep + 1, &event->scan_code) != 0) {
        return -1;
    }

    event->frame = (int)frame_value;
    event->pressed = 0;
    event->released = 0;
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
    mk90_tap_event tap_events[32];
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
    int tap_count = 0;
    int frame_count = 0;
    uint32_t last_ticks;
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    uint32_t *pixels = NULL;
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
        } else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            frame_limit = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--scale") && i + 1 < argc) {
            scale = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--headless")) {
            headless = 1;
        } else if (!strcmp(argv[i], "--trace")) {
            trace = 1;
        } else if (!strcmp(argv[i], "--tick-ms") && i + 1 < argc) {
            tick_ms = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--dump-pgm") && i + 1 < argc) {
            dump_pgm_path = argv[++i];
        } else if (!strcmp(argv[i], "--tap-key") && i + 1 < argc) {
            if (parse_octal_word(argv[++i], &tap_scan_code) != 0) {
                fprintf(stderr, "mk90: invalid octal key scan code\n");
                return 1;
            }
        } else if (!strcmp(argv[i], "--tap-frame") && i + 1 < argc) {
            tap_frame = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--tap") && i + 1 < argc) {
            if (tap_count >= (int)(sizeof(tap_events) / sizeof(tap_events[0])) ||
                parse_tap_spec(argv[++i], &tap_events[tap_count]) != 0) {
                fprintf(stderr, "mk90: invalid tap spec, expected <frame>:<octal>\n");
                return 1;
            }
            tap_count++;
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
        if (tap_count >= (int)(sizeof(tap_events) / sizeof(tap_events[0]))) {
            fprintf(stderr, "mk90: too many tap events\n");
            return 1;
        }
        tap_events[tap_count].scan_code = tap_scan_code;
        tap_events[tap_count].frame = tap_frame;
        tap_events[tap_count].pressed = 0;
        tap_events[tap_count].released = 0;
        tap_count++;
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
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
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
    }

    last_ticks = SDL_GetTicks();
    while (!quit) {
        uint32_t now_ticks = SDL_GetTicks();
        uint32_t elapsed_ms = now_ticks - last_ticks;

        last_ticks = now_ticks;
        if (tick_ms >= 0) {
            elapsed_ms = (uint32_t)tick_ms;
        } else if (elapsed_ms == 0u) {
            elapsed_ms = 1u;
        }

        for (int tap_index = 0; tap_index < tap_count; tap_index++) {
            if (tap_events[tap_index].pressed &&
                !tap_events[tap_index].released &&
                frame_count == tap_events[tap_index].frame + 1) {
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
                } else if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                    word scan_code = mk90_translate_key(event.key.keysym.sym);
                    if (event.key.keysym.sym == SDLK_ESCAPE) {
                        quit = 1;
                    } else if (scan_code != 0u) {
                        mk90_machine_key_press(scan_code);
                    }
                } else if (event.type == SDL_KEYUP) {
                    mk90_machine_key_release();
                }
            }
        }

        for (int i = 0; i < steps_per_frame; i++) {
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
            SDL_Delay(16);
        }
    }

    if (dump_pgm_path) {
        mk90_machine_render(pixels, MK90_SCREEN_WIDTH);
        if (mk90_save_pgm(dump_pgm_path, pixels) != 0) {
            fprintf(stderr, "mk90: failed to save %s\n", dump_pgm_path);
        }
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
