#!/usr/bin/env python3
"""Require the CP34 comparison to reject build-only datapath corruptions."""
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    candidate = 'rtl/experimental/uj11_datapath_borrow.v'
    sources = ['tb/tb_mmu_dp_compare.v', 'rtl/uj11_alu.v', 'rtl/uj11_regfile.v',
               'rtl/uj11_datapath.v', candidate, 'synth/machxo2/uj11_mmu_dp_compare.v']
    before = {p: digest(ROOT/p) for p in sources+['tools/check_mmu_dp_negative.py']}
    original = (ROOT/candidate).read_text()
    mutations = [
        ('rf-hold', 'enable && !reset && !borrow &&', 'enable && !reset &&'),
        ('q-hold', 'else if (enable && !borrow)', 'else if (enable)'),
        ('plf', "{9'b0,apr_data[14:8]}", "{9'b0,apr_data[15:9]}"),
        ('va-block', "{9'b0,read_a[12:6]}", "{9'b0,read_a[11:5]}")]
    results = []
    for name, old, new in mutations:
        assert original.count(old) == 1, name
        tag = 'cp34-negative-'+name
        path = ROOT/f'build/{tag}.v'
        path.write_text(original.replace(old, new))
        with (ROOT/f'build/{tag}-build.log').open('w') as log:
            subprocess.run(['iverilog', '-g2012', '-Wall', '-s', 'tb_mmu_dp_compare',
                            '-Ptb_mmu_dp_compare.FULL=0', '-o', f'build/{tag}']+
                           [str(path) if s == candidate else s for s in sources],
                           cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        with (ROOT/f'build/{tag}.log').open('w') as log:
            result = subprocess.run(['vvp', f'build/{tag}'], cwd=ROOT,
                                    stdout=log, stderr=subprocess.STDOUT)
        text = (ROOT/f'build/{tag}.log').read_text()
        assert result.returncode != 0 and 'FATAL' in text and 'MMU datapath' in text, name
        results.append(dict(mutation=name, rejected=True, returncode=result.returncode,
                            mutated_source_sha256=digest(path), failure=text.strip()))
        print(f'PASS mutation rejected: {name}', flush=True)
    assert before == {p: digest(ROOT/p) for p in before}
    report = dict(scope='Build-only CP34 fault-injection controls, CANDIDATE=1 FULL=0',
                  inputs_sha256=before, production_inputs_unchanged=True, tests=results)
    (ROOT/'build/cp34-dp-negative.json').write_text(json.dumps(report, indent=2)+'\n')


if __name__ == '__main__':
    main()
