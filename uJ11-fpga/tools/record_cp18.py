#!/usr/bin/env python3
"""Audit exact CP18e/f sources, all old results, new oracle cases and raw fits."""
import csv,gzip,io,importlib.util,shutil,tarfile
from pathlib import Path
from record_ea import ROOT,MODES,compare_results,read_json,require,sha256,write_json
from record_cp16 import COUNTS
from verify_cp18 import VENDOR,STEMS
FINAL=['cp18e','cp18f']
LOGS=['cp18-tests.log','cp18-benchmarks-portable.log','cp18-decode-equivalence.log','cp18-negative-controls.log','cp18-vendor-prepare.log']+['cp18-vendor-'+n+'.log' for n in VENDOR]
NEGATIVE=['missing-cc','clear-sets','set-clears','wrong-mfpt']
def main():
 build=ROOT/'build';reports=ROOT/'tb/reports'
 stats=read_json(ROOT/'microcode/generated/m0.stats.json')
 require((stats['encoding_version'],stats['used_words'],stats['word_bits'])==(10,493,36),'format/occupancy')
 logs={n:(build/n).read_text() for n in LOGS}
 for n,t in logs.items():require(t and not any(x in t for x in ['FATAL','FAILED','%Error','%Warning']),n)
 portable=logs['cp18-tests.log'];vendor='\n'.join(logs['cp18-vendor-'+n+'.log'] for n in VENDOR if n!='benchmarks')
 counts=dict(COUNTS,**{'bus-fault':(32780,213080),'trace_bit':(11196,58965),'system_flags':(21120,75504)})
 for t in [portable,vendor]:
  for suite,(n,_) in counts.items():
   for mode in [-1,2]:require(f'PASS {suite.replace("bus-fault","bus fault")} differential mode{mode}: {n}' in t,suite+': missing tests')
  for marker in ['PASS differential: 12928','PASS FRAM differential: 12928','PASS stream engine: opcode+3','PASS EA directed: 14','PASS single directed: 46','PASS byte I/O: 23','PASS single byte directed: 46','PASS control directed: 58','PASS extra directed: 36','PASS trap directed: 112','PASS IRQ directed: 104','PASS legacy IRQ: 5 scenarios, 6 settled-state checks','PASS illegal directed: 96','PASS bus fault system: 32','PASS bus double fault: 96','PASS trace system: 24','PASS trace faults: 140']:require(marker in t,marker)
 for marker in ['Ran 15 tests','4096 pair checks','PASS prefetch: 525','PASS lsi11 peripherals: 19','exactly 56302 supported encodings','PASS byte ALU: 2097152','PASS byte datapath: 16384','PASS IRQ adapter: 763','PASS fault IRQ oracle: 8']:require(marker in portable,marker)
 require('PASS CP18 decoder: all 65536 checked; only CC/NOP and MFPT differ from archived CP17a' in logs['cp18-decode-equivalence.log'],'decoder miter')
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
   if folder.name in FINAL:require(result['timing_pass'] and result['fully_routed'] and result['ebr']==4,'final fit')
  archives.append(folder.name)
 spec=importlib.util.spec_from_file_location('asm',ROOT/'microasm/uj11asm.py');asm=importlib.util.module_from_spec(spec);spec.loader.exec_module(asm)
 with tarfile.open(ROOT/'synth/reports/cp17a/source.tgz') as a:old_source=a.extractfile('microcode/m0.uasm').read().decode()
 old,lst,labels,_=asm.assemble(old_source);current,_,newlabels,_=asm.assemble((ROOT/'microcode/m0.uasm').read_text())
 old_addresses={int(l.split()[0],16) for l in lst.splitlines()}
 require(len(old_addresses)==452 and all(old[n]==current[n] for n in old_addresses),'old microinstructions changed')
 require(all(newlabels[k]==v for k,v in labels.items()),'old labels changed')
 expected_changed={0x17,*range(0x214,0x218),*(0x104+8*n+d for n in range(16) for d in [0,1])}
 require({i for i in range(1024) if old[i]!=current[i]}==expected_changed,'only 37 non-padding ROM words change')
 old_inputs=read_json(ROOT/'synth/reports/cp17a/inputs.json')['files']
 for n,h in old_inputs.items():
  if n.startswith('rtl/') and n!='rtl/uj11_decode.v':require(sha256((ROOT/n).read_bytes())==h,'unchanged RTL '+n)
 fixtures={};cycles={}
 for suite,(count,beats) in counts.items():
  cycles[suite]={}
  for mode,label in [(-1,'ram'),(2,'fram')]:
   data=(build/f'{suite}-cycles-{mode}.csv').read_bytes()
   require(data==(build/f'{suite}-cycles-{mode}-portable.csv').read_bytes(),suite+': portable/vendor CSV')
   rows=list(csv.DictReader(io.StringIO(data.decode())))
   require(len(rows)==count and [int(r['case']) for r in rows]==list(range(count)),suite+': IDs/count')
   require(sum(int(r['memory_beats']) for r in rows)==beats,suite+': beats')
   total=sum(int(r['microclocks']) for r in rows)
   if suite!='system_flags':require(data==gzip.decompress((reports/f'cp17-{suite}-cycles-{label}.csv.gz').read_bytes()),suite+': old cycles')
   else:require(total==(421120 if mode==-1 else 9099624),'new cycle baseline')
   cycles[suite][label]=dict(cases=count,microclocks=total,memory_beats=beats,portable_vendor_equal=True)
   (reports/f'cp18-{suite}-cycles-{label}.csv.gz').write_bytes(gzip.compress(data,mtime=0))
  data=(build/f'{suite}-vectors.txt').read_bytes()
  if suite!='system_flags':require(data==gzip.decompress((reports/f'cp17-{suite}-vectors.txt.gz').read_bytes()),suite+': old oracle fixture')
  fixtures[suite]=sha256(data);(reports/f'cp18-{suite}-vectors.txt.gz').write_bytes(gzip.compress(data,mtime=0))
  shutil.copyfile(build/f'{suite}-oracle.log',reports/f'cp18-{suite}-oracle.log')
 require('21120 completed cases; 0 excluded' in (build/'system_flags-oracle.log').read_text(),'new oracle count')
 require(not list(csv.DictReader((build/'system_flags-excluded.csv').open())),'new exclusions')
 old_candidates=set(gzip.decompress((reports/'cp17-illegal-opcodes.txt.gz').read_bytes()).splitlines());new_candidates=set((build/'illegal-opcodes.txt').read_bytes().splitlines())
 removed={f'{op:04x} 00000008'.encode() for op in [7,*range(0o240,0o300)]}
 require(old_candidates-new_candidates==removed and not new_candidates-old_candidates,'exactly 33 removed fallback encodings')
 require('4160 completed of 18468 candidates; 14308 excluded' in (build/'illegal-oracle.log').read_text(),'old exclusion accounting')
 for name in ['system_flags-excluded.csv','trace_bit-excluded.csv','bus-fault-excluded.csv','bus-fault-continuation.csv','illegal-opcodes.txt']:(reports/('cp18-'+name+'.gz')).write_bytes(gzip.compress((build/name).read_bytes(),mtime=0))
 cp8=read_json(ROOT/'docs/benchmarks-cp8.json')
 require(compare_results('benchmarks',27)==cp8['rr_ram']['results'],'RR RAM')
 require([dict(memory_mode=m,**r) for m in range(3) for r in compare_results(f'fram-benchmarks-{m}',9)]==cp8['rr_fram']['results'],'RR FRAM')
 def bench(stem,n):
  rows=[]
  for mode,label in MODES.items():
   for r in compare_results(f'{stem}-benchmarks-{mode}',n):
    cpi=r['microclocks']/r['instructions'];rows.append(dict(memory_mode=mode,memory_model=label,**r,microclocks_per_instruction=cpi,memory_beats_per_instruction=r['memory_beats']/r['instructions'],calculated_ips_at_29_56_mhz=29560000/cpi))
  return rows
 fields=[('ea',25,8,'ea_loop'),('cp9',69,9,'results'),('byte',20,10,'double_byte'),('single-byte',24,10,'single_byte'),('control',8,11,'control_flow'),('extra',7,12,'extra'),('trap',6,13,'trap'),('irq',6,14,'irq'),('illegal',3,15,'illegal'),('trace',5,17,'trace')]
 for stem,n,rev,field in fields:
  baseline=read_json(ROOT/f'docs/benchmarks-cp{rev}.json')[field]
  if stem=='ea':baseline=baseline['results']
  require(bench(stem,n)==baseline,stem+': historical benchmark')
 newbench=bench('system-flags',5)
 for log in ['cp18-benchmarks-portable.log','cp18-vendor-benchmarks.log']:
  for m in [-1,0,1,2]:require(f'PASS system flags benchmarks mode{m}: 5' in logs[log],log)
 for stem in STEMS:(reports/('cp18-'+stem+'.json.gz')).write_bytes(gzip.compress((build/(stem+'.json')).read_bytes(),mtime=0))
 write_json(ROOT/'docs/benchmarks-cp18.json',dict(date='2026-09-09',clock_hz_nominal=29560000,spi_divider=1,method='Functional RTL simulation with portable and vendor DP8KC; no board measurement.',runs_each_rom_model=766,cp17_all_746_benchmark_runs_unchanged=True,system_flags=newbench,differential_cycles=cycles))
 for n in LOGS:shutil.copyfile(build/n,reports/n)
 for n in NEGATIVE:
  require(f'PASS system flags negative: {n}' in logs['cp18-negative-controls.log'],'negative control')
  data=(build/f'cp18-negative-{n}.log').read_bytes();require(b'FATAL' in data,'negative failure missing');(reports/f'cp18-negative-{n}.log').write_bytes(data)
 original=(ROOT/'../core/core.c').read_bytes()
 from oracle_core import instrument
 from fault_oracle import instrument_faults
 for n,expected in [('oracle_core.c',instrument(original.decode())),('oracle_control_core.c',instrument(original.decode(),vectors=True)),('oracle_fault_core.c',instrument_faults(original.decode()))]:
  data=(build/n).read_bytes();require(data==expected.encode(),'oracle hooks changed');(reports/('cp18-'+n+'.gz')).write_bytes(gzip.compress(data,mtime=0))
 require(sha256(original)==read_json(ROOT/'docs/verification-cp17.json')['oracle']['original_core_sha256'],'original core changed')
 source=set()
 for pattern in ['rtl/*.v','microcode/*.uasm','microcode/generated/*','microasm/*.py','tb/*.v','tb/*.c','tb/*.h','tb/test_*.py','tools/*.py','reference/lsi11/*','synth/machxo2/*.v','synth/machxo2/*.lpf','synth/machxo2/*.sty']:source.update(p for p in ROOT.glob(pattern) if p.is_file())
 source.update(ROOT/p for p in ['Makefile','README.md','docs/system-flags.md','../core/core.c','../core/core.h','../core/hardware.c','../core/hardware.h','../core/pdp11_fp.c'])
 buffer=io.BytesIO()
 with tarfile.open(fileobj=buffer,mode='w') as a:
  for p in sorted(source):
   data=p.read_bytes();name=('core/'+p.name) if p.parent==ROOT/'../core' else 'uJ11-fpga/'+str(p.relative_to(ROOT));info=tarfile.TarInfo(name);info.size=len(data);a.addfile(info,io.BytesIO(data))
 (reports/'cp18-verification-source.tgz').write_bytes(gzip.compress(buffer.getvalue(),mtime=0))
 files=source|set(reports.glob('cp18*'))|{ROOT/'docs/benchmarks-cp18.json'}
 for n in FINAL:files.update([ROOT/'synth/reports'/n/'inputs.json',ROOT/'synth/reports'/n/'result.json'])
 write_json(ROOT/'docs/verification-cp18.json',dict(date='2026-09-09',encoding_version=10,microcode_words=493,profile='One kernel register set; CM=PM=RS=0; NZVC/IPL/T; no MMU.',checks=dict(python_test_methods=15,completed_dcj11_cases_each_memory_rom_pair=157285,cp17_completed_cases_unchanged=136165,new_system_flags_cases=21120,new_exclusions=0,bus_fault_frame_cases=32780,benchmark_runs_each_rom_model=766,new_benchmark_runs_each_rom_model=20,decoder_encodings_checked=65536,decoder_supported=56302,new_opcodes='000007 and 000240..000277 octal',old_words_and_labels_unchanged=452,new_assembled_words=41,new_non_padding_rom_words=37,synthesis_archives_verified=archives,raw_report_hashes_verified=raw,current_inputs_equal_synthesis=FINAL),oracle=dict(original_core_sha256=sha256(original),source_unchanged=True),limits=['Historical trace/abort/internal I/O exclusions and fault-frame comparison boundaries remain unchanged.','No register-bank/mode exchange, HALT/RESET, MFPS/MTPS, EIS, red/yellow stack recovery, I/O timeout or MMU.','Fit includes probes and FRAM/IRQ resolver, not full UART/timer/panel/SD/RK or external pin timing; no FPGA programming.'],fixtures_uncompressed_sha256=fixtures,vendor_models_sha256={p.name:sha256(p.read_bytes()) for p in (build/'vendor').glob('*.v')},files={str(p.relative_to(ROOT)):sha256(p.read_bytes()) for p in sorted(files) if p.is_file()}))
 print(f'CP18 recorded: {len(archives)} archives/{raw} raw report hashes; 157285 completed cases + 32780 fault frames per memory/ROM pair; 766 benchmarks per ROM.')
if __name__=='__main__':main()
