#!/usr/bin/env python3
"""Check ASHC alias EA order and full 32-bit flags against an independent model.

The C records are preserved exactly. The count operand is read first, then
the destination pair is shifted. No mismatches or expected-output rewrites are allowed.
"""
import csv,hashlib,json,shutil,subprocess,tarfile,tempfile
from pathlib import Path
from check_psw_transfer_negative import records
ROOT=Path(__file__).resolve().parents[1]
def arithmetic_shift(value,count):
 overflow=carry=0
 for _ in range(count if count<32 else 64-count):
  if count<32:
   carry=value>>31;value=(value*2)&0xffffffff;overflow|=carry!=(value>>31)
  else:
   carry=value&1;value=(value>>1)|(value&0x80000000)
 return value,((value>>31)<<3)|((value==0)<<2)|(int(overflow)<<1)|carry
def main():
 subprocess.run(['make','build/eis_ashc_alias-vectors.txt','build/eis_ashc-vectors.txt','build/eis_ash-vectors.txt'],cwd=ROOT,check=True)
 # Register count: independently audit all even operand/NZVC edges and odd
 # duplicate inputs. This also checks full32 N/Z before aliased stores.
 ordinary=list(records('eis_ashc'));register_cases=ordinary[:15360]+ordinary[-2096:-48]
 assert len(register_cases)==17408
 for h,c,t in register_cases:
  op=int(h[1],16);rs=(op>>6)&7;pre=[int(v,16) for v in h[3:11]]
  assert op in [0o73002,0o73102] and int(c[0],16)==0
  value,flags=arithmetic_shift((pre[rs]<<16)|pre[rs|1],pre[2]&63)
  expected=[(int(h[2],16)&~15)|flags]+pre
  expected[8]=(pre[7]+2)&0xffff;expected[rs+1]=value>>16;expected[(rs|1)+1]=value&0xffff
  actual=[int(v,16) for v in t.splitlines()[2+int(h[11],16)].split()][:9]
  assert actual==expected,('independent register result mismatch',h,actual,expected)
 ash_aliases=0
 for h,c,t in records('eis_ash'):
  op=int(h[1],16);rs=(op>>6)&7;mode=(op>>3)&7
  if rs==7 or (op&7)!=rs or mode not in [2,4] or int(h[2],16)!=0 or int(c[0],16)!=0:continue
  lines=t.splitlines();idx=2+int(h[11],16)
  if lines[-1].split()[-1]!='0000':continue
  pre=[int(v,16) for v in h[3:11]];pre[rs]=(pre[rs]+(2 if mode==2 else -2))&0xffff;pre[7]=(pre[7]+2)&0xffff
  expected=[((pre[rs]>>15)<<3)|((pre[rs]==0)<<2)]+pre
  assert [int(v,16) for v in lines[idx].split()][:9]==expected,('ASH count-zero EA ordering',h)
  ash_aliases+=1
 assert ash_aliases==14,('ASH alias coverage',ash_aliases)
 raw=(ROOT/'build/eis_ashc_alias-vectors.txt').read_bytes()
 out=[];rows=[]
 for h,c,t in records('eis_ashc_alias'):
  lines=t.splitlines();rs=(int(h[1],16)>>6)&7;pre=list(map(lambda x:int(x,16),h[3:11]));idx=2+int(h[11],16)
  actual=list(map(lambda x:int(x,16),lines[idx].split()))
  assert int(c[0],16)==0 and int(c[1],16)==0 and int(c[2],16)==1
  patches={int(a,16):int(v,16) for a,v in (x.split() for x in lines[2:idx])};n=patches[0x7ffe]&63
  pre[7]=(pre[7]+2)&0xffff
  pre[rs]=(pre[rs]+(2 if ((int(h[1],16)>>3)&7)==2 else -2))&0xffff
  expected=[int(h[2],16)]+pre+[actual[-1]]
  x,flags=arithmetic_shift((pre[rs]<<16)|pre[rs|1],n)
  expected[1+rs]=x>>16;expected[1+(rs|1)]=x&0xffff
  expected[0]=(int(h[2],16)&~15)|flags
  row=dict(case=int(h[0],16),opcode=f'{int(h[1],16):06o}',rs=rs,count=n,destination_high_after_ea=pre[rs],c_psw=actual[0],reference_psw=expected[0],c_high=actual[rs+1],reference_high=expected[rs+1],c_low=actual[(rs|1)+1],reference_low=expected[(rs|1)+1])
  rows.append(row)
  assert expected==actual,('C reference contradicts documented ASHC result',row,expected,actual)
  out.append(t)
 assert len(rows)==256,'boundary checks incomplete'
 assert ''.join(out).encode()==raw,'C records must not be rewritten'
 (ROOT/'build/eis_ashc_alias-checked-vectors.txt').write_text(''.join(out))
 with (ROOT/'build/cp22-alias-independent.csv').open('w') as f:
  w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
 results={}
 with tempfile.TemporaryDirectory(prefix='uj11-ashc-alias-') as temp:
  root=Path(temp)
  with tarfile.open(ROOT/'synth/reports/cp22d/source.tgz') as a:a.extractall(root,filter='data')
  for rel in ['tb/tb_trace_bit.v','tb/uj11_ram.v','rtl/uj11_rom.v','reference/lsi11/spi_fram_model.v']:
   (root/rel).parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/rel,root/rel)
  (root/'build').mkdir(exist_ok=True);(root/'build/eis_ashc-vectors.txt').write_text(''.join(out))
  subprocess.run(['python3','microasm/uj11asm.py','microcode/m0.uasm','-o','microcode/generated/m0.mem'],cwd=root,check=True,stdout=subprocess.DEVNULL)
  rtl=[str(f.relative_to(root)) for f in sorted((root/'rtl').glob('*.v')) if f.name!='uj11_rom.v']
  ref=['reference/lsi11/spi_fram_model.v','reference/lsi11/spi_fram_guest_ram.v']
  for vendor in [False,True]:
   label='vendor' if vendor else 'portable'
   rom=['microcode/generated/uj11_m0_ebr.v']+[str(ROOT/'build/vendor'/n) for n in ['DP8KC.v','GSR.v','PUR.v']] if vendor else ['rtl/uj11_rom.v']
   for mode in [-1,2]:
    log=ROOT/f'build/cp22-alias-{label}-{mode}.log'
    with log.open('w') as f:
     f.write('Raw corrected C fixtures, independently checked against the 32-bit instruction definition.\n');f.flush()
     subprocess.run(['iverilog','-g2012']+(['-DVENDOR_ROM'] if vendor else [])+['-s','tb_trace_bit','-Ptb_trace_bit.EIS_ASHC=1',f'-Ptb_trace_bit.MEMORY_MODE={mode}','-o','build/alias','tb/tb_trace_bit.v','tb/uj11_ram.v']+rtl+ref+rom,cwd=root,check=True,stdout=f,stderr=subprocess.STDOUT)
     subprocess.run(['vvp','build/alias'],cwd=root,check=True,stdout=f,stderr=subprocess.STDOUT)
    data=(root/f'build/eis_ashc-cycles-{mode}.csv').read_bytes()
    path=ROOT/f'build/cp22-alias-cycles-{label}-{mode}.csv';path.write_bytes(data)
    if vendor:assert data==(ROOT/f'build/cp22-alias-cycles-portable-{mode}.csv').read_bytes()
    results[f'{label}/{mode}']=dict(cases=256,cycles_sha256=hashlib.sha256(data).hexdigest())
 report=dict(method='Actual corrected DCJ11 C fixtures independently checked against bit-by-bit 32-bit ASHC; destination pair read after count EA; NZ from 32-bit result before aliased stores.',raw_c_cases=len(rows),independent_register_cases=len(register_cases),independent_ash_alias_cases=ash_aliases,independent_reference_mismatches=0,rewritten_expected_records=0,raw_c_fixture_sha256=hashlib.sha256(raw).hexdigest(),rtl_tests=results)
 (ROOT/'build/cp22-alias-diagnostic.json').write_text(json.dumps(report,indent=2)+'\n')
 print('PASS ASHC alias reference: 256 alias cases x RAM/FRAM x portable/vendor + 17408 independent register and 14 ASH alias checks; zero independent-reference mismatches; no rewritten C expectations.')

if __name__=='__main__':main()
