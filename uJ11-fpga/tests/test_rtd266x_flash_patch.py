import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from rtd266x_flash_patch import SectorEditor
from rtd266x_flash_read import crc8
from rtd266x_firmware import SIZE, SECTOR_OFFSET, SECTOR_SIZE


class VirtualFlash:
    def __init__(self):
        self.flash = bytearray(b'\xff'*SIZE)
        self.flash[SECTOR_OFFSET:SECTOR_OFFSET+SECTOR_SIZE] = bytes(range(256))*16
        self.original = bytes(self.flash)
        self.regs = {r: 0 for r in (*SectorEditor.SAVED, 0xf3, 0xf4, 0x6f)}
        self.regs.update({0xf3: 0x20, 0xf4: 0x74, 0x6a: 0x0b, 0x62: 6, 0x6d: 2, 0x6e: 5})
        self.saved = self.regs.copy()
        self.page, self.status, self.pointer = 2, 0x1c, 0
        self.buffer = bytearray()
        self.page_count = 0
        self.fail_page = 0
        self.fail_always = False

    def address(self, first=0x64):
        return sum(self.regs[first+n] << (16-8*n) for n in range(3))

    def read(self, register):
        if register == 0xf5:
            assert self.regs[0xf4] == 0x9f
            return self.page
        return self.regs.get(register, 0)

    def write(self, register, value):
        assert register in (*self.regs, 0xf5)
        if register == 0xf5:
            assert self.regs[0xf4] == 0x9f
            self.page = value
            return
        self.regs[register] = value
        if register == 0x71:
            self.buffer.clear()
        elif register == 0x60 and value & 1:
            self.regs[0x60] &= ~1
            opcode = self.regs[0x61]
            if opcode == 0x9f:
                self.regs.update({0x67: 0xef, 0x68: 0x30, 0x69: 0x13})
            elif opcode == 0x05:
                self.regs[0x67] = self.status
            elif opcode == 0x03:
                self.pointer = self.address()
            elif opcode == 0x01:
                assert value == 0x69 and self.regs[0x62] == 6
                self.status = self.regs[0x64] & 0xfc
            elif opcode == 0x20:
                assert value == 0x79 and self.regs[0x62] == 6
                assert self.address() == SECTOR_OFFSET and self.status & 0x1c == 0
                self.flash[SECTOR_OFFSET:SECTOR_OFFSET+SECTOR_SIZE] = b'\xff'*SECTOR_SIZE
            else:
                raise AssertionError(f'Unexpected SPI opcode {opcode:02x}')
        elif register == 0x6f:
            assert value in (0, 0x80, 0x84, 0xa0)
            if value == 0x84:
                self.regs[0x75] = crc8(self.flash[self.address():self.address(0x72)+1])
                self.regs[0x6f] = 0x82
            elif value == 0xa0:
                self.page_count += 1
                self.regs[0x6f] = 0x80
                if self.fail_always or self.page_count == self.fail_page:
                    raise OSError('Injected page failure')
                address = self.address()
                assert SECTOR_OFFSET <= address <= SECTOR_OFFSET+SECTOR_SIZE-256
                assert address % 256 == 0 and self.regs[0x71] == 255
                assert self.regs[0x6d] == 2 and len(self.buffer) == 256
                assert self.status & 0x1c == 0
                for index, byte in enumerate(self.buffer):
                    self.flash[address+index] &= byte

    def read_block(self, register, length):
        assert register == 0x70 and 1 <= length <= 32
        result = bytes(self.flash[self.pointer:self.pointer+length])
        self.pointer += length
        return result

    def write_block(self, register, data):
        assert register == 0x70 and len(data) == 16
        self.buffer.extend(data)


class SectorPatchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original = VirtualFlash().original
        desired = bytearray(cls.original)
        desired[0x3fd0] ^= 8
        desired[0x3fd2] ^= 24
        cls.desired = bytes(desired)
        cls.before_crc, cls.after_crc = crc8(cls.original), crc8(cls.desired)

    def transaction(self, bus):
        editor, record = SectorEditor(bus), {}
        with editor.session():
            editor.apply(self.original[0x3000:0x4000], self.desired[0x3000:0x4000],
                         self.before_crc, self.after_crc, record)
        return editor, record

    def test_success_changes_only_expected_sector_and_restores_protection(self):
        bus = VirtualFlash()
        editor, record = self.transaction(bus)
        self.assertEqual(bytes(bus.flash), self.desired)
        self.assertEqual(bus.status, 0x1c)
        self.assertEqual({r: bus.regs[r] for r in bus.saved}, bus.saved)
        self.assertEqual(bus.page, 2)
        self.assertTrue(record['sector_verified'])
        self.assertTrue(record['protection_restored'])
        self.assertFalse(editor.leave_mcu_paused)

    def test_page_failure_rolls_back_all_partial_writes(self):
        bus = VirtualFlash()
        bus.fail_page = 3
        with self.assertRaisesRegex(OSError, 'Injected'):
            self.transaction(bus)
        self.assertEqual(bytes(bus.flash), self.original)
        self.assertEqual(bus.status, 0x1c)
        self.assertEqual(bus.regs[0x6f], 0)

    def test_unverified_rollback_keeps_mcu_in_isp(self):
        bus = VirtualFlash()
        bus.fail_always = True
        editor, record = SectorEditor(bus), {}
        with self.assertRaisesRegex(OSError, 'Injected'):
            with editor.session():
                editor.apply(self.original[0x3000:0x4000], self.desired[0x3000:0x4000],
                             self.before_crc, self.after_crc, record)
        self.assertTrue(editor.leave_mcu_paused)
        self.assertTrue(record['mcu_left_paused'])
        self.assertEqual(bus.regs[0x6f], 0x80)
        self.assertEqual(bus.status, 0x1c)

    def test_source_mismatch_does_not_unlock_or_erase(self):
        bus = VirtualFlash()
        bus.flash[0x3000] ^= 1
        before = bytes(bus.flash)
        with self.assertRaisesRegex(RuntimeError, 'does not match'):
            self.transaction(bus)
        self.assertEqual(bytes(bus.flash), before)
        self.assertEqual(bus.page_count, 0)
        self.assertEqual(bus.status, 0x1c)

    def test_program_outside_selected_sector_rejected_before_io(self):
        editor = SectorEditor(VirtualFlash())
        for address in (0x2000, 0x4000, 0x3001):
            with self.assertRaisesRegex(ValueError, 'limited'):
                editor.program_page(address, b'\x00'*256)


if __name__ == '__main__':
    unittest.main()
