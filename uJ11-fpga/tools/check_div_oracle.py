#!/usr/bin/env python3
"""Check ordinary DIV C records against independent integer/addressing rules.

Trace and interrupt frames are checked by RTL against the actual C executor;
this small independent model checks all records without T or an IRQ request.
It never rewrites expected results or implements a replacement C oracle.
"""
import json
from pathlib import Path
from check_psw_transfer_negative import records
ROOT=Path(__file__).resolve().parents[1]

def decode_record(h,c,text):
    lines=text.splitlines();np=int(h[11],16)
    patches=[tuple(int(v,16) for v in s.split()) for s in lines[2:2+np]]
    post=[int(v,16) for v in lines[2+np].split()]
    bus=[tuple(int(v,16) for v in s.split()) for s in lines[3+np:]]
    return patches,post,bus

def ordinary(h,patches):
    op=int(h[1],16);psw=int(h[2],16);r=[int(v,16) for v in h[3:11]]
    memory=dict(patches);bus=[]
    def read(a):
        a&=65535
        assert not a&1 and a<0xe000,(h[0],hex(a))
        v=memory.get(a,0x8000|((a^(a>>3)^0x3456)&0x1ffe))
        bus.append((0,a,v));return v
    assert read(r[7])==op
    r[7]=(r[7]+2)&65535
    rs=(op>>6)&7;rd=op&7;mode=(op>>3)&7
    a=r[rd]
    if mode==2:r[rd]=(r[rd]+2)&65535
    elif mode==3:
        a=read(r[rd]);r[rd]=(r[rd]+2)&65535
    elif mode in [4,5]:
        r[rd]=(r[rd]-2)&65535;a=r[rd]
        if mode==5:a=read(a)
    elif mode in [6,7]:
        displacement=read(r[7]);r[7]=(r[7]+2)&65535
        a=(r[rd]+displacement)&65535
        if mode==7:a=read(a)
    destination=read(a) if mode else r[rd]
    from check_div_algorithm import reference
    q,rem,flags=reference((r[rs]<<16)|r[rs|1],destination)
    if q is not None:r[rs]=q;r[rs|1]=rem
    psw=(psw&~15)|flags
    return psw,r,bus

def main():
    checked=0;normal=set();modes=set();aliases=0
    for h,c,text in records('eis_div'):
        op=int(h[1],16);normal.add(op)
        if int(h[2],16)&16 or int(c[0],16):continue
        patches,post,bus=decode_record(h,c,text)
        psw,r,expected_bus=ordinary(h,patches)
        assert (post[0],post[1:9],bus)==(psw,r,expected_bus),('independent DIV mismatch',h,post,psw,r,bus,expected_bus)
        checked+=1;modes.add((op>>3)&7);aliases+=((op>>6)&7)==(op&7)
    faults=set(int(h[1],16) for h,c,t in records('eis-div-fault',True))
    all_opcodes=set(range(0o71000,0o72000))
    assert checked>3000 and modes==set(range(8)) and normal|faults==all_opcodes
    result=dict(independent_ordinary_cases=checked,independent_alias_cases=aliases,
                normal_encodings=len(normal),fault_encodings=len(faults),normal_fault_union=512,
                modes=sorted(modes),method='Independent DIV and all eight word addressing modes; exact registers, PSW, bus trace. No T/IRQ in this extra model; no fixture rewriting.')
    (ROOT/'build/cp26-div-oracle.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS independent DIV oracle:',checked,'ordinary cases;',aliases,'aliases; all 8 modes; normal/fault union 512 encodings')
if __name__=='__main__':main()
