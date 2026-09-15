#!/usr/bin/env python3
"""Verify the separate physical CP79 deployment record; never access hardware."""
import hashlib
import json
import re
from pathlib import Path


def verify(root):
    project = Path(__file__).resolve().parents[1]
    deployment = json.loads((root / 'deployment.json').read_text())
    for name, digest in deployment['artifacts'].items():
        path = (root / name).resolve()
        assert path.is_relative_to(root.resolve()), name
        assert hashlib.sha256(path.read_bytes()).hexdigest() == digest, name

    release = project / 'demos/rt11/service/cp79'
    manifest = json.loads((release / 'release.json').read_text())
    for original, readback in [('FP11.BIN', 'FPCHK.BIN'),
                               ('ODT.BIN', 'ODCHK.BIN'),
                               ('FPTST.SAV', 'FTCHK.SAV')]:
        actual = (root / 'share' / readback).read_bytes()
        assert actual == (release / original).read_bytes(), readback
        assert hashlib.sha256(actual).hexdigest() == manifest['files'][original]

    def uart(name):
        return (root / name / 'uart.bin').read_bytes().decode('ascii').replace('\r', '')

    assert '!UJMOD-I-DONE' in uart('08-fp-register')
    assert '!UJMOD-I-DONE' in uart('09-odt-register')
    rows = {'0': '010000 014233 134145', '1': '006000 000342 053365',
            '2': '060000 004115 034323'}
    loaded = uart('10-status-loaded')
    cold = uart('12-status-cold')
    for slot, values in rows.items():
        assert f'{slot}: {values} 140407' in cold, slot
        assert f'{slot}: {values} 140407' in uart('19-final-status'), slot
        # SDBOOT is retained, not reloaded by this deployment.
        status = '140407' if slot == '1' else '140403'
        assert f'{slot}: {values} {status}' in loaded, slot
    assert re.search(r'RT-11FB[^\n]*V05\.03', uart('11-cold-boot'))
    assert '!FPTST-I-F D ARITHMETIC TEST' in uart('13-run-fptst')
    assert 'UJ11 ODT CP77: DEBUG' in uart('14-odt-enter')

    expected = (project / 'tb/reports/cp79/rt11/uart.txt').read_text()
    expected_steps = [re.search(r'R7=([0-7]{6})\nPSW=([0-7]{6})', block).groups()
                      for block in expected.split('ODT> S\n')[1:]]
    actual_steps = re.findall(r'R7=([0-7]{6})\nPSW=([0-7]{6})', uart('16-steps'))
    assert len(expected_steps) == 43
    assert actual_steps == expected_steps, 'physical STEP PC/PSW trace'
    state = uart('17-fp-state')
    for line in ['AC0=044200 000000 000000 000000',
                 'AC1=044200 000000 000000 000000',
                 'AC2=040200 012345 065432 023456',
                 'AC3=000000 000000 000000 000000',
                 'AC4=000000 000000 000000 000000',
                 'AC5=000000 000000 000000 000000',
                 'FPS=144300 MODE=D,L', 'FEC=000014', 'FEA=001106']:
        assert line in state, line
    for line in ['002342: 100123', '002344: 004567', '002346: 144000',
                 '002350: 000014', '002352: 001106',
                 '001014: 172667 001272  LDf 002312,AC2']:
        assert line in state, line
    finish = uart('18-finish')
    assert '!FPTST-I-RETURNED TO RT11' in finish
    assert '?FPTST-E-' not in finish
    assert '\n.' in finish.split('!FPTST-I-RETURNED TO RT11', 1)[1]
    assert 'FPTST .SAV' in uart('19-final-dir')
    assert deployment['fpga_reprogrammed'] is False
    assert deployment['hg_stopped'] is True
    assert deployment['uart_released'] is True
    assert deployment['reader_exited_during_capture'] is True
    print('PASS CP79 hardware: 3 SD readbacks, module registration/cold init, '
          '43 STEP PC/PSW states, FP registers and native FPTST return')


if __name__ == '__main__':
    verify(Path(__file__).resolve().parents[1] / 'tb/reports/cp79-hardware')
