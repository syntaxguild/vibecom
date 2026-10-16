"""Check production behavior + renderer on the host; optionally regenerate previews."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tomllib

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--compiler', help='Path to g++ (C++11 or later)')
parser.add_argument('--preview', action='store_true', help='Regenerate README PNG/GIF; requires Pillow')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
compiler = args.compiler or shutil.which('g++')
if not compiler:
    compiler = str(Path.home() / '.platformio/packages/toolchain-gccmingw32/bin/g++.exe')
if not Path(compiler).is_file():
    parser.error('Install g++ or pass --compiler. On Windows: pio pkg install --global --tool platformio/toolchain-gccmingw32')
gfx_files = [path for profile in ['esp32vibecom', 'esp32dev', 'esp32sim']
             for path in (root / '.pio/libdeps' / profile).glob('Adafruit GFX Library*/Adafruit_GFX.cpp')]
if not gfx_files:
    parser.error('Run pio run first to install the pinned GFX dependency.')
gfx = next((p for p in gfx_files if '@1.12.1' in str(p)), gfx_files[0])
output = root / '.pio/render-check'
output.mkdir(parents=True, exist_ok=True)
environment = dict(os.environ)
environment['PATH'] = str(Path(compiler).parent) + os.pathsep + environment.get('PATH', '')
profiles = [('legacy', []), ('reference', ['-DVIBECOM_REFERENCE']),
            ('large', ['-DVIBECOM_REFERENCE', '-DVIBECOM_LARGE_SCREEN']),
            ('tof', ['-DVIBECOM_REFERENCE', '-DVIBECOM_LARGE_SCREEN', '-DVIBECOM_TOF'])]
for profile, flags in profiles:
    behavior_executable = output / (f'behavior-{profile}' + ('.exe' if os.name == 'nt' else ''))
    subprocess.run([compiler, '-std=c++11', '-O2', '-Wall', '-Wextra', *flags,
                    '-I' + str(root / 'src'), str(root / 'tests/behavior_checks.cpp'),
                    '-o', str(behavior_executable)], check=True, env=environment)
    subprocess.run([str(behavior_executable)], check=True, env=environment)
for profile, flags in profiles:
    executable = output / (f'face-{profile}' + ('.exe' if os.name == 'nt' else ''))
    subprocess.run([compiler, '-std=c++11', '-O2', '-DARDUINO=10800', *flags,
                    '-I' + str(root / 'tests/host'), '-I' + str(root / 'src'),
                    '-I' + str(gfx.parent), str(root / 'tests/face_render.cpp'),
                    str(gfx), '-o', str(executable)], check=True, env=environment)
    preview_args = [str(output)] if args.preview and profile == 'large' else []
    subprocess.run([str(executable), *preview_args], check=True, env=environment)

# Verify that the diagram really uses the new sensor and matching WASM, then
# exercise its C implementation against the same Pololu driver as the firmware.
diagram = json.loads((root/'diagram.json').read_text(encoding='utf-8'))
assert not any(part['type'] == 'wokwi-hc-sr04' for part in diagram['parts'])
assert next(part for part in diagram['parts'] if part['id'] == 'distance')['type'] == 'chip-vl53l1x'
connections = {frozenset(wire[:2]) for wire in diagram['connections']}
for sensor_pin, esp_pin in [('VIN', '3V3'), ('GND', 'GND.2'), ('SDA', '21'), ('SCL', '22')]:
    assert frozenset([f'distance:{sensor_pin}', f'esp:{esp_pin}']) in connections
config = tomllib.loads((root/'wokwi.toml').read_text(encoding='utf-8'))
assert any(chip['name'] == 'vl53l1x' and chip['binary'] == 'sim/vl53l1x.chip.wasm' for chip in config['chip'])
report = json.loads((root/'sim/vl53l1x.build.json').read_text(encoding='utf-8'))
for name, expected in report['sha256'].items():
    assert hashlib.sha256((root/'sim'/name).read_bytes()).hexdigest() == expected, f'Rebuild custom chip: {name}'
assert (root/'sim/vl53l1x.chip.wasm').read_bytes()[:8] == b'\0asm\x01\0\0\0'
c_compiler = str(Path(compiler).with_name('gcc.exe' if os.name == 'nt' else 'gcc'))
chip_object = output/'vl53l1x-chip.o'
host_chip = output/'vl53l1x.chip.c'
# Keep a locally downloaded WASM SDK header from shadowing the host API shim.
shutil.copyfile(root/'sim/vl53l1x.chip.c', host_chip)
subprocess.run([c_compiler, '-std=c11', '-O2', '-Wall', '-Wextra',
                '-I'+str(root/'tests/chip-host'), '-c', str(host_chip),
                '-o', str(chip_object)], check=True, env=environment)
drivers = [root/'.pio/libdeps'/profile/'VL53L1X/VL53L1X.cpp' for profile in ['esp32vibecom', 'esp32sim']]
driver = next((path for path in drivers if path.is_file()), None)
if driver is None:
    parser.error('Build esp32vibecom or esp32sim to install the pinned Pololu driver.')
sim_test = output/('tof-sim.exe' if os.name == 'nt' else 'tof-sim')
subprocess.run([compiler, '-std=c++11', '-O2', '-DVIBECOM_REFERENCE', '-DVIBECOM_LARGE_SCREEN', '-DVIBECOM_TOF',
                '-I'+str(root/'tests/chip-host'), '-I'+str(root/'src'), '-I'+str(driver.parent),
                str(root/'tests/tof_sim_checks.cpp'), str(driver), str(chip_object),
                '-o', str(sim_test)], check=True, env=environment)
subprocess.run([str(sim_test)], check=True, env=environment)
print('PASS: ToF diagram wiring, custom chip source/JSON/WASM fingerprints and I2C driver integration')
if args.preview:
    from PIL import Image, ImageDraw
    sheet = Image.new('RGB', (1600, 274), '#10151a')
    labels = ['IDLE', 'HAPPY', 'ANGRY', 'LOVE / TOUCH', 'SLEEPY']
    draw = ImageDraw.Draw(sheet)
    for index, label in enumerate(labels):
        with Image.open(output / f'pose-{index}.ppm') as pose:
            sheet.paste(pose, (index * 320, 34))
        draw.text((index * 320 + 160, 14), label, fill='#c7d1da', anchor='mm')
    sheet.save(root / 'docs/eye-preview.png')
    frames = []
    for index in range(180):
        with Image.open(output / f'love-{index}.ppm') as frame:
            frames.append(frame.convert('RGB'))
    frames[0].save(root / 'docs/petting-preview.gif', save_all=True,
                   append_images=frames[1:], duration=[30, 30, 40] * 60, loop=0,
                   disposal=2)
    events = []
    labels = ['WAKE', 'GREETING', 'SINGLE TAP', 'DOUBLE TAP / PLAY']
    for index in range(192):
        with Image.open(output / f'event-{index}.ppm') as frame:
            preview = frame.convert('RGB')
        ImageDraw.Draw(preview).text((160, 16), labels[index // 48], fill='#c7d1da', anchor='mm')
        events.append(preview)
    durations = [round(ms / 48) for ms in [1100, 1600, 900, 1800] for _ in range(48)]
    events[0].save(root / 'docs/interaction-preview.gif', save_all=True,
                   append_images=events[1:], duration=durations, loop=0, disposal=2)
    print('Generated eye, petting and interaction previews from production rendering/poses (not a hardware recording).')
