#!/usr/bin/env python3
"""Conservative v12 microcode liveness for MMU scratch at memory boundaries.

Tracks T0..T7 and Q only. Dynamic RS/RD selectors are architectural registers.
All branch alternatives, all opcode entries, all CALL return sites and memory
fault paths are included. An empty live set proves no future read before
overwrite in this graph; it does not implement MMU entry/return or preserve PSW.
"""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NAMES = [f'T{i}' for i in range(8)]+['Q']
Q = 1 << 8


def temporary(selector):
    return 1 << (selector-8) if 8 <= selector <= 15 else 0


def analyze(words, dispatch):
    returns = {((pc+1)&1023) for pc,w in enumerate(words) if w>>35 and (w>>31)&15 == 10}
    repair = {(w>>11)&1023 for w in words if w>>35 and (w>>31)&15 == 11 and w&4}
    edges, reads, writes, memory = [], [], [], []
    for pc,w in enumerate(words):
        control = bool(w>>35)
        op=(w>>31)&15; a=temporary((w>>26)&31); b=temporary((w>>21)&31)
        target=(w>>11)&1023; following=(pc+1)&1023
        read=write=0
        if control:
            if op in (2,11,12):
                memory.append(pc)
                read=a | (b if op==12 else 0)
            if op==8:read|=a # OR_BT uses RF[A].bit0
            if op==1 and (w>>7)&7==5:read|=Q
            if op==1: successors={target,following}
            elif op in (2,4):successors=set(dispatch)
            elif op==3:successors=returns|{1023}
            elif op in (5,6):successors={target|i for i in range(8)}
            elif op in (7,8):successors={target|i for i in range(4)}
            elif op==9:successors={target,target|1}
            elif op==10:successors={target,1023} # occupied link stops
            elif op==13:successors=set()
            elif op==14:successors={pc,0x13,0x24}
            else:successors={target}
            if op in (2,11,12):
                successors|={0x15,1023}
                if w&4:successors.add(target)
        else:
            pair=(w>>18)&7; dest=(w>>15)&7; sequence=(w>>8)&3
            lhs=(a,a,a,0,0,0,0,b)[pair]
            rhs=(b,Q,0,b,b,Q,a,a)[pair]
            # PASSB reads only RHS; unary functions read only LHS.
            read=(rhs if op==1 else lhs if op in (0,10,11,12,13,14,15) else lhs|rhs)
            if dest in (1,3,4,5,6):write|=b
            if dest in (3,4):read|=Q
            if dest in (2,3,4):write|=Q
            if dest==5:read|=b # possible byte upper-half preservation
            if sequence==3:read|=a # FETCH_A1 uses RF[A], independent of ALU pair
            if sequence==0:successors={following}
            elif sequence==1:successors={(pc&0x300)|(w&255)}
            elif sequence==2:successors={0x20,0x13,0x24}
            else:successors={following,0x20,0x13,0x24}
            if pc in repair:successors.add(0x15)
        # Reset enters the ordinary initialization microprogram from any word.
        successors.add(0)
        edges.append(successors);reads.append(read);writes.append(write)
    live=[0]*1024
    rounds=0
    while True:
        changed=False;rounds+=1
        for pc in range(1023,-1,-1):
            out=0
            for target in edges[pc]:out|=live[target]
            value=reads[pc] | (out & ~writes[pc])
            if value!=live[pc]:live[pc]=value;changed=True
        if not changed:break
    common=(1<<len(NAMES))-1
    for pc in memory:common&=~live[pc]
    return live,memory,common,rounds


def names(mask):
    return [n for i,n in enumerate(NAMES) if mask & (1<<i)]


def main():
    image=ROOT/'microcode/generated/m0.mem'
    decode=ROOT/'microcode/generated/decode.mem'
    words=[int(s,16) for s in image.read_text().split()]
    entries={int(s,16) for s in decode.read_text().split()}
    assert len(words)==1024 and all(w < 1<<36 for w in words)
    live,memory,common,rounds=analyze(words,entries)
    # Negative controls must expose both a direct operand use and a future use
    # reached through FETCH/opcode dispatch, without changing the positive ROM.
    mutated=list(words)
    first=memory[0]
    mutated[first]=(mutated[first]&~(31<<26))|(15<<26)
    assert not analyze(mutated,entries)[2] & (1<<7), 'missed direct T7 use'
    mutated=list(words)
    target=next(pc for pc in sorted(entries) if words[pc]>>35==0)
    # A real result write to an architectural register with T7 as operand.
    mutated[target]=(15<<26)|(0<<21)|(0<<18)|(1<<15)|(2<<8)
    assert analyze(mutated,entries)[0][0x20] & (1<<7), 'missed dispatch T7 use'
    labels=json.loads((ROOT/'microcode/generated/m0.labels.json').read_text())
    by_address={}
    for label,address in labels.items():by_address.setdefault(address,[]).append(label)
    files=['microcode/generated/m0.mem','microcode/generated/decode.mem',
           'microcode/generated/m0.labels.json','microcode/m0.uasm','microcode/fis.uasm',
           'rtl/uj11_engine.v','rtl/uj11_microseq.v','rtl/uj11_datapath.v',
           'microasm/uj11asm.py','tools/audit_mmu_scratch.py']
    report=dict(scope='Conservative T0..T7/Q liveness for current v12 ROM',
                encoding_version=12, memory_words=len(memory), fixed_point_rounds=rounds,
                dead_at_every_memory_word=names(common), negative_controls_pass=True,
                inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in files},
                boundaries=[dict(upc=f'{pc:03x}',labels=by_address.get(pc,[]),
                                 live=names(live[pc]),dead=names(((1<<9)-1)&~live[pc])) for pc in memory],
                limits=['Conservative graph proof for this microcode image, not an ISA-level preservation test.',
                        'Tracks only whole T registers and Q; no claim about MDR, PSW, IR, architectural RF or CALL link.',
                        'All dynamic branches/dispatch and CALL returns are overapproximated.',
                        'No MMU microcode, micro-PC save/restore, automatic entry or return is implemented.'])
    out=ROOT/'build/cp34-scratch.json';out.write_text(json.dumps(report,indent=2)+'\n')
    print(f'PASS MMU scratch audit: {len(memory)} memory words; universally dead={names(common)}; direct/dispatch mutations detected')


if __name__=='__main__':
    main()
