#!/usr/bin/env python3
"""Inventory native HC1200 resources from verified MAP/TRACE and current images."""
import json
import re
import shutil
import tarfile
from board_common import ROOT, CORE, BOARD
from record_cp33 import digest, synthesis


def main():
    measured=synthesis('cp40h')  # checks raw reports and every archived source
    folder=ROOT/'synth/reports/cp40h'
    mapped=(folder/'design.mrp').read_text()
    timing=(folder/'design.twr').read_text()
    area=(folder/'design.areasrr').read_text()
    cp50=json.loads((ROOT/'docs/verification-cp50.json').read_text())
    inputs=set(CORE+BOARD+['boards/hc1200/uj11_microcomp.v','microcode/generated/uj11_m0_ebr.v',
        'microcode/generated/m0.mem','microcode/generated/decode.mem','microcode/generated/firmware.mem'])
    for path in inputs:assert digest(ROOT/path)==cp50['sources_sha256'][path],path
    assert cp50['source_comparisons']==96 and cp50['default_mmu'] is False
    inventory={}
    for name,pattern in {
        'lut4':r'Number of LUT4s:\s+(\d+) out of\s+(\d+)',
        'slices':r'Number of SLICEs:\s+(\d+) out of\s+(\d+)',
        'pfu_ff':r'PFU registers:\s+(\d+) out of\s+(\d+)',
        'pio_ff':r'PIO registers:\s+(\d+) out of\s+(\d+)',
        'ebr':r'Number of block RAMs:\s+(\d+) out of\s+(\d+)',
        'pll':r'Number of PLLs:\s+(\d+) out of\s+(\d+)',
    }.items():
        used,total=map(int,re.search(pattern,mapped).groups())
        inventory[name]=dict(used=used,total=total,remaining=total-used,used_percent=100*used/total)
    pio=re.search(r'Number of PIO sites used: (\d+) \+ (\d+)\(JTAGENB\) out of (\d+)',mapped)
    ordinary,jtag,total=map(int,pio.groups())
    inventory['pins']=dict(ordinary=ordinary,jtagenb=jtag,total=total,remaining=total-ordinary-jtag)
    assert inventory['pins']['remaining']==0
    inventory['lut_categories']={name:int(re.search(pattern,mapped)[1]) for name,pattern in {
        'logic':r'Number used as logic LUTs:\s+(\d+)',
        'distributed_ram':r'Number used as distributed RAM:\s+(\d+)',
        'carry':r'Number used as ripple logic:\s+(\d+)',
    }.items()}
    assert sum(inventory['lut_categories'].values())==inventory['lut4']['used']
    cells=[]
    for part in area.split('Report for cell ')[1:]:
        path=re.search(r'Instance path:\s+(\S+)',part)
        if not path:continue
        counts={n:int(c) for n,c in re.findall(r'^\s+(\w+)\s+(\d+)\s+[\d.]+\s*$',part.split('SUB MODULES')[0],re.M)}
        cells.append(dict(path=path[1],orcalut4=counts.get('ORCALUT4',0),pfumx=counts.get('PFUMX',0),
            ccu2d=counts.get('CCU2D',0),ebr=counts.get('DP8KC',0),
            register_primitives=sum(c for n,c in counts.items() if n.startswith(('FD','IFS','OFS')))))
    used={int(line.split()[0],16) for line in (ROOT/'microcode/generated/m0.lst').read_text().splitlines()}
    stats=json.loads((ROOT/'microcode/generated/m0.stats.json').read_text())
    assert len(used)==stats['used_words']==954
    runs=[]
    for address in sorted(set(range(1024))-used):
        if runs and runs[-1][-1]+1==address:runs[-1].append(address)
        else:runs.append([address])
    free_microcode=dict(words=1024-len(used),largest_run=max(map(len,runs)),
        spans=[dict(start=f'{r[0]:03x}',end=f'{r[-1]:03x}',words=len(r)) for r in runs])
    critical=re.search(r'Delay:\s+([\d.]+)ns\s+\(([\d.]+)% logic, ([\d.]+)% route\), (\d+) logic levels',timing)
    bench=json.loads((ROOT/'build/cp51-bench/result.json').read_text())
    for path,h in bench['sources_sha256'].items():assert digest(ROOT/path)==h,path
    inputs.update(bench['sources_sha256'])
    inputs.update(['tools/audit_resources_cp51.py','tools/record_cp33.py','tools/board_common.py',
        'docs/verification-cp50.json','microcode/generated/m0.lst','microcode/generated/m0.stats.json',
        'microcode/generated/firmware.json'])
    inputs.update(str(p.relative_to(ROOT)) for p in folder.iterdir() if p.is_file())
    out=ROOT/'tb/reports/cp51';out.mkdir(parents=True,exist_ok=True)
    for name in ('build.log','run.log','result.json'):
        shutil.copyfile(ROOT/'build/cp51-bench'/name,out/name)
    with tarfile.open(out/'test-sources.tgz','w:gz') as archive:
        for path in sorted(inputs):
            if not path.startswith(('synth/reports/','docs/')):archive.add(ROOT/path,arcname=path)
    result=dict(checkpoint='CP51: MMU-less resource and performance inventory',date='2026-09-11',
        measured_gate='cp40h',measurements=measured,current_rtl_matches_cp50=True,new_synthesis=False,
        resources=inventory,hierarchical_primitives=cells,hierarchical_counts_are_not_additive_map_costs=True,
        microcode=free_microcode,ebr_allocation=dict(microcode=4,opcode_dispatch=1,firmware=1,free=1),
        critical_path=dict(delay_ns=float(critical[1]),logic_percent=float(critical[2]),route_percent=float(critical[3]),
            levels=int(critical[4]),path='microstore -> register select/RF -> address -> board decode/ACK -> fault/sequence -> microstore'),
        benchmarks=bench,physical_board='CP29a',mmu_development_deferred=True,
        sources_sha256={p:digest(ROOT/p) for p in sorted(inputs)},
        archived_tests_sha256={str(p.relative_to(ROOT)):digest(p) for p in sorted(out.iterdir())})
    (ROOT/'docs/resources-cp51.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS CP51 resource inventory: verified CP40h MAP/TRACE, CP50 source identity, full native board benchmarks')
    print(json.dumps(dict(resources=inventory,microcode=free_microcode,benchmarks=[
        {k:r[k] for k in ('workload','cpi','fram_busy_percent')} for r in bench['workloads']]),indent=2))


if __name__=='__main__':main()
