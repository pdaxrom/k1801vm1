#!/usr/bin/env python3
"""Gate portable Diamond results and export a source-matched JED on Linux."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def canonical(statement):
    statement = re.sub(r'(?<![\w\[])([+-]?\d+\.\d+)(?!\w|\])',
                       lambda m: f'{float(m[1]):g}', statement)
    return ' '.join(statement.upper().split())


def phase_preferences(physical):
    return physical+'\n# Diagnostic preferences: 1 X adds zero cycles, preserving original bounds.\n'+(
        'MULTICYCLE FROM CELL "machine/cpu/core/ir[*]" TO CELL "machine/cpu/core/yreg[*]" 1 X ;\n'
        'MULTICYCLE FROM CELL "machine/cpu/core/sop_out[1]" TO CELL "machine/cpu/core/sop_out[2]" 1 X ;\n')


def jed_record(path):
    raw = path.read_bytes()
    text = raw.decode('ascii')
    require(text.count('\x02') == 1 and text.count('\x03') == 1, 'Invalid JEDEC framing')
    start, end = raw.index(b'\x02'), raw.index(b'\x03')
    checksum = re.findall(r'^C([0-9A-Fa-f]{4})\*', text, re.M)
    require(len(checksum) == 1, 'Missing JEDEC fuse checksum')
    count = re.search(r'QF(\d+)\*', text)
    default = re.search(r'(?:^|\*)\s*F([01])\*', text)
    require(count is not None and default is not None, 'Missing fuse configuration')
    count = int(count[1])
    require(count == 1441280, 'Wrong MachXO2-7000 fuse count')
    fuses = bytearray([int(default[1])])*count
    for match in re.finditer(r'(?:^|\*)\s*L(\d+)\s+([01\s]+)\*', text):
        offset = int(match[1])
        bits = bytes(map(int, re.sub(r'\s', '', match[2])))
        require(offset+len(bits) <= count, 'JEDEC data exceeds fuse array')
        fuses[offset:offset+len(bits)] = bits
    actual = sum(sum(bit << j for j, bit in enumerate(fuses[i:i+8]))
                 for i in range(0, count, 8)) & 0xffff
    require(actual == int(checksum[0], 16), 'JEDEC fuse checksum mismatch')
    transmission = raw[end+1:].strip().decode('ascii')
    require(re.fullmatch(r'[0-9A-Fa-f]{4}', transmission) is not None, 'Missing JEDEC transmission checksum')
    require(sum(raw[start:end+1]) & 0xffff == int(transmission, 16), 'JEDEC transmission checksum mismatch')
    return {'bytes': len(raw), 'sha256': sha(path), 'checksum': checksum[0],
            'transmission_checksum': transmission, 'fuses': count}


def check(out):
    inputs = json.loads((out/'inputs.json').read_text())
    for name, digest in inputs['sources'].items():
        path = out/name
        require(path.resolve().is_relative_to(out.resolve()), f'Invalid source path: {name}')
        require(sha(path) == digest, f'Source changed: {name}')
    require((out/'build.exit').read_text().strip() == '0', 'Diamond did not exit successfully')
    prefix = out/'impl1/bk0010_impl1'
    texts = {suffix: prefix.with_suffix(suffix).read_text()
             for suffix in ['.mrp', '.twr', '.par', '.srr', '.prf']}
    mapping, timing = texts['.mrp'], texts['.twr']
    require('LCMXO2-7000HC' in mapping, 'Wrong FPGA family')
    require('All signals are completely routed' in texts['.par'], 'Incomplete routing')
    require('Semantic error' not in mapping and 'ERROR -' not in mapping, 'MAP preference error')
    require('Error:' not in timing, 'TRACE reported a timing violation')
    slacks = re.findall(r'Cumulative negative slack:\s*(\d+)', timing)
    require(len(slacks) == 3 and all(int(n) == 0 for n in slacks), 'Incomplete or failing setup/hold summary')
    resources = {}
    for key, label in [('ff', 'registers'), ('slices', 'SLICEs'), ('lut4', 'LUT4s'), ('ebr', 'block RAMs')]:
        match = re.search(r'Number of '+label+r':\s+(\d+) out of\s+(\d+)', mapping)
        require(match is not None, f'Missing resource count: {key}')
        used, capacity = map(int, match.groups())
        require(0 < used <= capacity, f'Resources do not fit: {key}')
        resources[key] = {'used': used, 'capacity': capacity}
    require(resources['ebr']['used'] == 9, 'Expected eight bootstrap EBRs and one line buffer EBR')
    sections = re.split(r'(?=^Preference:)', timing, flags=re.M)
    # Synplify's generated-clock pass can name the PLL secondary output
    # CLKOS_i_0. Accept this alias only with its explicit physical PLL source.
    clock48 = 'clk48'
    if 'FREQUENCY NET "clk48"' not in texts['.prf']:
        require('Clock Domain: CLKOS_i_0   Source: main_pll/PLLInst_0.CLKOS' in timing,
                '48 MHz clock alias has an unexpected source')
        clock48 = 'CLKOS_i_0'
    preferences = {}
    for section in sections:
        match = re.match(r'Preference:\s*(.*?;)(?: Setup Analysis\.)?\s+(\d+) items? scored(?:, (\d+) timing errors? detected)?\.',
                         section, re.S)
        if match:
            statement, count, errors = match.groups()
            preferences.setdefault(canonical(statement), []).append({
                'items': int(count), 'errors': int(errors or 0), 'section': section})
    # Check every explicit frequency/I/O/mailbox/first-stage preference in the
    # active LPF, in both the setup and hold reports. Never accept empty paths.
    lpf = re.sub(r'#.*', '', (out/'clock.lpf').read_text())
    checked = {}
    for statement in lpf.split(';'):
        statement = statement.strip()
        if not statement.startswith(('FREQUENCY NET', 'INPUT_SETUP', 'CLOCK_TO_OUT', 'MAXDELAY', 'BLOCK PATH')):
            continue
        records = preferences.get(canonical(statement.replace('"clk48"', f'"{clock48}"')+' ;'), [])
        require(len(records) == 2, f'Missing setup/hold preference: {statement}')
        require(all(r['items'] > 0 and r['errors'] == 0 for r in records), f'Unscored/failing preference: {statement}')
        checked[statement] = [r['items'] for r in records]
    clocks = {}
    for net, frequency in [('clk', 24), ('clk48', 48), ('video_clk', 64), ('machine/cpu_p', 24)]:
        physical = clock48 if net == 'clk48' else net
        statement = canonical(f'FREQUENCY NET "{physical}" {frequency} MHz ;')
        section = preferences[statement][0]['section']
        match = re.search(r'Report:\s+([\d.]+)MHz is the maximum frequency', section)
        require(match is not None and float(match[1]) >= frequency, f'Clock does not meet {net} {frequency} MHz')
        clocks[net] = {'required_mhz': frequency, 'fmax_mhz': float(match[1]), 'physical_net': physical}
    cpu = preferences[canonical('FREQUENCY NET "machine/cpu_p" 24 MHz ;')][0]['section']
    require('(from machine/cpu_p -)' in cpu or '(to machine/cpu_p -)' in cpu, 'CPU inverted edge is missing')
    # A dedicated verbose phase report is used below when the ten longest
    # full-period paths do not include any half-period CPU path.
    phase = (out/'phase.twr').read_text()
    require((out/'phase.prf').read_text() == phase_preferences(texts['.prf']), 'Phase preferences changed the physical bounds')
    require('Error:' not in phase and '20.833ns delay constraint' in phase, 'CPU half-period timing is missing/failing')
    phase_paths = re.split(r'(?=(?:Passed|Error):)', phase)
    for direction in [('+', '-'), ('-', '+')]:
        source = r'Source:[^\n]*\(from machine/cpu_p '+re.escape(direction[0])+r'\)'
        destination = r'Destination:[^\n]*\(to machine/cpu_p '+re.escape(direction[1])+r'\)'
        require(any(re.search(source, p) and re.search(destination, p) and
                    re.search(r'20\.83[34]ns delay constraint', p) for p in phase_paths),
                f'Missing CPU phase direction/budget {direction}')
    unconstrained = re.findall(r'Unconstrained Preference:\s*\n\s*([^\n]+)', timing)
    allowed_outputs = re.compile(r'CLOCK_TO_OUT PORT "(?:audio\[\d\]|led_rgb\[\d\]|seg_led_[hl]\[\d\]|tvout\[\d\]|tx|ps2_clock)" CLKNET "(?:clk|video_clk|machine/cpu_p)"\s*$')
    require(all(allowed_outputs.fullmatch(line) for line in unconstrained), 'Unexpected unconstrained path')
    require('0 unconstrained paths found' in timing, 'Unconstrained check did not run')
    constraint_check = (out/'impl1/bk0010_impl1_cck.rpt').read_text()
    require(re.search(r'Inapplicable constraints\s*\*+\s*\(none\)', constraint_check) is not None,
            'Inapplicable synthesis constraint')
    issues = re.search(r'Applicable constraints with issues\s*\*+(.*?)Constraints with matching',
                       constraint_check, re.S).group(1)
    warnings = re.findall(r'@W:([^:]+):[^\n]*\|([^\n]+)', issues)
    expected = {'divider': 'main_pll.PLLInst_0', 'system': 'main_pll.PLLInst_0',
                'pal': 'video_pll.PLLInst_0', 'vm1': 'machine.cpu_positive'}
    require(len(warnings) == 4 and {message.strip() for code, message in warnings if code == 'MT688'} == {
        f'No path from master pin (-source) to source of clock {name} due to black box {cell}'
        for name, cell in expected.items()}, 'Unexpected synthesis constraint warning')
    # Synplify cannot trace through its primitive black boxes, but does retain
    # the generated periods. Check the actual summary, then all routed clocks.
    for name, freq in [('divider', 48), ('system', 24), ('pal', 64), ('vm1', 24)]:
        require(re.search(r'\b'+name+r'\s+'+str(freq)+r'\.0 MHz\s+[\d.]+\s+generated', texts['.srr']) is not None,
                f'Synplify generated clock not applied: {name}')
    require('@W: MT120' not in texts['.srr'], 'Synplify discarded related clock paths')
    result = {'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'device': inputs['device'], 'fully_routed': True, 'setup_errors': 0, 'hold_errors': 0,
              'timing_pass': True, 'physical_board_verified': False, 'resources': resources, 'clocks': clocks,
              'scored_preferences': checked, 'unconstrained_async_outputs': unconstrained,
              'sources': inputs['sources'], 'reports': {s: sha(prefix.with_suffix(s)) for s in texts},
              'phase_report_sha256': sha(out/'phase.twr'), 'ncd_sha256': sha(prefix.with_suffix('.ncd')),
              'synthesis_primitive_warnings': warnings,
              'notes': ['CPU p/n share one DCCA with inverted destination clock pins.',
                        'SRAM and SD external budgets include assumed PCB delay; board measurements remain.',
                        'Audio, LEDs, display, UART, PS/2 inhibit and PAL DAC have no external capture clock.',
                        'GSR inferred from CPU reset only; other reset domains have GSR disabled.']}
    (out/'result.json').write_text(json.dumps(result, indent=2)+'\n')
    return result


def export(out, diamond):
    result = check(out)
    tcl = out/'export.tcl'
    tcl.write_text('cd [file dirname [file normalize [info script]]]\n'
                   'if {[catch {\nprj_project open bk0010.ldf\n'
                   'prj_run Export -impl impl1 -task Jedecgen\nprj_project close\n'
                   '} message]} {puts stderr $message; exit 1}\nexit 0\n')
    with (out/'export.log').open('w') as log:
        subprocess.run([str(diamond), str(tcl)], cwd=out, check=True, stdout=log, stderr=subprocess.STDOUT,
                       env=dict(os.environ, LD_PRELOAD='/lib/x86_64-linux-gnu/libstdc++.so.6'))
    jed = out/'impl1/bk0010_impl1.jed'
    require(sha(out/'impl1/bk0010_impl1.ncd') == result['ncd_sha256'], 'Export changed the routed implementation')
    result['jed'] = jed_record(jed)
    (out/'result.json').write_text(json.dumps(result, indent=2)+'\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['check', 'export', 'phase-preferences'])
    parser.add_argument('project', type=Path)
    parser.add_argument('--diamond', type=Path, default=Path.home()/'.local/lscc/diamond/3.14/bin/lin64/diamondc')
    args = parser.parse_args()
    out = args.project.resolve()
    if args.action == 'phase-preferences':
        (out/'phase.prf').write_text(phase_preferences((out/'impl1/bk0010_impl1.prf').read_text()))
        return
    try:
        result = export(out, args.diamond) if args.action == 'export' else check(out)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        raise SystemExit(f'Diamond gate failed: {error}')
    print(json.dumps({k: v for k, v in result.items() if k in
                      ('device', 'resources', 'clocks', 'setup_errors', 'hold_errors', 'jed')}, indent=2))


if __name__ == '__main__':
    main()
