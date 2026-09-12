#!/usr/bin/env python3
"""Archive CP62 firmware, exact test inputs and a measured HC1200 gate."""
import argparse
import hashlib
import json
import re
import shutil
import tarfile
from pathlib import Path
from board_common import ROOT
from build_loader_cp62 import build as check_helper


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def verify(paths):
    for name,digest in paths.items():assert sha(ROOT/name)==digest,name


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run',required=True,type=Path)
    parser.add_argument('--asm-dir',required=True,type=Path)
    args=parser.parse_args();run=args.run.resolve();asm=args.asm_dir.resolve()
    result=json.loads((run/'result.json').read_text());inputs=json.loads((run/'inputs.json').read_text())
    assembly=json.loads((asm/'build-inputs.json').read_text())
    gate=json.loads((ROOT/'synth/reports/cp62a/result.json').read_text())
    assert result['passed'] and result['cases']==42
    assert gate['timing_pass'] and gate['fully_routed'] and gate['applied_clock_mhz']==31.824
    assert (gate['lut4'],gate['ff'],gate['ebr'],gate['fmax_mhz'])==(1229,343,6,32.087)
    check_helper();verify(inputs['files']);verify(assembly['source_sha256'])
    for name,digest in result['files'].items():assert sha(run/name)==digest,name
    for name,digest in assembly['outputs'].items():assert sha(asm/name)==digest,name
    for name,digest in inputs['profile']['outputs'].items():
        assert sha(ROOT/name)==digest==gate['inputs']['files'][name],name
    assert sha(ROOT/'demos/rt11/service/cp62/UJLOAD.SAV')==assembly['outputs']['UJLOAD.SAV']
    names=set(inputs['files'])|set(assembly['source_sha256'])
    proof=json.loads((ROOT/'build/cp62-hardware-proof.json').read_text())
    assert proof['rtl_files_identical']==19 and proof['microcode_identical'] and proof['cp62_outputs_match_synthesis']
    directed=[]
    for filename in ('boot-tests.json','helper-tests.json'):
        records=json.loads((ROOT/'build/cp62-boot'/filename).read_text())
        assert len(records)==2
        for record in records:
            assert record['passed'] and record['cases']==17
            verify(record['files']);names.update(record['files'])
        directed+=records
    names.update(str(p.relative_to(ROOT)) for p in (ROOT/'firmware/cp62').iterdir() if p.is_file())
    names.update(['tools/archive_loader_cp62.py','tools/build_loader_cp62.py','tools/build_vector_disk_cp62.py',
        'tools/test_loader_gate_cp62.py','tools/test_halt_boot_cp62.py','tools/rt11_build.py',
        'tb/test_service_image_cp62.py','demos/rt11/service/cp62/UJLOAD.SAV','build/cp62-loader/cases.json',
        'build/cp62-loader/cold.hex','build/cp62-hardware-proof.json'])
    out=ROOT/'tb/reports/cp62';out.mkdir(parents=True,exist_ok=False)
    def copy(src,dst):
        target=out/dst;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,target)
    for name in ('inputs.json','result.json','build.log','simulation.log','uart.txt','directory.txt','symbols.json'):
        copy(run/name,'rt11/'+name)
    for name in list(assembly['outputs'])+['build-inputs.json','console.log']:copy(asm/name,'asm/'+name)
    for name in ('boot-tests.json','helper-tests.json'):
        copy(ROOT/'build/cp62-boot'/name,'directed/'+name)
    for family in ('boot','helper'):
        for mode in ('portable','vendor'):
            for suffix in ('.log','-build.log'):
                name=f'{family}-{mode}{suffix}';copy(ROOT/'build/cp62-boot'/name,'directed/'+name)
    copy(ROOT/'build/cp62-hardware-proof.json','hardware-proof.json')
    copy(ROOT/'build/cp62-disk.json','disk.json')
    rejected=ROOT/'build/cp62-rejected32'
    old_assembly=json.loads((rejected/'build-inputs.json').read_text())
    assert sha(rejected/'UJLOAD.SAV')==old_assembly['outputs']['UJLOAD.SAV']
    assert 'CTRL/C did not leave only ODT ready' in (rejected/'simulation.log').read_text()
    for path in rejected.iterdir():
        if path.is_file():copy(path,'rejected32/'+path.name)
    with tarfile.open(out/'source.tgz','w:gz') as archive:
        for name in sorted(names):
            path=ROOT/name;assert path.resolve().is_relative_to(ROOT),name
            archive.add(path,arcname=name,recursive=False)
    text=(run/'simulation.log').read_text()
    counts=re.search(r'PASS CP62 full RT-11 loader: (\d+) file/command cases.*?; (\d+) clocks, (\d+) retired, (\d+) upper writes, (\d+) wire bytes',text)
    assert counts
    record=dict(checkpoint='CP62',synthesis='cp62a',comparison='cp61g',rtl_logic_changed=False,
        firmware_rom_changed=True,helper_in_fram_bytes=456,max_block_words=8,rejected_32_word_ctrl_c=True,mmu=False,full_odt=False,full_fp11=False,
        board_flashed=False,cases=int(counts[1]),clocks=int(counts[2]),retired=int(counts[3]),
        upper_writes=int(counts[4]),uart_bytes=int(counts[5]),ctrl_c=True,ctrl_c_uart_reads=2,ctrl_c_overruns=0,cold_reboot=True,
        boot_cases_per_rom=17,helper_cases_per_rom=17,rom_models=['portable','Lattice'],
        host_format_tests=6,clock_tests=4,hardware=proof,
        sources={p:sha(ROOT/p) for p in sorted(names)},
        archived={str(p.relative_to(out)):sha(p) for p in sorted(out.rglob('*')) if p.is_file()})
    (out/'archive.json').write_text(json.dumps(record,indent=2)+'\n')
    (ROOT/'docs/verification-cp62.json').write_text(json.dumps({k:v for k,v in record.items() if k not in ('sources','archived')},indent=2)+'\n')
    (ROOT/'docs/synthesis-cp62.json').write_text(json.dumps({k:v for k,v in gate.items() if k not in ('inputs','reports')},indent=2)+'\n')
    print(json.dumps({k:v for k,v in record.items() if k not in ('sources','archived')},indent=2))


if __name__=='__main__':main()
