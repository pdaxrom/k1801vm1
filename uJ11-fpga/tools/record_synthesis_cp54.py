#!/usr/bin/env python3
"""Verify both real CP54 board gates against the frozen local validation."""
import gzip
import json
import re
from board_common import ROOT, CORE, BOARD, MMU
from record_cp33 import digest, synthesis
from audit_edif_cp53 import audit

GATES={'cp54a':'dma','cp54b':'dma-ack'}


def details(name):
    folder=ROOT/'synth/reports'/name
    measured=synthesis(name,current=True)
    raw=json.loads((folder/'result.json').read_text())
    assert raw['ack_cp54']==GATES[name] and not raw['mmu']
    assert raw['microcode_words']==954 and raw['constraint_mhz']==29.56
    assert not raw['external_pin_delays_constrained']
    mapped=(folder/'design.mrp').read_text()
    timing=(folder/'design.twr').read_text()
    measured.update(remaining_lut4=1280-measured['lut4'],remaining_slices=640-measured['slices'],
        remaining_ebr=7-measured['ebr'],fully_routed=raw['fully_routed'])
    measured['lut_categories']={key:int(re.search(pattern,mapped)[1]) for key,pattern in {
        'logic':r'Number used as logic LUTs:\s+(\d+)',
        'distributed_ram':r'Number used as distributed RAM:\s+(\d+)',
        'carry':r'Number used as ripple logic:\s+(\d+)',
    }.items()}
    assert sum(measured['lut_categories'].values())==measured['lut4']
    measured['pfu_ff']=int(re.search(r'PFU registers:\s+(\d+)',mapped)[1])
    measured['pio_ff']=int(re.search(r'PIO registers:\s+(\d+)',mapped)[1])
    assert measured['pfu_ff']+measured['pio_ff']==measured['ff']
    assert '21 + 1(JTAGENB) out of 22' in mapped
    measured['pins_free']=0
    match=re.search(r'Delay:\s+([\d.]+)ns\s+\(([\d.]+)% logic, ([\d.]+)% route\), (\d+) logic levels',timing)
    measured['critical_path']=dict(delay_ns=float(match[1]),logic_percent=float(match[2]),
        route_percent=float(match[3]),levels=int(match[4]),
        slack_ns=float(re.search(r'Passed: The following path meets requirements by ([\d.]+)ns',timing)[1]),
        source=re.search(r'Source:\s+\S+\s+Port\s+(\S+)',timing)[1],
        destination=re.search(r'Destination:\s+\S+\s+(?:Data in|Port)\s+(\S+)',timing)[1])
    area=(folder/'design.areasrr').read_text()
    primitives={}
    for instance in ('system.bus','system.bus.guest_memory','system.cpu.engine'):
        part=next(p for p in area.split('Report for cell ') if f'Instance path:  {instance}\n' in p)
        cells={n:int(c) for n,c in re.findall(r'^\s+(\w+)\s+(\d+)\s+[\d.]+\s*$',part,re.M)}
        primitives[instance]={n:cells.get(n,0) for n in ('ORCALUT4','PFUMX','CCU2D')}
    measured['synplify_hierarchy_primitives']=primitives
    srr=(folder/'design.srr').read_text()
    measured['synplify_warning_counts']={code:len(re.findall(r'@W: '+code+r'\b',srr))
        for code in sorted(set(re.findall(r'@W:\s+(\w+)',srr)))}
    measured['map_warning_count']=int(re.search(r'Number of warnings:\s+(\d+)',mapped)[1])
    assert int(re.search(r'Number of errors:\s+(\d+)',mapped)[1])==0
    edif=ROOT/f'build/cp54-netlist/{name}_impl1.edi'
    measured['edif_audit']=audit(edif)
    assert len(measured['edif_audit']['fram_ccu2d'])==5
    (folder/'design.edi.gz').write_bytes(gzip.compress(edif.read_bytes(),mtime=0))
    return measured,raw['inputs']['files']


def main():
    local=json.loads((ROOT/'docs/verification-cp54.json').read_text())
    for p,h in {**local['sources_sha256'],**local['logs_sha256'],**local['archives_sha256']}.items():
        assert digest(ROOT/p)==h,p
    measured={}
    for name,variant in GATES.items():
        measured[name],inputs=details(name)
        prepared=json.loads((ROOT/f'build/{name}/inputs.json').read_text())
        assert prepared==json.loads((ROOT/f'synth/reports/{name}/inputs.json').read_text())
        # OSCH/pin wrapper is synthesized; every other HDL input was exercised
        # by the already completed CP54 portable/vendor/cold board fixtures.
        for p,h in inputs.items():
            if p.endswith('.v') and p!='boards/hc1200/uj11_microcomp.v':
                assert local['sources_sha256'][p]==h,(name,p)
        with (ROOT/f'tb/reports/cp54/cp54-{variant}-board-inputs.json').open() as f:
            cold=json.load(f)
        bus=f'build/cp54-ack/{variant}/uj11_board_bus.v'
        fram='build/cp54-ack/baseline/uj11_board_fram.v'
        for p in (bus,fram):assert cold['files'][p]==inputs[p],p
    previous=synthesis('cp53a');baseline=synthesis('cp52a')
    prior=json.loads((ROOT/'docs/synthesis-cp53.json').read_text())
    chosen=measured['cp54b']
    assert chosen['lut4']<measured['cp54a']['lut4']<previous['lut4']
    assert chosen['fmax_mhz']>measured['cp54a']['fmax_mhz']>previous['fmax_mhz']
    for row in measured.values():
        assert row['ff']==previous['ff'] and row['ebr']==previous['ebr']
        assert row['synplify_warning_counts']==prior['measurements']['cp53a']['synplify_warning_counts']
    preserved=json.loads((ROOT/'docs/verification-cp50.json').read_text())['sources_sha256']
    for p in CORE+BOARD+MMU:assert digest(ROOT/p)==preserved[p],p
    transfer=json.loads((ROOT/'build/cp54-transfer.json').read_text())
    manifest=json.loads((ROOT/'build/cp54-transfer-manifest.json').read_text())
    for p,h in manifest['files_sha256'].items():assert digest(ROOT/p)==h,p
    report=dict(checkpoint='CP54 full native board synthesis completed',date='2026-09-11',
        device='LCMXO2-1200HC-4SG32C',clock_constraint_mhz=29.56,external_pin_delays_constrained=False,
        measurements=measured,previous_candidate=previous,production_baseline=baseline,
        selected_candidate='cp54b',production_gate='cp52a',production_changed=False,mmu_changed=False,
        physical_board='CP29a',microcode_words=954,
        decision='Use CP54b for further sequential FRAM board optimization; default remains CP52a until area headroom improves.',
        delta_from_cp53a={k:round(chosen[k]-previous[k],3) for k in ('lut4','ff','ebr','slices','fmax_mhz')},
        delta_from_default={k:round(chosen[k]-baseline[k],3) for k in ('lut4','ff','ebr','slices','fmax_mhz')},
        verified_against_simulation=True,validation_manifest_sha256=digest(ROOT/'docs/verification-cp54.json'),
        cold_rt11=local['cold_rt11'],cold_counters_and_raw_uart_identical_to_cp53=True,
        approved_transfer=transfer,
        sources_sha256={p:digest(ROOT/p) for p in ['tools/record_synthesis_cp54.py','tools/record_cp33.py',
            'tools/audit_edif_cp53.py','docs/verification-cp54.json','docs/synthesis-cp53.json']},
        archives_sha256={str(p.relative_to(ROOT)):digest(p) for name in GATES
            for p in sorted((ROOT/'synth/reports'/name).iterdir()) if p.is_file()})
    (ROOT/'docs/synthesis-cp54.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(measurements={g:{k:measured[g][k] for k in ('lut4','ff','ebr','fmax_mhz')} for g in GATES},
        selected='cp54b',delta=report['delta_from_cp53a'],production_changed=False),indent=2))


if __name__=='__main__':main()
