#!/usr/bin/env python3
"""Reject deliberate FIS bugs with the real RTL; leave production inputs intact."""
import hashlib
import json
from pathlib import Path
import subprocess
from fis_reference import vectors
from make_fis_vectors import case
from link_fis import link
from uj11asm import assemble
from run_fis_tests import compile_test, ROOT


def main():
    folder=ROOT/'build/fis-negative';folder.mkdir(exist_ok=True)
    rows=[]
    for op,a,b in vectors(0):
        rows.append(case(len(rows),op,a,b,psw=0xa0 | (len(rows)%16)))
    for op in range(4):
        for reg in (0,6,7):
            for beat in range(1,7):
                for flags in (0,15):
                    rows.append(case(len(rows),op,0x41000000,0x40800000,reg,0xa0|flags,fault_beat=beat))
    fixture=folder/'vectors.txt';fixture.write_text('\n'.join(rows)+'\n')
    source=(ROOT/'microcode/fis.uasm').read_text()
    base=(ROOT/'microcode/m0.uasm').read_text()
    def rom(text,name):
        combined,_,_=link(base,text)
        image,_,_,_=assemble(combined)
        path=folder/(name+'.mem');path.write_text(''.join(f'{w:09x}\n' for w in image))
        return path
    correct=rom(source,'correct')
    binary=compile_test(candidate=True,tag='fis-negative')
    def run(command,name,image,negative=True):
        r=subprocess.run(command+[f'+rom={image}',f'+vectors={fixture}',
            f'+results={folder/name}.csv'],cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        (folder/(name+'.log')).write_text(r.stdout)
        if negative:
            assert r.returncode!=0 and 'FIS case' in r.stdout, (name,r.stdout[-2000:])
        else:
            assert r.returncode==0 and f': {len(rows)} exact' in r.stdout,r.stdout[-2000:]
        return {'name':name,'rejected':negative and r.returncode!=0,
                'log_sha256':hashlib.sha256(r.stdout.encode()).hexdigest()}
    run(binary,'positive',correct,False)
    def region(start,end,old,new):
        a=source.index(start+':');b=source.index(end+':',a) if end else len(source)
        part=source[a:b]
        assert part.count(old)==1,(start,old,part.count(old))
        return source[:a]+part.replace(old,new)+source[b:]
    mutations=[
        ('halfway-toward-zero','FIS_ROUND','FIS_RANGE','imm=32','imm=31'),
        ('lost-sticky','FIS_ALIGN_JAM','FIS_ALIGN_NEXT','alu OR, pair=DQ, d=ONE, dst=Q','alu PASSB, pair=ZQ, dst=Q'),
        ('wrong-hidden-bit','FIS_EXP_SHIFT','FIS_A_ZERO','imm=128','imm=64'),
        ('dirty-zero-is-nonzero','FIS_EXP_SHIFT','FIS_A_ZERO','cond=Z, target=FIS_A_ZERO','cond=NOT_Z, target=FIS_A_ZERO'),
        ('mul-29-iterations','FIS_MULDIV_DISPATCH','FIS_MUL_LOOP','imm=30','imm=29'),
        ('div-29-iterations','FIS_DIV','FIS_DIV_LOOP','imm=30','imm=29'),
        ('lost-quotient-bit','FIS_DIV_LOOP','FIS_DIV_RESTORE','alu OR, pair=DQ, d=ONE, dst=Q','alu PASSB, pair=ZQ, dst=Q'),
        ('lost-negative-borrow','FIS_ADD_ABS','FIS_MULDIV','alu SBC, pair=ZB','alu SUB, pair=ZB'),
        ('wrong-divzero-signature','FIS_DIVZERO','FIS_ERROR','imm=11','imm=3'),
        ('add-underflow-traps','FIS_UNDERFLOW','FIS_OVERFLOW','CJUMP, cond=Z, target=FIS_ZERO, prefetch=0','alu PASSA'),
        ('missing-stack-advance','FIS_COMMIT','FIS_UNDERFLOW','alu ADD, a=RD, pair=AD, d=IMM, imm=4, b=RD, dst=RF','alu PASSA'),
        ('flags-on-failed-write','FIS_STORE','FIS_STORE_LOW','alu OR, a=T6, b=T4, flags=LOAD','alu PASSA'),
        ('lost-trap-ipl','FIS_ERROR',None,'alu BIC, a=T1, pair=DA, d=PSW, b=T4, dst=RF','alu PASSA, pair=DA, d=ZERO, b=T4, dst=RF'),
    ]
    results=[]
    for name,start,end,old,new in mutations:
        image=rom(region(start,end,old,new),name)
        results.append(run(binary,name,image))
        print('Rejected:',name,flush=True)
    for name,path,old,new in [
        ('missing-dq','rtl/uj11_datapath.v','pair==3 || pair==5 || pair==6','pair==3 || pair==6'),
        ('missing-decode','rtl/uj11_decode.v',"wire fis = ir[15:5]==11'o1720","wire fis = 1'b0")]:
        original=(ROOT/path).read_text();assert original.count(old)==1
        altered=folder/(name+'.v');altered.write_text(original.replace(old,new))
        executable=compile_test(candidate=True,tag='fis-negative-'+name,replacements={path:str(altered)})
        results.append(run(executable,name,correct))
        print('Rejected:',name,flush=True)
    report={'positive_cases':len(rows),'fixture_sha256':hashlib.sha256(fixture.read_bytes()).hexdigest(),
            'production_microcode_sha256':hashlib.sha256(source.encode()).hexdigest(),'results':results}
    (ROOT/'build/cp27-fis-negative.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'PASS FIS negative controls: {len(results)} mutations rejected; production sources untouched')


if __name__=='__main__':main()
