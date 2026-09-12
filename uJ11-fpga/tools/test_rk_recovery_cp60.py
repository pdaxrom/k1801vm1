#!/usr/bin/env python3
"""Check RK recovery CSR sequencing in portable/vendor models and a negative control."""
import hashlib
import json
import subprocess
from board_common import ROOT
from build_rk_recovery_cp60 import OUT, adapt


def main():
    _, board = adapt()
    records = []
    for mode in ('portable','vendor','broken-clear'):
        sources = ['tb/tb_rk_recovery_cp60.v']+[p for p in board if not p.endswith('/uj11_board.v')]
        defines = []
        if mode == 'vendor':
            defines = ['-DUJ11_VENDOR_ROM']
            sources += ['build/vendor/'+n+'.v' for n in ('DP8KC','GSR','PUR','ODDRXE')]
        else:
            sources += ['tb/models/ODDRXE.v']
        if mode == 'broken-clear':
            path = next(p for p in sources if p.endswith('/uj11_board_bus.v'))
            text = (ROOT/path).read_text()
            old = '(rk_cs1_selected && byte_select[1] && wdata[15] && !rk_service_active)'
            assert text.count(old) == 1
            mutant = OUT/'broken-clear.v'
            mutant.write_text(text.replace(old, "1'b0"))
            sources[sources.index(path)] = str(mutant.relative_to(ROOT))
        binary = OUT/('rk-'+mode+'.vvp')
        build = subprocess.run(['iverilog','-g2012','-s','tb_rk_recovery_cp60','-o',str(binary)]+defines+sources,
                               cwd=ROOT, capture_output=True, text=True)
        (OUT/('rk-'+mode+'-build.log')).write_text(build.stdout+build.stderr)
        build.check_returncode()
        run = subprocess.run(['vvp',str(binary)],cwd=ROOT,capture_output=True,text=True)
        log = OUT/('rk-'+mode+'.log');log.write_text(run.stdout+run.stderr)
        if mode == 'broken-clear':
            assert run.returncode != 0 and 'CCLR generated stale IRQ' in run.stdout
        else:
            run.check_returncode()
            assert 'PASS CP60 RK recovery' in run.stdout
        records.append(dict(mode=mode,returncode=run.returncode,
                            sources={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources},
                            log_sha256=hashlib.sha256(log.read_bytes()).hexdigest()))
        print(run.stdout.strip())
    (OUT/'rk-tests.json').write_text(json.dumps(records,indent=2)+'\n')


if __name__ == '__main__':main()
