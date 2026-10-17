"""Package the current prototype for a GitHub release; Python standard library."""
from pathlib import Path
import hashlib
import json
import shutil
import zipfile

ROOT = Path(__file__).resolve().parent.parent
version = (ROOT / 'VERSION').read_text(encoding='utf-8').strip()
spec = json.loads((ROOT / 'mechanical/reference.json').read_text(encoding='utf-8'))
assert spec['revision'].startswith('v' + version + '-'), 'Release/CAD version mismatch'
exports = ROOT / 'mechanical/exports'
parts = sorted(exports.glob('*.stl'))
assert {p.name for p in parts} == set(spec['print_parts']), 'Unexpected or missing printable parts'
out = ROOT / '.pio/release'
out.mkdir(parents=True, exist_ok=True)
archive = out / f'vibecom-v{version}-print-parts.zip'
with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED) as bundle:
    for part in parts:
        bundle.write(part, part.name)
    bundle.write(ROOT / 'mechanical/ASSEMBLY.md', 'ASSEMBLY.md')
    bundle.write(ROOT / 'mechanical/reference.json', 'reference.json')
    bundle.writestr('README.txt',
        f'Vibecom v{version} - prototype fit-test parts\n\n'
        'Print one of each of the eight STL files. Units: millimeters. Scale: 100%.\n'
        'Orient parts and choose supports in the slicer. This is not a production-certified kit.\n'
        'See ASSEMBLY.md for fit checks, assembly order and candidate fasteners.\n'
        'Confirm the actual LCD stack, connectors, horn and wiring before a finished print set.\n'
        'Do not mix the body/base with earlier versions.\n\n'
        'Project: https://github.com/syntaxguild/vibecom\n')
assets = [archive]
for suffix in ['blend', 'glb']:
    target = out / f'vibecom-v{version}.{suffix}'
    shutil.copyfile(exports / f'vibecom.{suffix}', target)
    assets.append(target)
(out / 'SHA256SUMS.txt').write_text(''.join(
    f'{hashlib.sha256(asset.read_bytes()).hexdigest()}  {asset.name}\n'
    for asset in assets), encoding='utf-8')
print(f'Packaged {len(assets)} assets and SHA256SUMS.txt in .pio/release/')
