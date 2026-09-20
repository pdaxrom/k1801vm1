#!/usr/bin/env python3
"""Full CP67 board simulation of DEC BASIC FIS/FPU and CP80 software FPP."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
from board_common import ROOT
from build_modules_cp67 import adapt, OUT as HW, firmware_rom


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def run(out, built, programs):
    out.mkdir(parents=True, exist_ok=True)
    image=out/'test.dsk'
    assert not image.exists(), 'fresh output directory required'
    core,board=adapt()
    sym=json.loads((HW/'boot-symbols.json').read_text())
    words=[int(w,16) for w in (HW/'firmware.mem').read_text().split()]
    overrides=[]
    for n in ('RLOOPS','RSPINS'):
        idx=(1056+sym[n]-sym['MSTART'])//2
        overrides.append(dict(symbol=n,index=idx,production=words[idx],test=1));words[idx]=1
    (out/'firmware.mem').write_text(''.join(f'{w:04x}\n' for w in words))
    (out/'firmware.v').write_text(firmware_rom(words).replace('build/cp67-modules/firmware.mem',str((out/'firmware.mem').relative_to(ROOT))))
    board=[str((out/'firmware.v').relative_to(ROOT)) if p.endswith('/uj11_firmware_rom.v') else p for p in board]
    # Previously built and qualified CP80 modules; no new firmware compilation.
    locations=[ROOT/'build/cp80-odt',ROOT/'build/cp67-software/sdboot',
               ROOT/'build/cp67-software/loader',ROOT/'build/cp80-fp11/software']
    modules=[p/n for p,n in zip(locations,['ODT.BIN','SDBOOT.BIN','UJMOD.SAV','FP11.BIN'])]
    shutil.copyfile(built/'build.dsk',image)
    rt=ROOT/'../lsi11/rt11tool'
    paths=modules+[programs/'B81TST.BAS',programs/'B81DBL.BAS']
    for p in paths:
        subprocess.run([str(rt),'add',str(image),str(p),p.name],check=True,capture_output=True)
    original=ROOT/'tb/tb_odt_rt11.v';cases=ROOT/'tb/basic_rt11_cp81.vh'
    text=original.read_text()
    text=text[:text.index('    initial begin\n        #1;')]+cases.read_text()+'\nendmodule\n'
    text=text.replace('integer clocks=0,prompts=0','longint clocks=0; integer prompts=0')
    text=text.replace('if(window=="UJLOAD> ")','if(window[55:0]=="UJMOD> ")')
    # The existing UART monitor shifts window with a blocking assignment.
    # Detect complete suffixes in that monitor, avoiding ordering races.
    text=text.replace('previous_char<=dut.data[7:0];',
        'if(window[47:0]==48\'h52454144590d)ready_count<=ready_count+1;\n'
        '            if(window[47:0]=="DUAL)?")option_count<=option_count+1;\n'
        '            previous_char<=dut.data[7:0];')
    a=text.index('        if(dut.acknowledge && dut.writing',text.index('integer ready_count'))
    b=text.index('        if(counting',a)
    text=text[:a]+text[b:]
    text=text.replace('clocks>500000000',"clocks>64'd10000000000").replace('clocks%20000000','clocks%100000000').replace('CP64 progress','CP81 progress')
    test=out/'tb.v';test.write_text(text)
    od=json.loads((locations[0]/'result.json').read_text())
    fp=json.loads((locations[3]/'result.json').read_text())
    (out/'odt_symbols.vh').write_text(''.join(f'localparam integer {prefix}_{n}={v};\n' for prefix,sym in (('O',od['symbols']),('F',fp['symbols'])) for n,v in sym.items()))
    sources=[str(test.relative_to(ROOT))]+core+board+['rtl/uj11_rom.v','tb/models/ODDRXE.v','reference/lsi11/spi_fram_model.v','reference/lsi11/spi_sd_model_cp28.v']
    paths += [ROOT/p for p in sources]+[original,cases,Path(__file__),out/'odt_symbols.vh',out/'firmware.mem',HW/'inputs.json',HW/'m0.mem',HW/'decode.mem']
    manifest=dict(files={str(p.relative_to(ROOT)):sha(p) for p in paths},base_sha256=sha(built/'build.dsk'),image_sha256=sha(image),
                  recovery_window_overrides=overrides,basic_inputs=json.loads((built/'build-inputs.json').read_text()))
    (out/'inputs.json').write_text(json.dumps(manifest,indent=2)+'\n')
    cmd=['verilator','--binary','--timing','-Wno-WIDTH','--top-module','tb_odt_rt11','-j','4','--Mdir',str(out/'obj'),'-I'+str(out)]+sources
    with (out/'build.log').open('w') as log:subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'simulation.log').open('w') as log:
        result=subprocess.run([str(out/'obj/Vtb_odt_rt11'),f'+SD_IMAGE={image}',f'+UART_LOG={out}/uart.txt'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    print((out/'simulation.log').read_text()[-3000:]);result.check_returncode()
    assert sha(image)==manifest['image_sha256']
    for p,h in manifest['files'].items():assert sha(ROOT/p)==h,p
    counts=re.search(r'PASS CP81 BASIC: (\d+) checks, (\d+) clocks, (\d+) UART bytes',(out/'simulation.log').read_text());assert counts
    (out/'result.json').write_text(json.dumps(dict(passed=True,checks=int(counts[1]),clocks=int(counts[2]),uart_bytes=int(counts[3]),
        files={p:sha(out/p) for p in ('inputs.json','simulation.log','uart.txt','build.log')}),indent=2)+'\n')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True);p.add_argument('--built',type=Path,required=True);p.add_argument('--programs',type=Path,required=True)
    a=p.parse_args();run(a.out.resolve(),a.built.resolve(),a.programs.resolve())
