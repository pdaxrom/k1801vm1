"""BIOS profiles and bounded FAT chains, using the actual SERV C reader."""
import json
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
sys.path.insert(0, str(ROOT.parent/'bk0010-01-fpga/tools'))
from prepare_disk_sd import Fat16
from terak_mbr import create, ALIGN, FAT_BLOCKS
from test_terak_image import fixture


class TerakBiosTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temp.name)
        cls.exe = cls.root/'bios-media'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-DUJ11_TERAK_BIOS',
                        '-fsanitize=address,undefined', str(ROOT/'tests/terak/test_bios_media.c'),
                        str(ROOT/'firmware/terak/media.c'), str(ROOT/'firmware/terak/mbr.c'),
                        str(ROOT/'firmware/terak/fat.c'), str(ROOT/'firmware/storage/label.c'),
                        '-o', str(cls.exe)], check=True)
        disk = cls.root/'fixture.IMD'
        disk.write_bytes(fixture())
        image = cls.root/'source.img'
        cls.info = create([('fd', disk)]*4, image, logical=True)
        cls.data = image.read_bytes()
        cls.payload = bytes(range(256))*7

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def volume(self):
        f = Fat16(self.data[ALIGN*512:(ALIGN+FAT_BLOCKS)*512])
        f.put('TBIOS.BIN', self.payload)
        nums = [d['partition'] for d in self.info['disks']]
        f.put('TERAK1.CFG', f'version=1\ndrive0={nums[3]},fd,ro\ndrive1={nums[0]},fd,rw\n'.encode())
        return f

    def check(self, volume, profile=1):
        data = bytearray(self.data)
        data[ALIGN*512:(ALIGN+FAT_BLOCKS)*512] = volume.data
        path = self.root/'check.img'
        path.write_bytes(data)
        result = subprocess.run([str(self.exe), str(path), str(profile)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(path.read_bytes(), data)
        return json.loads(result.stdout)

    def test_config_selection_and_multicluster_bios(self):
        r = self.check(self.volume())
        self.assertEqual((r['mounted'], r['selected'], r['valid']), (1, 1, 1))
        self.assertEqual(r['drives'], [self.info['disks'][3]['start'], self.info['disks'][0]['start'], 0, 0])
        self.assertEqual(bytes.fromhex(r['bios']), self.payload)

    def test_failed_selection_keeps_previous_mounts(self):
        for profile in (0, 2, 5):
            r = self.check(self.volume(), profile)
            self.assertEqual(r['selected'], 0)
            self.assertEqual(r['drives'], [d['start'] for d in self.info['disks']])
        f = self.volume()
        f.put('TERAK1.CFG', b'version=1\ndrive0=5,fd,ro\ndrive1=5,fd,rw\n')
        self.assertEqual(self.check(f)['selected'], 0)

    def test_fragmented_allocation(self):
        f = self.volume()
        entry, raw, _ = f.files()['TBIOS.BIN']
        old = f.chain(struct.unpack_from('<H', raw, 26)[0])
        new = [old[0], 40, 60, 80]
        for i, c in enumerate(new):
            pos = f.first+(c-2)*f.cluster_size
            f.data[pos:pos+f.cluster_size] = self.payload[i*512:(i+1)*512].ljust(512, b'\0')
            for copy in range(2):
                struct.pack_into('<H', f.data, f.fat+copy*f.fat_size+c*2, new[i+1] if i<3 else 0xffff)
        r = self.check(f)
        self.assertEqual(r['valid'], 1)
        self.assertEqual(bytes.fromhex(r['bios']), self.payload)

    def test_invalid_chains_and_fat_disagreement(self):
        for kind in ('short', 'long', 'cycle', 'bad', 'outside', 'copies'):
            with self.subTest(kind=kind):
                f = self.volume()
                _, entry, _ = f.files()['TBIOS.BIN']
                chain = f.chain(struct.unpack_from('<H', entry, 26)[0])
                at = chain[-1] if kind=='long' else chain[0]
                value = dict(short=0xffff, long=200, cycle=chain[0], bad=0xfff7,
                             outside=f.clusters+2, copies=0xffff)[kind]
                for copy in range(1 if kind=='copies' else 2):
                    struct.pack_into('<H', f.data, f.fat+copy*f.fat_size+at*2, value)
                self.assertEqual(self.check(f)['valid'], 0)


if __name__ == '__main__':
    unittest.main()
