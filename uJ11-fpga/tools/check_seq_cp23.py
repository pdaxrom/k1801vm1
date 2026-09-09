#!/usr/bin/env python3
"""Prove the CP23 sequencer equivalent to frozen CP22 RTL with Yosys SAT."""
import argparse,hashlib,json,os,subprocess,tarfile,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--yosys',default='yosys')
 args=p.parse_args()
 old_path='rtl/uj11_microseq.v'
 with tarfile.open(ROOT/'synth/reports/cp22c/source.tgz') as a:old=a.extractfile(old_path).read().decode()
 new=(ROOT/old_path).read_text()
 assert old.count('module uj11_microseq (')==new.count('module uj11_microseq (')==1
 # The register update block must be identical as an additional structural check.
 assert old.split('    always @(posedge clk)',1)[1]==new.split('    always @(posedge clk)',1)[1]
 script='''read_verilog -sv gold.v gate.v
proc
memory
opt_clean
equiv_make gold gate equiv
hierarchy -top equiv
equiv_simple
equiv_induct -seq 4
equiv_status -assert
'''
 results={}
 with tempfile.TemporaryDirectory(prefix='uj11-cp23-equiv-') as d:
  work=Path(d);(work/'gold.v').write_text(old.replace('module uj11_microseq (','module gold ('))
  (work/'proof.ys').write_text(script)
  for negative in [False,True]:
   candidate=new
   if negative:
    # Change the repair vector; the same proof must reject this RTL.
    needle="if (fault_repair) next_address = 10'h015;"
    assert candidate.count(needle)==1
    candidate=candidate.replace(needle,"if (fault_repair) next_address = 10'h014;")
   (work/'gate.v').write_text(candidate.replace('module uj11_microseq (','module gate ('))
   r=subprocess.run([args.yosys,'-s','proof.ys'],cwd=work,env=dict(os.environ,YOWASP_CACHE_DIR=str(ROOT/'build/yowasp-cache')),text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
   kind='negative' if negative else 'positive';(ROOT/f'build/cp23-seq-equivalence-{kind}.log').write_text(r.stdout)
   if negative:assert r.returncode!=0 and 'unproven' in r.stdout,r.stdout[-2000:]
   else:assert r.returncode==0 and 'Equivalence successfully proven!' in r.stdout,r.stdout[-2000:]
   results[kind]=dict(returncode=r.returncode,log_sha256=hashlib.sha256(r.stdout.encode()).hexdigest())
 (ROOT/'build/cp23-seq-equivalence.json').write_text(json.dumps(dict(method='Yosys equiv_simple + equiv_induct -seq 4 + equiv_status -assert; all inputs unconstrained; unchanged state update block; deliberate changed fault-repair vector must fail.',baseline='cp22c',baseline_sha256=hashlib.sha256(old.encode()).hexdigest(),candidate_sha256=hashlib.sha256(new.encode()).hexdigest(),results=results),indent=2)+'\n')
 print('PASS CP23 sequencer: SAT equivalence + induction; identical state updates; altered fault-repair vector rejected.')
if __name__=='__main__':main()
