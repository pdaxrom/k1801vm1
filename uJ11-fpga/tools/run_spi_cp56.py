#!/usr/bin/env python3
"""CP56 forwarded SPI: vendor primitive, pin timing envelope and board workloads."""
import hashlib
import json
import subprocess
from board_common import ROOT
from build_spi_cp56 import adapt
from build_fram_cp52 import replace_once
import run_fram_cp52 as runner


def main():
    core, board = adapt()
    out = ROOT/'build/cp56-tests'; out.mkdir(parents=True, exist_ok=True); runner.OUT = out
    fram = 'build/cp56-spi/fast/uj11_board_fram.v'
    model = 'reference/lsi11/spi_fram_model.v'
    portable = ['tb/models/ODDRXE.v']
    vendor = ['build/vendor/'+n+'.v' for n in ('ODDRXE', 'GSR', 'PUR')]
    init = "\n    GSR GSR_INST(.GSR(1'b1)); PUR PUR_INST(.PUR(1'b1));"
    renamed = replace_once((ROOT/portable[0]).read_text(), 'module ODDRXE(', 'module ODDRXE_portable(')
    (out/'ODDRXE_portable.v').write_text(renamed)
    runs = [runner.simulate('tb_oddrxe_cp56', 'primitive',
        ['tb/tb_oddrxe_cp56.v', 'build/cp56-tests/ODDRXE_portable.v']+vendor, ['-Wall'])]
    random = replace_once((ROOT/'tb/tb_board_fram.v').read_text(),
        'bank,address,data,value', "bank,1'b1,1'b0,address,data,value")
    protocol = (ROOT/'tb/tb_fram_cp52.v').read_text()
    for tag, top, text in [('random', 'tb_board_fram', random), ('protocol', 'tb_fram_cp52', protocol)]:
        for kind, io in [('portable', portable), ('vendor', vendor)]:
            path = out/f'tb_{tag}_{kind}.v'
            # These count-only fixtures release reset after the vendor PUR interval.
            body = text
            if kind == 'vendor':
                pos = body.index(';', body.index('module '))+1
                body = body[:pos]+init+body[pos:]
                body = body.replace('rst=0;', '#200;rst=0;')
            path.write_text(body)
            runs.append(runner.simulate(top, f'{tag}-{kind}',
                [str(path.relative_to(ROOT)), fram, model]+io, ['-Wall']))
    corners = [
        ('nominal', {}),
        ('fast-late-data', dict(HALF_NS=16.109285, SCK_NS=2, DATA_NS=6, CS_NS=6, RETURN_NS=2)),
        ('fast-late-clock', dict(HALF_NS=16.109285, SCK_NS=6, DATA_NS=2, CS_NS=2, RETURN_NS=2)),
        ('slow', dict(HALF_NS=17.805, SCK_NS=6, DATA_NS=6, CS_NS=6, RETURN_NS=2)),
    ]
    for kind, io in [('portable', portable), ('vendor', vendor)]:
        for name, params in corners:
            flags = ['-Wall']+(['-DUJ11_VENDOR_IO'] if kind == 'vendor' else [])
            flags += [f'-Ptb_spi_cp56.{k}={v}' for k,v in params.items()]
            runs.append(runner.simulate('tb_spi_cp56', f'timed-{kind}-{name}',
                ['tb/tb_spi_cp56.v', fram, model]+io, flags))
    negative = []
    for defect, old, new, message in [
        ('extra-pulse', '(!active || bit_count!=7)', "1'b1", 'timed MISO read'),
        ('stale-bit', 'rdata[7:0]<={rx[6:0],spi_miso};', "rdata[7:0]<={1'b0,rx[6:0]};", 'timed MISO read')]:
        path = out/(defect+'.v'); path.write_text(replace_once((ROOT/fram).read_text(), old, new))
        with (out/(defect+'-build.log')).open('w') as log:
            subprocess.run(['iverilog','-g2012','-Wall','-s','tb_spi_cp56','-o',str(out/defect),
                'tb/tb_spi_cp56.v',str(path),model]+portable,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        with (out/(defect+'.log')).open('w') as log:
            run = subprocess.run(['vvp',str(out/defect)],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
        assert run.returncode != 0 and message in (out/(defect+'.log')).read_text(), defect
        negative.append(dict(defect=defect, returncode=run.returncode, detected=message))
        print('PASS CP56 executable mutation rejected:',defect,flush=True)
    with (out/'lint.log').open('w') as log:
        subprocess.run(['verilator','--lint-only','--Wall','--top-module','uj11_board_fram',fram]+portable,
            cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    bus = replace_once((ROOT/'tb/tb_board_bus.v').read_text(),'        $display("PASS board bus:',
        (ROOT/'tb/bus_fram_cp52_checks.vh').read_text()+'\n        $display("PASS board bus:')
    (out/'tb_bus.v').write_text(bus)
    runs.append(runner.simulate('tb_board_bus','bus',['build/cp56-tests/tb_bus.v']+board+[model]+portable))
    bench = replace_once((ROOT/'tb/tb_board_bench_cp51.v').read_text(),'sck_edges-start_sck!=12288',
        'sck_edges-start_sck!=(workload==7 ? 12288 : 4224)')
    bench = bench.replace('build/cp51-board-bench.json','build/cp56-tests/bench.json').replace('PASS CP51','PASS CP56')
    (out/'tb_bench.v').write_text(bench)
    runs.append(runner.simulate('tb_board_bench_cp51','bench',['build/cp56-tests/tb_bench.v']+
        core+board+['rtl/uj11_rom.v',model]+portable,verilator=True))
    vendor_bench = replace_once(bench,'module tb_board_bench_cp51;', 'module tb_board_bench_cp51;'+init)
    vendor_bench = vendor_bench.replace('build/cp56-tests/bench.json','build/cp56-tests/vendor-bench.json')
    (out/'tb_vendor.v').write_text(vendor_bench)
    runs.append(runner.simulate('tb_board_bench_cp51','vendor',['build/cp56-tests/tb_vendor.v']+core+board+
        ['microcode/generated/uj11_m0_ebr.v',model,'build/vendor/DP8KC.v']+vendor,['-DUJ11_VENDOR_ROM']))
    rows = json.loads((out/'bench.json').read_text())
    assert rows == json.loads((out/'vendor-bench.json').read_text()), 'portable/vendor counters'
    reference = json.loads((ROOT/'tb/reports/cp54/cp54-tests/result.json').read_text())['variants'][0]['workloads']
    assert len(rows) == len(reference) == 9
    for row, ref in zip(rows, reference):
        for key in ('workload','instructions','memory_beats','opcode_fetches','writes','spi_clocks','spi_transactions'):
            assert row[key] == ref[key], (row['workload'],key,row[key],ref[key])
        assert row['microclocks'] < ref['microclocks']
        row.update(cpi=row['microclocks']/row['instructions'],speedup=ref['microclocks']/row['microclocks'],
            nominal_instructions_per_second=29560000*row['instructions']/row['microclocks'])
    # Unmodified Diamond ODDRXE uses four legal implicit wires. Preserve and
    # enumerate those exact library warnings; fail for any new diagnostic.
    library_warnings = {
        f"build/vendor/ODDRXE.v:{line}: warning: implicit definition of wire '{name}'."
        for line,name in [(50,'OP'),(51,'ON'),(52,'RSTB1'),(84,'SR')]
    }
    diagnostics = {}
    for path in out.glob('*build.log'):
        messages = [line for line in path.read_text().splitlines()
            if any(x in line.lower() for x in ('warning','error','sorry'))]
        assert not messages or (len(messages)==4 and set(messages)==library_warnings), (path,messages)
        if messages: diagnostics[path.name] = messages
    inputs = ['tools/run_spi_cp56.py','tools/run_fram_cp52.py','tools/build_spi_cp56.py','tools/build_fram_cp52.py',
        'tools/board_common.py','tb/tb_spi_cp56.v','tb/tb_oddrxe_cp56.v','tb/models/ODDRXE.v',
        'tb/tb_fram_cp52.v','tb/tb_board_fram.v','tb/tb_board_bus.v','tb/bus_fram_cp52_checks.vh',
        'tb/tb_board_bench_cp51.v','microcode/generated/m0.mem','microcode/generated/decode.mem',
        'microcode/generated/firmware.mem','tb/reports/cp54/cp54-tests/result.json','build/cp56-spi/inputs.json']
    inputs += [str(p.relative_to(ROOT)) for p in out.glob('*.v')]
    report = dict(runs=runs,negative=negative,workloads=rows,assumed_pin_delay_envelopes=corners,
        unmodified_vendor_model_diagnostics=diagnostics,
        physical_timing_closed=False,
        inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},
        logs_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in out.glob('*.log') if p.name != 'driver.log'})
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS CP56 local full-board tests: identical portable/vendor results; all nine workloads faster')


if __name__ == '__main__': main()
