#!/usr/bin/env python3
"""Prove the actual board combinational cones with all inputs unconstrained.

Device read data, state flags and all three enable parameters are independent
inputs. No software reachability assumptions or address masks hide aliases.
The builder separately checks unchanged state-update/peripheral blocks.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
from board_common import ROOT

SELECTORS = '''local_boot_selected boot_program_selected boot_selected io_page
service_dma_selected service_program_selected program_selected cpu_io_page
uart_selected ltc_selected panel_selected maint_selected sd_selected rk_selected
rk_cs1_selected rk_cs2_selected rk_ds_selected rk_fixed_selected rk_store_selected
guest_fram_selected fram_selected firmware_selected'''.split()


def cut(source, name):
    constants = source[source.index('\tlocalparam'):source.index('\n\twire [15:0] word_address')]
    declarations = source[source.index('\twire [15:0] word_address'):source.index('\twire uart_strobe')]
    declarations = re.sub(r'^\treg .*?;\n', '', declarations, flags=re.M)
    a = source.index('\talways @(*) begin\n\t\tcase (word_address')
    rom = source[a:source.index('\n\tgenerate if (UART_XO2)', a)]
    a = source.index('\twire [15:0] small_rdata') if '\twire [15:0] small_rdata' in source else source.index('\tassign rdata')
    mux = source[a:source.index('\n\tassign acknowledge', a)]
    text = f'''module {name}(
    input [15:0] address, uart_rdata, ltc_rdata, panel_rdata, sd_rdata, boot_program_word, fram_rdata,
    input write, instruction_fetch, BOOT_ROM_ENABLE, SD_BOOT_ENABLE, RK_SERVICE_ENABLE,
    input boot_overlay_active, boot_release_armed, rk_service_active, rk_service_movb,
    input rk_write_command, rk_cs1_initialized, rk_immediate_done,
    output [15:0] rdata, output [{len(SELECTORS)-1}:0] selections);
    reg [15:0] local_rdata;
'''+constants+declarations+rom+mux+'\nassign selections={'+','.join(SELECTORS)+'};\nendmodule\n'
    # Compare observable outputs, not unqualified ROM values at addresses
    # where local_boot_selected is false. Keep both complete cones intact.
    private = set(re.findall(r'\b(?:wire|reg)\s+(?:\[[^]]+\]\s*)?(\w+)', text))
    return re.sub(r'\b\w+\b', lambda m: name+'_'+m[0] if m[0] in private else m[0], text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--yosys', default='yosys')
    args = parser.parse_args()
    folder = ROOT/'build/cp38-proof'; folder.mkdir(parents=True, exist_ok=True)
    baseline = ROOT/'build/cp38-bus/baseline/uj11_board_bus.v'
    (folder/'gold.v').write_text(cut(baseline.read_text(), 'gold'))
    script = '''read_verilog -sv gold.v gate.v
proc
opt_clean
equiv_make gold gate equiv
hierarchy -top equiv
equiv_simple
equiv_status -assert
'''
    (folder/'proof.ys').write_text(script)
    results = []
    inputs = [str(baseline.relative_to(ROOT)), 'tools/check_board_decode.py', 'build/cp38-bus/inputs.json']
    for variant in ('prefix', 'split', 'split-base', 'priority'):
        path = ROOT/f'build/cp38-bus/{variant}/uj11_board_bus.v'
        inputs.append(str(path.relative_to(ROOT)))
        source = path.read_text()
        for defect in ('none', 'uart_alias', 'rom_alias') if variant == 'prefix' else ('none',):
            candidate = source
            if defect == 'uart_alias':
                old, new = 'word_address[12:3] == KL11_BASE[12:3]', 'word_address[12:4] == KL11_BASE[12:4]'
                assert candidate.count(old) == 1; candidate = candidate.replace(old, new)
            if defect == 'rom_alias':
                old, new = "6'o40: local_rdata = 16'o012706;", "6'o40: local_rdata = 16'o012707;"
                assert candidate.count(old) == 1; candidate = candidate.replace(old, new)
            (folder/'gate.v').write_text(cut(candidate, 'gate'))
            tag = f'cp38-proof-{variant}-{defect}'
            with (ROOT/'build'/(tag+'.log')).open('w') as log:
                result = subprocess.run([args.yosys, '-s', 'proof.ys'], cwd=folder, stdout=log,
                    stderr=subprocess.STDOUT, env=dict(os.environ,YOWASP_CACHE_DIR=str(ROOT/'build/yowasp-cache')))
            log = (ROOT/'build'/(tag+'.log')).read_text()
            if defect == 'none':
                assert result.returncode == 0 and 'Equivalence successfully proven!' in log, log[-2000:]
            else:
                assert result.returncode != 0 and 'unproven' in log, log[-2000:]
            results.append(dict(variant=variant,defect=defect,returncode=result.returncode,tag=tag))
            print('PASS CP38 proof:',variant,defect,flush=True)
    (ROOT/'build/cp38-proof.json').write_text(json.dumps(dict(method='Extracted real combinational cones; all address/data/state/enable inputs unconstrained; Yosys equiv_simple',
        tests=results,inputs_sha256={p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs}),indent=2)+'\n')


if __name__ == '__main__':
    main()
