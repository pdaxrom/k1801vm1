#!/usr/bin/env python3
"""Prove CP59 from actual engine/memory/sequencer RTL, for arbitrary uwords."""
import hashlib
import json
import os
import re
import subprocess
from board_common import ROOT
from build_timing_cp59 import baseline,adapt,OUT


def main():
    adapt();old=baseline();dest=OUT/'proof';dest.mkdir(exist_ok=True)
    engine=old['build/cp58-service/src/rtl/uj11_engine.v'].decode()
    seq=old['build/cp58-service/src/rtl/uj11_microseq.v'].decode()
    mem=old['build/cp58-service/src/rtl/uj11_mem.v'].decode()
    candidate=(OUT/'src/rtl/uj11_engine.v').read_text()
    assert (OUT/'src/rtl/uj11_microseq.v').read_text()==seq
    assert (OUT/'src/rtl/uj11_mem.v').read_text()==mem
    # Exact active/command/fault cone from the measured engine, with data/RF
    # values as unrestricted inputs. ACK and ERROR are not assumed low idle.
    declarations='\n'.join(re.search(r'    wire (?:\[[^\]]+\] )?'+name+r'\s*=.*?;',engine,re.S)[0]
                           for name in ['control','command','fetching','reading','writing','memory_op'])
    memory=re.search(r'    uj11_mem memory\(.*?\);',engine,re.S)[0]
    assert memory==re.search(r'    uj11_mem memory\(.*?\);',candidate,re.S)[0]
    assert all(line in candidate for line in declarations.splitlines())
    miter='''module miter(input clk,reset,enable,stopped,
input [35:0] uword,input [15:0] ir,read_a,read_b,input [3:0] nzvc,selected_a,
input [9:0] dispatch_address,fault_target,
input effective_ack,mem_error,decode_wait,byte_instruction,address_odd,
q0,loop_zero,a_one,irq_pending,trace_pending,fault_redirect,fault_repair,step_return,
output ok);
wire [1:0] bus_fault; wire raw_request,raw_read,raw_write,mem_byte,complete;
wire [15:0] mem_addr,mem_write_data;
'''+declarations+'\n'+memory+'\n'
    ports=['clk','reset','enable','uword','ir','nzvc','dispatch_address','fault_target','step_return',
           'address_odd','byte_instruction','selected_a','q0','loop_zero','a_one','irq_pending',
           'trace_pending','fault_redirect','fault_repair']
    for name in ('gold','gate'):
        modified=seq.replace('module uj11_microseq (',f'module {name} (').replace('output reg [9:0] upc,',
                    'output wire [10:0] saved_link,\n    output reg [9:0] upc,').replace('    reg link_valid;',
                    '    reg link_valid;\n    assign saved_link={link_valid,link};')
        (dest/f'{name}.v').write_text(modified)
        miter+=f'wire [9:0] {name}_pc,{name}_next;wire[10:0] {name}_link;\n'
        error=re.search(r'\.bus_error\(([^)]+)\)',engine if name=='gold' else candidate)[1]
        miter+=name+' '+name+'_inst('+','.join('.'+p+'('+p+')' for p in ports)+f',.bus_error({error}),.upc({name}_pc),.next_address({name}_next),.saved_link({name}_link));\n'
    miter+='assign ok={gold_pc,gold_next,gold_link}=={gate_pc,gate_next,gate_link};\nendmodule\n'
    (dest/'memory.v').write_text(mem)
    ys='''read_verilog -sv gold.v gate.v memory.v miter.v
prep -top miter -flatten
async2sync
dffunmap
opt
sat -verify -prove ok 1 -set-init-zero -set-at 1 reset 1 -seq 2 -tempinduct -maxsteps 6 -show-inputs
'''
    (dest/'proof.ys').write_text(ys);runs=[]
    for mutation in ('none','predicate-one','drop-fault-redirect'):
        trial=miter
        if mutation=='predicate-one':trial=trial.replace(".bus_error(1'b0)",".bus_error(1'b1)")
        if mutation=='drop-fault-redirect':
            head,tail=trial.split('gate gate_inst(',1);trial=head+'gate gate_inst('+tail.replace('.fault_redirect(fault_redirect)',".fault_redirect(1'b0)")
        (dest/'miter.v').write_text(trial)
        proc=subprocess.run([str(ROOT/'build/formal/bin/yowasp-yosys'),'-s','proof.ys'],cwd=dest,env=dict(os.environ,YOWASP_CACHE_DIR=str(ROOT/'build/yowasp-cache')),text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        (dest/f'{mutation}.log').write_text(proc.stdout)
        if mutation=='none':assert proc.returncode==0 and 'SUCCESS!' in proc.stdout,proc.stdout[-2000:]
        else:assert proc.returncode!=0 and 'proof did fail' in proc.stdout,proc.stdout[-2000:]
        runs.append(dict(mutation=mutation,returncode=proc.returncode));print('PASS CP59 proof:',mutation,flush=True)
    (dest/'miter.v').write_text(miter)
    paths=[str(p.relative_to(ROOT)) for p in sorted(dest.iterdir()) if p.is_file()]
    paths+=['tools/check_timing_cp59.py','build/cp59-service/inputs.json']
    record=dict(method='unbounded temporal induction of exact microsequencer state/next address; arbitrary uword and all bus inputs',
        runs=runs,files={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths})
    (OUT/'proof-results.json').write_text(json.dumps(record,indent=2)+'\n')


if __name__=='__main__':main()
