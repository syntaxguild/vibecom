"""Blender: audit the shipped assembly without modifying its CAD exports.

Run: blender --background --python-exit-code 1 --python mechanical/audit_assembly.py
Intersections are envelope checks, not a physical fit certification.
"""
import bpy
import bmesh
import hashlib
import json
from pathlib import Path
from mathutils import Matrix

ROOT = Path(__file__).resolve().parent
OUT = ROOT / 'audit'
OUT.mkdir(exist_ok=True)
blend = ROOT / 'exports/vibecom.blend'
bpy.ops.wm.open_mainfile(filepath=str(blend))
objects = {}
for original in list(bpy.data.objects):
    if original.type != 'MESH' or not (original.name[:2].isdigit() or original.name.startswith('REF_')):
        continue
    obj = original.copy()
    obj.data = original.data.copy()
    obj.parent = None
    obj.matrix_world = Matrix.Identity(4)
    for vertex in obj.data.vertices:
        vertex.co = (original.matrix_world @ vertex.co) * 1000
    bpy.context.collection.objects.link(obj)
    objects[original.name] = obj


def overlap(first, second):
    probe = first.copy()
    probe.data = first.data.copy()
    bpy.context.collection.objects.link(probe)
    bpy.context.view_layer.objects.active = probe
    modifier = probe.modifiers.new('Audit intersection', 'BOOLEAN')
    modifier.operation = 'INTERSECT'
    modifier.solver = 'EXACT'
    modifier.object = second
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    bm = bmesh.new()
    bm.from_mesh(probe.data)
    volume = abs(bm.calc_volume(signed=True))
    bm.free()
    bpy.data.objects.remove(probe, do_unlink=True)
    return round(volume, 4)


def box(name, dims, center):
    bpy.ops.mesh.primitive_cube_add(size=1, location=center)
    obj = bpy.context.object
    obj.name = name
    obj.dimensions = dims
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    return obj


parts = [name for name in objects if name[:2].isdigit()]
guides = [name for name in objects if name.startswith('REF_')]
checks = []
checked_pairs = 0
for guide in guides:
    for part in parts:
        checked_pairs += 1
        volume = overlap(objects[guide], objects[part])
        if volume > 0.01:
            checks.append({'first': guide, 'second': part, 'overlap_mm3': volume,
                           'interpretation': 'Review overlap: simplified envelopes omit holes and detailed shapes'})
for index, first in enumerate(guides):
    for second in guides[index+1:]:
        # The component envelope intentionally includes its own PCB/tabs.
        family = lambda name: name.split('_')[1]
        if family(first) == family(second):
            continue
        checked_pairs += 1
        volume = overlap(objects[first], objects[second])
        if volume > 0.01:
            checks.append({'first': first, 'second': second, 'overlap_mm3': volume})

# Espressif drawing dimension 48.26 excludes the 6.04 mm antenna overhang.
# Keep the existing USB end at X=24.13 and extend the RF end towards -X.
proxies = [
    ('PROBE_DevKitC_antenna_extension', (6.04, 1.6, 18), (-27.15, 16, 25), 'Manufacturer T-shaped antenna extension; 1.6 mm thickness is assumed'),
    ('PROBE_DevKitC_header_upper', (48.26, 8.5, 2.54), (0, 10.95, 37.7), 'Assumed header/socket height; not a selected connector'),
    ('PROBE_DevKitC_header_lower', (48.26, 8.5, 2.54), (0, 10.95, 12.3), 'Assumed header/socket height; not a selected connector'),
    ('PROBE_USB_plug_keepout', (20, 12, 8), (30, 16, 25), 'Provisional plug shell envelope; cable/plug not selected'),
    ('PROBE_LCD_module_front', (47.83, 4.0, 34.60), (0, -19.7, 75.8), 'Drawing backlight outline; provisional stack location'),
    ('PROBE_LCD_connector_back', (8, 8, 20), (-25, -12.1, 75.8), 'Approximate PH connector/plug reservation; actual plug not selected'),
    ('PROBE_touch_connector_under', (12, 8, 8), (0, 7, 93), 'Provisional touch connector reservation; actual connector orientation unknown'),
]
proxy_checks = []
for name, dims, center, evidence in proxies:
    proxy = box(name, dims, center)
    for part in parts:
        volume = overlap(proxy, objects[part])
        if volume > 0.01:
            proxy_checks.append({'first': name, 'second': part, 'overlap_mm3': volume,
                                 'evidence': evidence})
    bpy.data.objects.remove(proxy, do_unlink=True)

# Rear-open ToF insertion: raise inside the body, then move towards the aperture.
tof = objects['REF_Pololu_3415_PCB']
path_checks = []
for z in [2, 8, 14, 20, 26]:
    tof.location = (0, 8, z-26)
    path_checks.append({'y': -18, 'z': z, 'overlap_mm3': overlap(tof, objects['04_body_shell'])})
for y in [-20, -22, -24, -26]:
    tof.location = (0, y+26, 0)
    path_checks.append({'y': y, 'z': 26, 'overlap_mm3': overlap(tof, objects['04_body_shell'])})
tof.location = (0, 0, 0)

bounds = {}
for name in parts:
    obj = objects[name]
    bounds[name] = [[round(min(v.co[i] for v in obj.data.vertices), 3),
                     round(max(v.co[i] for v in obj.data.vertices), 3)] for i in range(3)]
assembly_dimensions = [round(max(bounds[name][i][1] for name in parts)-min(bounds[name][i][0] for name in parts), 3) for i in range(3)]
report = {'revision': json.loads((ROOT/'reference.json').read_text(encoding='utf-8'))['revision'], 'scope': 'Read-only audit of shipped BLEND and declared provisional proxies',
          'blend_sha256': hashlib.sha256(blend.read_bytes()).hexdigest(),
          'audit_script_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
          'manufacturer_sources': {
              'display': 'https://files.waveshare.com/upload/6/6d/2inch_LCD_Module_2D_Drawing.pdf',
              'controller': 'https://dl.espressif.com/dl/schematics/esp32_devkitc_v4_dimensions.pdf',
              'servo': 'https://towerpro.com.tw/product/sg90-7/',
              'touch': 'https://wiki.dfrobot.com/dfr0030/',
              'distance': 'https://www.pololu.com/file/0J1194/vl53l0x-vl53l1x-vl53l3cx-vl53l4cd-carrier-dimension-diagram.pdf'},
          'print_ready': False, 'assembly_dimensions_mm': assembly_dimensions, 'part_bounds_mm': bounds,
          'existing_envelope_pairs_checked': checked_pairs, 'existing_envelope_overlaps': checks, 'provisional_proxy_overlaps': proxy_checks,
          'tof_insertion_path': path_checks,
          'limits': ['No exact selected plug, screw-head, horn or wire CAD', 'No physical parts measured',
                     'No cable bending, full servo spline geometry, or tool-access certification',
                     'Proxy overlap is a design concern; zero overlap does not prove fit']}
(OUT/'assembly-audit.json').write_text(json.dumps(report,indent=2)+'\n', encoding='utf-8')
print(json.dumps(report,indent=2))
