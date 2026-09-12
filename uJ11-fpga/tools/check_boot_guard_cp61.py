#!/usr/bin/env python3
"""Exhaust the changed dispatch cone and prove all other CP61e RTL identical."""
import hashlib
import json
import re
import subprocess
import tarfile
from board_common import ROOT
from build_halt_boot_cp61 import adapt, OUT


def main():
    core,board=adapt();ref=ROOT/'synth/reports/cp61e'
    manifest=json.loads((ref/'inputs.json').read_text());identical=[]
    with tarfile.open(ref/'source.tgz') as archive:
        for name in core+board+['build/cp61-boot/'+n for n in ('m0.mem','decode.mem','firmware.mem','uj11_m0_ebr.v')]:
            old=archive.extractfile(name).read();assert hashlib.sha256(old).hexdigest()==manifest['files'][name]
            new=(ROOT/name).read_bytes()
            if name.endswith('/uj11_engine.v'):
                expected=old.replace(b'service_ir[5] ? service_ir[4:1]==2 : |service_ir[4:3]',
                                     b'service_ir[5] ? ~|service_ir[4:3] : |service_ir[4:3]')
                assert expected!=old and new==expected
                engines=[old.decode(),new.decode()]
            else:assert new==old,name;identical.append(name)
    cones=[]
    for i,text in enumerate(engines):
        constants='\n'.join(re.findall(r'    localparam \[9:0\].*;',text))
        start=text.index('    wire service_privileged=');end=text.index('    wire [35:0] uword;',start)
        cones.append(f'''module cone{i}(input [15:0] service_ir,input [1:0] service_ready,
input service_mode,input [9:0] dispatch_address,output [9:0] result);
{constants}
{text[start:end]}
assign result=service_dispatch;
endmodule
''')
    tb='''module test;
reg[15:0] ir;reg[1:0] ready;reg mode;wire[9:0] expected,observed;
wire [9:0] normal;
uj11_decode decode(ir,normal);
integer op,m,r,count=0;
cone0 reference_cone(ir,ready,mode,normal,expected);cone1 dut(ir,ready,mode,normal,observed);
initial begin for(r=0;r<4;r=r+1)for(m=0;m<2;m=m+1)for(op=0;op<65536;op=op+1)begin
ir=op;ready=r;mode=m;#1;
if(m==0 && op>=32 && op<=35)begin
 if(observed!==10'h42)$fatal(1,"guest installation opcode escaped %o",ir);
end else if(observed!==expected)$fatal(1,"unrelated dispatch changed %o mode%0d",ir,m);
count=count+1;
end
$display("PASS CP61 dispatch: %0d opcode/mode/ready combinations",count);$finish;end
endmodule
'''
    paths=[]
    for negative in (False,True):
        name='guard-negative' if negative else 'guard'
        path=OUT/(name+'.v');path.write_text(''.join(cones if not negative else [cones[0],cones[0].replace('cone0','cone1')])+tb)
        binary=OUT/name
        subprocess.run(['iverilog','-g2012','-s','test','-o',str(binary),str(path),
                        str(OUT/'src/rtl/uj11_decode.v')],check=True,cwd=ROOT)
        run=subprocess.run(['vvp',str(binary)],capture_output=True,text=True,cwd=ROOT)
        (OUT/(name+'.log')).write_text(run.stdout+run.stderr)
        assert (run.returncode!=0)==negative,run.stdout
        if negative:assert 'guest installation opcode escaped' in run.stdout
        else:assert '524288 opcode/mode/ready combinations' in run.stdout
        paths += [str(path.relative_to(ROOT)),str((OUT/(name+'.log')).relative_to(ROOT))]
    paths += ['tools/check_boot_guard_cp61.py','build/cp61-boot/inputs.json','synth/reports/cp61e/inputs.json','synth/reports/cp61e/source.tgz']
    result=dict(passed=True,combinations=524288,negative_control_rejected=True,
                identical_rtl_and_rom=identical,only_changed_guest_opcodes_octal=['000040','000041','000042','000043'],
                files={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths})
    (OUT/'guard-proof.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS CP61: only four USER dispatches changed; 524288 combinations; negative control rejected')


if __name__=='__main__':main()
