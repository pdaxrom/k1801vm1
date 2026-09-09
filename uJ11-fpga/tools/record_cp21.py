#!/usr/bin/env python3
"""Audit the bounded ASH checkpoint, exact HC1200 fit sources and both ROMs."""
import csv,gzip,io,importlib.util,shutil,tarfile
from pathlib import Path
from record_ea import ROOT,MODES,compare_results,read_json,require,sha256,write_json
from record_cp16 import COUNTS
from verify_cp21 import VENDOR,STEMS
from verify_cp20 import STEMS as OLD_STEMS
from check_eis_ash_negative import NAMES as NEGATIVE
from check_psw_transfer_negative import records
FINAL=['cp21a','cp21b']
LOGS=['cp21-tests.log','cp21-benchmarks-portable.log','cp21-decode-equivalence.log','cp21-negative-controls.log','cp21-vendor-prepare.log']+['cp21-vendor-'+n+'.log' for n in VENDOR]
NEW={'eis_ash':(26316,70304),'eis-ash-fault':(1024,6280)}
def main():
 build=ROOT/'build';reports=ROOT/'tb/reports'
 require(not list((ROOT/'rtl').glob('*mmu*')),'unexpected MMU RTL')
 stats=read_json(ROOT/'microcode/generated/m0.stats.json')
 require((stats['encoding_version'],stats['used_words'],stats['word_bits'])==(11,551,36),'format/occupancy')
 logs={n:(build/n).read_text() for n in LOGS}
 for n,t in logs.items():require(t and not any(x in t for x in ['FATAL','FAILED','%Error','%Warning']),n)
 portable=logs['cp21-tests.log'];vendor='\n'.join(logs['cp21-vendor-'+n+'.log'] for n in VENDOR if n!='benchmarks')
 counts=dict(COUNTS,**{'bus-fault':(32780,213080),'trace_bit':(11196,58965),'system_flags':(21120,75504),'psw_transfer':(12852,54492),'psw-transfer-fault':(1008,6336),'system_control':(6144,26368)},**NEW)
 for t in [portable,vendor]:
  for suite,(n,_) in counts.items():
   display={'bus-fault':'bus fault','psw-transfer-fault':'PSW-transfer fault','eis-ash-fault':'ASH fault'}.get(suite,suite)
   for mode in [-1,2]:require(f'PASS {display} differential mode{mode}: {n}' in t,suite+': missing tests')
  for marker in ['PASS differential: 12928','PASS FRAM differential: 12928','PASS stream engine: opcode+3','PASS EA directed: 14','PASS single directed: 46','PASS byte I/O: 23','PASS single byte directed: 46','PASS control directed: 58','PASS extra directed: 36','PASS trap directed: 112','PASS IRQ directed: 104','PASS legacy IRQ: 5 scenarios, 6 settled-state checks','PASS illegal directed: 96','PASS bus fault system: 32','PASS bus double fault: 96','PASS trace system: 24','PASS trace faults: 140','PASS system control legacy: 3 scenarios','PASS system control faults: 64']:require(marker in t,marker)
 for marker in ['Ran 16 tests','4096 pair checks','PASS prefetch: 525','PASS lsi11 peripherals: 19','exactly 56944 supported encodings','PASS byte ALU: 2097152','PASS byte datapath: 16384','PASS IRQ adapter: 763','PASS fault IRQ oracle: 8']:require(marker in portable,marker)
 require('PASS CP21 decoder: all 65536 checked; only ASH differ from archived CP20c' in logs['cp21-decode-equivalence.log'],'decoder miter')
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
 with tarfile.open(ROOT/'synth/reports/cp20c/source.tgz') as a:old_source=a.extractfile('microcode/m0.uasm').read().decode()
 old,lst,labels,_=asm.assemble(old_source);current,_,newlabels,_=asm.assemble((ROOT/'microcode/m0.uasm').read_text())
 old_addresses={int(l.split()[0],16) for l in lst.splitlines()}
 require(len(old_addresses)==521 and all(old[n]==current[n] for n in old_addresses),'old microinstructions changed')
 require(all(newlabels[k]==v for k,v in labels.items()),'old labels changed')
 changed={*range(0x019,0x01c),*range(0x218,0x220),*range(0x262,0x274),0x27f}
 require({i for i in range(1024) if old[i]!=current[i]}==changed and len(changed)==30,'only 30 ROM words change')
 for gate in ['cp20c','cp20d']:
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
   if suite not in NEW:require(data==gzip.decompress((reports/f'cp20-{suite}-cycles-{label}.csv.gz').read_bytes()),suite+': old cycles')
   cycles[suite][label]=dict(cases=count,microclocks=sum(int(r['microclocks']) for r in rows),memory_beats=beats,portable_vendor_equal=True)
   (reports/f'cp21-{suite}-cycles-{label}.csv.gz').write_bytes(gzip.compress(data,mtime=0))
  data=(build/f'{suite}-vectors.txt').read_bytes()
  if suite not in NEW:require(data==gzip.decompress((reports/f'cp20-{suite}-vectors.txt.gz').read_bytes()),suite+': old oracle fixture')
  fixtures[suite]=sha256(data);(reports/f'cp21-{suite}-vectors.txt.gz').write_bytes(gzip.compress(data,mtime=0))
  shutil.copyfile(build/f'{suite}-oracle.log',reports/f'cp21-{suite}-oracle.log')
 require('26316 completed of 27204 candidates; 888 excluded. Reasons abort=320 vector=336 I/O=524 odd=0 stack=204' in (build/'eis_ash-oracle.log').read_text(),'normal oracle accounting')
 excluded=list(csv.DictReader((build/'eis_ash-excluded.csv').open()));require(len(excluded)==888,'normal exclusions missing')
 require('1024 completed frames (752 ACK error, 272 odd-word), 0 excluded candidates' in (build/'eis-ash-fault-oracle.log').read_text(),'fault accounting')
 require(not list(csv.DictReader((build/'eis-ash-fault-excluded.csv').open())),'fault exclusions')
 require(not list(csv.DictReader((build/'eis-ash-fault-continuation.csv').open())),'unexpected post-frame C continuation')
 items=list(records('eis_ash'));require(len(items)==26316,'normal fixture count')
 normal_ops={int(h[1],16) for h,c,t in items};fault_ops={int(h[1],16) for h,c,t in records('eis-ash-fault',True)}
 odd_pc={0o72057|(rs<<6) for rs in range(8)}
 require(normal_ops==set(range(0o72000,0o73000))-odd_pc,'504 normal ASH encodings')
 require(normal_ops|fault_ops==set(range(0o72000,0o73000)) and odd_pc<=fault_ops,'all 512 ASH encodings covered, including eight unavoidable odd-PC faults')
 require(all(int(c[2],16)==1 for h,c,t in items),'one instruction per fixture')
 # Isolated register tests: measured formula over all values/counts/NZVC,
 # independent of overflow data; initial group is the full 15 x 64 x 16 grid.
 ideal=list(csv.DictReader((build/'eis_ash-cycles--1.csv').open()));standalone={}
 for n in range(64):
  ids={int(h[0],16) for h,c,t in items[:15360] if (int(h[4],16)&63)==n}
  measured={int(r['microclocks']) for r in ideal if int(r['case']) in ids and int(r['wait_clocks'])==0}
  expected=10 if n==0 else 16+4*n if n<32 else 13+3*(64-n)
  require(measured=={expected},f'ASH count {n} ideal CPI');standalone[str(n)]=expected
 high={(int(h[4],16)&0xffc0,int(h[4],16)&63,int(h[3],16)) for h,c,t in items[15360:16384]}
 require(high=={(hi,n,v) for hi in [0,64,128,0xffc0] for n in range(64) for v in [0,1,0x8000,0xffff]},'ignored count bits coverage')
 require({int(c[1],16) for h,c,t in items}=={0,1},'IRQ coverage')
 require(any(int(h[2],16)&16 for h,c,t in items),'trace coverage')
 old_candidates=set(gzip.decompress((reports/'cp20-illegal-opcodes.txt.gz').read_bytes()).splitlines());new_candidates=set((build/'illegal-opcodes.txt').read_bytes().splitlines())
 removed={f'{op:04x} 00000008'.encode() for op in range(0o72000,0o73000)}
 require(old_candidates-new_candidates==removed and not new_candidates-old_candidates,'exactly 512 removed fallback encodings')
 require('4160 completed of 17184 candidates; 13024 excluded' in (build/'illegal-oracle.log').read_text(),'fallback accounting')
 for name in ['eis_ash-excluded.csv','eis-ash-fault-excluded.csv','eis-ash-fault-continuation.csv','system_control-excluded.csv','psw_transfer-excluded.csv','psw-transfer-fault-excluded.csv','psw-transfer-fault-continuation.csv','system_flags-excluded.csv','trace_bit-excluded.csv','bus-fault-excluded.csv','bus-fault-continuation.csv','illegal-opcodes.txt']:(reports/('cp21-'+name+'.gz')).write_bytes(gzip.compress((build/name).read_bytes(),mtime=0))
 old_runs=0
 for stem in OLD_STEMS:
  old=read_json_gzip(reports/('cp20-'+stem+'.json.gz'))
  require(compare_results(stem,len(old))==old,stem+': historical benchmark');old_runs+=len(old)
 require(old_runs==794,'historical benchmark count')
 newbench=[]
 for mode,label in MODES.items():
  for r in compare_results(f'eis-ash-benchmarks-{mode}',24):
   cpi=r['microclocks']/r['instructions'];newbench.append(dict(memory_mode=mode,memory_model=label,**r,microclocks_per_instruction=cpi,memory_beats_per_instruction=r['memory_beats']/r['instructions'],calculated_ips_at_29_56_mhz=29560000/cpi))
 for log in ['cp21-benchmarks-portable.log','cp21-vendor-benchmarks.log']:
  for m in [-1,0,1,2]:require(f'PASS ASH benchmarks mode{m}: 24' in logs[log],log)
 for stem in STEMS:(reports/('cp21-'+stem+'.json.gz')).write_bytes(gzip.compress((build/(stem+'.json')).read_bytes(),mtime=0))
 write_json(ROOT/'docs/benchmarks-cp21.json',dict(date='2026-09-09',clock_hz_nominal=29560000,spi_divider=1,method='Functional RTL simulation with portable and vendor DP8KC; no board measurement.',runs_each_rom_model=890,cp20_all_794_benchmark_runs_unchanged=True,ash=newbench,standalone_ideal_clocks_including_fetch=standalone,differential_cycles=cycles))
 for n in LOGS:shutil.copyfile(build/n,reports/n)
 for n in NEGATIVE:
  require(f'PASS ASH negative: {n}' in logs['cp21-negative-controls.log'],'negative control')
  data=(build/f'cp21-negative-{n}.log').read_bytes();require(b'FATAL' in data,'negative failure missing');(reports/f'cp21-negative-{n}.log').write_bytes(data)
 original=(ROOT/'../core/core.c').read_bytes()
 from oracle_core import instrument
 from fault_oracle import instrument_faults
 for n,expected in [('oracle_core.c',instrument(original.decode())),('oracle_control_core.c',instrument(original.decode(),vectors=True)),('oracle_fault_core.c',instrument_faults(original.decode()))]:
  data=(build/n).read_bytes();require(data==expected.encode(),'oracle hooks changed');(reports/('cp21-'+n+'.gz')).write_bytes(gzip.compress(data,mtime=0))
 baseline=read_json(ROOT/'docs/verification-cp20.json')
 require(sha256(original)==baseline['oracle']['original_core_sha256'],'original core changed')
 for n,h in baseline['files'].items():
  if n.startswith('../core/'):require(sha256((ROOT/n).read_bytes())==h,'original source changed '+n)
 source=set()
 for pattern in ['docs/*.md','rtl/*.v','microcode/*.uasm','microcode/generated/*','microasm/*.py','tb/*.v','tb/*.c','tb/*.h','tb/test_*.py','tools/*.py','reference/lsi11/*','synth/machxo2/*.v','synth/machxo2/*.lpf','synth/machxo2/*.sty']:source.update(p for p in ROOT.glob(pattern) if p.is_file())
 source.update(ROOT/p for p in ['Makefile','README.md','../core/core.c','../core/core.h','../core/hardware.c','../core/hardware.h','../core/pdp11_fp.c'])
 buffer=io.BytesIO()
 with tarfile.open(fileobj=buffer,mode='w') as a:
  for p in sorted(source):
   data=p.read_bytes();name=('core/'+p.name) if p.parent==ROOT/'../core' else 'uJ11-fpga/'+str(p.relative_to(ROOT));info=tarfile.TarInfo(name);info.size=len(data);a.addfile(info,io.BytesIO(data))
 (reports/'cp21-verification-source.tgz').write_bytes(gzip.compress(buffer.getvalue(),mtime=0))
 files=source|set(reports.glob('cp21*'))|{ROOT/'docs/benchmarks-cp21.json'}
 for n in FINAL:files.update([ROOT/'synth/reports'/n/'inputs.json',ROOT/'synth/reports'/n/'result.json'])
 write_json(ROOT/'docs/verification-cp21.json',dict(date='2026-09-09',encoding_version=11,microcode_words=551,profile='One kernel register set; CM=PM=RS=0; NZVC/IPL/T; project DCJ11 HALT restart profile; ASH; no MMU.',checks=dict(python_test_methods=16,completed_dcj11_cases_each_memory_rom_pair=202597,cp20_completed_cases_unchanged=176281,new_ash_cases=26316,new_candidates=27204,new_exclusions=888,bus_fault_frame_cases=34812,old_bus_fault_frames=33788,new_ash_fault_frames=1024,new_ash_fault_exclusions=0,negative_controls=NEGATIVE,new_state_ff=0,new_probe_observation_ff=0,benchmark_runs_each_rom_model=890,new_benchmark_runs_each_rom_model=96,decoder_encodings_checked=65536,decoder_supported=56944,new_opcodes='072000..072777 ASH octal',normal_completed_opcode_encodings=504,unavoidable_odd_pc_fault_encodings=8,old_words_and_labels_unchanged=521,new_assembled_words=30,new_non_padding_rom_words=30,synthesis_archives_verified=archives,raw_report_hashes_verified=raw,current_inputs_equal_synthesis=FINAL,portable_vendor_identical_result_files=96),oracle=dict(original_core_sha256=sha256(original),source_unchanged=True),limits=['ASH normal/trace/IRQ candidates outside the abort/I/O/stack profile are excluded explicitly; separate ASH bus fault frame tests have no exclusions.','MUL/DIV/ASHC/XOR, register-bank/mode exchange, native ODT, red/yellow stack recovery, I/O timeout and MMU remain absent.','Count uses the existing RF, 3 or 4 microclocks per shift; no added hardware counter or barrel shifter.','Fit includes probes and FRAM/IRQ resolver, not full UART/timer/panel/SD/RK or external pin timing; no FPGA programming.'],fixtures_uncompressed_sha256=fixtures,vendor_models_sha256={p.name:sha256(p.read_bytes()) for p in (build/'vendor').glob('*.v')},files={str(p.relative_to(ROOT)):sha256(p.read_bytes()) for p in sorted(files) if p.is_file()}))
 print(f'CP21 recorded: {len(archives)} archives/{raw} raw report hashes; 202597 completed cases + 34812 fault frames per memory/ROM pair; 890 benchmarks per ROM.')
def read_json_gzip(path):
 import json
 return json.loads(gzip.decompress(path.read_bytes()))
if __name__=='__main__':main()
