#!/usr/bin/env python3
"""Run an isolated named HC1200 resource gate. Never programs the board."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET
from report_synthesis import extract

CONFIGS = {
    'cp2': ('uj11_probe_datapath', 0, [
        'rtl/uj11_alu.v','rtl/uj11_regfile.v','rtl/uj11_datapath.v',
        'synth/machxo2/uj11_probe_datapath.v']),
    'cp3': ('uj11_probe_engine', 4, [
        'rtl/uj11_alu.v','rtl/uj11_regfile.v','rtl/uj11_datapath.v',
        'rtl/uj11_microseq.v','rtl/uj11_psw.v','rtl/uj11_mem.v','rtl/uj11_engine.v',
        'microcode/generated/uj11_m0_ebr.v','synth/machxo2/uj11_probe_engine.v']),
    'cp4': ('uj11_probe_decode', 0, ['rtl/uj11_decode.v','synth/machxo2/uj11_probe_decode.v']),
    'cp5': ('uj11_probe_core', 4, [
        'rtl/uj11_alu.v','rtl/uj11_regfile.v','rtl/uj11_datapath.v',
        'rtl/uj11_microseq.v','rtl/uj11_psw.v','rtl/uj11_mem.v','rtl/uj11_engine.v',
        'rtl/uj11_decode.v','rtl/uj11_core.v',
        'microcode/generated/uj11_m0_ebr.v','synth/machxo2/uj11_probe_core.v']),
}
CONFIGS['cp6a'] = ('uj11_probe_fram', 4, CONFIGS['cp5'][2][:-1] + [
    'reference/lsi11/spi_fram_guest_ram.v','rtl/uj11_fram_baseline.v',
    'rtl/uj11_fram_system.v','rtl/uj11_stream.v','synth/machxo2/uj11_probe_fram.v'])
for suffix, top in [('b','uj11_probe_fram_seq'),('c','uj11_probe_fram_pf')]:
    CONFIGS['cp6'+suffix] = (top, 4, CONFIGS['cp6a'][2] + [
        'rtl/uj11_fram_transport.v','rtl/uj11_prefetch.v','rtl/uj11_prefetch_control.v','synth/machxo2/'+top+'.v'])
CONFIGS['cp7a']=CONFIGS['cp6c']
CONFIGS['cp7b']=CONFIGS['cp5']
CONFIGS['cp7c']=CONFIGS['cp6c']
CONFIGS['cp7d']=CONFIGS['cp6c']
CONFIGS['cp7e']=CONFIGS['cp5']
CONFIGS['cp8a']=CONFIGS['cp5']
CONFIGS['cp8b']=CONFIGS['cp6c']
CONFIGS['cp8c']=CONFIGS['cp5']
CONFIGS['cp8d']=CONFIGS['cp6c']
CONFIGS['cp9a']=CONFIGS['cp5']
CONFIGS['cp9b']=CONFIGS['cp6c']
CONFIGS['cp9c']=CONFIGS['cp5']
CONFIGS['cp9d']=CONFIGS['cp6c']
CONFIGS['cp9e']=CONFIGS['cp5']
CONFIGS['cp9f']=CONFIGS['cp6c']
CONFIGS['cp10a']=CONFIGS['cp2']
CONFIGS['cp10b']=CONFIGS['cp5']
CONFIGS['cp10c']=CONFIGS['cp6c']
CONFIGS['cp10d']=CONFIGS['cp5']
CONFIGS['cp10e']=CONFIGS['cp6c']
CONFIGS['cp10f']=CONFIGS['cp5']
CONFIGS['cp10g']=CONFIGS['cp6c']
CONFIGS['cp10h']=CONFIGS['cp5']
CONFIGS['cp10i']=CONFIGS['cp6c']
CONFIGS['cp10j']=CONFIGS['cp5']
CONFIGS['cp10k']=CONFIGS['cp6c']
CONFIGS['cp11a']=CONFIGS['cp5']
CONFIGS['cp11b']=CONFIGS['cp6c']
CONFIGS['cp11c']=CONFIGS['cp5']
CONFIGS['cp11d']=CONFIGS['cp6c']
CONFIGS['cp11e']=CONFIGS['cp5']
CONFIGS['cp11f']=CONFIGS['cp6c']
CONFIGS['cp12a']=CONFIGS['cp5']
CONFIGS['cp12b']=CONFIGS['cp6c']
CONFIGS['cp12c']=CONFIGS['cp5']
CONFIGS['cp12d']=CONFIGS['cp6c']
CONFIGS['cp13a']=CONFIGS['cp5']
CONFIGS['cp13b']=CONFIGS['cp6c']
CONFIGS['cp14a']=CONFIGS['cp5']
CONFIGS['cp14b']=CONFIGS['cp6c']
CONFIGS['cp14c']=('uj11_probe_fram_irq',4,CONFIGS['cp6c'][2][:-1]+['rtl/uj11_irq_lsi11.v','synth/machxo2/uj11_probe_fram_irq.v'])
CONFIGS['cp14d']=CONFIGS['cp5']
CONFIGS['cp15a']=CONFIGS['cp5']
CONFIGS['cp15b']=CONFIGS['cp14c']
CONFIGS['cp15c']=CONFIGS['cp5']
CONFIGS['cp15d']=CONFIGS['cp14c']
CONFIGS['cp15e']=CONFIGS['cp5']
CONFIGS['cp15f']=CONFIGS['cp14c']
CONFIGS['cp16a']=CONFIGS['cp5']
CONFIGS['cp16b']=CONFIGS['cp14c']
CONFIGS['cp16c']=CONFIGS['cp5']
CONFIGS['cp16d']=CONFIGS['cp14c']
CONFIGS['cp16e']=CONFIGS['cp5']
CONFIGS['cp16f']=CONFIGS['cp14c']
CONFIGS['cp17a']=CONFIGS['cp5']
CONFIGS['cp17b']=CONFIGS['cp14c']
CONFIGS['cp18a']=CONFIGS['cp5']
CONFIGS['cp18b']=CONFIGS['cp14c']
CONFIGS['cp18c']=CONFIGS['cp5']
CONFIGS['cp18d']=CONFIGS['cp14c']
CONFIGS['cp18e']=CONFIGS['cp5']
CONFIGS['cp18f']=CONFIGS['cp14c']
CONFIGS['cp19a']=CONFIGS['cp5']
CONFIGS['cp19b']=CONFIGS['cp14c']
CONFIGS['cp20a']=CONFIGS['cp5']
CONFIGS['cp20b']=CONFIGS['cp14c']
CONFIGS['cp20c']=CONFIGS['cp5']
CONFIGS['cp20d']=CONFIGS['cp14c']
CONFIGS['cp21a']=CONFIGS['cp5']
CONFIGS['cp21b']=CONFIGS['cp14c']
CONFIGS['cp22a']=CONFIGS['cp5']
CONFIGS['cp22b']=CONFIGS['cp14c']
CONFIGS['cp22c']=CONFIGS['cp5']
CONFIGS['cp22d']=CONFIGS['cp14c']
CONFIGS['cp23a']=CONFIGS['cp5']
CONFIGS['cp23b']=CONFIGS['cp14c']
CONFIGS['cp23c']=CONFIGS['cp5']
CONFIGS['cp23d']=CONFIGS['cp14c']
CONFIGS['cp23e']=CONFIGS['cp5']
CONFIGS['cp23f']=CONFIGS['cp14c']
CONFIGS['cp24a']=CONFIGS['cp5']
CONFIGS['cp24b']=CONFIGS['cp14c']
CONFIGS['cp24c']=CONFIGS['cp5']
CONFIGS['cp24d']=CONFIGS['cp14c']
CONFIGS['cp25a']=CONFIGS['cp5']
CONFIGS['cp25b']=CONFIGS['cp14c']
CONFIGS['cp25c']=CONFIGS['cp5']
CONFIGS['cp25d']=CONFIGS['cp14c']
CONFIGS['cp26a']=CONFIGS['cp5']
CONFIGS['cp26b']=CONFIGS['cp14c']
CONFIGS['cp27a']=CONFIGS['cp5']
CONFIGS['cp27b']=CONFIGS['cp14c']
CONFIGS['cp31b']=('uj11_probe_mmu18',0,[
    'rtl/uj11_mmu_translate18.v','synth/machxo2/uj11_probe_mmu18.v'])
CONFIGS['cp31d']=CONFIGS['cp31b']


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('name',choices=CONFIGS)
    p.add_argument('--mhz',type=float,default=50,help='Explicit internal clock constraint; default 50 MHz')
    args=p.parse_args()
    root=Path(__file__).resolve().parents[1]
    name=args.name
    top,ebr,sources=CONFIGS[name]
    out=root/'build'/name
    out.mkdir(parents=True,exist_ok=True)
    if not 0 < args.mhz <= 200:
        p.error('--mhz must be in (0, 200]')
    if (out/'impl1').exists():
        p.exit(1,f'{out}/impl1 exists: use a fresh source copy for each measurement.\n')
    proj=ET.Element('BaliProject',version='3.2',title=name,device='LCMXO2-1200HC-4SG32C',default_implementation='impl1')
    ET.SubElement(proj,'Options')
    impl=ET.SubElement(proj,'Implementation',title='impl1',dir='impl1',synthesis='synplify',default_strategy='Strategy1')
    ET.SubElement(impl,'Options',def_top=top)
    for source in sources:
        node=ET.SubElement(impl,'Source',name=str(root/source),type='Verilog',type_short='Verilog')
        ET.SubElement(node,'Options')
    clock_lpf,replacements=re.subn(r'(FREQUENCY PORT "clk" )[0-9.]+( MHz)',
                                  rf'\g<1>{args.mhz:g}\2',
                                  (root/'synth/machxo2/uj11.lpf').read_text())
    if replacements!=1:
        p.exit(1,'Expected exactly one clock constraint in input LPF.\n')
    (out/'clock.lpf').write_text(clock_lpf)
    node=ET.SubElement(impl,'Source',name=str(out/'clock.lpf'),type='Logic Preference',type_short='LPF')
    ET.SubElement(node,'Options')
    ET.SubElement(proj,'Strategy',name='Strategy1',file=str(root/'synth/machxo2/uj11.sty'))
    ET.ElementTree(proj).write(out/f'{name}.ldf',encoding='utf-8',xml_declaration=True)
    (out/'build.tcl').write_text(f'''cd [file dirname [file normalize [info script]]]
if {{[catch {{
    prj_project open {name}.ldf
    prj_run Synthesis -impl impl1
    prj_run Translate -impl impl1
    prj_run Map -impl impl1
    prj_run PAR -impl impl1
    prj_run PAR -impl impl1 -task PARTrace
    prj_project close
}} message]}} {{ puts stderr $message; exit 1 }}
exit 0
''')
    inputs=sorted(set(sources+['synth/machxo2/uj11.lpf','synth/machxo2/uj11.sty',
                               'tools/checkpoint.py','tools/report_synthesis.py']+
                              (['microcode/m0.uasm','microcode/fis.uasm','microasm/uj11asm.py',
                                'tools/link_fis.py','tools/make_ebr.py'] if ebr else [])))
    hashes={n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in inputs}
    hashes['generated:clock.lpf']=hashlib.sha256(clock_lpf.encode()).hexdigest()
    manifest={'name':name,'top':top,'files':hashes,
              'input_revision_sha256':hashlib.sha256(json.dumps(hashes,sort_keys=True).encode()).hexdigest()}
    (out/'inputs.json').write_text(json.dumps(manifest,indent=2)+'\n')
    diamond=Path(os.environ.get('DIAMOND_HOME',str(Path.home()/'.local/lscc/diamond/3.14')))/'bin/lin64/diamondc'
    env=dict(os.environ,LD_PRELOAD=os.environ.get('DIAMOND_LIBSTDCPP','/lib/x86_64-linux-gnu/libstdc++.so.6'))
    with (out/'diamond.log').open('w') as log:
        rc=subprocess.run([str(diamond),str(out/'build.tcl')],env=env,stdout=log,stderr=subprocess.STDOUT).returncode
    prefix=out/'impl1'/f'{name}_impl1'
    try:
        report=extract(prefix.with_suffix('.mrp').read_text(),prefix.with_suffix('.twr').read_text(),prefix.with_suffix('.par').read_text())
    except (OSError,ValueError) as e:
        p.exit(1,f'Diamond rc={rc}: {e}; inspect {out}/diamond.log\n')
    trace=prefix.with_suffix('.twr').read_text()
    reported_clock=re.search(r'FREQUENCY PORT "clk" ([0-9.]+) MHz',trace)
    if not reported_clock or float(reported_clock[1])!=args.mhz:
        p.exit(1,'TRACE clock preference does not match requested constraint.\n')
    report.update({'inputs':manifest,'scope':top,'device':'LCMXO2-1200HC-4SG32C',
        'expected_ebr':ebr,'constraint_mhz':args.mhz,'diamond_returncode':rc,
        'reports':{s:hashlib.sha256(prefix.with_suffix(s).read_bytes()).hexdigest() for s in ['.mrp','.par','.twr','.srr']}})
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ['lut4','ff','ebr','fmax_mhz','timing_pass']},indent=2))
    if rc or not report['timing_pass'] or report['ebr']!=ebr:
        p.exit(1,'Gate failed; preserve reports and investigate.\n')


if __name__=='__main__': main()
