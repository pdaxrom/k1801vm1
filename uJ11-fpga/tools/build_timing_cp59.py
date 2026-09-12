#!/usr/bin/env python3
"""Isolated timing checkpoint: eliminate an unobservable bus-error predicate."""
import hashlib
import json
import tarfile
from board_common import ROOT, CORE, BOARD
from build_fram_cp52 import replace_once as rep

OUT=ROOT/'build/cp59-service'


def baseline():
    manifest=json.loads((ROOT/'synth/reports/cp58a/inputs.json').read_text())
    files={}
    with tarfile.open(ROOT/'synth/reports/cp58a/source.tgz') as ar:
        for path,digest in manifest['files'].items():
            if path.startswith('generated:'):continue
            data=ar.extractfile(path).read()
            assert hashlib.sha256(data).hexdigest()==digest,path
            files[path]=data
    return files


def build():
    old=baseline();OUT.mkdir(parents=True,exist_ok=True);outputs=[]
    prefix='build/cp58-service/'
    for name in CORE+BOARD:
        text=old[prefix+'src/'+name].decode().replace('build/cp58-service/','build/cp59-service/')
        if name=='rtl/uj11_engine.v':
            text=rep(text,'.bus_error(bus_fault!=0)', ".bus_error(1'b0)")
            text=rep(text,'    uj11_microseq seq(','''    // CJUMP and a memory operation are mutually exclusive. Its current
    // bus-error predicate is therefore always zero. Fault redirect/repair
    // remain connected to the real fault; no error is masked or delayed.
    uj11_microseq seq(''')
        path=OUT/'src'/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(text)
        outputs.append(str(path.relative_to(ROOT)))
    for name in ['service.uasm','m0.mem','m0.lst','m0.labels.json','m0.stats.json','uj11_m0_ebr.v','decode.mem']:
        path=OUT/name;path.write_bytes(old[prefix+name]);outputs.append(str(path.relative_to(ROOT)))
    # Lattice Timing Closure, p.38: request packing of the *existing* output
    # register. No extra pipeline stage, pin, or changed logical CS behavior.
    top=rep(old['boards/hc1200/uj11_microcomp.v'].decode(),
            '    output wire gpio_msck, gpio_mcs, gpio_din, gpio_ce, gpio_clk,',
            '    output wire gpio_mcs /* synthesis syn_useioff = 1 */,\n    output wire gpio_msck, gpio_din, gpio_ce, gpio_clk,')
    path=OUT/'src/boards/hc1200/uj11_microcomp.v';path.write_text(top);outputs.append(str(path.relative_to(ROOT)))
    inputs=['tools/build_timing_cp59.py','tools/build_fram_cp52.py','tools/board_common.py',
            'synth/reports/cp58a/inputs.json','synth/reports/cp58a/source.tgz']
    record=dict(reference='cp58a',mmu=False,used_words=1002,cs_ioff_requested=True,
        inputs={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},
        outputs={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in outputs})
    (OUT/'inputs.json').write_text(json.dumps(record,indent=2)+'\n')
    return record


def adapt():
    build()
    return ([str((OUT/'src'/p).relative_to(ROOT)) for p in CORE],
            [str((OUT/'src'/p).relative_to(ROOT)) for p in BOARD])


if __name__=='__main__':print(json.dumps(build(),indent=2))
