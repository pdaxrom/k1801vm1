import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from rtd266x_capture import Ftdi, I2c, Rtd, SCL, SDA


class TraceIo:
    def __init__(self, sampled=()):
        self.trace = []
        self.sampled = iter(sampled)

    def pins(self, scl, sda):
        self.trace.append((scl, sda))
        return (SCL if scl else 0) | (SDA if next(self.sampled, sda) else 0)


class RegisterBus:
    def __init__(self):
        self.regs = {0x6f: 0, 0xf3: 0x20, 0xf4: 0x74}
        self.scaler = {0x9f: 2, 0x18: 0x80, 0x19: 26, 0x1a: 0xa0, 0x1b: 232}
        self.writes = []
        self.fail_register = None

    def read(self, register):
        if register == 0xf5:
            if self.regs[0xf4] == self.fail_register:
                raise OSError('injected capture read failure')
            return self.scaler.get(self.regs[0xf4], 0)
        return self.regs.get(register, 0)

    def write(self, register, value):
        self.writes.append((register, value))
        if register == 0xf5:
            # Capture values and flash controller registers must NEVER be written.
            assert self.regs[0xf4] == 0x9f
            self.scaler[0x9f] = value
        else:
            assert register in (0x6f, 0xf3, 0xf4)
            self.regs[register] = value


class CaptureTests(unittest.TestCase):
    def test_gpio_only_drives_selected_pins_low(self):
        io = object.__new__(Ftdi)
        commands = []
        io.exchange = lambda data, length: commands.append(data) or b'\x30'
        for scl in (False, True):
            for sda in (False, True):
                io.pins(scl, sda)
        self.assertEqual(commands, [bytes((0x80, 0, mask, 0x81, 0x87))
                                    for mask in (SCL | SDA, SCL, SDA, 0)])

    def test_send_msb_first_and_nack(self):
        # Three GPIO samples per data bit, then ACK at sample index 25.
        response = [True]*27
        response[25] = False
        io = TraceIo(response)
        I2c(io).send(0x94)
        self.assertEqual(io.trace[:24:3], [(False, bool(0x94 & (1 << b))) for b in range(7, -1, -1)])
        with self.assertRaisesRegex(OSError, 'NACK'):
            I2c(TraceIo()).send(0x94)

    def test_receive_final_byte_nack(self):
        response = []
        for bit in (1, 0, 1, 0, 0, 1, 0, 1):
            response.extend((True, bool(bit), True))
        io = TraceIo(response)
        self.assertEqual(I2c(io).receive(True), 0xa5)
        self.assertEqual(io.trace[-3:], [(False, True), (True, True), (False, True)])

    def test_bus_stop_on_nack(self):
        io = TraceIo()
        with self.assertRaises(OSError):
            I2c(io).read(0x6f)
        self.assertEqual(io.trace[-3:], [(False, False), (True, False), (True, True)])

    def test_clock_stretch_timeout(self):
        io = TraceIo()
        io.pins = lambda scl, sda: 0
        with patch('rtd266x_capture.time.monotonic', side_effect=(0, 0.5, 1.1)):
            with self.assertRaises(TimeoutError):
                I2c(io).high(True)

    def test_decode_and_restore_every_selector(self):
        bus = RegisterBus()
        result = Rtd(bus).snapshot()
        self.assertEqual(result['capture_register_interpretation']['vertical_start'], 26)
        self.assertEqual(result['capture_register_interpretation']['vertical_length'], 232)
        self.assertEqual(bus.regs, {0x6f: 0, 0xf3: 0x20, 0xf4: 0x74})
        self.assertEqual(bus.scaler[0x9f], 2)
        self.assertFalse(result['capture_registers_modified'])

    def test_read_failure_still_restores_and_exits_isp(self):
        bus = RegisterBus()
        bus.fail_register = 0x1a
        with self.assertRaisesRegex(OSError, 'injected'):
            Rtd(bus).snapshot()
        self.assertEqual(bus.regs, {0x6f: 0, 0xf3: 0x20, 0xf4: 0x74})
        self.assertEqual(bus.scaler[0x9f], 2)
        self.assertEqual(bus.writes[-1], (0x6f, 0))

    def test_existing_isp_is_left_untouched(self):
        bus = RegisterBus()
        bus.regs[0x6f] = 0x80
        with self.assertRaisesRegex(RuntimeError, 'already'):
            Rtd(bus).snapshot()
        self.assertEqual(bus.writes, [])


if __name__ == '__main__':
    unittest.main()
