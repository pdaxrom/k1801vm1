#!/usr/bin/env python3
"""Bind CP40 datapath/ALU savings to exact synthesis and regression evidence."""
import hashlib
import json
import shutil
import tarfile
from board_common import ROOT
from record_cp33 import digest,synthesis
from record_cp36 import board_counts
from record_cp37 import checked


def overflow(name):
    folder=ROOT/'synth/reports'/name
    result=json.loads((folder/'result.json').read_text())
    assert result['diamond_returncode']!=0 and not result['timing_pass'] and not result['fully_routed']
    assert 'fmax_mhz' not in result and (result['lut4']>1280 or result['slices']>640)
    assert result['inputs']==json.loads((folder/'inputs.json').read_text())
    with tarfile.open(folder/'source.tgz') as archive:
        for p,h in result['inputs']['files'].items():
            data=(folder/'clock.lpf').read_bytes() if p.startswith('generated:') else archive.extractfile(p).read()
            assert hashlib.sha256(data).hexdigest()==h,(name,p)
    for suffix,h in result['reports'].items():assert digest(folder/('design'+suffix))==h
    return {k:result[k] for k in ('lut4','ff','ebr','slices','timing_pass','fully_routed')}


def main():
    failures={'cp40a','cp40d','cp40e','cp40g'}
    fits={n:overflow(n) if n in failures else synthesis(n,n in ('cp40h','cp40i'))
          for n in (f'cp40{x}' for x in 'abcdefghi')}
    assert fits['cp40i']==fits['cp40f']
    production=synthesis('cp38f');apr=synthesis('cp39d')
    baseline_inputs=json.loads((ROOT/'synth/reports/cp39d/inputs.json').read_text())['files']
    changed=sorted(p for p,h in baseline_inputs.items() if not p.startswith('generated:') and digest(ROOT/p)!=h)
    assert changed==['rtl/uj11_alu.v','rtl/uj11_datapath.v'],changed
    assert digest(ROOT/'rtl/uj11_datapath.v')==digest(ROOT/'build/cp40-mux/both/uj11_datapath.v')
    assert digest(ROOT/'rtl/uj11_alu.v')==digest(ROOT/'build/cp40-alu/priority/uj11_alu.v')
    sources={'tools/record_cp40.py','tools/checkpoint_datapath_mux.py'};logs=[]
    for name in ('cp40-mux/inputs.json','cp40-alu/inputs.json'):
        build=checked(name)
        for p,h in build['outputs_sha256'].items():assert digest(ROOT/p)==h
        sources.update(build['inputs_sha256']);sources.update(build['outputs_sha256']);logs.append(name)
    proofs={}
    for name,folder,positives,negatives in [('cp40-proof.json','cp40-proof',3,2),('cp40-alu-proof.json','cp40-alu-proof',2,2)]:
        report=checked(name);proofs[name]=report['tests'];sources.update(report['inputs_sha256']);logs.append(name)
        assert sum(not t['negative'] for t in report['tests'])==positives
        assert sum(t['negative'] for t in report['tests'])==negatives
        for t in report['tests']:
            key=t.get('variant',t.get('kind'));path=ROOT/'build'/folder/(key+'.log')
            assert digest(path)==t['log_sha256']
            assert (t['returncode']!=0) == t['negative']
            logs.append(folder+'/'+key+'.log')
        sources.update(str(p.relative_to(ROOT)) for p in (ROOT/'build'/folder).glob('*.ys'))
    for name,folder in [('cp40-sim.json','cp40-sim'),('cp40-alu-sim.json','cp40-alu-sim')]:
        report=checked(name);sources.update(report['inputs_sha256']);logs.append(name)
        assert len(report['tests'])==3
        for t in report['tests']:
            assert '266257 cycles' in t['pass_line']
            assert t['pass_line'] in (ROOT/'build'/folder/(t['variant']+'.log')).read_text()
            logs += [folder+'/'+t['variant']+suffix for suffix in ('.log','-build.log')]
    lint=checked('cp40-lint.json');assert lint['configurations']==4
    sources.update(lint['inputs_sha256']);logs+=['cp40-lint.json','cp40-lint.log']
    cpu=checked('cp40-miter-verilator.json');sources.update(cpu['inputs_sha256'])
    assert cpu['cases']==69632 and len(cpu['memory_upcs'])==88
    for tag in ('cp40-miter-verilator','cp40-cpu-portable','cp40-cpu-vendor'):
        report=checked(tag+'.json');sources.update(report['inputs_sha256'])
        text=(ROOT/'build'/(tag+'.log')).read_text()
        assert all(line in text for line in report['pass_lines'])
        if tag.startswith('cp40-cpu'):
            assert '432 readbacks, 720 beats, 4902 lookup reads, 221988 clocks' in text
            assert 'vector4, no write, APR preserved' in text
        logs += [tag+'.json',tag+'.log',tag+'-build.log']
    for name in ('miter-portable','csr-portable','csr-vendor'):
        report=checked('cp40-cpu/'+name+'.json');sources.update(report['inputs_sha256']);logs.append('cp40-cpu/'+name+'.json')
    wrapper=json.loads((ROOT/'build/cp40-miter-wrapper.json').read_text())
    for p,h in wrapper.items():assert digest(ROOT/p)==h
    sources.update(wrapper);logs.append('cp40-miter-wrapper.json')
    fis=checked('cp40-fis.json');sources.update(fis['inputs_sha256']);logs.append('cp40-fis.json')
    assert [t['cases'] for t in fis['tests']]==[23840,23840,645]
    previous_fis=json.loads((ROOT/'docs/verification-cp37.json').read_text())['sources_sha256']['build/fis-vectors.txt']
    assert digest(ROOT/'build/fis-vectors.txt')==previous_fis
    for t in fis['tests']:
        assert t['pass_line'] in (ROOT/f'build/{t["tag"]}.log').read_text()
        logs += [t['tag']+suffix for suffix in ('.log','-build.log','.csv')]
    for p,h in fis['vendor_sha256'].items():assert digest(ROOT/'build/vendor'/p)==h
    assert digest(ROOT/'build/cp40-fis/vendor-vectors.txt')==fis['vendor_subset_sha256']
    sources.update(str(p.relative_to(ROOT)) for p in (ROOT/'build/cp40-fis').iterdir() if p.suffix in ('.v','.txt'))
    boards={}
    for kind,old in [('production','cp38/cp38-production'),('apr','cp39/cp39')]:
        tag='cp40-'+kind
        report=checked(tag+'-verified.json','files');sources.update(report['files'])
        counts=board_counts((ROOT/f'build/{tag}-board-rt11.log').read_text())
        assert counts==board_counts((ROOT/f'tb/reports/{old}-board-rt11.log').read_text()),(kind,counts)
        assert digest(ROOT/f'build/{tag}-uart.txt')==digest(ROOT/f'tb/reports/{old}-uart.txt')
        assert report['image_sha256']==digest(ROOT/'../lsi11-fpga/images/rt11v503.dsk')
        boards[kind]=counts
        logs += [tag+suffix for suffix in ('-verified.json','-board-inputs.json','-board-build.log','-board-rt11.log','-uart.txt')]
    xm=ROOT/'../lsi11/disks/rt11v5.3/system.dsk'
    xm_sha='9350c62f50e2713f56904b7222f2d829f6bf020cd67e28562e3252a48b6993dd'
    assert digest(xm)==xm_sha and xm.stat().st_size==27540480
    out=ROOT/'tb/reports/cp40';out.mkdir(parents=True,exist_ok=True)
    for name in logs:
        target=out/name;target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(ROOT/'build'/name,target)
    with tarfile.open(out/'test-sources.tgz','w:gz') as archive:
        for p in sorted(sources):
            if not p.startswith(('/', 'build/vendor/', 'synth/reports/')):archive.add(ROOT/p,arcname=p)
    report=dict(checkpoint='CP40: equivalent datapath input/writeback and ALU result muxes',date='2026-09-10',
        synthesis=fits,baseline_production_cp38f=production,baseline_apr_cp39d=apr,
        selected=dict(production='cp40h',apr='cp40i',datapath='both',alu='priority'),changed_production_inputs=changed,
        lut_saved=dict(production=29,apr=10),free=dict(production=dict(lut4=121,slices=56,ebr=1),apr=dict(lut4=22,slices=9,ebr=0)),
        microcode_words=dict(production=954,apr=963),extra_cycles=0,new_state_bits=0,
        proofs=proofs,four_state_cycles_per_variant=266257,four_state_variants=6,
        cpu_miter=cpu['pass_lines'],fis=fis['tests'],fis_corpus_matches_cp37=True,strict_lint_configurations=4,
        cold_rt11fb=boards,counts_and_uart_unchanged=True,physical_board='CP29a',board_programmed=False,
        rt11_xm=dict(boot_tested=False,image_modified=False,path='lsi11/disks/rt11v5.3/system.dsk',sha256=xm_sha),
        sources_sha256={p:digest(ROOT/p) for p in sorted(sources)},
        archived_tests_sha256={str(p.relative_to(ROOT)):digest(p) for p in sorted(out.rglob('*')) if p.is_file()},
        archived_synthesis_sha256={str(p.relative_to(ROOT)):digest(p) for n in fits for p in sorted((ROOT/'synth/reports'/n).iterdir())},
        limits=['No translation/MMRs/abort-restart/PA22 CPU bus or high DMA added.',
                '22 free LUT in the APR build do not establish a fit for the full MMU.',
                'SAT uses two-state controls/data and matched arbitrary RF/Q states; Icarus covers cold unknown RF separately.',
                'FIS uses the existing independent oracle corpus; vendor run is a deterministic 645-case subset.',
                'TRACE with unconstrained external pin delays; no physical programming or RT-11XM boot.'])
    (ROOT/'docs/verification-cp40.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS CP40 evidence: nine fit archives, formal/four-state/CPU/FIS, exact paired FB counts; only ALU/datapath changed')


if __name__=='__main__':main()
