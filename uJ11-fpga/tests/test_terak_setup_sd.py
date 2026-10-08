"""Setup upgrade preserves shared-card disks, settings and unrelated FAT files."""
import io
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from prepare_terak_setup_sd import prepare, Fat16
from terak_mbr import create, partitions, raw_size, ALIGN, FAT_BLOCKS


def bios(text):
    data = text.encode().ljust((len(text)+1)//2*2, b'\0')
    return struct.pack('<4sIII', b'UJTB', len(data), sum(struct.unpack('<'+'H'*(len(data)//2), data)) & 65535, 1)+data


class TerakSetupSdTests(unittest.TestCase):
    def test_ramdiag_replaces_only_selected_disk(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            other, old_test, new_test = [root/n for n in ('other.raw', 'old.raw', 'new.raw')]
            other.write_bytes(bytes(range(256))*1001)
            old_test.write_bytes(bytes(raw_size('terak-dsdd')))
            new_test.write_bytes(b'\xa5'*raw_size('terak-dsdd'))
            source, boot = root/'card.img', root/'TBIOS.BIN'
            create([('sssd', other), ('terak-dsdd', old_test)], source)
            before = bytearray(source.read_bytes())
            fat = Fat16(before[ALIGN*512:(ALIGN+FAT_BLOCKS)*512])
            fat.put('TBIOS.BIN', bios('Old BIOS'))
            fat.put('TERAK4.CFG', b'version=1\ndrive0=3,raw,terak-dsdd,ro\n')
            fat.put('TERAK.SET', bytes(range(256))*4)
            original = fat.files()
            before[ALIGN*512:(ALIGN+FAT_BLOCKS)*512] = fat.data
            source.write_bytes(before)
            boot.write_bytes(bios('DEL: Enter BIOS Setup - 2048 KiB'))
            plan = prepare(source, boot, root/'plan', len(before)//512, new_test)
            after = (root/'plan/card-after.img').read_bytes()
            table = partitions(io.BytesIO(before))
            self.assertEqual(partitions(io.BytesIO(after)), table)
            disk = table[2]
            start, end = disk['start']*512, (disk['start']+disk['blocks'])*512
            self.assertEqual(after[start:end], before[start:end])
            disk = table[3]
            start, end = disk['start']*512, (disk['start']+disk['blocks'])*512
            self.assertEqual(after[start:end], new_test.read_bytes().ljust(end-start, b'\0'))
            current = Fat16(after[ALIGN*512:(ALIGN+FAT_BLOCKS)*512]).files()
            for name, value in original.items():
                if name != 'TBIOS.BIN':
                    self.assertEqual(current[name], value)
            self.assertEqual(set(plan['replaced_disk_partitions']), {3})
            self.assertEqual(source.read_bytes(), before)
            for value in (b'drive0=1,raw,terak-dsdd,ro\n', b'drive0=3,fd,ro\n'):
                fat.put('TERAK4.CFG', value)
                before[ALIGN*512:(ALIGN+FAT_BLOCKS)*512] = fat.data
                source.write_bytes(before)
                with self.assertRaises(ValueError):
                    prepare(source, boot, root/'bad', len(before)//512, new_test)
                self.assertFalse((root/'bad').exists())

    def test_upgrade_and_repeat_preserve_settings_and_disks(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            floppy = root/'floppy.raw'
            floppy.write_bytes(bytes(256256))
            source, boot = root/'card.img', root/'TBIOS.BIN'
            create([('sssd', floppy)]*4, source)
            before = bytearray(source.read_bytes())
            fat = Fat16(before[ALIGN*512:(ALIGN+FAT_BLOCKS)*512])
            old_bios = bios('Old BIOS')
            fat.put('TBIOS.BIN', old_bios)
            fat.put('BK0010.CFG', b'Original BK configuration')
            fat.put('PULDIN.BIN', bytes(range(256))*8)
            old_files = fat.files()
            before[ALIGN*512:(ALIGN+FAT_BLOCKS)*512] = fat.data
            source.write_bytes(before)
            boot.write_bytes(bios('DEL: Enter BIOS Setup'))
            result = prepare(source, boot, root/'plan', len(before)//512)
            after = (root/'plan/card-after.img').read_bytes()
            self.assertEqual(partitions(io.BytesIO(after)), partitions(io.BytesIO(before)))
            self.assertEqual(after[:ALIGN*512], before[:ALIGN*512])
            self.assertEqual(after[(ALIGN+FAT_BLOCKS)*512:], before[(ALIGN+FAT_BLOCKS)*512:])
            fat = Fat16(after[ALIGN*512:(ALIGN+FAT_BLOCKS)*512])
            for name, value in old_files.items():
                if name != 'TBIOS.BIN':
                    self.assertEqual(fat.files()[name], value)
            self.assertEqual(fat.files()['TERAK.SET'][2], bytes(1024))
            self.assertEqual(fat.files()['TBIOS.BAK'][2], old_bios)
            self.assertEqual(len(set(result['settings_lbas'])), 2)
            fat.put('TERAK.SET', bytes(range(256))*4)
            saved = fat.files()['TERAK.SET']
            second = bytearray(after)
            second[ALIGN*512:(ALIGN+FAT_BLOCKS)*512] = fat.data
            source.write_bytes(second)
            prepare(source, boot, root/'repeat', len(before)//512)
            final = (root/'repeat/card-after.img').read_bytes()
            self.assertEqual(Fat16(final[ALIGN*512:(ALIGN+FAT_BLOCKS)*512]).files()['TERAK.SET'], saved)
            self.assertEqual(source.read_bytes(), second)
            for bad in (bytes(512), bytes(1536)):
                fat.put('TERAK.SET', bad)
                second[ALIGN*512:(ALIGN+FAT_BLOCKS)*512] = fat.data
                source.write_bytes(second)
                with self.assertRaisesRegex(ValueError, 'exactly 1024'):
                    prepare(source, boot, root/'bad', len(before)//512)
                self.assertFalse((root/'bad').exists())


if __name__ == '__main__':
    unittest.main()
