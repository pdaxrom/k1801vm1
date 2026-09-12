#!/usr/bin/env python3
"""Two real RT-11 cold boots and HALT-copy calls, no injected CPU state."""
import hashlib
import argparse
import json
import shutil
import subprocess
from pathlib import Path
from board_common import ROOT
from build_halt_boot_cp61 import adapt, OUT
from build_fram_cp52 import replace_once as rep


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--legacy-guard',action='store_true')
    args=parser.parse_args()
    core,board=adapt()
    run=ROOT/('build/cp61-guard-rt11' if args.legacy_guard else 'build/cp61-rt11');run.mkdir(parents=True,exist_ok=True)
    base=ROOT/'../lsi11-fpga/images/rt11v503.dsk';image=run/'test.dsk'
    assert not image.exists(),'fresh RT-11 test directory required'
    build=ROOT/'build/cp61-asm';record=json.loads((build/'build-inputs.json').read_text())
    for name,digest in record['source_sha256'].items():assert sha(ROOT/name)==digest,name
    for name,digest in record['outputs'].items():assert sha(build/name)==digest,name
    shutil.copyfile(base,image)
    subprocess.run([str(ROOT/'../lsi11/rt11tool'),'add',str(image),str(build/'HBTEST.SAV'),'HBTEST.SAV'],check=True,capture_output=True)
    if args.legacy_guard:
        subprocess.run([str(ROOT/'../lsi11/rt11tool'),'add',str(image),str(ROOT/'demos/rt11/service/UJLOAD.SAV'),'UJLOAD.SAV'],check=True,capture_output=True)
    text=(ROOT/'tb/tb_board_rt11.v').read_text()
    start=text.index('    initial begin\n        trace_rk=')
    end=text.index('    always @(posedge clk) if(!reset)begin',start)
    include='tb/halt_guard_rt11_cp61.vh' if args.legacy_guard else 'tb/halt_boot_rt11_cp61.vh'
    text=text[:start]+(ROOT/include).read_text()+'\n'+text[end:]
    text=rep(text,'if(clocks>=500000000)','if(clocks>=700000000)')
    pass_line='PASS CP61 legacy loader rejected + HALT copy + DIR:' if args.legacy_guard else 'PASS CP61 two RAM RT-11 boots + HALT copy + DIR:'
    text=text.replace('PASS uJ11 cold RT-11 boot + DIR:',pass_line)
    tb=run/'tb.v';tb.write_text(text)
    sources=[str(tb.relative_to(ROOT))]+core+board+['rtl/uj11_rom.v','tb/models/ODDRXE.v',
             'reference/lsi11/spi_fram_model.v','reference/lsi11/spi_sd_model_cp28.v']
    paths=sources+['tb/tb_board_rt11.v',include,'tb/rt11/HBTEST.MAC',
                  'tools/run_halt_rt11_cp61.py','build/cp61-asm/HBTEST.SAV','build/cp61-asm/build-inputs.json',
                  'build/cp61-boot/inputs.json','build/cp61-boot/m0.mem','build/cp61-boot/decode.mem','build/cp61-boot/firmware.mem']
    if args.legacy_guard:paths+=['demos/rt11/service/UJLOAD.SAV','demos/rt11/service/UJLOAD.MAC']
    manifest=dict(files={p:sha(ROOT/p) for p in paths},base_sha256=sha(base),disk_sha256=sha(image),
                  profile=json.loads((OUT/'inputs.json').read_text()))
    (run/'inputs.json').write_text(json.dumps(manifest,indent=2)+'\n')
    for i,name in enumerate(sources):
        if name.startswith('reference/') or '/reference/' in name:
            copy=run/Path(name).name
            copy.write_text('/* verilator lint_off WIDTH */\n'+(ROOT/name).read_text()+'\n/* verilator lint_on WIDTH */\n')
            sources[i]=str(copy)
    with (run/'build.log').open('w') as log:
        subprocess.run(['verilator','--binary','--timing','--top-module','tb_board_rt11','-j','4',
                        '--Mdir',str(run/'obj')]+sources,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (run/'simulation.log').open('w') as log:
        process=subprocess.run([str(run/'obj/Vtb_board_rt11'),f'+SD_IMAGE={image}',f'+UART_LOG={run}/uart.txt'],
                               cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    text=(run/'simulation.log').read_text();print(text[-3500:]);process.check_returncode()
    assert pass_line in text
    count=1 if args.legacy_guard else 2
    uart=(run/'uart.txt').read_text();assert uart.count('!HBTEST-I-HALT COPY 512 BYTES PASS')==count and '?HBTEST' not in uart
    if args.legacy_guard:assert '?UJLOAD-E-Service instructions unavailable' in uart and 'UJLOAD> ' not in uart
    assert sha(base)==manifest['base_sha256'] and sha(image)==manifest['disk_sha256']
    for name,digest in manifest['files'].items():assert sha(ROOT/name)==digest,name
    result=dict(passed=True,cold_boots=count,copy_calls=count*4,bytes_per_run=512,resident_repaired=not args.legacy_guard,
                stored_data_survives_reset=not args.legacy_guard,legacy_loader_rejected=args.legacy_guard,actual_uart_waveform=True,
                files={p.name:sha(p) for p in (run/'inputs.json',run/'build.log',run/'simulation.log',run/'uart.txt')})
    (run/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))


if __name__=='__main__':main()
