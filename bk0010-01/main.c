#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Required for stub implementations */
#include <sys/time.h>
#include <unistd.h>

/* GUI backend selection */
#ifdef USE_SDL
    /* Full SDL2 support */
    #include <SDL.h>
#else
    /* Minimal SDL‑like stubs required for the X11 backend. Only the symbols
       used in the source are defined. */
    typedef void *SDL_Window;
    typedef void *SDL_Renderer;
    typedef void *SDL_Texture;
    typedef unsigned int Uint32;
    typedef unsigned long long Uint64;
    typedef unsigned char Uint8;
    typedef int SDL_AudioDeviceID;
    static Uint32 SDL_GetTicks(void) { struct timeval tv; gettimeofday(&tv, NULL); return (Uint32)(tv.tv_sec * 1000 + tv.tv_usec / 1000); }
    static void SDL_Delay(Uint32 ms) { usleep((useconds_t)ms * 1000); }
    static Uint64 SDL_GetPerformanceFrequency(void) { return 1000000ULL; }
    static Uint64 SDL_GetPerformanceCounter(void) { struct timeval tv; gettimeofday(&tv, NULL); return (Uint64)tv.tv_sec * 1000000ULL + tv.tv_usec; }
    static const char *SDL_GetError(void) { return "SDL not available"; }
    static void SDL_Quit(void) {}
    static void SDL_PauseAudioDevice(int, int) {}
    static void SDL_CloseAudioDevice(int) {}
    static void SDL_DestroyTexture(SDL_Texture *) {}
    static void SDL_DestroyRenderer(SDL_Renderer *) {}
    static void SDL_DestroyWindow(SDL_Window *) {}
    static void SDL_SetWindowTitle(SDL_Window *, const char *) {}
    static void SDL_LockAudioDevice(int) {}
    static void SDL_UnlockAudioDevice(int) {}
    static void SDL_RenderClear(SDL_Renderer *) {}
    static void SDL_RenderCopy(SDL_Renderer *, void *, void *, void *) {}
    static void SDL_RenderPresent(SDL_Renderer *) {}
    static void SDL_UpdateTexture(SDL_Texture *, void *, const void *, int) {}
    /* Minimal SDL_AudioSpec definition */
    typedef struct {
        int freq;
        unsigned int format;
        unsigned char channels;
        unsigned short samples;
        void (*callback)(void *, Uint8 *, int);
        void *userdata;
    } SDL_AudioSpec;
    #define AUDIO_S16SYS 0x8010
    #define SDL_zero(x) memset(&(x), 0, sizeof(x))
    /* X11 specific GUI */
    #include "x11_gui.h"
#endif

/* Core emulation headers */
#include "core/core.h"
#include "core/disas.h"
#include "bk_hw.h"
#include "bk_tape.h"
#include "bk_timing.h"

#define FPS 60
#define CPU_HZ_3MHZ 3000000u
#define CPU_HZ_4MHZ 4000000u
#define MAX_CYCLES_PER_FRAME 200000u

#define MONITOR_BASE 0100000
#define BASIC_BASE 0120000

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [--rom-dir <path>] [--monitor <file>] [--basic <file>] [--tape-in <file>] [--tape-raw] [--tape-name <name>] [--tape-name-pad <zero|space>] [--tape-name-zero] [--tape-name-space] [--tape-out <file>] [--tape-out-bin <file>] "
            "[--cpu-3mhz] [--cpu-4mhz] [--cpu-max] [--start <octal>] [--trace] [--show-shift] [--show-cpu]\n"
            "Defaults to ROM/MONIT10.ROM and ROM/BASIC10.ROM\n",
            prog);
}

static int load_rom_file(const char *path, byte **out_data, word *out_size)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return -1;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 65536) {
        fclose(fp);
        return -1;
    }
    byte *buf = (byte *)malloc((size_t)sz);
    if (!buf) {
        fclose(fp);
        return -1;
    }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        fclose(fp);
        free(buf);
        return -1;
    }
    fclose(fp);
    *out_data = buf;
    *out_size = (word)sz;
    return 0;
}

static int load_tape_file(const char *path, byte **out_data, size_t *out_size)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return -1;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > (1024 * 1024)) {
        fclose(fp);
        return -1;
    }
    byte *buf = (byte *)malloc((size_t)sz);
    if (!buf) {
        fclose(fp);
        return -1;
    }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        fclose(fp);
        free(buf);
        return -1;
    }
    fclose(fp);
    *out_data = buf;
    *out_size = (size_t)sz;
    return 0;
}

static int save_tape_file(const char *path, const byte *data, size_t size)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return -1;
    }
    if (size > 0 && fwrite(data, 1, size, fp) != size) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

static word peek_rom_word(const byte *rom, word size, word offset)
{
    if (!rom || offset + 1 >= size) {
        return 0;
    }
    return (word)(rom[offset] | (rom[offset + 1] << 8));
}

/* Rendering helper – SDL implementation only. */
#ifdef USE_SDL
static void draw_screen(SDL_Renderer *renderer, SDL_Texture *tex)
{
    byte *vram = bk_hw_vram_ptr();
    if (!vram) return;
    word vram_size = bk_hw_vram_size();
    word shift = bk_hw_shift_reg();

    static uint32_t *pixels;
    static size_t pixels_size;
    const size_t needed = (size_t)BK_SCREEN_WIDTH * BK_SCREEN_HEIGHT;
    if (pixels_size != needed) {
        free(pixels);
        pixels = (uint32_t *)calloc(needed, sizeof(uint32_t));
        pixels_size = needed;
    }
    word line_bytes = (word)(BK_SCREEN_WIDTH / 8);
    word scroll = (word)(shift & 0377);
    word base_offset = (word)(((scroll - 0330) & 0377) * 0100);
    if (vram_size) base_offset %= vram_size;
    for (int y = 0; y < BK_SCREEN_HEIGHT; ++y) {
        for (int x = 0; x < BK_SCREEN_WIDTH; ++x) {
            word line_offset = (word)(y * line_bytes);
            word byte_index = (word)(base_offset + line_offset + (x >> 3));
            if (vram_size) byte_index %= vram_size;
            byte mask = (byte)(1 << (x & 7));
            byte bit = vram[byte_index] & mask;
            pixels[y * BK_SCREEN_WIDTH + x] = bit ? 0xFFFFFFFFu : 0x00000000u;
        }
    }
    SDL_UpdateTexture(tex, NULL, pixels, BK_SCREEN_WIDTH * sizeof(uint32_t));
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, tex, NULL, NULL);
    SDL_RenderPresent(renderer);
}
#else
/* No‑op stub for X11 backend – drawing is performed by x11_gui_draw(). */
static void draw_screen(void *unused1, void *unused2) { (void)unused1; (void)unused2; }
#endif

/* Key translation – only needed when SDL backend is compiled. */
#ifdef USE_SDL
static int bk_translate_key(SDL_Keycode key, SDL_Keymod mod)
{
    int code = -1;

    /* Ctrl‑modified shortcuts. */
    if (mod & KMOD_CTRL) {
        switch (key) {
            case SDLK_g: code = 0007; break; /* BEL */
            case SDLK_m: code = 0015; break; /* set tab stop */
            case SDLK_p: code = 0020; break; /* repeat */
            case SDLK_r: code = 0022; break; /* cursor home */
            case SDLK_t: code = 0024; break; /* clear screen */
            case SDLK_u: code = 0025; break; /* line delete */
            default: break;
        }
    }

    /* Regular keys – only processed if no Ctrl shortcut matched. */
    if (code == -1) {
        switch (key) {
            case SDLK_ESCAPE:   code = 003;  break; /* cancel line */
            case SDLK_BACKSPACE:code = 030;  break; /* cursor left */
            case SDLK_RETURN:   code = 012;  break; /* VK */
            case SDLK_TAB:      code = 011;  break;
            case SDLK_LEFT:     code = 010;  break;
            case SDLK_RIGHT:    code = 031;  break;
            case SDLK_UP:       code = 032;  break;
            case SDLK_DOWN:     code = 033;  break;
            case SDLK_HOME:     code = 023;  break;
            case SDLK_F1:  code = 0201; break; /* povt */
            case SDLK_F2:  code = 003;  break; /* kt */
            case SDLK_F3:  code = 0213; break; /* -|--> */
            case SDLK_F4:  code = 026;  break; /* |<--- */
            case SDLK_F5:  code = 027;  break; /* |---> */
            case SDLK_F6:  code = 0202; break; /* ind su */
            case SDLK_F7:  code = 0204; break; /* blk red */
            case SDLK_F8:  code = 0200; break; /* shag */
            case SDLK_F9:  code = 014;  break; /* sbr */
            case SDLK_F10: code = 0016; break; /* RUS */
            case SDLK_F11: code = 0017; break; /* LAT */
            default: break;
        }
    }

    /* Numeric keys. */
    if (code == -1 && key >= SDLK_0 && key <= SDLK_9) {
        static const char shifted[] = ")!@#$%^&*(";
        char c = (char)('0' + (key - SDLK_0));
        if (mod & KMOD_SHIFT) c = shifted[key - SDLK_0];
        code = (int)c;
    }

    /* Alphabetic keys. */
    if (code == -1 && key >= SDLK_a && key <= SDLK_z) {
        char c = (char)('a' + (key - SDLK_a));
        if (mod & KMOD_SHIFT) c = (char)('A' + (key - SDLK_a));
        code = (int)c;
    }

    /* Punctuation and other symbols. */
    if (code == -1) {
        switch (key) {
            case SDLK_SPACE: code = ' '; break;
            case SDLK_MINUS: code = (mod & KMOD_SHIFT) ? '_' : '-'; break;
            case SDLK_EQUALS: code = (mod & KMOD_SHIFT) ? '+' : '='; break;
            case SDLK_LEFTBRACKET: code = (mod & KMOD_SHIFT) ? '{' : '['; break;
            case SDLK_RIGHTBRACKET: code = (mod & KMOD_SHIFT) ? '}' : ']'; break;
            case SDLK_SEMICOLON: code = (mod & KMOD_SHIFT) ? ':' : ';'; break;
            case SDLK_QUOTE: code = (mod & KMOD_SHIFT) ? '\"' : '\''; break;
            case SDLK_COMMA: code = (mod & KMOD_SHIFT) ? '<' : ','; break;
            case SDLK_PERIOD: code = (mod & KMOD_SHIFT) ? '>' : '.'; break;
            case SDLK_SLASH: code = (mod & KMOD_SHIFT) ? '?' : '/'; break;
            case SDLK_BACKSLASH: code = (mod & KMOD_SHIFT) ? '|' : '\\'; break;
            case SDLK_BACKQUOTE: code = (mod & KMOD_SHIFT) ? '~' : '`'; break;
            default: break;
        }
    }

    /* If Alt (left or right) is held, add 0200 to the result. */
    if (code != -1 && (mod & KMOD_ALT)) {
        code |= 0200;
    }

    return code;
}
#else
/* Stub for X11 backend – key handling performed in x11_gui.c */
static int bk_translate_key(int key, int mod) { (void)key; (void)mod; return 0; }
#endif

static void update_window_title(SDL_Window *win, unsigned int cpu_hz, int cpu_max, int name_pad_space)
{
    char title[128];
    if (cpu_max) {
        snprintf(title, sizeof(title), "BK0010-01 (CPU: MAX, NAME: %s)",
                 name_pad_space ? "SPACE" : "ZERO");
    } else {
        double mhz = cpu_hz / 1000000.0;
        snprintf(title, sizeof(title), "BK0010-01 (CPU: %.1f MHz, NAME: %s)",
                 mhz, name_pad_space ? "SPACE" : "ZERO");
    }
    SDL_SetWindowTitle(win, title);
}

typedef struct {
    int sample_rate;
    int freq;
    int phase;
    Uint32 beep_until_ms;
} beeper_state;

static void audio_callback(void *userdata, Uint8 *stream, int len)
{
    beeper_state *st = (beeper_state *)userdata;
    int16_t *out = (int16_t *)stream;
    int samples = len / (int)sizeof(int16_t);
    Uint32 now = SDL_GetTicks();
    int on = 0;
    if (st->beep_until_ms != 0) {
        on = ((int32_t)(now - st->beep_until_ms) < 0);
    }
    int step = st->sample_rate / st->freq;
    if (step <= 0) {
        step = 1;
    }
    for (int i = 0; i < samples; i++) {
        if (on) {
            int v = (st->phase < step / 2) ? 8000 : -8000;
            out[i] = (int16_t)v;
            st->phase++;
            if (st->phase >= step) {
                st->phase = 0;
            }
        } else {
            out[i] = 0;
        }
    }
}

int main(int argc, char **argv)
{
    const char *rom_dir = "ROM";
    const char *monitor_path_arg = NULL;
    const char *basic_path_arg = NULL;
    const char *tape_in_path = NULL;
    const char *tape_out_path = NULL;
    const char *tape_out_bin_path = NULL;
    const char *tape_name = NULL;
    char tape_name_buf[32];
    int tape_raw = 0;
    word start_addr = 0;
    int have_start = 0;
    int trace = 0;
    int show_shift = 0;
    int show_cpu = 0;
    unsigned int cpu_hz = CPU_HZ_3MHZ;
    int cpu_max = 0;
    int cpu_mode = 0;
    int name_pad_space = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--rom-dir") == 0 && i + 1 < argc) {
            rom_dir = argv[++i];
        } else if (strcmp(argv[i], "--monitor") == 0 && i + 1 < argc) {
            monitor_path_arg = argv[++i];
        } else if (strcmp(argv[i], "--basic") == 0 && i + 1 < argc) {
            basic_path_arg = argv[++i];
        } else if (strcmp(argv[i], "--tape-in") == 0 && i + 1 < argc) {
            tape_in_path = argv[++i];
        } else if (strcmp(argv[i], "--tape-raw") == 0) {
            tape_raw = 1;
        } else if (strcmp(argv[i], "--tape-name") == 0 && i + 1 < argc) {
            tape_name = argv[++i];
            if (strcmp(tape_name, "-") == 0) {
                tape_name = "";
            }
        } else if (strcmp(argv[i], "--tape-out") == 0 && i + 1 < argc) {
            tape_out_path = argv[++i];
        } else if (strcmp(argv[i], "--tape-out-bin") == 0 && i + 1 < argc) {
            tape_out_bin_path = argv[++i];
        } else if (strcmp(argv[i], "--trace") == 0) {
            trace = 1;
        } else if (strcmp(argv[i], "--show-shift") == 0) {
            show_shift = 1;
        } else if (strcmp(argv[i], "--show-cpu") == 0) {
            show_cpu = 1;
        } else if (strcmp(argv[i], "--cpu-3mhz") == 0) {
            cpu_hz = CPU_HZ_3MHZ;
            cpu_max = 0;
            cpu_mode = 0;
        } else if (strcmp(argv[i], "--cpu-4mhz") == 0) {
            cpu_hz = CPU_HZ_4MHZ;
            cpu_max = 0;
            cpu_mode = 1;
        } else if (strcmp(argv[i], "--cpu-max") == 0) {
            cpu_max = 1;
            cpu_mode = 2;
        } else if (strcmp(argv[i], "--tape-name-pad") == 0 && i + 1 < argc) {
            const char *pad = argv[++i];
            if (strcmp(pad, "space") == 0) {
                name_pad_space = 1;
            } else if (strcmp(pad, "zero") == 0) {
                name_pad_space = 0;
            } else {
                fprintf(stderr, "Unknown pad mode: %s\n", pad);
                return 1;
            }
        } else if (strcmp(argv[i], "--tape-name-space") == 0) {
            name_pad_space = 1;
        } else if (strcmp(argv[i], "--tape-name-zero") == 0) {
            name_pad_space = 0;
        } else if (strcmp(argv[i], "--start") == 0 && i + 1 < argc) {
            unsigned int tmp = 0;
            sscanf(argv[++i], "%o", &tmp);
            start_addr = (word)tmp;
            have_start = 1;
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    char monitor_path_buf[512];
    char basic_path_buf[512];
    const char *rom_dirs[] = { rom_dir, "../ROM", "./rom", "../rom" };
    const char *monitor_path = monitor_path_arg;
    const char *basic_path = basic_path_arg;

    if (!monitor_path || !basic_path) {
        for (size_t i = 0; i < sizeof(rom_dirs) / sizeof(rom_dirs[0]); i++) {
            if (!monitor_path) {
                snprintf(monitor_path_buf, sizeof(monitor_path_buf), "%s/MONIT10.ROM", rom_dirs[i]);
                FILE *f = fopen(monitor_path_buf, "rb");
                if (f) {
                    fclose(f);
                    monitor_path = monitor_path_buf;
                }
            }
            if (!basic_path) {
                snprintf(basic_path_buf, sizeof(basic_path_buf), "%s/BASIC10.ROM", rom_dirs[i]);
                FILE *f = fopen(basic_path_buf, "rb");
                if (f) {
                    fclose(f);
                    basic_path = basic_path_buf;
                }
            }
            if (monitor_path && basic_path) {
                break;
            }
        }
    }

    if (!monitor_path) {
        fprintf(stderr, "MONIT10.ROM not found. Use --monitor or --rom-dir.\n");
        return 1;
    }

    byte *monitor_rom = NULL;
    word monitor_size = 0;
    if (load_rom_file(monitor_path, &monitor_rom, &monitor_size) != 0) {
        fprintf(stderr, "Failed to load MONIT10.ROM from %s\n", monitor_path);
        return 1;
    }

    byte *basic_rom = NULL;
    word basic_size = 0;
    if (basic_path && load_rom_file(basic_path, &basic_rom, &basic_size) != 0) {
        fprintf(stderr, "Failed to load BASIC10.ROM from %s\n", basic_path);
        free(monitor_rom);
        return 1;
    }

    fprintf(stderr, "MONIT10.ROM: %s size=%06o first=%06o\n",
            monitor_path, monitor_size, peek_rom_word(monitor_rom, monitor_size, 0));
    if (basic_rom) {
        fprintf(stderr, "BASIC10.ROM: %s size=%06o first=%06o\n",
                basic_path, basic_size, peek_rom_word(basic_rom, basic_size, 0));
    } else {
        fprintf(stderr, "BASIC10.ROM: not loaded\n");
    }

    regs r;
    memset(&r, 0, sizeof(r));
    r.model = K1801VM1;

    bk_hw_connect(&r);
    bk_tape_set_name_pad(name_pad_space);
    if (r.init(&r) != 0) {
        fprintf(stderr, "Hardware init failed\n");
        free(monitor_rom);
        free(basic_rom);
        return 1;
    }

    bk_hw_set_rom_segment(monitor_rom, MONITOR_BASE, monitor_size);
    if (basic_rom) {
        bk_hw_set_rom_segment(basic_rom, BASIC_BASE, basic_size);
    }
    r.SEL1 = MONITOR_BASE;
    core_reset(&r);

    bk_hw_set_tick_hz(cpu_hz);

    if (tape_in_path && !tape_raw && !tape_name) {
        const char *base = strrchr(tape_in_path, '/');
        base = base ? base + 1 : tape_in_path;
        const char *dot = strrchr(base, '.');
        size_t nlen = dot ? (size_t)(dot - base) : strlen(base);
        if (nlen > 16) {
            nlen = 16;
        }
        if (nlen > 0) {
            memcpy(tape_name_buf, base, nlen);
            tape_name_buf[nlen] = '\0';
            tape_name = tape_name_buf;
        }
    }

    if (tape_in_path) {
        byte *tape_data = NULL;
        size_t tape_size = 0;
        if (load_tape_file(tape_in_path, &tape_data, &tape_size) != 0) {
            fprintf(stderr, "Failed to load tape from %s\n", tape_in_path);
        } else if (tape_raw) {
            if (bk_hw_tape_set_input_raw(tape_data, tape_size) != 0) {
                fprintf(stderr, "Failed to set raw tape input\n");
            }
        } else if (tape_name) {
            if (bk_hw_tape_set_input_named(tape_data, tape_size, tape_name) != 0) {
                fprintf(stderr, "Failed to set tape input\n");
            } else {
                fprintf(stderr, "TAPE IN: %s size=%zu name=%s\n",
                        tape_in_path, tape_size, tape_name);
                bk_hw_tape_rewind();
            }
        } else if (bk_hw_tape_set_input(tape_data, tape_size) != 0) {
            fprintf(stderr, "Failed to set tape input\n");
        } else {
            fprintf(stderr, "TAPE IN: %s size=%zu\n", tape_in_path, tape_size);
            bk_hw_tape_rewind();
        }
        free(tape_data);
    }

    if (tape_out_path) {
        bk_hw_tape_set_output_enabled(1);
        fprintf(stderr, "TAPE OUT: %s\n", tape_out_path);
    }

    if (have_start) {
        r.r[7] = start_addr;
    } else {
        r.r[7] = MONITOR_BASE;
    }

    r.r[6] = 01000;

    /* Initialise the chosen GUI backend */
    int gui_init_ok = 0;
#ifdef USE_SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        goto gui_fail;
    }
    SDL_Window *win = SDL_CreateWindow(
        "BK0010-01",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        BK_SCREEN_WIDTH * 2,
        BK_SCREEN_HEIGHT * 2,
        SDL_WINDOW_SHOWN);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        goto gui_fail;
    }
    SDL_Renderer *renderer = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        goto gui_fail;
    }
    SDL_Texture *tex = SDL_CreateTexture(renderer,
                                         SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING,
                                         BK_SCREEN_WIDTH,
                                         BK_SCREEN_HEIGHT);
    if (!tex) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        goto gui_fail;
    }
    gui_init_ok = 1;
#else /* USE_X11 */
    if (x11_gui_init() != 0) {
        fprintf(stderr, "X11 GUI init failed\n");
        goto gui_fail;
    }
    gui_init_ok = 1;
#endif
    if (!gui_init_ok) {
    gui_fail:
        free(monitor_rom);
        free(basic_rom);
        return 1;
    }

    /* Audio initialisation – only when SDL is available */
#ifdef USE_SDL
    SDL_AudioDeviceID audio_dev = 0;
    beeper_state beep_state = { 44100, 1000, 0, 0 };
    SDL_AudioSpec want;
    SDL_AudioSpec have;
    SDL_zero(want);
    want.freq = beep_state.sample_rate;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = audio_callback;
    want.userdata = &beep_state;
    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio_dev != 0) {
        beep_state.sample_rate = have.freq;
        SDL_PauseAudioDevice(audio_dev, 0);
    }
#else
    /* No audio support in X11 mode */
    int audio_dev = 0;
    beeper_state beep_state = { 0, 0, 0, 0 };
#endif

    int running = 1;
    Uint32 frame_delay = 1000 / FPS;

    char disas_buf[256];
    word last_shift = 0xFFFF;
    /* Timing helpers – use SDL when available, otherwise fall back to
       gettimeofday. */
    Uint64 perf_freq = 0;
    Uint64 perf_last = 0;
#ifdef USE_SDL
    perf_freq = SDL_GetPerformanceFrequency();
    perf_last = SDL_GetPerformanceCounter();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    perf_last = (Uint64)tv.tv_sec * 1000000ULL + tv.tv_usec;
    perf_freq = 1000000ULL; /* microseconds */
#endif
    uint64_t instr_count = 0;
    uint64_t cycle_count = 0;
    uint64_t cycles_per_frame = cpu_hz / FPS;
    /* Set window title – only relevant for SDL backend */
#ifdef USE_SDL
    update_window_title(win, cpu_hz, cpu_max, name_pad_space);
#endif
    while (running) {
        Uint32 frame_start = 0;
        /* Timing start */
#ifdef USE_SDL
        frame_start = SDL_GetTicks();
#endif
        if (show_shift) {
            word shift = bk_hw_shift_reg();
            if (shift != last_shift) {
                fprintf(stderr, "SHIFT=%06o (%04X)\n", shift, shift);
                last_shift = shift;
            }
        }

        /* Input handling */
#ifdef USE_SDL
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = 0;
            } else if (ev.type == SDL_KEYDOWN) {
                SDL_Keycode key = ev.key.keysym.sym;
                if (key == SDLK_ESCAPE) {
                    r.fHaltSignal = 1;
                } else if (key == SDLK_F23) {
                    name_pad_space = !name_pad_space;
                    bk_tape_set_name_pad(name_pad_space);
                    update_window_title(win, cpu_hz, cpu_max, name_pad_space);
                } else if (key == SDLK_F24) {
                    cpu_mode = (cpu_mode + 1) % 3;
                    if (cpu_mode == 0) {
                        cpu_hz = CPU_HZ_3MHZ;
                        cpu_max = 0;
                    } else if (cpu_mode == 1) {
                        cpu_hz = CPU_HZ_4MHZ;
                        cpu_max = 0;
                    } else {
                        cpu_max = 1;
                    }
                    cycles_per_frame = cpu_hz / FPS;
                    bk_hw_set_tick_hz(cpu_hz);
                    update_window_title(win, cpu_hz, cpu_max, name_pad_space);
                } else {
                    int code = bk_translate_key(key, ev.key.keysym.mod);
                    if (code >= 0) {
                        bk_hw_handle_key(code);
                    }
                }
            }
    }
#else
    if (x11_gui_handle_events()) {
        running = 0;
    }
#endif

        uint64_t frame_cycles = 0;
        uint64_t frame_budget = cpu_max ? MAX_CYCLES_PER_FRAME : cycles_per_frame;
        while (frame_cycles < frame_budget) {
            if (trace) {
                word pc = r.r[7];
                word addr = pc;
                disas(&r, &addr, disas_buf);
                printf("%06o %06o %s\n", pc, r.load_word(&r, pc), disas_buf);
            }
            if (core_step(&r) != 0) {
                break;
            }
            unsigned cycles = bk_timing_cycles(r.ir);
            if (cycles == 0) {
                cycles = 1;
            }
            if (audio_dev != 0 && bk_hw_beeper_pulse()) {
                Uint32 now = SDL_GetTicks();
                SDL_LockAudioDevice(audio_dev);
                if ((Uint32)(now + 30) > beep_state.beep_until_ms) {
                    beep_state.beep_until_ms = now + 30;
                }
                SDL_UnlockAudioDevice(audio_dev);
            }
            bk_hw_tick_n(cycles);
            instr_count++;
            cycle_count += cycles;
            frame_cycles += cycles;
        }

        /* Render */
#ifdef USE_SDL
        draw_screen(renderer, tex);
#else
        x11_gui_draw();
#endif

        if (show_cpu) {
            Uint64 now = SDL_GetPerformanceCounter();
            double elapsed = (double)(now - perf_last) / (double)perf_freq;
            if (elapsed >= 1.0) {
                double ips = instr_count / elapsed;
                double mhz = cycle_count / elapsed / 1.0e6;
                fprintf(stderr, "CPU: %.2f KIPS (%.2f MHz)\n", ips / 1000.0, mhz);
                instr_count = 0;
                cycle_count = 0;
                perf_last = now;
            }
        }

        /* Frame pacing */
        Uint32 frame_time = 0;
#ifdef USE_SDL
        frame_time = SDL_GetTicks() - frame_start;
        if (!cpu_max && frame_time < frame_delay) {
            SDL_Delay(frame_delay - frame_time);
        }
#else
        if (!cpu_max && frame_time < frame_delay) {
            usleep((frame_delay - frame_time) * 1000);
        }
#endif
    }

    /* Cleanup */
#ifdef USE_SDL
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(win);
    if (audio_dev != 0) {
        SDL_CloseAudioDevice(audio_dev);
    }
    SDL_Quit();
#else
    x11_gui_cleanup();
    /* No audio or SDL cleanup needed */
#endif

    if (tape_out_path) {
        size_t out_size = 0;
        const byte *out_data = bk_hw_tape_output_data(&out_size);
        if (out_size == 0) {
            fprintf(stderr, "TAPE OUT: decoded size is 0\n");
        }
        if (save_tape_file(tape_out_path, out_data, out_size) != 0) {
            fprintf(stderr, "Failed to write tape to %s\n", tape_out_path);
        } else {
            fprintf(stderr, "Wrote tape: %s size=%zu\n", tape_out_path, out_size);
        }
            if (tape_out_bin_path && out_data && out_size > 0) {
                byte *bin_data = NULL;
                size_t bin_size = 0;
                char name_buf[32];
                byte name_raw[16];
                if (bk_tape_decode_raw_to_bin(out_data, out_size, &bin_data, &bin_size,
                                              name_buf, sizeof(name_buf),
                                              name_raw, sizeof(name_raw)) != 0) {
                    fprintf(stderr, "Failed to decode raw tape\n");
                } else if (save_tape_file(tape_out_bin_path, bin_data, bin_size) != 0) {
                    fprintf(stderr, "Failed to write decoded tape to %s\n", tape_out_bin_path);
                } else {
                    fprintf(stderr, "Wrote tape bin: %s size=%zu name=%s\n",
                            tape_out_bin_path, bin_size, name_buf);
                }
                free(bin_data);
            }
        }

    core_fini(&r);
    free(monitor_rom);
    free(basic_rom);
    return 0;
}
