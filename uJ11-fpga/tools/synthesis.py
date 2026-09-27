#!/usr/bin/env python3
"""Prepare or run the production HC1200 MAP/PAR/TRACE gate."""
import argparse, hashlib, json, os, re, subprocess
import xml.etree.ElementTree as ET
from board_common import ROOT, CORE, BOARD, TOP, PROFILES
from build_hardware import build
from report_synthesis import extract, check_clock


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('name',help='fresh build directory name, e.g. hc1200-check')
    p.add_argument('--prepare-only',action='store_true')
    p.add_argument('--board',choices=PROFILES,default='hc1200')
    p.add_argument('--clock-mhz',type=float,default=31.824)
    args=p.parse_args()
    if not re.fullmatch(r'[a-z][a-z0-9-]*',args.name):p.error('invalid build name')
    if args.board != 'hc1200':
        from synthesis_hc7000 import run
        return run(args.name,args.prepare_only)
    if not 31.824<=args.clock_mhz<=100:p.error('FRAM timing requires at least 31.824 MHz constraint')
    args.fram_timing=True
    out=ROOT/'build'/args.name;out.mkdir(parents=True,exist_ok=True)
    if (out/'impl1').exists():p.error('use a fresh implementation directory')
    hardware=build()
    top='uj11_hc1200_microcomp'
    sources=CORE+BOARD+[TOP,'build/hardware/uj11_m0_ebr.v']
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
# UJ11_MMU is undefined: production MMU-less profile
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
    inputs=set(sources)|set(hardware['files'])|{
        'boards/hc1200/pins.lpf','synth/machxo2/uj11-board.sty',
        'synth/machxo2/fram-timing.lpf','tools/synthesis.py','tools/board_common.py',
        'tools/report_synthesis.py','build/hardware/m0.stats.json'}
    hashes={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in sorted(inputs)}
    hashes['generated:clock.lpf']=hashlib.sha256(lpf.encode()).hexdigest()
    hashes['generated:build.tcl']=hashlib.sha256((out/'build.tcl').read_bytes()).hexdigest()
    manifest=dict(name=args.name,top=top,files=hashes,mmu=False,defines=[],
        input_revision_sha256=hashlib.sha256(json.dumps(hashes,sort_keys=True).encode()).hexdigest())
    (out/'inputs.json').write_text(json.dumps(manifest,indent=2)+'\n')
    if args.prepare_only:
        print(f'Prepared {args.name}: {len(hashes)} inputs, MMU-less, external FRAM timing enabled')
        return
    diamond=os.environ.get('DIAMOND_HOME',str(__import__('pathlib').Path.home()/'.local/lscc/diamond/3.14'))+'/bin/lin64/diamondc'
    with (out/'diamond.log').open('w') as log:
        rc=subprocess.run([diamond,str(out/'build.tcl')],cwd=ROOT,
                          env=dict(os.environ,LD_PRELOAD=os.environ.get('DIAMOND_LIBSTDCPP','/lib/x86_64-linux-gnu/libstdc++.so.6')),
                          stdout=log,stderr=subprocess.STDOUT).returncode
    prefix=out/'impl1'/f'{args.name}_impl1'
    report=dict(inputs=manifest,scope='Full production HC1200 computer',
        device='LCMXO2-1200HC-4SG32C',constraint_mhz=args.clock_mhz,expected_ebr=7,
        diamond_returncode=rc,mmu=False,microcode_words=hardware['microcode_words'],
        external_pin_delays_constrained=True)
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
