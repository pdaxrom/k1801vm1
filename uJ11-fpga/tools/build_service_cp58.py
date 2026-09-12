#!/usr/bin/env python3
"""CP58: bounded HALT fault recovery and VM2 STEP over frozen CP57e."""
import hashlib
import json
import subprocess
import sys
import tarfile
from board_common import ROOT, CORE, BOARD
from build_fram_cp52 import replace_once as rep
from make_ebr import generate
sys.path.insert(0, str(ROOT/'microasm'))
from uj11asm import assemble

OUT=ROOT/'build/cp58-service'


def build():
    OUT.mkdir(parents=True,exist_ok=True)
    manifest=json.loads((ROOT/'synth/reports/cp57e/inputs.json').read_text())
    baseline={}
    with tarfile.open(ROOT/'synth/reports/cp57e/source.tgz') as ar:
        for path,sha in manifest['files'].items():
            if path.startswith('generated:'):continue
            data=ar.extractfile(path).read()
            assert hashlib.sha256(data).hexdigest()==sha,path
            baseline[path]=data.decode() if not path.endswith('.tgz') else data
    prefix='build/cp57-service/'
    base=baseline[prefix+'service.uasm']
    old,old_listing,labels,_=assemble(base)
    used={int(l.split()[0],16) for l in old_listing.splitlines()}
    # Keep all CP57 addresses. Select two adjacent holes; prefer a small
    # constant mux against the existing guest fault entry 015 (hex).
    address=min((a for a in range(1023) if a not in used and a+1 not in used),
                key=lambda a:((a^0x15).bit_count(),a))
    base=rep(base,'READ, space=GUEST, a=R5, target=S_MFUS_INC, prefetch=0',
             'READ, space=GUEST, a=R5, target=S_MFUS_INC, fault_inc=1, prefetch=0')
    extension=(ROOT/'microcode/service_cp58.uasm').read_text()
    source=base+f'\n.org ${address:03x}\n'+extension
    image,listing,new_labels,stats=assemble(source)
    assert all(image[a]==old[a] for a in used if a!=labels['S_MFUS'])
    assert image[labels['S_MFUS']]==old[labels['S_MFUS']]|4
    assert all(new_labels[k]==v for k,v in labels.items())
    outputs=[]
    def put(name,data):
        path=OUT/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(data)
        outputs.append(str(path.relative_to(ROOT)))
    put('service.uasm',source)
    put('m0.mem',''.join(f'{w:09x}\n' for w in image))
    put('m0.lst',listing+'\n')
    put('m0.labels.json',json.dumps(new_labels,indent=2)+'\n')
    put('m0.stats.json',json.dumps(dict(baseline_words=1000,used_words=stats['used_words'],
                                     free_words=1024-stats['used_words']),indent=2)+'\n')
    put('uj11_m0_ebr.v',generate(image))
    files={p:baseline[prefix+'src/'+p].replace('build/cp57-service/','build/cp58-service/') for p in CORE+BOARD}
    e=files['rtl/uj11_engine.v']
    e=rep(e,'.fault_redirect(fault_redirect),.fault_repair(fault_repair)',
          f".fault_target(service_mode ? 10'h{address:03x} : 10'h015),.step_return(service_leave && ir[2]),.fault_redirect(fault_redirect),.fault_repair(fault_repair)")
    e=rep(e,'if (bus_fault!=0 && (frame_active || service_mode)) fault_latched <= bus_fault;',
          'if (bus_fault!=0 && frame_active) fault_latched <= bus_fault;')
    e=rep(e,'if (step && trap_command) frame_active <= 1;\n            else if (step && alu_boundary) frame_active <= 0;',
          '''// Guard incomplete entry/context and fault-vector construction through
            // the first successful handler opcode fetch. A fault while guarded
            // remains terminal; SEL174/274 escalation is a separate checkpoint.
            if ((step && (trap_command || service_enter)) || (fault_redirect && service_mode)) frame_active <= 1;
            else if (service_mode ? ((step && fetching && ROM_DECODE==0) || fetch_capture) : (step && alu_boundary)) frame_active <= 0;''')
    e=rep(e,'((alu_boundary && irq_active==0) || (wait_command && !wait_seen))',
          '((alu_boundary && irq_active==0) || (wait_command && !wait_seen) || (service_leave && ir[2]))')
    files['rtl/uj11_engine.v']=e
    seq=files['rtl/uj11_microseq.v']
    seq=rep(seq,'input wire [9:0] dispatch_address,','input wire [9:0] dispatch_address, fault_target,\n    input wire step_return,')
    seq=rep(seq,"        if (fault_redirect) next_address = uword[2] ? target : 10'h015;\n        if (fault_repair) next_address = 10'h015;",
        '''        // STEP returns directly to FETCH, bypassing this one boundary's
        // trace and IRQ arbitration. It does not set T or suppress later faults.
        if (step_return) next_address = FETCH;
        if (fault_redirect) next_address = uword[2] ? target : fault_target;
        if (fault_repair) next_address = fault_target;''')
    files['rtl/uj11_microseq.v']=seq
    decode=files['rtl/uj11_decode.v']
    cases='\n'.join(f"            16'o{i:06o}: entry=10'h{labels['S_START']:03x};" for i in range(0o14,0o20))
    decode=rep(decode,'            default: begin end\n        endcase\n    end',cases+'\n            default: begin end\n        endcase\n    end')
    files['rtl/uj11_decode.v']=decode
    words=[int(w,16) for w in baseline[prefix+'decode.mem'].splitlines()]
    for opcode in range(0o14,0o20):
        assert words[0x300|opcode]==0x42
        words[0x300|opcode]=labels['S_START']
    put('decode.mem',''.join(f'{w:03x}\n' for w in words))
    table=files['microcode/generated/uj11_decode_table.v']
    # Replace only vendor lane zero; portable model consumes the same table.
    table=rep(table,'    DP8KC #('+table.split('    DP8KC #(',1)[1].split('`else',1)[0],
              '    DP8KC #('+generate(words).split('    DP8KC #(',2)[1])
    files['microcode/generated/uj11_decode_table.v']=table
    for p,data in files.items():put('src/'+p,data)
    # Exhaustively compare logic decoder to the synchronous table's indexer.
    put('dump.v','''module dump; reg[15:0] ir; wire[9:0] entry; integer i;
uj11_decode dut(ir,entry);
initial begin for(i=0;i<65536;i=i+1)begin ir=i[15:0];#1;$display("%03x",entry);end $finish;end endmodule
''')
    subprocess.run(['iverilog','-g2012','-s','dump','-o',str(OUT/'dump'),str(OUT/'dump.v'),str(OUT/'src/rtl/uj11_decode.v')],check=True)
    truth=[int(s,16) for s in subprocess.check_output(['vvp',str(OUT/'dump')],text=True).splitlines() if len(s)==3]
    assert len(truth)==65536
    for opcode,entry in enumerate(truth):
        g=opcode>>12;m=int(bool(opcode&0o70))
        if g in (0,8):i=(0x100|((opcode>>15)<<7)|(((opcode>>6)&63)<<1)|m) if opcode>>8 else (0x300|(opcode&255))
        elif g==7:i=0x200|(((opcode>>9)&7)<<3)|(int(bool(opcode&0o700))<<2)|(((opcode>>5)&1)<<1)|m
        else:i=(g<<4)|(int(bool(opcode&0o7000))<<3)|(m<<2)
        assert words[i]==entry,(opcode,i,entry)
    inputs=['tools/build_service_cp58.py','tools/build_fram_cp52.py','tools/board_common.py','tools/make_ebr.py',
            'microasm/uj11asm.py','microcode/service_cp58.uasm','synth/reports/cp57e/inputs.json','synth/reports/cp57e/source.tgz']
    record=dict(reference='cp57e',mmu=False,used_words=stats['used_words'],fault_entry=address,
                inputs={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},
                outputs={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sorted(set(outputs))})
    (OUT/'inputs.json').write_text(json.dumps(record,indent=2)+'\n')
    return record


def adapt():
    build()
    return ([str((OUT/'src'/p).relative_to(ROOT)) for p in CORE],
            [str((OUT/'src'/p).relative_to(ROOT)) for p in BOARD])


if __name__=='__main__':print(json.dumps(build(),indent=2))
