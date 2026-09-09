#!/usr/bin/env python3
"""Reproduce CP26 DIV regression, frozen old-opcode behavior and synthesis evidence."""
import argparse,concurrent.futures,os,shutil,subprocess,sys
from pathlib import Path
from verify_cp25 import SUITES as OLD_SUITES,STEMS as OLD_STEMS,VENDOR as OLD_VENDOR
SUITES=OLD_SUITES+['eis_div','eis-div-fault']
STEMS=OLD_STEMS+[f'eis-div-benchmarks-{m}' for m in [-1,0,1,2]]
# Start long, independent groups first; every group owns its result files.
VENDOR={n:OLD_VENDOR[n] for n in ['eis-ashc','psw-transfer','faults']}
VENDOR['eis-div']=['vendor-eis-div','vendor-eis-div-fault','vendor-div-io']
VENDOR.update({n:v for n,v in OLD_VENDOR.items() if n not in VENDOR})
VENDOR['eis-div-benchmark']=['vendor-eis-div-benchmark']
from verify_cp17 import BENCH
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--phase',choices=['all','portable','vendor','record'],default='all');args=p.parse_args()
 env=dict(os.environ)
 if 'LATTICE_SIM_DIR' not in env and (ROOT/'build/vendor/DP8KC.v').exists():env['LATTICE_SIM_DIR']=str(ROOT/'build/vendor')
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
  run([sys.executable,'tools/check_decode_cp26.py'],'cp26-decode-equivalence.log')
  run(['make','test-reference-core'],'cp26-core-tests.log')
  run(['make','-j4','test','test-fault-irq-order','test-system-flags'],'cp26-tests.log')
  run([sys.executable,'tools/check_div_oracle.py'],'cp26-div-oracle.log')
  run([sys.executable,'tools/check_div_negative.py'],'cp26-negative-controls.log')
  run([sys.executable,'tools/check_div_core_negative.py'],'cp26-old-core-negative.log')
  run([sys.executable,'tools/check_div_algorithm.py'],'cp26-div-algorithm.log')
  snapshot([f'{s}-cycles-{m}.csv' for s in SUITES for m in [-1,2]])
  run(['make','-j4']+BENCH+['benchmark-trace','benchmark-system-flags','benchmark-psw-transfer','benchmark-system-control','benchmark-eis-ash','benchmark-eis-ashc','benchmark-eis-xor','benchmark-eis-mul','benchmark-eis-div'],'cp26-benchmarks-portable.log')
  snapshot([s+'.json' for s in STEMS])
 if args.phase in ['all','vendor']:
  run(['make','all','build/isa_vectors.mem']+['build/'+s+'-vectors.txt' for s in SUITES],'cp26-vendor-prepare.log')
  with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
   futures=[pool.submit(run,['make']+targets,'cp26-vendor-'+name+'.log') for name,targets in VENDOR.items()]
   for f in concurrent.futures.as_completed(futures):f.result()
 if args.phase in ['all','record']:run([sys.executable,'tools/record_cp26.py'])
if __name__=='__main__':main()
