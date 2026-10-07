"""Qualify the BIOS-only change and its targeted shared-card checks."""
from pathlib import Path
import hashlib
import json

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
PROJECT = ROOT/'build/diamond-shared-sd-20261006'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


boot_sha = sha(ROOT/'build/boot.hex')
names = ['models-check.json', 'ebr-checks.json', 'primary-bios-check.json',
         'shared-bios-check.json', 'exact-card-handoff.json', 'results-mapped-ebr.json', 'scope.json']
checks = {}
for name in names:
    data = json.loads((HERE/name).read_text())
    assert data['passed'], name
    if 'boot_hex_sha256' in data:
        assert data['boot_hex_sha256'] == boot_sha, name
    for source, digest in data.get('sources', {}).items():
        assert sha(ROOT/source) == digest, (name, source)
    for entry in data.get('results', []):
        assert entry['passed'], (name, entry['name'])
        if 'log' in entry:
            assert sha(ROOT/entry['log']) == entry['log_sha256'], entry['name']
    checks[name] = sha(HERE/name)

handoff = json.loads((HERE/'exact-card-handoff.json').read_text())
assert sha(HERE/'boot.log') == handoff['log_sha256']
assert sha(HERE/'shared-card.img') == handoff['image_sha256']
assert sha(HERE/'obj/Vtb_sd_boot') == handoff['binary_sha256']
assert sha(ROOT/'tests/tb_sd_boot.v') == handoff['fixture']['source_sha256']
assert sha(HERE/'tb_sd_boot.v') == handoff['fixture']['derived_sha256']
mapped = json.loads((HERE/'results-mapped-ebr.json').read_text())
assert sha(PROJECT/'impl1/bk0010_impl1.edi') == mapped['edif_sha256']
assert sha(ROOT/'tools/test_mapped_ebr.py') == mapped['test_sha256']
assert sha(HERE/'tb_mapped_ebr.v') == mapped['generated_test_sha256']
models = json.loads((HERE/'models-check.json').read_text())
assert sha(HERE/'models.log') == models['log_sha256']
assert sha(ROOT/'tests/test_models.py') == models['test_sha256']
assert sha(ROOT/'tools/models.py') == models['model_sha256']
diamond = json.loads((PROJECT/'result.json').read_text())
assert diamond['fully_routed'] and diamond['timing_pass']
assert diamond['setup_errors'] == diamond['hold_errors'] == 0
for source, digest in diamond['sources'].items():
    assert sha(PROJECT/source) == digest, source
    if source == 'clock.lpf':
        expected = (ROOT/'boards/hardware-lcd/pins.lpf').read_text()+'\n'+(ROOT/'boards/hardware-lcd/cdc.lpf').read_text()
        assert hashlib.sha256(expected.encode()).hexdigest() == digest, source
    else:
        assert sha(ROOT/source) == digest, source
assert sha(PROJECT/'impl1/bk0010_impl1.jed') == diamond['jed']['sha256']
assert sha(HERE/'bk-shared-sd-2a29.jed') == diamond['jed']['sha256']
record = dict(passed=True, jed=diamond['jed'], boot_hex_sha256=boot_sha,
              checks=checks, resources=diamond['resources'], clocks=diamond['clocks'],
              physical_flash_verified=False, physical_shared_card_boot_verified=False,
              scope='BIOS-only update. Primary and four-logical-partition save/cold/warm cases, '
                    '15 malformed-chain rejections, exact-card BIOS handoff, host mapping and synthesized ROM. '
                    'Unchanged peripheral RTL retains the previous release validation; the full suite was not rerun.')
(HERE/'qualification.json').write_text(json.dumps(record, indent=2)+'\n')
print('PASS BIOS-only qualification: JED '+record['jed']['checksum'])
