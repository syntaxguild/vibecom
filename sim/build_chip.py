"""Compile the Wokwi custom chip and record matching source/binary fingerprints."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cli', default=shutil.which('wokwi-cli') or str(root/'.pio/tools/wokwi-cli.exe'))
args = parser.parse_args()
subprocess.run([args.cli, 'chip', 'compile', 'sim/vl53l1x.chip.c',
                '-o', 'sim/vl53l1x.chip.wasm'], cwd=root, check=True)
names = ['vl53l1x.chip.c', 'vl53l1x.chip.json', 'vl53l1x.chip.wasm']
report = {'compiler': subprocess.check_output([args.cli, '--short-version'], text=True).strip(),
          'sha256': {name: hashlib.sha256((root/'sim'/name).read_bytes()).hexdigest() for name in names}}
(root/'sim/vl53l1x.build.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8', newline='\n')
