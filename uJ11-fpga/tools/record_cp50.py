#!/usr/bin/env python3
"""Archive the MMU-less/opt-in split, preserving the pending synthesis boundary."""
import json
import re
import shutil
import subprocess
import tarfile
from board_common import ROOT, MMU
from record_cp33 import digest, synthesis
from record_cp36 import board_counts
from record_cp37 import failed_fit


def main():
    assert not list((ROOT/'synth/reports').glob('cp50*')), 'update manifest for actual synthesis'
    test=json.loads((ROOT/'build/cp50-profiles/verified.json').read_text())
    cold=json.loads((ROOT/'build/cp50-mmuless-board-inputs.json').read_text())
    sources={}
    for hashes in [test['inputs_sha256'],cold['files'],test['test_logs_sha256']]:
        for name,expected in hashes.items():
            assert digest(ROOT/name)==expected,name
            sources[name]=expected
    assert len(test['comparisons'])==96 and len(test['runs'])==9
    assert not cold['mmu'] and cold['defines']==[]
    counts=board_counts((ROOT/'build/cp50-mmuless-board-rt11.log').read_text())
    previous=json.loads((ROOT/'docs/verification-cp40.json').read_text())
    assert counts==previous['cold_rt11fb']['production'],counts
    uart=ROOT/'build/cp50-mmuless-uart.txt'
    assert uart.read_bytes()==(ROOT/'tb/reports/cp40/cp40-production-uart.txt').read_bytes()
    assert digest(ROOT/'../lsi11-fpga/images/rt11v503.dsk')==cold['image_sha256']
    profile_lists={}
    for mode in ('mmuless','mmu'):
        prefix=ROOT/'build'/('board-'+mode)
        profile=json.loads((prefix/'inputs.json').read_text())
        for name,expected in profile['files'].items():
            assert digest(ROOT/name)==expected,name
            sources[name]=expected
        assert set(MMU).issubset(profile['sources']) if mode=='mmu' else not set(MMU).intersection(profile['sources'])
        # Exercise the public .f files, including their define syntax.
        executable=ROOT/'build'/('cp50-'+mode+'-elaborated')
        subprocess.run(['iverilog','-g2012','-s','uj11_board','-o',str(executable),
                        '-f',str(prefix/'simulation.f')],cwd=ROOT,check=True)
        modules=sorted(set(re.findall(r'\.scope module, "[^"]+" "(uj11_mm[ur][^"]*)"',executable.read_text())))
        assert len(modules)==(6 if mode=='mmu' else 0),modules
        subprocess.run(['iverilog','-g2012','-E','-o',str(ROOT/'build'/('cp50-'+mode+'-synthesis.v')),
                        '-f',str(prefix/'synthesis.f')],cwd=ROOT,check=True)
        profile_lists[mode]=dict(elaborated_mmu_modules=modules,
            simulation_list=(prefix/'simulation.f').read_text(),synthesis_list=(prefix/'synthesis.f').read_text())
    invalid=subprocess.run(['make','-n','board','MMU=2'],cwd=ROOT,capture_output=True,text=True)
    assert invalid.returncode and 'MMU must be 0 or 1' in invalid.stderr
    (ROOT/'build/cp50-invalid-option.log').write_text(invalid.stderr)
    sources.update({p:digest(ROOT/p) for p in [
        'Makefile','tools/record_cp50.py','tools/record_cp33.py','tools/record_cp36.py',
        'tools/record_cp37.py','tools/checkpoint_board.py','tools/archive_synthesis.py']})
    out=ROOT/'tb/reports/cp50';out.mkdir(parents=True,exist_ok=True)
    paths=sorted((ROOT/'build/cp50-profiles').glob('*.log'))+[
        ROOT/'build/cp50-profiles/verified.json',ROOT/'build/cp50-mmuless-board-inputs.json',
        ROOT/'build/cp50-mmuless-board-build.log',ROOT/'build/cp50-mmuless-board-rt11.log',uart,
        ROOT/'build/cp50-invalid-option.log']
    for mode in ('mmuless','mmu'):
        paths+=list((ROOT/'build'/('board-'+mode)).glob('*'))
    for path in paths:
        relative=path.relative_to(ROOT/'build')
        dest=out/relative;dest.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(path,dest)
    with tarfile.open(out/'test-sources.tgz','w:gz') as archive:
        for path in sorted(sources):
            if path.startswith(('build/vendor/','build/cp50-profiles/')):continue
            archive.add(ROOT/path,arcname=path)
    result=dict(checkpoint='CP50: MMU-less default; CP47c retained under UJ11_MMU',date='2026-09-11',
        mmu_development_deferred=True,default_mmu=False,profiles=profile_lists,
        source_comparisons=96,unit_runs=test['runs'],default_cold_fb=counts,
        uart_sha256=digest(uart),microcode_words=954,
        historical_mmuless_cp40h=synthesis('cp40h'),historical_mmu_cp47c=failed_fit('cp47c'),
        synthesis_complete=False,synthesis_pending_reason='Automatic approval review rejected CP50 source transfer; explicit payload approval pending.',
        board_programmed=False,physical_board='CP29a',mmu_complete=False,rt11_xm_tested=False,
        sources_sha256=sources,
        archived_tests_sha256={str(p.relative_to(ROOT)):digest(p) for p in sorted(out.rglob('*')) if p.is_file()})
    (ROOT/'docs/verification-cp50.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS CP50: default has zero MMU modules; both source profiles preserved; tests and cold FB pass; new synthesis pending')


if __name__=='__main__':main()
