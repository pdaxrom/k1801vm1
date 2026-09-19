#!/usr/bin/env python3
"""Verify saved CP80 board evidence; never access the board or UART."""
import hashlib
import json
import re
import tarfile
from pathlib import Path

from fp_psw_hardware_cp80 import plan, verify_capture


def verify(root):
    project = Path(__file__).resolve().parents[1]
    deployment = json.loads((root / 'deployment.json').read_text())
    for name, digest in deployment['qualification_references'].items():
        assert hashlib.sha256((project / name).read_bytes()).hexdigest() == digest, name
    for name, digest in deployment['artifacts'].items():
        path = (root / name).resolve()
        assert path.is_relative_to(root.resolve()), name
        assert hashlib.sha256(path.read_bytes()).hexdigest() == digest, name

    def uart(name):
        text = (root / name / 'uart.bin').read_bytes().decode('ascii').replace('\r', '')
        return re.sub(r'\x1b\[[0-9;]*[A-Za-z]', '', text)

    def memory(text):
        return {int(a, 8): int(v, 8) for a, v in
                re.findall(r'^([0-7]{6}): ([0-7]{6})$', text, re.M)}

    def registers(text):
        regs = {r: int(v, 8) for r, v in re.findall(r'^R([0-7])=([0-7]{6})', text, re.M)}
        psw = int(re.findall(r'^PSW=([0-7]{6})', text, re.M)[-1], 8)
        return regs, psw

    release = project / 'demos/rt11/service/cp80'
    manifest = json.loads((release / 'release.json').read_text())
    for name in ('FP11.BIN', 'FPCHK.BIN'):
        data = (root / 'share' / name).read_bytes()
        assert data == (release / 'FP11.BIN').read_bytes(), name
        assert hashlib.sha256(data).hexdigest() == manifest['files']['FP11.BIN']
    assert (root / 'share/OLDFP.BIN').read_bytes() == (
        project / 'demos/rt11/service/cp79/FP11.BIN').read_bytes()

    assert '!UJMOD-I-DONE' in uart('06-register')
    rows = {'0': '010000 014233 134145', '1': '006000 000342 053365',
            '2': '060000 004174 142034'}
    for slot, values in rows.items():
        status = '140403' if slot == '2' else '140407'
        assert f'{slot}: {values} {status}' in uart('07-status-loaded'), slot
        for log in ('09-status-cold', '21-final-status'):
            assert f'{slot}: {values} 140407' in uart(log), (slot, log)
    # User confirmed RESET after these passive capture windows had expired.
    # STATUS transition and the later ODT probe are the captured evidence.
    assert not (root / '08-cold-boot/uart.bin').read_bytes()
    assert not (root / '11-odt-enter/uart.bin').read_bytes()
    assert deployment['cold_reset_user_confirmed'] is True
    assert deployment['boot_banner_captured'] is False
    assert '!FPTST-I-F D ARITHMETIC TEST' in uart('10-run-fptst')
    assert uart('11b-odt-probe').rstrip().endswith('ODT>')

    p = plan()
    assert json.loads((root / 'rtl-program/plan.json').read_text()) == json.loads(json.dumps(p))
    qualification = json.loads((root / 'rtl-program/result.json').read_text())
    assert qualification['passed'] and qualification['instructions'] == 22
    assert 'PASS CP80 hardware program: 22 instructions / 97 checks' in (
        root / 'rtl-program/simulation.log').read_text()
    inputs = json.loads((root / 'rtl-program/source-inputs.json').read_text())
    assert all(inputs[k] == v for k, v in qualification['inputs'].items())
    seen = set()
    with tarfile.open(root / 'rtl-program/source.tgz', 'r:gz') as archive:
        for member in archive:
            assert member.isfile() and member.name in inputs and member.name not in seen
            seen.add(member.name)
            assert hashlib.sha256(archive.extractfile(member).read()).hexdigest() == inputs[member.name]
    assert seen == set(inputs)

    expected_memory = {p['base'] + 2*i: w for i, w in enumerate(p['words'])}
    assert memory(uart('13-write-probe')) == expected_memory
    combined = (root / '14-psw-steps/uart.bin').read_bytes()
    assert combined == b''.join((root / f'14-psw-steps/{i:02d}/uart.bin').read_bytes()
                                for i in range(1, 23))
    assert verify_capture(combined)['passed']
    original = json.loads((root / 'original-context.json').read_text())
    assert memory(uart('12-snapshot')) == dict(original['memory'])
    assert registers(uart('12-snapshot')) == (original['registers'], original['psw'])
    regs, psw = registers(uart('15-probe-results'))
    assert [regs[str(i)] for i in range(4)] == [1, 0o144757, 0, 0]
    assert all(regs[str(i)] == original['registers'][str(i)] for i in (4, 5, 6))
    assert regs['7'] == 0o6114 and psw == 0
    assert memory(uart('16-restore')) == dict(original['memory'])
    assert registers(uart('16-restore')) == (original['registers'], original['psw'])

    expected = (project / 'tb/reports/cp80/rt11/uart.txt').read_text().replace('\r', '')
    expected_steps = [re.search(r'R7=([0-7]{6})\nPSW=([0-7]{6})', b).groups()
                      for b in expected.split('ODT> S\n')[1:]]
    actual_steps = re.findall(r'R7=([0-7]{6})\nPSW=([0-7]{6})', uart('18-regression-steps'))
    assert len(expected_steps) == 43 and actual_steps == expected_steps
    state = uart('19-fp-state')
    for line in ['AC0=044200 000000 000000 000000',
                 'AC1=044200 000000 000000 000000',
                 'AC2=040200 012345 065432 023456',
                 'AC3=000000 000000 000000 000000',
                 'AC4=000000 000000 000000 000000',
                 'AC5=000000 000000 000000 000000',
                 'FPS=144300 MODE=D,L', 'FEC=000014', 'FEA=001106',
                 '002342: 100123', '002344: 004567', '002346: 144000',
                 '002350: 000014', '002352: 001106',
                 '001014: 172667 001272  LDf 002312,AC2']:
        assert line in state, line
    finish = uart('20-finish')
    assert '!FPTST-I-RETURNED TO RT11' in finish
    assert '?FPTST-E-' not in finish and 'STATE MISMATCH' not in finish
    assert '\n.' in finish.split('!FPTST-I-RETURNED TO RT11', 1)[1]
    assert uart('21-final-status').rstrip().endswith('.')

    host = json.loads((root / 'final-host-state.json').read_text())
    assert host['picocom']['state'] not in ('T', 't', 'Z')
    assert host['picocom']['name'] == 'picocom'
    assert host['hg_pid_present'] is False and host['uart_helpers'] == []
    assert deployment['fpga_reprogrammed'] is False
    assert deployment['new_synthesis'] is False
    assert deployment['hg_stopped'] is True and deployment['uart_released'] is True
    print('PASS CP80 hardware: SD readback, cold init, 22 PSW/FPS steps, '
          '39 words and CPU context restored, 43 regression PC/PSW states, '
          'FP state and native FPTST return')


if __name__ == '__main__':
    verify(Path(__file__).resolve().parents[1] / 'tb/reports/cp80-hardware')
