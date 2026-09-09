#!/usr/bin/env python3
"""Inventory existing HC1200 MAP/TRACE evidence without modifying old projects."""
import argparse
import hashlib
import json
import re
from pathlib import Path


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--microcpu', type=Path, default=Path('/Users/sash/Work/FPGA/microcpu'))
    p.add_argument('--output', type=Path, default=Path('docs/previous_reports.json'))
    a = p.parse_args()
    rows = []
    roots = [a.microcpu/'boards', Path(__file__).resolve().parents[2]/'lsi11-fpga/boards']
    for root in roots:
        for report in sorted(root.rglob('*.mrp')):
            raw = report.read_bytes()
            text = raw.decode(errors='replace')
            def count(name):
                m = re.search(r'Number of '+name+r':\s+(\d+)\s+out of\s+(\d+)', text)
                return {'used': int(m[1]), 'available': int(m[2])} if m else None
            row = {'map_path': str(report), 'map_sha256': hashlib.sha256(raw).hexdigest(),
                   'lut4': count('LUT4s'), 'registers': count('registers'),
                   'slices': count('SLICEs'), 'ebr': count('block RAMs')}
            twr = report.with_suffix('.twr')
            if twr.exists():
                timing = twr.read_bytes()
                row.update({'trace_path': str(twr),
                            'trace_sha256': hashlib.sha256(timing).hexdigest(),
                            'trace_reported_frequencies_mhz': [float(x) for x in re.findall(
                                rb'Report:\s+([0-9.]+)MHz is the maximum frequency', timing)]})
            rows.append(row)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps({'reports': rows, 'note':
        'Historical artifacts; source revision and build completeness not inferred from filenames.'},
        indent=2)+'\n')
    print(f'Inventoried {len(rows)} MAP reports')


if __name__ == '__main__':
    main()
