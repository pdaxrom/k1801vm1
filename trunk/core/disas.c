/*
 * disas.c
 *
 *  Created on: 19.10.2016
 *      Author: Alexander Chukov <sash@pdaXrom.org>
 */

#include <stdio.h>
#include "core.h"

enum {
	NONE = 0,
	SOP,		/* single operand */
	DOP,		/* double operand */
	RD,			/* register destination */
	RS,			/* register source */
	BRN,		/* branch */
	SRG,		/* single register */
	ROF,		/* register offset */
	EMT,		/* EMT and TRAP */
	FLG,		/* flags */
	NN			/* numeric */
};

static const struct _OPCODE {
	char *name;
	word code;
	byte type;
	byte mode;
} OPS[] = {
	{ "HALT",	0000000, NONE, 0 },
	{ "WAIT",	0000001, NONE, 0 },
	{ "RTI",	0000002, NONE, 0 },
	{ "BPT",	0000003, NONE, 0 },
	{ "IOT",	0000004, NONE, 0 },
	{ "RESET",	0000005, NONE, 0 },
	{ "RTT",	0000006, NONE, 0 },
	{ "NOP",	0000240, NONE, 0 },
	{ "CLC",	0000241, NONE, 0 },
	{ "CLV",	0000242, NONE, 0 },
	{ "CLZ",	0000244, NONE, 0 },
	{ "CLN",	0000250, NONE, 0 },
	{ "CCC",	0000257, NONE, 0 },
	{ "SEC",	0000261, NONE, 0 },
	{ "SEV",	0000262, NONE, 0 },
	{ "SEZ",	0000264, NONE, 0 },
	{ "SEN",	0000270, NONE, 0 },
	{ "SCC",	0000277, NONE, 0 },

	/* К1806ВМ2 */
	{ "START",		0000012, NONE, 0 },
	{ "STEP",	0000016, NONE, 0 },
	{ "RSEL",	0000020, NONE, 0 },
	{ "MFUS",	0000021, NONE, 0 },
	{ "RCPC",	0000022, NONE, 0 },
	{ "RCPS",	0000024, NONE, 0 },
	{ "MTUS",	0000031, NONE, 0 },
	{ "WCPC",	0000032, NONE, 0 },
	{ "WCPS",	0000034, NONE, 0 },
	/* */

	{ "JMP",	0000100, SOP, 0 },
	{ "CLR",	0005000, SOP, 1 },
	{ "COM",	0005100, SOP, 1 },
	{ "INC",	0005200, SOP, 1 },
	{ "DEC",	0005300, SOP, 1 },
	{ "NEG",	0005400, SOP, 1 },
	{ "ADC",	0005500, SOP, 1 },
	{ "SBC",	0005600, SOP, 1 },
	{ "TST",	0005700, SOP, 1 },
	{ "ROR",	0006000, SOP, 1 },
	{ "ROL",	0006100, SOP, 1 },
	{ "ASR",	0006200, SOP, 1 },
	{ "ASL",	0006300, SOP, 1 },
	{ "SWAB",	0000300, SOP, 0 },
	{ "SXT",	0006700, SOP, 0 },
	{ "MTPS",	0106400, SOP, 0 },
	{ "MFPS",	0106700, SOP, 0 },

	{ "MOV",	0010000, DOP, 1 },
	{ "CMP",	0020000, DOP, 1 },
	{ "BIT",	0030000, DOP, 1 },
	{ "BIC",	0040000, DOP, 1 },
	{ "BIS",	0050000, DOP, 1 },
	{ "ADD",	0060000, DOP, 0 },
	{ "SUB",	0160000, DOP, 0 },

	{ "MUL",	0070000, RS, 0 },
	{ "DIV",	0071000, RS, 0 },
	{ "ASH",	0072000, RS, 0 },
	{ "ASHC",	0073000, RS, 0 },

	{ "JSR",	0004000, RD, 0 },
	{ "XOR",	0074000, RD, 0 },

	{ "RTS",    0000200, SRG, 0 },

	{ "SOB",	0077000, ROF, 0 },

	{ "MARK",	0006400, NN,  0 },

	{ "EMT",	0104000, EMT, 0 },
	{ "TRAP",	0104400, EMT, 0 },

	{ "BR",		0000400, BRN, 0 },
	{ "BNE",	0001000, BRN, 0 },
	{ "BEQ",	0001400, BRN, 0 },
	{ "BPL",	0100000, BRN, 0 },
	{ "BMI",	0100400, BRN, 0 },
	{ "BVC",	0102000, BRN, 0 },
	{ "BVS",	0102400, BRN, 0 },
	{ "BGE",	0002000, BRN, 0 },
	{ "BLT",	0002400, BRN, 0 },
	{ "BGT",	0003000, BRN, 0 },
	{ "BLE",	0003400, BRN, 0 },
	{ "BHI",	0101000, BRN, 0 },
	{ "BLOS",	0101400, BRN, 0 },
	{ "BCC",	0103000, BRN, 0 },
	{ "BCS",	0103400, BRN, 0 },
};

static const char *REG[] = {
	"R0", "R1", "R2", "R3", "R4", "R5", "SP", "PC"
};

static char *decode_operand(regs *r, word *addr, byte operand, char *out)
{
	byte mode = operand >> 3;
	byte reg = operand & 07;

	word data = 0;

	if (mode == 6 || mode == 7 || operand == 027 || operand == 037) {
		data = r->mem[*addr] | (r->mem[*addr + 1] << 8);
		*addr += 2;
	}

	switch (operand) {
	case 027:
		sprintf(out, "#%o", data);
		return out;
	case 037:
		sprintf(out, "@#%o", data);
		return out;
	case 067: {
		word tmp = data + *addr;
		sprintf(out, "%o", tmp);
		}
		return out;
	case 077: {
		word tmp = data + *addr;
		sprintf(out, "@%o", tmp);
		}
		return out;
	}

	switch (mode) {
	case 0:
		sprintf(out, "%s", REG[reg]);
		return out;
	case 1:
		sprintf(out, "(%s)", REG[reg]);
		return out;
	case 2:
		sprintf(out, "(%s)+", REG[reg]);
		return out;
	case 3:
		sprintf(out, "@(%s)+", REG[reg]);
		return out;
	case 4:
		sprintf(out, "-(%s)", REG[reg]);
		return out;
	case 5:
		sprintf(out, "@-(%s)", REG[reg]);
		return out;
	case 6:
		sprintf(out, "%o(%s)", data, REG[reg]);
		return out;
	case 7:
		sprintf(out, "@%o(%s)", data, REG[reg]);
		return out;
	}

	return NULL;
}

char *disas(regs *r, word *addr, char *out)
{
	char tmpbuf[256];
	char tmpbuf2[256];
	word instr = (r->mem[*addr + 1] << 8) | r->mem[*addr];

	*addr += 2;

	sprintf(out, "UNKNOWN");

	int i;

	for (i = 0; i < sizeof(OPS) / sizeof(struct _OPCODE); i++) {
		char *mode = "";
		if ((OPS[i].mode == 1) && (instr & 0100000)) {
			mode = "B";
		}

		if ((OPS[i].type == NONE) && (OPS[i].code == instr)) {
			sprintf(out, "%s", OPS[i].name);
			break;
		}

		if ((OPS[i].type == SOP) && (OPS[i].code == (instr & 0077700)) && (OPS[i].mode == 1)) {
			sprintf(out, "%s%s\t%s", OPS[i].name, mode,
					decode_operand(r, addr, instr & 077, tmpbuf));
			break;
		}

		if ((OPS[i].type == SOP) && (OPS[i].code == (instr & 0177700)) && (OPS[i].mode == 0)) {
			sprintf(out, "%s\t%s", OPS[i].name,
					decode_operand(r, addr, instr & 077, tmpbuf));
			break;
		}

		if ((OPS[i].type == DOP) && (OPS[i].code == (instr & 0070000)) && (OPS[i].mode == 1)) {
			decode_operand(r, addr, (instr >> 6) & 077, tmpbuf);
			decode_operand(r, addr, instr & 077, tmpbuf2);
			sprintf(out, "%s%s\t%s,%s", OPS[i].name, mode,
					tmpbuf, tmpbuf2);
			break;
		}

		if ((OPS[i].type == DOP) && (OPS[i].code == (instr & 0170000)) && (OPS[i].mode == 0)) {
			decode_operand(r, addr, (instr >> 6) & 077, tmpbuf);
			decode_operand(r, addr, instr & 077, tmpbuf2);
			sprintf(out, "%s\t%s,%s", OPS[i].name,
					tmpbuf, tmpbuf2);
			break;
		}

		if ((OPS[i].type == RS) && (OPS[i].code == (instr & 0177000))) {
			sprintf(out, "%s\t%s,%s", OPS[i].name,
					decode_operand(r, addr, instr & 077, tmpbuf),
					REG[(instr >> 6) & 07]
					);
			break;
		}

		if ((OPS[i].type == RD) && (OPS[i].code == (instr & 0177000))) {
			sprintf(out, "%s\t%s,%s", OPS[i].name,
					REG[(instr >> 6) & 07],
					decode_operand(r, addr, instr & 077, tmpbuf));
			break;
		}

		if ((OPS[i].type == SRG) && (OPS[i].code == (instr & 0177770))) {
			sprintf(out, "%s\t%s", OPS[i].name, REG[instr & 07]);
			break;
		}

		if ((OPS[i].type == ROF) && (OPS[i].code == (instr & 0177000))) {
			word tmp = *addr - ((instr & 077) << 1);
			sprintf(out, "%s\t%s,%o", OPS[i].name, REG[(instr >> 6) & 07], tmp);
			break;
		}

		if ((OPS[i].type == NN) && (OPS[i].code == (instr & 0177700))) {
			sprintf(out, "%s%o", OPS[i].name, instr & 077);
			break;
		}

		if ((OPS[i].type == EMT) && (OPS[i].code == (instr & 0177400))) {
			sprintf(out, "%s\t%o", OPS[i].name, instr & 0377);
			break;
		}

		if ((OPS[i].type == BRN) && (OPS[i].code == (instr & 0177400))) {
			word tmp = instr & 0377;
			if (tmp & 0200) {
				tmp += 0177400;
			}
			tmp = tmp * 2 + *addr;
			sprintf(out, "%s\t%06o", OPS[i].name, tmp);
			break;
		}
	}

	return out;
}
