#!/usr/bin/env python3
"""Export a JED only from a complete, source-matched HC1200 MAP/PAR/TRACE gate."""
import argparse, hashlib, json, os, re, subprocess
from pathlib import Path
from board_common import ROOT


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('name')
    a=parser.parse_args()
    assert re.fullmatch(r'[a-z][a-z0-9-]*',a.name)
    out=ROOT/'build'/a.name
    result=json.loads((out/'result.json').read_text())
    assert result['device']=='LCMXO2-1200HC-4SG32C'
    assert result['diamond_returncode']==0 and result['fully_routed'] and result['timing_pass']
    prefix=out/'impl1'/(a.name+'_impl1')
    for name,want in result['inputs']['files'].items():
        if name.startswith('generated:'):
            generated=name.removeprefix('generated:')
            assert generated in ('clock.lpf','build.tcl'),name
            path=out/generated
        else:path=ROOT/name
        assert hashlib.sha256(path.read_bytes()).hexdigest()==want, str(path)
    for suffix,want in result['reports'].items():
        assert hashlib.sha256(prefix.with_suffix(suffix).read_bytes()).hexdigest()==want,suffix
    tcl=out/'export.tcl'
    tcl.write_text('cd [file dirname [file normalize [info script]]]\n'
        'if {[catch {\nprj_project open '+a.name+'.ldf\n'
        'prj_run Export -impl impl1 -task Jedecgen\nprj_project close\n'
        '} message]} {puts stderr $message; exit 1}\nexit 0\n')
    diamond=Path(os.environ.get('DIAMOND_HOME',str(Path.home()/'.local/lscc/diamond/3.14')))/'bin/lin64/diamondc'
    with (out/'export.log').open('w') as log:
        subprocess.run([str(diamond),str(tcl)],cwd=ROOT,
            env=dict(os.environ,LD_PRELOAD=os.environ.get('DIAMOND_LIBSTDCPP','/lib/x86_64-linux-gnu/libstdc++.so.6')),
            stdout=log,stderr=subprocess.STDOUT,check=True)
    jed=prefix.with_suffix('.jed')
    checksums=re.findall(r'^C([0-9A-Fa-f]+)\*',jed.read_text(),re.M)
    assert checksums
    record=dict(name=a.name,jed=str(jed),sha256=hashlib.sha256(jed.read_bytes()).hexdigest(),
                checksum=checksums[-1],input_revision_sha256=result['inputs']['input_revision_sha256'])
    (out/'jed.json').write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps(record,indent=2))
if __name__=='__main__':main()
