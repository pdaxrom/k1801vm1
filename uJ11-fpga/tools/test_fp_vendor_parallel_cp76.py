#!/usr/bin/env python3
"""Run the unchanged CP76 vendor test in independent, delay-aligned chunks."""
import csv
import hashlib
import io
import json
import re
import subprocess
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import test_fp_paths_cp76 as paths
from board_common import ROOT


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def run():
    original=subprocess.run;out=paths.OUT/'vendor';artifacts={};partitions=[]
    source=Path(__file__).resolve();source_hash=sha(source)
    def execute(args,**kw):
        if not isinstance(args,list) or args[:2]!=['vvp',str(out/'sim')]:
            return original(args,**kw)
        vector=next(Path(a.split('=',1)[1]) for a in args if a.startswith('+VECTORS='))
        metric=next(Path(a.split('=',1)[1]) for a in args if a.startswith('+METRICS='))
        rows=vector.read_text().splitlines(keepends=True);jobs=[]
        for start in range(0,len(rows),128):
            # ack_wait=(local_case/2)%4: an 8-case multiple preserves phase.
            assert start%8==0
            folder=out/'parts'/f'{start//128:02d}';folder.mkdir(parents=True,exist_ok=True)
            data=rows[start:start+128];(folder/'vectors.txt').write_text(''.join(data))
            jobs.append((start,folder,data))
        def worker(job):
            start,folder,data=job
            cmd=args[:2]+[f'+VECTORS={folder}/vectors.txt',f'+METRICS={folder}/metrics.csv']
            with (folder/'simulation.log').open('w') as log:
                p=original(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
            p.check_returncode();log=(folder/'simulation.log').read_text()
            m=re.search(r'PASS CP76 paths: (\d+) cases / (\d+) checks / (\d+) manual DEC cases',log);assert m,folder
            cases,checks,manual=map(int,m.groups())
            values=[[int(x,16) for x in row.split()] for row in data]
            assert cases==len(data) and checks==sum(68+2*v[60] for v in values)
            assert manual==sum(bool(v[128]) for v in values)
            return dict(start=start,cases=cases,checks=checks,manual=manual,folder=str(folder.relative_to(out)))
        with ThreadPoolExecutor(max_workers=4) as pool:partitions.extend(pool.map(worker,jobs))
        combined=[];seen=set();restored=[]
        for record in partitions:
            folder=out/record['folder'];kw['stdout'].write((folder/'simulation.log').read_text())
            restored.extend((folder/'vectors.txt').read_text().splitlines(keepends=True))
            for row in csv.DictReader((folder/'metrics.csv').open()):
                key=(row['opcode'],bool(int(row['initial_fps'],8)&0o200))
                if key not in seen:seen.add(key);combined.append(row)
            for name in ('vectors.txt','simulation.log','metrics.csv'):
                p=folder/name;artifacts[str(p.relative_to(out))]=sha(p)
        assert restored==rows
        text=io.StringIO();writer=csv.DictWriter(text,fieldnames=('opcode','initial_fps','ack_wait_clocks','core_clocks','memory_beats'))
        writer.writeheader();writer.writerows(combined);metric.write_text(text.getvalue())
        total=[sum(r[n] for r in partitions) for n in ('cases','checks','manual')]
        kw['stdout'].write('PASS CP76 paths: %d cases / %d checks / %d manual DEC cases (all aligned partitions)\n'%tuple(total))
        return subprocess.CompletedProcess(args,0)
    subprocess.run=execute
    try:paths.run(mode='vendor')
    finally:subprocess.run=original
    assert sha(source)==source_hash
    result=json.loads((out/'result.json').read_text());assert result['passed']
    result['inputs'][str(source.relative_to(ROOT))]=source_hash
    result['partition_artifacts']=artifacts;result['partitions']=partitions
    for name,h in artifacts.items():assert sha(out/name)==h
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')


if __name__=='__main__':run()
