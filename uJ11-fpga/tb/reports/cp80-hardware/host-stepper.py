#!/usr/bin/env python3
"""Run the qualified CP80 UART probe, stopping at the first mismatch."""
import json,re,subprocess
from pathlib import Path
root=Path("/tmp/uj11-cp80-hardware-20260919")
out=root/"14-psw-steps"
assert not out.exists(),"use a fresh run; do not overwrite evidence"
out.mkdir()
steps=[(3076,224,0),(3080,224,0),(3084,1,0),(3088,1,1),(3090,1,1),(3094,1,0),(3098,1,0),(3102,0,0),(3106,0,0),(3108,0,8),(3112,63983,8),(3116,63983,51695),(3118,63983,51695),(3122,63983,0),(3124,63983,4),(3128,0,4),(3132,0,0),(3134,0,0),(3138,0,1536),(3142,0,1536),(3146,0,0),(3148,0,0)]
checks=[]
with (out/"uart.bin").open("wb") as combined:
    for i,(pc,psw,fps) in enumerate(steps,1):
        part=out/f"{i:02d}"
        args=["python3",str(root/"hardware_uart.py"),"--pause-pid","217518",
              "--out",str(part),"--expect-odt-prompt","--listen","0.1",
              "--command-wait","15","--command","S","--command","F 6"]
        r=subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        (part/"host.log").write_bytes(r.stdout)
        raw=(part/"uart.bin").read_bytes()
        combined.write(raw);combined.flush()
        r.check_returncode()
        s=raw.decode("ascii").replace("\r","")
        actual=re.findall(r"R7=([0-7]{6})\nPSW=([0-7]{6})",s)
        fp=re.findall(r"FPS=([0-7]{6}) MODE=",s)
        assert actual==[(f"{pc:06o}",f"{psw:06o}")],(i,actual,pc,psw)
        assert fp==[f"{fps:06o}"],(i,fp,fps)
        assert s.rstrip().endswith("ODT>")
        checks.append(dict(step=i,pc=pc,psw=psw,fps=fps))
        (out/"progress.json").write_text(json.dumps(checks,indent=2)+"\n")
        print(f"PASS STEP {i:02d}: PC={pc:06o} PSW={psw:06o} FPS={fps:06o}",flush=True)
(out/"verification.json").write_text(json.dumps(dict(passed=True,steps=checks),indent=2)+"\n")
