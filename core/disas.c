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
    SOP,        /* single operand */
    DOP,        /* double operand */
    RD,         /* register destination */
    RS,         /* register source */
    BRN,        /* branch */
    SRG,        /* single register */
    ROF,        /* register offset */
    EMT,        /* EMT and TRAP */
    FLG,        /* flags */
    NN,         /* numeric */
    SPLN,       /* SPL n */
    FIS,        /* FIS */
    FP11_F1,
    FP11_F2,
    FP11_F3,
    FP11_F4,
    FP11_F5
};

enum {
    NAME_FIXED = 0,
    NAME_DF,
    NAME_LDCIF,
    NAME_STCFI,
    NAME_LDCFF,
    NAME_STCFF
};

enum {
    OPF_NONE = 0,
    OPF_BYTE_SUFFIX = 1 << 0,
    OPF_AC_FIRST = 1 << 1
};

static const struct _OPCODE {
    const char *name;
    word code;
    word mask;
    byte type;
    byte name_type;
    byte flags;
} OPS[] = {
    { "HALT", 0000000, 0177777, NONE, NAME_FIXED, OPF_NONE },
    { "WAIT", 0000001, 0177777, NONE, NAME_FIXED, OPF_NONE },
    { "RTI", 0000002, 0177777, NONE, NAME_FIXED, OPF_NONE },
    { "BPT", 0000003, 0177777, NONE, NAME_FIXED, OPF_NONE },
    { "IOT", 0000004, 0177777, NONE, NAME_FIXED, OPF_NONE },
    { "RESET", 0000005, 0177777, NONE, NAME_FIXED, OPF_NONE },
    { "RTT", 0000006, 0177777, NONE, NAME_FIXED, OPF_NONE },
    { "MFPT", 0000007, 0177777, NONE, NAME_FIXED, OPF_NONE },
    { "NOP", 0000240, 0177777, NONE, NAME_FIXED, OPF_NONE },
    { "CLC", 0000241, 0177777, NONE, NAME_FIXED, OPF_NONE },
    { "CLV", 0000242, 0177777, NONE, NAME_FIXED, OPF_NONE },
    { "CLZ", 0000244, 0177777, NONE, NAME_FIXED, OPF_NONE },
    { "CLN", 0000250, 0177777, NONE, NAME_FIXED, OPF_NONE },
    { "CCC", 0000257, 0177777, NONE, NAME_FIXED, OPF_NONE },
    { "NOP", 0000260, 0177777, NONE, NAME_FIXED, OPF_NONE },
    { "SEC", 0000261, 0177777, NONE, NAME_FIXED, OPF_NONE },
    { "SEV", 0000262, 0177777, NONE, NAME_FIXED, OPF_NONE },
    { "SEZ", 0000264, 0177777, NONE, NAME_FIXED, OPF_NONE },
    { "SEN", 0000270, 0177777, NONE, NAME_FIXED, OPF_NONE },
    { "SCC", 0000277, 0177777, NONE, NAME_FIXED, OPF_NONE },
    { "CL", 0000240, 0177760, FLG, NAME_FIXED, OPF_NONE },
    { "SE", 0000260, 0177760, FLG, NAME_FIXED, OPF_NONE },

    /* K1806VM2 */
    { "START", 0000012, 0177777, NONE, NAME_FIXED, OPF_NONE },
    { "STEP", 0000016, 0177777, NONE, NAME_FIXED, OPF_NONE },
    { "RSEL", 0000020, 0177777, NONE, NAME_FIXED, OPF_NONE },
    { "MFUS", 0000021, 0177777, NONE, NAME_FIXED, OPF_NONE },
    { "RCPC", 0000022, 0177777, NONE, NAME_FIXED, OPF_NONE },
    { "RCPS", 0000024, 0177777, NONE, NAME_FIXED, OPF_NONE },
    { "MTUS", 0000031, 0177777, NONE, NAME_FIXED, OPF_NONE },
    { "WCPC", 0000032, 0177777, NONE, NAME_FIXED, OPF_NONE },
    { "WCPS", 0000034, 0177777, NONE, NAME_FIXED, OPF_NONE },

    { "JMP", 0000100, 0177700, SOP, NAME_FIXED, OPF_NONE },
    { "CLR", 0005000, 0077700, SOP, NAME_FIXED, OPF_BYTE_SUFFIX },
    { "COM", 0005100, 0077700, SOP, NAME_FIXED, OPF_BYTE_SUFFIX },
    { "INC", 0005200, 0077700, SOP, NAME_FIXED, OPF_BYTE_SUFFIX },
    { "DEC", 0005300, 0077700, SOP, NAME_FIXED, OPF_BYTE_SUFFIX },
    { "NEG", 0005400, 0077700, SOP, NAME_FIXED, OPF_BYTE_SUFFIX },
    { "ADC", 0005500, 0077700, SOP, NAME_FIXED, OPF_BYTE_SUFFIX },
    { "SBC", 0005600, 0077700, SOP, NAME_FIXED, OPF_BYTE_SUFFIX },
    { "TST", 0005700, 0077700, SOP, NAME_FIXED, OPF_BYTE_SUFFIX },
    { "ROR", 0006000, 0077700, SOP, NAME_FIXED, OPF_BYTE_SUFFIX },
    { "ROL", 0006100, 0077700, SOP, NAME_FIXED, OPF_BYTE_SUFFIX },
    { "ASR", 0006200, 0077700, SOP, NAME_FIXED, OPF_BYTE_SUFFIX },
    { "ASL", 0006300, 0077700, SOP, NAME_FIXED, OPF_BYTE_SUFFIX },
    { "SWAB", 0000300, 0177700, SOP, NAME_FIXED, OPF_NONE },
    { "SXT", 0006700, 0177700, SOP, NAME_FIXED, OPF_NONE },
    { "MTPS", 0106400, 0177700, SOP, NAME_FIXED, OPF_NONE },
    { "MFPS", 0106700, 0177700, SOP, NAME_FIXED, OPF_NONE },
    { "MFPD", 0006500, 0177700, SOP, NAME_FIXED, OPF_NONE },
    { "MFPI", 0106500, 0177700, SOP, NAME_FIXED, OPF_NONE },
    { "MTPI", 0006600, 0177700, SOP, NAME_FIXED, OPF_NONE },
    { "MTPD", 0106600, 0177700, SOP, NAME_FIXED, OPF_NONE },
    { "TSTSET", 0007200, 0177700, SOP, NAME_FIXED, OPF_NONE },
    { "WRTLCK", 0007300, 0177700, SOP, NAME_FIXED, OPF_NONE },

    { "MOV", 0010000, 0070000, DOP, NAME_FIXED, OPF_BYTE_SUFFIX },
    { "CMP", 0020000, 0070000, DOP, NAME_FIXED, OPF_BYTE_SUFFIX },
    { "BIT", 0030000, 0070000, DOP, NAME_FIXED, OPF_BYTE_SUFFIX },
    { "BIC", 0040000, 0070000, DOP, NAME_FIXED, OPF_BYTE_SUFFIX },
    { "BIS", 0050000, 0070000, DOP, NAME_FIXED, OPF_BYTE_SUFFIX },
    { "ADD", 0060000, 0170000, DOP, NAME_FIXED, OPF_NONE },
    { "SUB", 0160000, 0170000, DOP, NAME_FIXED, OPF_NONE },

    { "MUL", 0070000, 0177000, RS, NAME_FIXED, OPF_NONE },
    { "DIV", 0071000, 0177000, RS, NAME_FIXED, OPF_NONE },
    { "ASH", 0072000, 0177000, RS, NAME_FIXED, OPF_NONE },
    { "ASHC", 0073000, 0177000, RS, NAME_FIXED, OPF_NONE },

    { "FADD", 0075000, 0177070, FIS, NAME_FIXED, OPF_NONE },
    { "FSUB", 0075010, 0177070, FIS, NAME_FIXED, OPF_NONE },
    { "FMUL", 0075020, 0177070, FIS, NAME_FIXED, OPF_NONE },
    { "FDIV", 0075030, 0177070, FIS, NAME_FIXED, OPF_NONE },

    { "JSR", 0004000, 0177000, RD, NAME_FIXED, OPF_NONE },
    { "XOR", 0074000, 0177000, RD, NAME_FIXED, OPF_NONE },

    { "RTS", 0000200, 0177770, SRG, NAME_FIXED, OPF_NONE },

    { "SOB", 0077000, 0177000, ROF, NAME_FIXED, OPF_NONE },

    { "MARK", 0006400, 0177700, NN, NAME_FIXED, OPF_NONE },
    { "SPL", 0000230, 0177770, SPLN, NAME_FIXED, OPF_NONE },

    { "EMT", 0104000, 0177400, EMT, NAME_FIXED, OPF_NONE },
    { "TRAP", 0104400, 0177400, EMT, NAME_FIXED, OPF_NONE },

    { "BR", 0000400, 0177400, BRN, NAME_FIXED, OPF_NONE },
    { "BNE", 0001000, 0177400, BRN, NAME_FIXED, OPF_NONE },
    { "BEQ", 0001400, 0177400, BRN, NAME_FIXED, OPF_NONE },
    { "BPL", 0100000, 0177400, BRN, NAME_FIXED, OPF_NONE },
    { "BMI", 0100400, 0177400, BRN, NAME_FIXED, OPF_NONE },
    { "BVC", 0102000, 0177400, BRN, NAME_FIXED, OPF_NONE },
    { "BVS", 0102400, 0177400, BRN, NAME_FIXED, OPF_NONE },
    { "BGE", 0002000, 0177400, BRN, NAME_FIXED, OPF_NONE },
    { "BLT", 0002400, 0177400, BRN, NAME_FIXED, OPF_NONE },
    { "BGT", 0003000, 0177400, BRN, NAME_FIXED, OPF_NONE },
    { "BLE", 0003400, 0177400, BRN, NAME_FIXED, OPF_NONE },
    { "BHI", 0101000, 0177400, BRN, NAME_FIXED, OPF_NONE },
    { "BLOS", 0101400, 0177400, BRN, NAME_FIXED, OPF_NONE },
    { "BCC", 0103000, 0177400, BRN, NAME_FIXED, OPF_NONE },
    { "BCS", 0103400, 0177400, BRN, NAME_FIXED, OPF_NONE },

    /* FP11/FPP */
    { "CFCC", 0170000, 0177777, FP11_F5, NAME_FIXED, OPF_NONE },
    { "SETF", 0170001, 0177777, FP11_F5, NAME_FIXED, OPF_NONE },
    { "SETI", 0170002, 0177777, FP11_F5, NAME_FIXED, OPF_NONE },
    { "SETD", 0170011, 0177777, FP11_F5, NAME_FIXED, OPF_NONE },
    { "SETL", 0170012, 0177777, FP11_F5, NAME_FIXED, OPF_NONE },

    { "LDFPS", 0170100, 0177700, FP11_F4, NAME_FIXED, OPF_NONE },
    { "STFPS", 0170200, 0177700, FP11_F4, NAME_FIXED, OPF_NONE },
    { "STST", 0170300, 0177700, FP11_F4, NAME_FIXED, OPF_NONE },

    { "CLR", 0170400, 0177700, FP11_F2, NAME_DF, OPF_NONE },
    { "TST", 0170500, 0177700, FP11_F2, NAME_DF, OPF_NONE },
    { "ABS", 0170600, 0177700, FP11_F2, NAME_DF, OPF_NONE },
    { "NEG", 0170700, 0177700, FP11_F2, NAME_DF, OPF_NONE },

    { "MUL", 0171000, 0177400, FP11_F1, NAME_DF, OPF_NONE },
    { "MOD", 0171400, 0177400, FP11_F1, NAME_DF, OPF_NONE },
    { "ADD", 0172000, 0177400, FP11_F1, NAME_DF, OPF_NONE },
    { "LD", 0172400, 0177400, FP11_F1, NAME_DF, OPF_NONE },
    { "SUB", 0173000, 0177400, FP11_F1, NAME_DF, OPF_NONE },
    { "CMP", 0173400, 0177400, FP11_F1, NAME_DF, OPF_NONE },
    { "ST", 0174000, 0177400, FP11_F1, NAME_DF, OPF_AC_FIRST },
    { "DIV", 0174400, 0177400, FP11_F1, NAME_DF, OPF_NONE },
    { NULL, 0176000, 0177400, FP11_F1, NAME_STCFF, OPF_AC_FIRST },
    { NULL, 0177400, 0177400, FP11_F1, NAME_LDCFF, OPF_NONE },

    { "STEXP", 0175000, 0177400, FP11_F3, NAME_FIXED, OPF_AC_FIRST },
    { NULL, 0175400, 0177400, FP11_F3, NAME_STCFI, OPF_AC_FIRST },
    { "LDEXP", 0176400, 0177400, FP11_F3, NAME_FIXED, OPF_NONE },
    { NULL, 0177000, 0177400, FP11_F3, NAME_LDCIF, OPF_NONE },
};

static const char *const REG[] = {
    "R0", "R1", "R2", "R3", "R4", "R5", "SP", "PC"
};

static const char *const FPREG[] = {
    "AC0", "AC1", "AC2", "AC3", "AC4", "AC5", "AC6", "AC7"
};

#define DISAS_OPERAND_BUFSZ 32

#define FP11_FPS_D 0000200
#define FP11_FPS_L 0000100

static char *decode_operand(regs *r, word *addr, byte operand, char *out)
{
    byte mode = operand >> 3;
    byte reg = operand & 07;

    word data = 0;

    if (mode == 6 || mode == 7 || operand == 027 || operand == 037) {
        data = r->load_word(r, *addr);
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

static char *decode_fp_operand(regs *r, word *addr, byte operand, char *out)
{
    if ((operand >> 3) == 0) {
        sprintf(out, "%s", FPREG[operand & 07]);
        return out;
    }

    return decode_operand(r, addr, operand, out);
}

static const char *fp11_df_suffix(const regs *r)
{
    return (r->fpu_fps & FP11_FPS_D) ? "D" : "F";
}

static const char *fp11_ldcif_name(const regs *r)
{
    int d = (r->fpu_fps & FP11_FPS_D) != 0;
    int l = (r->fpu_fps & FP11_FPS_L) != 0;

    if (l) {
        return d ? "LDCLD" : "LDCLF";
    }

    return d ? "LDCID" : "LDCIF";
}

static const char *fp11_stcfi_name(const regs *r)
{
    int d = (r->fpu_fps & FP11_FPS_D) != 0;
    int l = (r->fpu_fps & FP11_FPS_L) != 0;

    if (d) {
        return l ? "STCDL" : "STCDI";
    }

    return l ? "STCFL" : "STCFI";
}

static const char *fp11_ldcff_name(const regs *r)
{
    return (r->fpu_fps & FP11_FPS_D) ? "LDCDF" : "LDCFD";
}

static const char *fp11_stcff_name(const regs *r)
{
    return (r->fpu_fps & FP11_FPS_D) ? "STCDF" : "STCFD";
}

static const char *opcode_name(const regs *r, const struct _OPCODE *op,
                               word instr, char *name_buf, size_t name_buf_sz)
{
    switch (op->name_type) {
    case NAME_FIXED:
        if (!op->name) {
            return NULL;
        }
        if ((op->flags & OPF_BYTE_SUFFIX) && (instr & 0100000)) {
            snprintf(name_buf, name_buf_sz, "%sB", op->name);
            return name_buf;
        }
        return op->name;
    case NAME_DF:
        snprintf(name_buf, name_buf_sz, "%s%s", op->name, fp11_df_suffix(r));
        return name_buf;
    case NAME_LDCIF:
        return fp11_ldcif_name(r);
    case NAME_STCFI:
        return fp11_stcfi_name(r);
    case NAME_LDCFF:
        return fp11_ldcff_name(r);
    case NAME_STCFF:
        return fp11_stcff_name(r);
    }

    return NULL;
}

char *disas(regs *r, word *addr, char *out)
{
    char tmpbuf[DISAS_OPERAND_BUFSZ];
    char tmpbuf2[DISAS_OPERAND_BUFSZ];
    word instr = r->load_word(r, *addr);
    const struct _OPCODE *op = NULL;
    const char *name;
    char name_buf[32];

    *addr += 2;

    for (size_t i = 0; i < sizeof(OPS) / sizeof(OPS[0]); i++) {
        if ((instr & OPS[i].mask) == OPS[i].code) {
            op = &OPS[i];
            break;
        }
    }

    if (!op) {
        sprintf(out, "UNKNOWN [0%0o]", instr);
        return out;
    }

    name = opcode_name(r, op, instr, name_buf, sizeof(name_buf));
    if (!name) {
        sprintf(out, "UNKNOWN [0%0o]", instr);
        return out;
    }

    switch (op->type) {
    case NONE:
        sprintf(out, "%s", name);
        break;
    case FLG: {
        char *p = out;
        const char *sep = "";
        word mask = instr & 017;

        if (mask == 0) {
            sprintf(out, "UNKNOWN [0%0o]", instr);
            break;
        }

        if (mask & FLAG_N) {
            p += sprintf(p, "%s%sN", sep, name);
            sep = "|";
        }
        if (mask & FLAG_Z) {
            p += sprintf(p, "%s%sZ", sep, name);
            sep = "|";
        }
        if (mask & FLAG_V) {
            p += sprintf(p, "%s%sV", sep, name);
            sep = "|";
        }
        if (mask & FLAG_C) {
            sprintf(p, "%s%sC", sep, name);
        }
    }
    break;
    case SOP:
        sprintf(out, "%s\t%s", name, decode_operand(r, addr, instr & 077, tmpbuf));
        break;
    case DOP:
        decode_operand(r, addr, (instr >> 6) & 077, tmpbuf);
        decode_operand(r, addr, instr & 077, tmpbuf2);
        sprintf(out, "%s\t%s,%s", name, tmpbuf, tmpbuf2);
        break;
    case RS:
        sprintf(out, "%s\t%s,%s", name,
                decode_operand(r, addr, instr & 077, tmpbuf),
                REG[(instr >> 6) & 07]);
        break;
    case FIS:
        sprintf(out, "%s\t%s", name, REG[instr & 07]);
        break;
    case RD:
        sprintf(out, "%s\t%s,%s", name,
                REG[(instr >> 6) & 07],
                decode_operand(r, addr, instr & 077, tmpbuf));
        break;
    case SRG:
        sprintf(out, "%s\t%s", name, REG[instr & 07]);
        break;
    case ROF: {
        word tmp = *addr - ((instr & 077) << 1);
        sprintf(out, "%s\t%s,%o", name, REG[(instr >> 6) & 07], tmp);
    }
    break;
    case NN:
        sprintf(out, "%s%o", name, instr & 077);
        break;
    case EMT:
        sprintf(out, "%s\t%o", name, instr & 0377);
        break;
    case SPLN:
        sprintf(out, "%s\t%o", name, instr & 07);
        break;
    case BRN: {
        word tmp = instr & 0377;
        if (tmp & 0200) {
            tmp += 0177400;
        }
        tmp = tmp * 2 + *addr;
        sprintf(out, "%s\t%06o", name, tmp);
    }
    break;
    case FP11_F5:
        sprintf(out, "%s", name);
        break;
    case FP11_F4:
        sprintf(out, "%s\t%s", name, decode_operand(r, addr, instr & 077, tmpbuf));
        break;
    case FP11_F2:
        sprintf(out, "%s\t%s", name, decode_fp_operand(r, addr, instr & 077, tmpbuf));
        break;
    case FP11_F1: {
        byte ac = (instr >> 6) & 03;
        if (op->flags & OPF_AC_FIRST) {
            sprintf(out, "%s\t%s,%s", name, FPREG[ac],
                    decode_fp_operand(r, addr, instr & 077, tmpbuf));
        } else {
            sprintf(out, "%s\t%s,%s", name,
                    decode_fp_operand(r, addr, instr & 077, tmpbuf), FPREG[ac]);
        }
    }
    break;
    case FP11_F3: {
        byte ac = (instr >> 6) & 03;
        if (op->flags & OPF_AC_FIRST) {
            sprintf(out, "%s\t%s,%s", name, FPREG[ac],
                    decode_operand(r, addr, instr & 077, tmpbuf));
        } else {
            sprintf(out, "%s\t%s,%s", name,
                    decode_operand(r, addr, instr & 077, tmpbuf), FPREG[ac]);
        }
    }
    break;
    default:
        sprintf(out, "UNKNOWN [0%0o]", instr);
        break;
    }

    return out;
}
