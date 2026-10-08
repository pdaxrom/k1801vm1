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
from terak_mbr import create, partitions, ALIGN, FAT_BLOCKS


def bios(text):
    data = text.encode().ljust((len(text)+1)//2*2, b'\0')
    return struct.pack('<4sIII', b'UJTB', len(data), sum(struct.unpack('<'+'H'*(len(data)//2), data)) & 65535, 1)+data


class TerakSetupSdTests(unittest.TestCase):
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
