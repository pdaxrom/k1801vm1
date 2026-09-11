#!/usr/bin/env python3
"""Verify the measured CP55 gate against its approved and simulated inputs."""
import gzip
import json
import re
from board_common import ROOT, CORE, BOARD, MMU
from record_cp33 import digest, synthesis
from audit_edif_cp53 import audit, parse, child, children, ident


def sequential_cells(text):
    result = {}
    for library in children(parse(text), 'library'):
        for cell in children(library, 'cell'):
            for contents in children(child(cell, 'view'), 'contents'):
                for instance in children(contents, 'instance'):
                    primitive = ident(child(child(instance, 'viewRef'), 'cellRef')[1])
                    if primitive.startswith(('FD1', 'IFS1', 'OFS1')):
                        result[ident(cell[1]) + '/' + ident(instance[1])] = primitive
    return result


def details():
    folder = ROOT/'synth/reports/cp55a'
    measured = synthesis('cp55a', current=True)
    raw = json.loads((folder/'result.json').read_text())
    assert raw['device'] == 'LCMXO2-1200HC-4SG32C'
    assert raw['rx_cp55'] and not raw['mmu']
    assert raw['microcode_words'] == 954 and raw['constraint_mhz'] == 29.56
    assert not raw['external_pin_delays_constrained']
    assert raw['inputs'] == json.loads((ROOT/'build/cp55a/inputs.json').read_text())
    mapped = (folder/'design.mrp').read_text()
    timing = (folder/'design.twr').read_text()
    measured.update(remaining_lut4=1280-measured['lut4'], remaining_slices=640-measured['slices'],
                    remaining_ebr=7-measured['ebr'], fully_routed=raw['fully_routed'])
    measured['lut_categories'] = {key: int(re.search(pattern, mapped)[1]) for key, pattern in {
        'logic': r'Number used as logic LUTs:\s+(\d+)',
        'distributed_ram': r'Number used as distributed RAM:\s+(\d+)',
        'carry': r'Number used as ripple logic:\s+(\d+)',
    }.items()}
    assert sum(measured['lut_categories'].values()) == measured['lut4']
    measured['pfu_ff'] = int(re.search(r'PFU registers:\s+(\d+)', mapped)[1])
    measured['pio_ff'] = int(re.search(r'PIO registers:\s+(\d+)', mapped)[1])
    assert measured['pfu_ff'] + measured['pio_ff'] == measured['ff']
    assert '21 + 1(JTAGENB) out of 22' in mapped
    measured['pins_free'] = 0
    match = re.search(r'Delay:\s+([\d.]+)ns\s+\(([\d.]+)% logic, ([\d.]+)% route\), (\d+) logic levels', timing)
    measured['critical_path'] = dict(delay_ns=float(match[1]), logic_percent=float(match[2]),
        route_percent=float(match[3]), levels=int(match[4]),
        slack_ns=float(re.search(r'Passed: The following path meets requirements by ([\d.]+)ns', timing)[1]),
        source=re.search(r'Source:\s+\S+\s+Port\s+(\S+)', timing)[1],
        destination=re.search(r'Destination:\s+\S+\s+(?:Data in|Port)\s+(\S+)', timing)[1])
    area = (folder/'design.areasrr').read_text()
    primitives = {}
    for instance in ('system.bus', 'system.bus.guest_memory', 'system.cpu.engine'):
        part = next(p for p in area.split('Report for cell ') if f'Instance path:  {instance}\n' in p)
        cells = {n: int(c) for n, c in re.findall(r'^\s+(\w+)\s+(\d+)\s+[\d.]+\s*$', part, re.M)}
        primitives[instance] = {n: cells.get(n, 0) for n in ('ORCALUT4', 'PFUMX', 'CCU2D')}
    measured['synplify_hierarchy_primitives'] = primitives
    srr = (folder/'design.srr').read_text()
    measured['synplify_warning_counts'] = {code: len(re.findall(r'@W: '+code+r'\b', srr))
        for code in sorted(set(re.findall(r'@W:\s+(\w+)', srr)))}
    measured['warning_count_scope'] = 'Printed diagnostics; BN161 is capped at 100, not a total occurrence count.'
    measured['map_warning_count'] = int(re.search(r'Number of warnings:\s+(\d+)', mapped)[1])
    assert int(re.search(r'Number of errors:\s+(\d+)', mapped)[1]) == 0
    edif = ROOT/'build/cp55-netlist/cp55a_impl1.edi'
    measured['edif_audit'] = audit(edif)
    assert len(measured['edif_audit']['fram_ccu2d']) == 5
    old = sequential_cells(gzip.decompress((ROOT/'synth/reports/cp54b/design.edi.gz').read_bytes()).decode())
    new = sequential_cells(edif.read_text())
    removed = {p: cell for p, cell in old.items() if p not in new}
    added = {p: cell for p, cell in new.items() if p not in old}
    expected = {f'uj11_board_fram_Z1/rx_{bit}': 'FD1P3IX' for bit in range(1, 8)}
    expected['uj11_hc1200_microcomp/system_bus_guest_memory_rxio_0'] = 'IFS1P3IX'
    assert removed == expected and not added, (removed, added)
    assert all(new[p] == old[p] for p in new.keys() & old.keys())
    assert len(new) == measured['ff'] and len(old) - len(new) == 8
    measured['removed_sequential_cells'] = removed
    (folder/'design.edi.gz').write_bytes(gzip.compress(edif.read_bytes(), mtime=0))
    return measured, raw['inputs']['files']


def main():
    local = json.loads((ROOT/'docs/verification-cp55.json').read_text())
    for p, h in {**local['sources_sha256'], **local['logs_sha256'], **local['archives_sha256']}.items():
        assert digest(ROOT/p) == h, p
    prior = json.loads((ROOT/'docs/synthesis-cp54.json').read_text())
    for p, h in {**prior['sources_sha256'], **prior['archives_sha256']}.items():
        assert digest(ROOT/p) == h, p
    measured, inputs = details()
    # The pin/OSCH wrapper is synthesized; every other Verilog input was
    # exercised by the completed portable, vendor EBR and cold board tests.
    for p, h in inputs.items():
        if p.endswith('.v') and p != 'boards/hc1200/uj11_microcomp.v':
            assert local['sources_sha256'][p] == h, p
    cold = json.loads((ROOT/'tb/reports/cp55/cp55-final-board-inputs.json').read_text())
    assert cold['rx_cp55'] and not cold['mmu']
    for p in ('build/cp55-rx/baseline/uj11_board_bus.v', 'build/cp55-rx/shared/uj11_board_fram.v'):
        assert cold['files'][p] == inputs[p], p
    previous = synthesis('cp54b')
    baseline = synthesis('cp52a')
    old = prior['measurements']['cp54b']
    assert measured['synplify_warning_counts'] == old['synplify_warning_counts']
    assert measured['map_warning_count'] == old['map_warning_count'] == 3
    assert measured['lut4'] < previous['lut4'] and measured['ff'] == previous['ff'] - 8
    assert measured['ebr'] == previous['ebr']
    assert measured['fmax_mhz'] < previous['fmax_mhz']
    preserved = json.loads((ROOT/'docs/verification-cp50.json').read_text())['sources_sha256']
    for p in CORE + BOARD + MMU:
        assert digest(ROOT/p) == preserved[p], p
    transfer = json.loads((ROOT/'build/cp55-transfer.json').read_text())
    manifest = json.loads((ROOT/'build/cp55-transfer-manifest.json').read_text())
    for p, h in manifest['files_sha256'].items():
        assert digest(ROOT/p) == h, p
    keys = ('lut4', 'ff', 'ebr', 'slices', 'fmax_mhz')
    report = dict(checkpoint='CP55 full native board synthesis completed', date='2026-09-11',
        device='LCMXO2-1200HC-4SG32C', clock_constraint_mhz=29.56, external_pin_delays_constrained=False,
        measurements={'cp55a': measured}, previous_candidate=previous, production_baseline=baseline,
        selected_candidate='cp54b', retained_area_experiment='cp55a', production_gate='cp52a',
        production_changed=False, mmu_changed=False, physical_board='CP29a', board_programmed=False,
        microcode_words=954, selected_candidate_lut_reduction_needed_for_1100=previous['lut4']-1100,
        decision='Use CP54b for further optimization, as requested by the user after comparing CP55: its 32.273 MHz Fmax and 2.843 ns slack are preferred to saving 3 LUT and 8 FF. Retain CP55a as an experiment; do not carry shared RX into the next candidate. Default remains CP52a pending further area reduction.',
        decision_source='User selection after CP55 synthesis comparison, 2026-09-11',
        delta_from_cp54b={k: round(measured[k]-previous[k], 3) for k in keys},
        delta_from_default={k: round(measured[k]-baseline[k], 3) for k in keys},
        verified_against_simulation=True, validation_manifest_sha256=digest(ROOT/'docs/verification-cp55.json'),
        cold_rt11=local['cold_rt11'], cold_counters_and_raw_uart_identical_to_cp54=True,
        approved_transfer=transfer, transfer_completed=True,
        limits=[
            'One complete MAP/PAR/TRACE run per candidate; no seed sweep or hardware frequency measurement.',
            'External pin delays are unconstrained. RX bit 0 moved from a PIO register into the existing PFU result register; physical FRAM input timing is not proven by internal Fmax.',
            'Busy high rdata may change; equivalence is qualified by the documented ready/IDLE/DONE data contract.',
            'All simulation counts are unchanged at the nominal 29.56 MHz; the lower Fmax reduces timing margin, not those measured cycle counts.',
        ],
        sources_sha256={p: digest(ROOT/p) for p in ['tools/record_synthesis_cp55.py', 'tools/record_cp33.py',
            'tools/audit_edif_cp53.py', 'docs/verification-cp55.json', 'docs/synthesis-cp54.json']},
        archives_sha256={str(p.relative_to(ROOT)): digest(p)
            for p in sorted((ROOT/'synth/reports/cp55a').iterdir()) if p.is_file()})
    (ROOT/'docs/synthesis-cp55.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(dict(measurement={k: measured[k] for k in keys},
        slack_ns=measured['critical_path']['slack_ns'], delta=report['delta_from_cp54b'],
        selected_candidate='cp54b', retained_experiment='cp55a', production_changed=False), indent=2))


if __name__ == '__main__':
    main()
