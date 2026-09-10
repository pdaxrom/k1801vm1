#!/usr/bin/env python3
"""Run CP35 whole-opcode CPU miter with an independently clocked baseline."""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path
from board_common import ROOT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--simulator', choices=('iverilog','verilator'), default='verilator')
    parser.add_argument('--cases', type=int, default=69632)
    args = parser.parse_args()
    assert 0 < args.cases <= 1048576
    folder = ROOT/'build/cp35-reference'
    folder.mkdir(exist_ok=True)
    original = ['rtl/uj11_core.v','rtl/uj11_engine.v','rtl/uj11_microseq.v','rtl/uj11_rom.v']
    references = []
    for filename in original:
        text = (ROOT/filename).read_text()
        renamed = re.sub(r'\buj11_(core|engine|microseq|rom)\b', r'uj11_\1_reference', text)
        path = folder/(Path(filename).stem+'_reference.v')
        path.write_text(renamed)
        references.append(str(path.relative_to(ROOT)))
    sources = ['tb/tb_mmu_entry_core.v','tb/uj11_ram.v',
               'build/cp35/uj11_core.v','build/cp35/uj11_engine.v','build/cp35/uj11_microseq.v',
               'rtl/experimental/uj11_mmu_entry.v','rtl/uj11_alu.v','rtl/uj11_regfile.v',
               'rtl/uj11_datapath.v','rtl/uj11_psw.v','rtl/uj11_mem.v','rtl/uj11_decode.v',
               'rtl/uj11_decode_rom.v','build/cp35/uj11_decode_table.v','rtl/uj11_rom.v']+references
    paths = sources+original+['tools/run_mmu_entry.py','build/cp35/inputs.json',
                             'build/cp35/entry.mem','microcode/generated/m0.mem','microcode/generated/decode.mem']
    hashes = {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sorted(set(paths))}
    tag = 'cp35-miter-'+args.simulator
    if args.simulator == 'verilator':
        command = ['verilator','--binary','--timing','-j','4','--top-module','tb_mmu_entry_core',
                   '--Mdir','build/obj-'+tag,f'-GCASES={args.cases}']+sources
        binary = ['build/obj-'+tag+'/Vtb_mmu_entry_core']
    else:
        command = ['iverilog','-g2012','-Wall','-s','tb_mmu_entry_core','-o','build/'+tag,
                   f'-Ptb_mmu_entry_core.CASES={args.cases}']+sources
        binary = ['vvp','build/'+tag]
    with (ROOT/f'build/{tag}-build.log').open('w') as log:
        subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (ROOT/f'build/{tag}.log').open('w') as log:
        subprocess.run(binary,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    assert hashes == {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in hashes}
    log = (ROOT/f'build/{tag}.log').read_text()
    assert f'PASS entry CPU miter: {args.cases} cases' in log
    record = dict(scope='Original CPU clock-paused only during CP35 helper; ordinary bus/state compared',
                  simulator=args.simulator,cases=args.cases,inputs_sha256=hashes,
                  pass_lines=[s for s in log.splitlines() if s.startswith('PASS')],
                  memory_upcs=sorted(set(re.findall(r'ENTRY memory_upc=([0-9a-f]{3})', log))))
    (ROOT/f'build/{tag}.json').write_text(json.dumps(record,indent=2)+'\n')
    print('\n'.join(record['pass_lines']),flush=True)


if __name__=='__main__':
    main()
