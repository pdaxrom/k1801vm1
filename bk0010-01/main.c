#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

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
            "Usage: %s [--rom-dir <path>] [--monitor <file>] [--basic <file>] [--tape-in <file>] [--tape-raw] [--tape-name <name>] [--tape-out <file>] [--tape-out-bin <file>] "
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

static void update_window_title(SDL_Window *win, unsigned int cpu_hz, int cpu_max)
{
    char title[128];
    if (cpu_max) {
        snprintf(title, sizeof(title), "BK0010-01 (CPU: MAX)");
    } else {
        double mhz = cpu_hz / 1000000.0;
        snprintf(title, sizeof(title), "BK0010-01 (CPU: %.1f MHz)", mhz);
    }
    SDL_SetWindowTitle(win, title);
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

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        free(monitor_rom);
        free(basic_rom);
        return 1;
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
    uint64_t instr_count = 0;
    uint64_t cycle_count = 0;
    uint64_t cycles_per_frame = cpu_hz / FPS;
    update_window_title(win, cpu_hz, cpu_max);
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
                } else if (key == SDLK_F10) {
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
                    update_window_title(win, cpu_hz, cpu_max);
                } else {
                    int code = bk_translate_key(key, ev.key.keysym.mod);
                    if (code >= 0) {
                        bk_hw_handle_key(code);
                    }
                }
            }
        }

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
            bk_hw_tick_n(cycles);
            instr_count++;
            cycle_count += cycles;
            frame_cycles += cycles;
        }

        draw_screen(renderer, tex);

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

        Uint32 frame_time = SDL_GetTicks() - frame_start;
        if (!cpu_max && frame_time < frame_delay) {
            SDL_Delay(frame_delay - frame_time);
        }
    }

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(win);
    SDL_Quit();

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
