#!/usr/bin/env python3
"""Audit CP23 only after formal equivalence and exact CP22 cycle parity."""
import csv,gzip,io,json,re,shutil,tarfile
from pathlib import Path
from record_ea import ROOT,read_json,write_json,sha256,require
from verify_cp23 import VENDOR,STEMS,SUITES
FINAL=['cp23c','cp23d']
LOGS=['cp23-tests.log','cp23-core-tests.log','cp23-formal-driver.log','cp23-benchmarks-portable.log','cp23-vendor-prepare.log']+['cp23-vendor-'+n+'.log' for n in VENDOR]

def main():
 build=ROOT/'build';reports=ROOT/'tb/reports'
 baseline=read_json(ROOT/'docs/verification-cp22.json')
 baseline_bench=read_json(ROOT/'docs/benchmarks-cp22.json')
 oldcycles=baseline_bench['differential_cycles']
 require(set(oldcycles)==set(SUITES),'suite inventory changed')
 require(not list((ROOT/'rtl').glob('*mmu*')),'unexpected MMU RTL')
 stats=read_json(ROOT/'microcode/generated/m0.stats.json')
 require((stats['encoding_version'],stats['used_words'],stats['word_bits'])==(11,592,36),'microstore changed')
 # Only the sequencer implementation changes. Guard every executable ISA,
 # memory, test-fixture and emulator source against the accepted checkpoint.
 unchanged_prefixes=('rtl/','microcode/','microasm/','reference/lsi11/','tb/','../core/','../tests/')
 for n,h in baseline['files'].items():
  if n.startswith(unchanged_prefixes) and not n.startswith('tb/reports/') and n!='rtl/uj11_microseq.v':require(sha256((ROOT/n).read_bytes())==h,'unintended source change '+n)
 require({str(p.relative_to(ROOT)) for p in (ROOT/'rtl').glob('*.v')}=={n for n in baseline['files'] if n.startswith('rtl/') and n.endswith('.v')},'RTL file inventory')
 with tarfile.open(ROOT/'synth/reports/cp22c/source.tgz') as a:old_seq=a.extractfile('rtl/uj11_microseq.v').read()
 new_seq=(ROOT/'rtl/uj11_microseq.v').read_bytes()
 require(old_seq.split(b'    always @(posedge clk)',1)[1]==new_seq.split(b'    always @(posedge clk)',1)[1],'sequencer state update block changed')
 proof=read_json(build/'cp23-seq-equivalence.json')
 require(proof['baseline_sha256']==sha256(old_seq) and proof['candidate_sha256']==sha256(new_seq),'proof source hashes')
 for kind in ['positive','negative']:
  data=(build/f'cp23-seq-equivalence-{kind}.log').read_bytes();text=data.decode()
  require(sha256(data)==proof['results'][kind]['log_sha256'],'proof log hash')
  states=re.findall(r'Of those cells (\d+) are proven and (\d+) are unproven\.',text)
  require(states,'no formal proof status')
  if kind=='positive':require(states[-1]==('74','0') and proof['results'][kind]['returncode']==0 and 'Equivalence successfully proven!' in text,'positive proof')
  else:require(states[-1]==('73','1') and proof['results'][kind]['returncode']!=0 and 'ERROR: Found 1 unproven $equiv cells' in text,'fault-vector negative control')
  shutil.copyfile(build/f'cp23-seq-equivalence-{kind}.log',reports/f'cp23-seq-equivalence-{kind}.log')
 shutil.copyfile(build/'cp23-seq-equivalence.json',reports/'cp23-seq-equivalence.json')
 logs={n:(build/n).read_text() for n in LOGS}
 for n,t in logs.items():require(t and not any(x in t for x in ['FATAL','FAILED','Traceback','%Error','%Warning']),n)
 portable=logs['cp23-tests.log'];vendor='\n'.join(logs['cp23-vendor-'+n+'.log'] for n in VENDOR)
 require('All core instruction tests passed' in logs['cp23-core-tests.log'],'C core regression')
 for text in [portable,vendor]:
  for suite in SUITES:
   display={'bus-fault':'bus fault','psw-transfer-fault':'PSW-transfer fault','eis-ashc-fault':'ASHC fault','eis-ash-fault':'ASH fault'}.get(suite,suite)
   for mode,label in [(-1,'ram'),(2,'fram')]:require(f"PASS {display} differential mode{mode}: {oldcycles[suite][label]['cases']}" in text,suite+': missing tests')
  for marker in ['PASS differential: 12928','PASS FRAM differential: 12928','PASS stream engine: opcode+3','PASS EA directed: 14','PASS single directed: 46','PASS byte I/O: 23','PASS single byte directed: 46','PASS control directed: 58','PASS extra directed: 36','PASS trap directed: 112','PASS IRQ directed: 104','PASS legacy IRQ: 5 scenarios, 6 settled-state checks','PASS illegal directed: 96','PASS bus fault system: 32','PASS bus double fault: 96','PASS trace system: 24','PASS trace faults: 140','PASS system control legacy: 3 scenarios','PASS system control faults: 64']:require(marker in text,marker)
 for marker in ['Ran 16 tests','4096 pair checks','PASS prefetch: 525','PASS lsi11 peripherals: 19','exactly 57456 supported encodings','PASS byte ALU: 2097152','PASS byte datapath: 16384','PASS IRQ adapter: 763','PASS fault IRQ oracle: 8']:require(marker in portable,marker)
 # Verify every archived fit, including rejected experiments, and enforce
 # that the current source is precisely the selected measured implementation.
 archives=[];raw=0;selected={}
 for folder in sorted((ROOT/'synth/reports').iterdir()):
  info=read_json(folder/'inputs.json')
  with tarfile.open(folder/'source.tgz') as a:
   members={m.name.removeprefix('./'):m for m in a.getmembers()}
   for n,h in info['files'].items():
    data=(folder/n.split(':',1)[1]).read_bytes() if n.startswith('generated:') else a.extractfile(members[n]).read()
    require(sha256(data)==h,f'archive {folder.name}/{n}')
    if folder.name in FINAL and not n.startswith('generated:'):require(sha256((ROOT/n).read_bytes())==h,'current fit input '+n)
  if 'placement' in info:require(sha256((folder/'design.ncd').read_bytes())==info['placement']['ncd_sha256'],'placement')
  if (folder/'result.json').exists():
   result=read_json(folder/'result.json')
   for n,h in result.get('reports',{}).items():require(sha256((folder/('design'+(Path(n).suffix or n))).read_bytes())==h,'raw report');raw+=1
   if folder.name in FINAL:
    require(result['timing_pass'] and result['fully_routed'] and result['ebr']==4,'selected fit')
    selected[folder.name]={k:result[k] for k in ['lut4','ff','ebr','fmax_mhz','constraint_mhz','scope']}
  archives.append(folder.name)
 require((selected['cp23c']['lut4'],selected['cp23c']['ff'],selected['cp23d']['lut4'],selected['cp23d']['ff'])==(850,299,1087,416),'accepted resource results')
 require(len(archives)==100 and raw==395,'archive/report accounting')
 fixtures={};cycles={};normal=12928;faults=0
 for suite in SUITES:
  cycles[suite]={}
  for mode,label in [(-1,'ram'),(2,'fram')]:
   data=(build/f'{suite}-cycles-{mode}.csv').read_bytes()
   require(data==(build/f'{suite}-cycles-{mode}-portable.csv').read_bytes(),'ROM parity '+suite)
   require(data==gzip.decompress((reports/f'cp22-{suite}-cycles-{label}.csv.gz').read_bytes()),'CP22 cycles '+suite)
   rows=list(csv.DictReader(io.StringIO(data.decode())));count=oldcycles[suite][label]['cases']
   require(len(rows)==count and [int(r['case']) for r in rows]==list(range(count)),'cycle IDs '+suite)
   clocks=sum(int(r['microclocks']) for r in rows);beats=sum(int(r['memory_beats']) for r in rows)
   require((clocks,beats)==(oldcycles[suite][label]['microclocks'],oldcycles[suite][label]['memory_beats']),'cycle totals '+suite)
   cycles[suite][label]=dict(cases=count,microclocks=clocks,memory_beats=beats,portable_vendor_equal=True,cp22_equal=True)
   (reports/f'cp23-{suite}-cycles-{label}.csv.gz').write_bytes(gzip.compress(data,mtime=0))
  count=cycles[suite]['ram']['cases']
  if suite in ['bus-fault','psw-transfer-fault','eis-ash-fault','eis-ashc-fault']:faults+=count
  else:normal+=count
  data=(build/f'{suite}-vectors.txt').read_bytes()
  require(sha256(data)==baseline['fixtures_uncompressed_sha256'][suite],'C fixture '+suite)
  require(data==gzip.decompress((reports/f'cp22-{suite}-vectors.txt.gz').read_bytes()),'fixture bytes '+suite)
  fixtures[suite]=sha256(data);(reports/f'cp23-{suite}-vectors.txt.gz').write_bytes(gzip.compress(data,mtime=0))
  require((build/f'{suite}-oracle.log').read_bytes()==(reports/f'cp22-{suite}-oracle.log').read_bytes(),'oracle accounting '+suite)
  shutil.copyfile(build/f'{suite}-oracle.log',reports/f'cp23-{suite}-oracle.log')
 require((normal,faults)==(231105,35836),'full instruction/fault totals')
 for p in list(build.glob('*excluded.csv'))+list(build.glob('*continuation.csv'))+[build/'illegal-opcodes.txt']:
  old=reports/('cp22-'+p.name+'.gz')
  if old.exists():require(p.read_bytes()==gzip.decompress(old.read_bytes()),'exclusion/continuation '+p.name)
  (reports/('cp23-'+p.name+'.gz')).write_bytes(gzip.compress(p.read_bytes(),mtime=0))
 benchmarks={};runs=0
 for stem in STEMS:
  data=(build/(stem+'.json')).read_bytes()
  require(data==(build/(stem+'-portable.json')).read_bytes(),'benchmark ROM parity '+stem)
  require(data==gzip.decompress((reports/('cp22-'+stem+'.json.gz')).read_bytes()),'CP22 benchmark '+stem)
  benchmarks[stem]=json.loads(data);runs+=len(benchmarks[stem])
  (reports/('cp23-'+stem+'.json.gz')).write_bytes(gzip.compress(data,mtime=0))
 require(len(STEMS)==64 and runs==986,'benchmark inventory')
 for text in [logs['cp23-benchmarks-portable.log'],vendor]:
  for mode in [-1,0,1,2]:require(f'PASS ASHC benchmarks mode{mode}: 24' in text,'ASHC benchmark completion')
 for n in LOGS:shutil.copyfile(build/n,reports/n)
 models={p.name:sha256(p.read_bytes()) for p in (build/'vendor').glob('*.v')}
 require(models==baseline['vendor_models_sha256'],'Lattice simulation model changed')
 write_json(ROOT/'docs/benchmarks-cp23.json',dict(date='2026-09-09',clock_hz_nominal=29560000,spi_divider=1,method='Fresh functional RTL simulation with portable and vendor DP8KC; no board measurement.',runs_each_rom_model=runs,all_cp22_cycle_memory_spi_counts_unchanged=True,benchmarks=benchmarks,benchmark_metrics={stem:[dict(**row,microclocks_per_instruction=row['microclocks']/row['instructions'],memory_beats_per_instruction=row.get('memory_beats',row.get('memory_cycles'))/row['instructions'],calculated_ips_at_29_56_mhz=29560000*row['instructions']/row['microclocks']) for row in rows] for stem,rows in benchmarks.items()},differential_cycles=cycles))
 source=set()
 for pattern in ['docs/*.md','docs/*.patch','rtl/*.v','microcode/*.uasm','microcode/generated/*','microasm/*.py','tb/*.v','tb/*.c','tb/*.h','tb/test_*.py','tools/*.py','tools/formal-requirements.txt','reference/lsi11/*','synth/machxo2/*.v','synth/machxo2/*.lpf','synth/machxo2/*.sty']:source.update(p for p in ROOT.glob(pattern) if p.is_file())
 source.update(ROOT/n for n in ['Makefile','README.md','../core/core.c','../core/core.h','../core/hardware.c','../core/hardware.h','../core/pdp11_fp.c','../core/disas.c','../core/disas.h','../tests/core_tests.c'])
 buffer=io.BytesIO()
 with tarfile.open(fileobj=buffer,mode='w') as a:
  for p in sorted(source):
   data=p.read_bytes();name=('tests/'+p.name) if p.parent==ROOT/'../tests' else ('core/'+p.name) if p.parent==ROOT/'../core' else 'uJ11-fpga/'+str(p.relative_to(ROOT));info=tarfile.TarInfo(name);info.size=len(data);a.addfile(info,io.BytesIO(data))
 (reports/'cp23-verification-source.tgz').write_bytes(gzip.compress(buffer.getvalue(),mtime=0))
 files=source|set(reports.glob('cp23*'))|{ROOT/'docs/benchmarks-cp23.json'}
 for n in FINAL:files.update([ROOT/'synth/reports'/n/'inputs.json',ROOT/'synth/reports'/n/'result.json'])
 write_json(ROOT/'docs/verification-cp23.json',dict(date='2026-09-09',baseline='CP22',encoding_version=11,microcode_words=592,profile=baseline['profile'],checks=dict(completed_dcj11_cases_each_memory_rom_pair=normal,bus_fault_frame_cases_each_memory_rom_pair=faults,benchmark_runs_each_rom_model=runs,portable_vendor_identical_result_files=104,all_104_result_files_byte_identical_cp22=True,all_20_oracle_fixtures_byte_identical_cp22=True,formal_proven_equivalence_points=74,formal_negative_controls=1,sequencer_state_update_block_unchanged=True,all_other_rtl_microcode_emulator_and_test_sources_unchanged=True,python_test_methods=16,synthesis_archives_verified=archives,raw_report_hashes_verified=raw,current_inputs_equal_synthesis=FINAL),formal=proof,synthesis=selected,oracle=dict(original_core_sha256=sha256((ROOT/'../core/core.c').read_bytes()),source_unchanged_from_cp22=True),limits=['Same bounded ISA/kernel-mode and explicit exclusions as CP22; no new instruction or MMU.','Formal equivalence compares corresponding uPC/link/link_valid states, including reset, in two-state RTL semantics; all input combinations are unconstrained.','Fit includes probes and FRAM/IRQ resolver, not full UART/timer/panel/SD/RK or external pin timing; no FPGA programming.','35/29.56 MHz constraints pass; 50 MHz and the preferred 900-1000 LUT target remain unmet.'],fixtures_uncompressed_sha256=fixtures,vendor_models_sha256=models,files={str(p.relative_to(ROOT)):sha256(p.read_bytes()) for p in sorted(files) if p.is_file()}))
 print(f'CP23 recorded: {len(archives)} archives/{raw} raw hashes; 74 formal points + failing negative control; {normal} cases + {faults} fault frames per memory/ROM pair; {runs} benchmarks/ROM; all 104 results byte-identical to CP22.')
if __name__=='__main__':main()
