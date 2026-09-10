#!/usr/bin/env python3
"""Audit CP39 resource gates, CPU CSR/shared-port tests and cold FB evidence."""
import json
import shutil
import tarfile
from board_common import ROOT
from record_cp33 import digest,synthesis
from record_cp36 import board_counts
from record_cp37 import checked,failed_fit


def main():
    fits={n:failed_fit(n) if n in ('cp39a','cp39c') else synthesis(n,n=='cp39d')
          for n in ('cp39a','cp39b','cp39c','cp39d')}
    assert fits['cp39b']==fits['cp39d']
    production=synthesis('cp38f',True)
    baseline=synthesis('cp38g')
    build=checked('cp39-csr/inputs.json')
    assert build['microcode_words']==963 and build['read_mux']=='outer'
    for p,h in build['outputs_sha256'].items():assert digest(ROOT/p)==h
    sources={'tools/record_cp39.py','tools/checkpoint_mmu_apr_csr.py',*build['inputs_sha256'],*build['outputs_sha256']}
    logs=['cp39-csr/inputs.json']
    tests={}
    for suite in ('port','bus','cpu'):
        for variant in ('portable','vendor'):
            tag='cp39-'+suite+'-'+variant
            report=checked(tag+'.json')
            text=(ROOT/'build'/(tag+'.log')).read_text()
            assert all(line in text for line in report['pass_lines']) and len(report['pass_lines'])==2
            if suite=='port':assert '1144373 commands' in text and '2288746 coherent lookup reads' in text
            if suite=='bus':assert '2097152 decode/DMA combinations' in text and '33 beats' in text
            if suite=='cpu':assert '432 readbacks, 720 beats, 4902 lookup reads, 221988 clocks' in text and 'vector4, no write, APR preserved' in text
            tests[tag]=report['pass_lines'];sources.update(report['inputs_sha256'])
            logs += [tag+'.json',tag+'.log',tag+'-build.log']
    for tag in ('cp39-miter-verilator','cp39-lint','cp39-negative'):
        report=checked(tag+'.json');sources.update(report['inputs_sha256'])
        logs.append(tag+'.json')
        if tag=='cp39-negative':
            assert len(report['tests'])==4 and all(t['returncode']!=0 for t in report['tests'])
            logs += [str(p.relative_to(ROOT/'build')) for p in (ROOT/'build/cp39-negative').glob('*.log')]
        else:logs.append(tag+'.log')
    miter=checked('cp39-miter-verilator.json')
    assert miter['cases']==69632 and len(miter['memory_upcs'])==88
    wrapper=json.loads((ROOT/'build/cp39-miter-wrapper.json').read_text())
    for p,h in wrapper.items():assert digest(ROOT/p)==h
    sources.update(wrapper);logs+=['cp39-miter-wrapper.json','cp39-miter-verilator-build.log']
    board=checked('cp39-board-verified.json','files');sources.update(board['files'])
    counts=board_counts((ROOT/'build/cp39-board-rt11.log').read_text())
    assert counts==board_counts((ROOT/'tb/reports/cp38/cp38-apr-board-rt11.log').read_text())
    assert digest(ROOT/'build/cp39-uart.txt')==digest(ROOT/'tb/reports/cp38/cp38-apr-uart.txt')
    assert board['image_sha256']==digest(ROOT/'../lsi11-fpga/images/rt11v503.dsk')
    logs += ['cp39-board-verified.json','cp39-board-inputs.json','cp39-board-build.log',
             'cp39-board-rt11.log','cp39-uart.txt']
    xm=ROOT/'../lsi11/disks/rt11v5.3/system.dsk'
    xm_sha='9350c62f50e2713f56904b7222f2d829f6bf020cd67e28562e3252a48b6993dd'
    assert digest(xm)==xm_sha and xm.stat().st_size==27540480
    out=ROOT/'tb/reports/cp39';out.mkdir(parents=True,exist_ok=True)
    for name in logs:
        target=out/name;target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(ROOT/'build'/name,target)
    with tarfile.open(out/'test-sources.tgz','w:gz') as archive:
        for p in sorted(sources):
            if not p.startswith(('/', 'build/vendor/')):archive.add(ROOT/p,arcname=p)
    report=dict(checkpoint='CP39: CPU APR CSR and shared EBR lookup port',date='2026-09-10',
        synthesis=fits,production_cp38f=production,apr_read_only_cp38g=baseline,
        production_inputs_unchanged=True,selected_experiment='cp39d',
        microcode_words=dict(production=954,experiment=963),
        incremental_resources_from_cp38g=dict(lut4=40,ff=3,ebr=0,slices=16),
        experimental_free=dict(lut4=12,slices=5,ebr=0),
        csr=dict(pairs=48,words=96,par_bits=16,kernel_only_cpu=True,full_mode_selection=False,
                 shared_ebr=True,paired_w_clear=True,automatic_w_update=False),
        tests=tests,cpu_miter=miter['pass_lines'],negative_controls=4,strict_lint_configurations=3,
        latency_clocks=dict(lookup=1,csr_read=2,csr_write_pdr=2,csr_write_par=3,
                            helper_overhead_per_memory_word=10),
        cold_rt11fb=counts,cold_rt11fb_counts_and_uart_unchanged=True,
        physical_board='CP29a',board_programmed=False,production_has_mmu=False,
        manual=dict(path='../doc/EK-DCJ11-UG-PRE_J11ug_Oct83.pdf',
                    sha256=digest(ROOT/'../doc/EK-DCJ11-UG-PRE_J11ug_Oct83.pdf'),sections=['4.5.1','4.5.2','4.9']),
        rt11_xm=dict(boot_tested=False,image_modified=False,path='lsi11/disks/rt11v5.3/system.dsk',sha256=xm_sha),
        sources_sha256={p:digest(ROOT/p) for p in sorted(sources)},
        archived_tests_sha256={str(p.relative_to(ROOT)):digest(p) for p in sorted(out.rglob('*')) if p.is_file()},
        archived_synthesis_sha256={str(p.relative_to(ROOT)):digest(p)
            for n in fits for p in sorted((ROOT/'synth/reports'/n).iterdir())},
        limits=['Experimental CPU CSR integration only; production build unchanged.',
                'No MMRs, translation/PDR checks, automatic W updates, vector250/restart or PA22 CPU bus.',
                'K/S/U I/D CSR tables exist; CPU remains a single kernel register set with kernel unified lookup.',
                'RK DMA alias exclusion is tested, but high DMA and CPU use of upper 64 KiB remain absent.',
                '12 LUT and 5 slices free do not establish that a complete MMU fits HC1200.',
                'TRACE Fmax with unconstrained external pin delays; no physical programming or XM boot.'])
    (ROOT/'docs/verification-cp39.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS CP39 evidence: four fit archives, six unit runs, CPU miter, negatives, cold FB; production unchanged')


if __name__=='__main__':main()
