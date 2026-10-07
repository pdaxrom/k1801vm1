import sys
import io
import unittest
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from rtd266x_capture import SCL, SDA
from rtd266x_flash_read import BufferedI2c, FlashReader, crc8


class Wire:
    """Simulate I2C ACK/data; inspect GPIO directions on every command."""
    def __init__(self, data=b'\xa5\x5a', nack=False, stretch=False, write_length=0):
        self.data = data
        self.nack, self.stretch = nack, stretch
        self.mask = 0
        self.sample = 0
        self.released = False
        self.write_length = write_length
        self.samples = []

    def exchange(self, commands, length):
        assert len(commands) <= 710  # A bounded input/output batch.
        result, position = bytearray(), 0
        while position < len(commands):
            command = commands[position]
            position += 1
            if command == 0x80:
                value, self.mask = commands[position:position+2]
                position += 2
                assert value == 0 and self.mask & ~0x30 == 0
            elif command == 0x13:
                assert commands[position:position+2] == b'\x00\x00'
                position += 2
            elif command == 0x86:
                assert commands[position:position+2] == b'\x3b\x00'
                position += 2
            elif command == 0x81:
                scl, sda = not self.mask & SCL, not self.mask & SDA
                # Read packet: START, address+register (2x9), repeated
                # START, read address (9), then 9 samples per data byte.
                index = self.sample
                if self.write_length:
                    if 10 <= index < 2+9*(2+self.write_length) and (index-10) % 9 == 0:
                        sda = self.nack
                elif index in (10, 19, 30):
                    sda = self.nack
                elif 31 <= index < 31+len(self.data)*9:
                    byte, bit = divmod(index-31, 9)
                    if bit < 8:
                        sda = bool(self.data[byte] & (1 << (7-bit)))
                if self.stretch and index == 31:
                    scl = False
                result.append((SCL if scl else 0) | (SDA if sda else 0))
                self.samples.append(sda)
                self.sample += 1
            else:
                assert command == 0x87
        assert len(result) == length
        return bytes(result)

    def pins(self, scl, sda):
        if scl and sda:
            self.released = True
        return (SCL if scl else 0) | (SDA if sda else 0)


class Registers:
    def __init__(self):
        self.values = {0x6f: 0, 0xf3: 0x20, 0xf4: 0x74,
                       0x60: 0x46, 0x61: 0x9f, 0x64: 1, 0x65: 2, 0x66: 3, 0x6a: 0x0b,
                       0x72: 4, 0x73: 5, 0x74: 6, 0x75: 0xf4,
                       0x67: 0xef, 0x68: 0x30, 0x69: 0x13}
        self.original = self.values.copy()
        self.page = 2
        self.fail = False
        self.writes, self.blocks = [], []

    def read(self, register):
        if register == 0xf5:
            assert self.values[0xf4] == 0x9f
            return self.page
        return self.values.get(register, 0)

    def write(self, register, value):
        assert register in (0x6f, 0xf3, 0xf4, 0xf5, *FlashReader.SAVED)
        if register == 0x6f:
            assert value in (0, 0x80, 0x84)  # ISP/CRC, no program operation.
        self.writes.append((register, value))
        if register == 0xf5:
            assert self.values[0xf4] == 0x9f
            self.page = value
        else:
            if register == 0x60:
                value &= ~1
            elif register == 0x6f and value == 0x84:
                value = 0x82  # CRC completed.
            self.values[register] = value

    def read_block(self, register, length):
        assert register == 0x70 and 1 <= length <= 32
        if self.fail:
            raise OSError('Injected data failure')
        self.blocks.append(length)
        return b'\xaa'*length


class FlashReadTests(unittest.TestCase):
    def test_read_retry_restarts_address_and_never_appends_partial_data(self):
        reader = FlashReader(SimpleNamespace(io=Wire()))
        calls = []
        def chunk(address, length):
            calls.append(address)
            if len(calls) == 1:
                raise OSError('NACK after a partially consumed block')
            return bytes((address//1024,))*length
        reader.read_chunk = chunk
        output = io.BytesIO()
        output.name = 'memory'
        reader.backup(output, 2048)
        self.assertEqual(calls, [0, 0, 1024])
        self.assertEqual(output.getvalue(), b'\x00'*1024+b'\x01'*1024)

    def test_standard_crc_vector(self):
        self.assertEqual(crc8(b'123456789'), 0xf4)

    def test_controller_crc_does_not_enable_programming(self):
        bus = Registers()
        with FlashReader(bus).session():
            self.assertEqual(FlashReader(bus).checksum(512*1024), 0xf4)
            self.assertEqual([bus.values[r] for r in (0x72, 0x73, 0x74)], [7, 255, 255])
        self.assertEqual(bus.values, bus.original)

    def test_buffered_read_preserves_all_bits_and_never_drives_high(self):
        self.assertEqual(BufferedI2c(Wire()).read_block(0x70, 2), b'\xa5\x5a')

    def test_maximum_block_interleaves_output_with_fifo_reads(self):
        data = bytes(range(32))
        self.assertEqual(BufferedI2c(Wire(data)).read_block(0x70, 32), data)

    def test_sram_block_write_preserves_every_payload_bit(self):
        data = bytes(range(16))
        wire = Wire(b'', write_length=len(data))
        BufferedI2c(wire).write_block(0x70, data)
        observed = bytearray()
        for n in range(2+len(data)):
            byte = 0
            for bit in wire.samples[2+9*n:10+9*n]:
                byte = (byte << 1) | bit
            observed.append(byte)
        self.assertEqual(observed, b'\x94\x70'+data)

    def test_nack_rejected_and_bus_released(self):
        wire = Wire(nack=True)
        with self.assertRaisesRegex(OSError, 'NACK'):
            BufferedI2c(wire).read_block(0x70, 2)
        self.assertTrue(wire.released)

    def test_stretch_rejected_and_bus_released(self):
        wire = Wire(stretch=True)
        with self.assertRaisesRegex(OSError, 'clock stretch'):
            BufferedI2c(wire).read_block(0x70, 2)
        self.assertTrue(wire.released)

    def test_read_commands_and_all_selectors_restored(self):
        bus = Registers()
        reader = FlashReader(bus)
        with reader.session():
            self.assertEqual(reader.command(0x9f, 3), bytes.fromhex('ef3013'))
            self.assertEqual(reader.read_chunk(0x123456, 65), b'\xaa'*65)
        self.assertEqual(bus.blocks, [32, 32, 1])
        self.assertEqual(bus.values, bus.original)
        self.assertEqual(bus.page, 2)

    def test_failure_restores_flash_and_isp_selectors(self):
        bus = Registers()
        bus.fail = True
        with self.assertRaisesRegex(OSError, 'Injected'):
            with FlashReader(bus).session():
                FlashReader(bus).read_chunk(0, 32)
        self.assertEqual(bus.values, bus.original)
        self.assertEqual(bus.page, 2)
        self.assertEqual(bus.writes[-1], (0x6f, 0))

    def test_flash_mutation_commands_rejected_before_io(self):
        bus = Registers()
        for opcode in (0x06, 0x02, 0xc7, 0x20, 0x01):
            with self.assertRaises(ValueError):
                FlashReader(bus).command(opcode, 1)
        self.assertEqual(bus.writes, [])

    def test_active_isp_is_left_untouched(self):
        bus = Registers()
        bus.values[0x6f] = 0x80
        with self.assertRaisesRegex(RuntimeError, 'already'):
            with FlashReader(bus).session():
                pass
        self.assertEqual(bus.writes, [])


if __name__ == '__main__':
    unittest.main()
