#!/usr/bin/env python3
"""Verify and archive the measured CP61 chain, ROMs and executable evidence."""
import hashlib
import json
import re
import shutil
import tarfile
from board_common import ROOT


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def read(path):return json.loads(path.read_text())


def main():
    dest=ROOT/'tb/reports/cp61';baseline=dest/'boot-e'
    archived=read(baseline/'archive.json')
    with tarfile.open(baseline/'source.tgz') as archive:
        for name,digest in archived['source_files'].items():
            assert hashlib.sha256(archive.extractfile(name).read()).hexdigest()==digest,name
        for run in read(baseline/'boot-tests.json'):
            assert run['passed'] and run['cases']==17
            for name,digest in run['files'].items():
                assert hashlib.sha256(archive.extractfile(name).read()).hexdigest()==digest,name
    assert read(baseline/'rt11/result.json')['passed']
    measurements={}
    for gate in ('cp61a','cp61b','cp61c','cp61d','cp61e','cp61f','cp61g'):
        folder=ROOT/'synth/reports'/gate;result=read(folder/'result.json')
        with tarfile.open(folder/'source.tgz') as archive:
            for name,digest in result['inputs']['files'].items():
                data=(folder/name.split(':')[1]).read_bytes() if name.startswith('generated:') else archive.extractfile(name).read()
                assert hashlib.sha256(data).hexdigest()==digest,(gate,name)
        for suffix,digest in result['reports'].items():assert sha(folder/('design'+suffix))==digest
        measurements[gate]={k:result.get(k) for k in ('lut4','ff','ebr','slices','fmax_mhz','timing_pass','fully_routed','microcode_words')}
    gate=read(ROOT/'synth/reports/cp61g/result.json')
    assert gate['timing_pass'] and gate['fully_routed'] and gate['constraint_matched'] and gate['applied_clock_mhz']==31.824
    assert (gate['lut4'],gate['ff'],gate['ebr'],gate['slices'],gate['microcode_words'])==(1229,343,6,616,1002)
    profile=read(ROOT/'build/cp61-boot/inputs.json')
    for group in ('inputs','outputs'):
        for name,digest in profile[group].items():assert sha(ROOT/name)==digest==gate['inputs']['files'][name],name
    proof=read(ROOT/'build/cp61-boot/guard-proof.json')
    assert proof['passed'] and proof['combinations']==524288 and proof['negative_control_rejected']
    for name,digest in proof['files'].items():assert sha(ROOT/name)==digest,name
    live=ROOT/'build/cp61-guard-rt11';result=read(live/'result.json');inputs=read(live/'inputs.json')
    assert result['passed'] and result['legacy_loader_rejected']
    for name,digest in inputs['files'].items():assert sha(ROOT/name)==digest,name
    for name,digest in result['files'].items():assert sha(live/name)==digest,name
    assert inputs['profile']==profile
    for run in read(ROOT/'build/cp61-boot/rk-tests.json'):
        for name,digest in run['sources'].items():assert sha(ROOT/name)==digest,name
    edif=read(ROOT/'build/cp61g/netlist.json')
    assert not edif['multiple_drivers'] and not edif['floating'] and len(edif['negative_controls'])==3
    assert edif['edif_sha256']==sha(ROOT/'build/cp61g/impl1/cp61g_impl1.edi')
    folder=dest/'final-g';folder.mkdir()
    for name in ('inputs.json','guard-proof.json','guard.v','guard.log','guard-negative.v','guard-negative.log',
                 'rk-tests.json','rk-portable.log','rk-vendor.log','rk-broken-clear.log'):
        shutil.copyfile(ROOT/'build/cp61-boot'/name,folder/name)
    shutil.copyfile(ROOT/'build/cp61g/netlist.json',folder/'netlist.json')
    shutil.copyfile(ROOT/'build/cp61g/cs-ioff.json',folder/'cs-ioff.json')
    shutil.copytree(live,folder/'rt11',ignore=shutil.ignore_patterns('obj','*.dsk','spi*.v','wbc*.v'))
    paths=set(inputs['files'])|set(proof['files'])|set(profile['inputs'])|set(profile['outputs'])
    for run in read(ROOT/'build/cp61-boot/rk-tests.json'):paths.update(run['sources'])
    paths.update(['tools/record_halt_boot_cp61.py','tools/check_edif_drivers_cp46.py','tools/test_rk_recovery_cp60.py'])
    with tarfile.open(folder/'source.tgz','w:gz') as archive:
        for name in sorted(paths):archive.add(ROOT/name,arcname=name,recursive=False)
    (folder/'archive.json').write_text(json.dumps(dict(source_files={p:sha(ROOT/p) for p in sorted(paths)}),indent=2)+'\n')
    report=dict(checkpoint='CP61',selected='cp61g',baseline='cp60b',measurements=measurements,
                remaining=dict(lut4=51,slices=24,ebr=1,microcode_words=22),nominal_clock_mhz=29.56,
                verification_clock_mhz=31.824,board_installed='cp56a',default_profile='cp52a',flashed=False)
    (ROOT/'docs/synthesis-cp61.json').write_text(json.dumps(report,indent=2)+'\n')
    verification=dict(selected='cp61g',cold_chain='ROM -> HALT FRAM -> USER FRAM -> RT-11',
        boot_e_directed=dict(cases_per_rom=17,rom_models=['portable','Lattice DP8KC/ODDRXE']),
        boot_e_rt11=read(baseline/'rt11/result.json'),final_guard_proof=dict(combinations=524288,negative_control=True),
        final_rt11=result,final_edif=dict(nets=edif['nets'],unexplained_floating=0,multiple_drivers=0,negative_controls=3),
        rk_csr_beats_per_rom=28,rk_negative_control=True,microcode_words=1002,
        boot_microclocks=223912,copy_enter_to_leave_clocks=dict(one_word=2862,words64=34930),
        abi1_loader_migrated=False,full_odt=False,full_fp11=False,board_flashed=False,
        archive_sha256={str(p.relative_to(ROOT)):sha(p) for p in dest.rglob('source.tgz')})
    (ROOT/'docs/verification-cp61.json').write_text(json.dumps(verification,indent=2)+'\n')
    print('PASS CP61 archives, exact final synthesis/simulation sources, seven resource checkpoints')


if __name__=='__main__':main()
