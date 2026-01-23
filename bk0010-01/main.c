#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "core/core.h"
#include "core/disas.h"
#include "bk_hw.h"

#define FPS 60
#define CYCLES_PER_FRAME 40000

#define MONITOR_BASE 0100000
#define BASIC_BASE 0120000

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [--rom-dir <path>] [--monitor <file>] [--basic <file>] [--start <octal>] [--trace] [--show-shift] [--show-cpu]\n"
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

static word peek_rom_word(const byte *rom, word size, word offset)
{
    if (!rom || offset + 1 >= size) {
        return 0;
    }
    return (word)(rom[offset] | (rom[offset + 1] << 8));
}

static void draw_screen(SDL_Renderer *renderer, SDL_Texture *tex)
{
    byte *vram = bk_hw_vram_ptr();
    if (!vram) {
        return;
    }
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
    if (vram_size) {
        base_offset %= vram_size;
    }
    for (int y = 0; y < BK_SCREEN_HEIGHT; y++) {
        for (int x = 0; x < BK_SCREEN_WIDTH; x++) {
            word line_offset = (word)(y * line_bytes);
            word byte_index = (word)(base_offset + line_offset + (x >> 3));
            if (vram_size) {
                byte_index %= vram_size;
            }
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

static int bk_translate_key(SDL_Keycode key, SDL_Keymod mod)
{
    if (mod & KMOD_CTRL) {
        switch (key) {
        case SDLK_g: return 0007; /* BEL */
        case SDLK_m: return 0015; /* set tab stop */
        case SDLK_p: return 0020; /* repeat */
        case SDLK_r: return 0022; /* cursor home */
        case SDLK_t: return 0024; /* clear screen */
        case SDLK_u: return 0025; /* line delete */
        default: break;
        }
    }

    switch (key) {
    case SDLK_ESCAPE: return 0003; /* cancel line */
    case SDLK_BACKSPACE: return 0010; /* cursor left */
    case SDLK_RETURN: return 0012; /* VK */
    case SDLK_KP_ENTER: return 0012;
    case SDLK_TAB: return 0211;
    case SDLK_LEFT: return 0010;
    case SDLK_RIGHT: return 0037;
    case SDLK_UP: return 0040;
    case SDLK_DOWN: return 0041;
    case SDLK_HOME: return 0022;
    case SDLK_F1: return 0016; /* RUS */
    case SDLK_F2: return 0017; /* LAT */
    case SDLK_F5: return 0014; /* reset screen */
    default: break;
    }

    if (key >= SDLK_0 && key <= SDLK_9) {
        static const char shifted[] = ")!@#$%^&*(";
        char c = (char)('0' + (key - SDLK_0));
        if (mod & KMOD_SHIFT) {
            c = shifted[key - SDLK_0];
        }
        return (byte)c;
    }

    if (key >= SDLK_a && key <= SDLK_z) {
        char c = (char)('a' + (key - SDLK_a));
        if (mod & KMOD_SHIFT) {
            c = (char)('A' + (key - SDLK_a));
        }
        return (byte)c;
    }

    switch (key) {
    case SDLK_SPACE: return ' ';
    case SDLK_MINUS: return (mod & KMOD_SHIFT) ? '_' : '-';
    case SDLK_EQUALS: return (mod & KMOD_SHIFT) ? '+' : '=';
    case SDLK_LEFTBRACKET: return (mod & KMOD_SHIFT) ? '{' : '[';
    case SDLK_RIGHTBRACKET: return (mod & KMOD_SHIFT) ? '}' : ']';
    case SDLK_SEMICOLON: return (mod & KMOD_SHIFT) ? ':' : ';';
    case SDLK_QUOTE: return (mod & KMOD_SHIFT) ? '\"' : '\'';
    case SDLK_COMMA: return (mod & KMOD_SHIFT) ? '<' : ',';
    case SDLK_PERIOD: return (mod & KMOD_SHIFT) ? '>' : '.';
    case SDLK_SLASH: return (mod & KMOD_SHIFT) ? '?' : '/';
    case SDLK_BACKSLASH: return (mod & KMOD_SHIFT) ? '|' : '\\';
    case SDLK_BACKQUOTE: return (mod & KMOD_SHIFT) ? '~' : '`';
    default: break;
    }

    return -1;
}

int main(int argc, char **argv)
{
    const char *rom_dir = "ROM";
    const char *monitor_path_arg = NULL;
    const char *basic_path_arg = NULL;
    word start_addr = 0;
    int have_start = 0;
    int trace = 0;
    int show_shift = 0;
    int show_cpu = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--rom-dir") == 0 && i + 1 < argc) {
            rom_dir = argv[++i];
        } else if (strcmp(argv[i], "--monitor") == 0 && i + 1 < argc) {
            monitor_path_arg = argv[++i];
        } else if (strcmp(argv[i], "--basic") == 0 && i + 1 < argc) {
            basic_path_arg = argv[++i];
        } else if (strcmp(argv[i], "--trace") == 0) {
            trace = 1;
        } else if (strcmp(argv[i], "--show-shift") == 0) {
            show_shift = 1;
        } else if (strcmp(argv[i], "--show-cpu") == 0) {
            show_cpu = 1;
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

    if (have_start) {
        r.r[7] = start_addr;
    } else {
        r.r[7] = MONITOR_BASE;
    }

    r.r[6] = 01000;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        free(monitor_rom);
        free(basic_rom);
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow(
        "BK0010-01 (core)",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        BK_SCREEN_WIDTH * 2,
        BK_SCREEN_HEIGHT * 2,
        SDL_WINDOW_SHOWN);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        free(monitor_rom);
        free(basic_rom);
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        free(monitor_rom);
        free(basic_rom);
        return 1;
    }

    SDL_Texture *tex = SDL_CreateTexture(renderer,
                                         SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING,
                                         BK_SCREEN_WIDTH,
                                         BK_SCREEN_HEIGHT);
    if (!tex) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(win);
        SDL_Quit();
        free(monitor_rom);
        free(basic_rom);
        return 1;
    }

    int running = 1;
    Uint32 frame_delay = 1000 / FPS;

    char disas_buf[256];
    word last_shift = 0xFFFF;
    Uint64 perf_freq = SDL_GetPerformanceFrequency();
    Uint64 perf_last = SDL_GetPerformanceCounter();
    unsigned long long instr_count = 0;
    while (running) {
        Uint32 frame_start = SDL_GetTicks();
        if (show_shift) {
            word shift = bk_hw_shift_reg();
            if (shift != last_shift) {
                fprintf(stderr, "SHIFT=%06o (%04X)\n", shift, shift);
                last_shift = shift;
            }
        }

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = 0;
            } else if (ev.type == SDL_KEYDOWN) {
                SDL_Keycode key = ev.key.keysym.sym;
                if (key == SDLK_ESCAPE) {
                    r.fHaltSignal = 1;
                } else {
                    int code = bk_translate_key(key, ev.key.keysym.mod);
                    if (code >= 0) {
                        bk_hw_handle_key(code);
                    }
                }
            }
        }

        for (int i = 0; i < CYCLES_PER_FRAME; i++) {
            if (trace) {
                word pc = r.r[7];
                word addr = pc;
                disas(&r, &addr, disas_buf);
                printf("%06o %06o %s\n", pc, r.load_word(&r, pc), disas_buf);
            }
            if (core_step(&r) != 0) {
                break;
            }
            bk_hw_tick();
            instr_count++;
        }

        draw_screen(renderer, tex);

        if (show_cpu) {
            Uint64 now = SDL_GetPerformanceCounter();
            double elapsed = (double)(now - perf_last) / (double)perf_freq;
            if (elapsed >= 1.0) {
                double ips = instr_count / elapsed;
                fprintf(stderr, "CPU: %.2f KIPS (%.2f MHz)\n", ips / 1000.0, ips / 1.0e6);
                instr_count = 0;
                perf_last = now;
            }
        }

        Uint32 frame_time = SDL_GetTicks() - frame_start;
        if (frame_time < frame_delay) {
            SDL_Delay(frame_delay - frame_time);
        }
    }

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(win);
    SDL_Quit();

    core_fini(&r);
    free(monitor_rom);
    free(basic_rom);
    return 0;
}
