#!/usr/bin/env python3
"""Archive CP25 only after MUL checks and exact preservation of CP24 results."""
import csv,gzip,io,json,re,shutil,tarfile,sys
from pathlib import Path
from record_ea import ROOT,read_json,write_json,sha256,require
from verify_cp25 import SUITES,STEMS,VENDOR,OLD_SUITES,OLD_STEMS
FINAL=['cp25a','cp25b']
NEW_COUNTS={'eis_mul':13110,'eis-mul-fault':4000}
LOGS=['cp25-tests.log','cp25-core-tests.log','cp25-decode-equivalence.log',
      'cp25-mul-oracle.log','cp25-old-core-negative.log','cp25-mul-algorithm.log','cp25-negative-controls.log','cp25-benchmarks-portable.log',
      'cp25-vendor-prepare.log']+['cp25-vendor-'+n+'.log' for n in VENDOR]

def main():
    build=ROOT/'build';reports=ROOT/'tb/reports'
    baseline=read_json(ROOT/'docs/verification-cp24.json')
    oldcycles=read_json(ROOT/'docs/benchmarks-cp24.json')['differential_cycles']
    require(set(oldcycles)==set(OLD_SUITES),'old suite inventory')
    require(not list((ROOT/'rtl').glob('*mmu*')),'MMU RTL')
    stats=read_json(ROOT/'microcode/generated/m0.stats.json')
    require((stats['encoding_version'],stats['used_words'],stats['word_bits'])==(11,642,36),'microstore format')
    changed={'rtl/uj11_decode.v','microcode/m0.uasm','tb/tb_decode.v','tb/test_microasm.py',
             'tb/tb_trace_bit.v','tb/tb_bus_fault.v','../core/core.c'}
    changed.update('microcode/generated/'+n for n in ['m0.mem','m0.lst','m0.labels.json','m0.stats.json','uj11_m0_ebr.v'])
    for n,h in baseline['files'].items():
        if n.startswith(('rtl/','microcode/','microasm/','reference/lsi11/','tb/','../core/','../tests/')) and not n.startswith('tb/reports/') and n not in changed:
            require(sha256((ROOT/n).read_bytes())==h,'unintended source change '+n)
    require({str(p.relative_to(ROOT)) for p in (ROOT/'rtl').glob('*.v')}=={n for n in baseline['files'] if n.startswith('rtl/') and n.endswith('.v')},'RTL inventory')
    with tarfile.open(reports/'cp24-verification-source.tgz') as a:
        def old(n):return a.extractfile('uJ11-fpga/'+n).read().decode()
        sys.path.insert(0,str(ROOT/'microasm'));from uj11asm import assemble
        gold,_,goldlabels,goldstats=assemble(old('microcode/m0.uasm'))
        gate,_,labels,_=assemble((ROOT/'microcode/m0.uasm').read_text())
        added=[i for i,(x,y) in enumerate(zip(gold,gate)) if x!=y]
        expected_added=[0x062,0x063,0x066,0x067,0x082,0x083]+list(range(0x312,0x31c))+list(range(0x333,0x33e))+list(range(0x373,0x37f))+list(range(0x395,0x39b))
        require(added==expected_added and len(added)==45,'unexpected ROM changes')
        require(all(labels[n]==v for n,v in goldlabels.items()),'old label address changed')
        # The added suite selector must not alter any old testbench behavior.
        t=(ROOT/'tb/tb_trace_bit.v').read_text().replace(', parameter integer EIS_MUL=0','').replace('if(EIS_MUL)suite="eis_mul";else ','')
        require(t==old('tb/tb_trace_bit.v'),'old normal testbench modified')
        t=(ROOT/'tb/tb_bus_fault.v').read_text().replace(', parameter integer EIS_MUL=0','').replace('if(EIS_MUL)begin suite="eis-mul-fault";display_suite="MUL fault";end\n        else ','')
        require(t==old('tb/tb_bus_fault.v'),'old fault testbench modified')
        oldcore=a.extractfile('core/core.c').read().decode()
        currentcore=(ROOT/'../core/core.c').read_text()
        import difflib
        patch=''.join(difflib.unified_diff(oldcore.splitlines(True),currentcore.splitlines(True),fromfile='a/core/core.c',tofile='b/core/core.c'))
        require(patch==(ROOT/'docs/cp25-core-fix.patch').read_text(),'exact reviewed C correction')
        correction='''        if (r->model == DCJ11) {
            /* J-11 reads R after the S operand and its addressing side effects.
             * An operand fault must not overwrite the completed trap frame. */
            if (r->fAbort) {
                return 0;
            }
            src = (sdword)(sword)r->r[reg];
        }
'''
        require(currentcore.count(correction)==1 and currentcore.replace(correction,'')==oldcore,'unrelated C edit')
        require(currentcore.index('case 0070:') < currentcore.index(correction) < currentcore.index('case 0071:'),'correction outside MUL')
    logs={n:(build/n).read_text() for n in LOGS}
    for n,t in logs.items():require(t and not any(x in t for x in ['FATAL','FAILED','Traceback','%Error','%Warning','ERROR:']),n)
    require('PASS CP25 decoder: all 65536 checked; only MUL differ from archived CP24a' in logs['cp25-decode-equivalence.log'],'decoder miter')
    require('All core instruction tests passed' in logs['cp25-core-tests.log'],'C regression')
    oracle=read_json(build/'cp25-mul-oracle.json')
    require((oracle['independent_ordinary_cases'],oracle['independent_alias_cases'],oracle['normal_fault_union'])==(7002,385,512),'independent oracle counts')
    negatives=read_json(build/'cp25-negative-controls.json')
    require([r['name'] for r in negatives]==['missing-opcode','fifteen-iterations','unsigned-multiplicand','unsigned-register','lost-low-carry','lost-overflow-carry','set-carry-at-32767','low-word-negative','low-word-zero','odd-high-overwrites-low','early-register','flags-on-failed-read'] and all(r['rejected'] for r in negatives),'negative controls')
    oldnegative=read_json(build/'cp25-old-core-negative.json')
    require(oldnegative['core_sha256']==baseline['oracle']['original_core_sha256'] and oldnegative['late_register_bug_rejected'] and oldnegative['fault_postframe_register_or_psw_changes']==2368,'old C negative')
    require('861968 independent product/NZVC comparisons' in logs['cp25-mul-algorithm.log'],'serial algorithm arithmetic checks')
    for p in build.glob('cp25-old-core-*'):shutil.copyfile(p,reports/p.name)
    for item in negatives:
        n='cp25-negative-'+item['name']+'.log';s=(build/n).read_text()
        require('FATAL' in s and ('trace case' in s or 'fault case' in s or 'beat1 got' in s or 'beat2 got' in s),'negative mismatch '+n)
        shutil.copyfile(build/n,reports/n)
    for n in ['cp25-mul-oracle.json','cp25-negative-controls.json']:shutil.copyfile(build/n,reports/n)
    portable=logs['cp25-tests.log'];vendor='\n'.join(logs['cp25-vendor-'+n+'.log'] for n in VENDOR)
    for text in [portable,vendor]:
        for suite in SUITES:
            display={'bus-fault':'bus fault','psw-transfer-fault':'PSW-transfer fault','eis-ashc-fault':'ASHC fault','eis-ash-fault':'ASH fault','eis-xor-fault':'XOR fault','eis-mul-fault':'MUL fault'}.get(suite,suite)
            count=NEW_COUNTS[suite] if suite in NEW_COUNTS else oldcycles[suite]['ram']['cases']
            for mode in [-1,2]:require(f'PASS {display} differential mode{mode}: {count}' in text,'suite missing '+suite)
        for marker in ['PASS differential: 12928','PASS FRAM differential: 12928','PASS stream engine: opcode+3','PASS EA directed: 14','PASS single directed: 46','PASS byte I/O: 23','PASS single byte directed: 46','PASS control directed: 58','PASS extra directed: 36','PASS trap directed: 112','PASS IRQ directed: 104','PASS legacy IRQ: 5 scenarios, 6 settled-state checks','PASS illegal directed: 96','PASS bus fault system: 32','PASS bus double fault: 96','PASS trace system: 24','PASS trace faults: 140','PASS system control legacy: 3 scenarios','PASS system control faults: 64','PASS XOR CSR: 1024','PASS MUL CSR: 1024']:require(marker in text,marker)
    for marker in ['Ran 16 tests','4096 pair checks','PASS prefetch: 525','PASS lsi11 peripherals: 19','exactly 58480 supported encodings','PASS byte ALU: 2097152','PASS byte datapath: 16384','PASS IRQ adapter: 763','PASS fault IRQ oracle: 8']:require(marker in portable,marker)
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
    require((selected['cp25a']['lut4'],selected['cp25a']['ff'],selected['cp25b']['lut4'],selected['cp25b']['ff'])==(841,299,1089,416),'resource result')
    fit_delta={}
    for previous,current in [('cp24a','cp25a'),('cp24b','cp25b')]:
        a=read_json(ROOT/'synth/reports'/previous/'inputs.json')['files']
        b=read_json(ROOT/'synth/reports'/current/'inputs.json')['files']
        require(set(a)==set(b),'fit scope changed '+current)
        names=sorted(n for n in a if a[n]!=b[n])
        require(set(names)=={'rtl/uj11_decode.v','microcode/m0.uasm','microcode/generated/uj11_m0_ebr.v','tools/checkpoint.py'},'unintended fit change '+current)
        fit_delta[current]=dict(baseline=previous,changed_inputs=names,probe_strategy_clock_unchanged=True)
    require(len(archives)==104 and raw==411,'archive accounting')
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
                require(data==gzip.decompress((reports/f'cp24-{suite}-cycles-{label}.csv.gz').read_bytes()),'CP24 cycle parity '+suite)
                require((clocks,beats)==(oldcycles[suite][label]['microclocks'],oldcycles[suite][label]['memory_beats']),'old totals '+suite)
            cycles[suite][label]=dict(cases=count,microclocks=clocks,memory_beats=beats,portable_vendor_equal=True,cp24_equal=suite in OLD_SUITES)
            (reports/f'cp25-{suite}-cycles-{label}.csv.gz').write_bytes(gzip.compress(data,mtime=0))
        if suite in ['bus-fault','psw-transfer-fault','eis-ash-fault','eis-ashc-fault','eis-xor-fault','eis-mul-fault']:faults+=count
        else:normal+=count
        data=(build/f'{suite}-vectors.txt').read_bytes()
        if suite in OLD_SUITES:
            require(sha256(data)==baseline['fixtures_uncompressed_sha256'][suite],'old C fixture '+suite)
            require(data==gzip.decompress((reports/f'cp24-{suite}-vectors.txt.gz').read_bytes()),'old fixture bytes '+suite)
        fixtures[suite]=sha256(data);(reports/f'cp25-{suite}-vectors.txt.gz').write_bytes(gzip.compress(data,mtime=0))
        if suite in OLD_SUITES and suite!='illegal':require((build/f'{suite}-oracle.log').read_bytes()==(reports/f'cp24-{suite}-oracle.log').read_bytes(),'oracle accounting '+suite)
        shutil.copyfile(build/f'{suite}-oracle.log',reports/f'cp25-{suite}-oracle.log')
    require((normal,faults)==(255243,45596),'instruction/fault totals')
    # Newly supported MUL encodings leave the illegal candidate set. They were
    # already excluded by the old actual-C oracle, so its 4160 fixtures stay exact.
    old_illegal=gzip.decompress((reports/'cp24-illegal-opcodes.txt.gz').read_bytes()).decode().splitlines()
    current=(build/'illegal-opcodes.txt').read_text().splitlines()
    require(current==[s for s in old_illegal if int(s.split()[0],16)&0o177000!=0o70000],'illegal candidate change')
    require(len(old_illegal)-len(current)==512,'new opcode count')
    for p in list(build.glob('*excluded.csv'))+list(build.glob('*continuation.csv'))+[build/'illegal-opcodes.txt']:
        oldfile=reports/('cp24-'+p.name+'.gz')
        if oldfile.exists() and not p.name.startswith('illegal'):require(p.read_bytes()==gzip.decompress(oldfile.read_bytes()),'exclusion/continuation '+p.name)
        (reports/('cp25-'+p.name+'.gz')).write_bytes(gzip.compress(p.read_bytes(),mtime=0))
    benchmarks={};runs=0
    for stem in STEMS:
        data=(build/(stem+'.json')).read_bytes()
        require(data==(build/(stem+'-portable.json')).read_bytes(),'benchmark ROM parity '+stem)
        if stem in OLD_STEMS:require(data==gzip.decompress((reports/('cp24-'+stem+'.json.gz')).read_bytes()),'CP24 benchmark '+stem)
        benchmarks[stem]=json.loads(data);runs+=len(benchmarks[stem])
        if stem.startswith('eis-mul-benchmarks-'):
            rows=benchmarks[stem]
            require(len(rows)==16 and len({r['workload'] for r in rows})==16 and all(r['workload'] for r in rows),'MUL workload names')
            require(all(r['mul_instructions']==248 and r['mul_retire_clocks']>0 and r['microclocks']>r['mul_retire_clocks'] for r in rows),'MUL retirement counters')
        (reports/('cp25-'+stem+'.json.gz')).write_bytes(gzip.compress(data,mtime=0))
    require(len(STEMS)==72 and runs==1082,'benchmark inventory')
    for text in [logs['cp25-benchmarks-portable.log'],vendor]:
        for mode in [-1,0,1,2]:require(f'PASS MUL benchmarks mode{mode}: 16' in text,'MUL benchmark completion')
    from check_psw_transfer_negative import records
    ordinary_rr_ids=[i for i,(h,c,t) in enumerate(records('eis_mul')) if int(h[1],16) in [0o70002,0o70102] and int(h[2],16)<16 and int(c[0],16)==0]
    require(len(ordinary_rr_ids)==4614,'cold RR fixture scope')
    cold_rr={}
    for mode,label in [(-1,'ram'),(2,'fram')]:
        rows=list(csv.DictReader((build/f'eis_mul-cycles-{mode}.csv').open()))
        values=[int(rows[i]['microclocks']) for i in ordinary_rr_ids]
        cold_rr[label]=dict(cases=len(values),min_microclocks=min(values),max_microclocks=max(values),interval='First fetch through retirement; R0/R1 and S=R2, no T or IRQ')
    require((cold_rr['ram']['min_microclocks'],cold_rr['ram']['max_microclocks'],cold_rr['fram']['min_microclocks'],cold_rr['fram']['max_microclocks'])==(109,150,214,252),'MUL cold RR latency')
    mul_metrics={stem:[dict(workload=r['workload'],mul_retire_clocks_per_mul=r['mul_retire_clocks']/r['mul_instructions'],calculated_mul_per_sec_including_loop_overhead_at_29_56_mhz=29560000*r['mul_instructions']/r['microclocks']) for r in rows] for stem,rows in benchmarks.items() if stem.startswith('eis-mul-benchmarks-')}
    for n in LOGS:shutil.copyfile(build/n,reports/n)
    models={p.name:sha256(p.read_bytes()) for p in (build/'vendor').glob('*.v')}
    require(models==baseline['vendor_models_sha256'],'Lattice model changed')
    write_json(ROOT/'docs/benchmarks-cp25.json',dict(date='2026-09-09',clock_hz_nominal=29560000,spi_divider=1,method='Fresh functional RTL simulation with portable and vendor DP8KC; no board measurement.',runs_each_rom_model=runs,old_1018_benchmarks_and_all_cp24_cycle_memory_spi_counts_unchanged=True,fit_comparison=fit_delta,benchmarks=benchmarks,benchmark_metrics={stem:[dict(**row,microclocks_per_instruction=row['microclocks']/row['instructions'],memory_beats_per_instruction=row.get('memory_beats',row.get('memory_cycles'))/row['instructions'],calculated_ips_at_29_56_mhz=29560000*row['instructions']/row['microclocks']) for row in rows] for stem,rows in benchmarks.items()},differential_cycles=cycles,mul_cold_rr_cycles=cold_rr,mul_benchmark_metrics=mul_metrics))
    source=set()
    for pattern in ['docs/*.md','docs/*.patch','rtl/*.v','microcode/*.uasm','microcode/generated/*','microasm/*.py','tb/*.v','tb/*.c','tb/*.h','tb/test_*.py','tools/*.py','tools/formal-requirements.txt','reference/lsi11/*','synth/machxo2/*.v','synth/machxo2/*.lpf','synth/machxo2/*.sty']:source.update(p for p in ROOT.glob(pattern) if p.is_file())
    source.update(ROOT/n for n in ['Makefile','README.md','../core/core.c','../core/core.h','../core/hardware.c','../core/hardware.h','../core/pdp11_fp.c','../core/disas.c','../core/disas.h','../tests/core_tests.c'])
    buffer=io.BytesIO()
    with tarfile.open(fileobj=buffer,mode='w') as a:
        for p in sorted(source):
            data=p.read_bytes();name=('tests/'+p.name) if p.parent==ROOT/'../tests' else ('core/'+p.name) if p.parent==ROOT/'../core' else 'uJ11-fpga/'+str(p.relative_to(ROOT));info=tarfile.TarInfo(name);info.size=len(data);a.addfile(info,io.BytesIO(data))
    (reports/'cp25-verification-source.tgz').write_bytes(gzip.compress(buffer.getvalue(),mtime=0))
    files=source|set(reports.glob('cp25*'))|{ROOT/'docs/benchmarks-cp25.json'}
    for n in FINAL:files.update([ROOT/'synth/reports'/n/'inputs.json',ROOT/'synth/reports'/n/'result.json'])
    write_json(ROOT/'docs/verification-cp25.json',dict(date='2026-09-09',baseline='CP24',encoding_version=11,microcode_words=642,profile=baseline['profile']+'; MUL added',checks=dict(completed_dcj11_cases_each_memory_rom_pair=normal,bus_fault_frame_cases_each_memory_rom_pair=faults,benchmark_runs_each_rom_model=runs,portable_vendor_identical_result_files=120,all_112_old_result_files_byte_identical_cp24=True,all_22_old_oracle_fixtures_byte_identical_cp24=True,decoder_encodings_checked=65536,new_mul_encodings=512,unchanged_microcode_words=goldstats['used_words'],unchanged_labels=len(goldlabels),added_microaddresses=added,unchanged_other_rtl=True,python_test_methods=16,mul_csr_cases_each_rom=1024,synthesis_archives_verified=archives,raw_report_hashes_verified=raw,current_inputs_equal_synthesis=FINAL),mul_oracle=oracle,mul_negative_controls=negatives,old_core_negative=oldnegative,synthesis=selected,oracle=dict(original_core_sha256=sha256((ROOT/'../core/core.c').read_bytes()),cp24_core_sha256=sha256(oldcore.encode()),source_unchanged_from_cp24=False,reviewed_patch='docs/cp25-core-fix.patch',scope='DCJ11 MUL only: late R read and abort return'),limits=['Same bounded kernel-mode and explicit exclusions as CP24, plus MUL; no MMU.','MUL ordinary C fixture has 418 exclusions; all 512 encodings covered by normal/fault union. I/O directed cases use a read-clear CSR model.','Fit includes probes and FRAM/IRQ resolver, not full UART/timer/panel/SD/RK or external pin timing; no FPGA programming.','35/29.56 MHz pass; 50 MHz and preferred 900-1000 LUT remain unmet.'],fixtures_uncompressed_sha256=fixtures,vendor_models_sha256=models,files={str(p.relative_to(ROOT)):sha256(p.read_bytes()) for p in sorted(files) if p.is_file()}))
    print(f'CP25 recorded: {len(archives)} archives/{raw} raw hashes; {normal} instruction cases + {faults} fault frames per memory/ROM pair; {runs} benchmarks/ROM; all 112 old results unchanged, 120 portable/vendor results equal.')
if __name__=='__main__':main()
