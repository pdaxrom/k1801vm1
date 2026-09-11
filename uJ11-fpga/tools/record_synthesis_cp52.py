#!/usr/bin/env python3
"""Verify actual CP52 MAP/PAR/TRACE and relate the area cost to the tested RTL."""
import hashlib
import json
import re
from board_common import ROOT
from record_cp33 import digest, synthesis


def details(name):
    folder=ROOT/'synth/reports'/name
    result=synthesis(name, current=True)
    raw=json.loads((folder/'result.json').read_text())
    assert raw['mmu'] is False and raw['microcode_words']==954
    assert raw['fram_cp52']==(name=='cp52b')
    mapped=(folder/'design.mrp').read_text()
    timing=(folder/'design.twr').read_text()
    area=(folder/'design.areasrr').read_text()
    result['remaining_lut4']=1280-result['lut4']
    result['remaining_slices']=640-result['slices']
    result['remaining_ebr']=7-result['ebr']
    result['lut_categories']={name:int(re.search(pattern,mapped)[1]) for name,pattern in {
        'logic':r'Number used as logic LUTs:\s+(\d+)',
        'distributed_ram':r'Number used as distributed RAM:\s+(\d+)',
        'carry':r'Number used as ripple logic:\s+(\d+)',
    }.items()}
    assert sum(result['lut_categories'].values())==result['lut4']
    result['pfu_ff']=int(re.search(r'PFU registers:\s+(\d+)',mapped)[1])
    result['pio_ff']=int(re.search(r'PIO registers:\s+(\d+)',mapped)[1])
    assert result['pfu_ff']+result['pio_ff']==result['ff']
    result['pins_free']=0
    assert '21 + 1(JTAGENB) out of 22' in mapped
    match=re.search(r'Delay:\s+([\d.]+)ns\s+\(([\d.]+)% logic, ([\d.]+)% route\), (\d+) logic levels',timing)
    result['critical_path']=dict(delay_ns=float(match[1]),logic_percent=float(match[2]),
        route_percent=float(match[3]),levels=int(match[4]),
        slack_ns=float(re.search(r'Passed: The following path meets requirements by ([\d.]+)ns',timing)[1]),
        source=re.search(r'Source:\s+\S+\s+Port\s+(\S+)',timing)[1],
        destination=re.search(r'Destination:\s+\S+\s+(?:Data in|Port)\s+(\S+)',timing)[1])
    part=next(p for p in area.split('Report for cell ') if 'Instance path:  system.bus.guest_memory\n' in p)
    cells={n:int(c) for n,c in re.findall(r'^\s+(\w+)\s+(\d+)\s+[\d.]+\s*$',part,re.M)}
    result['fram_synplify_primitives']={n:cells.get(n,0) for n in ('ORCALUT4','PFUMX','CCU2D')}
    result['fully_routed']=raw['fully_routed']
    return result,raw['inputs']['files']


def main():
    measured={};gate_inputs={}
    for name in ('cp52a','cp52b'):measured[name],gate_inputs[name]=details(name)
    simulation=json.loads((ROOT/'docs/verification-cp52.json').read_text())
    for p,h in simulation['sources_sha256'].items():assert digest(ROOT/p)==h,p
    for p,h in simulation['archives_sha256'].items():assert digest(ROOT/p)==h,p
    # The actual sequential RTL in Diamond must be the copies checked in the
    # full-board simulation; matching generator names alone is insufficient.
    for p,h in gate_inputs['cp52b'].items():
        if p in simulation['sources_sha256']:assert h==simulation['sources_sha256'][p],p
    for p in ('build/cp52-fram/uj11_board_fram.v','build/cp52-fram/uj11_board_bus.v'):
        assert gate_inputs['cp52b'][p]==simulation['sources_sha256'][p]
    reference=synthesis('cp40h')
    assert all(measured['cp52a'][k]==v for k,v in reference.items()), 'native baseline changed'
    delta={k:measured['cp52b'][k]-measured['cp52a'][k] for k in ('lut4','ff','ebr','slices')}
    delta['fmax_mhz']=round(measured['cp52b']['fmax_mhz']-measured['cp52a']['fmax_mhz'],3)
    transfer=json.loads((ROOT/'build/cp52-transfer.json').read_text())
    inputs=['tools/record_synthesis_cp52.py','tools/record_cp33.py','docs/verification-cp52.json']
    inputs += [str(p.relative_to(ROOT)) for gate in ('cp52a','cp52b')
               for p in (ROOT/'synth/reports'/gate).iterdir() if p.is_file()]
    report=dict(checkpoint='CP52 synthesis completed',date='2026-09-11',device='LCMXO2-1200HC-4SG32C',
        clock_constraint_mhz=29.56,external_pin_delays_constrained=False,measurements=measured,delta=delta,
        microcode_words=954,production_gate='cp52a',production_changed=False,physical_board='CP29a',
        candidate_gate='cp52b',candidate_adopted=False,
        decision='Keep CP52b as measured performance candidate. Reduce area and improve timing margin before default adoption.',
        simulation_manifest_sha256=digest(ROOT/'docs/verification-cp52.json'),
        verified_against_simulation=True,approved_transfer=transfer,
        inputs_sha256={p:digest(ROOT/p) for p in inputs})
    (ROOT/'docs/synthesis-cp52.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(measurements=measured,delta=delta,candidate_adopted=False),indent=2))


if __name__=='__main__':main()
