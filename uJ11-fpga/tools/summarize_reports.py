#!/usr/bin/env python3
"""Rebuild the checkpoint index from archived, actual MAP/PAR/TRACE reports."""
import hashlib
import json
from pathlib import Path
import re
from report_synthesis import extract

ROOT=Path(__file__).resolve().parents[1]


def main():
    rows=[]
    for folder in sorted((ROOT/'synth/reports').iterdir()):
        if not folder.is_dir(): continue
        map_text=(folder/'design.mrp').read_text()
        trace=(folder/'design.twr').read_text()
        row=extract(map_text,trace,(folder/'design.par').read_text())
        inputs=json.loads((folder/'inputs.json').read_text())
        clock=re.search(r'FREQUENCY PORT "clk" ([0-9.]+) MHz',trace)
        top=re.search(r"Design Module '([^']+)'",map_text)
        if not clock or not top or 'LCMXO2-1200HC' not in map_text:
            raise ValueError(f'{folder}: wrong device or missing identity/clock')
        row.update(checkpoint=folder.name,scope=top[1],constraint_mhz=float(clock[1]),
                   input_revision_sha256=inputs['input_revision_sha256'],
                   reports={p.name:hashlib.sha256(p.read_bytes()).hexdigest()
                            for p in [folder/'source.tgz',folder/'inputs.json']+
                            [folder/('design'+ext) for ext in ['.mrp','.par','.twr','.srr']]})
        rows.append(row)
        print(f'{folder.name:12} {row["lut4"]:4} LUT4 {row["ff"]:3} FF {row["ebr"]} EBR '
              f'{row["fmax_mhz"]:7.3f} MHz, {row["constraint_mhz"]:g} MHz '
              f'{"PASS" if row["timing_pass"] else "FAIL"}')
    (ROOT/'docs/synthesis-results.json').write_text(json.dumps(rows,indent=2)+'\n')


if __name__=='__main__': main()
