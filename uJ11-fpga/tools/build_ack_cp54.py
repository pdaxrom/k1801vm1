#!/usr/bin/env python3
"""CP54: factor native I/O/DMA qualification and immediate ACK from frozen CP53a."""
import hashlib
import json
import re
import tarfile
from board_common import ROOT, CORE, BOARD
from build_fram_cp52 import replace_once

OUT=ROOT/'build/cp54-ack'
VARIANTS=('dma','dma-ack')


def build():
    OUT.mkdir(parents=True,exist_ok=True)
    manifest=json.loads((ROOT/'synth/reports/cp53a/inputs.json').read_text())
    names={'uj11_board_bus.v':'build/cp53-cursor/baseline/uj11_board_bus.v',
           'uj11_board_fram.v':'build/cp53-cursor/increment/uj11_board_fram.v'}
    frozen={}
    with tarfile.open(ROOT/'synth/reports/cp53a/source.tgz') as archive:
        for name,path in names.items():
            data=archive.extractfile(path).read()
            assert hashlib.sha256(data).hexdigest()==manifest['files'][path],path
            frozen[name]=data.decode()
    baseline=OUT/'baseline';baseline.mkdir(exist_ok=True)
    for name,text in frozen.items():(baseline/name).write_text(text)
    base=frozen['uj11_board_bus.v']
    old=re.search(r'\twire service_dma_selected =.*?;',base,re.S)[0]
    factored='''	// Direction and RK state do not depend on the decoded address region.
	wire service_dma_operand = RK_SERVICE_ENABLE && rk_service_active &&
		rk_service_movb && ((!rk_write_command && write) ||
		(rk_write_command && !write && !instruction_fetch));
	wire service_dma_selected = io_page && service_dma_operand;'''
    dma=replace_once(base,old,factored)
    dma=replace_once(dma,'wire cpu_io_page = io_page && !service_dma_selected;',
        'wire cpu_io_page = io_page && !service_dma_operand;')
    combined=replace_once(dma,'\twire uart_strobe =', '''	// Compute the small, zero-wait responses before the common I/O/DMA gate.
	wire immediate_rk_response = RK_SERVICE_ENABLE &&
		(word_address[12:1] == RK_DS[12:1] ||
		 (word_address[12:1] == RK_CS1[12:1] && !write &&
		  (!rk_cs1_initialized || rk_immediate_done)));
	wire immediate_io_response = word_address[12:1] == LTC_CSR[12:1] ||
		word_address[12:1] == PANEL_BASE[12:1] ||
		word_address[12:0] == 13'o17750 || immediate_rk_response;
	wire uart_strobe =''')
    ack=re.search(r'\tassign acknowledge =.*?;',combined,re.S)[0]
    combined=replace_once(combined,ack, '''	assign acknowledge = uart_ack || (sd_ready && !sd_error) ||
		boot_ack || boot_program_ack || (fram_ready && !fram_error) ||
		(request && cpu_io_page && immediate_io_response);''')
    outputs={}
    for variant,text in [('baseline',base),('dma',dma),('dma-ack',combined)]:
        # Changes are only in the combinational decode and (for dma-ack) ACK.
        # State, device instances, data mux and all state consumers stay exact.
        assert text[text.index('\tassign virq'):]==base[base.index('\tassign virq'):]
        start='\twire uart_strobe =';end='\tassign acknowledge'
        assert text[text.index(start):text.index(end)]==base[base.index(start):base.index(end)]
        folder=OUT/variant;folder.mkdir(exist_ok=True)
        path=folder/'uj11_board_bus.v';path.write_text(text)
        outputs[str(path.relative_to(ROOT))]=hashlib.sha256(path.read_bytes()).hexdigest()
    path=baseline/'uj11_board_fram.v';outputs[str(path.relative_to(ROOT))]=hashlib.sha256(path.read_bytes()).hexdigest()
    inputs=['tools/build_ack_cp54.py','tools/build_fram_cp52.py','tools/board_common.py',
        'synth/reports/cp53a/inputs.json','synth/reports/cp53a/source.tgz']
    record=dict(reference='cp53a',variants=VARIANTS,mmu=False,extra_state_bits=0,
        inputs={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},outputs=outputs)
    (OUT/'inputs.json').write_text(json.dumps(record,indent=2)+'\n')
    return record


def adapt(variant):
    assert variant in VARIANTS
    build()
    return CORE.copy(),[str((OUT/(variant if p.endswith('uj11_board_bus.v') else 'baseline')/p.rsplit('/',1)[-1]).relative_to(ROOT))
        if p in ('boards/hc1200/uj11_board_bus.v','boards/hc1200/uj11_board_fram.v') else p for p in BOARD]


if __name__=='__main__':print(json.dumps(build(),indent=2))
