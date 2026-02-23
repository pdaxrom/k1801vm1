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

#include "core/hardware.h"

int main(int argc, char *argv[])
{
	char out[1024];
	regs r;
	word addr;
	word length;
	byte *stub_mem;

	r.model = K1806VM2;

	stub_mem = calloc(1, hwstub_required_memory_size());
	if (!stub_mem) {
		fprintf(stderr, "Failed to allocate hwstub memory\n");
		return 1;
	}
	if (hwstub_set_memory(stub_mem, hwstub_required_memory_size()) != 0) {
		fprintf(stderr, "Failed to bind hwstub memory\n");
		free(stub_mem);
		return 1;
	}
	hwstub_connect(&r);

	r.init(&r);

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
		r.fini(&r);
		hwstub_clear_memory_binding();
		free(stub_mem);
		return 1;
	}

	word end_addr = (addr + length + 1) & 0177776;

	while (addr < end_addr) {
		word start_addr = addr;
		char *dis_str = disas(&r, &addr, out);
		printf("%06o ", start_addr);
		int i = 0;
		for (word a = start_addr; a < addr; a += 2) {
			printf("%06o ", r.load_word(&r, a));
			i++;
		}
		while (i < 3) {
			printf("       ");
			i++;
		}
		printf("%s\n", dis_str);
	}

	r.fini(&r);
	hwstub_clear_memory_binding();
	free(stub_mem);

	return 0;
}
