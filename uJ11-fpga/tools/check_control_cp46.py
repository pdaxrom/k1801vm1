#!/usr/bin/env python3
"""CP46 sequential bridge/APR and PA22-classified bus equivalence."""
import hashlib
import json
import os
import re
import subprocess
from board_common import ROOT
from check_bus_cp45 import cut


def port(source, name):
    a = source.index('.'+name+'(')+len(name)+2; depth = 1; b = a
    while depth:
        if source[b] == '(': depth += 1
        elif source[b] == ')': depth -= 1
        b += 1
    return source[a:b-1]


def apr_probe(source, name):
    source = source.replace('module uj11_mmu_apr_shared', 'module '+name)
    source = source.replace('output wire [15:0] read_data', 'input wire [15:0] read_data')
    source = source.replace('output wire busy);', 'output wire busy, output wire [25:0] ram_probe);')
    a = source.index('    uj11_mmu_apr_ram ram('); b = source.index('));', a)+3
    instance = source[a:b]
    expressions = [port(instance, n) for n in ('enable','address','write_enable','write_data')]
    enable, addr, lanes, data = expressions
    source = source[:a]+f'''    wire probe_en={enable};
    wire [6:0] probe_addr={addr};
    wire [1:0] probe_lanes={lanes};
    wire [15:0] probe_data={data};
    assign ram_probe={{probe_en,probe_en ? probe_addr : 7'b0,
        probe_en ? probe_lanes : 2'b0,
        probe_en && probe_lanes[1] ? probe_data[15:8] : 8'b0,
        probe_en && probe_lanes[0] ? probe_data[7:0] : 8'b0}};
'''+source[b:]
    # The disabled RAM address is intentionally a don't-care. Only ram_probe
    # is an equivalence boundary; do not match its unqualified helper wires.
    for helper in ('probe_en', 'probe_addr', 'probe_lanes', 'probe_data'):
        source = re.sub(r'\b'+helper+r'\b', name+'_'+helper, source)
    return source


def main():
    out = ROOT/'build/cp46-proof'; out.mkdir(parents=True, exist_ok=True)
    inputs = ['tools/check_control_cp46.py', 'tools/check_bus_cp45.py', 'tools/check_board_decode.py',
              'tools/build_control_cp46.py', 'build/cp46-control/inputs.json', 'rtl/uj11_mmu_apr_decode.v']
    suites = []
    for kind, filename, variant in [('bridge','uj11_mmu_relocate.v','phase'),
                                    ('apr','uj11_mmu_apr_shared.v','apr'),
                                    ('bus','uj11_board_bus.v','classified')]:
        texts = []
        for name, choice in [('gold','baseline'), ('gate',variant)]:
            path = f'build/cp46-control/{choice}/{filename}'; inputs.append(path)
            source = (ROOT/path).read_text()
            if kind == 'apr': source = apr_probe(source, name)
            elif kind == 'bus':
                release = re.search(r'if \((boot_release_armed && request && !write &&.*?)\) begin', source)[1]
                if choice == 'classified': release = release.replace('address == 0', 'address[16:0] == 0')
                source = cut(source, name).replace('output apr_request,', 'output release_probe, output apr_request,')
                # The APR decoder now sees a canonical I/O prefix; its raw
                # selected/address signals differ outside qualified I/O reads.
                source = source.replace(' apr_decode(', ' '+name+'_apr_decode(')
                source = source.replace('endmodule', f'''wire ram_region=address[21:17]==0;
wire io_region=&address[21:13];
assign release_probe={release};
endmodule''')
            else: source = source.replace('module uj11_mmu_relocate', 'module '+name)
            path = out/(kind+'-'+name+'.v'); path.write_text(source); texts.append(source)
        for defect in ('none','bad-phase') if kind == 'bridge' else ('none','write-on-lookup') if kind == 'apr' else ('none','ram-alias'):
            gate = texts[1]
            if defect == 'bad-phase': gate = gate.replace('phase[1] <= phase==CAPTURE ||', "phase[1] <= 1'b0 ||")
            if defect == 'write-on-lookup': gate = gate.replace('wire [1:0] gate_probe_lanes=lanes;', "wire [1:0] gate_probe_lanes=lanes | {2{lookup_grant}};")
            if defect == 'ram-alias': gate = gate.replace('ram_region=address[21:17]==0', 'ram_region=address[20:17]==0')
            path = out/(kind+'-'+defect+'.v'); path.write_text(gate)
            script = f'''read_verilog -sv {kind}-gold.v {path.name} {ROOT/'rtl/uj11_mmu_apr_decode.v'}
proc
flatten gold gate
opt_clean
equiv_make gold gate equiv
hierarchy -top equiv
equiv_simple
'''+('equiv_induct -seq 4\n' if kind != 'bus' else '')+'equiv_status -assert\n'
            ys = out/(kind+'-'+defect+'.ys'); ys.write_text(script)
            tag = 'cp46-proof-'+kind+'-'+defect
            with (ROOT/f'build/{tag}.log').open('w') as log:
                run = subprocess.run([str(ROOT/'build/formal/bin/yowasp-yosys'), '-s', ys.name], cwd=out,
                    stdout=log, stderr=subprocess.STDOUT,
                    env=dict(os.environ,YOWASP_CACHE_DIR=str(ROOT/'build/yowasp-cache')))
            text = (ROOT/f'build/{tag}.log').read_text()
            if defect == 'none': assert run.returncode == 0 and 'Equivalence successfully proven!' in text, text[-2200:]
            else: assert run.returncode != 0 and 'unproven' in text, text[-2200:]
            suites.append(dict(kind=kind, defect=defect, tag=tag, returncode=run.returncode))
            print('PASS CP46 proof:', kind, defect, flush=True)
    inputs += [str(p.relative_to(ROOT)) for p in sorted(out.iterdir()) if p.suffix in ('.v','.ys')]
    (ROOT/'build/cp46-proof.json').write_text(json.dumps(dict(tests=suites,
        inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs}), indent=2)+'\n')


if __name__ == '__main__': main()
