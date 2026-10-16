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
bounds = {}
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
    bounds[path.stem] = (min(v[2] for v in vertices), max(v[2] for v in vertices))
    print(f'PASS: {path.name}: closed, connected, {triangles} triangles, {volume:.1f} mm3')
spec = json.loads((root / 'reference.json').read_text(encoding='utf-8'))
head_bottom = min(bounds[name][0] for name in ['01_head_rear', '02_head_front'])
gap = head_bottom - bounds['04_body_shell'][1]
assert abs(gap-spec['enclosure']['head_body_gap']) < 0.001, 'Head/torso running seam'
joint = report['joint']
assert abs(gap-joint['head_body_gap_mm']) < 0.001
assert abs(bounds['06_neck_horn_adapter'][1]-joint['adapter_flange_top_mm']) < 0.001
assert head_bottom < joint['adapter_flange_top_mm'] < joint['internal_mount_top_mm']
assert joint['internal_mount_top_mm'] < joint['display_pcb_bottom_mm'], 'Joint/display clearance'
print(f'PASS: {gap:.1f} mm head/torso seam and recessed adapter below display PCB')
assert len(report['fit']) == 3 and all(pair['overlap_mm3'] < 0.01 for pair in report['fit'])
assert report['tof_aperture']['clear_reference_rays'] == 36
assert report['tof_aperture']['aperture_mm'] == spec['enclosure']['tof_aperture']
print('PASS: generated mount intersection and ToF aperture reports (reference geometry only)')
glb = (out / 'vibecom.glb').read_bytes()
magic, version, length = struct.unpack_from('<4sII', glb)
assert magic == b'glTF' and version == 2 and length == len(glb)
json_length, json_type = struct.unpack_from('<I4s', glb, 12)
assert json_type == b'JSON'
gltf = json.loads(glb[20:20+json_length])
assert any(all(abs(value - 0.001) < 1e-8 for value in node.get('scale', [1, 1, 1]))
           for node in gltf['nodes']), 'GLB must convert millimeters to meters'
for name in ['vibecom.blend', 'vibecom-assembled.png', 'vibecom-happy.png', 'vibecom-exploded.png']:
    assert (out / name).stat().st_size > 1000, name
print('PASS: source fingerprints, GLB container/scale, editable BLEND and rendered previews')

# The geometry audit must describe this exact assembly and its current script.
audit = json.loads((root/'audit/assembly-audit.json').read_text(encoding='utf-8'))
assert audit['revision'] == spec['revision']
assert audit['blend_sha256'] == hashlib.sha256((out/'vibecom.blend').read_bytes()).hexdigest()
assert audit['audit_script_sha256'] == hashlib.sha256((root/'audit_assembly.py').read_bytes()).hexdigest()
assert audit['geometry_gate_passed'], 'Regenerate/fix the assembly audit'
assert not audit['existing_envelope_overlaps'] and not audit['provisional_proxy_overlaps']
assert len(audit['base_fasteners']) == 4 and all(f['through_hole_clear'] and f['support_under_screw_head'] for f in audit['base_fasteners'])
print('PASS: current assembly audit, connector reservations, sampled insertion paths and four base screw pads')

robust = json.loads((root/'audit/robustness.json').read_text(encoding='utf-8'))
assert robust['revision'] == spec['revision'] and robust['blend_sha256'] == audit['blend_sha256']
assert robust['script_sha256'] == hashlib.sha256((root/'check_robustness.py').read_bytes()).hexdigest()
assert robust['audit_loader_sha256'] == audit['audit_script_sha256']
assert robust['digital_checks_passed'], 'Additional digital checks failed'
print('PASS: position stress scenarios, sampled head yaw and closure screw insertion checks')
