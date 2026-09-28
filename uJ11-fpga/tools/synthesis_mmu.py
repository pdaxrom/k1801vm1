"""HC7000 SRAM MAP/PAR/TRACE; keeps the HC1200 timing gate independent."""
import hashlib
import json
import os
import re
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path
from board_common import ROOT
from build_mmu_board import build as hardware,CORE,BOARD,TOP
from build_mmu import OUT as HW,clock_mhz
from report_synthesis import extract, check_clock
from build_iop_mmu import sources as iop_sources, OUT as IOP_OUT


def run(name,prepare_only=False):
    out=ROOT/'build'/name
    out.mkdir(parents=True,exist_ok=True)
    if (out/'impl1').exists():raise ValueError('Use a fresh implementation directory')
    hw=hardware();mhz=clock_mhz()
    core,board,top=CORE,BOARD,TOP
    inventory=core+board+[top,'boards/hc7000/uj11_pll.v','boards/hc7000/mmu/uj11_pll50.v']
    device='LCMXO2-7000HC-4TG144C'
    proj=ET.Element('BaliProject',version='3.2',title=name,device=device,default_implementation='impl1')
    ET.SubElement(proj,'Options')
    impl=ET.SubElement(proj,'Implementation',title='impl1',dir='impl1',synthesis='synplify',default_strategy='Strategy1')
    ET.SubElement(impl,'Options',def_top='uj11_mmu_microcomp')
    for path in inventory:
        node=ET.SubElement(impl,'Source',name=str(ROOT/path),type='Verilog',type_short='Verilog')
        ET.SubElement(node,'Options')
    sram_lpf='boards/hc7000/mmu/sram-timing-50.lpf' if mhz==50 else 'boards/hc7000/sram-timing.lpf'
    lpf=(ROOT/'boards/hc7000/pins.lpf').read_text()+'\n'+(ROOT/sram_lpf).read_text()
    lpf,count=re.subn(r'FREQUENCY NET "clk" 24 MHz',f'FREQUENCY NET "clk" {mhz} MHz',lpf)
    assert count==1,'Expected one system-clock constraint'
    (out/'clock.lpf').write_text(lpf)
    node=ET.SubElement(impl,'Source',name=str(out/'clock.lpf'),type='Logic Preference',type_short='LPF')
    ET.SubElement(node,'Options')
    strategy=ROOT/'boards/hc7000/synthesis.sty'
    generated=['clock.lpf','build.tcl']
    if mhz==50:
        st=ET.parse(strategy)
        for prop in st.getroot().findall('Property'):
            if prop.attrib['name']=='PROP_SYN_EdfFrequency':prop.set('value',str(mhz))
            if prop.attrib['name']=='PROP_SYN_EdfArea':prop.set('value','False')
        # Routing variation matters near 20 ns on this device. Try a bounded
        # set of placements and keep the best fully checked result.
        for key,value in [('PROP_PAR_PlcIterParDes','5'),('PROP_PAR_SaveBestRsltParDes','1'),
                          ('PROP_PAR_StopZero','True'),('PROP_PAR_MultiSeedSortMode','Worst Slack')]:
            ET.SubElement(st.getroot(),'Property',name=key,value=value,time='0')
        strategy=out/'strategy.sty';st.write(strategy,encoding='utf-8',xml_declaration=True)
        generated.append('strategy.sty')
    ET.SubElement(proj,'Strategy',name='Strategy1',file=str(strategy))
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
}} message]}} {{puts stderr $message; exit 1}}
exit 0
''')
    inputs=set(inventory)|set(hw['files'])|set(hw['cpu']['files'])|{
        'boards/hc7000/pins.lpf',sram_lpf,'boards/hc7000/synthesis.sty',
        'tools/synthesis_mmu.py','tools/board_common.py','tools/build_mmu_board.py',
        'tools/report_synthesis.py'}
    inputs.update(iop_sources())
    inputs.update(str(IOP_OUT.relative_to(ROOT)/p) for p in
                  ('build.json','firmware.bin','firmware.mem','uj11_mmu_iop_ram.v','uj11_sector_ram.v'))
    inputs.add('vendor/serv/UPSTREAM.json')
    hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sorted(inputs)}
    for p in generated:
        hashes['generated:'+p]=hashlib.sha256((out/p).read_bytes()).hexdigest()
    manifest=dict(name=name,board='hc7000-lcd-sram',top='uj11_mmu_microcomp',files=hashes,
        mmu=True,fpp=hw['fpp'],clock_mhz=mhz,pipeline=hw['cpu']['pipeline'],defines=[],input_revision_sha256=hashlib.sha256(json.dumps(hashes,sort_keys=True).encode()).hexdigest())
    (out/'inputs.json').write_text(json.dumps(manifest,indent=2)+'\n')
    if prepare_only:
        print(f'Prepared {name}: {device}, 12 MHz reference / {mhz} MHz PLL, SRAM')
        return
    diamond=Path(os.environ.get('DIAMOND_HOME',str(Path.home()/'.local/lscc/diamond/3.14')))/'bin/lin64/diamondc'
    with (out/'diamond.log').open('w') as log:
        rc=subprocess.run([str(diamond),str(out/'build.tcl')],cwd=ROOT,
            env=dict(os.environ,LD_PRELOAD=os.environ.get('DIAMOND_LIBSTDCPP','/lib/x86_64-linux-gnu/libstdc++.so.6')),
            stdout=log,stderr=subprocess.STDOUT).returncode
    prefix=out/'impl1'/f'{name}_impl1'
    report=dict(inputs=manifest,scope='MMU profile; physical-board qualification pending',device=device,fpp=hw['fpp'],
        constraint_mhz=mhz,input_clock_mhz=12,expected_ebr=hw['cpu']['microcode_ebr']+4+hw['iop']['ebr']+int(hw['fp_arithmetic'])+int(hw['iop']['profile']=='storage'),diamond_returncode=rc,
        mmu=True,microcode_words=hw['cpu']['microcode_words'],external_pin_delays_constrained=True)
    try:
        timing=prefix.with_suffix('.twr').read_text()
        report.update(check_clock(extract(prefix.with_suffix('.mrp').read_text(),timing,
            prefix.with_suffix('.par').read_text(),clock='clk'),timing,mhz))
        for port,kind in [('sram_data[*]','INPUT_SETUP')]+[(p,'CLOCK_TO_OUT') for p in
                ('sram_address[*]','sram_data[*]','sram_ce_n','sram_oe_n','sram_we_n','sram_lb_n','sram_ub_n')]:
            scored=re.findall(r'Preference:\s+'+kind+r' PORT "'+re.escape(port)+
                r'"[^;]+;\s+(\d+) items? scored',timing)
            if not scored or any(int(n)==0 for n in scored):
                raise ValueError(f'No scored SRAM timing paths: {kind} {port}')
    except (OSError,ValueError) as error:
        report.update(timing_pass=False,fully_routed=False,incomplete_reason=str(error))
    report['reports']={s:hashlib.sha256(prefix.with_suffix(s).read_bytes()).hexdigest()
        for s in ('.srr','.areasrr','.mrp','.par','.twr') if prefix.with_suffix(s).exists()}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ('inputs','reports')},indent=2))
    if rc or not report['timing_pass'] or report.get('ebr')!=report['expected_ebr']:
        raise SystemExit('HC7000 gate failed; inspect preserved reports')

if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('name')
    parser.add_argument('--prepare-only',action='store_true')
    args=parser.parse_args()
    if not re.fullmatch(r'[a-z][a-z0-9-]*',args.name):parser.error('invalid build name')
    run(args.name,args.prepare_only)
