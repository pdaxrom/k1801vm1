#!/usr/bin/env python3
"""Audit the bounded ASHC checkpoint, exact HC1200 fit sources and both ROMs."""
import csv,gzip,io,importlib.util,shutil,tarfile
from pathlib import Path
from record_ea import ROOT,MODES,compare_results,read_json,require,sha256,write_json
from record_cp16 import COUNTS
from verify_cp22 import VENDOR,STEMS
from verify_cp21 import STEMS as OLD_STEMS
from check_eis_ashc_negative import NAMES as NEGATIVE
from check_eis_ash_negative import NAMES as ASH_NEGATIVE
from check_psw_transfer_negative import records
FINAL=['cp22c','cp22d']
LOGS=['cp22-tests.log','cp22-benchmarks-portable.log','cp22-decode-equivalence.log','cp22-negative-controls.log','cp22-ash-negative-controls.log','cp22-vendor-prepare.log','cp22-core-tests.log']+['cp22-vendor-'+n+'.log' for n in VENDOR]
NEW={'eis_ashc':(28496,73060),'eis-ashc-fault':(1024,6280)}
UPDATED={'eis_ash':(26328,70388)}
def main():
 build=ROOT/'build';reports=ROOT/'tb/reports'
 require(not list((ROOT/'rtl').glob('*mmu*')),'unexpected MMU RTL')
 stats=read_json(ROOT/'microcode/generated/m0.stats.json')
 require((stats['encoding_version'],stats['used_words'],stats['word_bits'])==(11,592,36),'format/occupancy')
 logs={n:(build/n).read_text() for n in LOGS}
 for n,t in logs.items():require(t and not any(x in t for x in ['FATAL','FAILED','%Error','%Warning']),n)
 portable=logs['cp22-tests.log'];require('All core instruction tests passed' in logs['cp22-core-tests.log'],'existing C core regression');vendor='\n'.join(logs['cp22-vendor-'+n+'.log'] for n in VENDOR if n!='benchmarks')
 counts=dict(COUNTS,**{'bus-fault':(32780,213080),'trace_bit':(11196,58965),'system_flags':(21120,75504),'psw_transfer':(12852,54492),'psw-transfer-fault':(1008,6336),'system_control':(6144,26368),'eis-ash-fault':(1024,6280)},**UPDATED,**NEW)
 for t in [portable,vendor]:
  for suite,(n,_) in counts.items():
   display={'bus-fault':'bus fault','psw-transfer-fault':'PSW-transfer fault','eis-ashc-fault':'ASHC fault','eis-ash-fault':'ASH fault'}.get(suite,suite)
   for mode in [-1,2]:require(f'PASS {display} differential mode{mode}: {n}' in t,suite+': missing tests')
  for marker in ['PASS differential: 12928','PASS FRAM differential: 12928','PASS stream engine: opcode+3','PASS EA directed: 14','PASS single directed: 46','PASS byte I/O: 23','PASS single byte directed: 46','PASS control directed: 58','PASS extra directed: 36','PASS trap directed: 112','PASS IRQ directed: 104','PASS legacy IRQ: 5 scenarios, 6 settled-state checks','PASS illegal directed: 96','PASS bus fault system: 32','PASS bus double fault: 96','PASS trace system: 24','PASS trace faults: 140','PASS system control legacy: 3 scenarios','PASS system control faults: 64']:require(marker in t,marker)
 for marker in ['Ran 16 tests','4096 pair checks','PASS prefetch: 525','PASS lsi11 peripherals: 19','exactly 57456 supported encodings','PASS byte ALU: 2097152','PASS byte datapath: 16384','PASS IRQ adapter: 763','PASS fault IRQ oracle: 8']:require(marker in portable,marker)
 require('PASS CP22 decoder: all 65536 checked; only ASHC differ from archived CP21a' in logs['cp22-decode-equivalence.log'],'decoder miter')
 archives=[];raw=0
 for folder in sorted((ROOT/'synth/reports').iterdir()):
  info=read_json(folder/'inputs.json')
  with tarfile.open(folder/'source.tgz') as a:
   members={m.name.removeprefix('./'):m for m in a.getmembers()}
   for n,h in info['files'].items():
    data=(folder/n.split(':',1)[1]).read_bytes() if n.startswith('generated:') else a.extractfile(members[n]).read()
    require(sha256(data)==h,f'archive {folder.name}/{n}')
    if folder.name in FINAL and not n.startswith('generated:'):require(sha256((ROOT/n).read_bytes())==h,'current fit input: '+n)
  if 'placement' in info:require(sha256((folder/'design.ncd').read_bytes())==info['placement']['ncd_sha256'],'placement')
  if (folder/'result.json').exists():
   result=read_json(folder/'result.json')
   for n,h in result.get('reports',{}).items():require(sha256((folder/('design'+(Path(n).suffix or n))).read_bytes())==h,'raw report');raw+=1
   if folder.name in FINAL:require(result['timing_pass'] and result['fully_routed'] and result['ebr']==4 and result['lut4']<1100,'final fit')
  archives.append(folder.name)
 spec=importlib.util.spec_from_file_location('asm',ROOT/'microasm/uj11asm.py');asm=importlib.util.module_from_spec(spec);spec.loader.exec_module(asm)
 with tarfile.open(ROOT/'synth/reports/cp21a/source.tgz') as a:old_source=a.extractfile('microcode/m0.uasm').read().decode()
 old,lst,labels,_=asm.assemble(old_source);current,_,newlabels,_=asm.assemble((ROOT/'microcode/m0.uasm').read_text())
 old_addresses={int(l.split()[0],16) for l in lst.splitlines()}
 require(len(old_addresses)==551 and all(old[n]==current[n] for n in old_addresses-{0x019,0x01a}),'old microinstructions changed')
 require(all(newlabels[k]==v for k,v in labels.items()),'old labels changed')
 changed={*range(0x01c,0x020),*range(0x290,0x298),*range(0x2dc,0x2f9)}
 require({i for i in range(1024) if old[i]!=current[i]}==changed|{0x019,0x01a} and len(changed)==41,'41 added words and two corrected ASH words')
 for gate in ['cp21a','cp21b']:
  with tarfile.open(ROOT/f'synth/reports/{gate}/source.tgz') as a:
   for n in read_json(ROOT/f'synth/reports/{gate}/inputs.json')['files']:
    if (n.startswith(('rtl/','reference/','synth/machxo2/')) or n=='microasm/uj11asm.py') and n!='rtl/uj11_decode.v':
     require((ROOT/n).read_bytes()==a.extractfile(n).read(),'unchanged RTL/format/probe '+n)
 fixtures={};cycles={}
 for suite,(count,beats) in counts.items():
  cycles[suite]={}
  for mode,label in [(-1,'ram'),(2,'fram')]:
   data=(build/f'{suite}-cycles-{mode}.csv').read_bytes()
   require(data==(build/f'{suite}-cycles-{mode}-portable.csv').read_bytes(),suite+': portable/vendor CSV')
   rows=list(csv.DictReader(io.StringIO(data.decode())))
   require(len(rows)==count and [int(r['case']) for r in rows]==list(range(count)),suite+': IDs/count')
   require(sum(int(r['memory_beats']) for r in rows)==beats,suite+': beats')
   if suite not in NEW and suite not in UPDATED and suite!='eis-ash-fault':require(data==gzip.decompress((reports/f'cp21-{suite}-cycles-{label}.csv.gz').read_bytes()),suite+': old cycles')
   cycles[suite][label]=dict(cases=count,microclocks=sum(int(r['microclocks']) for r in rows),memory_beats=beats,portable_vendor_equal=True)
   (reports/f'cp22-{suite}-cycles-{label}.csv.gz').write_bytes(gzip.compress(data,mtime=0))
  data=(build/f'{suite}-vectors.txt').read_bytes()
  if suite not in NEW and suite not in UPDATED:require(data==gzip.decompress((reports/f'cp21-{suite}-vectors.txt.gz').read_bytes()),suite+': old oracle fixture')
  fixtures[suite]=sha256(data);(reports/f'cp22-{suite}-vectors.txt.gz').write_bytes(gzip.compress(data,mtime=0))
  shutil.copyfile(build/f'{suite}-oracle.log',reports/f'cp22-{suite}-oracle.log')
 # ASH count EA now precedes the snapshot. Early EA faults omit that word;
 # FRAM timing also changes because CALL(prefetch=0) and ALU exchange places.
 # Architectural fixtures and every non-cycle CSV column must remain identical.
 ash_fault_deltas={}
 fault_items=list(records('eis-ash-fault',True))
 for mode,label,expected_changed,expected_cycles in [(-1,'ram',408,34772),(2,'fram',1024,640308)]:
  oldrows=list(csv.DictReader(io.StringIO(gzip.decompress((reports/f'cp21-eis-ash-fault-cycles-{label}.csv.gz').read_bytes()).decode())))
  newrows=list(csv.DictReader((build/f'eis-ash-fault-cycles-{mode}.csv').open()))
  deltas=[]
  for (h,c,t),a,b in zip(fault_items,oldrows,newrows):
   require({k:v for k,v in a.items() if k!='microclocks'}=={k:v for k,v in b.items() if k!='microclocks'},'ASH fault non-cycle fields')
   delta=int(b['microclocks'])-int(a['microclocks']);deltas.append(delta)
   if mode==-1:
    op=int(h[1],16);ea=(op>>3)&7;idx=int(h[11],16)
    early=(idx==1 and ea in [3,5,6,7]) or (idx==2 and ea==7) or (idx==65535 and ea in [3,5] and int(h[3+(op&7)],16)&1)
    require(delta==(-1 if early else 0),'ASH EA-fault omitted snapshot timing')
  require(sum(d!=0 for d in deltas)==expected_changed and sum(int(r['microclocks']) for r in newrows)==expected_cycles,'ASH fault cycle accounting')
  ash_fault_deltas[label]=dict(changed_cases=expected_changed,old_microclocks=sum(int(r['microclocks']) for r in oldrows),microclocks=expected_cycles,minimum_delta=min(deltas),maximum_delta=max(deltas),all_non_cycle_columns_unchanged=True)
 require('28496 completed of 29300 candidates; 804 excluded. Reasons abort=320 vector=456 I/O=268 odd=0 stack=384' in (build/'eis_ashc-oracle.log').read_text(),'normal oracle accounting')
 excluded=list(csv.DictReader((build/'eis_ashc-excluded.csv').open()));require(len(excluded)==804,'normal exclusions missing')
 require('1024 completed frames (752 ACK error, 272 odd-word), 0 excluded candidates' in (build/'eis-ashc-fault-oracle.log').read_text(),'fault accounting')
 require(not list(csv.DictReader((build/'eis-ashc-fault-excluded.csv').open())),'fault exclusions')
 require(not list(csv.DictReader((build/'eis-ashc-fault-continuation.csv').open())),'unexpected post-frame C continuation')
 items=list(records('eis_ashc'));require(len(items)==28496,'normal fixture count')
 normal_ops={int(h[1],16) for h,c,t in items};fault_ops={int(h[1],16) for h,c,t in records('eis-ashc-fault',True)}
 odd_pc={0o73057|(rs<<6) for rs in range(8)}
 require(normal_ops==set(range(0o73000,0o74000))-odd_pc,'504 normal ASHC encodings')
 require(normal_ops|fault_ops==set(range(0o73000,0o74000)) and odd_pc<=fault_ops,'all 512 ASHC encodings covered, including eight unavoidable odd-PC faults')
 require(all(int(c[2],16)==1 for h,c,t in items),'one instruction per fixture')
 # Isolated register tests: measured formula over all values/counts/NZVC,
 # independent of overflow data; initial group is the full 15 x 64 x 16 grid.
 ideal=list(csv.DictReader((build/'eis_ashc-cycles--1.csv').open()));standalone={}
 for n in range(64):
  ids={int(h[0],16) for h,c,t in items[:15360] if (int(h[5],16)&63)==n}
  measured={int(r['microclocks']) for r in ideal if int(r['case']) in ids and int(r['wait_clocks'])==0}
  expected=21 if n==0 else 24+5*n if n<32 else 25+5*(64-n)
  require(measured=={expected},f'ASHC count {n} ideal CPI');standalone[str(n)]=expected
 high={(int(h[5],16)&0xffc0,int(h[5],16)&63,(int(h[3],16)<<16|int(h[4],16))) for h,c,t in items[15360:16384]}
 require(high=={(hi,n,v) for hi in [0,64,128,0xffc0] for n in range(64) for v in [0,1,0x80000000,0xffffffff]},'ignored count bits coverage')
 require({int(c[1],16) for h,c,t in items}=={0,1},'IRQ coverage')
 require(any(int(h[2],16)&16 for h,c,t in items),'trace coverage')
 old_candidates=set(gzip.decompress((reports/'cp21-illegal-opcodes.txt.gz').read_bytes()).splitlines());new_candidates=set((build/'illegal-opcodes.txt').read_bytes().splitlines())
 removed={f'{op:04x} 00000008'.encode() for op in range(0o73000,0o74000)}
 require(old_candidates-new_candidates==removed and not new_candidates-old_candidates,'exactly 512 removed fallback encodings')
 require('4160 completed of 16160 candidates; 12000 excluded' in (build/'illegal-oracle.log').read_text(),'fallback accounting')
 for name in ['eis_ash-excluded.csv','eis-ash-fault-excluded.csv','eis-ash-fault-continuation.csv','eis_ashc-excluded.csv','eis-ashc-fault-excluded.csv','eis-ashc-fault-continuation.csv','system_control-excluded.csv','psw_transfer-excluded.csv','psw-transfer-fault-excluded.csv','psw-transfer-fault-continuation.csv','system_flags-excluded.csv','trace_bit-excluded.csv','bus-fault-excluded.csv','bus-fault-continuation.csv','illegal-opcodes.txt']:(reports/('cp22-'+name+'.gz')).write_bytes(gzip.compress((build/name).read_bytes(),mtime=0))
 old_runs=0;changed_benchmarks=[]
 for stem in OLD_STEMS:
  old=read_json_gzip(reports/('cp21-'+stem+'.json.gz'));new=compare_results(stem,len(old));old_runs+=len(old)
  for before,after in zip(old,new):
   expected=dict(before)
   if stem=='eis-ash-benchmarks-2':
    name=before['workload']
    if name in ['ASH_register_count0','ASH_register_count1','ASH_register_count63']:expected['microclocks']+=496
    elif name.startswith('ASH_immediate_'):expected['microclocks']-=7440;expected['spi_clocks']-=3968
    elif name.startswith('ASH_memory_'):expected['microclocks']-=7688;expected['spi_clocks']-=3968
   require(after==expected,stem+': unexplained historical benchmark change')
   if before!=after:changed_benchmarks.append(dict(stem=stem,workload=before['workload'],before=before,after=after))
 require(old_runs==890 and len(changed_benchmarks)==19,'historical benchmark count/deltas')
 newbench=[]
 for mode,label in MODES.items():
  for r in compare_results(f'eis-ashc-benchmarks-{mode}',24):
   cpi=r['microclocks']/r['instructions'];newbench.append(dict(memory_mode=mode,memory_model=label,**r,microclocks_per_instruction=cpi,memory_beats_per_instruction=r['memory_beats']/r['instructions'],calculated_ips_at_29_56_mhz=29560000/cpi))
 for log,text in [('portable',logs['cp22-benchmarks-portable.log']),('vendor','\n'.join(logs['cp22-vendor-'+n+'.log'] for n in VENDOR))]:
  for m in [-1,0,1,2]:require(f'PASS ASHC benchmarks mode{m}: 24' in text,log)
 for stem in STEMS:(reports/('cp22-'+stem+'.json.gz')).write_bytes(gzip.compress((build/(stem+'.json')).read_bytes(),mtime=0))
 write_json(ROOT/'docs/benchmarks-cp22.json',dict(date='2026-09-09',clock_hz_nominal=29560000,spi_divider=1,method='Functional RTL simulation with portable and vendor DP8KC; no board measurement.',runs_each_rom_model=986,cp21_benchmark_runs=890,cp21_benchmark_runs_unchanged=871,cp21_ash_prefetch_benchmark_changes=changed_benchmarks,ash_fault_timing_changes=ash_fault_deltas,ashc=newbench,standalone_ideal_clocks_including_fetch=standalone,differential_cycles=cycles))
 for n in LOGS:shutil.copyfile(build/n,reports/n)
 for n in NEGATIVE:
  require(f'PASS ASHC negative: {n}' in logs['cp22-negative-controls.log'],'negative control')
  data=(build/f'cp22-negative-{n}.log').read_bytes();require(b'FATAL' in data,'negative failure missing');(reports/f'cp22-negative-{n}.log').write_bytes(data)
 for n in ASH_NEGATIVE:
  require(f'PASS ASH negative: {n}' in logs['cp22-ash-negative-controls.log'],'ASH negative control')
  data=(build/f'cp22-ash-negative-{n}.log').read_bytes();require(b'FATAL' in data,'ASH negative failure missing');(reports/f'cp22-ash-negative-{n}.log').write_bytes(data)
 require('26328 completed of 27204 candidates; 876 excluded. Reasons abort=320 vector=336 I/O=512 odd=0 stack=204' in (build/'eis_ash-oracle.log').read_text(),'corrected ASH accounting')
 require(len(list(csv.DictReader((build/'eis_ash-excluded.csv').open())))==876,'ASH exclusions missing')
 alias=read_json(build/'cp22-alias-diagnostic.json')
 require(alias['raw_c_cases']==256 and alias['independent_register_cases']==17408 and alias['independent_ash_alias_cases']==14 and alias['independent_reference_mismatches']==0 and alias['rewritten_expected_records']==0,'alias reference accounting')
 require(sha256((build/'eis_ashc_alias-vectors.txt').read_bytes())==alias['raw_c_fixture_sha256'],'alias C fixture hash')
 require((build/'eis_ashc_alias-vectors.txt').read_bytes()==(build/'eis_ashc_alias-checked-vectors.txt').read_bytes(),'no rewritten oracle expectations')
 for model in ['portable','vendor']:
  for m in [-1,2]:
   data=(build/f'cp22-alias-cycles-{model}-{m}.csv').read_bytes()
   require(data==(build/f'cp22-alias-cycles-portable-{m}.csv').read_bytes(),'alias ROM parity')
   require(len(list(csv.DictReader(io.StringIO(data.decode()))))==256,'alias positive tests missing')
   require(sha256(data)==alias['rtl_tests'][f'{model}/{m}']['cycles_sha256'],'alias cycle hash')
 for p in list(build.glob('cp22-alias-*'))+list(build.glob('eis_ashc_alias-*')):
  if p.suffix in ['.json','.csv','.txt','.log']:
   if p.suffix=='.log':require('FATAL' not in p.read_text() and 'Traceback' not in p.read_text(),'alias log '+p.name)
   (reports/('cp22-'+p.name.removeprefix('cp22-')+'.gz')).write_bytes(gzip.compress(p.read_bytes(),mtime=0))
 original=(ROOT/'../core/core.c').read_bytes()
 from oracle_core import instrument
 from fault_oracle import instrument_faults
 for n,expected in [('oracle_core.c',instrument(original.decode())),('oracle_control_core.c',instrument(original.decode(),vectors=True)),('oracle_fault_core.c',instrument_faults(original.decode()))]:
  data=(build/n).read_bytes();require(data==expected.encode(),'oracle hooks changed');(reports/('cp22-'+n+'.gz')).write_bytes(gzip.compress(data,mtime=0))
 baseline=read_json(ROOT/'docs/verification-cp21.json')
 # The exact, reviewed DCJ11-only fix replaces the former incorrect oracle.
 import difflib
 with tarfile.open(reports/'cp21-verification-source.tgz') as archive:old_core=archive.extractfile('core/core.c').read()
 require(sha256(old_core)==baseline['oracle']['original_core_sha256'],'original reference baseline hash')
 patch=''.join(difflib.unified_diff(old_core.decode().splitlines(keepends=True),original.decode().splitlines(keepends=True),fromfile='a/core/core.c',tofile='b/core/core.c'))
 require(patch==(ROOT/'docs/cp22-core-fix.patch').read_text(),'unexpected reference core edits')
 require(old_core.decode().split('case 0072: { /* ASH */')[0]==original.decode().split('case 0072: { /* ASH */')[0] and old_core.decode().split('case 0074: { /* XOR */')[1]==original.decode().split('case 0074: { /* XOR */')[1],'C changes outside ASH/ASHC')
 for n,h in baseline['files'].items():
  if n.startswith('../core/') and n!='../core/core.c':require(sha256((ROOT/n).read_bytes())==h,'original source changed '+n)
 source=set()
 for pattern in ['docs/*.md','docs/*.patch','rtl/*.v','microcode/*.uasm','microcode/generated/*','microasm/*.py','tb/*.v','tb/*.c','tb/*.h','tb/test_*.py','tools/*.py','reference/lsi11/*','synth/machxo2/*.v','synth/machxo2/*.lpf','synth/machxo2/*.sty']:source.update(p for p in ROOT.glob(pattern) if p.is_file())
 source.update(ROOT/p for p in ['Makefile','README.md','../core/core.c','../core/core.h','../core/hardware.c','../core/hardware.h','../core/pdp11_fp.c','../core/disas.c','../core/disas.h','../tests/core_tests.c'])
 buffer=io.BytesIO()
 with tarfile.open(fileobj=buffer,mode='w') as a:
  for p in sorted(source):
   data=p.read_bytes();name=('tests/'+p.name) if p.parent==ROOT/'../tests' else ('core/'+p.name) if p.parent==ROOT/'../core' else 'uJ11-fpga/'+str(p.relative_to(ROOT));info=tarfile.TarInfo(name);info.size=len(data);a.addfile(info,io.BytesIO(data))
 (reports/'cp22-verification-source.tgz').write_bytes(gzip.compress(buffer.getvalue(),mtime=0))
 files=source|set(reports.glob('cp22*'))|{ROOT/'docs/benchmarks-cp22.json'}
 for n in FINAL:files.update([ROOT/'synth/reports'/n/'inputs.json',ROOT/'synth/reports'/n/'result.json'])
 write_json(ROOT/'docs/verification-cp22.json',dict(date='2026-09-09',encoding_version=11,microcode_words=592,profile='One kernel register set; CM=PM=RS=0; NZVC/IPL/T; project DCJ11 HALT restart profile; ASHC; no MMU.',checks=dict(python_test_methods=16,completed_dcj11_cases_each_memory_rom_pair=231105,non_eis_completed_cases_unchanged=176281,corrected_ash_cases=26328,new_ashc_cases=28496,new_candidates=29300,new_exclusions=804,bus_fault_frame_cases=35836,old_bus_fault_frames=34812,new_ashc_fault_frames=1024,new_ashc_fault_exclusions=0,negative_controls=NEGATIVE,ash_negative_controls=ASH_NEGATIVE,total_negative_control_runs=21,new_state_ff=0,new_probe_observation_ff=0,benchmark_runs_each_rom_model=986,new_benchmark_runs_each_rom_model=96,historical_benchmarks_unchanged=871,corrected_ash_prefetch_benchmarks_changed=19,ash_fault_timing_changes=ash_fault_deltas,decoder_encodings_checked=65536,decoder_supported=57456,new_opcodes='073000..073777 ASHC octal',normal_completed_opcode_encodings=504,unavoidable_odd_pc_fault_encodings=8,old_labels_unchanged=551,old_words_unchanged=549,corrected_ash_addresses=[25,26],new_assembled_words=41,new_non_padding_rom_words=41,changed_existing_rom_words=2,synthesis_archives_verified=archives,raw_report_hashes_verified=raw,current_inputs_equal_synthesis=FINAL,portable_vendor_identical_result_files=104),alias_diagnostic=alias,oracle=dict(original_core_sha256=sha256(original),previous_core_sha256=sha256(old_core),source_unchanged=False,correction='DCJ11 ASH/ASHC destination capture after count EA; ASHC NZ from full 32-bit result.',exact_patch='docs/cp22-core-fix.patch',existing_core_regression_passed=True),limits=['ASHC normal/trace/IRQ candidates outside the abort/I/O/stack profile are excluded explicitly; separate ASHC bus fault frame tests have no exclusions.','MUL/DIV/XOR, register-bank/mode exchange, native ODT, red/yellow stack recovery, I/O timeout and MMU remain absent.','Count uses the existing RF, 5 microclocks per shift; no added hardware counter or barrel shifter.','Fit includes probes and FRAM/IRQ resolver, not full UART/timer/panel/SD/RK or external pin timing; no FPGA programming.'],fixtures_uncompressed_sha256=fixtures,vendor_models_sha256={p.name:sha256(p.read_bytes()) for p in (build/'vendor').glob('*.v')},files={str(p.relative_to(ROOT)):sha256(p.read_bytes()) for p in sorted(files) if p.is_file()}))
 print(f'CP22 recorded: {len(archives)} archives/{raw} raw report hashes; 231105 completed cases + 35836 fault frames plus 256 independently checked positive alias cases per memory/ROM pair; 986 benchmarks per ROM.')
def read_json_gzip(path):
 import json
 return json.loads(gzip.decompress(path.read_bytes()))
if __name__=='__main__':main()
