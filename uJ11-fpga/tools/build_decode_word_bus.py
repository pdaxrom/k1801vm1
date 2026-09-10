#!/usr/bin/env python3
"""CP36: move optional byte-lane alignment after the opcode-ROM input fork."""
import hashlib
import json
import tarfile
from board_common import ROOT


def change(source, before, after):
    assert source.count(before)==1, before
    return source.replace(before, after)


def adapt_core(source):
    source = change(source, 'ROM_DECODE=0, IRQ_VECTOR_BITS=8',
                    'ROM_DECODE=0, ALIGNED_WORD_READS=0, IRQ_VECTOR_BITS=8')
    source = change(source, '    wire [9:0] dispatch_address;', '''    wire [9:0] dispatch_address;
    // A board may return an aligned word for every read. FETCH always reads
    // a word; keep its ROM input ahead of the operand-only byte-lane mux.
    // The default preserves the established right-justified core interface.
    wire [15:0] operand_read_data = (ALIGNED_WORD_READS!=0 && mem_byte) ?
        (mem_addr[0] ? {8'b0,mem_read_data[15:8]} : {8'b0,mem_read_data[7:0]}) : mem_read_data;''')
    source = change(source, '.mem_read_data(mem_read_data),', '.mem_read_data(operand_read_data),')
    return source


def adapt_board(source):
    source = change(source, '    assign rdata=byte_access ? (address[0] ? {8\'b0,lane_rdata[15:8]} : {8\'b0,lane_rdata[7:0]}) : lane_rdata;',
                    '    assign rdata=lane_rdata; // operand byte lanes are aligned inside the CPU')
    return change(source, 'uj11_core #(.ROM_DECODE(1),',
                  'uj11_core #(.ROM_DECODE(1),.ALIGNED_WORD_READS(1),')


def main():
    out = ROOT/'build/cp36-word-bus'
    out.mkdir(parents=True, exist_ok=True)
    # Reproduce the experiment even after its changes enter production.
    paths = ['synth/reports/cp31c/source.tgz','synth/reports/cp35c/source.tgz',
             'synth/reports/cp31c/inputs.json','synth/reports/cp35c/inputs.json',
             'tools/build_decode_word_bus.py']
    hashes = {p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths}
    def frozen(checkpoint, path):
        folder=ROOT/'synth/reports'/checkpoint
        expected=json.loads((folder/'inputs.json').read_text())['files'][path]
        with tarfile.open(folder/'source.tgz') as archive:
            data=archive.extractfile(path).read()
        assert hashlib.sha256(data).hexdigest()==expected
        return data.decode()
    (out/'uj11_core.v').write_text(adapt_core(frozen('cp31c','rtl/uj11_core.v')))
    (out/'uj11_board.v').write_text(adapt_board(frozen('cp31c','boards/hc1200/uj11_board.v')))
    context = out/'context'
    context.mkdir(exist_ok=True)
    (context/'uj11_core.v').write_text(adapt_core(frozen('cp35c','build/cp35/uj11_core.v')))
    outputs = [out/'uj11_core.v',out/'uj11_board.v',context/'uj11_core.v']
    (out/'inputs.json').write_text(json.dumps(dict(inputs_sha256=hashes,
        outputs_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in outputs}),indent=2)+'\n')


if __name__=='__main__':
    main()
