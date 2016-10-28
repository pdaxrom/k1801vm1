all: emu11 dis11 mk90

CFLAGS = -Wall -g $(shell sdl2-config --cflags)

ifeq (opengl, $(VIDEO))
VIDEOLIB = -lGL
else
CFLAGS += -DUSE_GLES2
VIDEOLIB = -lGLESv2 -lEGL
endif

OBJS = core/core.o core/disas.o

emu11: $(OBJS) emu11.o core/hardware.o
	$(CC) -g -o $@ $^ -lncurses

dis11: core/disas.o dis11.o
	$(CC) -o $@ $^

mk90: $(OBJS) mk90.o shader.o main.o
	$(CC) -o $@ $^ $(shell sdl2-config --libs) $(VIDEOLIB)

clean:
	rm -f $(OBJS) *.o dis11 emu11 mk90
