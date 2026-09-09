#!/usr/bin/env python3
"""Reproduce the two real pre-CP25 C MUL bugs without rewriting fixtures."""
import csv,gzip,hashlib,json,shutil,subprocess,tarfile,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
    with tempfile.TemporaryDirectory(prefix='uj11-mul-old-core-') as tmp:
        with tarfile.open(ROOT/'tb/reports/cp24-verification-source.tgz') as a:a.extractall(tmp,filter='data')
        old=Path(tmp)/'uJ11-fpga';build=old/'build';build.mkdir()
        core=(old/'../core/core.c').read_bytes()
        expected=json.loads((ROOT/'docs/verification-cp24.json').read_text())['oracle']['original_core_sha256']
        assert hashlib.sha256(core).hexdigest()==expected
        for n in ['Makefile','tb/eis_mul_vectors.c','tb/eis_mul_fault_vectors.c','tools/check_mul_oracle.py']:
            shutil.copyfile(ROOT/n,old/n)
        with (ROOT/'build/cp25-old-core-build.log').open('w') as out:
            subprocess.run(['make','build/eis_mul-vectors.txt','build/eis-mul-fault-vectors.txt'],cwd=old,stdout=out,stderr=subprocess.STDOUT,check=True)
        r=subprocess.run(['python3','tools/check_mul_oracle.py'],cwd=old,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        (ROOT/'build/cp25-old-core-normal-negative.log').write_text(r.stdout)
        assert r.returncode and 'independent MUL mismatch' in r.stdout
        rows=list(csv.DictReader((build/'eis-mul-fault-continuation.csv').open()))
        changed=sum(int(row['changed_register_mask'],16)!=0 or row['frame_psw']!=row['step_psw'] for row in rows)
        extra=sum(row['frame_beats']!=row['step_beats'] for row in rows)
        assert len(rows)==changed==2368 and extra==0,(len(rows),changed,extra)
        for n in ['eis_mul-vectors.txt','eis_mul-excluded.csv','eis-mul-fault-vectors.txt','eis-mul-fault-continuation.csv']:
            (ROOT/('build/cp25-old-core-'+n+'.gz')).write_bytes(gzip.compress((build/n).read_bytes(),mtime=0))
        for n in ['eis_mul-oracle.log','eis-mul-fault-oracle.log']:
            shutil.copyfile(build/n,ROOT/('build/cp25-old-core-'+n))
        result=dict(archive='cp24-verification-source.tgz',core_sha256=expected,late_register_bug_rejected=True,fault_postframe_register_or_psw_changes=changed,fault_postframe_extra_bus=extra)
        (ROOT/'build/cp25-old-core-negative.json').write_text(json.dumps(result,indent=2)+'\n')
        print('PASS old C MUL negative: late register mismatch; 2368 post-fault register/PSW changes; exact archived CP24 C')
if __name__=='__main__':main()
