#!/usr/bin/env python3
"""CP78c area candidate: use existing ALU output for explicit PSW writes."""
import hashlib,json
from build_fram_cp52 import replace_once as rep
from board_common import ROOT,CORE,BOARD
from build_psw_cp78 import build as baseline,OUT as BASE
OUT=ROOT/'build/cp78c-psw'

def build():
    base=baseline();outputs=[]
    for p in base['outputs']:
        suffix=(ROOT/p).relative_to(BASE)
        text=(ROOT/p).read_text().replace('build/cp78-psw/','build/cp78c-psw/')
        if str(suffix)=='src/rtl/uj11_psw.v':text=(ROOT/'rtl/cp78/uj11_psw_shared.v').read_text()
        if str(suffix)=='src/rtl/uj11_engine.v':
            text=rep(text,'wire [3:0] operation = uword[34:31];',
                     "wire [3:0] operation = writing ? 4'd1 : uword[34:31];")
            text=rep(text,'wire [2:0] pair = uword[20:18];',
                     "wire [2:0] pair = writing ? 3'd0 : uword[20:18];")
        dest=OUT/suffix;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text(text)
        outputs.append(str(dest.relative_to(ROOT)))
    inputs=list(base['inputs'])+['tools/build_psw_cp78c.py','rtl/cp78/uj11_psw_shared.v']
    sha=lambda p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest()
    r=dict(reference='cp78a',mmu=False,microcode_changed=False,expected_ebr=7,
           inputs={p:sha(p) for p in inputs},outputs={p:sha(p) for p in outputs})
    (OUT/'inputs.json').write_text(json.dumps(r,indent=2)+'\n');return r

def adapt():
    build()
    return ([str((OUT/'src'/p).relative_to(ROOT)) for p in CORE],
            [str((OUT/'src'/p).relative_to(ROOT)) for p in BOARD+['boards/hc1200/uj11_button.v']])
if __name__=='__main__':print(json.dumps(build(),indent=2))
