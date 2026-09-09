#!/usr/bin/env python3
"""Audit CP19 exact fit inputs, DCJ11 fixtures, exclusions and both ROM models."""
import csv,gzip,io,importlib.util,shutil,tarfile
from pathlib import Path
from record_ea import ROOT,MODES,compare_results,read_json,require,sha256,write_json
from record_cp16 import COUNTS
from verify_cp19 import VENDOR,STEMS
from verify_cp18 import STEMS as OLD_STEMS
FINAL=['cp19a','cp19b']
LOGS=['cp19-tests.log','cp19-benchmarks-portable.log','cp19-decode-equivalence.log','cp19-negative-controls.log','cp19-vendor-prepare.log']+['cp19-vendor-'+n+'.log' for n in VENDOR]
NEGATIVE=['missing-mfps','mfps-zero-extends','mtps-overwrites-t','mtps-old-ipl','mfps-flags-before-write']
NEW={'psw_transfer':(12852,54492),'psw-transfer-fault':(1008,6336)}
def main():
 build=ROOT/'build';reports=ROOT/'tb/reports'
 stats=read_json(ROOT/'microcode/generated/m0.stats.json')
 require((stats['encoding_version'],stats['used_words'],stats['word_bits'])==(10,507,36),'format/occupancy')
 logs={n:(build/n).read_text() for n in LOGS}
 for n,t in logs.items():require(t and not any(x in t for x in ['FATAL','FAILED','%Error','%Warning']),n)
 portable=logs['cp19-tests.log'];vendor='\n'.join(logs['cp19-vendor-'+n+'.log'] for n in VENDOR if n!='benchmarks')
 counts=dict(COUNTS,**{'bus-fault':(32780,213080),'trace_bit':(11196,58965),'system_flags':(21120,75504)},**NEW)
 for t in [portable,vendor]:
  for suite,(n,_) in counts.items():
   display={'bus-fault':'bus fault','psw-transfer-fault':'PSW-transfer fault'}.get(suite,suite)
   for mode in [-1,2]:require(f'PASS {display} differential mode{mode}: {n}' in t,suite+': missing tests')
  for marker in ['PASS differential: 12928','PASS FRAM differential: 12928','PASS stream engine: opcode+3','PASS EA directed: 14','PASS single directed: 46','PASS byte I/O: 23','PASS single byte directed: 46','PASS control directed: 58','PASS extra directed: 36','PASS trap directed: 112','PASS IRQ directed: 104','PASS legacy IRQ: 5 scenarios, 6 settled-state checks','PASS illegal directed: 96','PASS bus fault system: 32','PASS bus double fault: 96','PASS trace system: 24','PASS trace faults: 140']:require(marker in t,marker)
 for marker in ['Ran 15 tests','4096 pair checks','PASS prefetch: 525','PASS lsi11 peripherals: 19','exactly 56430 supported encodings','PASS byte ALU: 2097152','PASS byte datapath: 16384','PASS IRQ adapter: 763','PASS fault IRQ oracle: 8']:require(marker in portable,marker)
 require('PASS CP19 decoder: all 65536 checked; only MFPS and MTPS differ from archived CP18e' in logs['cp19-decode-equivalence.log'],'decoder miter')
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
 with tarfile.open(ROOT/'synth/reports/cp18e/source.tgz') as a:old_source=a.extractfile('microcode/m0.uasm').read().decode()
 old,lst,labels,_=asm.assemble(old_source);current,_,newlabels,_=asm.assemble((ROOT/'microcode/m0.uasm').read_text())
 old_addresses={int(l.split()[0],16) for l in lst.splitlines()}
 require(len(old_addresses)==493 and all(old[n]==current[n] for n in old_addresses),'old microinstructions changed')
 require(all(newlabels[k]==v for k,v in labels.items()),'old labels changed')
 changed={*range(0x194,0x19a),0x1a8,*range(0x1aa,0x1ad),0x1d8,*range(0x1da,0x1dd)}
 require({i for i in range(1024) if old[i]!=current[i]}==changed,'only 14 ROM words change')
 for gate in ['cp18e','cp18f']:
  for n,h in read_json(ROOT/f'synth/reports/{gate}/inputs.json')['files'].items():
   if n.startswith(('rtl/','reference/')) and n!='rtl/uj11_decode.v':require(sha256((ROOT/n).read_bytes())==h,'unchanged RTL '+n)
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
   if suite not in NEW:require(data==gzip.decompress((reports/f'cp18-{suite}-cycles-{label}.csv.gz').read_bytes()),suite+': old cycles')
   else:
    expected={'psw_transfer':(355470,6370712),'psw-transfer-fault':(33840,615848)}[suite][mode==2]
    require(total==expected,'new cycle baseline')
    if suite=='psw-transfer-fault':require(sum(int(r['repair_clocks']) for r in rows)==112,'fault repair cycles')
   cycles[suite][label]=dict(cases=count,microclocks=total,memory_beats=beats,portable_vendor_equal=True)
   (reports/f'cp19-{suite}-cycles-{label}.csv.gz').write_bytes(gzip.compress(data,mtime=0))
  data=(build/f'{suite}-vectors.txt').read_bytes()
  if suite not in NEW:require(data==gzip.decompress((reports/f'cp18-{suite}-vectors.txt.gz').read_bytes()),suite+': old oracle fixture')
  fixtures[suite]=sha256(data);(reports/f'cp19-{suite}-vectors.txt.gz').write_bytes(gzip.compress(data,mtime=0))
  shutil.copyfile(build/f'{suite}-oracle.log',reports/f'cp19-{suite}-oracle.log')
 require('12852 completed of 13056 candidates; 204 excluded. Reasons abort=104 vector=98 I/O=117 odd=0 stack=10' in (build/'psw_transfer-oracle.log').read_text(),'new oracle counts')
 excluded=list(csv.DictReader((build/'psw_transfer-excluded.csv').open()))
 reasons=[sum(bool(int(r['reason_mask'],16)&(1<<k)) for r in excluded) for k in range(5)]
 require(len(excluded)==204 and reasons==[104,98,117,0,10],'exclusion audit')
 require('1008 completed frames (912 ACK error, 96 odd-word), 0 excluded' in (build/'psw-transfer-fault-oracle.log').read_text(),'fault oracle count')
 for n in ['psw-transfer-fault-excluded','psw-transfer-fault-continuation']:require(not list(csv.DictReader((build/(n+'.csv')).open())),n)
 # Check generated fixture coverage without calculating any expected ISA result.
 from check_psw_transfer_negative import records
 items=list(records('psw_transfer'));require(len(items)==12852,'PSW fixture count')
 mfps_psw={int(h[2],16) for h,c,t in items if int(h[1],16)==0o106700 and int(c[0],16)==0}
 require(mfps_psw==set(range(256)),'MFPS all low PSW values')
 mtps_states={(int(h[3],16)&255,(int(h[2],16)>>5)&7,(int(h[2],16)>>4)&1,int(c[0],16)!=0) for h,c,t in items if int(h[1],16)==0o106400}
 require(mtps_states=={(v,p,t,i) for v in range(256) for p in range(8) for t in range(2) for i in [False,True]},'MTPS value/IPL/T/IRQ coverage')
 for op in [0o106700,0o106400]:
  pairs={((int(h[1],16)>>3)&7,int(h[1],16)&7) for h,c,t in items if (int(h[1],16)&0o177700)==op}
  require(pairs=={(m,r) for m in range(8) for r in range(8)},'EA mode/register coverage')
 old_candidates=set(gzip.decompress((reports/'cp18-illegal-opcodes.txt.gz').read_bytes()).splitlines());new_candidates=set((build/'illegal-opcodes.txt').read_bytes().splitlines())
 removed={f'{op:04x} 00000008'.encode() for base in [0o106400,0o106700] for op in range(base,base+64)}
 require(old_candidates-new_candidates==removed and not new_candidates-old_candidates,'exactly 128 removed fallback encodings')
 require('4160 completed of 18212 candidates; 14052 excluded' in (build/'illegal-oracle.log').read_text(),'fallback exclusion accounting')
 for name in ['psw_transfer-excluded.csv','psw-transfer-fault-excluded.csv','psw-transfer-fault-continuation.csv','system_flags-excluded.csv','trace_bit-excluded.csv','bus-fault-excluded.csv','bus-fault-continuation.csv','illegal-opcodes.txt']:(reports/('cp19-'+name+'.gz')).write_bytes(gzip.compress((build/name).read_bytes(),mtime=0))
 old_runs=0
 for stem in OLD_STEMS:
  old=read_json_gzip(reports/('cp18-'+stem+'.json.gz'))
  require(compare_results(stem,len(old))==old,stem+': historical benchmark');old_runs+=len(old)
 require(old_runs==766,'historical benchmark count')
 newbench=[]
 for mode,label in MODES.items():
  for r in compare_results(f'psw-transfer-benchmarks-{mode}',5):
   cpi=r['microclocks']/r['instructions'];newbench.append(dict(memory_mode=mode,memory_model=label,**r,microclocks_per_instruction=cpi,memory_beats_per_instruction=r['memory_beats']/r['instructions'],calculated_ips_at_29_56_mhz=29560000/cpi))
 for log in ['cp19-benchmarks-portable.log','cp19-vendor-benchmarks.log']:
  for m in [-1,0,1,2]:require(f'PASS PSW transfer benchmarks mode{m}: 5' in logs[log],log)
 for stem in STEMS:(reports/('cp19-'+stem+'.json.gz')).write_bytes(gzip.compress((build/(stem+'.json')).read_bytes(),mtime=0))
 write_json(ROOT/'docs/benchmarks-cp19.json',dict(date='2026-09-09',clock_hz_nominal=29560000,spi_divider=1,method='Functional RTL simulation with portable and vendor DP8KC; no board measurement.',runs_each_rom_model=786,cp18_all_766_benchmark_runs_unchanged=True,psw_transfer=newbench,differential_cycles=cycles))
 for n in LOGS:shutil.copyfile(build/n,reports/n)
 for n in NEGATIVE:
  require(f'PASS PSW transfer negative: {n}' in logs['cp19-negative-controls.log'],'negative control')
  data=(build/f'cp19-negative-{n}.log').read_bytes();require(b'FATAL' in data,'negative failure missing');(reports/f'cp19-negative-{n}.log').write_bytes(data)
 original=(ROOT/'../core/core.c').read_bytes()
 from oracle_core import instrument
 from fault_oracle import instrument_faults
 for n,expected in [('oracle_core.c',instrument(original.decode())),('oracle_control_core.c',instrument(original.decode(),vectors=True)),('oracle_fault_core.c',instrument_faults(original.decode()))]:
  data=(build/n).read_bytes();require(data==expected.encode(),'oracle hooks changed');(reports/('cp19-'+n+'.gz')).write_bytes(gzip.compress(data,mtime=0))
 baseline=read_json(ROOT/'docs/verification-cp18.json')
 require(sha256(original)==baseline['oracle']['original_core_sha256'],'original core changed')
 for n,h in baseline['files'].items():
  if n.startswith('../core/'):require(sha256((ROOT/n).read_bytes())==h,'original source changed '+n)
 source=set()
 for pattern in ['docs/*.md','rtl/*.v','microcode/*.uasm','microcode/generated/*','microasm/*.py','tb/*.v','tb/*.c','tb/*.h','tb/test_*.py','tools/*.py','reference/lsi11/*','synth/machxo2/*.v','synth/machxo2/*.lpf','synth/machxo2/*.sty']:source.update(p for p in ROOT.glob(pattern) if p.is_file())
 source.update(ROOT/p for p in ['Makefile','README.md','docs/psw-transfer.md','../core/core.c','../core/core.h','../core/hardware.c','../core/hardware.h','../core/pdp11_fp.c'])
 buffer=io.BytesIO()
 with tarfile.open(fileobj=buffer,mode='w') as a:
  for p in sorted(source):
   data=p.read_bytes();name=('core/'+p.name) if p.parent==ROOT/'../core' else 'uJ11-fpga/'+str(p.relative_to(ROOT));info=tarfile.TarInfo(name);info.size=len(data);a.addfile(info,io.BytesIO(data))
 (reports/'cp19-verification-source.tgz').write_bytes(gzip.compress(buffer.getvalue(),mtime=0))
 files=source|set(reports.glob('cp19*'))|{ROOT/'docs/benchmarks-cp19.json'}
 for n in FINAL:files.update([ROOT/'synth/reports'/n/'inputs.json',ROOT/'synth/reports'/n/'result.json'])
 write_json(ROOT/'docs/verification-cp19.json',dict(date='2026-09-09',encoding_version=10,microcode_words=507,profile='One kernel register set; CM=PM=RS=0; NZVC/IPL/T; no MMU.',checks=dict(python_test_methods=15,completed_dcj11_cases_each_memory_rom_pair=170137,cp18_completed_cases_unchanged=157285,new_psw_transfer_cases=12852,new_candidates=13056,new_exclusions=204,exclusion_reasons_overlap=dict(zip(['abort','vector','io','odd','stack'],reasons)),bus_fault_frame_cases=33788,old_bus_fault_frames=32780,new_bus_fault_frames=1008,new_fault_ack_cases=912,new_fault_odd_word_cases=96,new_fault_exclusions=0,benchmark_runs_each_rom_model=786,new_benchmark_runs_each_rom_model=20,decoder_encodings_checked=65536,decoder_supported=56430,new_opcodes='106400..106477 and 106700..106777 octal',old_words_and_labels_unchanged=493,new_assembled_words=14,new_non_padding_rom_words=14,synthesis_archives_verified=archives,raw_report_hashes_verified=raw,current_inputs_equal_synthesis=FINAL),oracle=dict(original_core_sha256=sha256(original),source_unchanged=True),limits=['204 new primary-C cases excluded by explicit abort/I/O/stack status; excluded cases are not called compatible.','Fault comparison ends at completed vector004 frame; SPI errors are injected at the logical slave interface.','No register-bank/mode exchange, HALT/RESET, EIS, red/yellow stack recovery, I/O timeout or MMU.','Fit includes probes and FRAM/IRQ resolver, not full UART/timer/panel/SD/RK or external pin timing; no FPGA programming.'],fixtures_uncompressed_sha256=fixtures,vendor_models_sha256={p.name:sha256(p.read_bytes()) for p in (build/'vendor').glob('*.v')},files={str(p.relative_to(ROOT)):sha256(p.read_bytes()) for p in sorted(files) if p.is_file()}))
 print(f'CP19 recorded: {len(archives)} archives/{raw} raw report hashes; 170137 completed cases + 33788 fault frames per memory/ROM pair; 786 benchmarks per ROM.')
def read_json_gzip(path):
 import json
 return json.loads(gzip.decompress(path.read_bytes()))
if __name__=='__main__':main()
