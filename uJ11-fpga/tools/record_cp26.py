#!/usr/bin/env python3
"""Archive CP26 only after DIV checks and exact preservation of CP25 results."""
import csv,gzip,io,json,re,shutil,tarfile,sys
from pathlib import Path
from record_ea import ROOT,read_json,write_json,sha256,require
from verify_cp26 import SUITES,STEMS,VENDOR,OLD_SUITES,OLD_STEMS
FINAL=['cp26a','cp26b']
NEW_COUNTS={'eis_div':13664,'eis-div-fault':4000}
LOGS=['cp26-tests.log','cp26-core-tests.log','cp26-decode-equivalence.log',
      'cp26-div-oracle.log','cp26-old-core-negative.log','cp26-div-algorithm.log','cp26-negative-controls.log','cp26-benchmarks-portable.log',
      'cp26-vendor-prepare.log']+['cp26-vendor-'+n+'.log' for n in VENDOR]

def main():
    build=ROOT/'build';reports=ROOT/'tb/reports'
    baseline=read_json(ROOT/'docs/verification-cp25.json')
    oldcycles=read_json(ROOT/'docs/benchmarks-cp25.json')['differential_cycles']
    require(set(oldcycles)==set(OLD_SUITES),'old suite inventory')
    require(not list((ROOT/'rtl').glob('*mmu*')),'MMU RTL')
    stats=read_json(ROOT/'microcode/generated/m0.stats.json')
    require((stats['encoding_version'],stats['used_words'],stats['word_bits'])==(11,700,36),'microstore format')
    changed={'rtl/uj11_decode.v','microcode/m0.uasm','tb/tb_decode.v','tb/test_microasm.py',
             'tb/tb_trace_bit.v','tb/tb_bus_fault.v','../core/core.c'}
    changed.update('microcode/generated/'+n for n in ['m0.mem','m0.lst','m0.labels.json','m0.stats.json','uj11_m0_ebr.v'])
    for n,h in baseline['files'].items():
        if n.startswith(('rtl/','microcode/','microasm/','reference/lsi11/','tb/','../core/','../tests/')) and not n.startswith('tb/reports/') and n not in changed:
            require(sha256((ROOT/n).read_bytes())==h,'unintended source change '+n)
    require({str(p.relative_to(ROOT)) for p in (ROOT/'rtl').glob('*.v')}=={n for n in baseline['files'] if n.startswith('rtl/') and n.endswith('.v')},'RTL inventory')
    with tarfile.open(reports/'cp25-verification-source.tgz') as a:
        def old(n):return a.extractfile('uJ11-fpga/'+n).read().decode()
        sys.path.insert(0,str(ROOT/'microasm'));from uj11asm import assemble
        gold,_,goldlabels,goldstats=assemble(old('microcode/m0.uasm'))
        gate,_,labels,_=assemble((ROOT/'microcode/m0.uasm').read_text())
        added=[i for i,(x,y) in enumerate(zip(gold,gate)) if x!=y]
        expected_added=[0x072,0x073,0x076,0x077,0x07a,0x07b]+list(range(0x181,0x18b))+list(range(0x256,0x25f))+list(range(0x308,0x30f))+list(range(0x328,0x32d))+list(range(0x348,0x34f))+list(range(0x368,0x36b))+list(range(0x3b5,0x3c0))
        require(added==expected_added and len(added)==58,'unexpected ROM changes')
        require(all(labels[n]==v for n,v in goldlabels.items()),'old label address changed')
        # The added suite selector must not alter any old testbench behavior.
        t=(ROOT/'tb/tb_trace_bit.v').read_text().replace(', parameter integer EIS_DIV=0','').replace('if(EIS_DIV)suite="eis_div";else ','')
        require(t==old('tb/tb_trace_bit.v'),'old normal testbench modified')
        t=(ROOT/'tb/tb_bus_fault.v').read_text().replace(', parameter integer EIS_DIV=0','').replace('if(EIS_DIV)begin suite="eis-div-fault";display_suite="DIV fault";end\n        else ','')
        require(t==old('tb/tb_bus_fault.v'),'old fault testbench modified')
        oldcore=a.extractfile('core/core.c').read().decode()
        currentcore=(ROOT/'../core/core.c').read_text()
        import difflib
        patch=''.join(difflib.unified_diff(oldcore.splitlines(True),currentcore.splitlines(True),fromfile='a/core/core.c',tofile='b/core/core.c'))
        require(patch==(ROOT/'docs/cp26-core-fix.patch').read_text(),'exact reviewed C correction')
        a=oldcore.index('    case 0071:');b=oldcore.index('    case 0072:',a)
        c=currentcore.index('    case 0071:');d=currentcore.index('    case 0072:',c)
        require(currentcore[:c]==oldcore[:a] and currentcore[d:]==oldcore[b:],'correction outside DIV')
        require('if (r->fAbort)' in currentcore[c:d] and 'r->model == DCJ11 && q64 < 0' in currentcore[c:d], 'reviewed DIV guards')
    logs={n:(build/n).read_text() for n in LOGS}
    for n,t in logs.items():require(t and not any(x in t for x in ['FATAL','FAILED','Traceback','%Error','%Warning','ERROR:']),n)
    require('PASS CP26 decoder: all 65536 checked; only DIV differ from archived CP25a' in logs['cp26-decode-equivalence.log'],'decoder miter')
    require('All core instruction tests passed' in logs['cp26-core-tests.log'],'C regression')
    oracle=read_json(build/'cp26-div-oracle.json')
    require((oracle['independent_ordinary_cases'],oracle['independent_alias_cases'],oracle['normal_fault_union'])==(7322,385,512),'independent oracle counts')
    negatives=read_json(build/'cp26-negative-controls.json')
    require([r['name'] for r in negatives]==['missing-opcode','fifteen-iterations','unsigned-dividend','lost-negation-borrow','unsigned-divisor','lost-quotient-bit','wrong-quotient-sign','wrong-remainder-sign','positive-overflow-write','reject-minus-32768','zero-divisor-flags','negative-overflow-flags','odd-quotient-overwrites-remainder','early-low-dividend','flags-on-failed-read'] and all(r['rejected'] for r in negatives),'negative controls')
    oldnegative=read_json(build/'cp26-old-core-negative.json')
    require(oldnegative['core_sha256']==baseline['oracle']['original_core_sha256'] and oldnegative['normal_semantics_bug_rejected'] and oldnegative['fault_postframe_register_or_psw_changes']==2368,'old C negative')
    require('861968 independent quotient/remainder/NZVC comparisons' in logs['cp26-div-algorithm.log'],'serial algorithm arithmetic checks')
    for p in build.glob('cp26-old-core-*'):shutil.copyfile(p,reports/p.name)
    for item in negatives:
        n='cp26-negative-'+item['name']+'.log';s=(build/n).read_text()
        require('FATAL' in s and ('trace case' in s or 'fault case' in s or 'beat1 got' in s or 'beat2 got' in s),'negative mismatch '+n)
        shutil.copyfile(build/n,reports/n)
    for n in ['cp26-div-oracle.json','cp26-negative-controls.json']:shutil.copyfile(build/n,reports/n)
    portable=logs['cp26-tests.log'];vendor='\n'.join(logs['cp26-vendor-'+n+'.log'] for n in VENDOR)
    for text in [portable,vendor]:
        for suite in SUITES:
            display={'bus-fault':'bus fault','psw-transfer-fault':'PSW-transfer fault','eis-ashc-fault':'ASHC fault','eis-ash-fault':'ASH fault','eis-xor-fault':'XOR fault','eis-mul-fault':'MUL fault','eis-div-fault':'DIV fault'}.get(suite,suite)
            count=NEW_COUNTS[suite] if suite in NEW_COUNTS else oldcycles[suite]['ram']['cases']
            for mode in [-1,2]:require(f'PASS {display} differential mode{mode}: {count}' in text,'suite missing '+suite)
        for marker in ['PASS differential: 12928','PASS FRAM differential: 12928','PASS stream engine: opcode+3','PASS EA directed: 14','PASS single directed: 46','PASS byte I/O: 23','PASS single byte directed: 46','PASS control directed: 58','PASS extra directed: 36','PASS trap directed: 112','PASS IRQ directed: 104','PASS legacy IRQ: 5 scenarios, 6 settled-state checks','PASS illegal directed: 96','PASS bus fault system: 32','PASS bus double fault: 96','PASS trace system: 24','PASS trace faults: 140','PASS system control legacy: 3 scenarios','PASS system control faults: 64','PASS XOR CSR: 1024','PASS MUL CSR: 1024','PASS DIV CSR: 2048']:require(marker in text,marker)
    for marker in ['Ran 16 tests','4096 pair checks','PASS prefetch: 525','PASS lsi11 peripherals: 19','exactly 58992 supported encodings','PASS byte ALU: 2097152','PASS byte datapath: 16384','PASS IRQ adapter: 763','PASS fault IRQ oracle: 8']:require(marker in portable,marker)
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
    require((selected['cp26a']['lut4'],selected['cp26a']['ff'],selected['cp26b']['lut4'],selected['cp26b']['ff'])==(867,299,1099,416),'resource result')
    fit_delta={}
    for previous,current in [('cp25a','cp26a'),('cp25b','cp26b')]:
        a=read_json(ROOT/'synth/reports'/previous/'inputs.json')['files']
        b=read_json(ROOT/'synth/reports'/current/'inputs.json')['files']
        require(set(a)==set(b),'fit scope changed '+current)
        names=sorted(n for n in a if a[n]!=b[n])
        require(set(names)=={'rtl/uj11_decode.v','microcode/m0.uasm','microcode/generated/uj11_m0_ebr.v','tools/checkpoint.py'},'unintended fit change '+current)
        fit_delta[current]=dict(baseline=previous,changed_inputs=names,probe_strategy_clock_unchanged=True)
    require(len(archives)==106 and raw==419,'archive accounting')
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
                require(data==gzip.decompress((reports/f'cp25-{suite}-cycles-{label}.csv.gz').read_bytes()),'CP25 cycle parity '+suite)
                require((clocks,beats)==(oldcycles[suite][label]['microclocks'],oldcycles[suite][label]['memory_beats']),'old totals '+suite)
            cycles[suite][label]=dict(cases=count,microclocks=clocks,memory_beats=beats,portable_vendor_equal=True,cp25_equal=suite in OLD_SUITES)
            (reports/f'cp26-{suite}-cycles-{label}.csv.gz').write_bytes(gzip.compress(data,mtime=0))
        if suite in ['bus-fault','psw-transfer-fault','eis-ash-fault','eis-ashc-fault','eis-xor-fault','eis-mul-fault','eis-div-fault']:faults+=count
        else:normal+=count
        data=(build/f'{suite}-vectors.txt').read_bytes()
        if suite in OLD_SUITES:
            require(sha256(data)==baseline['fixtures_uncompressed_sha256'][suite],'old C fixture '+suite)
            require(data==gzip.decompress((reports/f'cp25-{suite}-vectors.txt.gz').read_bytes()),'old fixture bytes '+suite)
        fixtures[suite]=sha256(data);(reports/f'cp26-{suite}-vectors.txt.gz').write_bytes(gzip.compress(data,mtime=0))
        if suite in OLD_SUITES and suite!='illegal':require((build/f'{suite}-oracle.log').read_bytes()==(reports/f'cp25-{suite}-oracle.log').read_bytes(),'oracle accounting '+suite)
        shutil.copyfile(build/f'{suite}-oracle.log',reports/f'cp26-{suite}-oracle.log')
    require((normal,faults)==(268907,49596),'instruction/fault totals')
    # Newly supported DIV encodings leave the illegal candidate set. They were
    # already excluded by the old actual-C oracle, so its 4160 fixtures stay exact.
    old_illegal=gzip.decompress((reports/'cp25-illegal-opcodes.txt.gz').read_bytes()).decode().splitlines()
    current=(build/'illegal-opcodes.txt').read_text().splitlines()
    require(current==[s for s in old_illegal if int(s.split()[0],16)&0o177000!=0o71000],'illegal candidate change')
    require(len(old_illegal)-len(current)==512,'new opcode count')
    for p in list(build.glob('*excluded.csv'))+list(build.glob('*continuation.csv'))+[build/'illegal-opcodes.txt']:
        oldfile=reports/('cp25-'+p.name+'.gz')
        if oldfile.exists() and not p.name.startswith('illegal'):require(p.read_bytes()==gzip.decompress(oldfile.read_bytes()),'exclusion/continuation '+p.name)
        (reports/('cp26-'+p.name+'.gz')).write_bytes(gzip.compress(p.read_bytes(),mtime=0))
    benchmarks={};runs=0
    for stem in STEMS:
        data=(build/(stem+'.json')).read_bytes()
        require(data==(build/(stem+'-portable.json')).read_bytes(),'benchmark ROM parity '+stem)
        if stem in OLD_STEMS:require(data==gzip.decompress((reports/('cp25-'+stem+'.json.gz')).read_bytes()),'CP25 benchmark '+stem)
        benchmarks[stem]=json.loads(data);runs+=len(benchmarks[stem])
        if stem.startswith('eis-div-benchmarks-'):
            rows=benchmarks[stem]
            require(len(rows)==16 and len({r['workload'] for r in rows})==16 and all(r['workload'] for r in rows),'DIV workload names')
            require(all(r['div_instructions']==120 and r['div_retire_clocks']>0 and r['microclocks']>r['div_retire_clocks'] for r in rows),'DIV retirement counters')
        (reports/('cp26-'+stem+'.json.gz')).write_bytes(gzip.compress(data,mtime=0))
    require(len(STEMS)==76 and runs==1146,'benchmark inventory')
    for text in [logs['cp26-benchmarks-portable.log'],vendor]:
        for mode in [-1,0,1,2]:require(f'PASS DIV benchmarks mode{mode}: 16' in text,'DIV benchmark completion')
    from check_psw_transfer_negative import records
    ordinary_rr_ids=[i for i,(h,c,t) in enumerate(records('eis_div')) if int(h[1],16) in [0o71002,0o71102] and int(h[2],16)<16 and int(c[0],16)==0]
    require(len(ordinary_rr_ids)==4806,'cold RR fixture scope')
    cold_rr={}
    for mode,label in [(-1,'ram'),(2,'fram')]:
        rows=list(csv.DictReader((build/f'eis_div-cycles-{mode}.csv').open()))
        values=[int(rows[i]['microclocks']) for i in ordinary_rr_ids]
        cold_rr[label]=dict(cases=len(values),min_microclocks=min(values),max_microclocks=max(values),interval='First fetch through retirement; R0/R1 and S=R2, no T or IRQ')
    div_metrics={stem:[dict(workload=r['workload'],div_retire_clocks_per_div=r['div_retire_clocks']/r['div_instructions'],calculated_div_per_sec_including_loop_overhead_at_29_56_mhz=29560000*r['div_instructions']/r['microclocks']) for r in rows] for stem,rows in benchmarks.items() if stem.startswith('eis-div-benchmarks-')}
    for n in LOGS:shutil.copyfile(build/n,reports/n)
    models={p.name:sha256(p.read_bytes()) for p in (build/'vendor').glob('*.v')}
    require(models==baseline['vendor_models_sha256'],'Lattice model changed')
    write_json(ROOT/'docs/benchmarks-cp26.json',dict(date='2026-09-09',clock_hz_nominal=29560000,spi_divider=1,method='Fresh functional RTL simulation with portable and vendor DP8KC; no board measurement.',runs_each_rom_model=runs,old_1082_benchmarks_and_all_cp25_cycle_memory_spi_counts_unchanged=True,fit_comparison=fit_delta,benchmarks=benchmarks,benchmark_metrics={stem:[dict(**row,microclocks_per_instruction=row['microclocks']/row['instructions'],memory_beats_per_instruction=row.get('memory_beats',row.get('memory_cycles'))/row['instructions'],calculated_ips_at_29_56_mhz=29560000*row['instructions']/row['microclocks']) for row in rows] for stem,rows in benchmarks.items()},differential_cycles=cycles,div_cold_rr_cycles=cold_rr,div_benchmark_metrics=div_metrics))
    source=set()
    for pattern in ['docs/*.md','docs/*.patch','rtl/*.v','microcode/*.uasm','microcode/generated/*','microasm/*.py','tb/*.v','tb/*.c','tb/*.h','tb/test_*.py','tools/*.py','tools/formal-requirements.txt','reference/lsi11/*','synth/machxo2/*.v','synth/machxo2/*.lpf','synth/machxo2/*.sty']:source.update(p for p in ROOT.glob(pattern) if p.is_file())
    source.update(ROOT/n for n in ['Makefile','README.md','../core/core.c','../core/core.h','../core/hardware.c','../core/hardware.h','../core/pdp11_fp.c','../core/disas.c','../core/disas.h','../tests/core_tests.c'])
    buffer=io.BytesIO()
    with tarfile.open(fileobj=buffer,mode='w') as a:
        for p in sorted(source):
            data=p.read_bytes();name=('tests/'+p.name) if p.parent==ROOT/'../tests' else ('core/'+p.name) if p.parent==ROOT/'../core' else 'uJ11-fpga/'+str(p.relative_to(ROOT));info=tarfile.TarInfo(name);info.size=len(data);a.addfile(info,io.BytesIO(data))
    (reports/'cp26-verification-source.tgz').write_bytes(gzip.compress(buffer.getvalue(),mtime=0))
    files=source|set(reports.glob('cp26*'))|{ROOT/'docs/benchmarks-cp26.json'}
    for n in FINAL:files.update([ROOT/'synth/reports'/n/'inputs.json',ROOT/'synth/reports'/n/'result.json'])
    write_json(ROOT/'docs/verification-cp26.json',dict(date='2026-09-09',baseline='CP25',encoding_version=11,microcode_words=700,profile=baseline['profile']+'; DIV added',checks=dict(completed_dcj11_cases_each_memory_rom_pair=normal,bus_fault_frame_cases_each_memory_rom_pair=faults,benchmark_runs_each_rom_model=runs,portable_vendor_identical_result_files=128,all_120_old_result_files_byte_identical_cp25=True,all_24_old_oracle_fixtures_byte_identical_cp25=True,decoder_encodings_checked=65536,new_div_encodings=512,unchanged_microcode_words=goldstats['used_words'],unchanged_labels=len(goldlabels),added_microaddresses=added,unchanged_other_rtl=True,python_test_methods=16,div_csr_cases_each_rom=2048,synthesis_archives_verified=archives,raw_report_hashes_verified=raw,current_inputs_equal_synthesis=FINAL),div_oracle=oracle,div_negative_controls=negatives,old_core_negative=oldnegative,synthesis=selected,oracle=dict(original_core_sha256=sha256((ROOT/'../core/core.c').read_bytes()),cp25_core_sha256=sha256(oldcore.encode()),source_unchanged_from_cp25=False,reviewed_patch='docs/cp26-core-fix.patch',scope='DCJ11 DIV only: late dividend, abort return, zero/overflow flags, explicit SIMH-style odd-R extension'),limits=['Same bounded kernel-mode and explicit exclusions as CP25, plus DIV; no MMU.','DIV ordinary C fixture has 184 exclusions; all 512 encodings covered by normal/fault union. I/O directed cases use a read-clear CSR model.','Fit includes probes and FRAM/IRQ resolver, not full UART/timer/panel/SD/RK or external pin timing; no FPGA programming.','35/29.56 MHz pass; 50 MHz and preferred 900-1000 LUT remain unmet.'],fixtures_uncompressed_sha256=fixtures,vendor_models_sha256=models,files={str(p.relative_to(ROOT)):sha256(p.read_bytes()) for p in sorted(files) if p.is_file()}))
    print(f'CP26 recorded: {len(archives)} archives/{raw} raw hashes; {normal} instruction cases + {faults} fault frames per memory/ROM pair; {runs} benchmarks/ROM; all 120 old results unchanged, 128 portable/vendor results equal.')
if __name__=='__main__':main()
