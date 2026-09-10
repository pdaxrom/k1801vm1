#!/usr/bin/env python3
"""Run the established whole-opcode CPU miter through CP39's shared port."""
import hashlib
import json
import subprocess
import sys
from board_common import ROOT
from build_mmu_entry import change


def main():
    out=ROOT/'build/cp39-tests';out.mkdir(exist_ok=True)
    source=(ROOT/'tb/tb_mmu_apr_lookup.v').read_text()
    source=source.replace('dut.engine.apr_ram','apr.ram')
    source=change(source, '    uj11_core #', '''    wire apr_request,apr_grant;
    wire [6:0] apr_address;
    wire [15:0] apr_data;
    uj11_mmu_apr_shared apr(.clk(clk),.reset(reset),.request(1'b0),.entry(6'b0),
        .pdr_select(1'b0),.writing(1'b0),.mark_written(1'b0),.byte_enable(2'b0),.write_data(16'b0),
        .read_data(apr_data),.ready(),.busy(),.lookup_request(apr_request),
        .lookup_address(apr_address),.lookup_grant(apr_grant));
    uj11_core #''')
    source=change(source,'.mmu_enabled(mmu_enabled),', '.apr_request(apr_request),.apr_address(apr_address),.apr_data(apr_data),.apr_grant(apr_grant),.mmu_enabled(mmu_enabled),')
    (out/'tb_mmu_apr_lookup.v').write_text(source)
    runner=(ROOT/'tools/run_mmu_apr_lookup.py').read_text()
    runner=runner.replace("'tb/tb_mmu_apr_lookup.v'", "'build/cp39-tests/tb_mmu_apr_lookup.v'")
    runner=runner.replace('build/cp37-lookup/', 'build/cp39-csr/')
    runner=runner.replace("'rtl/experimental/uj11_mmu_entry.v'", "'build/cp39-csr/uj11_mmu_apr_shared.v','rtl/experimental/uj11_mmu_entry.v'")
    runner=runner.replace('cp37-miter-', 'cp39-miter-')
    runner='import sys\nfrom pathlib import Path\nsys.path.insert(0,str(Path(__file__).resolve().parents[2]/"tools"))\n'+runner
    (out/'run_miter.py').write_text(runner)
    inputs=['tb/tb_mmu_apr_lookup.v','tools/run_mmu_apr_lookup.py','tools/check_mmu_apr_csr_miter.py',
            'build/cp39-tests/run_miter.py','build/cp39-tests/tb_mmu_apr_lookup.v']
    hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs}
    subprocess.run([sys.executable,str(out/'run_miter.py')]+sys.argv[1:],cwd=ROOT,check=True)
    assert hashes=={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in hashes}
    (ROOT/'build/cp39-miter-wrapper.json').write_text(json.dumps(hashes,indent=2)+'\n')


if __name__=='__main__':main()
