#!/usr/bin/env python3
"""Record CP17 only after exact source, portable/vendor and CP16 regression checks."""
import csv,gzip,io,json,tarfile,shutil,importlib.util
from pathlib import Path
from record_ea import ROOT,MODES,compare_results,read_json,require,sha256,write_json
from record_cp16 import COUNTS
FINAL=['cp17a','cp17b']
LOGS=['cp17-tests.log','cp17-trace-bit.log','cp17-trace-directed.log',
      'cp17-benchmarks-portable.log','cp17-trace-benchmarks-portable.log',
      'cp17-vendor-base.log','cp17-vendor-isa.log','cp17-vendor-faults.log',
      'cp17-vendor-trace.log','cp17-vendor-benchmarks.log',
      'cp17-decode-equivalence.log','cp17-negative-controls.log']

def main():
 reports=ROOT/'tb/reports';build=ROOT/'build'
 stats=read_json(ROOT/'microcode/generated/m0.stats.json')
 require((stats['encoding_version'],stats['used_words'],stats['word_bits'])==(10,452,36),'microcode v10/452/36')
 logs={n:(build/n).read_text() for n in LOGS}
 for name,text in logs.items():
  require(text and not any(x in text for x in ['FATAL','FAILED','%Error','%Warning']),name)
 for name in ['cp17-tests.log','cp17-vendor-base.log']:
  for marker in ['PASS differential: 12928','PASS FRAM differential: 12928','PASS stream engine: opcode+3']:
   require(marker in logs[name],name+': '+marker)
 for name in ['cp17-tests.log','cp17-vendor-isa.log']:
  for suite,(n,_) in COUNTS.items():
   for mode in [-1,2]:require(f'PASS {suite} differential mode{mode}: {n}' in logs[name],name+': '+suite)
  for marker in ['PASS EA directed: 14','PASS single directed: 46','PASS byte I/O: 23',
                 'PASS single byte directed: 46','PASS control directed: 58','PASS extra directed: 36',
                 'PASS trap directed: 112','PASS IRQ directed: 104','PASS legacy IRQ: 5 scenarios, 6 settled-state checks',
                 'PASS illegal directed: 96']:
   require(marker in logs[name],name+': '+marker)
 for marker in ['Ran 15 tests','4096 pair checks','PASS prefetch: 525','PASS lsi11 peripherals: 19',
                'exactly 56269 supported encodings','PASS byte ALU: 2097152','PASS byte datapath: 16384','PASS IRQ adapter: 763',
                'PASS fault IRQ oracle: 8']:
  require(marker in logs['cp17-tests.log'],marker)
 for name in ['cp17-tests.log','cp17-vendor-faults.log']:
  for marker in ['PASS bus fault differential mode-1: 32780','PASS bus fault differential mode2: 32780',
                 'PASS bus fault system: 32','PASS bus double fault: 96']:
   require(marker in logs[name],name+': '+marker)
 for name in ['cp17-trace-bit.log','cp17-vendor-trace.log']:
  for mode in [-1,2]:require(f'PASS trace_bit differential mode{mode}: 11196' in logs[name],name)
 for name in ['cp17-trace-directed.log','cp17-vendor-trace.log']:
  for marker in ['PASS trace system: 24','PASS trace faults: 140']:require(marker in logs[name],name)
 require('PASS CP17 decoder: all 65536 checked; only RTT differs from archived CP16e' in logs['cp17-decode-equivalence.log'],'decoder miter')
 archives=[];raw=0
 for folder in sorted((ROOT/'synth/reports').iterdir()):
  info=read_json(folder/'inputs.json')
  with tarfile.open(folder/'source.tgz') as a:
   members={m.name.removeprefix('./'):m for m in a.getmembers()}
   for name,h in info['files'].items():
    if name.startswith('generated:'):data=(folder/name.split(':',1)[1]).read_bytes()
    else:
     data=a.extractfile(members[name]).read()
     if folder.name in FINAL:require(sha256((ROOT/name).read_bytes())==h,f'Current synthesis input changed: {name}')
    require(sha256(data)==h,f'Archive hash: {folder.name}/{name}')
  if 'placement' in info:require(sha256((folder/'design.ncd').read_bytes())==info['placement']['ncd_sha256'],'placement hash')
  if (folder/'result.json').exists():
   result=read_json(folder/'result.json')
   for name,h in result.get('reports',{}).items():
    require(sha256((folder/('design'+(Path(name).suffix or name))).read_bytes())==h,'raw report hash');raw+=1
   if folder.name in FINAL:require(result['timing_pass'] and result['fully_routed'] and result['ebr']==4,'final fit gate')
  archives.append(folder.name)
 spec=importlib.util.spec_from_file_location('asm',ROOT/'microasm/uj11asm.py');asm=importlib.util.module_from_spec(spec);spec.loader.exec_module(asm)
 with tarfile.open(ROOT/'synth/reports/cp16f/source.tgz') as a:old_source=a.extractfile('microcode/m0.uasm').read().decode()
 old,_,labels,_=asm.assemble(old_source);current,_,current_labels,_=asm.assemble((ROOT/'microcode/m0.uasm').read_text())
 require([i for i in range(1024) if old[i]!=current[i]]==[0x37] and current[0x37]==(old[0x37]|1),'only RTI/RTT terminal bit0 changes')
 require(labels==current_labels,'label addresses changed')
 fixtures={};cycles={}
 for suite,(count,beats) in dict(COUNTS,**{'bus-fault':(32780,213080),'trace_bit':(11196,58965)}).items():
  cycles[suite]={}
  for mode,label in [(-1,'ram'),(2,'fram')]:
   data=(build/f'{suite}-cycles-{mode}.csv').read_bytes()
   require(data==(build/f'{suite}-cycles-{mode}-portable.csv').read_bytes(),suite+': portable/vendor CSV mismatch')
   rows=list(csv.DictReader(io.StringIO(data.decode())))
   require(len(rows)==count and [int(r['case']) for r in rows]==list(range(count)),suite+': case IDs/count')
   require(sum(int(r['memory_beats']) for r in rows)==beats,suite+': bus count')
   if suite!='trace_bit':require(data==gzip.decompress((reports/f'cp16-{suite}-cycles-{label}.csv.gz').read_bytes()),suite+': CP16 cycles changed')
   else:require(sum(int(r['microclocks']) for r in rows)==(328315 if mode==-1 else 6684208),'trace cycle baseline')
   cycles[suite][label]=dict(cases=count,microclocks=sum(int(r['microclocks']) for r in rows),memory_beats=beats,portable_vendor_equal=True)
   (reports/f'cp17-{suite}-cycles-{label}.csv.gz').write_bytes(gzip.compress(data,mtime=0))
  data=(build/f'{suite}-vectors.txt').read_bytes()
  if suite!='trace_bit':require(data==gzip.decompress((reports/f'cp16-{suite}-vectors.txt.gz').read_bytes()),suite+': old fixture changed')
  fixtures[suite]=sha256(data);(reports/f'cp17-{suite}-vectors.txt.gz').write_bytes(gzip.compress(data,mtime=0))
  shutil.copyfile(build/f'{suite}-oracle.log',reports/f'cp17-{suite}-oracle.log')
 require('4160 completed of 18534 candidates; 14374 excluded' in (build/'illegal-oracle.log').read_text(),'illegal candidate accounting')
 old_candidates=set(gzip.decompress((reports/'cp16-illegal-opcodes.txt.gz').read_bytes()).splitlines());new_candidates=set((build/'illegal-opcodes.txt').read_bytes().splitlines())
 require(old_candidates-new_candidates=={b'0006 00000008'} and not new_candidates-old_candidates,'only RTT removed from fallback candidates')
 exclusions=list(csv.DictReader((build/'trace_bit-excluded.csv').open()))
 require(len(exclusions)==276 and all(int(r['reason_mask'],16)&5 for r in exclusions),'trace exclusions without abort/I/O')
 reason_counts={name:sum(bool(int(r['reason_mask'],16)&bit) for r in exclusions) for bit,name in [(1,'abort'),(2,'vector'),(4,'io'),(8,'odd_word')]}
 require(reason_counts==dict(abort=220,vector=206,io=62,odd_word=0),'trace exclusion accounting')
 stale_wait=0
 lines=iter((build/'trace_bit-vectors.txt').read_text().splitlines())
 for line in lines:
  h=line.split();ctrl=next(lines).split()
  for _ in range(int(h[11],16)):next(lines)
  post=next(lines).split()
  for _ in range(int(post[9],16)):next(lines)
  if int(h[1],16)==1 and int(ctrl[1],16) and int(ctrl[3],16):stale_wait+=1
 require(stale_wait==224,'raw C fWait audit')
 for name in ['trace_bit-excluded.csv','bus-fault-excluded.csv','bus-fault-continuation.csv','illegal-opcodes.txt']:
  (reports/('cp17-'+name+'.gz')).write_bytes(gzip.compress((build/name).read_bytes(),mtime=0))
 cp8=read_json(ROOT/'docs/benchmarks-cp8.json')
 require(compare_results('benchmarks',27)==cp8['rr_ram']['results'],'RR RAM benchmark regression')
 rr_fram=[dict(memory_mode=m,**r) for m in range(3) for r in compare_results(f'fram-benchmarks-{m}',9)]
 require(rr_fram==cp8['rr_fram']['results'],'RR FRAM benchmark regression')
 fields=[('ea',25,8,'ea_loop'),('cp9',69,9,'results'),('byte',20,10,'double_byte'),('single-byte',24,10,'single_byte'),
         ('control',8,11,'control_flow'),('extra',7,12,'extra'),('trap',6,13,'trap'),('irq',6,14,'irq'),('illegal',3,15,'illegal')]
 for stem,n,revision,field in fields:
  rows=[]
  for mode,label in MODES.items():
   for r in compare_results(f'{stem}-benchmarks-{mode}',n):
    cpi=r['microclocks']/r['instructions'];rows.append(dict(memory_mode=mode,memory_model=label,**r,microclocks_per_instruction=cpi,
      memory_beats_per_instruction=r['memory_beats']/r['instructions'],calculated_ips_at_29_56_mhz=29560000/cpi))
  baseline=read_json(ROOT/f'docs/benchmarks-cp{revision}.json')[field]
  if stem=='ea':baseline=baseline['results']
  require(rows==baseline,stem+': historical benchmark changed')
 trace_bench=[]
 for mode,label in MODES.items():
  for r in compare_results(f'trace-benchmarks-{mode}',5):
   cpi=r['microclocks']/r['instructions'];trace_bench.append(dict(memory_mode=mode,memory_model=label,**r,
      microclocks_per_instruction=cpi,memory_beats_per_instruction=r['memory_beats']/r['instructions'],calculated_ips_at_29_56_mhz=29560000/cpi))
 for name in ['cp17-trace-benchmarks-portable.log','cp17-vendor-trace.log']:
  for mode in [-1,0,1,2]:require(f'PASS trace benchmarks mode{mode}: 5' in logs[name],name)
 write_json(ROOT/'docs/benchmarks-cp17.json',dict(date='2026-09-09',clock_hz_nominal=29560000,spi_divider=1,
  method='Functional RTL simulation with portable and vendor DP8KC; no physical board measurement.',runs_each_rom_model=746,
  cp16_all_726_benchmark_runs_unchanged=True,trace=trace_bench,differential_cycles=cycles))
 for name in LOGS:shutil.copyfile(build/name,reports/name)
 for stem in ['missing-trace','rti-old-T','rtt-traces','trace-irq-ack']:
  require(f'PASS trace negative: {stem}' in logs['cp17-negative-controls.log'],'negative control not run')
  name=f'cp17-negative-{stem}.log';data=(build/name).read_bytes();require(b'FATAL' in data,'missing actual negative failure');(reports/name).write_bytes(data)
 original=(ROOT/'../core/core.c').read_bytes()
 from oracle_core import instrument
 from fault_oracle import instrument_faults
 for name,expected in [('oracle_core.c',instrument(original.decode())),('oracle_control_core.c',instrument(original.decode(),vectors=True)),('oracle_fault_core.c',instrument_faults(original.decode()))]:
  data=(build/name).read_bytes();require(data==expected.encode(),'oracle hooks changed');(reports/('cp17-'+name+'.gz')).write_bytes(gzip.compress(data,mtime=0))
 require(sha256(original)==read_json(ROOT/'docs/verification-cp16.json')['oracle']['original_core_sha256'],'original core changed')
 source=set()
 for pattern in ['rtl/*.v','microcode/*.uasm','microcode/generated/*','microasm/*.py','tb/*.v','tb/*.c','tb/*.h','tb/test_*.py',
                 'tools/*.py','reference/lsi11/*','synth/machxo2/*.v','synth/machxo2/*.lpf','synth/machxo2/*.sty']:
  source.update(p for p in ROOT.glob(pattern) if p.is_file())
 source.update(ROOT/p for p in ['Makefile','README.md','docs/trace-rtt.md','../core/core.c','../core/core.h','../core/hardware.c','../core/hardware.h','../core/pdp11_fp.c'])
 buffer=io.BytesIO()
 with tarfile.open(fileobj=buffer,mode='w') as a:
  for p in sorted(source):
   data=p.read_bytes();name=('core/'+p.name) if p.parent==ROOT/'../core' else 'uJ11-fpga/'+str(p.relative_to(ROOT))
   info=tarfile.TarInfo(name);info.size=len(data);a.addfile(info,io.BytesIO(data))
 (reports/'cp17-verification-source.tgz').write_bytes(gzip.compress(buffer.getvalue(),mtime=0))
 files=source|set(reports.glob('cp17*'))|{ROOT/'docs/benchmarks-cp17.json'}
 for name in FINAL:files.update([ROOT/'synth/reports'/name/'inputs.json',ROOT/'synth/reports'/name/'result.json'])
 write_json(ROOT/'docs/verification-cp17.json',dict(date='2026-09-09',encoding_version=10,microcode_words=452,
  profile='One kernel register set; CM=PM=RS=0; NZVC/IPL/T; no MMU.',
  checks=dict(python_test_methods=15,completed_dcj11_cases_each_memory_rom_pair=136165,cp16_completed_cases_unchanged=124969,
   new_trace_cases=11196,trace_candidates=11472,trace_exclusions=dict(total=276,**reason_counts),trace_system_cases=24,trace_fault_cases=140,
   bus_fault_frame_cases=32780,trace_benchmark_runs_each_rom_model=20,benchmark_runs_each_rom_model=746,
   decoder_encodings_checked=65536,decoder_supported=56269,only_new_opcode='000006',only_changed_microcode_address='037',labels_unchanged=True,
   synthesis_archives_verified=archives,raw_report_hashes_verified=raw,current_inputs_equal_synthesis=FINAL),
  oracle=dict(original_core_sha256=sha256(original),source_unchanged=True,raw_wait_flag_after_irq_cases=224),
  limits=['Trace candidates with abort/internal DCJ11 I/O are excluded and logged; T=1 fault priority has directed coverage.',
   'The existing 32780 memory-fault differential cases remain T=0 and stop at the vector-frame boundary.',
   'Raw internal C fWait remains set after initial WAIT accepts IRQ; it is recorded, and RTL completion uses the IRQ-frame boundary.',
   'No processor-mode/register-bank exchange, HALT/RESET or remaining PSW ISA, EIS, red/yellow stack recovery, I/O timeout, MMU.',
   'Fit includes probes and FRAM/IRQ resolver, not full UART/timer/panel/SD/RK or external pin timing; no board programming.'],
  fixtures_uncompressed_sha256=fixtures,vendor_models_sha256={p.name:sha256(p.read_bytes()) for p in (build/'vendor').glob('*.v')},
  files={str(p.relative_to(ROOT)):sha256(p.read_bytes()) for p in sorted(files) if p.is_file()}))
 print(f'CP17 recorded: {len(archives)} synthesis archives, {raw} raw hashes; 136165 completed cases and 32780 fault frames per RAM/FRAM/ROM pair; 746 benchmarks per ROM.')
if __name__=='__main__':main()
