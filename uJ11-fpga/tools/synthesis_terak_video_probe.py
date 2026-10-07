#!/usr/bin/env python3
"""Build a standalone, unscaled raster for measuring TERAK monitor cropping."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET
from board_common import ROOT
from report_synthesis import extract, check_clock

# Synplify maps the registered 35-bit diagnostic font into two EBR blocks.
EXPECTED_EBR = 2


def build(name, mode='ntsc'):
    if not re.fullmatch(r'[a-z][a-z0-9-]*', name):
        raise ValueError('Invalid project name')
    out = ROOT/'build'/name
    out.mkdir(parents=True, exist_ok=True)
    if (out/'impl1').exists():
        raise ValueError('Use a fresh implementation directory')
    sources = ['boards/hc7000/terak/uj11_terak_video_probe_board.v',
               'boards/hc7000/terak/uj11_terak_video_probe.v',
               'boards/hc7000/terak/uj11_terak_timing.v',
               'boards/hc7000/video/uj11_pal_pll.v']
    top = 'uj11_terak_video_probe_board'
    clock_name = 'video_clk'
    pal = mode in ('pal50', 'pal50-fast')
    progressive = mode == '240p-measure'
    measure = mode in ('ntsc-measure', '240p-measure')
    faster = mode in ('pal50-fast', 'ntsc-measure', '240p-measure')
    if pal:
        wrapper = 'fast' if faster else 'pal50'
        sources += [f'boards/hc7000/terak/uj11_terak_video_probe_{wrapper}_board.v',
                    'boards/hc7000/video/uj11_pal_timing.v']
        top = f'uj11_terak_video_probe_{wrapper}_board'
        clock_name = 'board.video_clk'
    elif measure:
        wrapper = '240p' if progressive else 'ntsc_measure'
        sources += [f'boards/hc7000/terak/uj11_terak_video_probe_{wrapper}_board.v']
        if progressive:
            sources += ['boards/hc7000/terak/uj11_terak_240p_timing.v']
        top = f'uj11_terak_video_probe_{wrapper}_board'
        clock_name = 'board.video_clk'
    device = 'LCMXO2-7000HC-4TG144C'
    project = ET.Element('BaliProject', version='3.2', title=name, device=device, default_implementation='impl1')
    ET.SubElement(project, 'Options')
    impl = ET.SubElement(project, 'Implementation', title='impl1', dir='impl1', synthesis='synplify', default_strategy='Strategy1')
    ET.SubElement(impl, 'Options', def_top=top)
    for source in sources:
        node = ET.SubElement(impl, 'Source', name=str(ROOT/source), type='Verilog', type_short='Verilog')
        ET.SubElement(node, 'Options')
    preferences = ['boards/hc7000/pins.lpf', 'boards/hc7000/ps2-pins.lpf']
    lpf = '\n'.join((ROOT/p).read_text() for p in preferences)
    lpf = lpf.replace('FREQUENCY NET "clk" 24 MHz ;', f'FREQUENCY NET "{clock_name}" 64 MHz ;')
    (out/'clock.lpf').write_text(lpf)
    node = ET.SubElement(impl, 'Source', name=str(out/'clock.lpf'), type='Logic Preference', type_short='LPF')
    ET.SubElement(node, 'Options')
    strategy = ET.parse(ROOT/'boards/hc7000/synthesis.sty')
    for prop in strategy.getroot().findall('Property'):
        if prop.attrib['name'] == 'PROP_SYN_EdfFrequency':
            prop.set('value', '64')
    for key, value in [('PROP_PAR_PlcIterParDes', '3'), ('PROP_PAR_SaveBestRsltParDes', '1'),
                       ('PROP_PAR_RunTimeReduction', 'False'), ('PROP_PAR_StopZero', 'True')]:
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
    inputs = sources+preferences+['boards/hc7000/synthesis.sty', 'tools/synthesis_terak_video_probe.py',
                                  'tools/report_synthesis.py', 'tools/export_jed.py']
    hashes = {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs}
    for p in ('clock.lpf', 'strategy.sty', 'build.tcl'):
        hashes['generated:'+p] = hashlib.sha256((out/p).read_bytes()).hexdigest()
    manifest = dict(name=name, board='hc7000-lcd-sram', machine='terak-video-probe',
                    top=top, files=hashes, clock_mhz=64, clock_net=clock_name, mode=mode,
                    expected_ebr=EXPECTED_EBR, native_picture=[640, 240],
                    test_picture=[640, 247 if measure else 240],
                    field_hz=50 if pal else 60, frame_lines=262 if progressive else 625 if pal else 525,
                    progressive=progressive, frame_hz=60 if progressive else 25 if pal else 30,
                    picture_start_clock=695 if faster else 685 if pal else 768,
                    picture_clocks=3180 if faster else 3200,
                    pixel_clocks=(3180/640 if faster else 5),
                    pixel_clocks_min=4 if faster else 5, pixel_clocks_max=5,
                    pixel_rate_mhz=640*64/(3180 if faster else 3200),
                    horizontal_width_change_percent=-0.625 if faster else 0,
                    edge_ruler=pal or measure, physical_line_labels=measure,
                    first_field_line_range=[12, 258] if measure else [48, 287] if pal else [18, 257],
                    guest_first_field_line_range=[18, 257] if measure else None,
                    storage_access=False, input_revision_sha256=hashlib.sha256(
                        json.dumps(hashes, sort_keys=True).encode()).hexdigest())
    (out/'inputs.json').write_text(json.dumps(manifest, indent=2)+'\n')
    diamond = Path.home()/'.local/lscc/diamond/3.14/bin/lin64/diamondc'
    with (out/'diamond.log').open('w') as log:
        rc = subprocess.run([str(diamond), str(out/'build.tcl')], cwd=ROOT,
                            env=dict(os.environ, LD_PRELOAD='/lib/x86_64-linux-gnu/libstdc++.so.6'),
                            stdout=log, stderr=subprocess.STDOUT).returncode
    prefix = out/'impl1'/f'{name}_impl1'
    report = dict(inputs=manifest, device=device, diamond_returncode=rc, expected_ebr=EXPECTED_EBR,
                  scope='Standalone unscaled measurement pattern; no guest CPU or storage; physical test pending')
    try:
        timing = prefix.with_suffix('.twr').read_text()
        report.update(check_clock(extract(prefix.with_suffix('.mrp').read_text(), timing,
                                         prefix.with_suffix('.par').read_text(), clock=clock_name),
                                  timing, 64, clock=clock_name))
    except (OSError, ValueError) as error:
        report.update(timing_pass=False, fully_routed=False, incomplete_reason=str(error))
    report['reports'] = {suffix: hashlib.sha256(prefix.with_suffix(suffix).read_bytes()).hexdigest()
                         for suffix in ('.srr', '.mrp', '.par', '.twr', '.ncd') if prefix.with_suffix(suffix).exists()}
    (out/'result.json').write_text(json.dumps(report, indent=2)+'\n')
    if rc or not report['timing_pass'] or not report['fully_routed'] or report['ebr'] != EXPECTED_EBR:
        raise SystemExit('Probe MAP/PAR/TRACE gate failed; preserved reports')
    print(f"Unscaled raster gate passed: {report['lut4']} LUT, {report['ff']} FF, "
          f"{report['ebr']} EBR, Fmax {report['fmax_mhz']} MHz")


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('name')
    parser.add_argument('--mode', choices=('ntsc', 'pal50', 'pal50-fast', 'ntsc-measure', '240p-measure'), default='ntsc')
    args = parser.parse_args()
    build(args.name, args.mode)
