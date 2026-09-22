#!/usr/bin/env python3
"""Relink DEC FIS BASIC with a small .SFPA stack adapter for FPP hosts."""
import argparse,hashlib,json,re,shutil,subprocess
from pathlib import Path
from rt11_build import ROOT,Console


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def build(built,out):
    out.mkdir(parents=True,exist_ok=True);disk=out/'build.dsk'
    assert not disk.exists(),'fresh output directory required'
    shutil.copyfile(built/'build.dsk',disk)
    src=ROOT/'demos/rt11/basic/FISABI.MAC';local=out/src.name
    local.write_bytes(src.read_text().replace('\n','\r\n').encode('ascii'))
    command=(built/'B81FIS.COM').read_text().rstrip('\0').replace('B81FIS','B81FIJ')
    assert command.count('SUE1ID')==1
    command=command.replace('SUE1ID','FISABI\nSUE1ID')
    (out/'B81FIJ.COM').write_bytes(command.replace('\n','\r\n').encode('ascii'))
    rt=ROOT/'../lsi11/rt11tool'
    for p in (local,out/'B81FIJ.COM'):
        subprocess.run([str(rt),'add',str(disk),str(p),p.name],check=True,capture_output=True)
    ini=out/'build.ini';ini.write_text(f'set cpu 11/73\nset cpu 64k\nset hk0 rk07\nattach hk0 {disk}\nboot hk0\n')
    with (out/'console.log').open('wb') as log:
        c=Console(ini,log)
        try:
            c.expect(rb'RT-11FB')
            for _ in range(3):c.expect(rb'\r\n\.')
            c.send('SET SL OFF\r');c.expect(rb'\r\n\.')
            c.send('R MACRO\r');c.expect(rb'\*')
            c.send('FISABI,FISABI=FISABI\r');r=c.expect(rb'\*');assert b'Error' not in r,r
            c.send('\x03');c.expect(rb'\r\n\.')
            c.send('@B81FIJ\r');c.expect(rb'\?LINK-W-Undefined globals:\r\n')
            r=c.expect(rb'\.\.MSP\$\r\n');assert sorted(re.findall(rb'\.\.[A-Z]+\$',r))==[b'..MSP$',b'..NRC$',b'..UAC$'],r
            c.expect(rb'\r\n\.')
        finally:c.close()
    for name in ['FISABI.OBJ','FISABI.LST','B81FIJ.SAV','B81FIJ.MAP']:
        subprocess.run([str(rt),'extract',str(disk),str(out),name],check=True,capture_output=True)
    listing=(out/'FISABI.LST').read_text(errors='replace');assert re.search(r'Errors detected:\s+0',listing)
    mapping=(out/'B81FIJ.MAP').read_text(errors='replace')
    undefined=re.search(r'Undefined globals:\s*(.*?)\s*Transfer address',mapping,re.S)
    assert undefined and sorted(undefined[1].split())==['..MSP$','..NRC$','..UAC$'],mapping[-2000:]
    symbols={n:int(re.search(r'\b'+n+r'\s+([0-7]{6})',mapping)[1],8) for n in ['IFPMP','FIADPT']}
    p=out/'B81FIJ.SAV';blob=bytearray(p.read_bytes());a=symbols['IFPMP']
    get=lambda k:int.from_bytes(blob[k:k+2],'little')
    # Original setup: MOV #block,R0 / EMT 375 / RTS PC / 0 / block / handler.
    expected=[0o12700,a+0o12,0o104375,0o207,0,0o14000,a+0o16,0o10046]
    assert [get(a+2*i) for i in range(len(expected))]==expected,'unknown DEC handler layout'
    original_sha=sha(p);shutil.copyfile(p,out/'B81FIJ.unpatched')
    blob[a+0o14:a+0o16]=symbols['FIADPT'].to_bytes(2,'little');p.write_bytes(blob)
    subprocess.run([str(rt),'rm',str(disk),p.name],check=True,capture_output=True)
    subprocess.run([str(rt),'add',str(disk),str(p),p.name],check=True,capture_output=True)
    record=dict(source_sha256=sha(src),builder_sha256=sha(Path(__file__)),original_build_sha256=sha(built/'build-inputs.json'),
                unpatched_sha256=original_sha,symbols=symbols,patch=dict(address=a+0o14,before=a+0o16,after=symbols['FIADPT']),
                outputs={n:sha(out/n) for n in ['FISABI.OBJ','FISABI.LST','B81FIJ.SAV','B81FIJ.MAP','B81FIJ.COM']})
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n');return record

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--built',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();print(json.dumps(build(a.built.resolve(),a.out.resolve()),indent=2))
