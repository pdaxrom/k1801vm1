#!/usr/bin/env python3
"""CP47 sequential equivalence: SPI/handshake every cycle, data when valid."""
import hashlib
import json
import os
import subprocess
from board_common import ROOT
from build_fram_cp47 import VARIANTS


def probe(text, name, divider):
    return text.replace('module uj11_board_fram', 'module '+name).replace('CLK_DIV=1', 'CLK_DIV='+str(divider))


def sat_miter(gold, gate):
    modules=[]
    for text in (gold,gate):
        text=text.replace('input wire spi_miso\n', 'input wire spi_miso,\n'
            '    output wire [31:0] p_control, output wire [3:0] p_state,p_samples,\n'
            '    output wire p_active, output wire [7:0] p_rx\n')
        text=text.replace('    assign busy=state!=IDLE;', """    assign p_control={state,active,seen,tx,bit_count,divider};
    assign p_state=state;
    assign p_active=active;
    assign p_samples={1'b0,bit_count}+{3'b0,spi_sck};
    assign p_rx=rx;
    assign busy=state!=IDLE;""")
        modules.append(text)
    miter="""module miter(input clk,rst,req,write,byte_access,bank,spi_miso,
input [15:0] address,wdata,output ok);
"""
    for n in ('gold','gate'):
        miter+=f"""wire [15:0] {n}_data;
wire [5:0] {n}_pins;
wire [31:0] {n}_control;
wire [3:0] {n}_state,{n}_samples;
wire {n}_active;
wire [7:0] {n}_rx;
{n} dut_{n}(.clk(clk),.rst(rst),.req(req),.write(write),.byte_access(byte_access),.bank(bank),
.address(address),.wdata(wdata),.spi_miso(spi_miso),.rdata({n}_data),
.ready({n}_pins[0]),.error({n}_pins[1]),.busy({n}_pins[2]),
.spi_cs_n({n}_pins[3]),.spi_sck({n}_pins[4]),.spi_mosi({n}_pins[5]),
.p_control({n}_control),.p_state({n}_state),.p_samples({n}_samples),
.p_active({n}_active),.p_rx({n}_rx));
"""
    miter+="""wire [7:0] sample_mask=8'hff >> (4'd8-gold_samples);
wire serial_state=gold_state==1 || (gold_state>=3 && gold_state<=8);
// Inductive invariants: the already received low bits agree; complete data
// agree in DONE/IDLE. Control equality includes state/count/divider/TX/seen.
assign ok=gold_control==gate_control && gold_pins==gate_pins &&
 gold_state<=9 && (!gold_active || serial_state) &&
 gold_data[7:0]==gate_data[7:0] &&
 ((gold_state!=0 && gold_state!=9 && !gold_pins[0]) || gold_data[15:8]==gate_data[15:8]) &&
 (!gold_active || (gold_rx & sample_mask)==(gate_rx & sample_mask));
endmodule
"""
    return modules[0],modules[1],miter


def main():
    out=ROOT/'build/cp47-proof';out.mkdir(parents=True,exist_ok=True)
    inputs=['tools/check_fram_cp47.py','tools/build_fram_cp47.py','build/cp47-fram/inputs.json']
    tests=[]
    for variant in VARIANTS[1:]:
        for divider in (1,3):
            qualified=variant!='byte-mux'
            texts=[]
            for name,choice in [('gold','baseline'),('gate',variant)]:
                path=f'build/cp47-fram/{choice}/uj11_board_fram.v';inputs.append(path)
                texts.append(probe((ROOT/path).read_text(),name,divider))
            # Negative controls verify bank transmission and byte zero-extension.
            defects=('none','bank-alias') if variant=='byte-mux' and divider==1 else ('none','byte-high') if variant=='shared-rx' and divider==1 else ('none',)
            for defect in defects:
                gold,gate=texts
                if defect=='bank-alias': gate=gate.replace("{7'b0,bank}","8'b0")
                if defect=='byte-high': gate=gate.replace('if(byte_access)rdata[15:8]<=0;','if(byte_access)rdata[15:8]<=8\'hff;')
                tag=f'cp47-proof-{variant}-{divider}-{defect}'
                if qualified:
                    gold,gate,miter=sat_miter(gold,gate)
                    (out/(tag+'-miter.v')).write_text(miter)
                a=out/(tag+'-gold.v');a.write_text(gold)
                b=out/(tag+'-gate.v');b.write_text(gate)
                script=f'''read_verilog -sv {a.name} {b.name}
proc
opt_clean
equiv_make gold gate equiv
hierarchy -top equiv
equiv_simple
equiv_induct -seq 4
equiv_status -assert
'''
                if qualified:
                    script=f'''read_verilog -sv {a.name} {b.name} {tag}-miter.v
prep -top miter -flatten
opt
sat -verify -prove ok 1 -set-init-zero -set-at 1 rst 1 -seq 2 -tempinduct -maxsteps 12
'''
                ys=out/(tag+'.ys');ys.write_text(script)
                with (ROOT/f'build/{tag}.log').open('w') as log:
                    run=subprocess.run([str(ROOT/'build/formal/bin/yowasp-yosys'),'-s',ys.name],cwd=out,
                        stdout=log,stderr=subprocess.STDOUT,env=dict(os.environ,YOWASP_CACHE_DIR=str(ROOT/'build/yowasp-cache')))
                log=(ROOT/f'build/{tag}.log').read_text()
                if defect=='none':assert run.returncode==0 and ('Induction step proven: SUCCESS!' if qualified else 'Equivalence successfully proven!') in log,log[-3000:]
                else:assert run.returncode!=0 and ('proof did fail' if qualified else 'unproven') in log,log[-3000:]
                tests.append(dict(variant=variant,divider=divider,qualified_data=qualified,defect=defect,tag=tag,returncode=run.returncode))
                print('PASS CP47 proof:',variant,divider,defect,flush=True)
    inputs += [str(p.relative_to(ROOT)) for p in sorted(out.iterdir()) if p.suffix in ('.v','.ys')]
    (ROOT/'build/cp47-proof.json').write_text(json.dumps(dict(tests=tests,
        inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sorted(set(inputs))}),indent=2)+'\n')


if __name__=='__main__':main()
