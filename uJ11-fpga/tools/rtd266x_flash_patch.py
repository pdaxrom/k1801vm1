#!/usr/bin/env python3
"""Apply/restore the verified two-byte NTSC patch in one W25X40 sector.

Requires two factory copies, their controller CRC, and the exact prepared
patch. Never erases the whole chip. Verifies the sector and full-image CRC,
rolls back on failure, restores protection, and performs a full readback.
An unverified rollback keeps the MCU paused in ISP for recovery.
"""
import argparse
import contextlib
import hashlib
import json
import time
from pathlib import Path

from rtd266x_capture import Ftdi, I2c, devices
from rtd266x_flash_read import BufferedI2c, FlashReader
from rtd266x_firmware import prepare, SIZE, SECTOR_OFFSET, SECTOR_SIZE


class SectorEditor(FlashReader):
    SAVED = (*FlashReader.SAVED, 0x62, 0x6d, 0x6e, 0x71)

    def __init__(self, bus):
        super().__init__(bus)
        self.leave_mcu_paused = False

    @contextlib.contextmanager
    def session(self):
        if self.bus.read(0x6f) & 0xe0:
            raise RuntimeError('ISP/programming is already active; leave untouched')
        saved = {}
        controller_changed = False
        try:
            self.bus.write(0x6f, 0x80)
            if not self.bus.read(0x6f) & 0x80:
                raise RuntimeError('ISP did not enable')
            for register in (0xf3, 0xf4):
                saved[register] = self.bus.read(register)
            self.bus.write(0xf3, 0x22)
            saved['page'] = self.scaler_read(0x9f)
            for register in self.SAVED:
                saved[register] = self.bus.read(register)
            if saved[0x60] & 1:
                raise RuntimeError('SPI command is already active')
            controller_changed = True
            for register, value in ((0x6a, 3), (0x62, 6), (0x6d, 2), (0x6e, 5)):
                self.bus.write(register, value)
            yield
        finally:
            if not self.leave_mcu_paused:
                errors = []
                actions = []
                if 'page' in saved:
                    actions.append(lambda: self.page(saved['page']))
                flash_registers = tuple(reversed(self.SAVED)) if controller_changed else ()
                for register in (*flash_registers, 0xf4, 0xf3):
                    if register in saved:
                        actions.append(lambda r=register: self.bus.write(r, saved[r]))
                actions.append(lambda: self.bus.write(0x6f, 0))
                for action in actions:
                    try:
                        action()
                    except BaseException as error:
                        errors.append(str(error))
                if errors:
                    raise RuntimeError('ISP/controller restoration failed: '+ '; '.join(errors))

    def ready(self):
        deadline = time.monotonic()+5
        while self.command(0x05, 1)[0] & 1:
            if time.monotonic() >= deadline:
                raise TimeoutError('W25X40 remains busy')

    def write_status(self, value):
        if value & 3:
            raise ValueError('Status value must omit volatile BUSY/WEL bits')
        self.ready()
        self.bus.write(0x60, 0x68)  # Write one byte after WREN.
        self.bus.write(0x61, 0x01)
        self.bus.write(0x64, value)
        self.bus.write(0x60, 0x69)
        self.wait()
        self.ready()
        if self.command(0x05, 1)[0] & 0xfc != value:
            raise RuntimeError('Flash protection/status change did not verify')

    def erase_sector(self):
        self.ready()
        self.bus.write(0x60, 0x78)  # Write three address bytes after WREN.
        self.bus.write(0x61, 0x20)  # Only a 4 KiB sector, never chip erase.
        for register, shift in ((0x64, 16), (0x65, 8), (0x66, 0)):
            self.bus.write(register, (SECTOR_OFFSET >> shift) & 255)
        self.bus.write(0x60, 0x79)
        self.wait()
        self.ready()

    def program_page(self, address, data):
        if not SECTOR_OFFSET <= address < SECTOR_OFFSET+SECTOR_SIZE or address % 256 or len(data) != 256:
            raise ValueError('Programming is limited to full pages in sector 3000')
        self.ready()
        self.bus.write(0x71, 255)
        for register, shift in ((0x64, 16), (0x65, 8), (0x66, 0)):
            self.bus.write(register, (address >> shift) & 255)
        for offset in range(0, 256, 16):
            self.bus.write_block(0x70, data[offset:offset+16])
        self.bus.write(0x6f, 0xa0)  # ISP + page program, fixed opcode 02.
        deadline = time.monotonic()+5
        while self.bus.read(0x6f) & 0x20:
            if time.monotonic() >= deadline:
                raise TimeoutError('Page programming did not finish')
        self.ready()

    def replace_sector(self, data):
        if len(data) != SECTOR_SIZE:
            raise ValueError('Expected exactly one 4 KiB sector')
        self.erase_sector()
        for offset in range(0, SECTOR_SIZE, 256):
            self.program_page(SECTOR_OFFSET+offset, data[offset:offset+256])

    def sector(self):
        return b''.join(self.read_chunk(SECTOR_OFFSET+offset, 1024)
                        for offset in range(0, SECTOR_SIZE, 1024))

    def apply(self, expected, desired, before_crc, after_crc, record):
        if self.sector() != expected or self.checksum(SIZE) != before_crc:
            raise RuntimeError('Installed flash does not match the verified source image')
        original_status = self.command(0x05, 1)[0] & 0xfc
        record['status_nonvolatile_before'] = original_status
        touched = False
        try:
            self.write_status(original_status & ~0x1c)
            # Until the whole sector verifies, the MCU must stay halted.
            touched = True
            self.leave_mcu_paused = True
            record['flash_write_started'] = True
            self.replace_sector(desired)
            if self.sector() != desired or self.checksum(SIZE) != after_crc:
                raise RuntimeError('Patched sector or full flash CRC did not verify')
            record['sector_verified'] = True
            record['full_flash_crc8'] = after_crc
            self.leave_mcu_paused = False
        except BaseException as error:
            record['write_error'] = str(error)
            if touched:
                try:
                    self.replace_sector(expected)
                    if self.sector() != expected or self.checksum(SIZE) != before_crc:
                        raise RuntimeError('Rollback verification failed')
                    self.leave_mcu_paused = False
                    record['rolled_back'] = True
                except BaseException as rollback_error:
                    record['rollback_error'] = str(rollback_error)
                    record['mcu_left_paused'] = True
            raise
        finally:
            self.write_status(original_status)
            record['protection_restored'] = True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--usb-device', required=True)
    parser.add_argument('--backup-directory', required=True, type=Path)
    parser.add_argument('--patch-directory', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path, help='New operation JSON file')
    parser.add_argument('--restore', action='store_true')
    args = parser.parse_args()
    first = (args.backup_directory/'factory-1.bin').read_bytes()
    second = (args.backup_directory/'factory-2.bin').read_bytes()
    verification = json.loads((args.backup_directory/'verification.json').read_text())
    image, old, new, manifest = prepare(first, second, verification)
    if (args.patch_directory/'ntsc-240.bin').read_bytes() != image:
        parser.error('Prepared patch image mismatch')
    for filename, sector in (('factory-sector-3000.bin', old), ('ntsc-240-sector-3000.bin', new)):
        if (args.patch_directory/filename).read_bytes() != sector:
            parser.error('Prepared sector mismatch')
    selected = [d for d in devices() if d['usb_device'] == args.usb_device and d.get('bcdDevice') == '0500']
    if len(selected) != 1:
        parser.error('Select one detected FT2232C/D')
    if args.restore:
        old, new = new, old
        before_crc, after_crc = manifest['patched_crc8'], manifest['factory_crc8']
        desired_hash = manifest['factory_sha256']
    else:
        before_crc, after_crc = manifest['factory_crc8'], manifest['patched_crc8']
        desired_hash = manifest['patched_sha256']
    record = {'passed': False, 'restore': args.restore, 'usb': selected[0], 'sector_offset': SECTOR_OFFSET,
              'flash_write_started': False, 'desired_sha256': desired_hash, 'fpga_modified': False}
    io = None
    readback_path = args.out.with_suffix('.bin')
    if readback_path.exists():
        parser.error('Readback file already exists')
    with args.out.open('x') as output:
        try:
            io = Ftdi(*map(int, args.usb_device.split(':')))
            bus = BufferedI2c(io)
            editor = SectorEditor(bus)
            with editor.session():
                if editor.command(0x9f, 3).hex() != 'ef3013':
                    raise RuntimeError('Wrong flash chip')
                editor.apply(old, new, before_crc, after_crc, record)
            print('Sector and full-image CRC verified; reading back the complete flash', flush=True)
            reader = FlashReader(bus)
            with readback_path.open('xb') as readback, reader.session():
                digest = reader.backup(readback, SIZE)
            record['readback'] = str(readback_path)
            record['readback_sha256'] = digest
            if digest != desired_hash:
                raise RuntimeError('Full flash readback differs from prepared image')
            record['isp_status_after'] = f'{I2c(io).read(0x6f):02x}'
            if int(record['isp_status_after'], 16) & 0x80:
                raise RuntimeError('ISP is still enabled')
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
                json.dump(record, output, indent=2)
                output.write('\n')
    print(json.dumps(record, indent=2))


if __name__ == '__main__':
    main()
