"""Check repository-local Markdown/image links and release version consistency."""
from pathlib import Path
import json
import re
import subprocess
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parent.parent
version = (ROOT / 'VERSION').read_text(encoding='utf-8').strip()
assert re.fullmatch(r'\d+\.\d+\.\d+', version), 'VERSION must be MAJOR.MINOR.PATCH'
revision = json.loads((ROOT / 'mechanical/reference.json').read_text(encoding='utf-8'))['revision']
assert revision.startswith('v' + version + '-'), 'VERSION and CAD revision differ'
tracked = subprocess.check_output(['git', 'ls-files', '--cached', '--others', '--exclude-standard'], cwd=ROOT, text=True).splitlines()
count = 0
errors = []
for name in sorted(set(tracked)):
    path = ROOT / name
    if path.suffix != '.md' or not path.exists():
        continue
    content = path.read_text(encoding='utf-8')
    content = re.sub(r'```.*?```', '', content, flags=re.S)
    targets = re.findall(r'\]\(([^)]+)\)', content)
    targets += re.findall(r'(?:src|href)="([^"]+)"', content)
    for target in targets:
        target = target.strip().strip('<>')
        url = urlsplit(target)
        if url.scheme or url.netloc or not url.path:
            continue
        count += 1
        resolved = (path.parent / unquote(url.path)).resolve()
        if not resolved.exists():
            errors.append(f'{name}: {target}')
assert not errors, 'Broken local links:\n' + '\n'.join(errors)
print(f'PASS: version {version}, CAD revision and {count} local documentation links')
