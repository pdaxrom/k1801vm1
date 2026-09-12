#!/usr/bin/env python3
"""Preserve CP63 gates, exact tested sources, negative controls and RT-11 run."""
import hashlib
import json
import re
import shutil
import tarfile
from pathlib import Path
from board_common import ROOT


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def verify(names):
    for name,digest in names.items():assert sha(ROOT/name)==digest,name


def main():
    work=ROOT/'build/cp63-debug'
    rt_ready=json.loads((ROOT/'build/cp63a-rt11/result.json').read_text())
    assert rt_ready['passed'] and rt_ready['cases']==42 and rt_ready['cold_reboot']
    gates={}
    for name,archive in (('cp63a','/tmp/uj11-cp63-source.tgz'),('cp63b','/tmp/uj11-cp63b-source.tgz')):
        run=ROOT/'build'/name
        result=json.loads((run/'result.json').read_text())
        assert result['timing_pass'] and result['fully_routed'] and result['applied_clock_mhz']==31.824
        manifest=result['inputs']['files']
        with tarfile.open(archive) as ar:
            assert set(ar.getnames())=={n for n in manifest if not n.startswith('generated:')}
            for n in ar.getnames():assert hashlib.sha256(ar.extractfile(n).read()).hexdigest()==manifest[n],n
        for name2 in ('clock.lpf','build.tcl'):assert sha(run/name2)==manifest['generated:'+name2]
        out=ROOT/'synth/reports'/name;out.mkdir(parents=True,exist_ok=False)
        for filename in ('inputs.json','result.json','diamond.log','build.tcl','clock.lpf'):
            shutil.copyfile(run/filename,out/filename)
        for suffix in ('.srr','.areasrr','.mrp','.par','.twr','.edi','.prf'):
            p=run/'impl1'/(name+'_impl1'+suffix)
            if suffix in result['reports']:assert sha(p)==result['reports'][suffix]
            shutil.copyfile(p,out/('design'+suffix))
        shutil.copyfile(archive,out/'source.tgz')
        gates[name]=result
    profile=json.loads((work/'inputs.json').read_text());verify(profile['inputs']);verify(profile['outputs'])
    for n,digest in profile['outputs'].items():assert gates['cp63b']['inputs']['files'][n]==digest,n
    tests=json.loads((work/'tests.json').read_text())
    assert tests['cases']==19 and len(tests['records'])==6
    names=set(profile['inputs'])|set(profile['outputs'])
    for test in tests['records']:assert test['passed'];verify(test['sources']);names.update(test['sources'])
    board=json.loads((work/'board-tests.json').read_text());assert len(board)==2
    for test in board:assert test['passed'];verify(test['files']);names.update(test['files'])
    checks=json.loads((work/'checks/result.json').read_text());verify(checks['sources']);names.update(checks['sources'])
    assert checks['synthesis_source_match'] and len(checks['negative_controls'])==4
    edif=json.loads((ROOT/'build/cp63b/edif-audit.json').read_text())
    assert not edif['multiple_drivers'] and not edif['floating'] and len(edif['negative_controls'])==3
    run=ROOT/'build/cp63a-rt11'
    rt=json.loads((run/'result.json').read_text());inputs=json.loads((run/'inputs.json').read_text())
    assert rt['passed'] and rt['cases']==42 and rt['cold_reboot'] and rt['ctrl_c']
    verify(inputs['files']);names.update(inputs['files'])
    for n,digest in rt['files'].items():assert sha(run/n)==digest,n
    assert inputs['profile']['outputs']==profile['outputs']
    text=(run/'simulation.log').read_text()
    counts=re.search(r'PASS CP62 full RT-11 loader: (\d+) file/command cases.*?; (\d+) clocks, (\d+) retired, (\d+) upper writes, (\d+) wire bytes',text)
    assert counts and int(counts[1])==42
    old_text=(ROOT/'tb/reports/cp62/rt11/simulation.log').read_text()
    old_counts=re.search(r'PASS CP62 full RT-11 loader: (\d+) file/command cases.*?; (\d+) clocks, (\d+) retired, (\d+) upper writes, (\d+) wire bytes',old_text)
    assert old_counts and counts.groups()==old_counts.groups(), 'investigate changed normal execution counts'
    names.update(['tools/archive_debug_cp63.py','tools/test_debug_cp63.py',
        'build/cp63-debug/debug_cases.vh','build/cp63-debug/debug_constants.vh',
        'build/cp63-debug/inputs.json','tools/check_edif_drivers_cp46.py'])
    out=ROOT/'tb/reports/cp63';out.mkdir(parents=True,exist_ok=False)
    def copy(src,dest):
        p=out/dest;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,p)
    for name in ('inputs.json','result.json','build.log','simulation.log','uart.txt','symbols.json','directory.txt'):
        copy(run/name,'rt11/'+name)
    for name in ('tests.json','board-tests.json','test-all.log','board-all.log'):
        copy(work/name,'directed/'+name)
    for name in ('button10','button100','button128','logic','sync','vendor'):
        for suffix in ('.log','-build.log'):copy(work/f'test-{name}{suffix}',f'directed/test-{name}{suffix}')
    for mode in ('portable','vendor'):
        for suffix in ('.log','-build.log'):copy(work/f'board-{mode}{suffix}',f'directed/board-{mode}{suffix}')
    for p in sorted((work/'checks').rglob('*')):
        relative=p.relative_to(work/'checks')
        if 'baseline' not in relative.parts and p.is_file() and p.suffix in ('.v','.log','.json'):
            copy(p,'checks/'+str(relative))
    copy(ROOT/'build/cp63b/edif-audit.json','checks/edif-audit.json')
    with tarfile.open(out/'checks/negative-edif.tgz','w:gz') as ar:
        for p in sorted((ROOT/'build/cp63b/negative-edif').glob('*.edi')):ar.add(p,arcname=p.name,recursive=False)
    with tarfile.open(out/'source.tgz','w:gz') as ar:
        for name in sorted(names):
            p=ROOT/name;assert p.resolve().is_relative_to(ROOT),name
            ar.add(p,arcname=name,recursive=False)
    summary=dict(checkpoint='CP63',selected_synthesis='cp63b',mmu=False,board_flashed=False,
        full_odt=False,debug_vector_octal='000110',firmware_rom_changed=False,microcode_words=1005,
        cpu_cases=19,cpu_modes=3,cpu_checks_per_mode=169,button_checks_per_hold=28,button_holds=[10,100,128],
        spi_board_modes=['portable','vendor'],behavioral_negative_controls=4,edif_negative_controls=3,
        rt11_cases=42,ctrl_c=True,cold_reboot=True,legacy_button_pulses=True,
        rt11_counts_identical_to_cp62=True,
        rt11_clocks=int(counts[2]),rt11_retired=int(counts[3]),rt11_upper_writes=int(counts[4]),rt11_wire_bytes=int(counts[5]),
        known_limits=['No production UART/HDSP ODT yet','No board programming or electrical RESET-net verification',
            'CPU debug_block tested directly; full RT-11 regression keeps debug disabled',
            'Old UJLOAD CONFIG clears debug extension; monitor installer activation is a later software checkpoint'])
    (ROOT/'docs/verification-cp63.json').write_text(json.dumps(summary,indent=2)+'\n')
    (ROOT/'docs/synthesis-cp63.json').write_text(json.dumps({k:v for k,v in gates['cp63b'].items() if k not in ('inputs','reports')},indent=2)+'\n')
    record=dict(summary=summary,sources={n:sha(ROOT/n) for n in sorted(names)},
                archived={str(p.relative_to(out)):sha(p) for p in sorted(out.rglob('*')) if p.is_file()})
    (out/'archive.json').write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps(summary,indent=2))


if __name__=='__main__':main()
