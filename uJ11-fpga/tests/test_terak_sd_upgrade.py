"""Shared-card RL upgrade preserves old disks, configurations and FAT metadata."""
import hashlib
import io
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from prepare_terak_bios_sd import prepare, Fat16
from terak_mbr import create, partitions, put_entry, align, ALIGN, FAT_BLOCKS


def bios(text):
    data = text.encode().ljust((len(text)+1)//2*2, b'\0')
    return struct.pack('<4sIII', b'UJTB', len(data), sum(struct.unpack('<'+'H'*(len(data)//2), data))&65535, 1)+data


class TerakSdUpgradeTests(unittest.TestCase):
    def test_append_rl_and_upgrade_bios_preserves_shared_card(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            floppy = root/'floppy.raw'
            floppy.write_bytes(bytes(range(256))*(256256//256))
            source = root/'shared.img'
            info = create([('sssd', floppy)]*4, source)
            data = bytearray(source.read_bytes())
            base = info['disks'][2]['ebr']
            previous = info['disks'][3]['ebr']
            cursor = info['disks'][3]['start']+info['disks'][3]['blocks']
            for number in range(7, 14):
                at, start = align(cursor), align(cursor)+ALIGN
                cursor = start+501
                data.extend(bytes(max(0, align(cursor)*512-len(data))))
                block = bytearray(512)
                put_entry(block, 0, 218, start-at, 501)
                data[at*512:(at+1)*512] = block
                block = bytearray(data[previous*512:(previous+1)*512])
                put_entry(block, 1, 15, at-base, cursor-at)
                data[previous*512:(previous+1)*512] = block
                data[start*512:(start+501)*512] = bytes([number])*(501*512)
                previous = at
            struct.pack_into('<I', data, 506, cursor-base)
            volume = Fat16(data[ALIGN*512:(ALIGN+FAT_BLOCKS)*512])
            old_bios = bios('PRESS 1-4\0')
            volume.put('TBIOS.BIN', old_bios)
            for number in range(1, 5):
                volume.put(f'TERAK{number}.CFG', b'version=1\ndrive0=13,raw,sssd,ro\n')
            volume.put('BK0010.CFG', b'BK configuration\r\n')
            volume.put('USER.TXT', bytes(range(256))*5)
            old_files = volume.files()
            data[ALIGN*512:(ALIGN+FAT_BLOCKS)*512] = volume.data
            source.write_bytes(data)
            original = hashlib.sha256(data).hexdigest()
            rl01, rl02, new_bios = root/'system.rl01', root/'source.rl02', root/'TBIOS.BIN'
            rl01.write_bytes(b'\x5a'*5242880); rl02.write_bytes(b'\xa5'*10485760)
            new_bios.write_bytes(bios('5  RT-11\0PRESS 1-5\0'))
            result = prepare(source, new_bios, [rl01, rl02], root/'plan', 262144, 5)
            after = (root/'plan/card-after.img').read_bytes()
            table = partitions(io.BytesIO(after))
            old_table = partitions(io.BytesIO(data))
            self.assertEqual(table[14]['blocks'], 10240)
            self.assertEqual(table[15]['blocks'], 20480)
            for n, p in old_table.items():
                if not p or n == 4:
                    continue
                self.assertEqual(table[n], p)
                if n != 1:
                    lo, hi = p['start']*512, (p['start']+p['blocks'])*512
                    self.assertEqual(after[lo:hi], data[lo:hi])
            new_files = Fat16(after[ALIGN*512:(ALIGN+FAT_BLOCKS)*512]).files()
            for name, value in old_files.items():
                if name != 'TBIOS.BIN':
                    self.assertEqual(new_files[name], value)
            self.assertEqual(new_files['TBIOS.OLD'][2], old_bios)
            self.assertEqual(new_files['TBIOS.BIN'][2], new_bios.read_bytes())
            self.assertIn('rl0=14,raw,rl01,rw', new_files['TERAK5.CFG'][2].decode())
            self.assertIn('rl1=15,raw,rl02,ro', new_files['TERAK5.CFG'][2].decode())
            self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(), original)
            self.assertEqual(result['source_sha256'], original)
            with self.assertRaisesRegex(ValueError, 'do not fit'):
                prepare(source, new_bios, [rl01, rl02], root/'too-small', align(cursor), 5)
            with self.assertRaises(ValueError):
                prepare(root/'plan/card-after.img', new_bios, [rl01, rl02], root/'duplicate', 262144, 5)
            self.assertFalse((root/'too-small').exists())
            self.assertFalse((root/'duplicate').exists())


if __name__ == '__main__':
    unittest.main()
