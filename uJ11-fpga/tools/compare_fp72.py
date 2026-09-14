#!/usr/bin/env python3
"""Compare measured CP72 SPI FRAM timings with the frozen CP70/CP71 runs."""
import csv
import hashlib
import json
from pathlib import Path
from board_common import ROOT
from build_fp11_cp72 import OUT


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def compare():
    inputs={}
    def measured(path):
        inputs[str(path.relative_to(ROOT))]=sha(path)
        rows=list(csv.DictReader(path.open()))
        result={(int(r['opcode'],8),int(r['initial_fps'],8)):r for r in rows}
        assert len(result)==len(rows),'duplicate benchmark key'
        return result
    previous={}
    for cp in ('cp70','cp71'):
        archive=ROOT/f'tb/reports/{cp}'
        record=json.loads((archive/'archive.json').read_text())
        assert record['passed']
        inputs[str((archive/'archive.json').relative_to(ROOT))]=sha(archive/'archive.json')
        assert sha(archive/'board-sync/metrics.csv')==record['artifacts']['board-sync/metrics.csv']
        previous[cp]=measured(archive/'board-sync/metrics.csv')
    result=json.loads((OUT/'board-sync/result.json').read_text());assert result['passed']
    for name,h in result['inputs'].items():assert sha(ROOT/name)==h,name
    inputs[str((OUT/'board-sync/result.json').relative_to(ROOT))]=sha(OUT/'board-sync/result.json')
    current=measured(OUT/'board-sync/metrics.csv');before=previous['cp71']
    assert current.keys()==before.keys() and len(current)==169
    optimized={(op,fd) for op in (0o172605,0o174205) for fd in (0,0o200)}
    rows=[]
    for key,now in current.items():
        old=before[key]
        assert now['entry_to_handler_clocks']=='316' and now['start_fetch_to_return_clocks']=='177'
        if key not in optimized:
            assert now==old,(key,old,now)
            continue
        a,b=int(old['core_clocks']),int(now['core_clocks'])
        assert b<a and int(now['memory_beats'])<int(old['memory_beats']),key
        baseline=int(previous['cp70'][key]['core_clocks'])
        rows.append(dict(opcode=f'{key[0]:06o}',fps=f'{key[1]:06o}',
                         cp70_clocks=baseline,cp71_clocks=a,cp72_clocks=b,
                         saved_clocks=a-b,reduction_percent=round(100*(a-b)/a,3),
                         cp70_delta=b-baseline,
                         cp71_beats=int(old['memory_beats']),cp72_beats=int(now['memory_beats'])))
    assert len(rows)==4
    # Performance is compared on the exact same architectural fixtures.
    cp71=json.loads((ROOT/'tb/reports/cp71/archive.json').read_text())
    for mode in ('sync','logic'):
        p=OUT/mode/'vectors.txt'
        assert sha(p)==cp71['source_files'][f'build/cp71-fp11/{mode}/vectors.txt']['sha256'],mode
        inputs[str(p.relative_to(ROOT))]=sha(p)
    inputs['tools/compare_fp72.py']=sha(Path(__file__))
    report=dict(passed=True,interval='USER opcode request through START return',
                nominal_hz=29560000,operations=169,unchanged_operations=165,optimized=rows,inputs=inputs)
    (OUT/'comparison.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='inputs'},indent=2))
    return report


if __name__=='__main__':compare()
