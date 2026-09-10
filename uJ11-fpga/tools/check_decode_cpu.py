#!/usr/bin/env python3
"""CP36 word-bus/compact decoder + CP35 excursion versus the frozen CP31 CPU."""
import argparse
import hashlib
import json
import re
import subprocess
import tarfile
from board_common import ROOT


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--simulator',choices=('iverilog','verilator'),default='verilator')
    parser.add_argument('--cases',type=int,default=69632)
    parser.add_argument('--negative-lanes',action='store_true')
    args=parser.parse_args()
    out=ROOT/'build/cp36-reference'
    out.mkdir(parents=True,exist_ok=True)
    frozen=ROOT/'synth/reports/cp31c/source.tgz'
    inputs=json.loads((ROOT/'synth/reports/cp31c/inputs.json').read_text())['files']
    old=['rtl/uj11_core.v','rtl/uj11_engine.v','rtl/uj11_microseq.v',
         'rtl/uj11_decode_rom.v','microcode/generated/uj11_decode_table.v']
    references=[]
    with tarfile.open(frozen) as archive:
        for name in old+['rtl/uj11_rom.v']:
            data=(ROOT/name).read_bytes() if name=='rtl/uj11_rom.v' else archive.extractfile(name).read()
            if name in inputs: assert hashlib.sha256(data).hexdigest()==inputs[name]
            text=re.sub(r'\buj11_(core|engine|microseq|rom|decode_rom|decode_table)\b',
                        r'uj11_\1_reference',data.decode())
            path=out/(name.rsplit('/',1)[-1].replace('.v','_reference.v'))
            path.write_text(text);references.append(str(path.relative_to(ROOT)))
    tb=(ROOT/'tb/tb_mmu_entry_core.v').read_text()
    assert tb.count('uj11_core #(.ROM_DECODE(1)) dut')==1
    tb=tb.replace('uj11_core #(.ROM_DECODE(1)) dut',
                  'uj11_core #(.ROM_DECODE(1),.ALIGNED_WORD_READS(1)) dut')
    # The reference keeps the original right-justified memory interface. The
    # candidate sees both actual RAM lanes, as the physical board bus returns.
    tb=tb.replace('.mem_read_data(rdata),.stopped(cs)',
                  '.mem_read_data(aligned_data),.stopped(cs)')
    tb=tb.replace('    assign ack=inject_error || ram_ack;', '''    wire [15:0] aligned_data=inject_error ? 16'b0 :
        {memory.bytes[{ca[15:1],1'b1}],memory.bytes[{ca[15:1],1'b0}]};
    assign ack=inject_error || ram_ack;''')
    bench=out/'tb_mmu_entry_core.v';bench.write_text(tb)
    candidate='build/cp36-word-bus/context/uj11_core.v'
    if args.negative_lanes:
        text=(ROOT/candidate).read_text()
        needle='mem_addr[0] ? {8\'b0,mem_read_data[15:8]} : {8\'b0,mem_read_data[7:0]}'
        assert text.count(needle)==1
        path=out/'negative_core.v'
        path.write_text(text.replace(needle,'mem_addr[0] ? {8\'b0,mem_read_data[7:0]} : {8\'b0,mem_read_data[15:8]}'))
        candidate=str(path.relative_to(ROOT))
    sources=[str(bench.relative_to(ROOT)),candidate,'build/cp35/uj11_engine.v','build/cp35/uj11_microseq.v',
             'build/cp36-decode/bits/uj11_decode_rom.v','build/cp36-decode/uj11_decode_table.v',
             'rtl/experimental/uj11_mmu_entry.v','rtl/uj11_alu.v','rtl/uj11_regfile.v',
             'rtl/uj11_datapath.v','rtl/uj11_psw.v','rtl/uj11_mem.v','rtl/uj11_decode.v',
             'rtl/uj11_rom.v','tb/uj11_ram.v']+references
    paths=sources+['tools/check_decode_cpu.py','tb/tb_mmu_entry_core.v','build/cp35/entry.mem',
                   'microcode/generated/m0.mem','microcode/generated/decode.mem','build/cp36-decode/decode.mem',
                   'synth/reports/cp31c/source.tgz']
    hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths}
    tag='cp36-cpu-'+('negative' if args.negative_lanes else args.simulator)
    first=36864 if args.negative_lanes else 0
    if args.simulator=='verilator':
        command=['verilator','--binary','--timing','-j','4','--top-module','tb_mmu_entry_core',
                 '--Mdir','build/obj-'+tag,f'-GCASES={args.cases}',f'-GFIRST={first}']+sources
        executable=['build/obj-'+tag+'/Vtb_mmu_entry_core']
    else:
        command=['iverilog','-g2012','-Wall','-s','tb_mmu_entry_core','-o','build/'+tag,
                 f'-Ptb_mmu_entry_core.CASES={args.cases}',f'-Ptb_mmu_entry_core.FIRST={first}']+sources
        executable=['vvp','build/'+tag]
    with (ROOT/f'build/{tag}-build.log').open('w') as log:
        subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (ROOT/f'build/{tag}.log').open('w') as log:
        result=subprocess.run(executable,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    assert hashes=={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths}
    text=(ROOT/f'build/{tag}.log').read_text()
    if args.negative_lanes:
        assert result.returncode!=0 and ('mismatch' in text),text
    else:
        assert result.returncode==0 and f'PASS entry CPU miter: {args.cases} cases' in text,text[-3000:]
    record=dict(simulator=args.simulator,cases=args.cases,first=first,negative_lanes=args.negative_lanes,
                result_returncode=result.returncode,inputs_sha256=hashes,
                pass_lines=[s for s in text.splitlines() if s.startswith('PASS')],
                failure=text.strip() if args.negative_lanes else None,
                covered_memory_upcs=sorted(set(re.findall(r'ENTRY memory_upc=([0-9a-f]{3})',text))))
    (ROOT/f'build/{tag}.json').write_text(json.dumps(record,indent=2)+'\n')
    print('PASS rejected swapped byte lanes' if args.negative_lanes else '\n'.join(record['pass_lines']),flush=True)


if __name__=='__main__':
    main()
