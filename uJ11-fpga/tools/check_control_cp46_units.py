#!/usr/bin/env python3
"""CP46 frozen C relocation oracle, strict lint and four-state bus words."""
import hashlib
import inspect
import json
import subprocess
import types
from board_common import ROOT
from check_bus_cp45 import four_state


def digest(path): return hashlib.sha256((ROOT/path).read_bytes()).hexdigest()


def main():
    inputs = ['tools/check_control_cp46_units.py', 'tools/check_bus_cp45.py',
              'build/cp46-control/inputs.json', 'build/cp46-proof.json',
              'tb/reports/cp44/cp44-units.json', 'tb/tb_relocate_oracle_cp44.v',
              'build/cp46-control/combined/uj11_mmu_relocate.v', 'build/cp44-relocate-oracle.txt']
    previous = json.loads((ROOT/inputs[4]).read_text())
    corpus = inputs[-1]
    assert digest(corpus) == previous['tests'][0]['corpus_sha256']
    tag = 'cp46-oracle'; out = ROOT/'build'/tag; out.mkdir(exist_ok=True)
    files = inputs[5:7]
    with (ROOT/f'build/{tag}-build.log').open('w') as log:
        subprocess.run(['iverilog','-g2012','-Wall','-s','tb_relocate_oracle_cp44',
                        '-o',str(out/'run')]+files, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    with (ROOT/f'build/{tag}.log').open('w') as log:
        subprocess.run(['vvp',str(out/'run')], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    with (ROOT/f'build/{tag}-lint.log').open('w') as log:
        subprocess.run(['verilator','--lint-only','--Wall','--top-module','uj11_mmu_relocate',files[1]],
                        cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    text = (ROOT/f'build/{tag}.log').read_text()
    assert '262144 addresses, 196608 stalled lookup edges' in text
    assert not (ROOT/f'build/{tag}-build.log').read_text()
    lint = (ROOT/f'build/{tag}-lint.log').read_text()
    assert '%Warning' not in lint and '%Error' not in lint, lint
    print(text.splitlines()[0], flush=True)
    # The same real-cone X/Z test as CP45, with separate output paths.
    out = ROOT/'build/cp46-four-state'; out.mkdir(exist_ok=True)
    for original, dest in [('build/cp46-proof/bus-gold.v','gold.v'),
                           ('build/cp46-proof/bus-none.v','classified-none.v'),
                           ('rtl/uj11_mmu_apr_decode.v','apr.v')]:
        inputs.append(original); (out/dest).write_bytes((ROOT/original).read_bytes())
    code = inspect.getsource(four_state).replace('cp45-four-state-', 'cp46-four-state-').replace('CP45', 'CP46')
    driver_path = out/'driver.py'
    driver_path.write_text('import subprocess\nfrom board_common import ROOT\n'+code)
    driver = types.ModuleType('cp46_four_state')
    exec(compile(driver_path.read_text(), str(driver_path), 'exec'), driver.__dict__)
    four = driver.four_state(out, 'classified')
    inputs += [str(p.relative_to(ROOT)) for p in sorted(out.iterdir()) if p.suffix in ('.v','.py')]
    result = dict(oracle_pass_line=text.splitlines()[0], frozen_corpus=True,
                  four_state=four, inputs_sha256={p:digest(p) for p in inputs})
    (ROOT/'build/cp46-units.json').write_text(json.dumps(result, indent=2)+'\n')


if __name__ == '__main__': main()
