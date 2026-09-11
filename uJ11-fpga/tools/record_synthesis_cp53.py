#!/usr/bin/env python3
"""Archive the measured CP53 gates and selected-board validation with exact hashes."""
import gzip
import json
import re
import shutil
import tarfile
from board_common import ROOT, CORE, BOARD, MMU
from record_cp33 import digest, synthesis
from audit_edif_cp53 import audit

GATES={'cp53a':'increment','cp53b':'compare','cp53c':'both'}


def details(name):
    directory=ROOT/'synth/reports'/name
    measured=synthesis(name,current=True)
    raw=json.loads((directory/'result.json').read_text())
    assert raw['cursor_cp53']==GATES[name] and not raw['mmu']
    assert raw['microcode_words']==954 and raw['constraint_mhz']==29.56
    assert not raw['external_pin_delays_constrained']
    mapped=(directory/'design.mrp').read_text()
    timing=(directory/'design.twr').read_text()
    measured.update(remaining_lut4=1280-measured['lut4'],remaining_slices=640-measured['slices'],
        remaining_ebr=7-measured['ebr'],fully_routed=raw['fully_routed'])
    measured['lut_categories']={key:int(re.search(pattern,mapped)[1]) for key,pattern in {
        'logic':r'Number used as logic LUTs:\s+(\d+)',
        'distributed_ram':r'Number used as distributed RAM:\s+(\d+)',
        'carry':r'Number used as ripple logic:\s+(\d+)',
    }.items()}
    assert sum(measured['lut_categories'].values())==measured['lut4']
    assert '21 + 1(JTAGENB) out of 22' in mapped
    measured['pins_free']=0
    match=re.search(r'Delay:\s+([\d.]+)ns\s+\(([\d.]+)% logic, ([\d.]+)% route\), (\d+) logic levels',timing)
    measured['critical_path']=dict(delay_ns=float(match[1]),logic_percent=float(match[2]),
        route_percent=float(match[3]),levels=int(match[4]),
        slack_ns=float(re.search(r'Passed: The following path meets requirements by ([\d.]+)ns',timing)[1]),
        source=re.search(r'Source:\s+\S+\s+Port\s+(\S+)',timing)[1],
        destination=re.search(r'Destination:\s+\S+\s+(?:Data in|Port)\s+(\S+)',timing)[1])
    area=(directory/'design.areasrr').read_text()
    part=next(p for p in area.split('Report for cell ') if 'Instance path:  system.bus.guest_memory\n' in p)
    cells={n:int(c) for n,c in re.findall(r'^\s+(\w+)\s+(\d+)\s+[\d.]+\s*$',part,re.M)}
    measured['fram_synplify_primitives']={n:cells.get(n,0) for n in ('ORCALUT4','PFUMX','CCU2D')}
    srr=(directory/'design.srr').read_text()
    measured['synplify_warning_counts']={code:len(re.findall(r'@W: '+code+r'\b',srr))
        for code in sorted(set(re.findall(r'@W:\s+(\w+)',srr)))}
    measured['map_warning_count']=int(re.search(r'Number of warnings:\s+(\d+)',mapped)[1])
    assert int(re.search(r'Number of errors:\s+(\d+)',mapped)[1])==0
    edif=ROOT/f'build/cp53-netlist/{name}_impl1.edi'
    measured['edif_audit']=audit(edif)
    (directory/'design.edi.gz').write_bytes(gzip.compress(edif.read_bytes(),mtime=0))
    return measured,raw['inputs']['files']


def main():
    local=json.loads((ROOT/'docs/verification-cp53.json').read_text())
    for path,h in local['archives_sha256'].items():assert digest(ROOT/path)==h,path
    measured={};gate_inputs={}
    for name in GATES:
        measured[name],gate_inputs[name]=details(name)
        # Tie each measured cursor/bus implementation to the earlier tests.
        for path,h in gate_inputs[name].items():
            if path in local['sources_sha256']:assert h==local['sources_sha256'][path],path
        for path in (f'build/cp53-cursor/{GATES[name]}/uj11_board_fram.v',
                     'build/cp53-cursor/baseline/uj11_board_bus.v'):
            assert gate_inputs[name][path]==local['sources_sha256'][path],path
    baseline=synthesis('cp52a');previous=synthesis('cp52b')
    assert measured['cp53a']['lut4']<previous['lut4']
    assert measured['cp53a']['fmax_mhz']>previous['fmax_mhz']
    inputs={}
    def merge(hashes):
        for p,h in hashes.items():
            assert p not in inputs or inputs[p]==h,p
            assert digest(ROOT/p)==h,p
            inputs[p]=h
    vendor=json.loads((ROOT/'build/cp53-vendor/increment/result.json').read_text())
    assert vendor['variant']=='increment' and vendor['portable_counters_identical']
    merge(vendor['inputs_sha256']);merge(vendor['run']['sources_sha256'])
    cold=json.loads((ROOT/'build/cp53-final-board-inputs.json').read_text())
    assert cold['cursor_cp53']=='increment' and not cold['mmu']
    merge(cold['files'])
    for p,h in gate_inputs['cp53a'].items():
        if p.endswith('.v') and p!='boards/hc1200/uj11_microcomp.v':
            assert inputs[p]==h,('tested/synthesized RTL differs',p)
    text=(ROOT/'build/cp53-final-board-rt11.log').read_text()
    match=re.search(r'PASS uJ11 cold RT-11 boot \+ DIR: (\d+) clocks, (\d+) RK commands, (\d+) timer edges, (\d+) UART wire bytes, (\d+) SD reads/(\d+) writes',text)
    counts=re.search(r'BOARD COUNTS retired(\d+) reads(\d+) writes(\d+) FRAM-CS(\d+)',text)
    assert match and counts,'cold boot did not pass'
    boot=dict(zip(['clocks','rk_commands','timer_edges','uart_bytes','sd_reads','sd_writes'],map(int,match.groups())))
    boot.update(zip(['retired','reads','writes','fram_transactions'],map(int,counts.groups())))
    boot['image_sha256']=cold['image_sha256']
    prior=json.loads((ROOT/'docs/verification-cp52.json').read_text())
    assert boot==prior['cold_rt11']['cp52-final'],'CP53 cold counters differ from CP52'
    assert (ROOT/'build/cp53-final-uart.txt').read_bytes()==(ROOT/'tb/reports/cp52/cp52-final-uart.txt').read_bytes()
    assert digest(ROOT/'../lsi11-fpga/images/rt11v503.dsk')==cold['image_sha256']
    assert cold['image_bytes']==27540480
    preserved=json.loads((ROOT/'docs/verification-cp50.json').read_text())['sources_sha256']
    for p in CORE+BOARD+MMU:assert digest(ROOT/p)==preserved[p],p
    out=ROOT/'tb/reports/cp53-final';out.mkdir(parents=True,exist_ok=True)
    files=[f'build/cp53-final-{s}' for s in ('board-inputs.json','board-build.log','board-rt11.log','uart.txt')]
    files += [f'build/cp53-vendor/increment/{s}' for s in ('result.json','bench.json','bench.log','bench-build.log')]
    files += ['build/cp53-transfer.json','build/cp53-transfer-manifest.json']
    for p in files:
        target=out/p.removeprefix('build/')
        target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/p,target)
    paths=files+['tools/record_synthesis_cp53.py','tools/audit_edif_cp53.py','tools/record_cp33.py',
                 'docs/verification-cp53.json','docs/verification-cp52.json']
    merge({p:digest(ROOT/p) for p in paths})
    with tarfile.open(out/'test-sources.tgz','w:gz') as archive:
        for p in sorted(inputs):
            if not p.startswith('tb/reports/') and not p.endswith('.log'):archive.add(ROOT/p,arcname=p)
    report=dict(checkpoint='CP53 full-board synthesis and selected CP53a validation',date='2026-09-11',
        device='LCMXO2-1200HC-4SG32C',clock_constraint_mhz=29.56,external_pin_delays_constrained=False,
        measurements=measured,baseline=baseline,previous_candidate=previous,
        selected_candidate='cp53a',production_gate='cp52a',production_changed=False,mmu_changed=False,
        physical_board='CP29a',microcode_words=954,
        decision='Use CP53a for further sequential FRAM optimization; default remains CP52a until area headroom improves.',
        delta_from_cp52b={k:round(measured['cp53a'][k]-previous[k],3) for k in ('lut4','ff','ebr','slices','fmax_mhz')},
        vendor_workloads=vendor['workloads'],vendor_counters_identical_to_portable=True,
        cold_rt11=boot,cold_counters_and_raw_uart_identical_to_cp52=True,
        cold_speedup_from_native=prior['cold_rt11']['cp52-base']['clocks']/boot['clocks'],
        sources_sha256=inputs,
        archives_sha256={str(p.relative_to(ROOT)):digest(p) for folder in [out]+[ROOT/'synth/reports'/g for g in GATES]
            for p in sorted(folder.rglob('*')) if p.is_file()})
    (ROOT/'docs/synthesis-cp53.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(measurements={g:{k:measured[g][k] for k in ('lut4','ff','ebr','fmax_mhz')} for g in GATES},
        selected='cp53a',cold_rt11=boot,production_changed=False),indent=2))


if __name__=='__main__':main()
