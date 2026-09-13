#!/usr/bin/env python3
"""Verify archived CP67 HG/ODT observations; never operate the board."""
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def raw(phase):
    return (ROOT / phase / 'uart.bin').read_bytes()


def verify():
    fixture = bytes(((i * 73) ^ (i >> 3) ^ (i >> 8) ^ 0xA5) & 255
                    for i in range(1024))
    assert (ROOT / 'fixture.bin').read_bytes() == fixture
    assert hashlib.sha256(fixture).hexdigest() == (
        'ca60af9efb88db2f5a9f25ee8718dfa6437878437ed8572e573ac784c2580dbf')
    for name in ('BEFORE.BIN', 'AFTER.BIN'):
        assert (ROOT / 'readback' / name).read_bytes() == fixture, name
    assert b'0 Files, 0 Blocks' in raw('check-name')
    assert b'H67TST.BIN' in raw('hg-load')
    for phase, target in (('copy-before', 'BEFORE'), ('copy-after', 'AFTER')):
        data = raw(phase)
        assert b'COPY HG:H67TST.BIN DK:H67TST.BIN\r\n' in data
        assert ('COPY DK:H67TST.BIN HG:' + target + '.BIN\r\n').encode() in data
        assert data.count(b'\n.') == 2 and b'?' not in data, phase
    cases = ['unused_sd_filename', 'hg_roundtrip_before_odt', 'hg_roundtrip_after_odt']

    # The first passive window ended before the user-confirmed RESET.
    assert raw('odt-hg-entry') == b''
    assert b'?SYNTAX OR RANGE; H FOR HELP\r\nODT> ' in raw('odt-hg-probe')
    regs = dict(re.findall(rb'(R[0-7]|PSW)=([0-7]{6})', raw('odt-hg-rd')))
    assert regs == {b'R0': b'050000', b'R1': b'126650', b'R2': b'126656',
                    b'R3': b'122136', b'R4': b'127240', b'R5': b'000000',
                    b'R6': b'155234', b'R7': b'142772', b'PSW': b'050004'}
    assert b'142772: 001772  BEQ 142760' in raw('odt-hg-rd')
    assert raw('odt-hg-rd').count(b'ODT> ') == 2
    assert b'C\r\nCONTINUE\r\n' in raw('odt-hg-continue')
    assert b'\n.' in raw('rt11-after-odt')
    cases.append('odt_registers_disassembly_continue')

    hg = json.loads((ROOT / 'hg-session.json').read_text())
    after = json.loads((ROOT / 'after-check.json').read_text())
    log = (ROOT / 'hg.log').read_text()
    assert hg['pid'] == after['same_daemon_pid'] == 180940
    assert after['passed'] and not after['daemon_restart']
    assert log.count('hgfsd: serving ') == 1
    assert not re.search(r'failed|malformed|cannot|error', log, re.I)
    assert 'read block 38' in log and 'write block 40' in log and 'write block 42' in log
    assert hg['stopped'] and hg['rt11_unload_hg_completed']
    assert b'UNLOAD HG\r\n' in raw('hg-unload') and b'\n.' in raw('hg-unload')
    cases.extend(['same_hg_daemon_without_errors', 'hg_unloaded_and_pins_released'])

    host = json.loads((ROOT / 'host-final.json').read_text())
    assert host['related_processes'] == [{
        'pid': 180461, 'state': 'S',
        'argv': ['picocom', '-b', '115200', '/dev/ttyUSB1']}]
    assert raw('panel-after-hg') == b''  # No observed keys, not a panel PASS.
    cases.append('uart_reader_restored_and_test_processes_stopped')

    for session in ROOT.glob('*/session.json'):
        record = json.loads(session.read_text())
        assert record.get('finished_utc'), session
        assert record['uart_bytes'] == len((session.parent / 'uart.bin').read_bytes()), session
        if session.parent.name not in ('odt-hg-probe', 'panel-after-hg'):
            assert b'?' not in (session.parent / 'uart.bin').read_bytes(), session
    return cases


if __name__ == '__main__':
    cases = verify()
    manifest = ROOT / 'verification.json'
    if manifest.exists():
        record = json.loads(manifest.read_text())
        assert record['passed_cases'] == cases
        for name, digest in record['artifacts_sha256'].items():
            assert hashlib.sha256((ROOT / name).read_bytes()).hexdigest() == digest, name
    print(f'PASS: {len(cases)} recorded scenarios; archive hashes checked when manifest present')
