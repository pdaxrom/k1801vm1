#!/usr/bin/env python3
"""CP55: share receive/high-result storage on the measured native CP54b board."""
import hashlib
import json
import tarfile
from board_common import ROOT, CORE, BOARD
from build_fram_cp52 import replace_once

OUT=ROOT/'build/cp55-rx'


def build():
    OUT.mkdir(parents=True,exist_ok=True)
    manifest=json.loads((ROOT/'synth/reports/cp54b/inputs.json').read_text())
    names={'uj11_board_bus.v':'build/cp54-ack/dma-ack/uj11_board_bus.v',
           'uj11_board_fram.v':'build/cp54-ack/baseline/uj11_board_fram.v'}
    frozen={}
    with tarfile.open(ROOT/'synth/reports/cp54b/source.tgz') as archive:
        for name,path in names.items():
            data=archive.extractfile(path).read()
            assert hashlib.sha256(data).hexdigest()==manifest['files'][path],path
            frozen[name]=data.decode()
    baseline=OUT/'baseline';baseline.mkdir(exist_ok=True)
    for name,text in frozen.items():(baseline/name).write_text(text)
    text=frozen['uj11_board_fram.v']
    text=replace_once(text,'    reg [7:0] rx;', '''    // Full rdata is valid at ready, DONE and IDLE; low byte agrees always.
    // During busy the high byte is the serial receive shift register.
    wire [7:0] rx=rdata[15:8];''')
    text=replace_once(text,'tx<=0;rx<=0;bit_count<=0;','tx<=0;bit_count<=0;')
    text=replace_once(text,'if(!spi_sck)rx<={rx[6:0],spi_miso};',
        'if(!spi_sck)rdata[15:8]<={rx[6:0],spi_miso};')
    text=replace_once(text,'                        if(state==DATA_HI)rdata[15:8]<=rx;\n','')
    candidate=OUT/'shared';candidate.mkdir(exist_ok=True)
    path=candidate/'uj11_board_fram.v';path.write_text(text)
    inputs=['tools/build_rx_cp55.py','tools/build_fram_cp52.py','tools/board_common.py',
        'synth/reports/cp54b/inputs.json','synth/reports/cp54b/source.tgz']
    outputs=[str(p.relative_to(ROOT)) for p in [baseline/n for n in names]+[path]]
    record=dict(reference='cp54b',mmu=False,removed_rtl_state_bits=8,
        data_contract='SPI/ready/error/busy and low data every cycle; full data at ready or state IDLE/DONE; busy high byte may change',
        inputs={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},
        outputs={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in outputs})
    (OUT/'inputs.json').write_text(json.dumps(record,indent=2)+'\n')
    return record


def adapt():
    build()
    return CORE.copy(),[str((OUT/('baseline' if p.endswith('uj11_board_bus.v') else 'shared')/p.rsplit('/',1)[-1]).relative_to(ROOT))
        if p in ('boards/hc1200/uj11_board_bus.v','boards/hc1200/uj11_board_fram.v') else p for p in BOARD]


if __name__=='__main__':print(json.dumps(build(),indent=2))
