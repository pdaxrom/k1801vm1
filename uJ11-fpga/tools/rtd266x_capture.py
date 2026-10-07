#!/usr/bin/env python3
"""Read RTD266x capture registers through spare HC7000 FT2232D GPIOs.

Uses libftdi1, channel A, ADBUS4=SCL and ADBUS5=SDA. Each edge is
acknowledged over USB: intentionally slow, with real input/low-output
open-drain emulation and clock stretching. No flash program/erase commands.
Sources: FTDI DS_FT2232D/AN_108; Mcsiv/OpenDisplayController programmer;
danyaPostfactum/RTD2660 Core/code/Mode.c and Core/header/ScalerDef.h.
"""
import argparse
import contextlib
import ctypes
import ctypes.util
import json
import re
import time
from pathlib import Path

SCL, SDA = 0x10, 0x20


def devices(root=Path('/sys/bus/usb/devices')):
    result = []
    for entry in sorted(root.glob('*')):
        if not (entry/'idVendor').is_file():
            continue
        values = {name: (entry/name).read_text().strip()
                  for name in ('idVendor', 'idProduct', 'bcdDevice', 'busnum', 'devnum', 'product')
                  if (entry/name).is_file()}
        if values.get('idVendor') == '0403' and values.get('idProduct') == '6010':
            values['usb_device'] = f"{int(values['busnum'])}:{int(values['devnum'])}"
            result.append(values)
    return result


class Ftdi:
    """Only claims interface A; does not reset USB or touch interface B/EEPROM."""
    def __init__(self, bus, address):
        library = ctypes.util.find_library('ftdi1')
        if not library:
            raise RuntimeError('libftdi1 is unavailable')
        self.lib = ctypes.CDLL(library)
        pointer, integer = ctypes.c_void_p, ctypes.c_int
        for name, args, result in (
                ('ftdi_new', [], pointer), ('ftdi_free', [pointer], None),
                ('ftdi_set_interface', [pointer, integer], integer),
                ('ftdi_usb_open_bus_addr', [pointer, integer, integer], integer),
                ('ftdi_usb_close', [pointer], integer),
                ('ftdi_set_bitmode', [pointer, ctypes.c_ubyte, ctypes.c_ubyte], integer),
                ('ftdi_set_latency_timer', [pointer, ctypes.c_ubyte], integer),
                ('ftdi_tcioflush', [pointer], integer),
                ('ftdi_write_data', [pointer, pointer, integer], integer),
                ('ftdi_read_data', [pointer, pointer, integer], integer),
                ('ftdi_get_error_string', [pointer], ctypes.c_char_p)):
            function = getattr(self.lib, name)
            function.argtypes, function.restype = args, result
        self.ctx = self.lib.ftdi_new()
        self.opened = False
        if not self.ctx:
            raise RuntimeError('ftdi_new failed')
        try:
            self.check(self.lib.ftdi_set_interface(self.ctx, 1))  # A only.
            self.check(self.lib.ftdi_usb_open_bus_addr(self.ctx, bus, address))
            self.opened = True
            self.check(self.lib.ftdi_set_latency_timer(self.ctx, 1))
            # Discard a previously stalled MPSSE command before starting a
            # new session. A second MPSSE-enable alone retains its parser.
            self.check(self.lib.ftdi_set_bitmode(self.ctx, 0, 0))
            self.check(self.lib.ftdi_tcioflush(self.ctx))
            self.check(self.lib.ftdi_set_bitmode(self.ctx, 0, 2))  # All inputs.
            self.check(self.lib.ftdi_tcioflush(self.ctx))
            if self.exchange(b'\xaa\x87', 2) != b'\xfa\xaa':
                raise RuntimeError('MPSSE synchronization failed')
        except BaseException:
            self.close()
            raise

    def check(self, value):
        if value < 0:
            error = self.lib.ftdi_get_error_string(self.ctx)
            raise OSError(error.decode(errors='replace') if error else 'FTDI error')
        return value

    def exchange(self, data, length):
        end, offset = time.monotonic()+3, 0
        buffer = ctypes.create_string_buffer(data)
        while offset < len(data):
            count = self.check(self.lib.ftdi_write_data(self.ctx, ctypes.byref(buffer, offset), len(data)-offset))
            if not count or time.monotonic() >= end:
                raise TimeoutError('FTDI write did not complete')
            offset += count
        received = ctypes.create_string_buffer(length)
        offset = 0
        while offset < length:
            count = self.check(self.lib.ftdi_read_data(self.ctx, ctypes.byref(received, offset), length-offset))
            offset += count
            if offset < length and time.monotonic() >= end:
                raise TimeoutError('FTDI read did not complete')
        return received.raw

    def pins(self, scl, sda):
        # Value is ALWAYS zero. A logic one means input, never driven high.
        direction = (0 if scl else SCL) | (0 if sda else SDA)
        return self.exchange(bytes((0x80, 0, direction, 0x81, 0x87)), 1)[0]

    def close(self):
        try:
            if self.opened:
                try:
                    self.pins(True, True)
                finally:
                    self.lib.ftdi_usb_close(self.ctx)
                    self.opened = False
        finally:
            if self.ctx:
                self.lib.ftdi_free(self.ctx)
                self.ctx = None


class I2c:
    def __init__(self, io):
        self.io = io

    def high(self, sda):
        deadline = time.monotonic()+1
        while True:
            pins = self.io.pins(True, sda)
            if pins & SCL:
                return pins
            if time.monotonic() >= deadline:
                raise TimeoutError('SCL remains low')

    def start(self):
        self.io.pins(False, True)
        if not self.high(True) & SDA:
            raise OSError('SDA remains low; check wiring/pull-ups')
        self.high(False)
        self.io.pins(False, False)

    def stop(self):
        self.io.pins(False, False)
        self.high(False)
        self.high(True)

    def send(self, value):
        for shift in range(7, -1, -1):
            bit = bool(value & (1 << shift))
            self.io.pins(False, bit)
            self.high(bit)
            self.io.pins(False, bit)
        self.io.pins(False, True)
        acknowledged = not self.high(True) & SDA
        self.io.pins(False, True)
        if not acknowledged:
            raise OSError(f'I2C NACK after byte {value:02x}')

    def receive(self, last):
        value = 0
        for _ in range(8):
            self.io.pins(False, True)
            value = (value << 1) | bool(self.high(True) & SDA)
            self.io.pins(False, True)
        self.io.pins(False, last)  # ACK except the final byte (NACK).
        self.high(last)
        self.io.pins(False, True)
        return value

    def transfer(self, register, value=None):
        try:
            self.start()
            self.send(0x94)  # RTD266x ISP, 7-bit address 0x4a.
            self.send(register)
            if value is not None:
                self.send(value)
                return None
            self.start()
            self.send(0x95)
            return self.receive(True)
        finally:
            self.stop()

    def read(self, register):
        return self.transfer(register)

    def write(self, register, value):
        self.transfer(register, value)


class Rtd:
    def __init__(self, bus):
        self.bus = bus

    @contextlib.contextmanager
    def isp(self):
        initial = self.bus.read(0x6f)
        if initial & 0xe0:
            raise RuntimeError('Controller already in ISP/program operation; leave it untouched')
        saved = {}
        try:
            self.bus.write(0x6f, 0x80)
            if not self.bus.read(0x6f) & 0x80:
                raise RuntimeError('Controller did not enter ISP mode')
            for register in (0xf3, 0xf4):
                saved[register] = self.bus.read(register)
            self.bus.write(0xf3, 0x22)  # No autoincrement; no MCU release by IRQ.
            saved['page'] = self.scaler_read(0x9f)
            yield
        finally:
            # Run every restoration even if an earlier one fails.
            errors = []
            actions = []
            if 'page' in saved:
                actions.append(lambda: self.page(saved['page']))
            for register in (0xf4, 0xf3):
                if register in saved:
                    actions.append(lambda r=register: self.bus.write(r, saved[r]))
            actions.append(lambda: self.bus.write(0x6f, 0))
            for action in actions:
                try:
                    action()
                except BaseException as error:
                    errors.append(str(error))
            if errors:
                raise RuntimeError('ISP cleanup failed; power-cycle display: '+ '; '.join(errors))

    def scaler_read(self, register):
        self.bus.write(0xf4, register)
        return self.bus.read(0xf5)

    def page(self, value):
        # The ONLY scaler register written is the page selector, never capture values.
        self.bus.write(0xf4, 0x9f)
        self.bus.write(0xf5, value)

    def snapshot(self):
        with self.isp():
            print('Reading capture window (MCU paused)', flush=True)
            common = {f'{r:02x}': self.scaler_read(r) for r in range(0x14, 0x1f)}
            self.page(9)
            decoder = {f'{r:02x}': self.scaler_read(r) for r in (0xe0, 0xe1)}
        if self.bus.read(0x6f) & 0x80:
            raise RuntimeError('ISP is still enabled; power-cycle display')
        def pair(high):
            return ((common[f'{high:02x}'] & 7) << 8) | common[f'{high+1:02x}']
        return {'raw_common': common, 'raw_decoder_page9': decoder,
                'capture_register_interpretation': {
                    'horizontal_start': pair(0x14), 'horizontal_length': pair(0x16),
                    'vertical_start': pair(0x18), 'vertical_length': pair(0x1a),
                    'vertical_delay_register': common['1c']},
                'interpretation_basis': 'RTD2660 register map; validate chip/firmware variant',
                'isp_exited': True, 'display_flash_modified': False,
                'capture_registers_modified': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--list', action='store_true', help='USB sysfs inventory only; does not open FTDI')
    parser.add_argument('--usb-device', help='Explicit BUS:DEVICE from --list, for example 1:59')
    parser.add_argument('--out', type=Path, help='New JSON output file')
    args = parser.parse_args()
    available = devices()
    if args.list:
        print(json.dumps(available, indent=2))
        return
    if not args.usb_device or not re.fullmatch(r'[0-9]+:[0-9]+', args.usb_device) or not args.out:
        parser.error('--usb-device BUS:DEVICE and --out NEW_FILE are required')
    matches = [d for d in available if d['usb_device'] == args.usb_device]
    if len(matches) != 1 or matches[0].get('bcdDevice') != '0500':
        parser.error('Select a detected FT2232C/D (bcdDevice 0500)')
    # Exclusive creation before opening hardware: never overwrite earlier evidence.
    with args.out.open('x') as output:
        result = {'usb': matches[0], 'scl': 'ADBUS4/pin20', 'sda': 'ADBUS5/pin19', 'passed': False}
        io = None
        try:
            io = Ftdi(*map(int, args.usb_device.split(':')))
            result.update(Rtd(I2c(io)).snapshot())
            result['passed'] = True
        except BaseException as error:
            result['error'] = str(error)
            raise
        finally:
            try:
                if io:
                    io.close()
            except BaseException as error:
                result['passed'] = False
                result['cleanup_error'] = str(error)
                raise
            finally:
                json.dump(result, output, indent=2)
                output.write('\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
