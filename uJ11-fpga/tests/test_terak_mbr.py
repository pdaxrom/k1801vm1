import json
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from terak_mbr import create, fat16, raw_size, raw_from_imd, partitions, PROFILES, ALIGN, FAT_BLOCKS
from terak_image import card
from test_terak_image import fixture, double_fixture


class TerakMbrTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temp.name)
        cls.exe = cls.root/'media'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                        str(ROOT/'tests/terak/test_media.c'), str(ROOT/'firmware/terak/media.c'),
                        str(ROOT/'firmware/terak/mbr.c'), str(ROOT/'firmware/storage/label.c'),
                        '-o', str(cls.exe)], check=True)
        cls.single = cls.root/'single.IMD'
        cls.single.write_bytes(fixture())
        cls.double = cls.root/'double.IMD'
        cls.double.write_bytes(double_fixture())

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def check(self, data):
        image = self.root/'check.img'
        image.write_bytes(data)
        run = subprocess.run([str(self.exe), str(image)], text=True, capture_output=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        result = json.loads(run.stdout)
        self.assertEqual(image.read_bytes(), data)
        return result

    def build(self, disks, **kwargs):
        image = self.root/'new.img'
        image.unlink(missing_ok=True)
        info = create(disks, image, **kwargs)
        return bytearray(image.read_bytes()), info

    def cfg(self, data, text):
        data[ALIGN*512:(ALIGN+FAT_BLOCKS)*512] = fat16(text)
        return data

    def test_profiles_exact_payload_and_roundtrip(self):
        expected = (256256, 512512, 591360, 1182720, 587008, 1177344)
        for profile, length in zip(PROFILES, expected):
            with self.subTest(profile=profile):
                self.assertEqual(raw_size(profile), length)
                disk = self.root/(profile+'.raw')
                disk.write_bytes(bytes(range(256))*(length//256))
                data, info = self.build([(profile, disk)])
                r = self.check(data)
                self.assertEqual(r['mounted'], 1)
                self.assertEqual(r['drives'][0]['format'], PROFILES.index(profile)+1)
                p = info['disks'][0]
                self.assertEqual(data[p['start']*512:p['start']*512+length], disk.read_bytes())
                with (self.root/'check.img').open('rb') as f:
                    self.assertEqual(partitions(f)[2]['start'], p['start'])
                out = self.root/'export.raw'
                out.unlink(missing_ok=True)
                subprocess.run([sys.executable, str(ROOT/'tools/terak_mbr.py'), 'export', str(self.root/'check.img'),
                                '--partition', '2', '--profile', profile, '--file', str(out)], check=True)
                self.assertEqual(out.read_bytes(), disk.read_bytes())
                replacement = self.root/'replacement.raw'
                replacement.write_bytes(b'\xa5'*length)
                subprocess.run([sys.executable, str(ROOT/'tools/terak_mbr.py'), 'import', str(self.root/'check.img'),
                                '--partition', '2', '--profile', profile, '--file', str(replacement)], check=True)
                after = (self.root/'check.img').read_bytes()
                start = p['start']*512
                self.assertEqual(after[:start], data[:start])
                self.assertEqual(after[start+length:], data[start+length:])
                self.assertEqual(after[start:start+length], replacement.read_bytes())

    def test_four_drives_logical_and_explicit_mapping(self):
        for logical in (False, True):
            data, info = self.build([('fd', self.single)]*4, logical=logical)
            nums = [r['partition'] for r in info['disks']]
            config = f' VERSION = 1\r\nDrive0 = {nums[3]}, FD, RO # system\r\ndrive1=off\n' \
                     f'drive2={nums[0]},fd,rw\ndrive3={nums[2]},fd,ro\n'
            r = self.check(self.cfg(data, config))
            self.assertEqual(r['mounted'], 1)
            self.assertEqual([d['start'] for d in r['drives']],
                             [info['disks'][3]['start'], 0, info['disks'][0]['start'], info['disks'][2]['start']])
            self.assertEqual([d['readonly'] for d in r['drives'] if d['start']], [1, 0, 1])

    def test_legacy_and_no_config_container_auto_mount(self):
        data, _ = self.build([('fd', self.single), ('fd', self.double)], config=False)
        r = self.check(data)
        self.assertEqual([(d['heads'], d['readonly']) for d in r['drives'] if d['start']], [(1, 1), (2, 1)])
        legacy = self.root/'legacy.img'
        card([self.single, self.double], legacy, [1])
        r = self.check(legacy.read_bytes())
        self.assertEqual([(d['heads'], d['readonly']) for d in r['drives'] if d['start']], [(1, 0), (2, 1)])

    def test_imd_conversion_requires_known_geometry_and_status_choice(self):
        with self.assertRaisesRegex(ValueError, 'status'):
            raw_from_imd(fixture(), 'sssd')
        self.assertEqual(len(raw_from_imd(fixture(), 'sssd', True)), 256256)
        self.assertEqual(len(raw_from_imd(double_fixture(), 'terak-dsdd', True)), 1177344)
        with self.assertRaises(ValueError):
            raw_from_imd(double_fixture(), 'dsdd', True)

    def test_config_rejects_duplicates_unknown_profiles_and_protected_partitions(self):
        data, _ = self.build([('fd', self.single)]*4)
        configs = ('', 'version=2\ndrive0=2,fd,rw', 'version=1\nversion=1\ndrive0=2,fd,rw',
                   'version=1\ndrive0=2,fd,rw\ndrive0=3,fd,rw', 'version=1\ndrive0=2,fd,rw\ndrive1=2,fd,ro',
                   'version=1\ndrive0=1,fd,rw', 'version=1\ndrive0=4,fd,rw', 'version=1\ndrive0=13,fd,rw',
                   'version=1\ndrive0=2,raw,bogus,rw', 'version=1\ndrive0=2,fd,rw junk',
                   'version=1\ndrive4=2,fd,rw', 'version=1\nunknown=2', 'drive0=2,fd,rw')
        for text in configs:
            with self.subTest(text=text):
                # Empty file is malformed rather than absent.
                changed = self.cfg(bytearray(data), text or b'\x00')
                self.assertEqual(self.check(changed)['mounted'], 0)

    def test_mbr_and_extended_damage_never_mounts(self):
        data, info = self.build([('fd', self.single)]*4)
        mutations = [(510, b'\x00'), (462+8, struct.pack('<I', 2048)),
                     (462+12, struct.pack('<I', 0xffffffff)), (462+12, struct.pack('<I', 1)),
                     (462+8, struct.pack('<I', 0xfffffff0)),
                     (info['disks'][2]['ebr']*512+510, b'\x00'),
                     (info['disks'][2]['ebr']*512+470, struct.pack('<I', 1)),
                     (info['disks'][2]['ebr']*512+478, b'\x01')]
        for offset, value in mutations:
            with self.subTest(offset=offset, value=value):
                changed = bytearray(data)
                changed[offset:offset+len(value)] = value
                self.assertEqual(self.check(changed)['mounted'], 0)

    def test_fat16_damage_never_mounts(self):
        data, _ = self.build([('fd', self.single)])
        boot, root = ALIGN*512, (ALIGN+255)*512
        mutations = [(boot+13, b'\x03'), (boot+11, struct.pack('<H', 1024)),
                     (boot+22, struct.pack('<H', 1)), (boot+19, struct.pack('<H', 0)),
                     (root+26, struct.pack('<H', 0)), (root+28, struct.pack('<I', 513)),
                     (root+11, b'\x10'), ((ALIGN+1)*512+4, struct.pack('<H', 2)),
                     ((ALIGN+128)*512+4, struct.pack('<H', 0)),
                     (root+32, bytes(data[root:root+32]))]
        for offset, value in mutations:
            with self.subTest(offset=offset, value=value):
                changed = bytearray(data)
                changed[offset:offset+len(value)] = value
                self.assertEqual(self.check(changed)['mounted'], 0)

    def test_raw_needs_config_and_whole_geometry(self):
        disk = self.root/'sssd.raw'
        disk.write_bytes(bytes(256256))
        data, _ = self.build([('sssd', disk)])
        changed = self.cfg(bytearray(data), None)
        self.assertEqual(self.check(changed)['mounted'], 0)
        struct.pack_into('<I', data, 462+12, 500)
        self.assertEqual(self.check(data)['mounted'], 0)
        disk.write_bytes(bytes(256255))
        with self.assertRaises(ValueError):
            self.build([('sssd', disk)])

    def test_mbr_takes_precedence_over_stale_uj11sd_backup(self):
        legacy = self.root/'old.img'
        card([self.single], legacy)
        disk = self.root/'direct.raw'
        disk.write_bytes(bytes(256256))
        data, info = self.build([('sssd', disk)])
        data[512:1024] = legacy.read_bytes()[512:1024]
        r = self.check(data)
        self.assertEqual(r['drives'][0]['start'], info['disks'][0]['start'])
        self.assertEqual(r['drives'][0]['format'], 1)


if __name__ == '__main__':
    unittest.main()
