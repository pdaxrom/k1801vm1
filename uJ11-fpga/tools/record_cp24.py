#!/usr/bin/env python3
"""Archive CP24 only after XOR checks and exact preservation of CP23 results."""
import csv,gzip,io,json,re,shutil,tarfile,sys
from pathlib import Path
from record_ea import ROOT,read_json,write_json,sha256,require
from verify_cp24 import SUITES,STEMS,VENDOR,OLD_SUITES,OLD_STEMS
FINAL=['cp24a','cp24b']
NEW_COUNTS={'eis_xor':11028,'eis-xor-fault':5760}
LOGS=['cp24-tests.log','cp24-core-tests.log','cp24-decode-equivalence.log',
      'cp24-xor-oracle.log','cp24-negative-controls.log','cp24-benchmarks-portable.log',
      'cp24-vendor-prepare.log']+['cp24-vendor-'+n+'.log' for n in VENDOR]

def main():
    build=ROOT/'build';reports=ROOT/'tb/reports'
    baseline=read_json(ROOT/'docs/verification-cp23.json')
    oldcycles=read_json(ROOT/'docs/benchmarks-cp23.json')['differential_cycles']
    require(set(oldcycles)==set(OLD_SUITES),'old suite inventory')
    require(not list((ROOT/'rtl').glob('*mmu*')),'MMU RTL')
    stats=read_json(ROOT/'microcode/generated/m0.stats.json')
    require((stats['encoding_version'],stats['used_words'],stats['word_bits'])==(11,597,36),'microstore format')
    changed={'rtl/uj11_decode.v','microcode/m0.uasm','tb/tb_decode.v','tb/test_microasm.py',
             'tb/tb_trace_bit.v','tb/tb_bus_fault.v'}
    changed.update('microcode/generated/'+n for n in ['m0.mem','m0.lst','m0.labels.json','m0.stats.json','uj11_m0_ebr.v'])
    for n,h in baseline['files'].items():
        if n.startswith(('rtl/','microcode/','microasm/','reference/lsi11/','tb/','../core/','../tests/')) and not n.startswith('tb/reports/') and n not in changed:
            require(sha256((ROOT/n).read_bytes())==h,'unintended source change '+n)
    require({str(p.relative_to(ROOT)) for p in (ROOT/'rtl').glob('*.v')}=={n for n in baseline['files'] if n.startswith('rtl/') and n.endswith('.v')},'RTL inventory')
    with tarfile.open(reports/'cp23-verification-source.tgz') as a:
        def old(n):return a.extractfile('uJ11-fpga/'+n).read().decode()
        sys.path.insert(0,str(ROOT/'microasm'));from uj11asm import assemble
        gold,_,goldlabels,goldstats=assemble(old('microcode/m0.uasm'))
        gate,_,labels,_=assemble((ROOT/'microcode/m0.uasm').read_text())
        added=[i for i,(x,y) in enumerate(zip(gold,gate)) if x!=y]
        require(added==[0x02c,0x02e,0x02f,0x094,0x095],'unexpected ROM changes')
        require(all(labels[n]==v for n,v in goldlabels.items()),'old label address changed')
        # The added suite selector must not alter any old testbench behavior.
        t=(ROOT/'tb/tb_trace_bit.v').read_text().replace(', parameter integer EIS_XOR=0','').replace('if(EIS_XOR)suite="eis_xor";else ','')
        require(t==old('tb/tb_trace_bit.v'),'old normal testbench modified')
        t=(ROOT/'tb/tb_bus_fault.v').read_text().replace(', parameter integer EIS_XOR=0','').replace('if(EIS_XOR)begin suite="eis-xor-fault";display_suite="XOR fault";end\n        else ','')
        require(t==old('tb/tb_bus_fault.v'),'old fault testbench modified')
    logs={n:(build/n).read_text() for n in LOGS}
    for n,t in logs.items():require(t and not any(x in t for x in ['FATAL','FAILED','Traceback','%Error','%Warning','ERROR:']),n)
    require('PASS CP24 decoder: all 65536 checked; only XOR differ from archived CP23c' in logs['cp24-decode-equivalence.log'],'decoder miter')
    require('All core instruction tests passed' in logs['cp24-core-tests.log'],'C regression')
    oracle=read_json(build/'cp24-xor-oracle.json')
    require((oracle['independent_ordinary_cases'],oracle['independent_alias_cases'],oracle['normal_fault_union'])==(4698,385,512),'independent oracle counts')
    negatives=read_json(build/'cp24-negative-controls.json')
    require([r['name'] for r in negatives]==['missing-opcode','inclusive-or','wrong-source','lost-carry','set-overflow','early-source','flags-before-write','byte-write'] and all(r['rejected'] for r in negatives),'negative controls')
    for item in negatives:
        n='cp24-negative-'+item['name']+'.log';s=(build/n).read_text()
        require('FATAL' in s and ('trace case' in s or 'fault case' in s or 'beat1 got' in s or 'beat2 got' in s),'negative mismatch '+n)
        shutil.copyfile(build/n,reports/n)
    for n in ['cp24-xor-oracle.json','cp24-negative-controls.json']:shutil.copyfile(build/n,reports/n)
    portable=logs['cp24-tests.log'];vendor='\n'.join(logs['cp24-vendor-'+n+'.log'] for n in VENDOR)
    for text in [portable,vendor]:
        for suite in SUITES:
            display={'bus-fault':'bus fault','psw-transfer-fault':'PSW-transfer fault','eis-ashc-fault':'ASHC fault','eis-ash-fault':'ASH fault','eis-xor-fault':'XOR fault'}.get(suite,suite)
            count=NEW_COUNTS[suite] if suite in NEW_COUNTS else oldcycles[suite]['ram']['cases']
            for mode in [-1,2]:require(f'PASS {display} differential mode{mode}: {count}' in text,'suite missing '+suite)
        for marker in ['PASS differential: 12928','PASS FRAM differential: 12928','PASS stream engine: opcode+3','PASS EA directed: 14','PASS single directed: 46','PASS byte I/O: 23','PASS single byte directed: 46','PASS control directed: 58','PASS extra directed: 36','PASS trap directed: 112','PASS IRQ directed: 104','PASS legacy IRQ: 5 scenarios, 6 settled-state checks','PASS illegal directed: 96','PASS bus fault system: 32','PASS bus double fault: 96','PASS trace system: 24','PASS trace faults: 140','PASS system control legacy: 3 scenarios','PASS system control faults: 64','PASS XOR CSR: 1024']:require(marker in text,marker)
    for marker in ['Ran 16 tests','4096 pair checks','PASS prefetch: 525','PASS lsi11 peripherals: 19','exactly 57968 supported encodings','PASS byte ALU: 2097152','PASS byte datapath: 16384','PASS IRQ adapter: 763','PASS fault IRQ oracle: 8']:require(marker in portable,marker)
    archives=[];raw=0;selected={}
    for folder in sorted((ROOT/'synth/reports').iterdir()):
        info=read_json(folder/'inputs.json')
        with tarfile.open(folder/'source.tgz') as a:
            members={m.name.removeprefix('./'):m for m in a.getmembers()}
            for n,h in info['files'].items():
                data=(folder/n.split(':',1)[1]).read_bytes() if n.startswith('generated:') else a.extractfile(members[n]).read()
                require(sha256(data)==h,'archive '+folder.name+'/'+n)
                if folder.name in FINAL and not n.startswith('generated:'):require(sha256((ROOT/n).read_bytes())==h,'current fit input '+n)
        if 'placement' in info:require(sha256((folder/'design.ncd').read_bytes())==info['placement']['ncd_sha256'],'placement')
        if (folder/'result.json').exists():
            result=read_json(folder/'result.json')
            for n,h in result.get('reports',{}).items():require(sha256((folder/('design'+(Path(n).suffix or n))).read_bytes())==h,'raw report');raw+=1
            if folder.name in FINAL:
                require(result['timing_pass'] and result['fully_routed'] and result['ebr']==4,'selected fit')
                selected[folder.name]={k:result[k] for k in ['lut4','ff','ebr','fmax_mhz','constraint_mhz','scope']}
        archives.append(folder.name)
    require((selected['cp24a']['lut4'],selected['cp24a']['ff'],selected['cp24b']['lut4'],selected['cp24b']['ff'])==(854,299,1089,416),'resource result')
    fit_delta={}
    for previous,current in [('cp23c','cp24a'),('cp23d','cp24b')]:
        a=read_json(ROOT/'synth/reports'/previous/'inputs.json')['files']
        b=read_json(ROOT/'synth/reports'/current/'inputs.json')['files']
        require(set(a)==set(b),'fit scope changed '+current)
        names=sorted(n for n in a if a[n]!=b[n])
        require(set(names)=={'rtl/uj11_decode.v','microcode/m0.uasm','microcode/generated/uj11_m0_ebr.v','tools/checkpoint.py'},'unintended fit change '+current)
        fit_delta[current]=dict(baseline=previous,changed_inputs=names,probe_strategy_clock_unchanged=True)
    require(len(archives)==102 and raw==403,'archive accounting')
    fixtures={};cycles={};normal=12928;faults=0
    for suite in SUITES:
        cycles[suite]={}
        for mode,label in [(-1,'ram'),(2,'fram')]:
            data=(build/f'{suite}-cycles-{mode}.csv').read_bytes()
            require(data==(build/f'{suite}-cycles-{mode}-portable.csv').read_bytes(),'ROM parity '+suite)
            rows=list(csv.DictReader(io.StringIO(data.decode())))
            count=NEW_COUNTS[suite] if suite in NEW_COUNTS else oldcycles[suite][label]['cases']
            require(len(rows)==count and [int(r['case']) for r in rows]==list(range(count)),'cycle IDs '+suite)
            clocks=sum(int(r['microclocks']) for r in rows);beats=sum(int(r['memory_beats']) for r in rows)
            if suite in OLD_SUITES:
                require(data==gzip.decompress((reports/f'cp23-{suite}-cycles-{label}.csv.gz').read_bytes()),'CP23 cycle parity '+suite)
                require((clocks,beats)==(oldcycles[suite][label]['microclocks'],oldcycles[suite][label]['memory_beats']),'old totals '+suite)
            cycles[suite][label]=dict(cases=count,microclocks=clocks,memory_beats=beats,portable_vendor_equal=True,cp23_equal=suite in OLD_SUITES)
            (reports/f'cp24-{suite}-cycles-{label}.csv.gz').write_bytes(gzip.compress(data,mtime=0))
        if suite in ['bus-fault','psw-transfer-fault','eis-ash-fault','eis-ashc-fault','eis-xor-fault']:faults+=count
        else:normal+=count
        data=(build/f'{suite}-vectors.txt').read_bytes()
        if suite in OLD_SUITES:
            require(sha256(data)==baseline['fixtures_uncompressed_sha256'][suite],'old C fixture '+suite)
            require(data==gzip.decompress((reports/f'cp23-{suite}-vectors.txt.gz').read_bytes()),'old fixture bytes '+suite)
        fixtures[suite]=sha256(data);(reports/f'cp24-{suite}-vectors.txt.gz').write_bytes(gzip.compress(data,mtime=0))
        if suite in OLD_SUITES and suite!='illegal':require((build/f'{suite}-oracle.log').read_bytes()==(reports/f'cp23-{suite}-oracle.log').read_bytes(),'oracle accounting '+suite)
        shutil.copyfile(build/f'{suite}-oracle.log',reports/f'cp24-{suite}-oracle.log')
    require((normal,faults)==(242133,41596),'instruction/fault totals')
    # Newly supported XOR encodings leave the illegal candidate set. They were
    # already excluded by the old actual-C oracle, so its 4160 fixtures stay exact.
    old_illegal=gzip.decompress((reports/'cp23-illegal-opcodes.txt.gz').read_bytes()).decode().splitlines()
    current=(build/'illegal-opcodes.txt').read_text().splitlines()
    require(current==[s for s in old_illegal if int(s.split()[0],16)&0o177000!=0o74000],'illegal candidate change')
    require(len(old_illegal)-len(current)==512,'new opcode count')
    for p in list(build.glob('*excluded.csv'))+list(build.glob('*continuation.csv'))+[build/'illegal-opcodes.txt']:
        oldfile=reports/('cp23-'+p.name+'.gz')
        if oldfile.exists() and not p.name.startswith('illegal'):require(p.read_bytes()==gzip.decompress(oldfile.read_bytes()),'exclusion/continuation '+p.name)
        (reports/('cp24-'+p.name+'.gz')).write_bytes(gzip.compress(p.read_bytes(),mtime=0))
    benchmarks={};runs=0
    for stem in STEMS:
        data=(build/(stem+'.json')).read_bytes()
        require(data==(build/(stem+'-portable.json')).read_bytes(),'benchmark ROM parity '+stem)
        if stem in OLD_STEMS:require(data==gzip.decompress((reports/('cp23-'+stem+'.json.gz')).read_bytes()),'CP23 benchmark '+stem)
        benchmarks[stem]=json.loads(data);runs+=len(benchmarks[stem])
        (reports/('cp24-'+stem+'.json.gz')).write_bytes(gzip.compress(data,mtime=0))
    require(len(STEMS)==68 and runs==1018,'benchmark inventory')
    for text in [logs['cp24-benchmarks-portable.log'],vendor]:
        for mode in [-1,0,1,2]:require(f'PASS XOR benchmarks mode{mode}: 8' in text,'XOR benchmark completion')
    for n in LOGS:shutil.copyfile(build/n,reports/n)
    models={p.name:sha256(p.read_bytes()) for p in (build/'vendor').glob('*.v')}
    require(models==baseline['vendor_models_sha256'],'Lattice model changed')
    write_json(ROOT/'docs/benchmarks-cp24.json',dict(date='2026-09-09',clock_hz_nominal=29560000,spi_divider=1,method='Fresh functional RTL simulation with portable and vendor DP8KC; no board measurement.',runs_each_rom_model=runs,old_986_benchmarks_and_all_cp23_cycle_memory_spi_counts_unchanged=True,fit_comparison=fit_delta,benchmarks=benchmarks,benchmark_metrics={stem:[dict(**row,microclocks_per_instruction=row['microclocks']/row['instructions'],memory_beats_per_instruction=row.get('memory_beats',row.get('memory_cycles'))/row['instructions'],calculated_ips_at_29_56_mhz=29560000*row['instructions']/row['microclocks']) for row in rows] for stem,rows in benchmarks.items()},differential_cycles=cycles))
    source=set()
    for pattern in ['docs/*.md','docs/*.patch','rtl/*.v','microcode/*.uasm','microcode/generated/*','microasm/*.py','tb/*.v','tb/*.c','tb/*.h','tb/test_*.py','tools/*.py','tools/formal-requirements.txt','reference/lsi11/*','synth/machxo2/*.v','synth/machxo2/*.lpf','synth/machxo2/*.sty']:source.update(p for p in ROOT.glob(pattern) if p.is_file())
    source.update(ROOT/n for n in ['Makefile','README.md','../core/core.c','../core/core.h','../core/hardware.c','../core/hardware.h','../core/pdp11_fp.c','../core/disas.c','../core/disas.h','../tests/core_tests.c'])
    buffer=io.BytesIO()
    with tarfile.open(fileobj=buffer,mode='w') as a:
        for p in sorted(source):
            data=p.read_bytes();name=('tests/'+p.name) if p.parent==ROOT/'../tests' else ('core/'+p.name) if p.parent==ROOT/'../core' else 'uJ11-fpga/'+str(p.relative_to(ROOT));info=tarfile.TarInfo(name);info.size=len(data);a.addfile(info,io.BytesIO(data))
    (reports/'cp24-verification-source.tgz').write_bytes(gzip.compress(buffer.getvalue(),mtime=0))
    files=source|set(reports.glob('cp24*'))|{ROOT/'docs/benchmarks-cp24.json'}
    for n in FINAL:files.update([ROOT/'synth/reports'/n/'inputs.json',ROOT/'synth/reports'/n/'result.json'])
    write_json(ROOT/'docs/verification-cp24.json',dict(date='2026-09-09',baseline='CP23',encoding_version=11,microcode_words=597,profile=baseline['profile']+'; XOR added',checks=dict(completed_dcj11_cases_each_memory_rom_pair=normal,bus_fault_frame_cases_each_memory_rom_pair=faults,benchmark_runs_each_rom_model=runs,portable_vendor_identical_result_files=112,all_104_old_result_files_byte_identical_cp23=True,all_20_old_oracle_fixtures_byte_identical_cp23=True,decoder_encodings_checked=65536,new_xor_encodings=512,unchanged_microcode_words=goldstats['used_words'],unchanged_labels=len(goldlabels),added_microaddresses=added,unchanged_other_rtl=True,python_test_methods=16,xor_csr_cases_each_rom=1024,synthesis_archives_verified=archives,raw_report_hashes_verified=raw,current_inputs_equal_synthesis=FINAL),xor_oracle=oracle,xor_negative_controls=negatives,synthesis=selected,oracle=dict(original_core_sha256=sha256((ROOT/'../core/core.c').read_bytes()),source_unchanged_from_cp23=True),limits=['Same bounded kernel-mode and explicit exclusions as CP23, plus XOR; no MMU.','XOR ordinary C fixture has 196 exclusions; all 512 encodings covered by normal/fault union. I/O directed cases use a read-clear CSR model.','Fit includes probes and FRAM/IRQ resolver, not full UART/timer/panel/SD/RK or external pin timing; no FPGA programming.','35/29.56 MHz pass; 50 MHz and preferred 900-1000 LUT remain unmet.'],fixtures_uncompressed_sha256=fixtures,vendor_models_sha256=models,files={str(p.relative_to(ROOT)):sha256(p.read_bytes()) for p in sorted(files) if p.is_file()}))
    print(f'CP24 recorded: {len(archives)} archives/{raw} raw hashes; {normal} instruction cases + {faults} fault frames per memory/ROM pair; {runs} benchmarks/ROM; all 104 old results unchanged, 112 portable/vendor results equal.')
if __name__=='__main__':main()
