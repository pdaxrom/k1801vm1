/*
 * emu11.c
 *
 *  Created on: 19.10.2016
 *      Author: Alexander Chukov <sash@pdaXrom.org>
 */

#include <stdio.h>
#include <stdlib.h>

#include "core/core.h"
#include "core/disas.h"

void dump_regs(regs *r)
{
#define F(f) ((p & SET_BIT(f))?1:0)
	word p = r->psw;
    printf("R0=%06o R1=%06o R2=%06o R3=%06o R4=%06o R5=%06o SP=%06o PC=%06o PSW=%06o ",
	    r->r[0], r->r[1], r->r[2], r->r[3], r->r[4], r->r[5], r->r[6], r->r[7], r->psw);
    printf("P=%d T=%d N=%d Z=%d V=%d C=%d", F(BIT_P), F(BIT_T), F(BIT_N), F(BIT_Z), F(BIT_V), F(BIT_C));
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
			byte bl = r->mem[start++];
			byte bh = r->mem[start++];
			buf[(i++) % 8] = (bl >= 32)?bl:'.';
			buf[i % 8] = (bh >= 32)?bh:'.';
			word w = (bh << 8) | bl;
			printf("%06o ", w);
		} else {
			byte b = r->mem[start++];
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

int main(int argc, char *argv[])
{
	char out[1024];
	regs r;
	word addr;
	word length;

	r.mem = malloc(65536);

	FILE *inf = fopen(argv[1], "rb");
	if (inf) {
		unsigned int tmp;
		sscanf(argv[2], "%o", &tmp);
		addr = tmp & 0177776;
		length = fread(&r.mem[addr], 1, 65536 - addr, inf);
		fprintf(stderr, "Loaded file %s to %06o length %06o\n", argv[1], addr, length);
		fclose(inf);
	} else {
		fprintf(stderr, "Can not open file %s\n", argv[1]);
		return 1;
	}

	r.SEL1 = addr & 0177400;

	fprintf(stderr, "Reset address set to %06o\n", r.SEL1);

	core_reset(&r);

	while (1) {
		addr = r.r[7];
		printf("\n%06o %06o ", addr, (r.mem[addr] | (r.mem[addr + 1] << 8)));
		printf("%s\n", disas(&r, &addr, out));
		dump_regs(&r);
		int key;
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
			}
		} while (key != 10);
		core_step(&r);
	}

	return 0;
}
