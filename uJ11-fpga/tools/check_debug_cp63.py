#!/usr/bin/env python3
"""Lint against frozen baseline, negative controls, exact HC1200 source proof."""
import hashlib
import json
import re
import subprocess
import tarfile
from board_common import ROOT, CORE, BOARD
from build_debug_cp63 import OUT


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    report={}
    out=OUT/'checks';out.mkdir(parents=True,exist_ok=True)
    profile=json.loads((OUT/'inputs.json').read_text())
    gate=json.loads((ROOT/'build/cp63b/result.json').read_text())
    for name,digest in profile['outputs'].items():
        assert sha(ROOT/name)==digest==gate['inputs']['files'][name],name
    assert gate['timing_pass'] and gate['fully_routed'] and gate['applied_clock_mhz']==31.824
    report['synthesis_source_match']=True
    # Existing unused parameters/wires in the frozen base are not new debug
    # warnings. Compare exact diagnostics, instead of globally waiving lint.
    base_manifest=json.loads((ROOT/'synth/reports/cp62a/inputs.json').read_text())
    with tarfile.open(ROOT/'synth/reports/cp62a/source.tgz') as ar:
        prefix='build/cp62-boot/'
        for name in ('decode.mem','firmware.mem'):
            assert ar.extractfile(prefix+name).read()==(OUT/name).read_bytes(),name
        old_words=ar.extractfile(prefix+'m0.mem').read().decode().splitlines()
        new_words=(OUT/'m0.mem').read_text().splitlines()
        labels=json.loads((OUT/'m0.labels.json').read_text())
        changed=[i for i in range(1024) if old_words[i]!=new_words[i]]
        assert changed==sorted(labels[n] for n in ('S_DEBUG','S_DEBUG_VECTOR','S_DEBUG_LINK'))
        changed_rtl=[]
        for name in CORE+BOARD+['boards/hc1200/uj11_microcomp.v']:
            old=ar.extractfile(prefix+'src/'+name).read().decode().replace(prefix,'build/cp63-debug/')
            if old!=(OUT/'src'/name).read_text():changed_rtl.append(name)
        assert set(changed_rtl)=={'rtl/uj11_core.v','rtl/uj11_engine.v','rtl/uj11_microseq.v',
            'boards/hc1200/uj11_board.v','boards/hc1200/uj11_board_bus.v','boards/hc1200/uj11_microcomp.v'}
        report['baseline_comparison']=dict(changed_microaddresses=changed,changed_rtl=changed_rtl,
            decode_identical=True,firmware_identical=True,existing_microinstructions_unchanged=True)
        for name in CORE:
            path='build/cp62-boot/src/'+name;data=ar.extractfile(path).read()
            assert hashlib.sha256(data).hexdigest()==base_manifest['files'][path]
            p=out/'baseline'/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
    diagnostics=[]
    for tag,prefix in (('baseline',out/'baseline'),('cp63',OUT/'src')):
        for mode in (0,1):
            sources=[str(prefix/p) for p in CORE]+['rtl/uj11_rom.v']
            r=subprocess.run(['verilator','--lint-only','--Wall','--top-module','uj11_core',
                f'-GROM_DECODE={mode}','-GALIGNED_WORD_READS=1','-GIRQ_VECTOR_BITS=15']+sources,cwd=ROOT,capture_output=True,text=True)
            (out/f'{tag}-lint-{mode}.log').write_text(r.stdout+r.stderr)
            errors=re.findall(r'^%Error: (.*)$',r.stderr,re.M)
            assert all(re.fullmatch(r'Exiting due to \d+ warning\(s\)',e) for e in errors),r.stderr
            warnings=re.findall(r'^%Warning-([^:]+): .*?/([^/\s]+\.v):\d+:\d+: (.*)$',r.stderr,re.M)
            assert len(warnings)==r.stderr.count('%Warning-'),r.stderr
            assert all(kind in ('UNUSEDPARAM','UNUSEDSIGNAL') for kind,_,_ in warnings),warnings
            diagnostics.append((tag,mode,sorted(warnings)))
    for mode in (0,1):assert diagnostics[mode][2]==diagnostics[mode+2][2],diagnostics
    report['core_lint_no_new_warnings']=diagnostics
    r=subprocess.run(['verilator','--lint-only','--Wall','--top-module','uj11_button',
        'boards/hc1200/uj11_button.v','boards/hc1200/uj11_tick.v'],cwd=ROOT,capture_output=True,text=True)
    (out/'button-lint.log').write_text(r.stdout+r.stderr);r.check_returncode()
    report['button_strict_lint']=True
    sources=['tb/tb_debug_cp63.v']+[str((OUT/'src'/p).relative_to(ROOT)) for p in CORE]+['rtl/uj11_rom.v']
    engine=(OUT/'src/rtl/uj11_engine.v').read_text();seq=(OUT/'src/rtl/uj11_microseq.v').read_text()
    entry=profile['debug_entry']
    mutations=[('no-stop','rtl/uj11_engine.v',engine,'wire debug_wanted=debug_enabled',
                "wire debug_wanted=1'b0 && debug_enabled"),
               ('no-wait-resume','rtl/uj11_microseq.v',seq,"if (wait_return) next_address = 10'h012;",''),
               ('loader-route','rtl/uj11_microseq.v',seq,f"debug_pending ? 10'h{entry:03x}","debug_pending ? 10'h053"),
               ('lost-trace','rtl/uj11_engine.v',engine,'debug_trace<=trace_pending;',"debug_trace<=1'b0;")]
    negatives=[]
    for name,path,text,old,new in mutations:
        assert text.count(old)==1
        p=out/name/path;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text.replace(old,new))
        changed=sources.copy();changed[changed.index(str((OUT/'src'/path).relative_to(ROOT)))]=str(p.relative_to(ROOT))
        binary=out/(name+'.vvp')
        subprocess.run(['iverilog','-g2012','-I'+str(OUT),'-s','tb_debug_cp63','-o',str(binary),
                        '-Ptb_debug_cp63.ROM_DECODE=0']+changed,cwd=ROOT,check=True,capture_output=True)
        r=subprocess.run(['vvp',str(binary)],cwd=ROOT,capture_output=True,text=True)
        log=out/(name+'.log');log.write_text(r.stdout+r.stderr)
        assert r.returncode!=0 and 'FATAL:' in r.stdout and 'PASS CP63 CPU' not in r.stdout,name
        negatives.append(dict(name=name,rejected=True,log_sha256=sha(log),source_sha256=sha(p)))
    report['negative_controls']=negatives
    report['sources']={p:sha(ROOT/p) for p in sources+['tools/check_debug_cp63.py','tools/test_debug_cp63.py',
        str((OUT/'debug_cases.vh').relative_to(ROOT)),str((OUT/'debug_constants.vh').relative_to(ROOT))]}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS CP63: exact synthesis sources; no new core lint warnings; strict button lint; four behavioral mutations rejected')


if __name__=='__main__':main()
