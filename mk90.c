/*
 * mk90.c
 *
 *  Created on: 26.10.2016
 *      Author: sash
 */

#include <stdio.h>
#include <unistd.h>
#include <SDL2/SDL.h>
#ifdef USE_GLES2
#include <SDL2/SDL_opengles2.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <SDL2/SDL_opengl.h>
#endif

#include "core/hardware.h"
#include "shader.h"

#define FB_WIDTH	120
#define FB_HEIGHT	64

typedef struct {
	regs *r;
    int w;
    int h;
} Video_Args;

static SDL_Thread *video_thread;
static SDL_Thread *inputThread;

static SDL_Window *window;
static SDL_Surface *framebuffer;

static float scale_x = 1;
static float scale_y = 1;

static int exit_request = 0;

static byte ka1835vg1_reg[8];

static byte ka1835vg1_read_byte(byte addr)
{
	return ka1835vg1_reg[addr];
}

static void ka1835vg1_write_byte(byte addr, byte value)
{
	ka1835vg1_reg[addr] = value;
}

int hardware_load_byte(regs *r, word offset, byte *value)
{
	if (offset >= 0xe800 && offset <= 0xe807) {
		*value = ka1835vg1_read_byte(offset & 0x7);
		SDL_Log("Read from KA1835VG1 [%04X] -> %02X\n", offset, *value);
		return 1;
	}

	return 0;
}

int hardware_store_byte(regs *r, word offset, byte value)
{
	if (offset >= 0xe800 && offset <= 0xe807) {
		SDL_Log("Write to KA1835VG1 [%04X] <- %02X\n", offset, value);
		ka1835vg1_write_byte(offset & 0x7, value);
		return 1;
	}

	return 0;
}

int hardware_load_word(regs *r, word offset, word *value)
{
	byte l, h;
	int ret = hardware_load_byte(r, offset, &l);
	ret |= hardware_load_byte(r, offset + 1, &h);
	*value = (h << 8) | l;

	return ret;
}

int hardware_store_word(regs *r, word offset, word value)
{
	//SDL_Log("Write to HW addr %04X <- %04X\n", offset, value);
	int ret = hardware_store_byte(r, offset, value & 0xff);
	ret |= hardware_store_byte(r, offset + 1, value >> 8);

	return ret;
}

static void draw_screen(regs *r, unsigned short *framebuffer, int width, int height)
{
	word *mem = (word *)&r->mem[(ka1835vg1_read_byte(1) << 8) | ka1835vg1_read_byte(0)];

//	SDL_Log("Video mem = %04X\n", (ka1835vg1_read_byte(1) << 8) | ka1835vg1_read_byte(0));
	int page = 0;
	int i = 0;
	int j = 0;
	int bit = 0x80;

	for (page = 0; page < 2; page++) {
		for (i = 0; i < FB_WIDTH * FB_HEIGHT / 16; i++) {
			int bit_count;
			word tmp = mem[i];
			for (bit_count = 0; bit_count < 8; bit_count++) {
				if (tmp & bit) {
					framebuffer[j++] = 0xffff;
				} else {
					framebuffer[j++] = 0;
				}
				tmp <<= 1;
			}
		}
		bit = 0x8000;
	}
}

static int SDLCALL HandleVideo(void *args)
{
    enum {
    	ATTRIB_VERTEX,
    	ATTRIB_TEXTUREPOSITON,
    	NUM_ATTRIBUTES
    };

    static const GLfloat squareVertices[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f,
    };

    static const GLfloat textureVertices[] = {
         0.0f,  1.0f,
         1.0f,  1.0f,
         0.0f,  0.0f,
         1.0f,  0.0f,
    };

    Video_Args *video_args = (Video_Args *) args;

    SDL_GLContext context;
    SDL_DisplayMode mode;

    SDL_Log("Video thread starting...");

    SDL_GetDesktopDisplayMode(0, &mode);

    SDL_GL_SetAttribute(SDL_GL_BUFFER_SIZE, 16);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);

    if (video_args->w > 0 && video_args->h > 0) {
    	mode.w = video_args->w;
    	mode.h = video_args->h;
    } else {
    	mode.w = 640;
    	mode.h = 480;
    }

    SDL_Log("Window size %dx%d\n", mode.w, mode.h);

    window = SDL_CreateWindow("MK90 LCD", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
    							mode.w, mode.h, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);

    if(!window) {
    	SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Window creation fail : %s\n",SDL_GetError());
    	return 1;
    }

    context = SDL_GL_CreateContext(window);
    if (!context) {
    	SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Unable to create GL context : %s\n",SDL_GetError());
    	return 1;
    }

    SDL_GL_MakeCurrent(window, context);

    // Start of GL init

    GLuint vertexShader = -1;
    GLuint fragmentShader = -1;

    if (process_shader(&vertexShader, "shaders/shader.vert", GL_VERTEX_SHADER)) {
    	SDL_Log("Unable load vertex shader");
    	return 1;
    }

    if (process_shader(&fragmentShader, "shaders/shader.frag", GL_FRAGMENT_SHADER)) {
    	SDL_Log("Unable load fragment shader");
    	return 1;
    }

    GLuint shaderProgram  = glCreateProgram ();                 // create program object
    glAttachShader ( shaderProgram, vertexShader );             // and attach both...
    glAttachShader ( shaderProgram, fragmentShader );           // ... shaders to it

    glBindAttribLocation(shaderProgram, ATTRIB_VERTEX, "position");
    glBindAttribLocation(shaderProgram, ATTRIB_TEXTUREPOSITON, "inputTextureCoordinate");

    glLinkProgram ( shaderProgram );    // link the program
    glUseProgram  ( shaderProgram );    // and select it for usage

    glActiveTexture(GL_TEXTURE0);
    GLuint videoFrameTexture = 0;
    glGenTextures(1, &videoFrameTexture);
    glBindTexture(GL_TEXTURE_2D, videoFrameTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindTexture(GL_TEXTURE_2D, videoFrameTexture);

    GLint tex = glGetUniformLocation(shaderProgram, "tex");

    glUniform1i(tex, 0);

    glVertexAttribPointer(ATTRIB_VERTEX, 2, GL_FLOAT, 0, 0, squareVertices);
    glEnableVertexAttribArray(ATTRIB_VERTEX);
    glVertexAttribPointer(ATTRIB_TEXTUREPOSITON, 2, GL_FLOAT, 0, 0, textureVertices);
    glEnableVertexAttribArray(ATTRIB_TEXTUREPOSITON);

    glViewport ( 0 , 0 , mode.w , mode.h );

    // End of GL init


#ifdef APP_ICON
    SetIcon(window);
#endif

    SDL_Surface *surface = SDL_CreateRGBSurface(SDL_SWSURFACE, FB_WIDTH, FB_HEIGHT, 32,
#if SDL_BYTEORDER == SDL_LIL_ENDIAN     /* OpenGL RGBA masks */
                                 0x000000FF,
                                 0x0000FF00, 0x00FF0000, 0xFF000000
#else
                                 0xFF000000,
                                 0x00FF0000, 0x0000FF00, 0x000000FF
#endif
        );


    framebuffer = SDL_CreateRGBSurface(SDL_SWSURFACE, FB_WIDTH, FB_HEIGHT, 16,
#if SDL_BYTEORDER == SDL_LIL_ENDIAN     /* OpenGL RGBA masks */
	0xF800, 0x07E0, 0x001F, 0x0000
#else
	0x001F, 0x07E0, 0xF800, 0x0000
#endif
        );

    scale_x = (float) mode.w / FB_WIDTH;
    scale_y = (float) mode.h / FB_HEIGHT;

    while (!exit_request) {
    	draw_screen(video_args->r, framebuffer->pixels, framebuffer->w, framebuffer->h);

    	usleep(10000);

    	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    	SDL_GL_SwapWindow(window);

    	SDL_BlitSurface(framebuffer, NULL, surface, NULL);
    	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, surface->w, surface->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, surface->pixels);
    }

    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);

    SDL_Log("Video thread finished...");
    return 0;
}

int start_hardware(regs *r, int width, int height)
{
    static Video_Args video_args = {
    	.w = 0,
    	.h = 0,
    };

    SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO);

    if (SDL_Init(SDL_INIT_EVERYTHING) < 0) { /* Initialize SDL's Video subsystem */
    	SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init fail : %s\n", SDL_GetError());
    	return 1;
    }

    exit_request = 0;

    video_args.r = r;
    video_args.w = width;
    video_args.h = height;

    video_thread = SDL_CreateThread(HandleVideo, "MK90 LCD emulation", &video_args);
    //inputThread = SDL_CreateThread(HandleKeyboard, "Pyldin keyboard", NULL);

    return 0;
}

void stop_hardware(regs *r)
{
	exit_request = 1;

    SDL_WaitThread(video_thread, NULL);
    //SDL_WaitThread(inputThread, NULL);

    SDL_Quit();
}
