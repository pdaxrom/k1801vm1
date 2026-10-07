#!/usr/bin/env python3
"""Read two RTD266x flash backups through external FT2232D ADBUS4/5.

Read-only SPI opcodes: 9F (JEDEC), 03 (data), 05 (status). No WREN,
program, erase or status-write commands. ISP selectors are restored.
Sources: FTDI AN_108/AN_135 and floppes/RTD266xFlash rtd266x.cpp.
"""
import argparse
import contextlib
import hashlib
import json
import time
from pathlib import Path

from rtd266x_capture import Ftdi, I2c, Rtd, SCL, SDA, devices


def crc8(data):
    """RTD flash-controller CRC: polynomial 07, initial 00, no reflection."""
    value = 0
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = ((value << 1) ^ (7 if value & 0x80 else 0)) & 255
    return value


class BufferedI2c(I2c):
    """Open-drain GPIO packets; abort on NACK, arbitration or stretching.

    A one-bit MPSSE shift with SK/DO configured as INPUTS provides a
    10us delay after GPIO transitions (6MHz / divisor 60 = 100kHz).
    SCL/SDA are still only driven low or released on ADBUS4/5.
    Every high phase is sampled. Stretching is detected and rejected;
    the slow transport is used to issue STOP even when a packet fails.
    """
    def __init__(self, io):
        super().__init__(io)
        io.exchange(b'\x86\x3b\x00\x87', 0)

    def packet(self, register, value=None, length=1):
        if not 0 <= register <= 255 or not 1 <= length <= 32:
            raise ValueError('Invalid register or block length')
        payload = None
        if value is not None:
            if isinstance(value, int):
                if not 0 <= value <= 255:
                    raise ValueError('Invalid register value')
                payload = bytes((value,))
            else:
                payload = bytes(value)
                if not 1 <= len(payload) <= 16:
                    raise ValueError('Invalid block-write length')
        command, checks, bits, batches = bytearray(), [], [], []
        sample_start = 0

        def edge(scl, sda, expected=None):
            nonlocal sample_start
            direction = (0 if scl else SCL) | (0 if sda else SDA)
            command.extend((0x80, 0, direction, 0x13, 0, 0))
            index = None
            if scl:
                command.append(0x81)
                checks.append(expected)
                index = len(checks)-1
            # FT2232D has small FIFOs: consume input while sending the
            # packet, instead of queuing a whole 32-byte transaction.
            if len(command) >= 700:
                command.append(0x87)
                batches.append((bytes(command), len(checks)-sample_start))
                sample_start = len(checks)
                command.clear()
            return index

        def start():
            edge(False, True)
            edge(True, True, True)
            edge(True, False)
            edge(False, False)

        def send(byte):
            for shift in range(7, -1, -1):
                bit = bool(byte & (1 << shift))
                edge(False, bit)
                edge(True, bit, bit)
                edge(False, bit)
            edge(False, True)
            edge(True, True, False)  # Slave ACK.
            edge(False, True)

        def receive(last):
            indices = []
            for _ in range(8):
                edge(False, True)
                indices.append(edge(True, True))
                edge(False, True)
            edge(False, last)
            edge(True, last)
            edge(False, True)
            bits.append(indices)

        start()
        send(0x94)
        send(register)
        if payload is None:
            start()
            send(0x95)
            for n in range(length):
                receive(n == length-1)
        else:
            for byte in payload:
                send(byte)
        edge(False, False)
        edge(True, False)
        edge(True, True, True)
        command.append(0x87)
        batches.append((bytes(command), len(checks)-sample_start))
        try:
            sampled = b''.join(self.io.exchange(data, count) for data, count in batches)
            for index, (pins, expected) in enumerate(zip(sampled, checks)):
                if not pins & SCL:
                    raise OSError(f'Buffered I2C clock stretch at sample {index}; use slow transport')
                if expected is not None and bool(pins & SDA) != expected:
                    raise OSError(f'Buffered I2C NACK/arbitration at sample {index}')
            result = bytearray()
            for indices in bits:
                byte = 0
                for index in indices:
                    byte = (byte << 1) | bool(sampled[index] & SDA)
                result.append(byte)
            return bytes(result)
        except BaseException:
            I2c(self.io).stop()
            raise

    def read(self, register):
        return self.packet(register)[0]

    def write(self, register, value):
        self.packet(register, value)

    def read_block(self, register, length):
        return self.packet(register, length=length)

    def write_block(self, register, data):
        if register != 0x70:
            raise ValueError('Block writes are limited to the SRAM data port')
        self.packet(register, bytes(data))


class FlashReader(Rtd):
    # None of these are program/erase/WREN registers. Restore only after
    # the read instruction completed, never restart a saved active command.
    SAVED = (0x60, 0x61, 0x64, 0x65, 0x66, 0x6a, 0x72, 0x73, 0x74)

    @contextlib.contextmanager
    def session(self):
        with self.isp():
            saved = {register: self.bus.read(register) for register in self.SAVED}
            if saved[0x60] & 1:
                raise RuntimeError('An SPI command is already active')
            try:
                self.bus.write(0x6a, 0x03)  # Read opcode only.
                yield
            finally:
                errors = []
                for register in reversed(self.SAVED):
                    try:
                        self.bus.write(register, saved[register])
                    except BaseException as error:
                        errors.append(str(error))
                if errors:
                    raise RuntimeError('Flash-selector restoration failed: '+ '; '.join(errors))
        if self.bus.read(0x6f) & 0x80:
            raise RuntimeError('ISP did not exit')

    def wait(self):
        deadline = time.monotonic()+3
        while self.bus.read(0x60) & 1:
            if time.monotonic() >= deadline:
                raise TimeoutError('SPI read command did not finish')

    def command(self, opcode, count):
        if opcode not in (0x9f, 0x05) or count not in (1, 3):
            raise ValueError('Only JEDEC/status reads are permitted')
        value = 0x40 | (count << 1)
        self.bus.write(0x60, value)
        self.bus.write(0x61, opcode)
        self.bus.write(0x60, value | 1)
        self.wait()
        return bytes(self.bus.read(0x67+n) for n in range(count))

    def read_chunk(self, address, length):
        if not 0 <= address < 0x1000000 or not 1 <= length <= 1024:
            raise ValueError('Invalid flash read range')
        self.bus.write(0x60, 0x46)
        self.bus.write(0x61, 0x03)
        for register, shift in ((0x64, 16), (0x65, 8), (0x66, 0)):
            self.bus.write(register, (address >> shift) & 255)
        self.bus.write(0x60, 0x47)
        self.wait()
        result = bytearray()
        while len(result) < length:
            count = min(32, length-len(result))
            if hasattr(self.bus, 'read_block'):
                result.extend(self.bus.read_block(0x70, count))
            else:
                # A single I2C read transaction with ACK between bytes.
                try:
                    self.bus.start()
                    self.bus.send(0x94)
                    self.bus.send(0x70)
                    self.bus.start()
                    self.bus.send(0x95)
                    result.extend(self.bus.receive(n == count-1) for n in range(count))
                finally:
                    self.bus.stop()
        return bytes(result)

    def checksum(self, size):
        if not 1 <= size <= 0x1000000:
            raise ValueError('Invalid CRC range')
        for register in (0x64, 0x65, 0x66):
            self.bus.write(register, 0)
        for register, shift in ((0x72, 16), (0x73, 8), (0x74, 0)):
            self.bus.write(register, ((size-1) >> shift) & 255)
        # CRC_START reads the flash internally; PROG_EN remains clear.
        self.bus.write(0x6f, 0x84)
        deadline = time.monotonic()+3
        while not self.bus.read(0x6f) & 2:
            if time.monotonic() >= deadline:
                raise TimeoutError('Flash CRC did not complete')
        return self.bus.read(0x75)

    def backup(self, output, size):
        digest = hashlib.sha256()
        start = last = time.monotonic()
        for address in range(0, size, 1024):
            for attempt in range(3):
                try:
                    data = self.read_chunk(address, min(1024, size-address))
                    break
                except OSError:
                    # Read port 70 advances its pointer. Restart the entire
                    # addressed SPI chunk, never retry only a consumed block.
                    if attempt == 2:
                        raise
                    print(f'Retrying read-only chunk {address:06x}', flush=True)
                    I2c(self.bus.io).stop()
            output.write(data)
            digest.update(data)
            now = time.monotonic()
            if now-last >= 15 or address+len(data) == size:
                output.flush()
                print(f'{output.name}: {address+len(data)}/{size} bytes, {now-start:.1f}s', flush=True)
                last = now
        return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--usb-device', required=True)
    parser.add_argument('--directory', required=True, type=Path,
                        help='New directory; existing backups are never overwritten')
    parser.add_argument('--probe-only', action='store_true', help='Compare slow/fast first 32 bytes, no full backup')
    args = parser.parse_args()
    selected = [d for d in devices() if d['usb_device'] == args.usb_device and d.get('bcdDevice') == '0500']
    if len(selected) != 1:
        parser.error('Select one detected FT2232C/D')
    args.directory.mkdir()
    record = {'usb': selected[0], 'passed': False, 'flash_modified': False,
              'capture_modified': False, 'probe_only': args.probe_only}
    io = None
    try:
        io = Ftdi(*map(int, args.usb_device.split(':')))
        slow = FlashReader(I2c(io))
        with slow.session():
            ident = slow.command(0x9f, 3)
            record['jedec'] = ident.hex()
            # Capacity encoding verified for installed Winbond W25X40.
            if ident != bytes.fromhex('ef3013'):
                raise RuntimeError(f'Expected verified W25X40 (ef3013), got {ident.hex()}')
            record['size'] = size = 512*1024
            record['status_before'] = slow.command(0x05, 1).hex()
            prefix = slow.read_chunk(0, 32)
        fast = FlashReader(BufferedI2c(io))
        with fast.session():
            if fast.command(0x9f, 3) != ident or fast.read_chunk(0, 32) != prefix:
                raise RuntimeError('Slow/fast JEDEC or flash-prefix comparison failed')
            record['prefix_hex'] = prefix.hex()
            record['slow_fast_match'] = True
        if not args.probe_only:
            record['copies'] = []
            for number in (1, 2):
                path = args.directory/f'factory-{number}.bin'
                with path.open('xb') as output, fast.session():
                    if fast.command(0x9f, 3) != ident:
                        raise RuntimeError('JEDEC changed before backup')
                    digest = fast.backup(output, size)
                record['copies'].append({'path': path.name, 'size': path.stat().st_size, 'sha256': digest})
            record['copies_match'] = record['copies'][0]['sha256'] == record['copies'][1]['sha256']
            if not record['copies_match']:
                raise RuntimeError('The two full backups differ')
        with fast.session():
            record['status_after'] = fast.command(0x05, 1).hex()
        if record['status_before'] != record['status_after']:
            raise RuntimeError('Flash status changed')
        record['isp_status_after'] = f'{I2c(io).read(0x6f):02x}'
        if int(record['isp_status_after'], 16) & 0x80:
            raise RuntimeError('ISP is still active')
        record['passed'] = True
    except BaseException as error:
        record['error'] = str(error)
        raise
    finally:
        try:
            if io:
                io.close()
        except BaseException as error:
            record['cleanup_error'] = str(error)
            record['passed'] = False
            raise
        finally:
            (args.directory/'result.json').write_text(json.dumps(record, indent=2)+'\n')
    print(json.dumps(record, indent=2))


if __name__ == '__main__':
    main()
