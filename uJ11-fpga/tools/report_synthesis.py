#!/usr/bin/env python3
"""Extract actual MAP/PAR/TRACE evidence. Missing results are errors, not zeros."""
import argparse
import hashlib
import json
import re
from pathlib import Path


def extract(map_text, timing_text, par_text):
    result = {}
    for name, pattern in [('lut4','LUT4s'),('ff','registers'),('ebr','block RAMs'),('slices','SLICEs')]:
        m = re.search(r'Number of '+pattern+r':\s+(\d+)\s+out of\s+(\d+)',map_text)
        if not m:
            raise ValueError(f'MAP missing {name}')
        result[name] = int(m[1])
        result[name+'_capacity'] = int(m[2])
    frequencies = re.findall(r'(?:Report|Warning):\s+([0-9.]+)MHz is the maximum frequency',timing_text)
    slacks = re.findall(r'Cumulative negative slack:\s*(-?[0-9.]+)',timing_text)
    if len(frequencies) != 1 or not slacks:
        raise ValueError('Expected one clock preference and completed TRACE slack summary')
    result['fmax_mhz'] = float(frequencies[0])
    result['cumulative_negative_slack_reported'] = [float(s) for s in slacks]
    result['timing_pass'] = all(float(s) == 0 for s in slacks)
    result['fully_routed'] = 'All signals are completely routed' in par_text
    if not result['fully_routed']:
        raise ValueError('PAR has no complete-routing confirmation')
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('map', type=Path)
    p.add_argument('trace', type=Path)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    par = a.map.with_suffix('.par')
    try:
        map_text, timing_text = a.map.read_text(), a.trace.read_text()
        if "Design Module 'uj11_probe_seq'" not in map_text or 'LCMXO2-1200HC' not in map_text:
            raise ValueError('MAP belongs to a different design/device')
        if not re.search(r'FREQUENCY (?:PORT|NET) "clk" 50\.0+ MHz', timing_text):
            raise ValueError('TRACE does not contain the CP1 50 MHz clock preference')
        result = extract(map_text, timing_text, par.read_text())
        result['reports'] = {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                             for path in [a.map,a.trace,par]}
        result['scope'] = 'CP1 ROM + microseq + 40 stimulus FF / 1 observation FF and checksum logic'
        result['device'] = 'LCMXO2-1200HC-4SG32C'
        result['constraint_mhz'] = 50
        result['physical_timing_verified'] = False
        result['inputs'] = json.loads(Path('build/cp1-inputs.json').read_text())
        result['microcode'] = json.loads(Path('microcode/generated/checkpoint_seq.stats.json').read_text())
        a.output.parent.mkdir(parents=True, exist_ok=True)
        a.output.write_text(json.dumps(result,indent=2)+'\n')
    except (ValueError,OSError) as e:
        p.exit(1,f'{e}\n')
    print(json.dumps({k: result[k] for k in ['lut4','ff','ebr','fmax_mhz','timing_pass']},indent=2))
    if not result['timing_pass']:
        p.exit(1,'50 MHz timing failed; preserve reports and investigate before expanding RTL.\n')
    if result['ebr'] != 4:
        p.exit(1,'CP1 did not retain four EBRs; inspect mapping before expanding RTL.\n')


if __name__ == '__main__':
    main()
