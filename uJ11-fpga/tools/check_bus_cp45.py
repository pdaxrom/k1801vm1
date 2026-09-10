#!/usr/bin/env python3
"""Prove CP45 actual PA22 decode/data cones, including all service states."""
import argparse
import hashlib
import json
import os
import re
import subprocess
from board_common import ROOT
from check_board_decode import SELECTORS
from build_bus_cp45 import VARIANTS


def four_state(out, variant):
    fields = dict(address=22, virtual_address=16, uart_rdata=16, ltc_rdata=16,
                  panel_rdata=16, sd_rdata=16, boot_program_word=16, fram_rdata=16,
                  apr_data=16, mmr0_value=16, mmr3_value=6)
    controls = '''write memory_writing instruction_fetch request BOOT_ROM_ENABLE
SD_BOOT_ENABLE RK_SERVICE_ENABLE boot_overlay_active boot_release_armed
rk_service_active rk_service_movb rk_write_command rk_cs1_initialized rk_immediate_done'''.split()
    fields.update({n: 1 for n in controls})
    source = '`timescale 1ns/1ps\nmodule tb;\n'
    source += '\n'.join(f'reg [{w-1}:0] {n};' for n, w in fields.items())
    for name in ('gold', 'gate'):
        source += f'\nwire [49:0] {name}_value;\n{name} {name}_instance('
        source += ','.join(f'.{n}({n})' for n in fields)
        source += f''',.rdata({name}_value[15:0]),.selections({name}_value[41:16]),
.apr_request({name}_value[42]),.apr_pdr({name}_value[43]),.apr_entry({name}_value[49:44]));
'''
    source += '''integer i,pattern,region,checks=0;
reg [31:0] seed=32'h31415926;
initial begin
for(pattern=0;pattern<4;pattern=pattern+1)begin
'''
    for name, width in fields.items():
        if name.endswith(('_rdata', '_value')) or name in ('boot_program_word', 'apr_data'):
            known = f"{width}'h{0x5a5a & ((1 << width)-1):x}"
            mixed = "6'bx01z10" if width == 6 else "16'hax5z"
            source += f"{name}=pattern==0 ? {width}'bx : pattern==1 ? {width}'bz : pattern==2 ? {known} : {mixed};\n"
    source += '''for(region=0;region<4;region=region+1)begin
for(i=0;i<8192;i=i+1)begin
    seed={seed[30:0],seed[31]^seed[21]^seed[1]^seed[0]};
    address=region==0 ? 22'h3fe000+22'(i) : region==1 ? 22'(i) :
            region==2 ? 22'h3e000+22'(i) : 22'h1e000+22'(i);
    virtual_address={3'b111,i[12:0]};
'''
    source += '{'+','.join(controls)+'}=seed[13:0];\n'
    source += '''#1;
    if(gold_value!==gate_value)$fatal(1,"four-state mismatch PA%h controls%h got%h expected%h",address,seed[13:0],gate_value,gold_value);
    checks=checks+1;
end end end
$display("PASS CP45 four-state data: %0d cases, X/Z selected and unselected device words",checks);
$finish;end
endmodule
'''
    (out/'four_state.v').write_text(source)
    tag = 'cp45-four-state-'+variant
    with (ROOT/f'build/{tag}-build.log').open('w') as log:
        subprocess.run(['iverilog', '-g2012', '-s', 'tb', '-o', tag, 'gold.v',
                        variant+'-none.v', 'apr.v', 'four_state.v'], cwd=out,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    with (ROOT/f'build/{tag}.log').open('w') as log:
        result = subprocess.run(['vvp', tag], cwd=out, stdout=log, stderr=subprocess.STDOUT)
    text = (ROOT/f'build/{tag}.log').read_text()
    if variant in ('parallel', 'paired'):
        # These two masked memory muxes pass binary equivalence but turn a
        # selected Z word into X. Retain the counterexample and reject them.
        assert result.returncode != 0 and 'four-state mismatch' in text and 'zzzz' in text, text
        print('REJECT CP45 four-state:', variant, 'selected Z becomes X', flush=True)
    else:
        assert result.returncode == 0 and 'PASS CP45 four-state data: 131072 cases' in text, text
        print('PASS CP45 four-state:', variant, flush=True)
    assert not (ROOT/f'build/{tag}-build.log').read_text()
    return dict(variant=variant, cases=131072 if result.returncode == 0 else None,
                returncode=result.returncode, tag=tag)


def cut(source, name):
    constants = source[source.index('\tlocalparam'):source.index('\n\twire [15:0] word_address')]
    declarations = source[source.index('\twire [15:0] word_address'):source.index('\twire uart_strobe')]
    declarations = re.sub(r'^\treg .*?;\n', '', declarations, flags=re.M)
    for module in ('uj11_mmr0_control', 'uj11_mmr3'):
        declarations = re.sub(r'    '+module+r' \w+\(.*?\);', '', declarations, flags=re.S)
    declarations = re.sub(r'    wire \[(15|5):0\] mmr[03]_value;', '', declarations)
    declarations = declarations.replace('    assign map22=mmr3_value[4];', '')
    a = source.index('\talways @(*) begin\n\t\tcase (word_address')
    rom = source[a:source.index('\n\tgenerate if (UART_XO2)', a)]
    a = source.index('\t// Decodes are mutually exclusive')
    mux = source[a:source.index('\n\tassign acknowledge', a)]
    selectors = SELECTORS+['apr_selected', 'mmr0_selected', 'mmr3_selected', 'mmu_bypass']
    text = f'''module {name}(
    input [21:0] address, input [15:0] virtual_address,
    input [15:0] uart_rdata, ltc_rdata, panel_rdata, sd_rdata, boot_program_word,
    input [15:0] fram_rdata, apr_data, mmr0_value, input [5:0] mmr3_value,
    input write, memory_writing, instruction_fetch, request,
    input BOOT_ROM_ENABLE, SD_BOOT_ENABLE, RK_SERVICE_ENABLE,
    input boot_overlay_active, boot_release_armed, rk_service_active, rk_service_movb,
    input rk_write_command, rk_cs1_initialized, rk_immediate_done,
    output [15:0] rdata, output [{len(selectors)-1}:0] selections,
    output apr_request, apr_pdr, output [5:0] apr_entry);
    reg [15:0] local_rdata;
    wire mmu_bypass;
'''+constants+declarations+rom+mux+'\nassign selections={'+','.join(selectors)+'};\nendmodule\n'
    # Avoid implicit correspondence of private nodes in equiv_make. Only
    # observable outputs must be identical; raw unselected ROM data need not.
    private = set(re.findall(r'\b(?:wire|reg)\s+(?:\[[^]]+\]\s*)?(\w+)', text))
    return re.sub(r'\b\w+\b', lambda m: name+'_'+m[0] if m[0] in private else m[0], text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--yosys', default=str(ROOT/'build/formal/bin/yowasp-yosys'))
    args = parser.parse_args()
    out = ROOT/'build/cp45-proof'; out.mkdir(parents=True, exist_ok=True)
    baseline = ROOT/'build/cp45-bus/baseline/uj11_board_bus.v'
    (out/'gold.v').write_text(cut(baseline.read_text(), 'gold'))
    (out/'apr.v').write_text((ROOT/'rtl/uj11_mmu_apr_decode.v').read_text())
    script = '''read_verilog -sv gold.v gate.v apr.v
proc
flatten gold gate
opt_clean
equiv_make gold gate equiv
hierarchy -top equiv
equiv_simple
equiv_status -assert
'''
    (out/'proof.ys').write_text(script)
    tests = []
    inputs = ['tools/check_bus_cp45.py', 'tools/check_board_decode.py', 'tools/build_bus_cp45.py',
              'build/cp45-bus/inputs.json', 'rtl/uj11_mmu_apr_decode.v', str(baseline.relative_to(ROOT))]
    for variant in VARIANTS[1:]:
        path = f'build/cp45-bus/{variant}/uj11_board_bus.v'; inputs.append(path)
        original = (ROOT/path).read_text()
        for defect in ('none', 'ram_alias', 'private_alias') if variant == 'prefix' else ('none',):
            source = original
            if defect == 'ram_alias': source = source.replace('address[21:17]==0', 'address[20:17]==0')
            if defect == 'private_alias': source = source.replace('virtual_address[7:6]==0', 'virtual_address[7]==0')
            gate = f'{variant}-{defect}.v'; (out/gate).write_text(cut(source, 'gate'))
            (out/'gate.v').write_text((out/gate).read_text())
            tag = f'cp45-proof-{variant}-{defect}'
            with (ROOT/f'build/{tag}.log').open('w') as log:
                result = subprocess.run([args.yosys, '-s', 'proof.ys'], cwd=out,
                    stdout=log, stderr=subprocess.STDOUT,
                    env=dict(os.environ, YOWASP_CACHE_DIR=str(ROOT/'build/yowasp-cache')))
            text = (ROOT/f'build/{tag}.log').read_text()
            if defect == 'none': assert result.returncode == 0 and 'Equivalence successfully proven!' in text, text[-2500:]
            else: assert result.returncode != 0 and 'unproven' in text, text[-2500:]
            tests.append(dict(variant=variant, defect=defect, returncode=result.returncode, tag=tag))
            print('PASS CP45 equivalence:', variant, defect, flush=True)
    four_state_tests = [four_state(out, variant) for variant in VARIANTS[1:]]
    inputs += [str(p.relative_to(ROOT)) for p in sorted(out.iterdir()) if p.suffix in ('.v', '.ys')]
    (ROOT/'build/cp45-proof.json').write_text(json.dumps(dict(tests=tests, four_state=four_state_tests,
        method='Actual combinational cones, all PA22/VA16/read-data/state/enable inputs unconstrained; 50 output bits',
        inputs_sha256={p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs}), indent=2)+'\n')


if __name__ == '__main__': main()
