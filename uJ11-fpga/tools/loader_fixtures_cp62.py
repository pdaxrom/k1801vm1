#!/usr/bin/env python3
"""Independent disk fixtures and expected upper-bank contents for CP62."""
import json
import struct
from pathlib import Path
from board_common import ROOT
from service_image_cp62 import pack, decode, SLOTS

OUT = ROOT/'build/cp62-loader'


def put(memory, address, value): memory[address:address+2] = struct.pack('<H', value)


def fixture(kind, *, fault=False, size=1026, memory=2048):
    base = SLOTS[kind][0]
    payload = bytearray((i*17+31) & 255 for i in range(size))
    # INC @#marker; START. Fault variant: TST @#1; START (never reached).
    marker=min(base+size,0o157770)
    code = [0o005737 if fault else 0o005237, 1 if fault else marker,
            0o177, (0o166-base-8)&65535, 0o005237, marker+2, 0o177, (0o166-base-16)&65535]
    if marker<base+size:
        put(payload,marker-base,0);put(payload,marker-base+2,0)
    for i, value in enumerate(code): put(payload, 2*i, value)
    return pack(payload, kind, memory, base, base+8)


def files():
    odt = fixture(1); fp = fixture(2, size=514, memory=49152)
    result = {'ODT': odt, 'FP11': fp, 'ODTFLT': fixture(1, fault=True),
              'BIGFP': fixture(2, size=49148, memory=49152)}
    def header(name, index, value, checksum=True):
        image = bytearray(odt); put(image, 2*index, value)
        if checksum:
            put(image, 22, 0); put(image, 22, -sum(struct.unpack('<16H', image[:32])) & 65535)
        result[name] = bytes(image)
    header('HDBAD', 0, 0); header('HDABI', 3, 1); header('HDTYPE', 4, 3)
    header('HDADDR', 5, 0o10002); header('HDODD', 6, 1025)
    header('HDSIZE', 7, 0o40000); header('HDENT', 8, 0o160000)
    header('HDRSV', 12, 1); header('HDSUM', 11, 1, False)
    result['HDPAD'] = odt[:510]+b'\x01\x00'+odt[512:]
    result['SHORT'] = odt[:-512]
    result['EXTRA'] = odt+bytes(512)
    result['CKBAD'] = odt[:600]+bytes([odt[600]^1])+odt[601:]
    result['PADBAD'] = odt[:-2]+b'\x01\x00'
    write_fail = bytearray(odt[512:512+1026]); write_fail[0] ^= 4
    result['WRFAIL'] = pack(write_fail, 1, 2048, 0o10000, 0o10010)
    OUT.mkdir(parents=True, exist_ok=True)
    for name, image in result.items(): (OUT/(name+'.BIN')).write_bytes(image)
    return result


def install(memory, image, other_ready):
    h = decode(image); base = h['base']; size = h['payload_bytes']
    memory[base:base+size] = image[512:512+size]
    memory[base+size:base+h['memory_bytes']] = bytes(h['memory_bytes']-size)
    wrap = 0o2000 if h['kind']==1 else 0o2020
    for i,v in enumerate([0o12737,h['fault'],4,0o137,h['entry']]): put(memory,wrap+2*i,v)


def cold(memory):
    memory[0o200:0o444]=(ROOT/'firmware/cp62/resident.bin').read_bytes()
    words=[int(w,16) for w in (ROOT/'build/cp62-boot/firmware.mem').read_text().split()]
    memory[0o4000:0o4652]=struct.pack('<213H',*words[:213])
    for a,v in [(4,0o312),(6,0o340),(0o170,0o200),(0o172,0o340),(0o160,0o422),(0o776,0o402)]:put(memory,a,v)


def helper(memory, ready):
    blob=(ROOT/'firmware/cp62/loader.bin').read_bytes()
    memory[0o1000:0o1000+len(blob)]=blob
    if not ready:
        for a,v in [(0o320,0o177),(0o322,0o177640),(0o326,0o777),(0o166,0o1700),(0o10,0o2020),(0o12,0o340)]:put(memory,a,v)
    put(memory,4,0o312)
    put(memory,0o164,0o2000 if ready&1 else 0o326)


def generate_cases(images):
    memory = bytearray([0xa5]*65536); cold(memory); ready = 0; lines = []; records = []
    def check(command, want, action=None, injection=0):
        nonlocal ready
        n = len(records)+1
        helper(memory,ready)
        if action: action()
        put(memory,0o164,0o2000 if want&1 else 0o326)
        expected = OUT/f'expected-{n}.hex'
        expected.write_text(''.join(f'{b:02x}\n' for b in memory))
        records.append(dict(case=n,command=command,ready=want,injection=injection))
        lines.append(f'run_case({n},"{command}",{want},{injection});\ncheck_upper("build/cp62-loader/expected-{n}.hex");')
        ready = want
    def success(name):
        kind = decode(images[name])['kind']; other=ready & ~kind
        check(name, ready|kind, lambda:install(memory,images[name],other))
    check('STATUS',0); check('NOSUCH',0)
    success('ODT'); success('FP11'); check('STATUS',3)
    check('ODT',3,lambda:memory.__setitem__(0o166,memory[0o166]^1),5)
    check('OFFODT',2);check('OFFFP',0);success('ODT');success('FP11')
    for name in ('HDBAD','HDABI','HDTYPE','HDADDR','HDODD','HDSIZE','HDENT','HDRSV','HDSUM','HDPAD','SHORT','EXTRA'):
        check(name,3)
    success('ODT')
    # Failed checksum/padding copied the payload, but have not cleared BSS
    # or published wrappers. The other module must remain intact.
    for name in ('CKBAD','PADBAD'):
        image=images[name]
        check(name,2,lambda image=image:memory.__setitem__(slice(0o10000,0o10000+1026),image[512:512+1026]))
        success('ODT')
    check('ODT',2,injection=1)  # SD .READW fails before payload transfer
    success('ODT')
    check('WRFAIL',2,injection=2)  # Upper-bank writes protected; lower RAM remains writable
    success('ODT')
    def late_corruption():
        install(memory,images['ODT'],2)
        memory[0o10000] ^= 1
    check('ODT',2,late_corruption,3)
    success('ODT')
    success('ODTFLT')
    # Guest test invokes both services. Compare every upper byte except
    # the hardware-owned CPC/CPSW, checked by the existing CP59 tests.
    put(memory,0o10000+1026+2,1); put(memory,0o40000+514,1)
    put(memory,4,0o312)
    check('@UJCHEK',3)
    check('OFFODT',2); check('OFFFP',0)
    success('ODT'); success('BIGFP')
    check('STATUS',3)
    # CTRL/C while replacing FP11: expected partial target bytes depend on
    # interrupt latency; all other addresses are checked against a snapshot.
    lines.append('run_interrupted();')
    # Reinstall the full FP slot to overwrite all partial bytes deterministically.
    ready=1; success('FP11')
    check('STATUS',3)
    cold(memory)
    (OUT/'cold.hex').write_text(''.join(f'{b:02x}\n' for b in memory))
    lines.append('reset_persistent();')
    (OUT/'loader_cases.vh').write_text('\n'.join(lines)+'\n')
    (OUT/'cases.json').write_text(json.dumps(records,indent=2)+'\n')
    return records


if __name__=='__main__': generate_cases(files())
