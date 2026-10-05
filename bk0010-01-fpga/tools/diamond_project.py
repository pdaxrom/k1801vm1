#!/usr/bin/env python3
"""Prepare a portable Diamond project in a fresh directory; no programming."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import xml.etree.ElementTree as ET

from build import ROOT, SOC


def prepare(output, device):
    if not re.fullmatch(r'LCMXO2-7000HC-[456]TG144[CI]', device):
        raise ValueError('Expected a MachXO2-7000HC TG144 marking')
    if output.exists():
        raise ValueError('Choose a fresh output directory; existing implementations are preserved')
    sources = SOC + ['boards/hardware-lcd/bk_hardware_lcd.v',
                     'boards/hardware-lcd/bk_main_pll.v', 'boards/hardware-lcd/bk_video_pll.v']
    extras = ['build/boot.hex', 'boards/hardware-lcd/pins.lpf', 'boards/hardware-lcd/cdc.lpf',
              'boards/hardware-lcd/synthesis.sty', 'vendor/manifest.json', 'vendor/cpu11/license.md',
              'boards/hardware-lcd/clocks.fdc', 'tools/diamond_report.py', 'tools/diamond_project.py']
    for item in sources + extras:
        source = ROOT/item
        if not source.is_file():
            raise ValueError(f'Missing {item}; run make build first')
    output.mkdir(parents=True)
    hashes = {}
    for item in sources + extras:
        destination = output/item
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT/item, destination)
        hashes[item] = hashlib.sha256(destination.read_bytes()).hexdigest()
    project = ET.Element('BaliProject', version='3.2', title='bk0010', device=device,
                         default_implementation='impl1')
    ET.SubElement(project, 'Options')
    impl = ET.SubElement(project, 'Implementation', title='impl1', dir='impl1',
                         synthesis='synplify', default_strategy='Strategy1')
    ET.SubElement(impl, 'Options', def_top='bk_hardware_lcd')
    for item in sources:
        node = ET.SubElement(impl, 'Source', name=item, type='Verilog', type_short='Verilog')
        ET.SubElement(node, 'Options')
    (output/'clock.lpf').write_text((ROOT/extras[1]).read_text()+'\n'+(ROOT/extras[2]).read_text())
    hashes['clock.lpf'] = hashlib.sha256((output/'clock.lpf').read_bytes()).hexdigest()
    node = ET.SubElement(impl, 'Source', name='clock.lpf', type='Logic Preference', type_short='LPF')
    ET.SubElement(node, 'Options')
    node = ET.SubElement(impl, 'Source', name='boards/hardware-lcd/clocks.fdc',
                         type='Synplify Design Constraints File', type_short='FDC')
    ET.SubElement(node, 'Options')
    ET.SubElement(project, 'Strategy', name='Strategy1', file=extras[3])
    ET.indent(project)
    ET.ElementTree(project).write(output/'bk0010.ldf', encoding='utf-8', xml_declaration=True)
    (output/'build.tcl').write_text('''cd [file dirname [file normalize [info script]]]
set status [open build.exit w]; puts $status 1; close $status
if {[catch {
prj_project open bk0010.ldf
prj_run Synthesis -impl impl1
prj_run Translate -impl impl1
prj_run Map -impl impl1
prj_run PAR -impl impl1
prj_run PAR -impl impl1 -task PARTrace
prj_project close
exec python3 tools/diamond_report.py phase-preferences .
exec trce -v 10 -c -u 0 -sethld -sp 4 -sphld m -o phase.twr impl1/bk0010_impl1.ncd phase.prf > phase.log 2>@1
set status [open build.exit w]; puts $status 0; close $status
puts [exec python3 tools/diamond_report.py check .]
} message]} {
set status [open build.exit w]; puts $status 1; close $status
puts stderr $message; exit 1
}
exit 0
''')
    generated = {name: hashlib.sha256((output/name).read_bytes()).hexdigest()
                 for name in ['bk0010.ldf', 'build.tcl']}
    (output/'inputs.json').write_text(json.dumps({'device': device, 'sources': hashes,
        'generated': generated,
        'status': 'prepared; synthesis, routing, all constraints and physical board are unverified',
        'acceptance': ['Fully routed; resources fit', 'All frequency, SRAM I/O and MAXDELAY preferences scored',
                       'No setup/hold failures; CPU inverted edge and half period recognized',
                       'Synchronizer stages preserved; reset release checked',
                       'SD SCK/MOSI output and MISO capture checked',
                       'Review unmatched BLOCK PATH and unconstrained connections']}, indent=2)+'\n')
    print(output/'bk0010.ldf')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', default='LCMXO2-7000HC-4TG144I')
    parser.add_argument('--out', type=Path, default=ROOT/'build/diamond')
    args = parser.parse_args()
    prepare(args.out.resolve(), args.device)


if __name__ == '__main__':
    main()
