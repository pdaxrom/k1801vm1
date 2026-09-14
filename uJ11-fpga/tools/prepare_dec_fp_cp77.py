#!/usr/bin/env python3
"""Extract the three DEC FP11-A diagnostics from the local read-only XXDP 2.5 disk.
Block numbers/lengths come from its native D *.* listing. Verify each linked
block and every DEC absolute-loader record; never write the original disk.
"""
import hashlib,json,struct
from pathlib import Path
from board_common import ROOT

def sha(data):return hashlib.sha256(data).hexdigest()

def prepare():
    out=ROOT/'build/cp77-diagnostics';out.mkdir(parents=True,exist_ok=True)
    disk=ROOT/'../lsi11/disks/xxdp25.dsk';data=disk.read_bytes();before=sha(data)
    entries=[]
    for name,start,blocks in [('FFPAA1.BIN',0o2550,60),('FFPBA0.BIN',0o2644,59),('FFPCB0.BIC',0o2737,59)]:
        chain=[];blob=b'';cur=start
        while cur:
            assert cur not in chain and 0<cur<len(data)//512
            chain.append(cur);block=data[cur*512:(cur+1)*512]
            blob+=block[2:];cur=int.from_bytes(block[:2],'little')
        assert len(chain)==blocks
        pos=0;records=[];memory=bytearray(65536);used=bytearray(65536);entry=None
        while pos<len(blob):
            if not blob[pos]:pos+=1;continue
            assert blob[pos:pos+2]==b'\x01\x00'
            count,address=struct.unpack_from('<HH',blob,pos+2)
            assert count>=6 and pos+count<len(blob)
            record=blob[pos:pos+count+1];assert sum(record)&255==0
            pos+=count+1
            if count==6:entry=address;break
            end=address+count-6;assert end<=0o160000 and not any(used[address:end])
            memory[address:end]=record[6:-1];used[address:end]=b'\1'*(end-address)
            records.append([address,count-6])
        assert entry is not None and not any(blob[pos:])
        # Odd transfer address 1 means HALT to loader; standard standalone
        # diagnostic entry is 0200. Preserve the original transfer unchanged.
        (out/name).write_bytes(blob)
        (out/(name+'.ram')).write_bytes(memory)
        entries.append(dict(name=name,start=start,blocks=blocks,chain=chain,
                            file_sha256=sha(blob),ram_sha256=sha(memory),records=records,
                            transfer=entry,standalone_start=0o200,high=max(a+n for a,n in records)))
    assert sha(disk.read_bytes())==before
    report=dict(disk=str(disk.relative_to(ROOT)),disk_sha256=before,entries=entries,
                directory_source='XXDP 2.5 native D *.* on read-only SIMH RL02',
                identification='FP11-A manual chapter 7.2, DFFPA/DFFPB/DFFPC',
                script_sha256=sha(Path(__file__).read_bytes()))
    (out/'extraction.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({e['name']:{'records':len(e['records']),'high_octal':f"{e['high']:06o}"} for e in entries},indent=2))
    return report

if __name__=='__main__':prepare()
