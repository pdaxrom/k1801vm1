#!/usr/bin/env python3
"""Check this physical UART capture; does not operate hardware or rerun RTL."""
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def raw(phase):
    return (ROOT / phase / 'uart.bin').read_bytes()


def contains(phase, *parts):
    data = raw(phase)
    for part in parts:
        assert part.encode('ascii') in data, (phase, part)


def registers(phase, **expected):
    data = raw(phase)
    for name, value in expected.items():
        matches = re.findall(rb'(?:^|\r\n)' + name.encode() + rb'=([0-7]{6})', data)
        assert matches and matches[-1].decode() == value, (phase, name, matches, value)


def verify():
    test = (ROOT / 'share/U66TST.SAV').read_bytes()
    assert len(test) == 1536
    assert hashlib.sha256(test).hexdigest() == '00dd4dd474e9ca528c4db80bafbe9250f0ff94cd5a5adf3775f02b835da4929a'
    assert test == (ROOT / 'share/TSTCHK.SAV').read_bytes()
    assert test == (ROOT.parent / 'cp66/guest/UJTEST.SAV').read_bytes()
    assert not json.loads((ROOT / 'test-readback-2/session.json').read_text()).get('finished_utc')
    contains('readback-prompt-2', '\n.')
    contains('reload-odt', '!UJLOAD-I-ODT READY')
    contains('debug-enable-2', '!UJON-I-CP64 DEBUG ENABLED')
    cases = ['native_test_sd_readback', 'odt_reload_and_activation']

    registers('entry-registers', R0='001234', R2='007654', R3='003210',
              R4='125061', R5='030000', R6='177777', R7='001046', PSW='010344')
    contains('fixture', '001074: 001000', '001076: 000000', '001046: 005201  INC R1')
    cases.append('reset_entry_context')
    contains('single-step', 'UJ11 ODT CP66: DEBUG')
    registers('single-step', R0='001234', R1='000001', R2='007654', R3='003210',
              R4='125061', R5='030000', R6='177777', R7='001050', PSW='010340')
    cases.append('single_step_odd_sp')

    contains('breakpoint-set', 'BP 001046 005201')
    contains('breakpoint-hit', 'UJ11 ODT CP66: BREAKPOINT')
    registers('breakpoint-hit', R1='000001', R4='125061', R6='177777', R7='001046')
    contains('breakpoint-resume', '001046: 005201', 'BP 001046', 'UJ11 ODT CP66: BREAKPOINT')
    registers('breakpoint-resume', R1='000002', R4='125061', R6='177777', R7='001046')
    cases.extend(['persistent_breakpoint_odd_sp', 'continue_at_breakpoint_executes_original_once'])
    contains('breakpoint-clear', 'BREAKPOINTS CLEARED', 'P\r\nODT> ')
    cases.append('clear_one_breakpoint')
    contains('run-to', 'UJ11 ODT CP66: RUN TO ADDRESS')
    registers('run-to', R1='000003', R6='177777', R7='001054')
    contains('over-setup', '001054: 001774')
    cases.append('run_to_and_opcode_restore')

    contains('over-nested', 'UJ11 ODT CP66: OVER RETURN')
    registers('over-nested', R0='001235', R1='000003', R2='007655', R3='003211',
              R4='125061', R5='030000', R6='001000', R7='001136')
    contains('over-noncall', 'STEP\r\nUJ11 ODT CP66: DEBUG')
    registers('over-noncall', R0='001235', R2='007655', R3='003211', R6='001000', R7='001140')
    cases.extend(['step_over_nested_jsr', 'step_over_noncall_is_one_step'])
    contains('over-timer', 'UJ11 ODT CP66: OVER RETURN')
    registers('over-timer', R6='001000', R7='001164', PSW='010000')
    contains('timer-check', '001324: 000001')
    cases.append('step_over_wait_kw11l')
    contains('over-disk', 'UJ11 ODT CP66: OVER RETURN')
    registers('over-disk', R6='001000', R7='001332', PSW='010000')
    contains('disk-check', '001462: 000001', '001506: 045125', '001510: 030461', 'P\r\nODT> ')
    cases.append('step_over_rt11_lookup_readw_close')

    contains('cancel-run', 'U 1136\r\nRUN (IRQ ENABLED; RESET TO STOP)')
    registers('cancel-check', R0='003001', R2='007655', R3='003211', R4='125061',
              R5='030000', R6='177777', R7='001054', PSW='010344')
    contains('cancel-patches', '001132: 004767', '001134: 000004', '001136: 000240', 'BP 001132')
    cases.append('reset_cancels_run_and_restores_both_patches')
    contains('exit-setup', 'BREAKPOINTS CLEARED', '001076: 000000 -> 000001')
    contains('return-rt11', '!UJTEST-I-CONTEXT RESUMED', '\n.')
    contains('final-activation', '!UJON-I-CP64 DEBUG ENABLED', '\n.')
    host = json.loads((ROOT / 'host-final.json').read_text())
    assert host['uart_owners'] == [] and host['related_processes'] == []
    for name in ('hg-session.json', 'hg-session-2.json'):
        assert json.loads((ROOT / name).read_text())['stopped']
    cases.extend(['clean_rt11_return_and_code_reverification', 'uart_and_hg_released'])

    # Known setup failures remain in the archive; execution phases must be clean.
    setup = {'probe', 'install-test', 'debug-enable'}
    for session in ROOT.glob('*/session.json'):
        phase = session.parent.name
        if phase not in setup:
            assert b'?' not in raw(phase), ('unexpected error', phase)
    return cases


if __name__ == '__main__':
    cases = verify()
    result = ROOT / 'verification.json'
    if result.exists():
        record = json.loads(result.read_text())
        assert record['passed_cases'] == cases
        for name, expected in record['artifacts_sha256'].items():
            assert hashlib.sha256((ROOT / name).read_bytes()).hexdigest() == expected, name
    print(f'PASS: {len(cases)} recorded physical scenarios; capture integrity verified when manifest present')
