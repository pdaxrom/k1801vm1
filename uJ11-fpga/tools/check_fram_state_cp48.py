#!/usr/bin/env python3
"""CP48 reset-reachable sequential equivalence, including busy rdata and pins."""
import hashlib
import json
import os
import subprocess
from board_common import ROOT
from build_fram_state_cp48 import STATES
from build_mmu_entry import change


def instrument(text, name, divider):
    text=change(text,'module uj11_board_fram','module '+name)
    text=change(text,'CLK_DIV=1','CLK_DIV='+str(divider))
    text=change(text,'input wire spi_miso\n','input wire spi_miso,\n    output wire [31:0] controls, output reg [3:0] canonical_state\n')
    # Debug ports are formal only. Canonical value 15 rejects all illegal codes.
    mapping='\n'.join(f'            {n}:canonical_state=4\'d{i};' for i,n in enumerate(STATES))
    text=change(text,'    assign busy=state!=IDLE;',f'''    assign controls={{active,seen,tx,bit_count,divider}};
    always @* begin
        case(state)
{mapping}
            default:canonical_state=4'd15;
        endcase
    end
    assign busy=state!=IDLE;''')
    return text


def main():
    out=ROOT/'build/cp48-proof';out.mkdir(parents=True,exist_ok=True)
    inputs=['tools/check_fram_state_cp48.py','tools/build_fram_state_cp48.py',
            'tools/build_mmu_entry.py','build/cp48-state/inputs.json']
    tests=[]
    for variant in ('successors','onehot'):
        for divider in (1,3):
            defects=('none','skip-high') if divider==1 else ('none',)
            for defect in defects:
                tag=f'cp48-proof-{variant}-{divider}-{defect}'
                for name,choice in [('gold','baseline'),('gate',variant)]:
                    source=f'build/cp48-state/{choice}/uj11_board_fram.v';inputs.append(source)
                    text=(ROOT/source).read_text()
                    if name=='gate' and defect=='skip-high':
                        if variant=='successors':text=change(text,'LOW:state<=DATA_LO;','LOW:state<=DATA_HI;')
                        else:text=change(text,"{state[8:0],1'b0}","{state[7:0],2'b0}")
                    (out/(tag+'-'+name+'.v')).write_text(instrument(text,name,divider))
                miter='module miter(input clk,rst,req,write,byte_access,bank,spi_miso,input [15:0] address,wdata,output ok);\n'
                for n in ('gold','gate'):
                    miter+=f'''wire [21:0] {n}_outputs;
wire [31:0] {n}_controls;
wire [3:0] {n}_state;
{n} dut_{n}(.clk(clk),.rst(rst),.req(req),.write(write),.byte_access(byte_access),.bank(bank),
.address(address),.wdata(wdata),.spi_miso(spi_miso),.rdata({n}_outputs[15:0]),
.ready({n}_outputs[16]),.error({n}_outputs[17]),.busy({n}_outputs[18]),
.spi_cs_n({n}_outputs[19]),.spi_sck({n}_outputs[20]),.spi_mosi({n}_outputs[21]),
.controls({n}_controls),.canonical_state({n}_state));
'''
                miter+=f"wire active=gold_controls[{11+max(1,(divider-1).bit_length())}];\n"
                miter+='wire serial_state=gold_state==1 || (gold_state>=3 && gold_state<=8);\nassign ok=gold_outputs==gate_outputs && gold_controls==gate_controls && gold_state==gate_state && gold_state<=9 && (!active || serial_state);\nendmodule\n'
                (out/(tag+'-miter.v')).write_text(miter)
                script=f'''read_verilog -sv {tag}-gold.v {tag}-gate.v {tag}-miter.v
prep -top miter -flatten
memory_map
opt
sat -verify -prove ok 1 -set-init-zero -set-at 1 rst 1 -seq 2 -tempinduct -maxsteps 12
'''
                (out/(tag+'.ys')).write_text(script)
                with (ROOT/f'build/{tag}.log').open('w') as log:
                    run=subprocess.run([str(ROOT/'build/formal/bin/yowasp-yosys'),'-s',tag+'.ys'],cwd=out,
                        stdout=log,stderr=subprocess.STDOUT,env=dict(os.environ,YOWASP_CACHE_DIR=str(ROOT/'build/yowasp-cache')))
                text=(ROOT/f'build/{tag}.log').read_text()
                if defect=='none':assert run.returncode==0 and 'Induction step proven: SUCCESS!' in text,text[-3000:]
                else:assert run.returncode!=0 and 'proof did fail' in text,text[-3000:]
                tests.append(dict(tag=tag,variant=variant,divider=divider,defect=defect,returncode=run.returncode))
                print('PASS CP48 proof:',variant,divider,defect,flush=True)
    inputs += [str(p.relative_to(ROOT)) for p in sorted(out.iterdir()) if p.suffix in ('.v','.ys')]
    (ROOT/'build/cp48-proof.json').write_text(json.dumps(dict(tests=tests,
        inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sorted(set(inputs))}),indent=2)+'\n')


if __name__=='__main__':main()
