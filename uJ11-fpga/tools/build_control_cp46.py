#!/usr/bin/env python3
"""CP46 control/physical-region alternatives, preserving CP45 native inputs."""
import hashlib
import json
from pathlib import Path
from board_common import ROOT
from build_mmu_entry import change
from build_bus_cp45 import adapt as prior_adapt

VARIANTS = dict(baseline=(False, False, False), classified=(True, False, False),
                phase=(False, True, False), apr=(False, False, True), combined=(True, True, True))
OUT = ROOT/'build/cp46-control'


def adapt(core, board, variant):
    core, board = prior_adapt(core, board, 'narrow-rom')
    names = {'uj11_board.v', 'uj11_board_bus.v', 'uj11_mmu_relocate.v', 'uj11_mmu_apr_shared.v'}
    def path(p): return f'build/cp46-control/{variant}/'+Path(p).name if Path(p).name in names else p
    return [path(p) for p in core], [path(p) for p in board]


def main():
    paths = ['build/cp44-direct/uj11_board.v', 'build/cp45-bus/narrow-rom/uj11_board_bus.v',
             'rtl/experimental/uj11_mmu_relocate.v', 'build/cp39-csr/uj11_mmu_apr_shared.v']
    frozen = json.loads((ROOT/'synth/reports/cp45k/inputs.json').read_text())['files']
    for p in paths: assert hashlib.sha256((ROOT/p).read_bytes()).hexdigest() == frozen[p], p
    base_board, base_bus, base_bridge, base_apr = [(ROOT/p).read_text() for p in paths]
    outputs = {}
    for variant, (classified, phase, apr) in VARIANTS.items():
        board, bus, bridge, shared = base_board, base_bus, base_bridge, base_apr
        if classified:
            board = change(board, 'wire mmu_enabled, map22, mmu_bypass;',
                           'wire mmu_enabled, map22, mmu_bypass, ram_region, io_region;')
            board = change(board, '.ram_region(),.io_region(),', '.ram_region(ram_region),.io_region(io_region),')
            board = change(board, '.byte_select(lanes),.address(address),',
                           '.ram_region(ram_region),.io_region(io_region),.byte_select(lanes),.address(address[16:0]),')
            bus = change(bus, 'input  wire [21:0] address,', 'input wire ram_region, io_region,\n    input wire [16:0] address,')
            bus = bus.replace('address[21:16]==0', 'ram_region && !address[16]')
            bus = change(bus, 'wire io_page = &address[21:13];', 'wire io_page = io_region;')
            bus = change(bus, '.physical_address(address)', ".physical_address({6'b111111,address[15:0]})")
            bus = change(bus, 'address[21:17]==0 && !boot_selected', 'ram_region && !boot_selected')
            bus = change(bus, 'boot_release_armed && request && !write && address == 0',
                         'boot_release_armed && request && !write && ram_region && address == 0')
        if phase:
            a = bridge.index('    always @(posedge clk)begin')
            bridge = bridge[:a]+'''    wire held=request && !acknowledge;
    always @(posedge clk)begin
        if(reset)phase<=IDLE;
        else begin
            phase[1] <= phase==CAPTURE || (held && (phase[1] || (phase==IDLE && !enabled)));
            phase[0] <= (held && phase==BYPASS) ||
                        (starting && (enabled ? lookup_grant : !acknowledge));
        end
    end
    always @(posedge clk)if(!reset && phase==CAPTURE)begin
        block_address<={(map22 ? block_sum[15:12] : {4{&block_sum[11:7]}}),block_sum[11:0]};
        mapped_ram<=!block_sum[11] && (!map22 || !(|block_sum[15:12]));
        mapped_io<=(&block_sum[11:7]) && (!map22 || (&block_sum[15:12]));
    end
endmodule
'''
        if apr:
            # During lookup_grant phase==READ, so both original lane enables
            # already equal zero. EBR address is observable only when enabled.
            shared = change(shared, '.address(lookup_grant ? lookup_address : address),',
                            '.address((lookup_address & {7{lookup_grant}}) | (address & {7{memory_enable}})),')
            shared = change(shared, '.write_enable(lookup_grant ? 2\'b00 : lanes)', '.write_enable(lanes)')
        folder = OUT/variant; folder.mkdir(parents=True, exist_ok=True)
        for name, text in [('uj11_board.v', board), ('uj11_board_bus.v', bus),
                           ('uj11_mmu_relocate.v', bridge), ('uj11_mmu_apr_shared.v', shared)]:
            path = folder/name; path.write_text(text)
            outputs[str(path.relative_to(ROOT))] = hashlib.sha256(text.encode()).hexdigest()
    inputs = paths+['tools/build_control_cp46.py', 'tools/build_bus_cp45.py',
                   'tools/build_mmu_entry.py', 'synth/reports/cp45k/inputs.json']
    (OUT/'inputs.json').write_text(json.dumps(dict(variants=VARIANTS,
        inputs_sha256={p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},
        outputs_sha256=outputs), indent=2)+'\n')
    print('PASS CP46 build:', len(VARIANTS), 'candidates; unchanged production/CP45 inputs')


if __name__ == '__main__': main()
