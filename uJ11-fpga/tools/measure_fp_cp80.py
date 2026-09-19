#!/usr/bin/env python3
"""Compare matching CP79/80 zero-wait samples; not physical FRAM timings."""
import csv
import hashlib
import json
from board_common import ROOT


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def measure():
    old=json.loads((ROOT/'tb/reports/cp79/archive.json').read_text())
    newdir=ROOT/'build/cp80-fp11/sync';new=json.loads((newdir/'result.json').read_text());assert new['passed']
    prior=json.loads((ROOT/'tb/reports/cp79/sync/result.json').read_text())
    assert [(p['begin'],p['end']) for p in prior['partitions']]==[(p['begin'],p['end']) for p in new['partitions']]
    master='build/cp79-fp11/sync/vectors.txt'
    assert sha(newdir/'vectors.txt')==old['source_files'][master]['sha256']
    samples={};inputs={}
    for cp in ('cp79','cp80'):
        values={}
        for path in sorted((ROOT/f'build/{cp}-fp11/sync/parts').glob('*/metrics.csv')):
            name=str(path.relative_to(ROOT));digest=sha(path);inputs[name]=digest
            expected=old['source_files'][name]['sha256'] if cp=='cp79' else next(p['files'][name] for p in new['partitions'] if name in p['files'])
            assert digest==expected
            for row in csv.DictReader(path.open()):
                key=(path.parent.name,row['opcode'],row['initial_fps'],row['ack_wait_clocks'])
                assert key not in values;values[key]=row
        samples[cp]=values
    assert samples['cp79'].keys()==samples['cp80'].keys()
    rows=[]
    for key,before in samples['cp79'].items():
        after=samples['cp80'][key]
        rows.append(dict(part=key[0],opcode=key[1],initial_fps=key[2],ack_wait_clocks=int(key[3]),
            before_clocks=int(before['core_clocks']),after_clocks=int(after['core_clocks']),
            before_beats=int(before['memory_beats']),after_beats=int(after['memory_beats'])))
    out=ROOT/'build/cp80-fp11/timing';out.mkdir(exist_ok=True)
    with (out/'metrics.csv').open('w') as f:
        writer=csv.DictWriter(f,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
    result=dict(passed=True,comparisons=len(rows),fixture='zero-wait RAM; identical vectors and partitions, not SPI FRAM',inputs=inputs,
                artifacts={'metrics.csv':sha(out/'metrics.csv')})
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ('passed','comparisons','fixture')},indent=2))


if __name__=='__main__':measure()
