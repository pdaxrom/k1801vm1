/*
 * emu11.c
 *
 *  Created on: 19.10.2016
 *      Author: Alexander Chukov <sash@pdaXrom.org>
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <SDL.h>

#include "core/core.h"
#include "core/disas.h"

#include "core/hardware.h"

#define FB_WIDTH	120
#define FB_HEIGHT	64

static int is_running;

void mk90_connect(regs *r);

void dump_regs(regs *r)
{
#define F(f,s) ((p & SET_BIT(f))?s:'-')
	word p = r->psw;
    printf("%c %c - - %c %c %c %c %c\n", F(BIT_H, 'H'), F(BIT_P, 'P'), F(BIT_T, 'T'), F(BIT_N, 'N'), F(BIT_Z, 'Z'), F(BIT_V, 'V'), F(BIT_C, 'C'));
    printf("R0=%06o R1=%06o R2=%06o R3=%06o R4=%06o R5=%06o SP=%06o PC=%06o PSW=%06o\n",
	    r->r[0], r->r[1], r->r[2], r->r[3], r->r[4], r->r[5], r->r[6], r->r[7], r->psw);
#undef F
}

void dump_mem(regs *r, word start, word length, byte mode) {
	char buf[9];
	int i = 0;

	buf[8] = 0;

	while (i < length) {
		if (i % 8 == 0) {
			printf("%06o: ", start);
		}
		if (mode) {
			byte bl = r->load_byte(r, start++);
			byte bh = r->load_byte(r, start++);
			buf[(i++) % 8] = (bl >= 32)?bl:'.';
			buf[i % 8] = (bh >= 32)?bh:'.';
			word w = (bh << 8) | bl;
			printf("%06o ", w);
		} else {
			byte b = r->load_byte(r, start++);
			buf[i % 8] = (b >= 32)?b:'.';
			printf("%03o ", b);
		}
		if (i % 8 == 7) {
			printf("%s\n", buf);
			buf[8] = 0;
		}
		i++;
	}
	buf[i % 8] = 0;

	if (i % 8 != 0) {
		printf("%s\n", buf);
	}
}

static int SDLCALL cpu_thread(void *args)
{
    char out[1024];
    regs *r = (regs *) args;

	while (is_running) {
		word start_addr = r->r[7];
		word addr = start_addr;
		dump_regs(r);
		char *dis_str = disas(r, &addr, out);
		printf("\n%06o ", start_addr);
		int i = 0;
		for (word a = start_addr; a < addr; a += 2) {
			printf("%06o ", r->load_word(r, a));
			i++;
		}
		while (i < 3) {
			printf("       ");
			i++;
		}
		printf("%s\n", dis_str);
		int key;
#if 0
		do {
			key = getchar();
			printf("--> %d\n", key);
			if (key == 'm' || key == 'M') {
				int start = 0;
				int len = 0;
				printf("Mem dump start address: ");
				scanf("%o", &start);
				printf("Mem dump length: ");
				scanf("%o", &len);
				dump_mem(&r, start, len, (key == 'M')?1:0);
			} else if (key == '0') {
				is_running = 0;
			}
		} while (key != 10);
#endif
if (r->load_word(r, addr) != 0) {
		core_step(r);
}
		
		usleep(100);
	}
}

static void draw_screen(regs *r, unsigned int *framebuffer, int width, int height)
{

//	word *vram = (word *)r->ramptr(r, (r->load_byte(r, 0xe800) << 8) | r->load_byte(r, 0xe801));
	word *vram = (word *)r->ramptr(r, r->load_word(r, 0xe800));

//	SDL_Log("Video mem = %04X\n", (ka1835vg1_read_byte(1) << 8) | ka1835vg1_read_byte(0));
	int page = 0;
	int i = 0;
	int j = 0;
	int bit = 0x80;

	for (page = 0; page < 2; page++) {
		for (i = 0; i < FB_WIDTH * FB_HEIGHT / 16; i++) {
			int bit_count;
			word tmp = vram[i];
			for (bit_count = 0; bit_count < 8; bit_count++) {
				if (tmp & bit) {
					framebuffer[j++] = 0xffffffff;
				} else {
					framebuffer[j++] = 0;
				}
				tmp <<= 1;
			}
		}
		bit = 0x8000;
	}
}

int main(int argc, char *argv[])
{
	char out[1024];
	regs r;
	word addr;
	word length;

	r.model = K1806VM2;

	r.r[6] = 0;

	hwstub_connect(&r);

	mk90_connect(&r);

	core_init(&r);

	byte *mem = r.ramptr(&r, 0);

	FILE *inf = fopen(argv[1], "rb");
	if (inf) {
		unsigned int tmp;
		sscanf(argv[2], "%o", &tmp);
		addr = tmp & 0177776;
		length = fread(&mem[addr], 1, 65536 - addr, inf);
		fprintf(stderr, "Loaded file %s to %06o length %06o\n", argv[1], addr, length);
		fclose(inf);
	} else {
		fprintf(stderr, "Can not open file %s\n", argv[1]);
		return 1;
	}

	r.SEL0 = addr & 0177400;

	fprintf(stderr, "Reset address set to %06o\n", r.SEL0);

	core_reset(&r);

//fixme: hack
r.r[7] = addr;
r.r[6] = 0x0200;
r.r[7] = 0xf630;

        if (SDL_Init(SDL_INIT_EVERYTHING) < 0) { /* Initialize SDL's Video subsystem */
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init fail : %s\n", SDL_GetError());
            return 1;
        }

    SDL_Window *window = SDL_CreateWindow("SDL2 Framebuffer Emulation",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          FB_WIDTH * 2, FB_HEIGHT * 2,   // scaled 2x
                                          SDL_WINDOW_SHOWN);
    if (!window) {
        printf("SDL_CreateWindow Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1,
                                                SDL_RENDERER_ACCELERATED |
                                                SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        printf("SDL_CreateRenderer Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Our framebuffer texture (ARGB8888 format)
    SDL_Texture *framebuffer = SDL_CreateTexture(renderer,
                                                 SDL_PIXELFORMAT_ARGB8888,
                                                 SDL_TEXTUREACCESS_STREAMING,
                                                 FB_WIDTH, FB_HEIGHT);
    if (!framebuffer) {
        printf("SDL_CreateTexture Error: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }



    is_running = 1;

        SDL_Thread *cpu = SDL_CreateThread(cpu_thread, "CPU thread", &r);

    SDL_Event event;

    while (is_running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                is_running = 0;
            }
        }

        // Lock framebuffer to access pixels directly
        void *pixels;
        int pitch;
        if (SDL_LockTexture(framebuffer, NULL, &pixels, &pitch) == 0) {
            // pitch = number of bytes per row
            draw_screen(&r, pixels, FB_WIDTH, FB_HEIGHT);

            SDL_UnlockTexture(framebuffer);
        }

        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, framebuffer, NULL, NULL);
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyTexture(framebuffer);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();


	core_fini(&r);

	return 0;
}
