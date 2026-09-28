/* Guest-level EA/EIS differential fixtures for core/core.c and tb_integer.v. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/core.h"
#include "core/hardware.h"

static byte memory[1u << 22];
static regs cpu;
static unsigned addresses[256], values[256], probes[64];
static unsigned nwords, nprobes, pos, cases, nins;
static unsigned random_state = 0x13579bdf;

static void put(unsigned a, word v)
{
	memory[a] = v;
	memory[a + 1] = v >> 8;
	for (unsigned i = 0; i < nwords; i++) {
		if (addresses[i] == a) {
			values[i] = v;
			return;
		}
	}
	if (nwords == 256) {
		abort();
	}
	addresses[nwords] = a;
	values[nwords++] = v;
}

static void data(unsigned a, word v)
{
	put(a, v);
	for (unsigned i = 0; i < nprobes; i++) {
		if (probes[i] == a) {
			return;
		}
	}
	if (nprobes == 64) {
		abort();
	}
	probes[nprobes++] = a;
}

static void emit(word v)
{
	put(pos, v);
	pos += 2;
}

static void reg(unsigned r, word v)
{
	emit(012700 | r);
	emit(v);
	nins++;
}

static void csr(word address, word value)
{
	emit(012737);
	emit(value);
	emit(address);
	nins++;
}

static void begin(unsigned flags)
{
	memset(memory, 0, 65536);
	memset(&cpu, 0, sizeof cpu);
	hwstub_set_memory(memory, sizeof memory);
	cpu.model = DCJ11;
	hwstub_connect(&cpu);
	if (core_init(&cpu)) {
		abort();
	}
	core_reset(&cpu);
	cpu.r[7] = pos = 04000;
	nwords = nprobes = nins = 0;
	for (unsigned r = 0; r < 7; r++) {
		reg(r, r == 6 ? 06000 : 0);
	}
	emit(012737);
	emit(flags & 017);
	emit(0177776);
	nins++;
	for (unsigned a = 4; a <= 034; a += 4) {
		put(a, 07000);
		put(a + 2, 0340);
	}
	put(07000, 1);
	data(05774, 0);
	data(05776, 0);
}

static void finish(const char *name)
{
	nins++;
	for (unsigned i = 0; i < nins; i++) {
		/* core_step clears a completed abort on a separate call without
		 * fetching an instruction. The RTL fixture counts fetches only. */
		if (cpu.fAbort && core_step(&cpu)) {
			abort();
		}
		if (core_step(&cpu)) {
			abort();
		}
	}
	printf("%x %x %x %x %s\n", cases++, nwords, nprobes, nins, name);
	for (unsigned i = 0; i < nwords; i++) {
		printf("%x %x\n", addresses[i], values[i]);
	}
	for (unsigned i = 0; i < 8; i++) {
		printf("%x ", cpu.r[i]);
	}
	printf("%x %x %x\n", cpu.psw, cpu.J11_CPUERR, cpu.J11_PIRQ);
	for (unsigned i = 0; i < nprobes; i++) {
		unsigned a = probes[i];
		printf("%x %x\n", a, memory[a] | ((word)memory[a + 1] << 8));
	}
	core_fini(&cpu);
}

static word random_word(void)
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 17;
	random_state ^= random_state << 5;
	return random_state;
}

static void operand(unsigned r, unsigned mode, unsigned base, word value, unsigned byte_op)
{
	unsigned step = byte_op ? 1 : 2;
	reg(r, mode == 0 ? value : mode == 4 ? base + step : mode == 5 ? base + 2 : base);
	data(base, mode == 3 || mode == 5 ? base + 0200 : value);
	data(base + 0200, mode == 7 ? base + 0400 : value);
	data(base + 0400, value);
}

int main(int argc, char **argv)
{
	int combined_abort = argc == 2 && strcmp(argv[1], "--combined-abort") == 0;
	if (argc > 2 || (argc == 2 && !combined_abort)) {
		fprintf(stderr, "usage: %s [--combined-abort]\n", argv[0]);
		return 2;
	}
	static const word ops[] = {010000, 020000, 030000, 040000, 050000, 060000,
	                           0110000, 0120000, 0130000, 0140000, 0150000, 0160000
	                          };
	static const word edges[] = {0, 1, 2, 0177, 0377, 077777, 0100000, 0177777};
	for (unsigned op = 0; op < sizeof ops / sizeof *ops; op++) {
		unsigned byte_op = ops[op] >= 0110000 && ops[op] <= 0150000;
		for (unsigned sm = 0; sm < 8; sm++) {
			for (unsigned dm = 0; dm < 8; dm++) {
				for (unsigned v = 0; v < 4; v++) {
					begin(v * 5);
					operand(0, sm, 01000, edges[v * 2], byte_op);
					operand(1, dm, 02000, edges[v * 2 + 1], byte_op);
					emit(ops[op] | ((sm << 3) << 6) | (dm << 3) | 1);
					if (sm >= 6) {
						emit(0200);
					}
					if (dm >= 6) {
						emit(0200);
					}
					finish("two-operand-modes");
				}
				/* Both fields name R0: source sampling must follow J11 EA order. */
				begin(017);
				reg(0, 01000);
				static const unsigned alias_addresses[] = {
					0776, 01000, 01002, 01004, 01176, 01200, 01202, 01204, 01376, 01400, 01402, 01404
				};
				for (unsigned i = 0; i < sizeof alias_addresses / sizeof *alias_addresses; i++) {
					unsigned a = alias_addresses[i];
					data(a, a < 01200 ? 01200 : 01400);
				}
				emit(ops[op] | ((sm << 3) << 6) | (dm << 3));
				if (sm >= 6) {
					emit(0200);
				}
				if (dm >= 6) {
					emit(0200);
				}
				finish("two-operand-alias");
			}
		}
	}
	for (unsigned odd = 0; odd < 2; odd++) {
		unsigned r = 2 + odd;
		for (unsigned a = 0; a < 8; a++) {
			for (unsigned b = 0; b < 8; b++) {
				begin(017);
				reg(r, edges[a]);
				reg(0, edges[b]);
				emit(070000 | (r << 6));
				finish("mul-boundary");
				for (unsigned c = 0; c < 8; c++) {
					begin(017);
					reg(r, edges[a]);
					reg(r | 1, edges[b]);
					reg(0, edges[c]);
					emit(071000 | (r << 6));
					finish("div-boundary");
				}
			}
			for (unsigned count = 0; count < 64; count++) {
				for (unsigned pair = 0; pair < 2; pair++) {
					begin(017);
					reg(r, edges[a]);
					reg(r | 1, edges[(a + 3) & 7]);
					reg(0, count);
					emit((pair ? 073000 : 072000) | (r << 6));
					finish("shift-count");
				}
			}
		}
	}
	for (unsigned i = 0; i < 512; i++) {
		unsigned r = 2 + (i & 1), mode = (i >> 3) & 7;
		begin(i & 017);
		operand(0, mode, 01000, random_word(), 0);
		reg(r, random_word());
		reg(r | 1, random_word());
		emit((070000 + ((i >> 1) & 3) * 01000) | (r << 6) | (mode << 3));
		if (mode >= 6) {
			emit(0200);
		}
		finish("eis-random-ea");
	}
	/* Abort inside the nested destination EA call, or at the final write.
	 * The trap handler copies frozen restart metadata into ordinary registers. */
	for (unsigned protection = 0; protection < 2; protection++) {
		for (unsigned op = 0; op < 3; op++) {
			for (unsigned sm = 0; sm < 8; sm++) {
				for (unsigned dm = 1; dm < 8; dm++) {
					begin(017);
					operand(0, sm, 01000, 012345, op == 2);
					operand(1, dm, 041000, 043000, op == 2);
					csr(0172300, 077406);
					csr(0172340, 0);
					csr(0172316, 077406);
					csr(0172356, 0177600);
					/* Isolate protection/nonresident from length by default.
					 * The optional combined case reproduces the C-core's
					 * single-cause MMR0 difference from RTL and SIMH. */
					csr(0172304, protection ? 077402 : combined_abort ? 0 : 077400);
					csr(0172344, 0400);
					csr(0177572, 1);
					put(0250, 07000);
					put(0252, 0340);
					put(07000, 013704);
					put(07002, 0177574);
					put(07004, 013705);
					put(07006, 0177576);
					put(07010, 013703);
					put(07012, 0177572);
					emit((op == 0 ? 010000 : op == 1 ? 060000 : 0110000) |
					     ((sm << 3) << 6) | (dm << 3) | 1);
					if (sm >= 6) {
						emit(0200);
					}
					if (dm >= 6) {
						emit(0200);
					}
					nins += 3;
					finish("destination-abort-mmr");
				}
			}
		}
	}
	fprintf(stderr, "DCJ11 EA/EIS reference: %u programs\n", cases);
	return 0;
}
