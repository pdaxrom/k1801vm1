#!/usr/bin/env python3
"""Inventory J11 opcode coverage against core/core.c; not a semantic proof.

Execute the actual legacy decoder and the extracted MMU dispatch block for
every 16-bit opcode. The catalogue describes instruction families implemented
by the DCJ11 paths in core/core.c (and its included pdp11_fp.c).
No hardware build outputs are regenerated.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[1]

def catalogue():
    rows=[]
    def add(name,base,mask,kind='integer',memory_only=False):
        rows.append(dict(name=name,base=base,mask=mask,kind=kind,memory_only=memory_only))
    for op,name in enumerate(('HALT','WAIT','RTI','BPT','IOT','RESET','RTT','MFPT')):add(name,op,0xffff)
    add('RTS',0o200,0o177770);add('SPL',0o230,0o177770);add('CCC/SCC',0o240,0o177740)
    for op,name in ((0o100,'JMP'),(0o300,'SWAB')):add(name,op,0o177700,memory_only=name=='JMP')
    for op,name in zip((0o400,0o1000,0o1400,0o2000,0o2400,0o3000,0o3400,
                        0o100000,0o100400,0o101000,0o101400,0o102000,0o102400,0o103000,0o103400),
                       ('BR','BNE','BEQ','BGE','BLT','BGT','BLE','BPL','BMI','BHI','BLOS','BVC','BVS','BCC/BHIS','BCS/BLO')):
        add(name,op,0o177400)
    add('EMT',0o104000,0o177400);add('TRAP',0o104400,0o177400)
    add('JSR',0o4000,0o177000,memory_only=True)
    for i,name in enumerate(('CLR','COM','INC','DEC','NEG','ADC','SBC','TST','ROR','ROL','ASR','ASL')):
        add(name,0o5000+i*0o100,0o177700);add(name+'B',0o105000+i*0o100,0o177700)
    for op,name in ((0o6400,'MARK'),(0o6500,'MFPI'),(0o6600,'MTPI'),(0o6700,'SXT'),
                    (0o106400,'MTPS'),(0o106500,'MFPD'),(0o106600,'MTPD'),(0o106700,'MFPS'),
                    (0o7000,'CSM'),(0o7200,'TSTSET'),(0o7300,'WRTLCK')):
        add(name,op,0o177700,memory_only=name in ('TSTSET','WRTLCK'))
    for i,name in enumerate(('MUL','DIV','ASH','ASHC','XOR')):add(name,0o70000+i*0o1000,0o177000)
    for i,name in enumerate(('FADD','FSUB','FMUL','FDIV')):add(name,0o75000+i*8,0o177770,'fis')
    add('SOB',0o77000,0o177000)
    for op,name in zip((1,2,3,4,5,6,9,10,11,12,13,14),
                       ('MOV','CMP','BIT','BIC','BIS','ADD','MOVB','CMPB','BITB','BICB','BISB','SUB')):
        add(name,op<<12,0o170000)
    for op,name in ((0o170000,'CFCC'),(0o170001,'SETF'),(0o170002,'SETI'),(0o170011,'SETD'),(0o170012,'SETL')):
        add(name,op,0xffff,'fpp')
    for i,name in enumerate(('LDFPS','STFPS','STST','CLRF/D','TSTF/D','ABSF/D','NEGF/D'),1):
        add(name,0o170000+i*0o100,0o177700,'fpp')
    for i,name in enumerate(('MULF/D','MODF/D','ADDF/D','LDF/D','SUBF/D','CMPF/D','STF/D','DIVF/D',
                             'STEXP','STCFI/L (STCDI/L)','STCFD/DC','LDEXP','LDCIF/ID/LF/LD','LDCDF/FD'),2):
        add(name,0o170000+i*0o400,0o177400,'fpp')
    return rows

def run(out):
    out.mkdir(parents=True,exist_ok=False)
    cpu=(ROOT/'rtl/mmu/uj11_mmu_cpu.v').read_text()
    dispatch=re.search(r'    always @\* begin\n        dispatch=\{2\x27b0,decoded_legacy\};[\s\S]*?\n    end',cpu).group()
    bench='''module dump;
reg [15:0] decode_ir,psw;reg [15:0] mmr3;reg fpp_enabled;
wire [9:0] decoded_legacy;reg [11:0] dispatch;integer op;
reg [11:0] enabled,disabled,kernel,fp;
uj11_decode legacy(decode_ir,decoded_legacy);
'''+dispatch+'''
initial begin
for(op=0;op<65536;op=op+1)begin
 decode_ir=op;psw=16'o140000;mmr3=8;fpp_enabled=0;#1;enabled=dispatch;
 mmr3=0;#1;disabled=dispatch;
 psw=0;mmr3=8;#1;kernel=dispatch;
 psw=16'o140000;fpp_enabled=1;#1;fp=dispatch;
 $display("%04x %03x %03x %03x %03x %03x",decode_ir,decoded_legacy,enabled,disabled,kernel,fp);
end
$finish;end
endmodule
'''
    (out/'dump.v').write_text(bench)
    subprocess.run(['iverilog','-g2012','-s','dump','-o',str(out/'dump'),str(out/'dump.v'),str(ROOT/'rtl/uj11_decode.v')],check=True)
    text=subprocess.check_output(['vvp',str(out/'dump')],text=True)
    (out/'dispatch.txt').write_text(text)
    truth={int(s[0],16):[int(n,16) for n in s[1:]] for line in text.splitlines()
           if re.fullmatch(r'[0-9a-f]{4}(?: [0-9a-f]{3}){5}',line) for s in [line.split()]}
    assert len(truth)==65536
    contexts=('mmuless','mmu_user_csm_on_fpp_off','mmu_user_csm_off','mmu_kernel','mmu_user_fpp_on')
    results=[]
    for row in catalogue():
        ops=[op for op in truth if (op&row['mask'])==row['base'] and (not row['memory_only'] or op&0o70)]
        assert ops,row
        counts={name:dict(encodings=len(ops),illegal=sum(truth[op][i]==0x42 for op in ops),
                          entries=sorted({f'{truth[op][i]:03x}' for op in ops})) for i,name in enumerate(contexts)}
        results.append(dict(name=row['name'],kind=row['kind'],base=f"{row['base']:06o}",mask=f"{row['mask']:06o}",contexts=counts))
    sources=['../core/core.c','../core/pdp11_fp.c','rtl/uj11_decode.v','rtl/mmu/uj11_mmu_cpu.v',
             'microcode/uj11.uasm','microcode/uj11-mmu.uasm','microcode/mmu/fpp.uasm','microcode/mmu/fpp-exec.uasm',
             'firmware/fpp/FP11.MAC','tools/audit_j11_isa.py']
    record=dict(scope='Opcode dispatch inventory, not instruction execution or full J11 conformance',
                opcodes_checked=65536,families=results,
                files={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources})
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n')
    for c in contexts:
        missing=[r['name'] for r in results if r['kind']=='integer' and r['contexts'][c]['illegal']==r['contexts'][c]['encodings']]
        print(c+': integer families routed to illegal: '+', '.join(missing))
    print('mmuless intentionally omits MFPI/MTPI/MFPD/MTPD/CSM; CSM is conditional in MMU.')
    print(f'{len(results)} catalogue families, 65536 opcodes, 4 MMU contexts; full record: {out}/result.json')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True)
    run(p.parse_args().out.resolve())
