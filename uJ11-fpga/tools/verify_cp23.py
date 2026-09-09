#!/usr/bin/env python3
"""Reproduce CP23 sequencer equivalence, full regression and identical timings."""
import argparse,concurrent.futures,os,shutil,subprocess,sys
from pathlib import Path
from verify_cp22 import SUITES,STEMS,VENDOR
from verify_cp17 import BENCH
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--phase',choices=['all','portable','vendor','record'],default='all');args=p.parse_args()
 env=dict(os.environ)
 if 'LATTICE_SIM_DIR' not in env and (ROOT/'build/vendor/DP8KC.v').exists():env['LATTICE_SIM_DIR']=str(ROOT/'build/vendor')
 yosys=env.get('YOSYS') or shutil.which('yosys') or shutil.which('yowasp-yosys')
 if args.phase in ['all','portable'] and not yosys:p.error('set YOSYS to yosys or yowasp-yosys (see tools/formal-requirements.txt)')
 (ROOT/'build').mkdir(exist_ok=True)
 def run(command,log=None):
  print('Run:', ' '.join(command), '→ '+log if log else '',flush=True)
  if log:
   with (ROOT/'build'/log).open('w') as out:subprocess.run(command,cwd=ROOT,env=env,stdout=out,stderr=subprocess.STDOUT,check=True)
  else:subprocess.run(command,cwd=ROOT,env=env,check=True)
 def snapshot(names):
  for n in names:
   path=ROOT/'build'/n;shutil.copyfile(path,path.with_stem(path.stem+'-portable'))
  print('Snapshot',len(names),'portable result files',flush=True)
 if args.phase in ['all','portable']:
  run(['make','all'])
  run([sys.executable,'tools/check_seq_cp23.py','--yosys',yosys],'cp23-formal-driver.log')
  run(['make','test-reference-core'],'cp23-core-tests.log')
  run(['make','test','test-fault-irq-order','test-system-flags'],'cp23-tests.log')
  snapshot([f'{s}-cycles-{m}.csv' for s in SUITES for m in [-1,2]])
  run(['make']+BENCH+['benchmark-trace','benchmark-system-flags','benchmark-psw-transfer','benchmark-system-control','benchmark-eis-ash','benchmark-eis-ashc'],'cp23-benchmarks-portable.log')
  snapshot([s+'.json' for s in STEMS])
 if args.phase in ['all','vendor']:
  run(['make','all','build/isa_vectors.mem']+['build/'+s+'-vectors.txt' for s in SUITES],'cp23-vendor-prepare.log')
  with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
   futures=[pool.submit(run,['make']+targets,'cp23-vendor-'+name+'.log') for name,targets in VENDOR.items()]
   for f in concurrent.futures.as_completed(futures):f.result()
 if args.phase in ['all','record']:run([sys.executable,'tools/record_cp23.py'])
if __name__=='__main__':main()
