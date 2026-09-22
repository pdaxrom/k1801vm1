#!/usr/bin/env python3
"""Install the current FPP firmware with native RT-11/UJMOD; step it in ODT."""
import argparse
import json
import re
import shutil
import subprocess
from pathlib import Path
from board_common import ROOT
from build_hardware import adapt, OUT as HW, firmware_rom
from build_software import odt, bootstrap, loader, OUT as SW, sha
from build_fpp import build as fp_build, OUT as FP
from build_software import native


def run(out):
    out.mkdir(parents=True,exist_ok=True)
    od=odt(ROOT/'build/software/odt');sd=bootstrap();ld=loader();core,board=adapt()
    # One UART poll for this long regression. Production timing is separately
    # exercised without acceleration by test_modules.py --full-window.
    sym=json.loads((HW/'boot-symbols.json').read_text())
    words=[int(w,16) for w in (HW/'firmware.mem').read_text().split()]
    overrides=[]
    for n in ('RLOOPS','RSPINS'):
        idx=(1056+sym[n]-sym['MSTART'])//2
        overrides.append(dict(symbol=n,index=idx,production=words[idx],test=1));words[idx]=1
    (out/'firmware.mem').write_text(''.join(f'{w:04x}\n' for w in words))
    (out/'firmware.v').write_text(firmware_rom(words).replace('build/hardware/firmware.mem',str((out/'firmware.mem').relative_to(ROOT))))
    board=[str((out/'firmware.v').relative_to(ROOT)) if p.endswith('/uj11_firmware_rom.v') else p for p in board]
    fp=fp_build();src=out/'FPTST.MAC';src.write_bytes((ROOT/'firmware/fpp/FPTST.MAC').read_bytes())
    blob,gs,assembly,directory=native(src);(out/'FPTST.SAV').write_bytes(blob)
    mapping=(directory/'FPTST.MAP').read_text(errors='replace')
    section=int(re.search(r'^ TEST\s+([0-7]{6})',mapping,re.M)[1],8)
    listing=(directory/'FPTST.LST').read_text(errors='replace')
    gs={n:section+int(v,8) for n,v in re.findall(r'\b([A-Z][A-Z0-9]{0,5})\s+([0-7]{6})R',listing)}
    assert int.from_bytes(blob[gs['ENTRY']:gs['ENTRY']+2],'little')==0o170011
    # MACRO treats FP immediates as floating constants, not raw octal bits.
    for name in ('FADD','FSUB'):
        assert int.from_bytes(blob[gs[name]+2:gs[name]+4],'little')==0o40200,name

    base=ROOT/'../lsi11-fpga/images/rt11v503.dsk';basehash=sha(base)
    image=out/'test.dsk'
    assert not image.exists(),'fresh RT-11 run directory required'
    shutil.copyfile(base,image)
    paths=[ROOT/'build/software/odt/ODT.BIN',SW/'sdboot/SDBOOT.BIN',SW/'loader/UJMOD.SAV']+[FP/'software/FP11.BIN',out/'FPTST.SAV']
    rt=ROOT/'../lsi11/rt11tool';extract=out/'extracted';extract.mkdir()
    for p in paths:
        subprocess.run([str(rt),'add',str(image),str(p),p.name],check=True,capture_output=True)
        subprocess.run([str(rt),'extract',str(image),str(extract),p.name],check=True,capture_output=True)
        assert (extract/p.name).read_bytes()==p.read_bytes()
    imagehash=sha(image)
    original=ROOT/'tests/rt11_harness.vh';cases=ROOT/'tests/fpp_rt11.vh'
    text=original.read_text()
    text=text+cases.read_text()+'\nendmodule\n'
    text=text.replace('if(window=="UJLOAD> ")','if(window[55:0]=="UJMOD> ")')
    text=text.replace('clocks>500000000','clocks>2000000000').replace('CP64 progress','CP79 progress')
    test=out/'tb.v';test.write_text(text)
    (out/'odt_symbols.vh').write_text(''.join(f'localparam integer {prefix}_{n}={v};\n' for prefix,sym in (('O',od['symbols']),('F',fp['symbols']),('G',gs)) for n,v in sym.items()))
    sources=[str(test.relative_to(ROOT))]+core+board+['rtl/uj11_rom.v','tests/models/ODDRXE.v','tests/models/spi_fram_model.v','tests/models/spi_sd_model.v']
    paths += [ROOT/p for p in sources]+[original,cases,Path(__file__),ROOT/'firmware/fpp/FPTST.MAC',src,out/'odt_symbols.vh',out/'firmware.mem',HW/'inputs.json',HW/'m0.mem',HW/'decode.mem']
    manifest=dict(files={str(p.relative_to(ROOT)):sha(p) for p in paths},base_sha256=basehash,image_sha256=imagehash,
                  odt=od,bootstrap=sd,loader=ld,fp=fp,guest_assembly=assembly,recovery_window_overrides=overrides)
    (out/'inputs.json').write_text(json.dumps(manifest,indent=2)+'\n')
    cmd=['verilator','--binary','--timing','-Wno-WIDTH','--top-module','tb_rt11','-j','4','--Mdir',str(out/'obj'),'-I'+str(out)]+sources
    with (out/'build.log').open('w') as log:subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'simulation.log').open('w') as log:
        result=subprocess.run([str(out/'obj/Vtb_rt11'),f'+SD_IMAGE={image}',f'+UART_LOG={out}/uart.txt'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    print((out/'simulation.log').read_text()[-3000:]);result.check_returncode()
    assert sha(base)==basehash and sha(image)==imagehash
    for p,h in manifest['files'].items():assert sha(ROOT/p)==h,p
    counts=re.search(r'PASS CP79 RT11 modules: (\d+) checks, (\d+) clocks, (\d+) UART bytes',(out/'simulation.log').read_text());assert counts
    (out/'result.json').write_text(json.dumps(dict(passed=True,checks=int(counts[1]),clocks=int(counts[2]),uart_bytes=int(counts[3]),
        files={p:sha(out/p) for p in ('inputs.json','simulation.log','uart.txt','build.log')}),indent=2)+'\n')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True)
    run(p.parse_args().out.resolve())
