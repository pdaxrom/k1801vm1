#!/usr/bin/env python3
"""Freeze CP59d simulation, formal and routed timing evidence with source hashes."""
import gzip
import hashlib
import json
import re
import shutil
import tarfile
from board_common import ROOT
from check_edif_drivers_cp46 import parse, children, child, ident


def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def read(path): return json.loads(path.read_text())
def verify(files):
    for name, digest in files.items(): assert sha(ROOT/name) == digest, name


def check_cs_duplicate(path):
    """Resolve hierarchical EDIF nets: both CS flops must share D/clock/enable/preset."""
    tree = parse(path.read_text()); cells = {}
    for lib in children(tree, 'library') + children(tree, 'external'):
        for cell in children(lib, 'cell'): cells[ident(lib[1]), ident(cell[1])] = cell
    parent = {}; leaves = {}
    def root(x):
        parent.setdefault(x, x)
        if parent[x] != x: parent[x] = root(parent[x])
        return parent[x]
    def join(a, b): parent[root(a)] = root(b)
    def walk(key, prefix):
        content = children(child(cells[key], 'view'), 'contents')
        if not content:
            leaves[prefix] = key[1]
            return
        for net in children(content[0], 'net'):
            net_id = prefix + '/net:' + ident(net[1])
            for port in children(child(net, 'joined'), 'portRef'):
                instance = children(port, 'instanceRef')
                inst_path = prefix + '/' + ident(instance[0][1]) if instance else prefix
                join(net_id, inst_path + '/pin:' + ident(port[1]))
        for inst in children(content[0], 'instance'):
            ref = child(child(inst, 'viewRef'), 'cellRef'); lib = children(ref, 'libraryRef')
            walk((ident(lib[0][1]) if lib else key[0], ident(ref[1])), prefix + '/' + ident(inst[1]))
    walk(('work', 'uj11_hc1200_microcomp'), 'top')
    internal = 'top/system/bus/guest_memory/spi_cs_n'
    output = 'top/system_bus_guest_memory_spi_cs_n_oregio'
    assert leaves[internal] == 'FD1P3JX' and leaves[output] == 'OFS1P3JX'
    pins = [('D', 'D'), ('CK', 'SCLK'), ('SP', 'SP'), ('PD', 'PD')]
    for a, b in pins: assert root(internal+'/pin:'+a) == root(output+'/pin:'+b), (a, b)
    assert root(output+'/pin:Q') == root('top/gpio_mcs_pad/pin:I')
    return dict(internal_cell=leaves[internal], output_cell=leaves[output],
                identical_inputs=pins, output_drives_pad=True, extra_pipeline_stage=False)


def main():
    out = ROOT/'build/cp59-service'; dest = ROOT/'tb/reports/cp59d'
    profile = read(out/'inputs.json'); verify(profile['inputs']); verify(profile['outputs'])
    assert profile['reference'] == 'cp58a' and profile['used_words'] == 1002 and profile['cs_ioff_requested']
    cpu = read(out/'test-results.json'); verify(cpu['files'])
    assert cpu['profile'] == profile and cpu['scenarios_per_mode'] == 91
    for mode in (0, 1, 'vendor'):
        assert 'PASS CP59 CPU: 91 scenarios' in (out/f'test-{mode}.log').read_text()
    for mode in ('portable', 'vendor'):
        verify(read(out/f'board-{mode}-inputs.json')['files'])
        assert 'PASS CP59 full board: 12 service scenarios' in (out/f'board-{mode}.log').read_text()
    proof = read(out/'proof-results.json'); verify(proof['files'])
    assert [r['returncode'] for r in proof['runs']] == [0, 1, 1]
    assert 'Induction step proven: SUCCESS!' in (out/'proof/none.log').read_text()
    fis = read(out/'fis-inputs.json'); verify(fis['inputs']); verify(fis['outputs'])
    assert (fis['portable_cases'], fis['vendor_cases']) == (23840, 645)
    bench = read(out/'bench-results.json')
    assert bench['profile_sha256'] == sha(out/'inputs.json') and len(bench['workloads']) == 9
    for run in bench['runs']: verify(run['sources_sha256'])
    cold = read(ROOT/'build/cp59d-cold-board-inputs.json'); verify(cold['files'])
    assert 'PASS uJ11 cold RT-11 boot + DIR: 173379163 clocks' in (ROOT/'build/cp59d-cold-board-rt11.log').read_text()
    assert (ROOT/'build/cp59d-cold-uart.txt').read_bytes() == (ROOT/'tb/reports/cp56/cp56-final-uart.txt').read_bytes()
    prepared = read(ROOT/'build/cp59d/inputs.json')
    verify({p: h for p, h in prepared['files'].items() if not p.startswith('generated:')})
    measurements = {}
    for name in ('cp59a', 'cp59b', 'cp59c', 'cp59d'):
        folder = ROOT/'synth/reports'/name; measured = read(folder/'result.json')
        assert measured['fully_routed'] and measured['diamond_returncode'] == 0
        with tarfile.open(folder/'source.tgz') as ar:
            for p, digest in measured['inputs']['files'].items():
                data = (folder/p.split(':')[1]).read_bytes() if p.startswith('generated:') else ar.extractfile(p).read()
                assert hashlib.sha256(data).hexdigest() == digest, (name, p)
        for suffix, digest in measured['reports'].items(): assert sha(folder/('design'+suffix)) == digest
        measurements[name] = {k: measured[k] for k in ('lut4', 'ff', 'ebr', 'slices', 'fmax_mhz', 'microcode_words',
                              'timing_pass', 'applied_clock_mhz', 'constraint_matched')}
        measurements[name]['input_revision_sha256'] = measured['inputs']['input_revision_sha256']
        source = ROOT/'build'/name
        for filename in ('mapped-original.prf',):
            if (source/filename).exists(): shutil.copyfile(source/filename, folder/filename)
        shutil.copyfile(source/'impl1'/f'{name}_impl1.prf', folder/'design.prf')
    gate = read(ROOT/'synth/reports/cp59d/result.json')
    assert gate['inputs'] == prepared
    assert (gate['lut4'], gate['ff'], gate['ebr'], gate['slices'], gate['fmax_mhz']) == (1239, 343, 6, 624, 32.531)
    assert gate['timing_pass'] and gate['constraint_matched'] and gate['applied_clock_mhz'] == 31.824
    assert not measurements['cp59a']['timing_pass'] and measurements['cp59b']['timing_pass']
    assert not measurements['cp59c']['timing_pass']
    folder = ROOT/'synth/reports/cp59d'
    for suffix in ('.edi', '.ncd'):
        (folder/('design'+suffix+'.gz')).write_bytes(gzip.compress((ROOT/f'build/cp59d/impl1/cp59d_impl1{suffix}').read_bytes(), mtime=0))
    edif = read(out/'edif-audit.json')
    assert not edif['multiple_drivers'] and not edif['floating'] and len(edif['negative_controls']) == 3
    assert sha(ROOT/'build/cp59d/impl1/cp59d_impl1.edi') == edif['edif_sha256']
    cs = check_cs_duplicate(ROOT/'build/cp59d/impl1/cp59d_impl1.edi')
    (out/'cs-ioff-audit.json').write_text(json.dumps(cs, indent=2)+'\n')
    twr = (folder/'design.twr').read_text()
    assert 'Timing errors: 0 (setup), 0 (hold)' in twr
    assert 'IOL_B4C.CLK to  IOL_B4C.IOLDO gpio_mcs_MGIOL' in twr
    # Raw preferences must exist in both setup and hold sections, not just JSON flags.
    margins = {}
    for label in ('FREQUENCY NET "clk"', 'INPUT_SETUP PORT "gpio_miso"',
                  'CLOCK_TO_OUT PORT "gpio_msck"', 'CLOCK_TO_OUT PORT "gpio_mosi"', 'CLOCK_TO_OUT PORT "gpio_mcs"'):
        sections = twr.split('Preference: '+label)[1:]
        assert len(sections) == 2, label
        values = []
        for section in sections:
            block = section.split('Preference:')[0]
            assert '0 timing errors detected' in block and 'Failed:' not in block
            values.append(float(re.search(r'meets requirements by ([\d.]+)ns', block)[1]))
        margins[label] = dict(setup_ns=values[0], hold_ns=values[1])
    bfolder = ROOT/'synth/reports/cp59b'
    for name in ('budget.prf', 'budget.sh', 'budget.twr'):
        shutil.copyfile(ROOT/'build/cp59-io'/name, bfolder/name)
    assert 'exceeds requirements by 1.475ns' in (bfolder/'budget.twr').read_text()
    dest.mkdir(parents=True, exist_ok=True)
    names = ['inputs.json', 'test-results.json', 'test-0.log', 'test-1.log', 'test-vendor.log',
             'service_cp59_cases.vh', 'service_cp59_board_cases.vh', 'fis-inputs.json',
             'board-portable-inputs.json', 'board-vendor-inputs.json', 'board-portable.log', 'board-vendor.log',
             'bench-results.json', 'bench-portable.log', 'bench-vendor.log', 'bench-portable.json', 'bench-vendor.json',
             'edif-audit.json', 'proof-results.json', 'cs-ioff-audit.json']
    for name in names: shutil.copyfile(out/name, dest/name)
    shutil.copytree(out/'proof', dest/'proof', dirs_exist_ok=True)
    for name in ('cp59-fis-portable.log', 'cp59-fis-vendor.log', 'cp59d-cold-board-inputs.json',
                 'cp59d-cold-board-rt11.log', 'cp59d-cold-uart.txt'):
        shutil.copyfile(ROOT/'build'/name, dest/name)
    for name in ('cp59-fis-portable.csv', 'cp59-fis-vendor.csv'):
        (dest/(name+'.gz')).write_bytes(gzip.compress((ROOT/'build'/name).read_bytes(), mtime=0))
    shutil.copyfile(ROOT/'build/cp59d/inputs.json', dest/'prepared-synthesis-inputs.json')
    paths = {p for p in prepared['files'] if not p.startswith('generated:')}
    for record in (cpu['files'], fis['inputs'], cold['files'], proof['files'], profile['inputs'], profile['outputs']): paths.update(record)
    for mode in ('portable', 'vendor'): paths.update(read(out/f'board-{mode}-inputs.json')['files'])
    for run in bench['runs']: paths.update(run['sources_sha256'])
    paths.update(['tools/record_timing_cp59.py', 'tools/check_edif_drivers_cp46.py', 'tools/run_service_board_cp59.py',
                  'tools/benchmark_service_cp59.py', 'tb/tb_service_board_cp59.v', 'tb/test_board_clock.py',
                  'tb/test_microasm.py', 'tb/test_fis.py'])
    with tarfile.open(dest/'sources.tgz', 'w:gz') as ar:
        for path in sorted(paths): ar.add(ROOT/path, arcname=path)
    delay, logic, route, levels = re.search(r'Delay:\s+([\d.]+)ns\s+\(([\d.]+)% logic, ([\d.]+)% route\), (\d+) logic levels', twr).groups()
    synthesis = dict(selected='cp59d', measurements=measurements,
        remaining=dict(lut4=41, slices=16, ebr=1, microcode_words=22, pio=0),
        delta_from_cp58a=dict(lut4=-7, ff=1, ebr=0, slices=-3, fmax_mhz=1.788, microcode_words=0),
        critical_path=dict(delay_ns=float(delay), logic_percent=float(logic), route_percent=float(route), levels=int(levels)),
        margins=margins, constraint_mhz=31.824, nominal_cpu_and_sck_mhz=29.56,
        internal_osch_tolerance_and_period_jitter_closed=True, fram_setup_hold_closed_under_assumed_pcb_budgets=True,
        pcb_delays_measured=False, physical_sck_pulse_width_closed=False, sck_pulse_width_budget_margin_ns=0.147009,
        unconstrained_other_io_remains=True, installed=False, default_changed=False,
        rejected=dict(cp59a='MAP substituted 29.56 MHz; requested 31.824 gate is invalid',
                      cp59b='Internal PASS; separate external FRAM TRACE failed CS setup by 1.475 ns',
                      cp59c='FRAM constraints included before PAR; CS setup still failed by 0.703 ns'),
        archive_files={str(p.relative_to(ROOT)): sha(p) for n in measurements
                       for p in sorted((ROOT/'synth/reports'/n).iterdir()) if p.is_file()})
    (ROOT/'docs/synthesis-cp59.json').write_text(json.dumps(synthesis, indent=2)+'\n')
    report = dict(checkpoint='CP59d', reference='CP58a', microcode_words=1002,
        prepared_input_revision_sha256=prepared['input_revision_sha256'], cpu_scenarios=91, cpu_modes=3,
        board_scenarios=12, board_modes=2, fis_portable=23840, fis_vendor=645, fis_vendor_stride=37,
        formal=proof['method'], formal_negative_controls=2, edif_nets=edif['nets'], edif_negative_controls=3,
        cs_duplicate=cs, workloads=bench['workloads'], cold_clocks=173379163,
        cold_uart_sha256=sha(dest/'cp59d-cold-uart.txt'), identical_guest_counters_and_uart_to_cp56=True,
        service_clocks=dict(entry=258, start=120, step=119), installed=False,
        files={str(p.relative_to(ROOT)): sha(p) for p in sorted(dest.rglob('*')) if p.is_file()})
    (ROOT/'docs/verification-cp59.json').write_text(json.dumps(report, indent=2)+'\n')
    print('PASS CP59d: exact source hashes, formal/simulation, CS duplicate and full routed FRAM timing verified')


if __name__ == '__main__': main()
