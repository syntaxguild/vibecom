"""Check production behavior + renderer on the host; optionally regenerate previews."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

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
gfx_files = [path for profile in ['esp32vibecom', 'esp32dev']
             for path in (root / '.pio/libdeps' / profile).glob('Adafruit GFX Library*/Adafruit_GFX.cpp')]
if not gfx_files:
    parser.error('Run pio run first to install the pinned GFX dependency.')
gfx = next((p for p in gfx_files if '@1.12.1' in str(p)), gfx_files[0])
output = root / '.pio/render-check'
output.mkdir(parents=True, exist_ok=True)
executable = output / ('face-check.exe' if os.name == 'nt' else 'face-check')
environment = dict(os.environ)
environment['PATH'] = str(Path(compiler).parent) + os.pathsep + environment.get('PATH', '')
for profile, flags in [('legacy', []), ('reference', ['-DVIBECOM_REFERENCE'])]:
    behavior_executable = output / (f'behavior-{profile}' + ('.exe' if os.name == 'nt' else ''))
    subprocess.run([compiler, '-std=c++11', '-O2', '-Wall', '-Wextra', *flags,
                    '-I' + str(root / 'src'), str(root / 'tests/behavior_checks.cpp'),
                    '-o', str(behavior_executable)], check=True, env=environment)
    subprocess.run([str(behavior_executable)], check=True, env=environment)
subprocess.run([compiler, '-std=c++11', '-O2', '-DARDUINO=10800',
                '-I' + str(root / 'tests/host'), '-I' + str(root / 'src'),
                '-I' + str(gfx.parent), str(root / 'tests/face_render.cpp'),
                str(gfx), '-o', str(executable)], check=True, env=environment)
subprocess.run([str(executable)] + ([str(output)] if args.preview else []), check=True, env=environment)
if args.preview:
    from PIL import Image, ImageDraw
    sheet = Image.new('RGB', (1200, 274), '#10151a')
    labels = ['IDLE', 'HAPPY', 'ANGRY', 'LOVE / TOUCH', 'SLEEPY']
    draw = ImageDraw.Draw(sheet)
    for index, label in enumerate(labels):
        with Image.open(output / f'pose-{index}.ppm') as pose:
            sheet.paste(pose, (index * 240, 34))
        draw.text((index * 240 + 120, 14), label, fill='#c7d1da', anchor='mm')
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
        ImageDraw.Draw(preview).text((120, 16), labels[index // 48], fill='#c7d1da', anchor='mm')
        events.append(preview)
    durations = [round(ms / 48) for ms in [1100, 1600, 900, 1800] for _ in range(48)]
    events[0].save(root / 'docs/interaction-preview.gif', save_all=True,
                   append_images=events[1:], duration=durations, loop=0, disposal=2)
    print('Generated eye, petting and interaction previews from production rendering/poses (not a hardware recording).')
