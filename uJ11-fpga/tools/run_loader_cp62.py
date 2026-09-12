#!/usr/bin/env python3
"""Run UJLOAD.SAV on the CP62 RK-recovery board, booting real RT-11."""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
from pathlib import Path
from board_common import ROOT, profile_flags
from build_vector_loader_cp62 import adapt
from loader_fixtures_cp62 import OUT, files, generate_cases


def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--asm-dir',required=True,type=Path)
    parser.add_argument('--tag',required=True)
    parser.add_argument('--ctrl-only',action='store_true')
    args=parser.parse_args()
    assert re.fullmatch('cp62[a-z][a-z0-9-]*',args.tag)
    out=ROOT/'build'/args.tag
    out.mkdir(parents=True,exist_ok=True)
    image=out/'test.dsk';assert not image.exists(),'fresh test run required'
    asm=args.asm_dir.resolve();built=json.loads((asm/'build-inputs.json').read_text())
    for path,digest in built['source_sha256'].items():assert sha(ROOT/path)==digest,path
    for path,digest in built['outputs'].items():assert sha(asm/path)==digest,path
    from build_loader_cp62 import build as check_helper
    check_helper()
    core,board=adapt()
    images=files();cases=generate_cases(images)
    base=ROOT/'../lsi11-fpga/images/rt11v503.dsk';base_hash=sha(base);shutil.copyfile(base,image)
    programs=[asm/'UJLOAD.SAV',asm/'UJCHEK.SAV']+[OUT/(n+'.BIN') for n in images]
    rt=ROOT/'../lsi11/rt11tool'
    extract=out/'extracted';extract.mkdir()
    for path in programs:
        subprocess.run([str(rt),'add',str(image),str(path),path.name],check=True,capture_output=True)
        subprocess.run([str(rt),'extract',str(image),str(extract),path.name],check=True,capture_output=True)
        assert (extract/path.name).read_bytes()==path.read_bytes(),path
    directory=subprocess.check_output([str(rt),'ls',str(image)],text=True)
    (out/'directory.txt').write_text(directory)
    error_lba=int(re.search(r'^ODT\.BIN\s+([0-7]+)',directory,re.M)[1],8)+1
    listing=(asm/'UJLOAD.LST').read_text(errors='replace')
    symbols={m[0]:int(m[1],8)+0o1000 for m in re.findall(r'\b([A-Z][A-Z0-9]{0,5})\s+([0-7]{6})R',listing)}
    assert all(n in symbols for n in ('COPY','VERIFY','COMMIT'))
    (out/'symbols.json').write_text(json.dumps(symbols,indent=2)+'\n')
    core,board=adapt()
    profile=json.loads((ROOT/'build/cp62-boot/inputs.json').read_text())
    for path in core+board:assert sha(ROOT/path)==profile['outputs'][path],path
    source_names=['tb/tb_vector_loader_rt11.v']+core+board+['rtl/uj11_rom.v','tb/models/ODDRXE.v',
                 'reference/lsi11/spi_fram_model.v','reference/lsi11/spi_sd_model_cp28.v']
    paths=source_names+['tools/run_loader_cp62.py','tools/loader_fixtures_cp62.py','tools/service_image_cp62.py',
        'tools/build_vector_loader_cp62.py','build/cp62-boot/inputs.json','build/cp62-boot/m0.mem','build/cp62-boot/decode.mem',
        'build/cp62-boot/firmware.mem','build/cp62-loader/loader_cases.vh']+list(profile['inputs'])
    paths += [str(p.relative_to(ROOT)) for p in OUT.glob('expected-*.hex')]
    paths += [str(p.relative_to(ROOT)) for p in programs]
    paths += ['tools/build_loader_cp62.py','build/cp62-loader/cold.hex']
    paths += [str(p.relative_to(ROOT)) for p in (ROOT/'firmware/cp62').iterdir() if p.is_file()]
    manifest=dict(files={p:sha(ROOT/p) for p in paths},base_sha256=base_hash,image_sha256=sha(image),
        baseline='cp61g',rtl_changed=False,profile=profile,cases=cases,symbols=symbols,build=built,error_lba=error_lba)
    (out/'inputs.json').write_text(json.dumps(manifest,indent=2)+'\n')
    sources=source_names.copy()
    for i,name in enumerate(sources):
        if name.startswith('reference/') or '/reference/' in name:
            path=out/Path(name).name
            path.write_text('/* verilator lint_off WIDTH */\n'+(ROOT/name).read_text()+'\n/* verilator lint_on WIDTH */\n')
            sources[i]=str(path)
    command=['verilator','--binary','--timing','--top-module','tb_loader_rt11','-j','4',
        '--Mdir',str(out/'obj'),'-I'+str(OUT),f"+define+CP62_COPY=16'h{symbols['COPY']:04x}",
        f"+define+CP62_VERIFY=16'h{symbols['VERIFY']:04x}",f"+define+CP62_ERROR_LBA=32'd{error_lba}"]+sources+profile_flags(False)
    with (out/'build.log').open('w') as log: subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'simulation.log').open('w') as log:
        run=subprocess.run([str(out/'obj/Vtb_loader_rt11'),f'+SD_IMAGE={image}',f'+UART_LOG={out}/uart.txt']+(['+CTRL_ONLY'] if args.ctrl_only else []),
                           cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    assert sha(base)==base_hash and sha(image)==manifest['image_sha256'],'test changed a disk backing file'
    for path,digest in manifest['files'].items():assert sha(ROOT/path)==digest,path
    log=(out/'simulation.log').read_text();print(log[-5000:]);run.check_returncode()
    assert ('PASS CP62 focused CTRL/C:' if args.ctrl_only else 'PASS CP62 full RT-11 loader:') in log
    uart=(out/'uart.txt').read_text()
    if args.ctrl_only:
        assert 'PASS CP62 CTRL/C during payload transfer' in log
        (out/'result.json').write_text(json.dumps(dict(passed=True,ctrl_only=True,files={p.name:sha(p) for p in (out/'inputs.json',out/'simulation.log',out/'uart.txt',out/'build.log')}),indent=2)+'\n')
        return
    assert '!UJCHEK-I-HALT FP11 START PASS' in uart and '?UJCHEK' not in uart
    assert uart.count('?UJLOAD-E-Invalid header or file length')==12
    assert '?UJLOAD-E-READ failed' in uart and '?UJLOAD-E-FRAM readback failed' in uart
    assert uart.count('?UJLOAD-E-Checksum or padding failed')==3
    result=dict(passed=True,cases=len(cases),ctrl_c=True,cold_reboot=True,guest_context=True,
        uut='full CP62 recovery RTL, real SPI FRAM/SD and RT-11FB V05.03; actual UART input/output',
        files={p.name:sha(p) for p in (out/'inputs.json',out/'simulation.log',out/'uart.txt',out/'build.log')})
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
