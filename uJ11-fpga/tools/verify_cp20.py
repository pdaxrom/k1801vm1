#!/usr/bin/env python3
"""Reproduce CP20 portable/vendor tests, benchmark comparisons and evidence."""
import argparse,concurrent.futures,os,shutil,subprocess,sys
from pathlib import Path
from verify_cp17 import BENCH,STEMS as OLD_STEMS
ROOT=Path(__file__).resolve().parents[1]
SUITES=['ea','single','branch','byte','single_byte','control','extra','trap','irq','illegal','bus-fault','trace_bit','system_flags','psw_transfer','psw-transfer-fault','system_control']
STEMS=OLD_STEMS+[f'{s}-benchmarks-{m}' for s in ['trace','system-flags','psw-transfer','system-control'] for m in [-1,0,1,2]]
VENDOR={
 'base':['vendor-test','vendor-engine','vendor-memory-engine','vendor-core','vendor-fram'],
 'isa-a':['vendor-ea','vendor-single','vendor-branch','vendor-control','vendor-control-faults','vendor-extra','vendor-extra-faults'],
 'isa-b':['vendor-byte','vendor-single-byte','vendor-trap','vendor-trap-faults','vendor-irq','vendor-irq-faults','vendor-irq-lsi11','vendor-illegal','vendor-illegal-faults'],
 'faults':['vendor-bus-fault','vendor-bus-fault-system','vendor-bus-fault-double','vendor-trace-bit','vendor-trace-system','vendor-trace-faults','vendor-system-flags'],
 'psw-transfer':['vendor-psw-transfer','vendor-psw-transfer-fault','vendor-system-control','vendor-system-control-lsi11','vendor-system-control-faults'],
 'benchmarks':['vendor-benchmark','vendor-fram-benchmark','vendor-ea-benchmark','vendor-cp9-benchmark','vendor-byte-benchmark','vendor-control-benchmark','vendor-extra-benchmark','vendor-trap-benchmark','vendor-irq-benchmark','vendor-illegal-benchmark','vendor-trace-benchmark','vendor-system-flags-benchmark','vendor-psw-transfer-benchmark','vendor-system-control-benchmark']}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--phase',choices=['all','portable','vendor','record'],default='all');p.add_argument('--dry-run',action='store_true');args=p.parse_args()
 env=dict(os.environ)
 if 'LATTICE_SIM_DIR' not in env and (ROOT/'build/vendor/DP8KC.v').exists():env['LATTICE_SIM_DIR']=str(ROOT/'build/vendor')
 def run(command,log=None):
  print('Run:', ' '.join(command), '→ '+log if log else '',flush=True)
  if args.dry_run:return
  if log:
   with (ROOT/'build'/log).open('w') as out:subprocess.run(command,cwd=ROOT,env=env,stdout=out,stderr=subprocess.STDOUT,check=True)
  else:subprocess.run(command,cwd=ROOT,env=env,check=True)
 def snapshot(names):
  print('Snapshot',len(names),'portable result files',flush=True)
  if not args.dry_run:
   for n in names:
    path=ROOT/'build'/n;shutil.copyfile(path,path.with_stem(path.stem+'-portable'))
 if not args.dry_run:(ROOT/'build').mkdir(exist_ok=True)
 if args.phase in ['all','portable']:
  run(['make','all'])
  run(['make','test','test-fault-irq-order','test-system-flags'],'cp20-tests.log')
  snapshot([f'{s}-cycles-{m}.csv' for s in SUITES for m in [-1,2]])
  run(['make']+BENCH+['benchmark-trace','benchmark-system-flags','benchmark-psw-transfer','benchmark-system-control'],'cp20-benchmarks-portable.log')
  snapshot([s+'.json' for s in STEMS])
  run([sys.executable,'tools/check_decode_cp20.py'],'cp20-decode-equivalence.log')
  run([sys.executable,'tools/check_system_control_negative.py'],'cp20-negative-controls.log')
 if args.phase in ['all','vendor']:
  run(['make','all','build/isa_vectors.mem']+['build/'+s+'-vectors.txt' for s in SUITES],'cp20-vendor-prepare.log')
  with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
   futures=[pool.submit(run,['make']+targets,'cp20-vendor-'+name+'.log') for name,targets in VENDOR.items()]
   for f in concurrent.futures.as_completed(futures):f.result()
 if args.phase in ['all','record']:run([sys.executable,'tools/record_cp20.py'])
if __name__=='__main__':main()
