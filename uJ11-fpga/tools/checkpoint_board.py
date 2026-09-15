#!/usr/bin/env python3
"""Real HC1200 board MAP/PAR gate, including OSCH, pins and all peripherals."""
import argparse
import hashlib
import json
import os
import re
import subprocess
import xml.etree.ElementTree as ET
from board_common import ROOT, CORE, BOARD, MMU
from report_synthesis import extract, check_clock


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('name')
    p.add_argument('--mmu',action='store_true',help='Retained CP47c prototype; known not to fit HC1200')
    p.add_argument('--fram-cp52',action='store_true',help='Experimental native demand sequential FRAM READ')
    p.add_argument('--cursor-cp53',choices=['increment','compare','both'],help='CP53 native cursor mapping experiment')
    p.add_argument('--ack-cp54',choices=['dma','dma-ack'],help='CP54 native I/O qualification and ACK experiment')
    p.add_argument('--rx-cp55',action='store_true',help='CP55 native shared FRAM receive/result storage')
    p.add_argument('--spi-cp56',action='store_true',help='CP56 native 29.56 MHz SPI via ODDRXE')
    p.add_argument('--prepare-only',action='store_true',help='Write exact synthesis project and input manifest without running Diamond')
    p.add_argument('--service-cp57',action='store_true',help='CP57 opt-in service FRAM bank, frozen CP56 base')
    p.add_argument('--service-cp58',action='store_true',help='CP58 HALT fault recovery and STEP, frozen CP57e base')
    p.add_argument('--timing-cp59',action='store_true',help='CP59 remove unobservable current bus-error predicate')
    p.add_argument('--rk-cp60',action='store_true',help='CP60 RK611 RECALIBRATE and SD error recovery')
    p.add_argument('--psw-cp78',action='store_true',help='CP78 CPU-local word/byte PSW over frozen CP67b')
    p.add_argument('--modules-cp67',action='store_true',help='CP67 generic FRAM module table and cold initialization')
    p.add_argument('--debug-cp63',action='store_true',help='CP63 button and external HALT/STEP gate')
    p.add_argument('--loader-cp62',action='store_true',help='CP62 vector-called RT-11 file loader gate')
    p.add_argument('--boot-cp61',action='store_true',help='CP61 cold HALT vector and ROM-installed resident copy')
    p.add_argument('--clock-mhz',type=float,default=29.56,help='STA/PAR clock constraint; does not change the OSCH nominal frequency')
    p.add_argument('--fram-timing',action='store_true',help='Apply documented CP56 external FRAM budgets to placement/routing and TRACE')
    args=p.parse_args()
    assert 1 <= args.clock_mhz <= 100
    assert not args.fram_timing or args.clock_mhz>=31.824, 'FRAM budgets require the conservative internal clock gate'
    assert sum(bool(x) for x in (args.mmu,args.fram_cp52,args.cursor_cp53,args.ack_cp54,args.rx_cp55,args.spi_cp56,args.service_cp57,args.service_cp58,args.timing_cp59,args.rk_cp60,args.boot_cp61,args.loader_cp62,args.debug_cp63,args.modules_cp67,args.psw_cp78))<=1, 'Choose one native candidate or the retained MMU profile'
    assert re.fullmatch(r'cp[0-9]+[a-z][a-z0-9-]*',args.name)
    out=ROOT/'build'/args.name
    out.mkdir(parents=True,exist_ok=True)
    assert not (out/'impl1').exists(), 'fresh implementation directory required'
    top='uj11_hc1200_microcomp'
    core,board=CORE,BOARD
    if args.fram_cp52:
        from build_fram_cp52 import adapt
        core,board=adapt()
    if args.cursor_cp53:
        from build_cursor_cp53 import adapt
        core,board=adapt(args.cursor_cp53)
    if args.ack_cp54:
        from build_ack_cp54 import adapt
        core,board=adapt(args.ack_cp54)
    if args.rx_cp55:
        from build_rx_cp55 import adapt
        core,board=adapt()
    if args.spi_cp56:
        from build_spi_cp56 import adapt
        core,board=adapt()
    if args.service_cp57 or args.service_cp58 or args.timing_cp59 or args.rk_cp60 or args.boot_cp61 or args.loader_cp62 or args.debug_cp63 or args.modules_cp67 or args.psw_cp78:
        if args.psw_cp78:
            from build_psw_cp78 import adapt
        elif args.modules_cp67:
            from build_modules_cp67 import adapt
        elif args.debug_cp63:
            from build_debug_cp63 import adapt
        elif args.loader_cp62:
            from build_vector_loader_cp62 import adapt
        elif args.boot_cp61:
            from build_halt_boot_cp61 import adapt
        elif args.rk_cp60:
            from build_rk_recovery_cp60 import adapt
        elif args.timing_cp59:
            from build_timing_cp59 import adapt
        elif args.service_cp58:
            from build_service_cp58 import adapt
        else:
            from build_service_cp57 import adapt
        core,board=adapt()
    service_path='build/cp78-psw' if args.psw_cp78 else 'build/cp67-modules' if args.modules_cp67 else 'build/cp63-debug' if args.debug_cp63 else 'build/cp62-boot' if args.loader_cp62 else 'build/cp61-boot' if args.boot_cp61 else 'build/cp60-rk' if args.rk_cp60 else 'build/cp59-service' if args.timing_cp59 else 'build/cp58-service' if args.service_cp58 else 'build/cp57-service'
    sources=core+board+['boards/hc1200/uj11_microcomp.v','microcode/generated/uj11_m0_ebr.v']
    if args.timing_cp59 or args.rk_cp60 or args.boot_cp61 or args.loader_cp62 or args.debug_cp63 or args.modules_cp67 or args.psw_cp78:sources[-2]=service_path+'/src/boards/hc1200/uj11_microcomp.v'
    if args.service_cp57 or args.service_cp58 or args.timing_cp59 or args.rk_cp60 or args.boot_cp61 or args.loader_cp62 or args.debug_cp63 or args.modules_cp67 or args.psw_cp78:
        sources[-1]=service_path+'/uj11_m0_ebr.v'
    if args.mmu:sources+=MMU
    proj=ET.Element('BaliProject',version='3.2',title=args.name,device='LCMXO2-1200HC-4SG32C',default_implementation='impl1')
    ET.SubElement(proj,'Options')
    impl=ET.SubElement(proj,'Implementation',title='impl1',dir='impl1',synthesis='synplify',default_strategy='Strategy1')
    ET.SubElement(impl,'Options',def_top=top)
    for source in sources:
        node=ET.SubElement(impl,'Source',name=str(ROOT/source),type='Verilog',type_short='Verilog')
        ET.SubElement(node,'Options')
    # MAP insists OSCH FREQUENCY equals NOM_FREQ. Apply a tighter *timing*
    # constraint to the mapped PRF afterwards; do not reconfigure OSCH.
    lpf=(ROOT/'boards/hc1200/pins.lpf').read_text()+'\nFREQUENCY NET "clk" 29.56 MHz ;\n'
    (out/'clock.lpf').write_text(lpf)
    node=ET.SubElement(impl,'Source',name=str(out/'clock.lpf'),type='Logic Preference',type_short='LPF')
    ET.SubElement(node,'Options')
    ET.SubElement(proj,'Strategy',name='Strategy1',file=str(ROOT/'synth/machxo2/uj11-board.sty'))
    ET.ElementTree(proj).write(out/f'{args.name}.ldf',encoding='utf-8',xml_declaration=True)
    post_map=''
    if args.clock_mhz!=29.56 or args.fram_timing:
        external=''
        if args.fram_timing:
            external=f'''set fd [open "{ROOT/'synth/machxo2/fram-timing.lpf'}" r]
append pref_data "\\n" [read $fd]
close $fd
'''
        post_map=f'''set pref_path "impl1/{args.name}_impl1.prf"
set fd [open $pref_path r]
set pref_data [read $fd]
close $fd
file copy -force $pref_path mapped-original.prf
if {{[regsub -all {{FREQUENCY NET "clk" [0-9.]+ MHz ;}} $pref_data {{FREQUENCY NET "clk" {args.clock_mhz:.6f} MHz ;}} pref_data] != 1}} {{error "Expected exactly one mapped OSCH frequency"}}
{external}
set fd [open $pref_path w]
puts -nonewline $fd $pref_data
close $fd
'''
    (out/'build.tcl').write_text(f'''cd [file dirname [file normalize [info script]]]
if {{[catch {{
prj_project open {args.name}.ldf
{'prj_impl option -impl impl1 VERILOG_DIRECTIVES {UJ11_MMU}' if args.mmu else '# UJ11_MMU undefined: MMU-less production profile'}
prj_run Synthesis -impl impl1
prj_run Translate -impl impl1
prj_run Map -impl impl1
{post_map}
prj_run PAR -impl impl1
prj_run PAR -impl impl1 -task PARTrace
prj_project close
}} message]}} {{puts stderr $message; exit 1}}
exit 0
''')
    inputs=sources+['boards/hc1200/pins.lpf','synth/machxo2/uj11-board.sty','tools/checkpoint_board.py','tools/board_common.py',
                   'tools/build_firmware.py','tools/build_decode_rom.py','tools/make_ebr.py','tools/report_synthesis.py',
                   'firmware/sd_boot.asm','firmware/rk_service.asm',
                   'microcode/m0.uasm','microcode/fis.uasm','microasm/uj11asm.py','tools/link_fis.py',
                   'microcode/generated/m0.stats.json']
    if args.fram_timing:inputs+=['synth/machxo2/fram-timing.lpf']
    if args.fram_cp52:
        inputs+=['tools/build_fram_cp52.py','build/cp52-fram/inputs.json',
                 'boards/hc1200/uj11_board_bus.v','boards/hc1200/uj11_board_fram.v']
    if args.cursor_cp53:
        inputs+=['tools/build_cursor_cp53.py','tools/build_fram_cp52.py','build/cp53-cursor/inputs.json',
                 'synth/reports/cp52b/inputs.json','synth/reports/cp52b/source.tgz']
    if args.ack_cp54:
        inputs+=['tools/build_ack_cp54.py','tools/build_fram_cp52.py','build/cp54-ack/inputs.json',
                 'synth/reports/cp53a/inputs.json','synth/reports/cp53a/source.tgz']
    if args.rx_cp55:
        inputs+=['tools/build_rx_cp55.py','tools/build_fram_cp52.py','build/cp55-rx/inputs.json',
                 'synth/reports/cp54b/inputs.json','synth/reports/cp54b/source.tgz']
    if args.spi_cp56:
        inputs+=['tools/build_spi_cp56.py','tools/build_fram_cp52.py','build/cp56-spi/inputs.json',
                 'synth/reports/cp54b/inputs.json','synth/reports/cp54b/source.tgz']
    if args.service_cp57 or args.service_cp58 or args.timing_cp59 or args.rk_cp60 or args.boot_cp61 or args.loader_cp62 or args.debug_cp63 or args.modules_cp67 or args.psw_cp78:
        record=json.loads((ROOT/service_path/'inputs.json').read_text())
        inputs+=list(record['inputs'])+list(record['outputs'])+[service_path+'/inputs.json']
    hashes={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in sorted(inputs)}
    hashes['generated:clock.lpf']=hashlib.sha256(lpf.encode()).hexdigest()
    hashes['generated:build.tcl']=hashlib.sha256((out/'build.tcl').read_bytes()).hexdigest()
    manifest=dict(name=args.name,top=top,files=hashes,mmu=args.mmu,fram_cp52=args.fram_cp52,cursor_cp53=args.cursor_cp53,ack_cp54=args.ack_cp54,rx_cp55=args.rx_cp55,spi_cp56=args.spi_cp56,service_cp57=args.service_cp57,service_cp58=args.service_cp58,timing_cp59=args.timing_cp59,rk_cp60=args.rk_cp60,boot_cp61=args.boot_cp61,loader_cp62=args.loader_cp62,debug_cp63=args.debug_cp63,modules_cp67=args.modules_cp67,psw_cp78=args.psw_cp78,defines=['UJ11_MMU'] if args.mmu else [],
                  input_revision_sha256=hashlib.sha256(json.dumps(hashes,sort_keys=True).encode()).hexdigest())
    (out/'inputs.json').write_text(json.dumps(manifest,indent=2)+'\n')
    if args.prepare_only:
        print(f'Prepared {args.name}: {len(hashes)} inputs; MMU={args.mmu}, sequential FRAM={args.fram_cp52}, cursor={args.cursor_cp53}, ACK={args.ack_cp54}, RX={args.rx_cp55}, SPI={args.spi_cp56}')
        return
    diamond=os.environ.get('DIAMOND_HOME',str(__import__('pathlib').Path.home()/'.local/lscc/diamond/3.14'))+'/bin/lin64/diamondc'
    with (out/'diamond.log').open('w') as log:
        rc=subprocess.run([diamond,str(out/'build.tcl')],cwd=ROOT,
                          env=dict(os.environ,LD_PRELOAD=os.environ.get('DIAMOND_LIBSTDCPP','/lib/x86_64-linux-gnu/libstdc++.so.6')),
                          stdout=log,stderr=subprocess.STDOUT).returncode
    prefix=out/'impl1'/f'{args.name}_impl1'
    report=dict(inputs=manifest,scope='Full board: core/FIS + SPI FRAM + KL11/KW11/panel/SD/RK + bootstrap + OSCH/reset/pins; prefetch disabled',
                device='LCMXO2-1200HC-4SG32C',constraint_mhz=args.clock_mhz,expected_ebr=7 if args.mmu or args.modules_cp67 or args.psw_cp78 else 6,diamond_returncode=rc,
                mmu=args.mmu,microcode_words=json.loads((ROOT/(service_path+'/m0.stats.json' if (args.service_cp57 or args.service_cp58 or args.timing_cp59 or args.rk_cp60 or args.boot_cp61 or args.loader_cp62 or args.debug_cp63 or args.modules_cp67 or args.psw_cp78) else 'microcode/generated/m0.stats.json')).read_text())['used_words'],
                fram_cp52=args.fram_cp52,cursor_cp53=args.cursor_cp53,ack_cp54=args.ack_cp54,rx_cp55=args.rx_cp55,spi_cp56=args.spi_cp56,service_cp57=args.service_cp57,service_cp58=args.service_cp58,timing_cp59=args.timing_cp59,rk_cp60=args.rk_cp60,boot_cp61=args.boot_cp61,loader_cp62=args.loader_cp62,debug_cp63=args.debug_cp63,modules_cp67=args.modules_cp67,psw_cp78=args.psw_cp78,
                external_pin_delays_constrained=args.fram_timing)
    try:
        timing_text=prefix.with_suffix('.twr').read_text()
        report.update(check_clock(extract(prefix.with_suffix('.mrp').read_text(),timing_text,prefix.with_suffix('.par').read_text()),timing_text,args.clock_mhz))
    except (OSError,ValueError) as error:
        report.update(timing_pass=False,fully_routed=False,incomplete_reason=str(error))
        if prefix.with_suffix('.mrp').exists():
            mapped=prefix.with_suffix('.mrp').read_text()
            for key,label in [('lut4','LUT4s'),('ff','registers'),('ebr','block RAMs'),('slices','SLICEs')]:
                match=re.search(r'Number of '+label+r':\s+(\d+)\s+out of\s+(\d+)',mapped)
                if match:report[key]=int(match[1])
    report['reports']={s:hashlib.sha256(prefix.with_suffix(s).read_bytes()).hexdigest()
                       for s in ('.srr','.areasrr','.mrp','.par','.twr') if prefix.with_suffix(s).exists()}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ('inputs','reports')},indent=2))
    if rc or not report['timing_pass'] or report.get('ebr')!=report['expected_ebr']:raise SystemExit('Board gate failed: preserve raw reports and inspect area/timing.')


if __name__=='__main__':main()
