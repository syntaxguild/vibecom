"""Blender: audit the shipped assembly without modifying its CAD exports.

Run: blender --background --python-exit-code 1 --python mechanical/audit_assembly.py
Intersections are envelope checks, not a physical fit certification.
"""
import bpy
import bmesh
import hashlib
import json
import math
import sys
from pathlib import Path
from mathutils import Matrix, Vector

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
    # Boolean tangency can leave a flat sheet. Signed volume is undefined for
    # that open surface; reject sheets thinner than 0.00001 mm before measuring.
    extents = [max((v.co[i] for v in bm.verts), default=0) -
               min((v.co[i] for v in bm.verts), default=0) for i in range(3)]
    volume = abs(bm.calc_volume(signed=True)) if min(extents) > 0.00001 else 0
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

# Remaining proxies reserve connector and backlight space beyond the PCB guides.
proxies = [
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

# Populate the removable tray outside the enclosure: lower the board into its
# open-top cradles, then slide the loaded tray through the open rear bay.
controller_path = []
controller_parts = [obj for name,obj in objects.items() if name.startswith('REF_DevKitC_')]
for dz in range(20,-1,-2):
    volume = 0
    for obj in controller_parts:
        obj.location.z = dz
        volume += overlap(obj,objects['08_electronics_tray'])
        obj.location.z = 0
    controller_path.append({'delta_z':dz,'overlap_mm3':round(volume,4)})
tray_path = []
for dy in range(40,-1,-2):
    volume = 0
    for obj in controller_parts+[objects['08_electronics_tray']]:
        obj.location.y = dy
        volume += overlap(obj,objects['04_body_shell'])
        volume += overlap(obj,objects['05_base_cover'])
        obj.location.y = 0
    tray_path.append({'delta_y':dy,'overlap_mm3':round(volume,4)})
front_shell_path=[]
for dy,dz in [(dy,3) for dy in range(-40,1,2)]+[(0,2),(0,1),(0,0)]:
    shell=objects['04_body_shell'];shell.location=(0,dy,dz)
    volume=sum(overlap(shell,obj) for obj in controller_parts+[objects['05_base_cover'],objects['08_electronics_tray']])
    shell.location=(0,0,0)
    front_shell_path.append({'delta_y':dy,'delta_z':dz,'overlap_mm3':round(volume,4)})
rear_cover_path = []
for dy,dz in [(dy,3) for dy in range(32,-1,-2)]+[(0,2),(0,1),(0,0)]:
    cover=objects['07_body_rear'];cover.location=(0,dy,dz)
    volume=sum(overlap(cover,obj) for obj in controller_parts+[objects['04_body_shell'],objects['05_base_cover'],objects['08_electronics_tray']])
    cover.location=(0,0,0)
    rear_cover_path.append({'delta_y':dy,'delta_z':dz,'overlap_mm3':round(volume,4)})
servo_path = []
for delta_z in [-40,-32,-24,-16,-8,0]:
    volumes = []
    for name in ['REF_SG90_case','REF_SG90_tabs']:
        obj = objects[name]
        obj.location.z = delta_z
        volumes.append(overlap(obj,objects['04_body_shell']))
        obj.location.z = 0
    servo_path.append({'delta_z':delta_z,'overlap_mm3':round(sum(volumes),4)})

# Outboard closure bosses leave a straight rear insertion path for the LCD.
lcd_parts = [obj for name,obj in objects.items() if name.startswith('REF_Waveshare_17344_')]
lcd_proxy = box('LCD stack insertion reservation',(47.83,4,34.6),(0,-19.7,75.8))
lcd_parts.append(lcd_proxy)
lcd_poses = [(y,0) for y in list(range(35,-16,-3))+[-16.9]]
lcd_path = []
for y,tilt in lcd_poses:
    matrix = Matrix.Translation(Vector((0,y,75.8))) @ Matrix.Rotation(math.radians(tilt),4,'X') @ Matrix.Translation(Vector((0,16.9,-75.8)))
    volumes = []
    for obj in lcd_parts:
        obj.matrix_world = matrix @ Matrix.Translation(Vector((0,-19.7,75.8))) if obj == lcd_proxy else matrix
        volumes.append(overlap(obj,objects['02_head_front']))
        obj.matrix_world = Matrix.Translation(Vector((0,-19.7,75.8))) if obj == lcd_proxy else Matrix.Identity(4)
    lcd_path.append({'pcb_center_y':y,'tilt_deg':tilt,'overlap_mm3':round(sum(volumes),4)})
bpy.data.objects.remove(lcd_proxy,do_unlink=True)
touch_path = []
touch_parts = [obj for name,obj in objects.items() if name.startswith('REF_DFR0030_')]
for y,dz in [(y,-2.8) for y in range(30,-1,-3)]+[(0,-1.4),(0,0)]:
    volumes = []
    for obj in touch_parts:
        obj.location = (0,y,dz)
        volumes.append(overlap(obj,objects['02_head_front']))
        obj.location = (0,0,0)
    touch_path.append({'delta_y':y,'delta_z':dz,'overlap_mm3':round(sum(volumes),4)})

# Each relocated closure screw must pass through a real pad, not empty space.
spec = json.loads((ROOT/'reference.json').read_text(encoding='utf-8'))
fasteners = []
for x in [-spec['assembly']['base_boss_x'],spec['assembly']['base_boss_x']]:
    for y in [-spec['assembly']['base_boss_y'],spec['assembly']['base_boss_y']]:
        base = objects['05_base_cover']
        hole_clear = not base.ray_cast(Vector((x,y,0)),Vector((0,0,1)),distance=10.21)[0]
        pad_present = all(base.ray_cast(Vector((x+2.5*math.cos(a),y+2.5*math.sin(a),0)),Vector((0,0,1)),distance=10.21)[0]
                          for a in [i*math.pi/4 for i in range(8)])
        fasteners.append({'xy':[x,y],'through_hole_clear':hole_clear,'support_under_screw_head':pad_present})

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
          'tof_insertion_path': path_checks, 'controller_insertion_path': controller_path,
          'front_shell_path':front_shell_path, 'tray_insertion_path': tray_path, 'rear_cover_path': rear_cover_path,
          'servo_insertion_path': servo_path, 'lcd_insertion_path': lcd_path, 'touch_insertion_path': touch_path,
          'base_fasteners': fasteners,
          'geometry_gate_passed': all(f['through_hole_clear'] and f['support_under_screw_head'] for f in fasteners) and not checks and not proxy_checks and all(p['overlap_mm3'] < 0.01 for p in path_checks+controller_path+tray_path+front_shell_path+rear_cover_path+servo_path+lcd_path+touch_path),
          'limits': ['No exact selected plug, screw-head, horn or wire CAD', 'No physical parts measured',
                     'No cable bending, full servo spline geometry, or tool-access certification',
                     'Proxy overlap is a design concern; zero overlap does not prove fit']}
(OUT/'assembly-audit.json').write_text(json.dumps(report,indent=2)+'\n', encoding='utf-8')
print(json.dumps({key:report[key] for key in ['revision','assembly_dimensions_mm','existing_envelope_pairs_checked','existing_envelope_overlaps','provisional_proxy_overlaps','geometry_gate_passed']},indent=2))
for name in ['controller_insertion_path','tray_insertion_path','front_shell_path','rear_cover_path','servo_insertion_path','lcd_insertion_path','touch_insertion_path']:
    print(name, [pose for pose in report[name] if pose['overlap_mm3'] >= 0.01])

if '--strict' in sys.argv and not report['geometry_gate_passed']:
    raise RuntimeError('Assembly clearance or sampled insertion path failed; see assembly-audit.json')
