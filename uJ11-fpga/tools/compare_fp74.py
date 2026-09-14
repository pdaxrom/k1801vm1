#!/usr/bin/env python3
"""Report measured CP74 SPI costs and CP73 regressions with frozen provenance."""
import csv
import hashlib
import json
from board_common import ROOT
from build_fp11_cp74 import OUT


def run():
    old=ROOT/'tb/reports/cp73/board-sync/metrics.csv';new=OUT/'board-sync/metrics.csv'
    record=json.loads((ROOT/'tb/reports/cp73/archive.json').read_text())
    assert hashlib.sha256(old.read_bytes()).hexdigest()==record['artifacts']['board-sync/metrics.csv']
    def rows(path):
        return {(r['opcode'],r['initial_fps']):r for r in csv.DictReader(path.open())}
    a,b=rows(old),rows(new);assert len(a)==205 and len(b)==241 and a.keys()<=b.keys()
    changes=[];extra=[];equal=0
    for key,r in b.items():
        assert r['entry_to_handler_clocks']=='316' and r['start_fetch_to_return_clocks']=='177'
        if key in a:
            if a[key]==r:equal+=1
            else:changes.append(dict(opcode=key[0],initial_fps=key[1],cp73=int(a[key]['core_clocks']),cp74=int(r['core_clocks']),delta=int(r['core_clocks'])-int(a[key]['core_clocks']),beat_delta=int(r['memory_beats'])-int(a[key]['memory_beats'])))
        else:extra.append(r)
    result=dict(passed=True,previous_operations=len(a),operations=len(b),unchanged_operations=equal,
        changed=changes,new_arithmetic=extra,inputs={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (old,new,ROOT/'tools/compare_fp74.py')})
    (OUT/'comparison.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(unchanged=equal,changed=len(changes),delta_range=[min(r['delta'] for r in changes),max(r['delta'] for r in changes)],arithmetic=extra),indent=2))


if __name__=='__main__':run()
