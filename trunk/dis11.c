/*
 * dis11.c
 *
 *  Created on: 19.10.2016
 *      Author: Alexander Chukov <sash@pdaXrom.org>
 */

#include <stdio.h>
#include <stdlib.h>

#include "core/core.h"
#include "core/disas.h"

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

	word end_addr = (addr + length + 1) & 0177776;
	while (addr < end_addr) {
		printf("%06o %06o ", addr, (r.mem[addr] | (r.mem[addr + 1] << 8)));
		printf("%s\n", disas(&r, &addr, out));
	}

	return 0;
}
