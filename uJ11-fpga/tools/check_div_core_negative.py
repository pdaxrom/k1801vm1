#!/usr/bin/env python3
"""Reproduce the observed pre-CP26 C DIV bugs without rewriting fixtures."""
import csv,gzip,hashlib,json,shutil,subprocess,tarfile,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
    with tempfile.TemporaryDirectory(prefix='uj11-div-old-core-') as tmp:
        with tarfile.open(ROOT/'tb/reports/cp25-verification-source.tgz') as a:a.extractall(tmp,filter='data')
        old=Path(tmp)/'uJ11-fpga';build=old/'build';build.mkdir()
        core=(old/'../core/core.c').read_bytes()
        expected=json.loads((ROOT/'docs/verification-cp25.json').read_text())['oracle']['original_core_sha256']
        assert hashlib.sha256(core).hexdigest()==expected
        for n in ['Makefile','tb/eis_div_vectors.c','tb/eis_div_fault_vectors.c','tools/check_div_oracle.py','tools/check_div_algorithm.py']:
            shutil.copyfile(ROOT/n,old/n)
        with (ROOT/'build/cp26-old-core-build.log').open('w') as out:
            subprocess.run(['make','build/eis_div-vectors.txt','build/eis-div-fault-vectors.txt'],cwd=old,stdout=out,stderr=subprocess.STDOUT,check=True)
        r=subprocess.run(['python3','tools/check_div_oracle.py','tools/check_div_algorithm.py'],cwd=old,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        (ROOT/'build/cp26-old-core-normal-negative.log').write_text(r.stdout)
        assert r.returncode and 'independent DIV mismatch' in r.stdout
        # Count independently rejected examples of every corrected semantic.
        audit_code="""
import sys,json
sys.path.insert(0,'tools')
from check_div_oracle import records,decode_record,ordinary
from check_div_algorithm import reference
counts=dict(zero_even=0,negative_overflow_even=0,odd_success_extension=0,late_low_dividend=0)
for h,c,t in records('eis_div'):
 if int(h[2],16)&16 or int(c[0],16):continue
 op=int(h[1],16);r=[int(v,16) for v in h[3:11]]
 patches,post,bus=decode_record(h,c,t);psw,regs,expected=ordinary(h,patches)
 if (post[0],post[1:9],bus)==(psw,regs,expected):continue
 if op==0o71002:
  q,rem,flags=reference((r[0]<<16)|r[1],r[2])
  if r[2]==0:counts['zero_even']+=1
  elif q is None and flags==10:counts['negative_overflow_even']+=1
 if op==0o71102 and reference(r[1]*65537,r[2])[0] is not None:counts['odd_success_extension']+=1
 if op in [0o71021,0o71041] and r[0] in [0,65535] and r[1]==0x4000:counts['late_low_dividend']+=1
assert all(counts.values()) and counts['late_low_dividend']==128,counts
print(json.dumps(counts))
"""
        audit=subprocess.run(['python3','-c',audit_code],cwd=old,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,check=True)
        rejected=json.loads(audit.stdout)
        rows=list(csv.DictReader((build/'eis-div-fault-continuation.csv').open()))
        changed=sum(int(row['changed_register_mask'],16)!=0 or row['frame_psw']!=row['step_psw'] for row in rows)
        extra=sum(row['frame_beats']!=row['step_beats'] for row in rows)
        assert len(rows)==changed==2368 and extra==0,(len(rows),changed,extra)
        for n in ['eis_div-vectors.txt','eis_div-excluded.csv','eis-div-fault-vectors.txt','eis-div-fault-continuation.csv']:
            (ROOT/('build/cp26-old-core-'+n+'.gz')).write_bytes(gzip.compress((build/n).read_bytes(),mtime=0))
        for n in ['eis_div-oracle.log','eis-div-fault-oracle.log']:
            shutil.copyfile(build/n,ROOT/('build/cp26-old-core-'+n))
        result=dict(archive='cp25-verification-source.tgz',core_sha256=expected,normal_semantics_bug_rejected=True,independent_rejected_cases=rejected,fault_postframe_register_or_psw_changes=changed,fault_postframe_extra_bus=extra)
        (ROOT/'build/cp26-old-core-negative.json').write_text(json.dumps(result,indent=2)+'\n')
        print('PASS old C DIV negative: ordinary semantics mismatch; 2368 post-fault register/PSW changes; exact archived CP25 C')
if __name__=='__main__':main()
