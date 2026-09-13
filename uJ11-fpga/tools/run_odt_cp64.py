#!/usr/bin/env python3
"""Cold RT-11 -> UJLOAD -> UJON -> actual debugger on full SPI/UART board RTL."""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
from pathlib import Path
from board_common import ROOT
from build_debug_cp63 import adapt
from rt11_build import build


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def run(odt,out,cp65=False,cp66=False):
    out.mkdir(parents=True,exist_ok=True);assert not (out/'test.dsk').exists(),'fresh run directory required'
    result=json.loads((odt/'result.json').read_text());sym=result['symbols']
    assert not (cp65 and cp66)
    revision='cp66' if cp66 else ('cp65' if cp65 else 'cp64')
    asm=out/'guest';build([ROOT/'demos/rt11/service'/revision/'UJTEST.MAC'],asm,ROOT/'../lsi11-fpga/images/rt11v503.dsk')
    listing=(asm/'UJTEST.LST').read_text(errors='replace')
    guest={n:int(v,8)+0o1000 for n,v in re.findall(r'\b([A-Z][A-Z0-9]{0,5})\s+([0-7]{6})R',listing)}
    base=ROOT/'../lsi11-fpga/images/rt11v503.dsk';basehash=sha(base)
    image=out/'test.dsk';shutil.copyfile(base,image)
    paths=[ROOT/'demos/rt11/service/cp62/UJLOAD.SAV',odt/'ODT.BIN',odt/'activation/UJON.SAV',asm/'UJTEST.SAV']
    extract=out/'extracted';extract.mkdir()
    for p in paths:
        subprocess.run([str(ROOT/'../lsi11/rt11tool'),'add',str(image),str(p),p.name],capture_output=True,check=True)
        subprocess.run([str(ROOT/'../lsi11/rt11tool'),'extract',str(image),str(extract),p.name],capture_output=True,check=True)
        assert (extract/p.name).read_bytes()==p.read_bytes()
    imagehash=sha(image)
    font_source=(ROOT/'demos/rt11/panel/PNLDRV.MAC').read_text().split('FONT:',1)[1]
    font=[]
    for match in re.finditer(r'\.BYTE\s+([^;\n]+)',font_source):font.extend(int(n.strip(),8) for n in match[1].split(','))
    assert len(font)==320
    (out/'font.hex').write_text(''.join(f'{n:02x}\n' for n in font))
    core,board=adapt()
    tb_path=ROOT/('tb/tb_odt_cp65_rt11.v' if cp65 or cp66 else 'tb/tb_odt_rt11.v')
    tb=tb_path.read_text()
    if cp66:
        tb=tb.replace('CP65','CP66')
        tb=tb.replace('reg [15:0] oldsp;', 'reg [15:0] oldsp,oldpsw;')
        tb=tb.replace('oldsp=upper(O_REGS+12);', 'oldsp=upper(O_REGS+12);oldpsw=upper(O_REGS+16);')
        point='        cmdtext=$sformatf("R 7 %o",G_LOOP);odt_command(cmdtext);'
        assert tb.count(point)==1
        tb=tb.replace(point,(ROOT/'tb/odt_cp66_rt11.vh').read_text()+point)
        tb=tb.replace('    integer clocks=', '    integer irq_total=0,rk_starts=0,old_irq=0,old_rk=0; reg rk_was=0;\n    always @(posedge clk)if(!reset)begin\n        if(dut.irq_ack)irq_total<=irq_total+1;\n        if(dut.bus.rk_service_active && !rk_was)rk_starts<=rk_starts+1;\n        rk_was<=dut.bus.rk_service_active;\n    end\n    integer clocks=')
    test=out/'tb.v';test.write_text(tb.replace('    integer clocks=',f'    initial $readmemh("{out}/font.hex",font);\n    integer clocks='))
    guest_names=('LOOP','DONE','OVCALL','OVDONE','OLDSP') if cp65 or cp66 else ('LOOP','DONE')
    if cp66:guest_names+=('WKCALL','WKDONE','TICKS','DKCALL','DKDONE','DKOK')
    (out/'odt_symbols.vh').write_text(''.join(f"localparam integer O_{n}={v};\n" for n,v in sym.items() if n in ('REGS','SCREEN','RESULT','VIEW','SCROLL','KLAST','PNEN','PANEDIT','MAIN','STKTOP','DCUR'))+''.join(f'localparam integer G_{n}={guest[n]};\n' for n in guest_names))
    sources=[str(test)]+core+board+['rtl/uj11_rom.v','tb/models/ODDRXE.v',
                                 'reference/lsi11/spi_fram_model.v','reference/lsi11/spi_sd_model_cp28.v']
    manifest={str(p.relative_to(ROOT)):sha(p) for p in paths+[tb_path,Path(__file__)]}
    if cp66:manifest['tb/odt_cp66_rt11.vh']=sha(ROOT/'tb/odt_cp66_rt11.vh')
    manifest.update({p:sha(ROOT/p) for p in core+board})
    (out/'inputs.json').write_text(json.dumps(dict(files=manifest,odt=result,guest=guest,image_sha256=imagehash),indent=2)+'\n')
    for i,name in enumerate(sources):
        if name.startswith('reference/') or '/reference/' in name:
            path=out/Path(name).name;path.write_text('/* verilator lint_off WIDTH */\n'+(ROOT/name).read_text()+'\n/* verilator lint_on WIDTH */\n');sources[i]=str(path)
    command=['verilator','--binary','--timing','--top-module','tb_odt_rt11','-j','4','--Mdir',str(out/'obj'),'-I'+str(out)]+sources
    with (out/'build.log').open('w') as log:subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'simulation.log').open('w') as log:r=subprocess.run([str(out/'obj/Vtb_odt_rt11'),f'+SD_IMAGE={image}',f'+UART_LOG={out}/uart.txt'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    print((out/'simulation.log').read_text()[-4000:]);r.check_returncode()
    assert sha(base)==basehash and sha(image)==imagehash
    for p,h in manifest.items():assert sha(ROOT/p)==h,p
    (out/'result.json').write_text(json.dumps(dict(passed=True,files={p:sha(out/p) for p in ('inputs.json','simulation.log','uart.txt','build.log')}),indent=2)+'\n')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--odt',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--cp65',action='store_true',help='CP65 automatic panel navigation and native STEP OVER fixture')
    p.add_argument('--cp66',action='store_true',help='CP66 breakpoints and STEP OVER with real KW11-L and RT-11 disk I/O')
    a=p.parse_args();run(a.odt.resolve(),a.out.resolve(),cp65=a.cp65,cp66=a.cp66)
