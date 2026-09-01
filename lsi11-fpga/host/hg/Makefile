CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L \
	-D_DARWIN_C_SOURCE

ROOT := ../../..
RT11_TOOLS := $(ROOT)/lsi11/tools
FTDI_CFLAGS := $(shell pkg-config --cflags libftdi1 2>/dev/null)
FTDI_LIBS := $(shell pkg-config --libs libftdi1 2>/dev/null)

COMMON = hg_protocol.c hg_image.c hg_directory.c $(RT11_TOOLS)/rt11fs.c

all: hgfsd

hgfsd: hgfsd.c hg_mpsse.c $(COMMON)
	$(CC) $(CFLAGS) $(FTDI_CFLAGS) -I$(RT11_TOOLS) -o $@ $^ $(FTDI_LIBS)

test_hg: test_hg.c $(COMMON)
	$(CC) $(CFLAGS) -I$(RT11_TOOLS) -o $@ $^

test: test_hg
	./test_hg

clean:
	rm -f hgfsd test_hg
	rm -rf hgfsd.dSYM test_hg.dSYM

.PHONY: all test clean
