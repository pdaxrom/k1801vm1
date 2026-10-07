"""Native BIOS save/create/error checks on shared FAT16 SD simulation media."""
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from make_sd import fat16
from models import settings, parse_settings

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'


def layout(image):
    base = struct.unpack_from('<I', image, 454)[0] * 512
    spc = image[base+13]
    reserved = struct.unpack_from('<H', image, base+14)[0]
    copies = image[base+16]
    entries = struct.unpack_from('<H', image, base+17)[0]
    spf = struct.unpack_from('<H', image, base+22)[0]
    fat = base+reserved*512
    root = fat+copies*spf*512
    data = root+entries*32
    return fat, root, data, spf, spc, entries


def directory(image):
    fat, root, data, _, spc, entries = layout(image)
    files = {}
    for index in range(entries):
        entry = root+index*32
        if image[entry] == 0:
            break
        if image[entry] == 0xe5 or image[entry+11] & 0x18:
            continue
        name = image[entry:entry+8].decode().rstrip()+'.'+image[entry+8:entry+11].decode().rstrip()
        first = struct.unpack_from('<H', image, entry+26)[0]
        size = struct.unpack_from('<I', image, entry+28)[0]
        cluster, seen, content = first, set(), bytearray()
        while cluster < 0xfff8:
            assert 2 <= cluster < 0xfff0 and cluster not in seen, 'Invalid FAT chain'
            seen.add(cluster)
            offset = data+(cluster-2)*spc*512
            content.extend(image[offset:offset+spc*512])
            cluster = struct.unpack_from('<H', image, fat+cluster*2)[0]
        assert len(content) >= size
        files[name] = (entry, first, bytes(content[:size]))
    return files


def run_checks(executable, run, only=None):
    original = (BUILD/'bk0010-sd.img').read_bytes()
    files = {'LOADER.BIN': bytes((index*17+3)&255 for index in range(481)),
             'P601.SET': b'P601 test sentinel', 'LOADERBK.BIN': (BUILD/'LOADERBK.BIN').read_bytes(),
             'BK02.ROM': (BUILD/'BK02.ROM').read_bytes(), 'BK.SET': settings(drives=(0,0,0,0))}
    for name, action, error, flags in [
        ('model-drive',3,0,['+expected-model=3','+screen=build/model-3-screen.hex','+native-runtime']),
        ('logical-default',3,0,['+expected-model=3','+screen=build/model-3-screen.hex','+native-runtime',
                              '+logical-bios','+disk-base=47104','+second-base=43008']),
        ('logical-shared',3,0,['+expected-model=3','+screen=build/model-3-screen.hex','+native-runtime',
                             '+logical-bios','+disk-base=47104','+second-base=43008']),
        ('logical-edit',5,0,['+expected-model=3','+screen=build/model-3-screen.hex','+native-runtime',
                           '+logical-bios','+disk-base=36864','+second-base=43008']),
        ('existing',1,0,[]), ('create',1,0,[]), ('temporary',2,0,[]),
        ('root-full',1,0x55,[]), ('readonly',1,0x53,[]), ('metadata',1,0x54,[]), ('full',1,0x55,[]),
        ('write-fail',1,0x52,['+write-fail-at=1']),
        ('readback-fail',1,0x56,['+corrupt-write-at=1'])]:
        if only is not None and name not in only:
            continue
        selected = dict(files)
        if name in ('create','full','root-full'):
            del selected['BK.SET']
        if name == 'model-drive' or name.startswith('logical-'):
            selected['BK03.ROM']=(BUILD/'BK03.ROM').read_bytes()
        before = bytearray(original)
        if name == 'model-drive' or name.startswith('logical-'):
            before.extend(b'\xa5'*(38912*512-len(before)))
            for index,start in enumerate((34816,36864),1):
                struct.pack_into('<B3sB3sII',before,446+index*16,0,b'\xfe\xff\xff',0xda,b'\xfe\xff\xff',start,1600)

        if name.startswith('logical-'):
            before.extend(bytes(49152*512-len(before)))
            struct.pack_into('<B3sB3sII',before,494,0,b'\xfe\xff\xff',0x0f,b'\xfe\xff\xff',40960,7744)
            for lba,link in ((40960,True),(45056,False)):
                offset=lba*512
                before[offset+510:offset+512]=b'\x55\xaa'
                struct.pack_into('<B3sB3sII',before,offset+446,0,b'\xfe\xff\xff',0xda,b'\xfe\xff\xff',2048,1600)
                if link:
                    struct.pack_into('<B3sB3sII',before,offset+462,0,b'\xfe\xff\xff',0x0f,b'\xfe\xff\xff',4096,3648)
            if name == 'logical-edit':
                selected['BK.SET']=settings(model=3,drives=(6,5,0,0),readonly=3)
            if name == 'logical-shared':
                before.extend(b'\xa5'*(57344*512-len(before)))
                struct.pack_into('<I',before,506,14837)
                for lba,next_lba,blocks in ((40960,45056,1600),(45056,49152,1600),
                                            (49152,53248,501),(53248,None,501)):
                    offset=lba*512
                    before[offset:offset+512]=bytes(512)
                    before[offset+510:offset+512]=b'\x55\xaa'
                    struct.pack_into('<B3sB3sII',before,offset+446,0,b'\xfe\xff\xff',0xda,
                                     b'\xfe\xff\xff',2048,blocks)
                    if next_lba is not None:
                        struct.pack_into('<B3sB3sII',before,offset+462,0,b'\xfe\xff\xff',0x0f,
                                         b'\xfe\xff\xff',next_lba-40960,55797-next_lba)
                selected['TERAK.CFG']=b'version=1\ndrive0=7,raw,sssd,rw\ndrive1=8,raw,sssd,rw\ndrive2=off\ndrive3=off\n'

        before[2048*512:(2048+32768)*512] = fat16(selected)
        fat, root, data, spf, _, entries = layout(before)
        entries_before = directory(before)
        if name == 'root-full':
            for index in range(len(selected),entries):
                offset=root+index*32
                before[offset:offset+11]=b'FULL    VOL'
                before[offset+11]=8
        elif name == 'readonly':
            before[entries_before['BK.SET'][0]+11] |= 1
        elif name == 'metadata':
            struct.pack_into('<I',before,entries_before['BK.SET'][0]+28,17)
        elif name == 'full':
            for copy in range(2):
                table = fat+copy*spf*512
                for cluster in range(2,(32768-289)+2):
                    if struct.unpack_from('<H',before,table+cluster*2)[0] == 0:
                        struct.pack_into('<H',before,table+cluster*2,0xffff)
        path = BUILD/f'bios-{name}.img'
        path.write_bytes(before)
        run(f'bios-{name}', [executable,'+no-dump',f'+image={path}',f'+bios-action={action}',
            *([f'+bios-error={error:02x}'] if error else []),*flags], timeout=1800)
        after = path.read_bytes()
        assert after[:2048*512] == before[:2048*512], 'BIOS modified MBR/pre-partition data'
        entries_after = directory(after)
        for other in selected:
            if other == 'BK.SET':
                continue
            assert entries_after[other] == entries_before[other], f'BIOS modified {other}'
        if name in ('temporary','readonly','metadata','full','root-full','write-fail'):
            assert after == before, f'{name} modified SD'
        elif not error:
            config = parse_settings(entries_after['BK.SET'][2])
            targets = {
                'model-drive':dict(model=3,frequency=4,color=1,drives=(2,3,0,0),readonly=0),
                'logical-default':dict(model=3,frequency=4,color=1,drives=(6,5,0,0),readonly=3),
                'logical-shared':dict(model=3,frequency=4,color=1,drives=(6,5,0,0),readonly=3),
                'logical-edit':dict(model=3,frequency=4,color=0,drives=(3,5,0,0),readonly=3)}
            target = targets.get(name,dict(model=2,frequency=16,color=0,drives=(0,0,0,0),readonly=0))
            assert config == target, config
            assert after[fat:fat+spf*512] == after[fat+spf*512:fat+2*spf*512], 'FAT copies disagree'
            if name in ('existing','model-drive') or name.startswith('logical-'):
                offset = data+(entries_before['BK.SET'][1]-2)*512
                expected = bytearray(before)
                expected[offset:offset+16] = settings(**target)
                assert after == expected, 'Existing BK.SET changed bytes outside its 16-byte record'
            else:
                # Creating BK.SET may change only its newly allocated data sector,
                # one FAT entry in each copy, and its own root directory entry.
                expected = bytearray(before)
                entry, cluster, _ = entries_after['BK.SET']
                offset = data+(cluster-2)*512
                expected[offset:offset+512] = after[offset:offset+512]
                expected[entry:entry+32] = after[entry:entry+32]
                for copy in range(2):
                    offset = fat+copy*spf*512+cluster*2
                    expected[offset:offset+2] = after[offset:offset+2]
                assert after == expected, 'Creating BK.SET modified unrelated SD bytes'
        print(f'bios-{name}: shared files and SD metadata verified',flush=True)
