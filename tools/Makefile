all: macro11 dumpobj

CFLAGS = -O2 -Wall

macro11: macro11.o mlb.o object.o stream2.o util.o rad50.o
	$(CC) -o $@ $^

dumpobj: dumpobj.o rad50.o
	$(CC) -o $@ $^

clean:
	rm -f macro11 dumpobj *.o

macro11_SOURCES = macro11.c macro11.h \
       mlb.c mlb.h \
       object.c object.h \
       stream2.c stream2.h \
       util.c util.h \
       rad50.c rad50.h

dumpobj_SOURCES = dumpobj.c \
       rad50.c rad50.h

genblkram_SOURCES = genblkram.c \
       rad50.c rad50.h

sdfmt_SOURCES = sdfmt.c

