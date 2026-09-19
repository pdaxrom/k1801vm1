#!/usr/bin/env python3
"""Generate/verify a bounded ODT PSW test; this tool never accesses hardware.

Run in a stopped FPTST job. Save and later restore the scratch words and
registers. Every test instruction is an FPP instruction, stepped through ODT.
The immutable CP80 firmware/release is not changed by this deployment test.
"""
import argparse
import json
import re
from pathlib import Path

BASE=0o6000


def plan():
    # Literal opcodes are ordinary PDP-11 test code, not microcode ROM.
    rows=[
        ('LDFPS #0',[0o170127,0],0o340,0,{}),
        ('LDF #1,AC0',[0o172427,0o40200],0o340,0,{}),
        ('STCFI AC0,@#177776',[0o175437,0o177776],1,0,{}),
        ('LDFPS @#177776',[0o170137,0o177776],1,1,{}),
        ('STFPS R0',[0o170200],1,1,{0:1}),
        ('STEXP AC0,@#177776',[0o175037,0o177776],1,0,{}),
        ('LDF #16,AC0',[0o172427,0o41200],1,0,{}),
        ('STCFI AC0,@#177776; cannot set T',[0o175437,0o177776],0,0,{}),
        ('LDF #1,AC0',[0o172427,0o40200],0,0,{}),
        ('NEGF AC0',[0o170700],0,0o10,{}),
        ('STCFI AC0,@#177776; negative',[0o175437,0o177776],0o174757,0o10,{}),
        ('LDFPS @#177776; reserved FPS mask',[0o170137,0o177776],0o174757,0o144757,{}),
        ('STFPS R1',[0o170201],0o174757,0o144757,{1:0o144757}),
        ('LDFPS #0',[0o170127,0],0o174757,0,{}),
        ('CLRF AC0',[0o170400],0o174757,4,{}),
        ('STCFI AC0,@#177776; zero',[0o175437,0o177776],0,4,{}),
        ('LDFPS @#177776; zero',[0o170137,0o177776],0,0,{}),
        ('STFPS R2',[0o170202],0,0,{2:0}),
        ('LDFPS #3000',[0o170127,0o3000],0,0o3000,{}),
        ('STFPS @#177776; reserved PSW bits',[0o170237,0o177776],0,0o3000,{}),
        ('LDFPS @#177776; reserved bits read zero',[0o170137,0o177776],0,0,{}),
        ('STFPS R3',[0o170203],0,0,{3:0}),
    ]
    words=[];steps=[]
    for mnemonic,code,psw,fps,regs in rows:
        words+=code
        steps.append(dict(instruction=mnemonic,pc=BASE+2*len(words),psw=psw,fps=fps,registers=regs))
    words.append(0o777) # Safe stopped-job loop beyond the last test step.
    return dict(base=BASE,words=words,initial_psw=0o340,steps=steps,
                writes=[f'W {BASE+2*i:o} {word:o}' for i,word in enumerate(words)],
                setup=[f'R 10 340',f'R 7 {BASE:o}'],
                commands=[cmd for _ in steps for cmd in ('S','F 6')])


def verify_capture(raw):
    text=raw.decode('ascii').replace('\r','');p=plan()
    actual=re.findall(r'R7=([0-7]{6})\nPSW=([0-7]{6})',text)
    fps=re.findall(r'FPS=([0-7]{6}) MODE=',text)
    expected=[(f'{s["pc"]:06o}',f'{s["psw"]:06o}') for s in p['steps']]
    assert actual==expected,dict(expected=expected,actual=actual)
    assert fps==[f'{s["fps"]:06o}' for s in p['steps']],fps
    assert text.rstrip().endswith('ODT>')
    return dict(passed=True,steps=len(actual),psw_checks=len(actual),fps_checks=len(fps))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify',type=Path)
    args=parser.parse_args()
    print(json.dumps(verify_capture(args.verify.read_bytes()) if args.verify else plan(),indent=2))
