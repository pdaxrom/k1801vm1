#!/usr/bin/env python3
"""Native RT-11 tests: system time/formatter, HG SPFUN and CLOCK UI.

SIMH has no HC1200 GPIO. These tests replace GPIO handshakes/byte I/O and PNLDRV with
explicit fixtures. The host wire framing is tested in host/hg/test_hg_service.c.
"""
import argparse
import json
from pathlib import Path
import re
from board_common import ROOT
from build_hg import build
from rt11_build import Console, build as build_program


def boot(directory):
    log = (directory/'test-console.log').open('wb')
    c = Console(directory/'build.ini', log)
    c.expect(rb'RT-11FB')
    for _ in range(3):
        c.expect(rb'\r\n\.')
    return c, log


def command(c, text):
    c.send(text+'\r')
    # Foreground output / CTRL-B can leave an older KMON prompt buffered.
    # Wait for this command's echo before accepting its completion prompt.
    # The panel fixture prints one line from FG. RT-11 may insert that output
    # between any two echoed BG characters, surrounding it with F>/B> tags.
    fg_line = rb'(?:\r\nF>\r\n[^\r\n]*\r\n\r\nB>\r\n)*'
    echo = c.expect(fg_line.join(re.escape(bytes([ch])) for ch in text.encode('ascii'))+rb'\r\n')
    return echo+c.expect(rb'\r\n\.')


def fixture(mode):
    hg = (ROOT/'demos/rt11/hostdisk/HG.MAC').read_text().replace('166000', '060000')
    date = (1 << 14) | (9 << 10) | (22 << 5) | 22
    ticks = 86390*50
    if mode == 'invalid_date':
        date = 0
    if mode == 'day_limit':
        ticks = 86400*50
    if mode == 'high_ticks':
        ticks = 0xffffffff
    payload = date.to_bytes(2, 'little')+(ticks>>16).to_bytes(2, 'little')+(ticks&65535).to_bytes(2, 'little')
    checksum = sum(payload) ^ (1 if mode == 'checksum' else 0)
    reply = bytes([3 if mode == 'status' else 0])+payload+checksum.to_bytes(2, 'little')
    header = bytes([72,71,1,3,0,50,0,6,0])
    xor = 0
    for value in header:
        xor ^= value
    header += bytes([xor])
    hg = hg.replace('HGXFER:\n', 'HGXFER:\n\tCLR RXPOS\n\tCLR TXPOS\n\tJMP HGHEAD\n')
    start = hg.index('HGTXBY:\n')
    end = hg.index('BLKNUM:', start)
    # The handler is relocated by LOAD: form table addresses relative to PC.
    byte_io = '''HGTXBY:
\tMOV R2,-(SP)
\tMOV PC,R2
TXBASE: ADD #TXDATA-TXBASE,R2
\tADD TXPOS,R2
\tCMPB R0,(R2)
\tBNE TXFAIL
\tINC TXPOS
\tMOV (SP)+,R2
\tCLC
\tRTS PC
TXFAIL: MOV (SP)+,R2
\tSEC
\tRTS PC
HGRXBY:
\tMOV R2,-(SP)
\tMOV PC,R2
RXBASE: ADD #RXDATA-RXBASE,R2
\tADD RXPOS,R2
\tMOVB (R2),R0
\tBIC #177400,R0
\tINC RXPOS
\tMOV (SP)+,R2
\tCLC
\tRTS PC
'''
    if mode == 'timeout':
        byte_io = byte_io.replace('HGRXBY:\n', 'HGRXBY:\n\tSEC\n\tRTS PC\n')
    data = '\t.BYTE '+','.join(str(x)+'.' for x in header)+'\n'
    data += 'RXDATA:\t.BYTE '+','.join(str(x)+'.' for x in reply)+'\n'
    hg = hg[:start]+byte_io+'TXPOS: .WORD 0\nRXPOS: .WORD 0\nTXDATA:\n'+data+'\t.EVEN\n'+hg[end:]
    panel = '''\t.TITLE PNMOCK
\t.MCALL .PRINT
\t.GLOBL PNINIT,PNDISP
\t.PSECT MOCK,I,RO
PNINIT: RTS PC
PNDISP: MOV R0,-(SP)
\tMOV R1,-(SP)
\tMOV R2,-(SP)
\tMOV R3,-(SP)
\tMOV R0,R1
\tMOV #BUFFER,R2
\tMOV #16.,R3
10$: MOVB (R1)+,(R2)+
\tSOB R3,10$
\t.PRINT #BUFFER
\tMOV (SP)+,R3
\tMOV (SP)+,R2
\tMOV (SP)+,R1
\tMOV (SP)+,R0
\tRTS PC
\t.PSECT MOCKD,D,RW
BUFFER: .BLKB 17.
\t.EVEN
\t.END
'''
    return {'demos/rt11/hostdisk/HG.MAC': hg, 'demos/rt11/panel/PNLDRV.MAC': panel}


def integration(out, mode):
    build(out, overrides=fixture(mode))
    c, log = boot(out)
    try:
        command(c, 'SET SL OFF')
        command(c, 'DATE 1-JAN-90')
        command(c, 'TIME 12:34:56')
        for cmd in ('REMOVE HG', 'INSTALL HG', 'LOAD HG'):
            text = command(c, cmd)
            assert b'?' not in text, text
        text = command(c, 'RUN HGTIME')
        if mode != 'success':
            assert b'TIME REQUEST FAILED; SYSTEM TIME UNCHANGED' in text, text
            assert b'12:34:' in command(c, 'TIME')
            assert b'1-Jan-1990' in command(c, 'DATE'), 'failed request changed date'
        else:
            assert b'RT-11 SYSTEM DATE AND TIME SET FROM HOST' in text, text
            assert b'23:59:50 22/09' in text, text
            assert b'23:59:5' in command(c, 'TIME')
            date = command(c, 'DATE')
            assert b'22-Sep-2026' in date, date
            c.send('RUN CLOCK\r')
            c.expect(rb'CLOCK: RT-11 TIME ON HDSP; UART Q OR ESC TO EXIT')
            c.expect(rb'23:59:5[0-9] 22/09')
            c.expect(rb'23:59:5[0-9] 22/09', timeout=5)
            c.send('q')
            c.expect(rb'RT-11 READY'); c.expect(rb'\r\n\.')
            command(c, 'TIME 23:59:59')
            c.send('RUN CLOCK\r'); c.expect(rb'CLOCK: RT-11 TIME ON HDSP')
            c.expect(rb'00:00:0[0-9] 23/09', timeout=5)
            c.send('\x1b'); c.expect(rb'RT-11 READY'); c.expect(rb'\r\n\.')
            foreground(c)
    finally:
        c.close(); log.close()


def foreground(c):
    """Run the actual relocated image under FB, with only the panel mocked.

    Background KMON, disk I/O and HGTIME must make progress while clock output
    continues. This catches a CPU WAIT/busy loop as well as bad relocation and
    terminal input routing. Repeated FRUN/UNLOAD also exercises timer cleanup.
    """
    command(c, 'UNLOAD HG')
    for stop_key in ('q', '\x1b'):
        c.send('FRUN CLOCK\r')
        c.expect(rb'CLOCK: RT-11 TIME ON HDSP; UART Q OR ESC TO EXIT')
        c.expect(rb'\d\d:\d\d:\d\d \d\d/\d\d')
        c.send('\x02')  # CTRL/B: background owns console input
        assert b'?' not in command(c, 'DIR CLOCK.REL'), 'background disk I/O stalled'
        text = command(c, 'LOAD HG')
        assert b'?' not in text, text
        text = command(c, 'RUN HGTIME')
        assert b'RT-11 SYSTEM DATE AND TIME SET FROM HOST' in text, text
        assert b'?' not in command(c, 'UNLOAD HG')
        first = c.expect(rb'\d\d:\d\d:\d\d \d\d/\d\d', timeout=5)
        second = c.expect(rb'\d\d:\d\d:\d\d \d\d/\d\d', timeout=5)
        stamp = rb'\d\d:\d\d:\d\d \d\d/\d\d'
        assert re.findall(stamp, first)[-1] != re.findall(stamp, second)[-1], (first, second)
        c.send('\x06')  # CTRL/F: foreground owns Q/ESC
        c.send(stop_key)
        c.expect(rb'RT-11 READY', timeout=5)
        c.send('\x02')
        assert b'?' not in command(c, 'UNLOAD F')
        assert re.search(rb'\d\d:\d\d:\d\d', command(c, 'TIME'))


def formatter(out):
    out.mkdir(parents=True, exist_ok=True)
    cases = []
    for hz in (50, 60):
        for hour, minute, second in ((0,0,0), (0,0,59), (0,1,0), (9,6,7),
                                      (12,34,56), (18,59,59), (23,59,59)):
            for year, month, day in ((1972,1,1), (2000,2,29), (2026,9,22), (2035,12,31)):
                age = year-1972
                date = ((age>>5)<<14) | (month<<10) | (day<<5) | (age&31)
                ticks = (hour*3600+minute*60+second)*hz
                cases.append(f'\t.WORD {hz}.,{date}.,{ticks>>16}.,{ticks&65535}.\n'
                             f'\t.ASCII "{hour:02}:{minute:02}:{second:02} {day:02}/{month:02}  "\n')
    test = '''\t.TITLE TMTST
\t.MCALL .SDTTM,.PRINT,.EXIT,.DATE
\t.PSECT TEST,I,RO
START: MOV @#54,R0
\tMOV 300(R0),OLD
NEXT: MOV PTR,R4
\tMOV @#54,R0
\tBIC #40,300(R0)
\tCMP (R4)+,#50.
\tBNE 10$
\tBIS #40,300(R0)
10$: MOV R4,PTR
\tJSR PC,TMINIT
\tBCS FAIL
\t.SDTTM #AREA,PTR
\t.DATE
\tCMP R0,@PTR
\tBNE FAIL
\tCMP CLKHZ,#60.
\tBEQ RAW
\tJSR PC,TMGET
\tBR CHECK
; The shipped V5.03 monitor is generated for 50 Hz; changing its flag alone
; does not change the midnight modulus. Test 60 Hz formatting from a snapshot.
RAW: MOV PTR,R4
\tMOV 2(R4),TMTICK
\tMOV 4(R4),TMTICK+2
\tJSR PC,TMFMT
CHECK:
\tMOV PTR,R4
\tADD #6,R4
\tMOV #16.,R1
20$: CMPB (R0)+,(R4)+
\tBNE FAIL
\tSOB R1,20$
\tMOV R4,PTR
\tDEC LEFT
\tBNE NEXT
\t.PRINT #PASS
\tBR DONE
FAIL: .PRINT #ERROR
\t.PRINT #TMTEXT
\tMOV PTR,R0
\tADD #6,R0
\t.PRINT
DONE: MOV @#54,R0
\tMOV OLD,300(R0)
\t.EXIT
\t.PSECT TESTD,D,RW
OLD: .WORD 0
AREA: .BLKW 2
PTR: .WORD CASES
'''+f'LEFT: .WORD {len(cases)}.\n'+'''PASS: .ASCIZ /TMTEST PASS/
ERROR: .ASCIZ /TMTEST FAIL/
\t.EVEN
CASES:
'''+''.join(cases)
    helper = (ROOT/'demos/rt11/panel/TMDRV.MAC').read_text().rsplit('\t.END', 1)[0]
    source = out/'TMTST.MAC'
    source.write_text(test+helper+'\t.END START\n')
    directory = out/'native'
    build_program([source], directory, ROOT/'../lsi11-fpga/images/rt11v503.dsk')
    c, log = boot(directory)
    try:
        text = command(c, 'RUN TMTST')
        assert b'TMTEST PASS' in text and b'TMTEST FAIL' not in text, text
    finally:
        c.close(); log.close()
    return len(cases)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    if out.exists():
        raise ValueError('Use a fresh test directory')
    count = formatter(out/'formatter')
    print('Native RT-11: date/time and formatter passed', count, 'cases', flush=True)
    integration(out/'success', 'success')
    print('Native RT-11: HG SPFUN, system time, CLOCK RUN/FRUN and Q/ESC passed', flush=True)
    modes = ('status', 'checksum', 'timeout', 'invalid_date', 'day_limit', 'high_ticks')
    for mode in modes:
        integration(out/mode, mode)
    (out/'result.json').write_text(json.dumps(dict(formatter_cases=count,
        system_time=True, failure_modes=list(modes), failure_preserves_time=True,
        clock_uart_exit=True, midnight_date_rollover=True,
        clock_foreground=True, foreground_background_commands=True,
        foreground_background_program=True, foreground_unload_restart=True,
        gpio='handshake and byte I/O mocked; actual HG framing/checksum and RT-11 requests tested'), indent=2)+'\n')
    print('Native RT-11: failed HG request preserves system date/time')
