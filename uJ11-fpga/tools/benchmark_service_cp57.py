#!/usr/bin/env python3
"""Compare all nine unchanged full-board workloads with frozen CP56 counters."""
import hashlib
import json
from board_common import ROOT
from build_service_cp57 import adapt, OUT
from build_fram_cp52 import replace_once
import run_fram_cp52 as runner


def main():
    core,board=adapt();runner.OUT=OUT
    bench=replace_once((ROOT/'tb/tb_board_bench_cp51.v').read_text(),'sck_edges-start_sck!=12288',
        'sck_edges-start_sck!=(workload==7 ? 12288 : 4224)')
    bench=bench.replace('PASS CP51','PASS CP57').replace('build/cp51-board-bench.json','build/cp57-service/bench-portable.json')
    runs=[]
    for vendor in (False,True):
        kind='vendor' if vendor else 'portable';text=bench
        sources=core+board+['reference/lsi11/spi_fram_model.v']
        if vendor:
            text=replace_once(text,'module tb_board_bench_cp51;',
                "module tb_board_bench_cp51;\n    GSR GSR_INST(.GSR(1'b1)); PUR PUR_INST(.PUR(1'b1));")
            text=text.replace('bench-portable.json','bench-vendor.json')
            sources+=['build/cp57-service/uj11_m0_ebr.v']+['build/vendor/'+n+'.v' for n in ('ODDRXE','DP8KC','GSR','PUR')]
        else:sources+=['rtl/uj11_rom.v','tb/models/ODDRXE.v']
        path=OUT/f'tb_bench_{kind}.v';path.write_text(text)
        runs.append(runner.simulate('tb_board_bench_cp51','bench-'+kind,[str(path.relative_to(ROOT))]+sources,
            ['-DUJ11_VENDOR_ROM'] if vendor else []))
    rows=json.loads((OUT/'bench-portable.json').read_text())
    assert rows==json.loads((OUT/'bench-vendor.json').read_text())
    reference_path=ROOT/'tb/reports/cp56/cp56-tests/result.json'
    if not reference_path.exists():
        choices=list((ROOT/'tb/reports').glob('**/cp56-tests/result.json'))
        assert len(choices)==1,choices
        reference_path=choices[0]
    reference=json.loads(reference_path.read_text())['workloads']
    assert len(rows)==len(reference)==9
    for row,ref in zip(rows,reference):
        assert all(row[k]==ref[k] for k in row),(row,ref)
        row['cpi']=row['microclocks']/row['instructions']
    result=dict(workloads=rows,runs=runs,reference=str(reference_path.relative_to(ROOT)),
        reference_sha256=hashlib.sha256(reference_path.read_bytes()).hexdigest(),
        profile_sha256=hashlib.sha256((OUT/'inputs.json').read_bytes()).hexdigest(),
        driver_sha256=hashlib.sha256((ROOT/'tools/benchmark_service_cp57.py').read_bytes()).hexdigest())
    (OUT/'bench-results.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS CP57: all nine portable/vendor workload counters identical to CP56')

if __name__=='__main__':main()
