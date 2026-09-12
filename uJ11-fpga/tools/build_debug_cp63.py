#!/usr/bin/env python3
"""Opt-in external HALT / STEP cost gate over the exact measured CP62a."""
import hashlib
import json
import sys
import tarfile
from board_common import ROOT, CORE, BOARD
from build_fram_cp52 import replace_once as rep
from make_ebr import generate
sys.path.insert(0, str(ROOT/'microasm'))
from uj11asm import assemble

OUT=ROOT/'build/cp63-debug'
BUTTON='boards/hc1200/uj11_button.v'


def build():
    manifest=json.loads((ROOT/'synth/reports/cp62a/inputs.json').read_text())
    frozen={}
    with tarfile.open(ROOT/'synth/reports/cp62a/source.tgz') as ar:
        for path,digest in manifest['files'].items():
            if path.startswith('generated:'): continue
            data=ar.extractfile(path).read()
            assert hashlib.sha256(data).hexdigest()==digest,path
            frozen[path]=data
    prefix='build/cp62-boot/'
    source=frozen[prefix+'service.uasm'].decode()
    old,listing,labels,_=assemble(source)
    used={int(line.split()[0],16) for line in listing.splitlines()}
    free=[a for a in range(1024) if a not in used]
    # Three words: enter HALT, select a separate vector, share CPC/CPSW save.
    entry=free[0]
    select=next(a for a in free if a+1 in free)
    jump=select+1
    source+=f'''
; CP63 external debugger uses vector 000110/112 in HALT RAM.
; It never enters the R4-signature native loader gate at vector 170.
.org ${entry:03x}
S_DEBUG:
    JUMP, service=ENTER, target=S_DEBUG_VECTOR, prefetch=0
.org ${select:03x}
S_DEBUG_VECTOR:
    alu PASSA, pair=DA, d=IMM, imm=72, b=T4, dst=RF
.org ${jump:03x}
S_DEBUG_LINK:
    JUMP, target=S_VECTOR, prefetch=0
'''
    image,listing,labels,stats=assemble(source)
    assert all(old[a]==image[a] for a in used)
    outputs=[]
    def put(name,text):
        p=OUT/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text)
        outputs.append(str(p.relative_to(ROOT)))
    put('service.uasm',source);put('m0.mem',''.join(f'{w:09x}\n' for w in image))
    put('m0.lst',listing+'\n');put('m0.labels.json',json.dumps(labels,indent=2)+'\n')
    put('m0.stats.json',json.dumps(dict(used_words=stats['used_words'],free_words=1024-stats['used_words']),indent=2)+'\n')
    put('uj11_m0_ebr.v',generate(image))
    for name in ('decode.mem','firmware.mem'):put(name,frozen[prefix+name].decode())
    files={p:frozen[prefix+'src/'+p].decode().replace(prefix,'build/cp63-debug/')
           for p in CORE+BOARD+['boards/hc1200/uj11_microcomp.v']}
    for p in ('rtl/uj11_core.v','rtl/uj11_engine.v'):
        files[p]=rep(files[p],'input wire clk, reset,','input wire clk, reset,\n    input wire halt_button, debug_block,')
    files['rtl/uj11_core.v']=rep(files['rtl/uj11_core.v'],'.irq_valid(irq_valid),',
        '.halt_button(halt_button),.debug_block(debug_block),.irq_valid(irq_valid),')
    e=files['rtl/uj11_engine.v']
    e=rep(e,'    reg service_mode;','''    reg service_mode;
    // CONFIG/STATUS low bits 0..1 retain ABI2 meaning. Bit2 opts into CP63;
    // old CP62 software cannot accidentally enter an uninstalled debug vector.
    reg debug_enabled, debug_pending, debug_context, debug_wait, debug_trace;
    wire debug_wanted=debug_enabled && service_ready[0] && debug_pending &&
                      !service_mode && !debug_block;
    wire debug_ack=step && debug_wanted && (alu_boundary || wait_command);
    wire wait_return=service_leave && debug_context && debug_wait;
    always @(posedge clk) begin
        if(reset) begin
            debug_enabled<=0; debug_pending<=0; debug_context<=0;
            debug_wait<=0; debug_trace<=0;
        end else begin
            if(halt_button && debug_enabled && service_ready[0]) debug_pending<=1;
            if(step && service_config) begin
                debug_enabled<=read_a[2];
                debug_pending<=read_a[3];
                debug_wait<=read_a[5];
                debug_trace<=read_a[6];
            end
            if(debug_ack) begin
                debug_pending<=0;
                debug_context<=1;
                debug_wait<=wait_command;
                debug_trace<=trace_pending;
            end
            if(step && service_leave) begin
                debug_context<=0;
                if(debug_context) debug_pending<=0;
                if(ir[2] && debug_enabled && service_ready[0]) debug_pending<=1;
            end
        end
    end''')
    e=rep(e,'step && trace_pending && (alu_boundary || wait_command)',
          'step && trace_pending && !debug_wanted && (alu_boundary || wait_command)')
    e=rep(e,'step && irq_pending && !trace_pending && (alu_boundary || wait_command)',
          'step && irq_pending && !trace_pending && !debug_wanted && (alu_boundary || wait_command)')
    e=rep(e,"{8'b0,!service_ready[1],5'b0,service_ready}",
          "{7'b0,1'b1,!service_ready[1],debug_trace,debug_wait,debug_context,debug_pending,debug_enabled,service_ready}")
    e=rep(e,'.step_return(service_leave && ir[2]),',
          '.debug_pending(debug_wanted),.wait_return(wait_return),.step_return(service_leave && ir[2]),')
    e=rep(e,'if (step && service_leave) trace_latched <= psw[4];',
          '''if (step && service_leave) begin
                trace_latched <= debug_context ? debug_trace : psw[4];
                if(wait_return) wait_seen<=1;
            end''')
    files['rtl/uj11_engine.v']=e
    s=files['rtl/uj11_microseq.v']
    s=rep(s,'input wire step_return,','input wire step_return, debug_pending, wait_return,')
    s=rep(s,"wire [9:0] fetch_address = trace_pending ?",f"wire [9:0] fetch_address = debug_pending ? 10'h{entry:03x} : trace_pending ?")
    s=rep(s,'(trace_pending || irq_pending) ? fetch_address','(debug_pending || trace_pending || irq_pending) ? fetch_address')
    s=rep(s,'if (step_return) next_address = FETCH;',
          "if (step_return) next_address = FETCH;\n        if (wait_return) next_address = 10'h012;")
    files['rtl/uj11_microseq.v']=s
    b=files['boards/hc1200/uj11_board.v']
    b=rep(b,'input wire clk, reset, uart_rx,','input wire clk, reset, uart_rx, halt_button,')
    b=rep(b,'wire bank, physical;','wire bank, physical, debug_block;')
    b=rep(b,'.clk(clk),.reset(reset),.irq_valid(irq_valid),',
          '.clk(clk),.reset(reset),.halt_button(halt_button),.debug_block(debug_block),.irq_valid(irq_valid),')
    b=rep(b,'.clk(clk),.rst(reset),.peripheral_reset(peripheral_reset),',
          '.clk(clk),.rst(reset),.debug_block(debug_block),.peripheral_reset(peripheral_reset),')
    files['boards/hc1200/uj11_board.v']=b
    b=files['boards/hc1200/uj11_board_bus.v']
    b=rep(b,'output wire [15:0] interrupt_vector,',
          'output wire debug_block,\n\toutput wire [15:0] interrupt_vector,')
    b=rep(b,'reg rk_service_active;','reg rk_service_active;\n\tassign debug_block=rk_service_active;')
    files['boards/hc1200/uj11_board_bus.v']=b
    top=files['boards/hc1200/uj11_microcomp.v']
    start=top.index('    reg [1:0] reset_sync=')
    end=top.index('    OSCH #',start)
    top=top[:start]+'''    reg [1:0] power_on=2'b11;
    always @(posedge clk) power_on<={1'b0,power_on[1]};
    wire hard_reset, halt_button;
    uj11_button button(.clk(clk),.power_on(power_on[0]),.button_n(res),
        .hard_reset(hard_reset),.halt_pulse(halt_button));
'''+top[end:]
    top=rep(top,'.reset(reset_sync[0]),','.reset(power_on[0] || hard_reset),.halt_button(halt_button),')
    files['boards/hc1200/uj11_microcomp.v']=top
    files[BUTTON]=(ROOT/BUTTON).read_text()
    for p,data in files.items():put('src/'+p,data)
    inputs=['tools/build_debug_cp63.py',BUTTON,'tools/build_fram_cp52.py','tools/board_common.py',
            'tools/make_ebr.py','microasm/uj11asm.py','synth/reports/cp62a/inputs.json','synth/reports/cp62a/source.tgz']
    record=dict(reference='cp62a',mmu=False,used_words=stats['used_words'],debug_entry=entry,
                inputs={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},
                outputs={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in outputs})
    (OUT/'inputs.json').write_text(json.dumps(record,indent=2)+'\n')
    return record


def adapt():
    build()
    return ([str((OUT/'src'/p).relative_to(ROOT)) for p in CORE],
            [str((OUT/'src'/p).relative_to(ROOT)) for p in BOARD+[BUTTON]])


if __name__=='__main__': print(json.dumps(build(),indent=2))
