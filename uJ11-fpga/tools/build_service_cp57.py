#!/usr/bin/env python3
"""Isolated service-bank gate over the exact installed CP56a RTL."""
import hashlib
import json
from pathlib import Path
import re
import sys
import tarfile
from board_common import ROOT, CORE, BOARD
from build_fram_cp52 import replace_once as rep
from make_ebr import generate
sys.path.insert(0, str(ROOT/'microasm'))
from uj11asm import assemble, AssemblyError
from link_fis import link
OUT = ROOT/'build/cp57-service'
ENTRIES = ['S_FP','S_ODT','S_START','S_RCPC','S_RCPS','S_WCPC','S_WCPS',
           'S_UPPER_READ','S_UPPER_WRITE','S_CONFIG','S_STATUS',
           'S_LOWER_READ','S_LOWER_WRITE','S_MFUS','S_MTUS','S_RSEL']


def place(base, extension):
    old, listing, old_labels, old_stats = assemble(base)
    used={int(l.split()[0],16) for l in listing.splitlines()}
    code=[]; labels={}; blocks=[]; start=0
    for raw in extension.splitlines():
        line=raw.split(';')[0].strip()
        if not line: continue
        if ':' in line:
            label,line=line.split(':',1); label=label.strip().upper(); line=line.strip()
            assert label not in labels and label not in old_labels
            labels[label]=len(code)
        if not line: continue
        assert not line.startswith('.')
        code.append(line)
        if line.split(',')[0].upper() in ('READ','WRITE','JUMP','STOP') or re.search(r'\bseq=FETCH\b',line,re.I):
            blocks.append(list(range(start,len(code))));start=len(code)
    assert start==len(code)
    placement={};bridges={}
    entry_indices={labels[k] for k in ENTRIES}
    for block in sorted(blocks,key=lambda b:(not any(i in entry_indices for i in b),-len(b),b[0])):
        while block:
            gaps=[]
            for a in range(1024):
                if a in used or (a and a-1 not in used):continue
                e=a
                while e<1024 and e not in used:e+=1
                if any(i in entry_indices for i in block):e=min(e,512)
                if e>a:gaps.append((a,e-a))
            fits=[g for g in gaps if g[1]>=len(block)]
            if fits:a,n=min(fits,key=lambda g:(g[1],g[0]));take=len(block)
            else:
                a,n=max(gaps,key=lambda g:(g[1],-g[0]))
                if n<2:raise AssemblyError('CP57 does not fit remaining microstore holes')
                take=n-1;bridges[a+take]=block[take];used.add(a+take)
            for offset,i in enumerate(block[:take]):placement[i]=a+offset;used.add(a+offset)
            block=block[take:]
    names={}
    for k,i in labels.items():names.setdefault(placement[i],[]).append(k)
    words={placement[i]:s for i,s in enumerate(code)}
    for a,i in bridges.items():
        k=f'S_LINK_{i}';names.setdefault(placement[i],[]).append(k)
        words[a]=f'JUMP, target={k}, prefetch=0'
    placed=[]
    for a,s in sorted(words.items()):
        placed+=[f'.org ${a:03x}']+[k+':' for k in names.get(a,[])]+['    '+s]
    source=base+'\n; CP57 generated placement\n'+'\n'.join(placed)+'\n'
    image,listing,mapped,stats=assemble(source)
    assert all(image[a]==old[a] for a in {int(l.split()[0],16) for l in assemble(base)[1].splitlines()})
    assert all(mapped[k]==v for k,v in old_labels.items())
    return source,image,listing,mapped,dict(baseline_words=old_stats['used_words'],source_words=len(code),
        bridge_words=len(bridges),used_words=stats['used_words'],free_words=1024-stats['used_words'])


def build():
    OUT.mkdir(parents=True,exist_ok=True)
    manifest=json.loads((ROOT/'synth/reports/cp56a/inputs.json').read_text())
    baseline={}
    with tarfile.open(ROOT/'synth/reports/cp56a/source.tgz') as ar:
        for path,sha in manifest['files'].items():
            if path.startswith('generated:'):continue
            try:data=ar.extractfile(path).read()
            except KeyError:continue
            assert hashlib.sha256(data).hexdigest()==sha,path
            baseline[path]=data
    # Frozen FIS link is reproducible; baseline ROM must match the release.
    base,_,_=link(baseline['microcode/m0.uasm'].decode(),baseline['microcode/fis.uasm'].decode())
    source,image,listing,labels,stats=place(base,(ROOT/'microcode/service_cp57.uasm').read_text())
    original=assemble(base)[0]
    from make_ebr import read_image
    # The archived vendor module encodes the assembled baseline exactly.
    assert generate(original).encode()==baseline['microcode/generated/uj11_m0_ebr.v']
    (OUT/'service.uasm').write_text(source)
    (OUT/'m0.mem').write_text(''.join(f'{w:09x}\n' for w in image))
    (OUT/'m0.lst').write_text(listing+'\n')
    (OUT/'m0.labels.json').write_text(json.dumps(labels,indent=2)+'\n')
    (OUT/'m0.stats.json').write_text(json.dumps(stats,indent=2)+'\n')
    (OUT/'uj11_m0_ebr.v').write_text(generate(image))
    files={p:baseline[p].decode() for p in CORE+BOARD if p in baseline}
    files['boards/hc1200/uj11_board_bus.v']=baseline['build/cp56-spi/baseline/uj11_board_bus.v'].decode()
    files['boards/hc1200/uj11_board_fram.v']=baseline['build/cp56-spi/fast/uj11_board_fram.v'].decode()
    for key in ('rtl/uj11_core.v','rtl/uj11_engine.v'):
        files[key]=rep(files[key],'    output wire mem_request, mem_read, mem_write, mem_byte,',
            '    output wire mem_request, mem_read, mem_write, mem_byte,\n    output wire mem_bank, mem_physical,')
    files['rtl/uj11_core.v']=rep(files['rtl/uj11_core.v'],'.mem_request(mem_request),.mem_read(mem_read)',
        '.mem_bank(mem_bank),.mem_physical(mem_physical),.mem_request(mem_request),.mem_read(mem_read)')
    e=files['rtl/uj11_engine.v']
    declarations='\n'.join(f"    localparam [9:0] {k}=10'h{labels[k]:03x};" for k in ENTRIES)+'\n'
    e=rep(e,'    wire [35:0] uword;',declarations+(ROOT/'experimental/cp57/engine-state.vh').read_text()+'\n    wire [35:0] uword;')
    e=rep(e,'wire irq_pending = irq_valid','wire irq_pending = !service_mode && irq_valid')
    e=rep(e,'wire trace_pending = irq_active==0','wire trace_pending = !service_mode && irq_active==0')
    e=rep(e,"            3'd0: d = 0;", "            3'd0: d = (uword[7] && uword[9:8]!=1) ? {8'b0,!service_ready[1],5'b0,service_ready} : 16'b0;")
    e=rep(e,'.dispatch_address(dispatch_address),.address_odd', '.dispatch_address(service_dispatch),.address_odd')
    e=rep(e,'.IMAGE("microcode/generated/m0.mem")','.IMAGE("build/cp57-service/m0.mem")')
    e=rep(e,'if (bus_fault!=0 && frame_active) fault_latched <= bus_fault;',
        'if (bus_fault!=0 && (frame_active || service_mode)) fault_latched <= bus_fault;')
    e=rep(e,'if (step) wait_seen <= wait_command;',
        'if (step) wait_seen <= wait_command;\n            if (step && service_leave) trace_latched <= psw[4];')
    files['rtl/uj11_engine.v']=e
    # Extend only reserved opcodes. VM2 STEP intentionally remains reserved at this gate.
    opcode_map={0:'S_ODT',**{i:'S_START' for i in range(0o10,0o14)},0o20:'S_RSEL',0o21:'S_MFUS',
        **{i:'S_RCPC' for i in range(0o22,0o24)},**{i:'S_RCPS' for i in range(0o24,0o30)},0o31:'S_MTUS',
        **{i:'S_WCPC' for i in range(0o32,0o34)},**{i:'S_WCPS' for i in range(0o34,0o40)},
        0o40:'S_UPPER_READ',0o41:'S_UPPER_WRITE',0o42:'S_CONFIG',0o43:'S_STATUS',
        0o44:'S_LOWER_READ',0o45:'S_LOWER_WRITE'}
    decode=files['rtl/uj11_decode.v']
    cases='\n'.join(f"            16'o{i:06o}: entry=10'h{labels[k]:03x};" for i,k in opcode_map.items())
    decode=rep(decode,'        endcase\n    end',"        endcase\n        if(ir[15:12]==4'hf)entry=10'h%03x;\n        case(ir)\n%s\n            default: begin end\n        endcase\n    end"%(labels['S_FP'],cases))
    files['rtl/uj11_decode.v']=decode
    # Dispatch table derived from exactly the new readable decoder, exhaustively checked.
    import subprocess
    (OUT/'decode.v').write_text(decode)
    (OUT/'dump.v').write_text('module dump; reg[15:0] ir; wire[9:0] entry; integer i; uj11_decode dut(ir,entry); initial begin for(i=0;i<65536;i=i+1)begin ir=i[15:0];#1;$display("%03x",entry);end $finish;end endmodule\n')
    subprocess.run(['iverilog','-g2012','-s','dump','-o',str(OUT/'dump'),str(OUT/'dump.v'),str(OUT/'decode.v')],check=True)
    truth=[int(s,16) for s in subprocess.check_output(['vvp',str(OUT/'dump')],text=True).splitlines() if len(s)==3]
    assert len(truth)==65536 and max(truth)<512
    words=[0x42]*1024;seen={}
    for opcode,entry in enumerate(truth):
        group=opcode>>12;memory=int(bool(opcode&0o70))
        if group in (0,8):index=(0x100|((opcode>>15)<<7)|(((opcode>>6)&63)<<1)|memory) if opcode>>8 else (0x300|(opcode&255))
        elif group==7:index=0x200|(((opcode>>9)&7)<<3)|(int(bool(opcode&0o700))<<2)|(((opcode>>5)&1)<<1)|memory
        else:index=(group<<4)|(int(bool(opcode&0o7000))<<3)|(memory<<2)
        assert index not in seen or seen[index]==entry,(opcode,index)
        seen[index]=entry;words[index]=entry
    (OUT/'decode.mem').write_text(''.join(f'{w:03x}\n' for w in words))
    generated=generate(words).split('    DP8KC #(',2)
    table=generated[0].replace('uj11_rom','uj11_decode_table').replace('[35:0]','[8:0]')
    table+='''`ifdef SYNTHESIS
`define UJ11_DECODE_EBR
`elsif UJ11_VENDOR_ROM
`define UJ11_DECODE_EBR
`endif
`ifdef UJ11_DECODE_EBR
'''+ '    DP8KC #('+generated[1]+'''`else
    reg [8:0] words[0:1023]; reg [8:0] value;
    initial $readmemh("build/cp57-service/decode.mem",words);
    always @(posedge clk) if(enable) value<=words[address];
    assign data=value;
`endif
`undef UJ11_DECODE_EBR
endmodule
'''
    files['microcode/generated/uj11_decode_table.v']=table
    b=files['boards/hc1200/uj11_board.v']
    b=rep(b,'    wire request, writing, byte_access, acknowledge, error, raw_ack;',
        '    wire request, writing, byte_access, acknowledge, error, raw_ack;\n    wire bank, physical;')
    b=rep(b,'.mem_write(writing),.mem_byte(byte_access)', '.mem_bank(bank),.mem_physical(physical),.mem_write(writing),.mem_byte(byte_access)')
    b=rep(b,'.byte_select(lanes),.address(address)', '.bank(bank),.physical(physical),.byte_select(lanes),.address(address)')
    files['boards/hc1200/uj11_board.v']=b
    b=files['boards/hc1200/uj11_board_bus.v']
    b=rep(b,'\tinput  wire [15:0] address,','\tinput  wire bank, physical,\n\tinput  wire [15:0] address,')
    b=rep(b,'wire local_boot_selected = BOOT_ROM_ENABLE','wire local_boot_selected = !bank && !physical && BOOT_ROM_ENABLE')
    b=rep(b,'wire boot_program_selected = BOOT_ROM_ENABLE','wire boot_program_selected = !bank && !physical && BOOT_ROM_ENABLE')
    b=rep(b,'wire service_program_selected = RK_SERVICE_ENABLE','wire service_program_selected = !bank && !physical && RK_SERVICE_ENABLE')
    b=rep(b,'wire cpu_io_page = io_page && !service_dma_operand;','wire cpu_io_page = io_page && !service_dma_operand && !physical;')
    b=rep(b,'wire guest_fram_selected = (!io_page && !boot_selected) ||', 'wire guest_fram_selected = physical || (!io_page && !boot_selected) ||')
    b=rep(b,".byte_access(fram_byte_access), .bank(1'b0)",'.byte_access(fram_byte_access), .bank(bank && !service_dma_operand)')
    b=rep(b,'.keep_read(!io_page && !boot_selected && !service_dma_selected)', '.keep_read(!io_page && !boot_selected && !service_dma_selected && !physical)')
    # Raw loader reads of address 0 must not release a pending bootstrap overlay.
    b=rep(b,'if (boot_release_armed && request && !write && word_address == 0)',
        'if (boot_release_armed && !bank && !physical && request && !write && word_address == 0)')
    files['boards/hc1200/uj11_board_bus.v']=b
    outputs=[]
    for p,data in files.items():
        dest=OUT/'src'/p;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text(data);outputs.append(str(dest.relative_to(ROOT)))
    inputs=['tools/build_service_cp57.py','tools/build_fram_cp52.py','tools/board_common.py','tools/link_fis.py','tools/make_ebr.py',
        'microasm/uj11asm.py','microcode/service_cp57.uasm','experimental/cp57/engine-state.vh',
        'synth/reports/cp56a/inputs.json','synth/reports/cp56a/source.tgz']
    outputs+=['build/cp57-service/'+p for p in ['service.uasm','m0.mem','m0.lst','m0.labels.json','m0.stats.json','uj11_m0_ebr.v','decode.mem']]
    record=dict(reference='cp56a',mmu=False,**stats,entries={k:labels[k] for k in ENTRIES},
        inputs={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},
        outputs={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in outputs})
    (OUT/'inputs.json').write_text(json.dumps(record,indent=2)+'\n')
    return record


def adapt():
    build()
    return ([str((OUT/'src'/p).relative_to(ROOT)) for p in CORE],
            [str((OUT/'src'/p).relative_to(ROOT)) for p in BOARD])

if __name__=='__main__':print(json.dumps(build(),indent=2))
