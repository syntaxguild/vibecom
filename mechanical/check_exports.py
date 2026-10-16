"""Check shipped CAD outputs using only Python's standard library (also run by CI)."""
from collections import Counter, defaultdict
import hashlib
import json
import math
from pathlib import Path
import struct

root = Path(__file__).resolve().parent
out = root / 'exports'
report = json.loads((out / 'validation.json').read_text(encoding='utf-8'))
for name, expected in report['source_sha256'].items():
    assert hashlib.sha256((root / name).read_bytes()).hexdigest() == expected, f'Regenerate CAD: {name}'
assert len(report['parts']) == 6
for part in report['parts']:
    path = out / part['file']
    data = path.read_bytes()
    triangles, = struct.unpack_from('<I', data, 80)
    assert len(data) == 84 + 50 * triangles, path
    edges, adjacency, volume, vertices = Counter(), defaultdict(set), 0.0, set()
    for normal_and_triangle in struct.iter_unpack('<12fH', data[84:]):
        xyz = normal_and_triangle[3:12]
        assert all(math.isfinite(value) for value in xyz), path
        a, b, c = [tuple(xyz[i:i+3]) for i in (0, 3, 6)]
        vertices.update((a, b, c))
        cross = (b[1]*c[2]-b[2]*c[1], b[2]*c[0]-b[0]*c[2], b[0]*c[1]-b[1]*c[0])
        volume += sum(a[i]*cross[i] for i in range(3)) / 6
        for u, v in [(a, b), (b, c), (c, a)]:
            assert u != v, f'Degenerate edge: {path}'
            edges[tuple(sorted((u, v)))] += 1
            adjacency[u].add(v)
            adjacency[v].add(u)
    assert all(count == 2 for count in edges.values()), f'Open/nonmanifold STL: {path}'
    remaining = set(vertices)
    components = 0
    while remaining:
        components += 1
        queue = [remaining.pop()]
        while queue:
            for neighbor in adjacency[queue.pop()]:
                if neighbor in remaining:
                    remaining.remove(neighbor)
                    queue.append(neighbor)
    assert components == 1, f'Disconnected solids: {path} ({components})'
    assert volume > 0 and abs(volume - part['volume_mm3']) < max(1, volume * 0.001), path
    dimensions = [max(v[i] for v in vertices) - min(v[i] for v in vertices) for i in range(3)]
    assert 1 < max(dimensions) < 150, f'STL unit error: {path}'
    print(f'PASS: {path.name}: closed, connected, {triangles} triangles, {volume:.1f} mm3')
glb = (out / 'vibecom.glb').read_bytes()
magic, version, length = struct.unpack_from('<4sII', glb)
assert magic == b'glTF' and version == 2 and length == len(glb)
json_length, json_type = struct.unpack_from('<I4s', glb, 12)
assert json_type == b'JSON'
gltf = json.loads(glb[20:20+json_length])
assert any(all(abs(value - 0.001) < 1e-8 for value in node.get('scale', [1, 1, 1]))
           for node in gltf['nodes']), 'GLB must convert millimeters to meters'
for name in ['vibecom.blend', 'vibecom-assembled.png', 'vibecom-exploded.png']:
    assert (out / name).stat().st_size > 1000, name
print('PASS: source fingerprints, GLB container/scale, editable BLEND and rendered previews')
