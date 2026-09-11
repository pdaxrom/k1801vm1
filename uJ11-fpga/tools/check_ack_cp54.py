#!/usr/bin/env python3
"""Prove actual native decode, data and ACK cones for every binary input/state."""
import hashlib
import json
import os
import re
import subprocess
from board_common import ROOT
from build_ack_cp54 import OUT, VARIANTS, build
from build_fram_cp52 import replace_once
from check_board_decode import SELECTORS

FIELDS={n:16 for n in ('address','uart_rdata','ltc_rdata','panel_rdata','sd_rdata','boot_program_word','fram_rdata')}
FIELDS.update({n:1 for n in '''write instruction_fetch BOOT_ROM_ENABLE SD_BOOT_ENABLE RK_SERVICE_ENABLE
boot_overlay_active boot_release_armed rk_service_active rk_service_movb rk_write_command
rk_cs1_initialized rk_immediate_done request uart_ack sd_ready sd_error boot_ack boot_program_ack fram_ready fram_error'''.split()})


def cut(source,name):
    constants=source[source.index('\tlocalparam'):source.index('\n\twire [15:0] word_address')]
    declarations=source[source.index('\twire [15:0] word_address'):source.index('\twire uart_strobe')]
    declarations=re.sub(r'^\treg .*?;\n','',declarations,flags=re.M)
    a=re.search(r'\talways @\(\*\) begin\s+case \(word_address\)',source).start()
    rom=source[a:source.index('\n\tgenerate if (UART_XO2)',a)]
    a=source.index('\twire [15:0] small_rdata')
    mux=source[a:source.index('\n\tassign virq',a)]
    text='module '+name+'(\n'+',\n'.join(f'input [{w-1}:0] {n}' for n,w in FIELDS.items())+f''',
output [15:0] rdata, output [{len(SELECTORS)-1}:0] selections, output acknowledge);
reg [15:0] local_rdata;
'''+constants+declarations+rom+mux+'\nassign selections={'+','.join(SELECTORS)+'};\nendmodule\n'
    private=set(re.findall(r'\b(?:wire|reg)\s+(?:\[[^]]+\]\s*)?(\w+)',text))
    return re.sub(r'\b\w+\b',lambda m:name+'_'+m[0] if m[0] in private else m[0],text)


def four_state(folder):
    width=17+len(SELECTORS)
    source='`timescale 1ns/1ps\nmodule tb;\n'
    source+='\n'.join(f'reg [{w-1}:0] {n};' for n,w in FIELDS.items())
    for name in ('gold','gate'):
        source+=f'\nwire [{width-1}:0] {name}_value;\n{name} {name}_instance('
        source+=','.join(f'.{n}({n})' for n in FIELDS)
        source+=f',.rdata({name}_value[15:0]),.selections({name}_value[{width-2}:16]),.acknowledge({name}_value[{width-1}]));\n'
    source+='''integer addr,pattern,checks=0;
reg [31:0] seed=32'h27182819;
initial begin
for(pattern=0;pattern<4;pattern=pattern+1)begin
'''
    for n,w in FIELDS.items():
        if w==16 and n!='address':source+=f"{n}=pattern==0 ? 16'hxxxx : pattern==1 ? 16'hzzzz : pattern==2 ? 16'h5a5a : 16'hax5z;\n"
    source+='for(addr=0;addr<65536;addr=addr+1)begin\naddress=addr[15:0];\n'
    controls=[n for n,w in FIELDS.items() if w==1]
    source+="seed={seed[30:0],seed[31]^seed[21]^seed[1]^seed[0]};\n"
    source+='{'+','.join(controls)+f'}}=seed[{len(controls)-1}:0];\n'
    source+='''#1;
if(gold_value!==gate_value)$fatal(1,"decode/data/ACK mismatch at address %o",address);
checks=checks+1;
end end
$display("PASS CP54 four-state device data: %0d cases",checks);
$finish;end
endmodule
'''
    (folder/'four_state.v').write_text(source)
    with (folder/'four-state-build.log').open('w') as log:
        subprocess.run(['iverilog','-g2012','-s','tb','-o','four-state','gold.v','gate.v','four_state.v'],cwd=folder,stdout=log,stderr=subprocess.STDOUT,check=True)
    assert not (folder/'four-state-build.log').read_text()
    with (folder/'four-state.log').open('w') as log:
        subprocess.run(['vvp','four-state'],cwd=folder,stdout=log,stderr=subprocess.STDOUT,check=True)
    assert 'PASS CP54 four-state device data: 262144 cases' in (folder/'four-state.log').read_text()


def main():
    build()
    out=ROOT/'build/cp54-proof';out.mkdir(parents=True,exist_ok=True)
    runs=[]
    for variant in VARIANTS:
        for defect in ('none','dma-alias','ack-alias') if variant=='dma-ack' else ('none','dma-alias'):
            folder=out/(variant+'-'+defect);folder.mkdir(exist_ok=True)
            for name,choice in [('gold','baseline'),('gate',variant)]:
                source=(OUT/choice/'uj11_board_bus.v').read_text()
                if name=='gate' and defect=='dma-alias':source=replace_once(source,'io_page && !service_dma_operand','io_page')
                if name=='gate' and defect=='ack-alias':source=replace_once(source,'request && cpu_io_page && immediate_io_response','request && immediate_io_response')
                (folder/(name+'.v')).write_text(cut(source,name))
            script='''read_verilog -sv gold.v gate.v
proc
opt_clean
equiv_make gold gate equiv
hierarchy -top equiv
equiv_simple
equiv_status -assert
'''
            (folder/'proof.ys').write_text(script)
            with (folder/'proof.log').open('w') as log:
                run=subprocess.run([str(ROOT/'build/formal/bin/yowasp-yosys'),'-s','proof.ys'],cwd=folder,
                    stdout=log,stderr=subprocess.STDOUT,env=dict(os.environ,YOWASP_CACHE_DIR=str(ROOT/'build/yowasp-cache')))
            text=(folder/'proof.log').read_text()
            if defect=='none':
                assert run.returncode==0 and 'Equivalence successfully proven!' in text,text[-1500:]
                four_state(folder)
            else:assert run.returncode!=0 and 'unproven' in text,text[-1500:]
            runs.append(dict(variant=variant,defect=defect,returncode=run.returncode))
            print('PASS CP54 proof',variant,defect,flush=True)
    inputs=['tools/check_ack_cp54.py','tools/check_board_decode.py','build/cp54-ack/inputs.json']
    inputs+=[str(p.relative_to(ROOT)) for p in OUT.rglob('*.v')]
    inputs+=[str(p.relative_to(ROOT)) for p in out.rglob('*') if p.suffix in ('.v','.ys')]
    record=dict(method='Actual combinational decode/data/ACK cones, all binary address/state/data/device-ready/enable inputs unconstrained',
        output_bits=17+len(SELECTORS),runs=runs,four_state_cases=524288,
        four_state_scope='Known address/control, X/Z selected and unselected device words; not unknown control equivalence',
        inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sorted(set(inputs))},
        logs_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in out.rglob('*.log')})
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n')


if __name__=='__main__':main()
