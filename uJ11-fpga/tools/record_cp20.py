#!/usr/bin/env python3
"""Audit CP20 exact fit inputs, DCJ11 fixtures, exclusions and both ROM models."""
import csv,gzip,io,importlib.util,shutil,tarfile
from pathlib import Path
from record_ea import ROOT,MODES,compare_results,read_json,require,sha256,write_json
from record_cp16 import COUNTS
from verify_cp20 import VENDOR,STEMS
from verify_cp19 import STEMS as OLD_STEMS
FINAL=['cp20c','cp20d']
LOGS=['cp20-tests.log','cp20-benchmarks-portable.log','cp20-decode-equivalence.log','cp20-negative-controls.log','cp20-vendor-prepare.log']+['cp20-vendor-'+n+'.log' for n in VENDOR]
NEGATIVE=['unregistered-reset','missing-reset','reset-no-pulse','halt-common-trap','halt-odd-pc','halt-wrong-psw']
NEW={'system_control':(6144,26368)}
def main():
 build=ROOT/'build';reports=ROOT/'tb/reports'
 require(not list((ROOT/'rtl').glob('*mmu*')),'unexpected MMU RTL')
 stats=read_json(ROOT/'microcode/generated/m0.stats.json')
 require((stats['encoding_version'],stats['used_words'],stats['word_bits'])==(11,521,36),'format/occupancy')
 logs={n:(build/n).read_text() for n in LOGS}
 for n,t in logs.items():require(t and not any(x in t for x in ['FATAL','FAILED','%Error','%Warning']),n)
 portable=logs['cp20-tests.log'];vendor='\n'.join(logs['cp20-vendor-'+n+'.log'] for n in VENDOR if n!='benchmarks')
 counts=dict(COUNTS,**{'bus-fault':(32780,213080),'trace_bit':(11196,58965),'system_flags':(21120,75504),'psw_transfer':(12852,54492),'psw-transfer-fault':(1008,6336)},**NEW)
 for t in [portable,vendor]:
  for suite,(n,_) in counts.items():
   display={'bus-fault':'bus fault','psw-transfer-fault':'PSW-transfer fault'}.get(suite,suite)
   for mode in [-1,2]:require(f'PASS {display} differential mode{mode}: {n}' in t,suite+': missing tests')
  for marker in ['PASS differential: 12928','PASS FRAM differential: 12928','PASS stream engine: opcode+3','PASS EA directed: 14','PASS single directed: 46','PASS byte I/O: 23','PASS single byte directed: 46','PASS control directed: 58','PASS extra directed: 36','PASS trap directed: 112','PASS IRQ directed: 104','PASS legacy IRQ: 5 scenarios, 6 settled-state checks','PASS illegal directed: 96','PASS bus fault system: 32','PASS bus double fault: 96','PASS trace system: 24','PASS trace faults: 140','PASS system control legacy: 3 scenarios','PASS system control faults: 64']:require(marker in t,marker)
 for marker in ['Ran 16 tests','4096 pair checks','PASS prefetch: 525','PASS lsi11 peripherals: 19','exactly 56432 supported encodings','PASS byte ALU: 2097152','PASS byte datapath: 16384','PASS IRQ adapter: 763','PASS fault IRQ oracle: 8']:require(marker in portable,marker)
 require('PASS CP20 decoder: all 65536 checked; only HALT and RESET differ from archived CP19a' in logs['cp20-decode-equivalence.log'],'decoder miter')
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
 with tarfile.open(ROOT/'synth/reports/cp19a/source.tgz') as a:old_source=a.extractfile('microcode/m0.uasm').read().decode()
 old,lst,labels,_=asm.assemble(old_source);current,_,newlabels,_=asm.assemble((ROOT/'microcode/m0.uasm').read_text())
 old_addresses={int(l.split()[0],16) for l in lst.splitlines()}
 require(len(old_addresses)==507 and all(old[n]==current[n] for n in old_addresses),'old microinstructions changed')
 require(all(newlabels[k]==v for k,v in labels.items()),'old labels changed')
 changed={0x018,*range(0x021,0x024),*range(0x3e9,0x3f3)}
 require({i for i in range(1024) if old[i]!=current[i]}==changed,'only 14 ROM words change')
 for gate in ['cp19a','cp19b']:
  with tarfile.open(ROOT/f'synth/reports/{gate}/source.tgz') as archive:
   for n,h in read_json(ROOT/f'synth/reports/{gate}/inputs.json')['files'].items():
    if n.startswith(('rtl/','reference/')) and n!='rtl/uj11_decode.v':
     data=(ROOT/n).read_text()
     if n in ['rtl/uj11_engine.v','rtl/uj11_core.v','rtl/uj11_fram_system.v']:
      data=data.replace('irq_ack, waiting, peripheral_reset,','irq_ack, waiting,').replace('.peripheral_reset(peripheral_reset),','').replace('    output reg peripheral_reset,\n','')
      if n=='rtl/uj11_engine.v':
       addition="    // Register JUMP.init for peripherals with asynchronous reset inputs.\n    // Pulse covers the following settling word; IRQ samples after release.\n    always @(posedge clk) begin\n        if (reset) peripheral_reset <= 0;\n        else peripheral_reset <= running && control && command==4'd0 && uword[0];\n    end\n"
       require(data.count(addition)==1,'reset output logic');data=data.replace(addition,'')
     require(data.encode()==archive.extractfile(n).read(),'unchanged functional RTL '+n)
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
   if suite not in NEW:require(data==gzip.decompress((reports/f'cp19-{suite}-cycles-{label}.csv.gz').read_bytes()),suite+': old cycles')
   else:
    expected=(137216,3055232)[mode==2]
    require(total==expected,'new cycle baseline')
   cycles[suite][label]=dict(cases=count,microclocks=total,memory_beats=beats,portable_vendor_equal=True)
   (reports/f'cp20-{suite}-cycles-{label}.csv.gz').write_bytes(gzip.compress(data,mtime=0))
  data=(build/f'{suite}-vectors.txt').read_bytes()
  if suite not in NEW:require(data==gzip.decompress((reports/f'cp19-{suite}-vectors.txt.gz').read_bytes()),suite+': old oracle fixture')
  fixtures[suite]=sha256(data);(reports/f'cp20-{suite}-vectors.txt.gz').write_bytes(gzip.compress(data,mtime=0))
  shutil.copyfile(build/f'{suite}-oracle.log',reports/f'cp20-{suite}-oracle.log')
 require('6144 completed of 6144 candidates; 0 excluded. Reasons abort=0 vector=0 I/O=0 odd=0 stack=0' in (build/'system_control-oracle.log').read_text(),'new oracle counts')
 require(not list(csv.DictReader((build/'system_control-excluded.csv').open())),'no hidden exclusions')
 from check_psw_transfer_negative import records
 items=list(records('system_control'));require(len(items)==6144,'system fixture count')
 coverage={(int(h[1],16),int(h[2],16),int(c[0],16)>>9,int(c[5],16)) for h,c,t in items}
 expected={(op,p,i,clear) for p in range(256) for i in range(8) for op,clear in [(0,0),(5,0),(5,1)]}
 require(coverage==expected,'HALT/RESET all PSW/IPL/IRQ/reset-policy combinations')
 require(all(int(c[4],16)==(int(h[1],16)==5) for h,c,t in items),'actual C RESET callback counts')
 ideal=list(csv.DictReader((build/'system_control-cycles--1.csv').open()))
 standalone={}
 for op,clocks in [(0,12),(5,4)]:
  ids={int(h[0],16) for h,c,t in items if int(h[1],16)==op and int(h[2],16)&16==0 and int(c[1],16)==0}
  measured={int(row['microclocks']) for row in ideal if int(row['case']) in ids and int(row['wait_clocks'])==0}
  require(measured=={clocks},'standalone ideal CPI');standalone['HALT' if op==0 else 'RESET']=clocks
 for op in [0,5]:
  require({int(c[2],16) for h,c,t in items if int(h[1],16)==op}=={1},'one instruction per fixture')
 old_candidates=set(gzip.decompress((reports/'cp19-illegal-opcodes.txt.gz').read_bytes()).splitlines());new_candidates=set((build/'illegal-opcodes.txt').read_bytes().splitlines())
 removed={f'{op:04x} 00000008'.encode() for op in [0,5]}
 require(old_candidates-new_candidates==removed and not new_candidates-old_candidates,'exactly two removed fallback encodings')
 require('4160 completed of 18208 candidates; 14048 excluded' in (build/'illegal-oracle.log').read_text(),'fallback exclusion accounting')
 for name in ['system_control-excluded.csv','psw_transfer-excluded.csv','psw-transfer-fault-excluded.csv','psw-transfer-fault-continuation.csv','system_flags-excluded.csv','trace_bit-excluded.csv','bus-fault-excluded.csv','bus-fault-continuation.csv','illegal-opcodes.txt']:(reports/('cp20-'+name+'.gz')).write_bytes(gzip.compress((build/name).read_bytes(),mtime=0))
 old_runs=0
 for stem in OLD_STEMS:
  old=read_json_gzip(reports/('cp19-'+stem+'.json.gz'))
  require(compare_results(stem,len(old))==old,stem+': historical benchmark');old_runs+=len(old)
 require(old_runs==786,'historical benchmark count')
 newbench=[]
 for mode,label in MODES.items():
  for r in compare_results(f'system-control-benchmarks-{mode}',2):
   cpi=r['microclocks']/r['instructions'];newbench.append(dict(memory_mode=mode,memory_model=label,**r,microclocks_per_instruction=cpi,memory_beats_per_instruction=r['memory_beats']/r['instructions'],calculated_ips_at_29_56_mhz=29560000/cpi))
 for log in ['cp20-benchmarks-portable.log','cp20-vendor-benchmarks.log']:
  for m in [-1,0,1,2]:require(f'PASS system control benchmarks mode{m}: 2' in logs[log],log)
 for stem in STEMS:(reports/('cp20-'+stem+'.json.gz')).write_bytes(gzip.compress((build/(stem+'.json')).read_bytes(),mtime=0))
 write_json(ROOT/'docs/benchmarks-cp20.json',dict(date='2026-09-09',clock_hz_nominal=29560000,spi_divider=1,method='Functional RTL simulation with portable and vendor DP8KC; no board measurement.',runs_each_rom_model=794,cp19_all_786_benchmark_runs_unchanged=True,system_control=newbench,standalone_ideal_clocks_including_fetch=standalone,differential_cycles=cycles))
 for n in LOGS:shutil.copyfile(build/n,reports/n)
 for n in NEGATIVE:
  require(f'PASS system control negative: {n}' in logs['cp20-negative-controls.log'],'negative control')
  data=(build/f'cp20-negative-{n}.log').read_bytes();require(b'FATAL' in data,'negative failure missing');(reports/f'cp20-negative-{n}.log').write_bytes(data)
 original=(ROOT/'../core/core.c').read_bytes()
 from oracle_core import instrument
 from fault_oracle import instrument_faults
 for n,expected in [('oracle_core.c',instrument(original.decode())),('oracle_control_core.c',instrument(original.decode(),vectors=True)),('oracle_fault_core.c',instrument_faults(original.decode()))]:
  data=(build/n).read_bytes();require(data==expected.encode(),'oracle hooks changed');(reports/('cp20-'+n+'.gz')).write_bytes(gzip.compress(data,mtime=0))
 baseline=read_json(ROOT/'docs/verification-cp19.json')
 require(sha256(original)==baseline['oracle']['original_core_sha256'],'original core changed')
 for n,h in baseline['files'].items():
  if n.startswith('../core/'):require(sha256((ROOT/n).read_bytes())==h,'original source changed '+n)
 source=set()
 for pattern in ['docs/*.md','rtl/*.v','microcode/*.uasm','microcode/generated/*','microasm/*.py','tb/*.v','tb/*.c','tb/*.h','tb/test_*.py','tools/*.py','reference/lsi11/*','synth/machxo2/*.v','synth/machxo2/*.lpf','synth/machxo2/*.sty']:source.update(p for p in ROOT.glob(pattern) if p.is_file())
 source.update(ROOT/p for p in ['Makefile','README.md','docs/system-control.md','../core/core.c','../core/core.h','../core/hardware.c','../core/hardware.h','../core/pdp11_fp.c'])
 buffer=io.BytesIO()
 with tarfile.open(fileobj=buffer,mode='w') as a:
  for p in sorted(source):
   data=p.read_bytes();name=('core/'+p.name) if p.parent==ROOT/'../core' else 'uJ11-fpga/'+str(p.relative_to(ROOT));info=tarfile.TarInfo(name);info.size=len(data);a.addfile(info,io.BytesIO(data))
 (reports/'cp20-verification-source.tgz').write_bytes(gzip.compress(buffer.getvalue(),mtime=0))
 files=source|set(reports.glob('cp20*'))|{ROOT/'docs/benchmarks-cp20.json'}
 for n in FINAL:files.update([ROOT/'synth/reports'/n/'inputs.json',ROOT/'synth/reports'/n/'result.json'])
 write_json(ROOT/'docs/verification-cp20.json',dict(date='2026-09-09',encoding_version=11,microcode_words=521,profile='One kernel register set; CM=PM=RS=0; NZVC/IPL/T; project DCJ11 HALT restart profile; no MMU.',checks=dict(python_test_methods=16,completed_dcj11_cases_each_memory_rom_pair=176281,cp19_completed_cases_unchanged=170137,new_system_control_cases=6144,new_candidates=6144,new_exclusions=0,bus_fault_frame_cases=33788,old_bus_fault_frames=33788,new_halt_directed_fault_frames=64,new_actual_peripheral_reset_scenarios=3,registered_reset_output=True,reset_phase_negative_control=True,negative_controls=NEGATIVE,new_state_ff=1,new_probe_observation_ff=1,rejected_synthesis_variants=['cp20a','cp20b'],benchmark_runs_each_rom_model=794,new_benchmark_runs_each_rom_model=8,decoder_encodings_checked=65536,decoder_supported=56432,new_opcodes='000000 HALT and 000005 RESET octal',old_words_and_labels_unchanged=507,new_assembled_words=14,new_non_padding_rom_words=14,synthesis_archives_verified=archives,raw_report_hashes_verified=raw,current_inputs_equal_synthesis=FINAL),oracle=dict(original_core_sha256=sha256(original),source_unchanged=True),limits=['HALT follows the existing project emulator restart profile, not the native DCJ11 console ODT/halt-option architecture.','HALT frame errors are directed terminal-double-fault checks, not DCJ11 abort equivalence. Existing CP19 exclusions remain explicit.','RESET is a one-cycle synchronous peripheral pulse; no physical bus INIT pulse timing or external PIRQ register implemented.','No register-bank/mode exchange, EIS, red/yellow stack recovery, I/O timeout or MMU.','Fit includes probes and FRAM/IRQ resolver, not full UART/timer/panel/SD/RK or external pin timing; no FPGA programming.'],fixtures_uncompressed_sha256=fixtures,vendor_models_sha256={p.name:sha256(p.read_bytes()) for p in (build/'vendor').glob('*.v')},files={str(p.relative_to(ROOT)):sha256(p.read_bytes()) for p in sorted(files) if p.is_file()}))
 print(f'CP20 recorded: {len(archives)} archives/{raw} raw report hashes; 176281 completed cases + 33788 fault frames per memory/ROM pair; 64 HALT faults; 794 benchmarks per ROM.')

def read_json_gzip(path):
 import json
 return json.loads(gzip.decompress(path.read_bytes()))
if __name__=='__main__':main()
