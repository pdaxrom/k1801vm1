#!/usr/bin/env python3
"""Run the existing architectural/bus oracles through the synchronous decoder."""
import argparse
import subprocess
from board_common import ROOT,CORE
MEMORY=['tb/uj11_ram.v','rtl/uj11_stream.v','rtl/uj11_prefetch_control.v','rtl/uj11_prefetch.v',
        'rtl/uj11_fram_baseline.v','rtl/uj11_fram_transport.v','reference/lsi11/spi_fram_model.v',
        'reference/lsi11/spi_fram_guest_ram.v']

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--suite',choices=('integer','fis'),default='integer');args=p.parse_args()
    if args.suite=='fis':
        from run_fis_tests import compile_test,execute
        # A build-only concatenation supplies the optional decoder modules;
        # the original source list and original fixtures remain unchanged.
        joined=ROOT/'build/cp28-core-sync.v'
        joined.write_text('\n'.join((ROOT/s).read_text() for s in
            ['rtl/uj11_core.v','rtl/uj11_decode_rom.v','microcode/generated/uj11_decode_table.v']))
        tb=ROOT/'build/cp28-tb_fis.v'
        tb.write_text((ROOT/'tb/tb_fis.v').read_text().replace('ROM_DECODE=0','ROM_DECODE=1',1))
        tag='cp28-fis-sync'
        command=compile_test(-1,'verilator',False,tag=tag,replacements={'rtl/uj11_core.v':str(joined),'tb/tb_fis.v':str(tb)})
        execute(command,tag,'build/fis-vectors.txt')
        print((ROOT/'build'/f'{tag}.log').read_text());return
    cases=[('core',{}),('ea',{'SUITE':0}),('ea',{'SUITE':9}),
           ('trace_bit',{}),('bus_fault',{})]
    for top,parameters in cases:
        name='tb_'+top
        tag='cp28-sync-'+top+''.join('-'+str(v) for v in parameters.values())
        flags=[f'-P{name}.ROM_DECODE=1']+[f'-P{name}.{k}={v}' for k,v in parameters.items()]
        subprocess.run(['iverilog','-g2012','-s',name,'-o','build/'+tag]+flags+
                       ['tb/'+name+'.v']+CORE+MEMORY+['rtl/uj11_rom.v'],cwd=ROOT,check=True)
        with (ROOT/'build'/f'{tag}.log').open('w') as log:
            subprocess.run(['vvp','build/'+tag],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        print((ROOT/'build'/f'{tag}.log').read_text(),flush=True)
if __name__=='__main__':main()
