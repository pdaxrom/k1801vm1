#!/usr/bin/env python3
"""Exercise single-segment HC7000 scan pins with accelerated clock dividers."""
import hashlib
import json
import subprocess
from board_common import ROOT


def run():
    out=ROOT/'build/test-diagnostics'
    out.mkdir(parents=True,exist_ok=True)
    sources=['boards/hc7000/uj11_diagnostics.v','tests/tb_hc7000_diagnostics.v']
    hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources+['tools/test_diagnostics.py']}
    results=[]
    for hz in (32000,768000,1600000):
        executable=out/f'sim-{hz}'
        subprocess.run(['iverilog','-g2012','-s','tb_hc7000_diagnostics',
                        f'-Ptb_hc7000_diagnostics.CLOCK_HZ={hz}','-o',str(executable)]+sources,cwd=ROOT,check=True)
        result=subprocess.run(['vvp',str(executable)],cwd=ROOT,text=True,capture_output=True)
        (out/f'{hz}.log').write_text(result.stdout+result.stderr)
        print(result.stdout,end='')
        result.check_returncode()
        assert 'PASS HC7000 diagnostics' in result.stdout
        results.append(dict(clock_hz=hz,passed=True))
    (out/'result.json').write_text(json.dumps(dict(files=hashes,tests=results,passed=True),indent=2)+'\n')


if __name__=='__main__':
    run()
