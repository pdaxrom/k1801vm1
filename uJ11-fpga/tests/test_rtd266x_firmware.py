import hashlib
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
import rtd266x_firmware as firmware
from rtd266x_flash_read import crc8


class FirmwareTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        data = bytearray(b'\xff'*firmware.SIZE)
        data[firmware.TABLE_OFFSET:firmware.TABLE_OFFSET+20] = firmware.FACTORY_TABLE
        cls.image = bytes(data)
        cls.digest = hashlib.sha256(data).hexdigest()
        cls.verification = {'passed': True, 'sha256': cls.digest, 'hardware_crc8': crc8(data)}

    def test_only_two_bytes_change_and_sector_contains_the_patch(self):
        with patch.object(firmware, 'FACTORY_SHA256', self.digest):
            image, old, new, report = firmware.prepare(self.image, self.image, self.verification)
        self.assertEqual([i for i in range(len(image)) if image[i] != self.image[i]], [0x3fd0, 0x3fd2])
        self.assertEqual(image[0x3fd0], 18)
        self.assertEqual(image[0x3fd2], 240)
        self.assertEqual(old, self.image[0x3000:0x4000])
        self.assertEqual(new, image[0x3000:0x4000])
        self.assertFalse(report['flash_programmed'])

    def test_differing_backups_rejected(self):
        with self.assertRaisesRegex(ValueError, 'identical'):
            firmware.prepare(self.image, self.image[:-1]+b'\x00', self.verification)

    def test_unsupported_firmware_rejected(self):
        with self.assertRaisesRegex(ValueError, 'fingerprint'):
            firmware.prepare(self.image, self.image, self.verification)

    def test_bad_controller_crc_rejected(self):
        verification = dict(self.verification, hardware_crc8=self.verification['hardware_crc8'] ^ 1)
        with patch.object(firmware, 'FACTORY_SHA256', self.digest):
            with self.assertRaisesRegex(ValueError, 'CRC'):
                firmware.prepare(self.image, self.image, verification)


if __name__ == '__main__':
    unittest.main()
