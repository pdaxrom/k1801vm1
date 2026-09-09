#!/usr/bin/env python3
"""Create a separate RT-11 image with the four existing HC1200 test programs."""
import argparse,hashlib,json,shutil,subprocess,tempfile
from pathlib import Path
from board_common import ROOT


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--base',type=Path,default=ROOT/'../lsi11-fpga/images/rt11v503.dsk')
    p.add_argument('--output',type=Path,default=ROOT/'build/rt11-uj11-panel-hg.dsk')
    a=p.parse_args()
    base=a.base.resolve();out=a.output.resolve()
    assert base!=out and not out.exists(),'Choose a new output path; existing images are preserved'
    before=hashlib.sha256(base.read_bytes()).hexdigest()
    out.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(base,out)
    tool=ROOT/'../lsi11/rt11tool'
    programs=sorted(list((ROOT/'demos/rt11').rglob('*.SAV'))+list((ROOT/'demos/rt11').rglob('*.SYS')))
    assert len(programs)==4
    with tempfile.TemporaryDirectory(prefix='uj11-image-') as d:
        for program in programs:
            subprocess.run([str(tool),'add',str(out),str(program),program.name],check=True)
            subprocess.run([str(tool),'extract',str(out),d,program.name],check=True)
            assert (Path(d)/program.name).read_bytes()==program.read_bytes(),program
    assert hashlib.sha256(base.read_bytes()).hexdigest()==before,'Base image changed'
    record=dict(base_sha256=before,image_sha256=hashlib.sha256(out.read_bytes()).hexdigest(),
                programs={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in programs})
    out.with_suffix('.json').write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps(record,indent=2))
if __name__=='__main__':main()
