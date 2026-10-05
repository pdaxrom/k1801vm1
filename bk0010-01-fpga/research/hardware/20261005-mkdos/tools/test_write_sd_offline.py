from pathlib import Path
import fcntl,gzip,hashlib,importlib.util,json,os,stat,struct,sys
root=Path('build/write-sd-offline')
root.mkdir(exist_ok=True)
evidence=Path('research/hardware/20261005-mkdos')
for name in ['mbr','fat16','partition2','partition3','partition4','extended']:
 source=evidence/(name+'-before.bin.gz')
 (root/(name+'-before.bin')).write_bytes(gzip.decompress(source.read_bytes()))
(root/'extended-before.json').write_bytes((evidence/'extended-before.json').read_bytes())
prepared=Path('build/two-disks-final-prepared')
desc=json.loads((prepared/'prepared.json').read_text())
size=desc['card_sectors']*512
fixture=root/'card-sparse.bin'
with fixture.open('wb') as f:
 f.truncate(size)
 for name,lba in [('mbr',0),('fat16',2048),('partition2',34816),('partition3',36864),('extended',40960)]:
  f.seek(lba*512);f.write((root/(name+'-before.bin')).read_bytes())
with fixture.open('rb') as f: before=bytearray(f.read(49152*512))
expected=bytearray(before)
for item in desc['regions']:
 data=(prepared/item['name']).read_bytes();base=item['start_lba']*512
 expected[base:base+len(data)]=data
expected[:512]=(prepared/'mbr-after.bin').read_bytes()
expected[2048*512:34816*512]=(prepared/'fat16-after.bin').read_bytes()
log=root/'offline-validation.log'
log.write_text('OFFLINE WRITER TEST ONLY\nPASS disk OS cold boot: native CPU/BIOS/ROM/KNGMD/raw partition/SPI; VRAM matches C\n')
record={'passed':True,'disks':{i['name']:desc['payloads'][i['name']] for i in desc['regions'] if i['kind']=='disk'},
 'simulation_sha256':desc['payloads']['simulation-sd.img'],
 'native_log_sha256':hashlib.sha256(log.read_bytes()).hexdigest()}
(root/'offline-validation.json').write_text(json.dumps(record))
spec=importlib.util.spec_from_file_location('writer',evidence/'tools/write_sd.py')
writer=importlib.util.module_from_spec(spec);spec.loader.exec_module(writer)
(root/'mounts').write_text('')
virtual='/dev/disk/by-id/usb-Generic_MassStorageClass_000000001210-0:2'
real_open,real_stat,real_resolve=Path.open,Path.stat,Path.resolve
Path.open=lambda self,*a,**k:real_open(fixture,*a,**k) if str(self)==virtual else real_open(root/'mounts',*a,**k) if str(self)=='/proc/mounts' else real_open(self,*a,**k)
Path.stat=lambda self,*a,**k:real_stat(fixture,*a,**k) if str(self)==virtual else real_stat(self,*a,**k)
Path.resolve=lambda self,*a,**k:real_resolve(fixture,*a,**k) if str(self)==virtual else real_resolve(self,*a,**k)
stat.S_ISBLK=lambda mode: True
real_ioctl=fcntl.ioctl
fcntl.ioctl=lambda fd,request,arg:struct.pack('Q',size) if request==0x80081272 else real_ioctl(fd,request,arg)
sys.argv=['write_sd.py','--device',virtual,'--directory',str(root),'--prepared-directory',str(prepared),
 '--validation',str(log),'--validation-record',str(root/'offline-validation.json'),'--overwrite-unallocated']
writer.main()
Path.open,Path.stat,Path.resolve=real_open,real_stat,real_resolve
with fixture.open('rb') as f: after=f.read(len(expected))
assert after==expected,'Writer changed bytes outside prepared extents or did not finish'
assert fixture.stat().st_size==size
report={'passed':True,'physical_sd_opened':False,'fixture_sparse_bytes':size,'checked_bytes':len(after),
 'writer_sha256':hashlib.sha256((evidence/'tools/write_sd.py').read_bytes()).hexdigest(),
 'preserved':'All bytes outside prepared MBR/FAT16/disk/EBR regions in first 24 MiB',
 'after_sha256':hashlib.sha256(after).hexdigest()}
(evidence/'writer-offline-test.json').write_text(json.dumps(report,indent=2)+'\n')
fixture.unlink()
print('PASS offline SD writer: exact prepared bytes; all other first-24-MiB bytes unchanged; physical SD never opened')
