#!/usr/bin/env python3
"""Linux board session: verified programming and passive BK boot UART."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
from xml.sax.saxutils import escape

from diamond_report import jed_record


def xcf(jed, checksum, operation, target):
    return f'''<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE ispXCF SYSTEM "IspXCF.dtd">
<ispXCF version="3.14">
  <Comment>BK0010-01 hardware-lcd board test</Comment>
  <Chain><Comm>JTAG</Comm><Device>
    <SelectedProg value="TRUE"/><Pos>1</Pos><Vendor>Lattice</Vendor>
    <Family>MachXO2</Family><Name>LCMXO2-7000HC</Name>
    <IDCode>0x012bd043</IDCode><Package>All</Package><PON>LCMXO2-7000HC</PON>
    <Bypass><InstrLen>8</InstrLen><InstrVal>11111111</InstrVal>
      <BScanLen>1</BScanLen><BScanVal>0</BScanVal></Bypass>
    <File>{escape(str(jed))}</File>
    <JedecChecksum>0x{checksum}</JedecChecksum><Operation>{operation}</Operation>
    <Option><SVFVendor>JTAG STANDARD</SVFVendor><IOState>HighZ</IOState>
      <PreloadLength>664</PreloadLength><IOVectorData>0x{'F'*166}</IOVectorData>
      <TCKFrequency>1.000000 MHz</TCKFrequency><Usercode>0x00000000</Usercode>
      <AccessMode>{target}</AccessMode></Option>
  </Device></Chain>
  <ProjectOptions><Program>SEQUENTIAL</Program><Process>ENTIRED CHAIN</Process>
    <OperationOverride>No Override</OperationOverride><StartTAP>TLR</StartTAP><EndTAP>TLR</EndTAP>
    <VerifyUsercode value="FALSE"/><TCKDelay>30</TCKDelay></ProjectOptions>
  <CableOptions><CableName>USB2</CableName><PortAdd>FTUSB-0</PortAdd>
    <USBID>DUAL RS232 A Location 0000 Serial DUAL RS232 A</USBID></CableOptions>
</ispXCF>
'''


def main():
    import serial
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jed', type=Path, required=True)
    parser.add_argument('--sha256', required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--port', default='/dev/ttyUSB1')
    parser.add_argument('--target', choices=('SRAM', 'FLASH'), default='SRAM')
    parser.add_argument('--jtag-helper', type=Path, required=True)
    parser.add_argument('--programmer', type=Path,
                        default=Path.home()/'.local/lscc/diamond/3.14/bin/lin64/pgrcmd')
    args = parser.parse_args()
    if not sys.platform.startswith('linux'):
        raise RuntimeError('Run this session on the Linux board host')
    args.jed = args.jed.resolve()
    exported = jed_record(args.jed)
    assert exported['sha256'] == args.sha256, 'Unexpected JED'
    assert 'NOTE DEVICE NAME:\tLCMXO2-7000HC-' in args.jed.read_text()
    owners = subprocess.run(['fuser', args.port], capture_output=True, text=True)
    assert owners.returncode in (0, 1), owners.stderr
    assert not owners.stdout.strip(), 'UART is already owned: '+owners.stdout
    args.out.mkdir(parents=True, exist_ok=False)
    capture = bytearray()
    record = {'started_utc': datetime.now(timezone.utc).isoformat(), 'jed': exported,
              'driver_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              'uart_port': args.port, 'actions': [], 'programmed_to': args.target,
              'flash_modified': False, 'physical_board_verified': False}

    def save():
        record['uart_bytes'] = len(capture)
        record['boot_stages'] = re.findall(r'B:([0-9A-F]{2})', capture.decode('ascii', 'replace'))
        record['boot_errors'] = re.findall(r'E:([0-9A-F]{2})', capture.decode('ascii', 'replace'))
        (args.out/'session.json').write_text(json.dumps(record, indent=2)+'\n')

    with serial.Serial(args.port, 115200, timeout=.05, exclusive=True) as port, \
            (args.out/'uart.bin').open('wb') as log:
        old_settings = port.get_settings()

        def read(seconds):
            deadline = time.monotonic()+seconds
            while time.monotonic() < deadline:
                data = port.read(max(1, port.in_waiting))
                if data:
                    capture.extend(data)
                    log.write(data)
                    log.flush()
                    print(data.decode('ascii', 'backslashreplace'), end='', flush=True)
            save()

        try:
            read(1)
            print('READY: exclusive passive UART; JSON actions verify-id/program/read/close', flush=True)
            for line in sys.stdin:
                request = json.loads(line)
                action = request['action']
                entry = {'action': action, 'utc': datetime.now(timezone.utc).isoformat(),
                         'uart_offset': len(capture), 'passed': False}
                record['actions'].append(entry)
                try:
                    if action == 'close':
                        entry['passed'] = True
                        break
                    if action == 'read':
                        read(max(0, min(30, float(request.get('seconds', 1)))))
                    elif action in ('verify-id', 'program'):
                        if action == 'program':
                            assert record.get('id_verified'), 'Verify ID first'
                        assert jed_record(args.jed) == exported, 'JED changed'
                        subprocess.run([str(args.jtag_helper), '--jtag-only'], check=True)
                        operation = args.target+(' Verify ID' if action == 'verify-id' else ' Erase,Program,Verify')
                        number = len(record['actions'])
                        config = args.out/f'{number:02d}-{action}.xcf'
                        config.write_text(xcf(args.jed, exported['checksum'], operation, args.target))
                        stdout = args.out/f'{number:02d}-{action}-stdout.log'
                        with stdout.open('w') as output:
                            if action == 'program' and args.target == 'FLASH':
                                record['flash_modified'] = True
                                save()
                            process = subprocess.Popen([str(args.programmer), '-infile', str(config),
                                '-logfile', str(args.out/f'{number:02d}-{action}.log')],
                                stdout=output, stderr=subprocess.STDOUT,
                                env=dict(os.environ, LD_PRELOAD='/lib/x86_64-linux-gnu/libstdc++.so.6'))
                            deadline = time.monotonic()+180
                            try:
                                while process.poll() is None:
                                    read(.1)
                                    if time.monotonic() >= deadline:
                                        raise TimeoutError('Programmer exceeded 180 seconds')
                            finally:
                                if process.poll() is None:
                                    process.terminate()
                                    try:
                                        process.wait(timeout=5)
                                    except subprocess.TimeoutExpired:
                                        process.kill()
                                        process.wait()
                        report = stdout.read_text()
                        print(report, flush=True)
                        entry.update(exit_code=process.returncode, operation=operation,
                                     stdout=str(stdout.name), config=str(config.name))
                        assert process.returncode == 0 and 'Operation: successful' in report, 'Programmer failed'
                        record['id_verified' if action == 'verify-id' else args.target.lower()+'_program_verified'] = True
                        read(2)
                    else:
                        raise ValueError('Unknown action '+action)
                    entry['passed'] = True
                except Exception as error:
                    entry['error'] = repr(error)
                    print('ACTION FAILED: '+repr(error), flush=True)
                finally:
                    save()
                print('ACTION COMPLETE: '+action, flush=True)
        finally:
            save()
            port.apply_settings(old_settings)
    print('CLOSED: UART released', flush=True)


if __name__ == '__main__':
    main()
