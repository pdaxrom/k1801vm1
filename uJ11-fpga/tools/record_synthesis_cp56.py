#!/usr/bin/env python3
"""Verify CP56 full-board synthesis, external timing budgets and source lineage."""
import gzip
import json
import re
import shutil
from board_common import ROOT, CORE, BOARD, MMU
from record_cp33 import digest, synthesis
from audit_edif_cp53 import audit, parse, child, children, ident
from record_synthesis_cp55 import sequential_cells


def main():
    local=json.loads((ROOT/'docs/verification-cp56.json').read_text())
    for group in ('sources_sha256','logs_sha256','archives_sha256'):
        for p,h in local[group].items(): assert digest(ROOT/p)==h,p
    timing=json.loads((ROOT/'build/cp56-clock-tests/result.json').read_text())
    for group in ('inputs_sha256','logs_sha256'):
        for p,h in timing[group].items(): assert digest(ROOT/p)==h,p
    folder=ROOT/'synth/reports/cp56a'
    measured=synthesis('cp56a',current=True);previous=synthesis('cp54b');baseline=synthesis('cp52a')
    raw=json.loads((folder/'result.json').read_text())
    assert raw['inputs']==json.loads((ROOT/'build/cp56a/inputs.json').read_text())
    assert raw['device']=='LCMXO2-1200HC-4SG32C' and raw['spi_cp56'] and not raw['mmu']
    assert raw['constraint_mhz']==29.56 and raw['microcode_words']==954
    inputs=raw['inputs']['files']
    for p,h in inputs.items():
        if p.endswith('.v') and p!='boards/hc1200/uj11_microcomp.v': assert local['sources_sha256'][p]==h,p
    transfer=json.loads((ROOT/'build/cp56-transfer.json').read_text())
    manifest=json.loads((ROOT/'build/cp56-transfer-manifest.json').read_text())
    for p,h in manifest['files_sha256'].items(): assert digest(ROOT/p)==h,p
    routed=json.loads((ROOT/'build/cp56-timing/routed-inputs.json').read_text())
    assert routed['approved_source_manifest_sha256']==digest(ROOT/'build/cp56-transfer-manifest.json')
    assert routed['synthesis_input_revision']==raw['inputs']['input_revision_sha256']
    assert routed['post_analysis_source_hashes_verified']
    mapped=(folder/'design.mrp').read_text();trace=(folder/'design.twr').read_text()
    measured.update(remaining_lut4=1280-measured['lut4'],remaining_slices=640-measured['slices'],
        remaining_ebr=7-measured['ebr'],fully_routed=raw['fully_routed'],pins_free=0)
    measured['lut_categories']={k:int(re.search(pattern,mapped)[1]) for k,pattern in {
        'logic':r'Number used as logic LUTs:\s+(\d+)',
        'distributed_ram':r'Number used as distributed RAM:\s+(\d+)',
        'carry':r'Number used as ripple logic:\s+(\d+)'}.items()}
    assert sum(measured['lut_categories'].values())==measured['lut4']
    measured['pfu_ff']=int(re.search(r'PFU registers:\s+(\d+)',mapped)[1])
    measured['pio_ff']=int(re.search(r'PIO registers:\s+(\d+)',mapped)[1])
    measured['oddr']=int(re.search(r'Number of ODDR cells:\s+(\d+)',mapped)[1])
    assert measured['pfu_ff']+measured['pio_ff']==measured['ff'] and measured['oddr']==1
    assert '21 + 1(JTAGENB) out of 22' in mapped
    path=re.search(r'Delay:\s+([\d.]+)ns\s+\(([\d.]+)% logic, ([\d.]+)% route\), (\d+) logic levels',trace)
    measured['critical_path']=dict(delay_ns=float(path[1]),logic_percent=float(path[2]),
        route_percent=float(path[3]),levels=int(path[4]),
        slack_ns=float(re.search(r'Passed: The following path meets requirements by ([\d.]+)ns',trace)[1]),
        source=re.search(r'Source:\s+\S+\s+Port\s+(\S+)',trace)[1],
        destination=re.search(r'Destination:\s+\S+\s+(?:Data in|Port)\s+(\S+)',trace)[1])
    srr=(folder/'design.srr').read_text()
    warnings=lambda t:{c:len(re.findall(r'@W: '+c+r'\b',t)) for c in sorted(set(re.findall(r'@W:\s+(\w+)',t)))}
    measured['synplify_warning_counts']=warnings(srr)
    old_warnings=warnings((ROOT/'synth/reports/cp54b/design.srr').read_text())
    assert measured['synplify_warning_counts']==dict(old_warnings,MT246=1)
    measured['added_warning']='MT246: Synplify has no user timing model for ODDRXE; physical Diamond MAP/TRACE supplies the MachXO2 I/O timing model.'
    measured['map_warning_count']=int(re.search(r'Number of warnings:\s+(\d+)',mapped)[1])
    assert measured['map_warning_count']==3 and int(re.search(r'Number of errors:\s+(\d+)',mapped)[1])==0
    netlist=ROOT/'build/cp56-netlist/cp56a_impl1.edi'
    assert digest(netlist)==routed['files_sha256']['build/cp56a/impl1/cp56a_impl1.edi']
    measured['edif_audit']=audit(netlist)
    oddr=[]
    for lib in children(parse(netlist.read_text()),'library'):
        for cell in children(lib,'cell'):
            for contents in children(child(cell,'view'),'contents'):
                for inst in children(contents,'instance'):
                    if ident(child(child(inst,'viewRef'),'cellRef')[1])=='ODDRXE':oddr.append(ident(cell[1])+'/'+ident(inst[1]))
    assert oddr==['uj11_board_fram_Z1/clock_output']
    measured['oddr_instances']=oddr
    old=sequential_cells(gzip.decompress((ROOT/'synth/reports/cp54b/design.edi.gz').read_bytes()).decode())
    new=sequential_cells(netlist.read_text())
    measured['removed_regular_sequential_cells']={p:c for p,c in old.items() if p not in new}
    measured['added_regular_sequential_cells']={p:c for p,c in new.items() if p not in old}
    assert len(new)==measured['ff'] and len(old)-len(new)==2
    (folder/'design.edi.gz').write_bytes(gzip.compress(netlist.read_bytes(),mtime=0))
    preserved=json.loads((ROOT/'docs/verification-cp50.json').read_text())['sources_sha256']
    for p in CORE+BOARD+MMU: assert digest(ROOT/p)==preserved[p],p
    timing_archive=folder/'io-timing';timing_archive.mkdir(exist_ok=True)
    for p in (ROOT/'build/cp56-timing').iterdir():
        if p.suffix in ('.sdf','.vo','.twr'):
            (timing_archive/(p.name+'.gz')).write_bytes(gzip.compress(p.read_bytes(),mtime=0))
        elif p.is_file():shutil.copyfile(p,timing_archive/p.name)
    tests_archive=ROOT/'tb/reports/cp56-clock';tests_archive.mkdir(exist_ok=True)
    for p in (ROOT/'build/cp56-clock-tests').iterdir():
        if p.suffix in ('.log','.v','.json'):shutil.copyfile(p,tests_archive/p.name)
    keys=('lut4','ff','ebr','slices','fmax_mhz')
    result=dict(checkpoint='CP56 full native board synthesis and bounded FRAM timing audit',date='2026-09-11',
        measurements={'cp56a':measured},previous_candidate=previous,production_baseline=baseline,
        selected_candidate='cp54b',retained_speed_experiment='cp56a',physical_board='CP54b',board_programmed=False,
        production_changed=False,mmu_changed=False,microcode_words=954,
        delta_from_cp54b={k:round(measured[k]-previous[k],3) for k in keys},
        verified_against_simulation=True,cold_rt11=local['cold_rt11'],workloads=local['workloads'],
        approved_transfer=transfer,transfer_completed=True,source_inputs_verified_before_and_after=True,
        original_gate_external_pin_delays_constrained=False,additional_trace=timing,
        decision='Retain CP56a as a verified speed experiment. Internal and bounded I/O TRACE pass; only 0.147 ns modeled SCK pulse-width margin remains at the conservative oscillator corner. Keep installed CP54b until external clock waveform is validated or margin is increased.',
        limits=['One complete MAP/PAR run; extra TRACE/SDF exports reuse the same immutable routed NCD.',
            'No SDF-annotated full-board simulation or scope measurements; exported models are symmetric for SCK rising/falling delays.',
            'The 2 ns PCB flight/skew budgets are explicit assumptions, not measured board values.',
            'The input zero-delay/wrong-sign preliminary probes intentionally fail hold and are retained as diagnostic evidence.',
            'CP56 local verification JSON is the historical pre-synthesis snapshot; this report records the completed gate.'],
        sources_sha256={p:digest(ROOT/p) for p in ['tools/record_synthesis_cp56.py','tools/check_timing_cp56.py',
            'tools/record_cp33.py','tools/record_synthesis_cp55.py','tools/audit_edif_cp53.py',
            'docs/verification-cp56.json','build/cp56-clock-tests/result.json']},
        archives_sha256={str(p.relative_to(ROOT)):digest(p) for directory in (folder,tests_archive)
            for p in sorted(directory.rglob('*')) if p.is_file()})
    (ROOT/'docs/synthesis-cp56.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(measured={k:measured[k] for k in keys},delta=result['delta_from_cp54b'],
        internal_slack_ns=measured['critical_path']['slack_ns'],external_budget_pass=timing['trace_budget_pass'],
        pulse_margin_ns=timing['pulse_margin_ns'],physical_board='CP54b'),indent=2))


if __name__=='__main__':main()
