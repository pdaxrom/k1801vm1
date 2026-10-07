#!/usr/bin/env python3
"""Prepare/run a source-matched Terak MAP/PAR/TRACE gate; export uses export_jed."""
import argparse
import hashlib
import json
import os
import re
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path
from board_common import ROOT
from build_terak import build
from report_synthesis import extract, check_clock


def run(name, prepare_only=False, seeds=15, start=1):
    if not re.fullmatch(r'[a-z][a-z0-9-]*', name):
        raise ValueError('Invalid synthesis name')
    if not 1 <= seeds <= 50:
        raise ValueError('PAR seeds must be 1..50')
    if not 1 <= start <= 100 or start+seeds > 101:
        raise ValueError('PAR cost tables must remain within 1..100')
    out = ROOT/'build'/name
    out.mkdir(parents=True, exist_ok=True)
    if (out/'impl1').exists():
        raise ValueError('Use a fresh implementation directory')
    hardware = build()
    inventory = hardware['inventory']+['boards/hc7000/mmu/uj11_pll50.v',
                                       'boards/hc7000/uj11_pll.v', 'boards/hc7000/video/uj11_pal_pll.v']
    device = 'LCMXO2-7000HC-4TG144C'
    project = ET.Element('BaliProject', version='3.2', title=name, device=device, default_implementation='impl1')
    ET.SubElement(project, 'Options')
    implementation = ET.SubElement(project, 'Implementation', title='impl1', dir='impl1', synthesis='synplify', default_strategy='Strategy1')
    ET.SubElement(implementation, 'Options', def_top='uj11_terak_microcomp')
    for path in inventory:
        node = ET.SubElement(implementation, 'Source', name=str(ROOT/path), type='Verilog', type_short='Verilog')
        ET.SubElement(node, 'Options')
    preferences = ['boards/hc7000/pins.lpf', 'boards/hc7000/mmu/sram-timing-50.lpf',
                   'boards/hc7000/ps2-pins.lpf', 'boards/hc7000/terak/video-timing.lpf',
                   'boards/hc7000/terak/cpu-placement.lpf']
    lpf = '\n'.join((ROOT/p).read_text() for p in preferences)
    lpf, count = re.subn(r'FREQUENCY NET "clk" 24 MHz', 'FREQUENCY NET "clk" 50 MHz', lpf)
    if count != 1:
        raise ValueError('Missing system clock constraint')
    (out/'clock.lpf').write_text(lpf)
    node = ET.SubElement(implementation, 'Source', name=str(out/'clock.lpf'), type='Logic Preference', type_short='LPF')
    ET.SubElement(node, 'Options')
    strategy = ET.parse(ROOT/'boards/hc7000/synthesis.sty')
    for prop in strategy.getroot().findall('Property'):
        if prop.attrib['name'] == 'PROP_SYN_EdfFrequency':
            prop.set('value', '50')
        if prop.attrib['name'] == 'PROP_SYN_EdfArea':
            prop.set('value', 'False')
    # Complete optimization before comparing candidates. Early checkpoint
    # pruning can discard a route that improves enough to meet timing.
    for key, value in [('PROP_PAR_PlcIterParDes', str(seeds)), ('PROP_PAR_SaveBestRsltParDes', '1'),
                       ('PROP_PAR_PlcStCostTblParDes', str(start)), ('PROP_PAR_RunTimeReduction', 'False'),
                       ('PROP_PAR_StopZero', 'True'), ('PROP_PAR_MultiSeedSortMode', 'Worst Slack'),
                       ('PROP_PAR_parPathBased', 'OFF')]:
        ET.SubElement(strategy.getroot(), 'Property', name=key, value=value, time='0')
    strategy.write(out/'strategy.sty', encoding='utf-8', xml_declaration=True)
    ET.SubElement(project, 'Strategy', name='Strategy1', file=str(out/'strategy.sty'))
    ET.ElementTree(project).write(out/f'{name}.ldf', encoding='utf-8', xml_declaration=True)
    (out/'build.tcl').write_text(f'''cd [file dirname [file normalize [info script]]]
if {{[catch {{
prj_project open {name}.ldf
prj_run Synthesis -impl impl1
prj_run Translate -impl impl1
prj_run Map -impl impl1
prj_run PAR -impl impl1
prj_run PAR -impl impl1 -task PARTrace
prj_project close
}} message]}} {{puts stderr $message; exit 1}}
exit 0
''')
    inputs = set(inventory+preferences+['boards/hc7000/synthesis.sty', 'tools/synthesis_terak.py',
                                       'tools/report_synthesis.py', 'tools/export_jed.py'])
    inputs.update(hardware['files'])
    inputs.update(hardware['cpu']['files'])
    inputs.update(hardware['iop']['inputs'])
    hashes = {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sorted(inputs)}
    for p in ('clock.lpf', 'strategy.sty', 'build.tcl'):
        hashes['generated:'+p] = hashlib.sha256((out/p).read_bytes()).hexdigest()
    manifest = dict(name=name, machine='terak', board='hc7000-lcd-sram', top='uj11_terak_microcomp',
                    files=hashes, clock_mhz=50, video_mhz=64, expected_ebr=hardware['expected_ebr'],
                    hardware=hardware, par_seeds=seeds, par_start=start, par_runtime_reduction=False,
                    input_revision_sha256=hashlib.sha256(json.dumps(hashes, sort_keys=True).encode()).hexdigest())
    (out/'inputs.json').write_text(json.dumps(manifest, indent=2)+'\n')
    if prepare_only:
        print(f'Prepared {name}: native Terak display/PS2/QX, 50/64 MHz, {device}')
        return
    diamond = Path(os.environ.get('DIAMOND_HOME', str(Path.home()/'.local/lscc/diamond/3.14')))/'bin/lin64/diamondc'
    if not diamond.exists():
        raise RuntimeError('Diamond 3.14 is unavailable here; prepared project and source hashes are preserved')
    with (out/'diamond.log').open('w') as log:
        rc = subprocess.run([str(diamond), str(out/'build.tcl')], cwd=ROOT,
                            env=dict(os.environ, LD_PRELOAD=os.environ.get('DIAMOND_LIBSTDCPP', '/lib/x86_64-linux-gnu/libstdc++.so.6')),
                            stdout=log, stderr=subprocess.STDOUT).returncode
    prefix = out/'impl1'/f'{name}_impl1'
    report = dict(inputs=manifest, device=device, diamond_returncode=rc, expected_ebr=hardware['expected_ebr'],
                  scope='Terak FPGA timing/resource gate; physical-board test pending')
    try:
        timing = prefix.with_suffix('.twr').read_text()
        mrp, par = prefix.with_suffix('.mrp').read_text(), prefix.with_suffix('.par').read_text()
        report.update(check_clock(extract(mrp, timing, par, clock='clk'), timing, 50))
        video = check_clock(extract(mrp, timing, par, clock='video_clk'), timing, 64, clock='video_clk')
        report['video_fmax_mhz'] = video['fmax_mhz']
        report['timing_pass'] &= video['timing_pass']
        # Missing constraints are failures, including silently renamed CDC cells.
        normalized_timing = re.sub(r'(\d+)\.0+(?=\s+ns\b)', r'\1', timing, flags=re.IGNORECASE)
        for phrase in re.findall(r'(MAXDELAY[^;]+);', lpf):
            scores = re.findall(r'Preference:\s+'+re.escape(phrase.strip())+
                                r'\s*;\s+(\d+) items? scored, (\d+) timing errors detected', normalized_timing, re.IGNORECASE)
            if not scores or any(int(n) == 0 or int(errors) for n, errors in scores):
                raise ValueError('Missing/failing Terak CDC constraint: '+phrase)
        field_cdc = 'BLOCK PATH FROM CELL "system/bus/video/field_toggle" TO CELL "system/bus/field_sync[0]"'
        scores = re.findall(r'Preference:\s+'+re.escape(field_cdc)+r'\s*;\s+(\d+) items? scored', timing)
        if not scores or any(int(n) != 1 for n in scores):
            raise ValueError('Missing Terak BEVNT toggle CDC constraint')
        report['bevent_cdc_constrained'] = True
        # LOCATE preferences must name preserved FF primitives and actually
        # bind their routed sites; merely accepting the LPF is insufficient.
        home = diamond.parents[2]
        environment = dict(os.environ, LD_PRELOAD=os.environ.get('DIAMOND_LIBSTDCPP', '/lib/x86_64-linux-gnu/libstdc++.so.6'),
                           FOUNDRY=str(home/'ispfpga'),
                           LD_LIBRARY_PATH=str(home/'ispfpga/bin/lin64')+':'+str(home/'bin/lin64'))
        placement = prefix.with_suffix('.placement.txt')
        with (out/'placement.log').open('w') as log:
            rc_read = subprocess.run([str(home/'ispfpga/bin/lin64/ncdread'), '-o', str(placement),
                                      str(prefix.with_suffix('.ncd'))], env=environment,
                                     stdout=log, stderr=subprocess.STDOUT).returncode
        if rc_read:
            raise ValueError('Cannot verify Terak CPU register placement')
        sites = {}
        for block in re.split(r'(?=\s*NC_COMP:\d+ -)', placement.read_text()):
            head = re.search(r'NC_COMP:\d+ - <[^>]+>, site = (\S+)', block)
            if head:
                for primitive in re.findall(r'Primitive REG[01] = <([^>]+)>', block):
                    sites[primitive] = head[1]
        wanted = dict(re.findall(r'LOCATE COMP "([^"]+)" SITE "([^"]+)"',
                                 (ROOT/'boards/hc7000/terak/cpu-placement.lpf').read_text()))
        if not wanted or any(sites.get(p) != site for p, site in wanted.items()):
            raise ValueError('Missing/moved Terak CPU register placement constraint')
        report['fixed_register_sites'] = wanted
        for port, kind in [('sram_data[*]', 'INPUT_SETUP')]+[(p, 'CLOCK_TO_OUT') for p in
                ('sram_address[*]', 'sram_data[*]', 'sram_ce_n', 'sram_oe_n', 'sram_we_n', 'sram_lb_n', 'sram_ub_n')]:
            scores = re.findall(r'Preference:\s+'+kind+r' PORT "'+re.escape(port)+r'"[^;]+;\s+(\d+) items? scored', timing)
            if not scores or any(int(n) == 0 for n in scores):
                raise ValueError('Unscored SRAM constraint: '+port)
    except (OSError, ValueError) as error:
        report.update(timing_pass=False, fully_routed=False, incomplete_reason=str(error))
    report['reports'] = {suffix: hashlib.sha256(prefix.with_suffix(suffix).read_bytes()).hexdigest()
                         for suffix in ('.srr', '.areasrr', '.mrp', '.par', '.twr', '.ncd', '.placement.txt')
                         if prefix.with_suffix(suffix).exists()}
    (out/'result.json').write_text(json.dumps(report, indent=2)+'\n')
    if rc or not report['timing_pass'] or not report['fully_routed'] or report['ebr'] != report['expected_ebr']:
        raise SystemExit('Terak MAP/PAR/TRACE gate failed; inspect preserved reports')
    print(f"Terak gate passed: {report['ebr']} EBR")


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('name')
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--seeds', type=int, default=15)
    parser.add_argument('--start', type=int, default=1, help='First PAR cost table (1..100)')
    args = parser.parse_args()
    run(args.name, args.prepare_only, args.seeds, args.start)
