#!/usr/bin/env python3
"""Build a private reference: unchanged CPU/EA + explicit FP11-A adaptations.

Never edit the shared emulator. Keep the original copyright in the copy.
ADD/SUB use reference_fp_add_cp73.h; ABS/NEG defer FIUV until after writes.
MUL/DIV use exact unsigned 128-bit arithmetic.
MOD keeps the original 59-bit product helper unchanged.
All remaining oracle differences retain the CP71 vector markers.
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
    s=s[:begin]+'''/* CP75 local FP11-A reference adaptation; shared source is untouched. */
#include "reference_fp_add_cp73.h"
static sdword addfp11(regs *r, fpac_t *facp, fpac_t *fsrcp)
{
    fp73_sum sum=fp73_add(((uint64_t)facp->h<<32)|facp->l,
        ((uint64_t)fsrcp->h<<32)|fsrcp->l, !!(FPS&FPS_D), !!(FPS&FPS_T), 7);
    facp->h=sum.bits>>32;facp->l=sum.bits;
    /* Exact cancellation/zero is not an underflow. */
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
    # Suppress only the operand-read UV check for ABS/NEG. Do not suppress
    # bus faults or UV checks for AC operands (which never generate FIUV).
    s=s.replace('sdword newV, exp, sign;','sdword newV, exp, sign, saved_uv;')
    for title,operation in (('ABSf','& ~FP_SIGN'),('NEGf','^ FP_SIGN')):
        start=s.index('        case '+('2' if title=='ABSf' else '3')+': /* '+title+' */')
        end=s.index('            break;',start)+len('            break;')
        s=s[:start]+f'''        case {2 if title=='ABSf' else 3}: /* {title}: FP11-A completes before UV trap. */
            saved_uv=FPS&FPS_IUV;FPS&=~FPS_IUV;
            i=ReadFP(r,&fsrc,ea=GeteaFP(r,dstspec,lenf),dstspec,lenf);
            FPS|=saved_uv;
            if(i){{
                saved_uv=saved_uv && dstspec>07 && GET_SIGN(fsrc.h) && !GET_EXP(fsrc.h);
                if(GET_EXP(fsrc.h)==0)fsrc=zero_fac;
                else fsrc.h=fsrc.h {operation};
                WriteFP(r,&fsrc,ea,dstspec,lenf);
                if(!r->fAbort){{FPS=setfcc(FPS,fsrc.h,0);if(saved_uv)fpnotrap(r,FEC_UNDFV);}}
            }}
            break;'''+s[end:]
    (out/'pdp11_fp.c').write_text(s)
    (out/'reference_fp_add_cp73.h').write_bytes((ROOT/'tb/reference_fp_add_cp73.h').read_bytes())
    record=dict(description=__doc__,original={str((original/n).relative_to(ROOT)):
        hashlib.sha256((original/n).read_bytes()).hexdigest() for n in ('core.c','core.h','hardware.c','hardware.h','pdp11_fp.c')},
        adapted={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(out.iterdir()) if p.suffix in ('.c','.h')})
    (out/'adaptation.json').write_text(json.dumps(record,indent=2)+'\n')
    return record
