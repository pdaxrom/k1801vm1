#!/usr/bin/env python3
"""CP56 post-route timing audit plus oscillator duty/jitter protocol tests."""
import hashlib
import json
import re
import subprocess
from pathlib import Path
from board_common import ROOT
from build_fram_cp52 import replace_once
from audit_edif_cp53 import parse, child, children
import run_fram_cp52 as runner


def sections(text):
    reports = {}
    for part in re.split(r'^Preference:',text,flags=re.M)[1:]:
        header=part.split(';')[0]
        if not header.strip().startswith(('FREQUENCY NET','INPUT_SETUP PORT','CLOCK_TO_OUT PORT')): continue
        key='frequency' if 'FREQUENCY' in header else re.search(r'PORT "([^"]+)"',header)[1]
        passed=re.findall(r'following path meets requirements by ([\d.]+)ns',part)
        scored=re.search(r'(\d+) items? scored, (\d+) timing errors detected',part)
        assert scored and passed,(key,part[:300])
        reports.setdefault(key,[]).append(dict(items=int(scored[1]),errors=int(scored[2]),
            worst_slack_ns=min(map(float,passed))))
    return reports


def main():
    timing=ROOT/'build/cp56-timing';out=ROOT/'build/cp56-clock-tests'
    out.mkdir(exist_ok=True);runner.OUT=out
    text=(timing/'budget.twr').read_text()
    assert 'Timing errors: 0 (setup), 0 (hold)' in text
    budgets=sections(text)
    assert set(budgets)=={'frequency','gpio_miso','gpio_msck','gpio_mosi','gpio_mcs'}
    assert all(len(rows)==2 and all(r['errors']==0 for r in rows) for rows in budgets.values())
    assert [r['items'] for r in budgets['gpio_miso']]==[3,3]
    assert 'PRIMARY "clk"' in (ROOT/'synth/reports/cp56a/design.par').read_text()
    # SDF export, not an SDF-annotated simulation. Check both ODDRXE and
    # output buffer paths and interconnect for modeled rise/fall asymmetry.
    sdf_paths={}
    for kind in ('slow','fast'):
        tree=parse((timing/(kind+'.sdf')).read_text());picked=[]
        for cell in children(tree,'CELL'):
            inst=child(cell,'INSTANCE')
            name=inst[1] if len(inst)>1 else ''
            if name in ('gpio_msck_I','gpio_msck_MGIOL'):
                for delay in children(cell,'DELAY'):
                    for absolute in children(delay,'ABSOLUTE'):
                        for path in children(absolute,'IOPATH'):
                            assert path[-1]==path[-2],(kind,name,path)
                            picked.append(dict(instance=name,arc=path))
        assert len(picked)==2
        interconnect=[line.strip() for line in (timing/(kind+'.sdf')).read_text().splitlines()
                      if 'INTERCONNECT' in line and ('gpio_msck_MGIOL/CLK' in line or 'gpio_msck_I/IOLDO' in line)]
        assert len(interconnect)==2,interconnect
        for line in interconnect:
            triples=re.findall(r'\((\d+:\d+:\d+)\)',line)
            assert len(triples)==2 and triples[0]==triples[1],line
        sdf_paths[kind]=dict(arcs=picked,interconnect=interconnect)
    # Keep archived CP56 fixtures unchanged. This extra fixture varies duty
    # and shortens one half by the full conservative 2% period-jitter bound.
    fixture=(ROOT/'tb/tb_spi_cp56.v').read_text()
    fixture=replace_once(fixture,'parameter real RETURN_NS=1.0, VALID_NS=13.0',
        'parameter real RETURN_NS=1.0, VALID_NS=13.0, HIGH_NS=HALF_NS, LOW_NS=HALF_NS')
    fixture=replace_once(fixture,'    always #(HALF_NS) clk=~clk;',
        '    initial forever begin #(LOW_NS) clk=1; #(HIGH_NS) clk=0; end')
    fixture=replace_once(fixture,'    initial begin\n        for(i=0;',
        '    initial begin\n        $display("CP56 oscillator high=%0.5f low=%0.5f ns",HIGH_NS,LOW_NS);\n        for(i=0;')
    fixture=replace_once(fixture,'half=%0.5f ns SCK/MOSI/CS=',
        'CPU high=%0.5f low=%0.5f ns SCK/MOSI/CS=')
    fixture=replace_once(fixture,'beats,HALF_NS,SCK_NS,DATA_NS',
        'beats,HIGH_NS,LOW_NS,SCK_NS,DATA_NS')
    (out/'tb_clock.v').write_text(fixture)
    period=1000/(29.56*1.055)
    short=period*(.43-.02);long=period*.57
    runs=[]
    # Routed output delays are relative to the oscillator. The protocol
    # fixture has a common ideal CPU clock. It exercises pin phase; the
    # three actual MISO capture paths and hold checks are covered by TRACE.
    corners={'slow':dict(SCK_NS=7.798,DATA_NS=7.121,CS_NS=8.334,RETURN_NS=2),
             'fast':dict(SCK_NS=2.219,DATA_NS=2.182,CS_NS=2.454,RETURN_NS=2)}
    sources=['build/cp56-clock-tests/tb_clock.v','build/cp56-spi/fast/uj11_board_fram.v',
        'reference/lsi11/spi_fram_model.v','tb/models/ODDRXE.v']
    for corner,params in corners.items():
        for high_short in (True,False):
            values=dict(params,HIGH_NS=short if high_short else long,LOW_NS=long if high_short else short)
            flags=['-Wall']+[f'-Ptb_spi_cp56.{k}={v:.9f}' for k,v in values.items()]
            runs.append(runner.simulate('tb_spi_cp56',corner+('-high' if high_short else '-low'),sources,flags))
    # Demonstrate that an additional 0.2ns pulse-width distortion consumes
    # the 0.147ns modeled margin, without modifying the CPU/FRAM RTL.
    negative=replace_once(fixture,'assign #(SCK_NS) pin_sck=sck;',
        'assign #(SCK_NS+0.2,SCK_NS) pin_sck=sck;')
    (out/'tb_narrow.v').write_text(negative)
    command=['iverilog','-g2012','-Wall','-s','tb_spi_cp56','-o',str(out/'narrow'),
        f'-Ptb_spi_cp56.HIGH_NS={long:.9f}',f'-Ptb_spi_cp56.LOW_NS={short:.9f}',
        'build/cp56-clock-tests/tb_narrow.v']+sources[1:]
    with (out/'narrow-build.log').open('w') as log:subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'narrow.log').open('w') as log:
        rc=subprocess.run(['vvp',str(out/'narrow')],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT).returncode
    assert rc!=0 and 'SCK high timing' in (out/'narrow.log').read_text()
    for path in out.glob('*build.log'):
        assert not any(x in path.read_text().lower() for x in ('warning','error','sorry')),path
    inputs=['tools/check_timing_cp56.py','tools/run_fram_cp52.py','tools/build_fram_cp52.py',
        'tools/audit_edif_cp53.py','tools/board_common.py','tb/tb_spi_cp56.v']
    inputs += [str(p.relative_to(ROOT)) for p in timing.iterdir() if p.is_file()]
    inputs += [str(p.relative_to(ROOT)) for p in out.glob('*.v')]
    inputs += sources[1:]
    result=dict(trace_budget_pass=True,trace=budgets,oscillator_nominal_mhz=29.56,
        commercial_frequency_factor=1.055,conservative_period_jitter_fraction=.02,
        min_duty=.43,shortest_modeled_pulse_ns=short,min_fram_pulse_ns=13,
        pulse_margin_ns=short-13,sdf_equal_rise_fall_paths=sdf_paths,
        sdf_simulation_performed=False,pin_model_tests=runs,
        pulse_distortion_negative_control=dict(extra_rise_delay_ns=.2,returncode=rc,detected='SCK high timing'),
        assumptions=['External SCK+MISO flight time <=2 ns for MISO setup; >=0 ns for hold.',
            'Board MOSI/CS to SCK skew <=2 ns.',
            'Exported SDF models equal rise/fall delays; no analog PCB pulse distortion is included.',
            'No hardware scope measurements. Only 0.147 ns modeled pulse-width margin remains at the conservative oscillator corner.'],
        physical_pin_timing_signoff=False,board_programmed=False,
        inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},
        logs_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in out.glob('*.log')})
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS CP56 TRACE and clock-corner tests; narrow-pulse control rejected; physical pulse-width signoff pending')


if __name__=='__main__': main()
