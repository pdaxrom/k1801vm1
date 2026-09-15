#!/usr/bin/env python3
"""CP79 DCJ11 reference with documented numerical/bus-commit corrections.

CPU/EA and floating opcode dispatch come from the shared DCJ11 emulator.
No FP11-A unary/load exception adaptations: FIUV precedes execution.
ADD/SUB use jammed alignment; independent exact-integer tests check 7.6.
MUL/DIV use the CP74 exact 128-bit helper; MOD retains 59 product bits.
STC/STEXP retain CP76 bus-commit corrections and CPU carry correction.
Invalid-AC, immediate-read and unary-write abort corrections are marked in
fixture field 128. This is an adapted reference, not an unmodified oracle.
"""
import hashlib
import json
from pathlib import Path
from board_common import ROOT


def prepare(out):
    out.mkdir(parents=True,exist_ok=True)
    original=ROOT/'../core'
    for name in ('core.c','core.h','hardware.c','hardware.h'):
        (out/name).write_bytes((original/name).read_bytes())
    s=(original/'pdp11_fp.c').read_text()
    begin=s.index('static sdword addfp11(regs *r, fpac_t *facp, fpac_t *fsrcp)\n{')
    end=s.index('/* Floating point multiply',begin)
    helper=(ROOT/'tb/reference_fp_add_cp79.h').read_text()
    (out/'reference_fp_add_cp79.h').write_text(helper)
    s=s[:begin]+'''#include "reference_fp_add_cp79.h"
static sdword addfp11(regs *r, fpac_t *facp, fpac_t *fsrcp)
{
    fp79_sum sum=fp79_add(((uint64_t)facp->h<<32)|facp->l,
        ((uint64_t)fsrcp->h<<32)|fsrcp->l, !!(FPS&FPS_D), !!(FPS&FPS_T));
    facp->h=sum.bits>>32;facp->l=sum.bits;
    if(sum.zero)return 0;
    if(sum.exponent>255){if(fpnotrap(r,FEC_OVFLO))*facp=zero_fac;return FPS_V;}
    if(sum.exponent<=0 && fpnotrap(r,FEC_UNFLO))*facp=zero_fac;
    return 0;
}

'''+s[end:]
    # Exact 128-bit product/ratio replaces the two numeric helpers only.
    s='#include "reference_fp_muldiv_cp74.h"\n'+s
    for name,divide,next_comment in (('mulfp11',0,'/* Floating point mod'),('divfp11',1,'/* Update floating condition codes')):
        begin=s.index('static sdword '+name+'(regs *r, fpac_t *facp, fpac_t *fsrcp)\n{')
        end=s.index(next_comment,begin)
        s=s[:begin]+f'static sdword {name}(regs *r, fpac_t *facp, fpac_t *fsrcp)\n{{\n    fp74_result v=fp74_muldiv(((uint64_t)facp->h<<32)|facp->l,\n        ((uint64_t)fsrcp->h<<32)|fsrcp->l,!!(FPS&FPS_D),!!(FPS&FPS_T),{divide});\n    facp->h=v.bits>>32;facp->l=v.bits;\n    if(v.zero)return 0;\n    if(v.exponent>255){{if(fpnotrap(r,FEC_OVFLO))*facp=zero_fac;return FPS_V;}}\n    if(v.exponent<=0 && fpnotrap(r,FEC_UNFLO))*facp=zero_fac;\n    return 0;\n}}\n\n'+s[end:]
    (out/'reference_fp_muldiv_cp74.h').write_bytes((ROOT/'tb/reference_fp_muldiv_cp74.h').read_bytes())
    # Store conversions commit condition codes and deferred exceptions only
    # after successful destination accesses. Run the numeric round helper on
    # a private CPU snapshot so it cannot schedule a trap ahead of a bus fault.
    a=s.index("    case 014: /* STCff' */")
    b=s.index("    case 007: /* CMPf */",a)
    s=s[:a]+'''    case 014: /* STCff': deferred exception, atomic FPS. */
        F_LOAD(qdouble, FR[ac], fac);
        if (!GET_EXP(fac.h)) fac=zero_fac;
        newV=0;
        if ((FPS&(FPS_D+FPS_T))==FPS_D) {
            regs numeric=*r;
            newV=roundfp11(&numeric,&fac);
        }
        WriteFP(r,&fac,GeteaFP(r,dstspec,12-lenf),dstspec,12-lenf);
        if (!r->fAbort) {
            FPS=setfcc(FPS,fac.h,newV);
            if(newV) fpnotrap(r,FEC_OVFLO);
        }
        break;

'''+s[b:]
    a=s.index('    case 012: /* STEXP */')
    b=s.index('    case 016: /* LDCif */',a)
    s=s[:a]+'''    case 012: /* STEXP: bus access precedes flags. */
        dst=(GET_EXP(FR[ac].h)-FP_BIAS)&0177777;
        if(dstspec<=07)R[dstspec]=dst;
        else WriteW(r,dst,GeteaFW(r,dstspec));
        if(r->fAbort)break;
        SET_N(GET_SIGN_W(dst));SET_Z(dst==0);SET_V(0);SET_C(0);
        FPS=(FPS&~FPS_CC)|(N<<PSW_V_N)|(Z<<PSW_V_Z);
        break;

'''+s[b:]
    a=s.index('        SET_N(GET_SIGN_L(dst));',s.index('case 013:'))
    b=s.index('        break;',a)
    s=s[:a]+'''        if(dstspec<=07)R[dstspec]=(dst>>16)&0177777;
        else WriteI(r,dst,GeteaFP(r,dstspec,leni),dstspec,leni);
        if(r->fAbort)break;
        SET_N(GET_SIGN_L(dst));SET_Z(dst==0);SET_V(0);SET_C(c_flag);
        FPS=(FPS&~FPS_CC)|(N<<PSW_V_N)|(Z<<PSW_V_Z)|(C<<PSW_V_C);
        if(c_flag)fpnotrap(r,FEC_ICVT);
'''+s[b:]
    (out/'pdp11_fp.c').write_text(s)
    (out/'reference_fp_add_cp73.h').write_bytes((ROOT/'tb/reference_fp_add_cp73.h').read_bytes())
    record=dict(description=__doc__,original={str((original/n).relative_to(ROOT)):
        hashlib.sha256((original/n).read_bytes()).hexdigest() for n in ('core.c','core.h','hardware.c','hardware.h','pdp11_fp.c')},
        adapted={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(out.iterdir()) if p.suffix in ('.c','.h')})
    (out/'adaptation.json').write_text(json.dumps(record,indent=2)+'\n')
    return record
