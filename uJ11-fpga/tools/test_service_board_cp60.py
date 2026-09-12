#!/usr/bin/env python3
"""Repeat the twelve CP59 service-bank scenarios on CP60's complete board."""
import hashlib
import json
import subprocess
from board_common import ROOT
from build_rk_recovery_cp60 import OUT, adapt
from run_service_board_cp59 import fixture


def main():
    fixture()
    core, board = adapt()
    records = []
    for vendor in (False, True):
        mode = 'vendor' if vendor else 'portable'
        sources = ['tb/tb_service_board_cp59.v']+core+board+['reference/lsi11/spi_fram_model.v']
        defines = []
        if vendor:
            defines = ['-DUJ11_VENDOR_ROM']
            sources += ['build/cp60-rk/uj11_m0_ebr.v']+['build/vendor/'+n+'.v' for n in ('DP8KC','GSR','PUR','ODDRXE')]
        else:
            sources += ['rtl/uj11_rom.v','tb/models/ODDRXE.v']
        binary = OUT/('service-'+mode+'.vvp')
        build = subprocess.run(['iverilog','-g2012','-I'+str(ROOT/'build/cp59-service'),
                                '-s','tb_service_board_cp59','-o',str(binary)]+defines+sources,
                               cwd=ROOT,capture_output=True,text=True)
        (OUT/('service-'+mode+'-build.log')).write_text(build.stdout+build.stderr)
        build.check_returncode()
        run = subprocess.run(['vvp',str(binary)],cwd=ROOT,capture_output=True,text=True)
        log = OUT/('service-'+mode+'.log');log.write_text(run.stdout+run.stderr)
        run.check_returncode()
        assert 'PASS CP59 full board: 12 service scenarios' in run.stdout
        paths = sources+['build/cp59-service/service_cp59_board_cases.vh',
                         'tools/run_service_board_cp59.py','tools/run_service_cp59.py','tools/test_service_board_cp60.py',
                         'build/cp60-rk/inputs.json','build/cp60-rk/m0.mem',
                         'build/cp60-rk/decode.mem','build/cp60-rk/firmware.mem']
        records.append(dict(mode=mode,cases=12,files={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths},
                            log_sha256=hashlib.sha256(log.read_bytes()).hexdigest()))
        print(mode+': '+run.stdout.strip())
    (OUT/'service-tests.json').write_text(json.dumps(records,indent=2)+'\n')


if __name__ == '__main__':main()
