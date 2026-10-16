"""Blender 5.2: editable Vibecom enclosure, STL parts, GLB and studio previews.

Run: blender --background --factory-startup --python mechanical/build_model.py
All CAD construction coordinates are millimeters; the saved assembly/GLB uses meters.
"""
import bpy
import bmesh
import hashlib
import json
import math
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parent
SPEC = json.loads((ROOT / 'reference.json').read_text(encoding='utf-8'))
E = SPEC['enclosure']
OUT = ROOT / 'exports'
OUT.mkdir(exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
scene = bpy.context.scene
scene.unit_settings.system = 'METRIC'
scene.unit_settings.length_unit = 'MILLIMETERS'
scene.unit_settings.scale_length = 1
parts, visual, guides = [], [], []


def material(name, color, roughness=0.4, metallic=0):
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = (*color, 1)
    mat.use_nodes = True
    shader = mat.node_tree.nodes.get('Principled BSDF')
    shader.inputs['Base Color'].default_value = (*color, 1)
    shader.inputs['Roughness'].default_value = roughness
    shader.inputs['Metallic'].default_value = metallic
    return mat


ivory = material('Warm ivory polymer', (0.78, 0.75, 0.67))
graphite = material('Graphite face insert', (0.012, 0.019, 0.022), 0.24)
orange = material('Terracotta neck', (0.66, 0.22, 0.12))
rubber = material('Dark base', (0.035, 0.045, 0.044), 0.65)
blue = material('Reference servo blue', (0.025, 0.16, 0.48))
green = material('Reference PCB', (0.025, 0.20, 0.10))
metal = material('Sensor aluminum', (0.45, 0.48, 0.49), 0.25, 0.8)


def recalc(obj):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    bm.to_mesh(obj.data)
    bm.free()


def rounded(name, dims, pos, radius=5, plane='XZ'):
    """Closed rounded rectangle extruded along its third axis, with real CAD radii."""
    axes = (0, 2, 1) if plane == 'XZ' else (0, 1, 2)
    width, height, depth = [dims[i] for i in axes]
    radius = min(radius, width / 2 - 0.01, height / 2 - 0.01)
    perimeter = []
    for cx, cy, start in [(width/2-radius, height/2-radius, 0),
                          (-width/2+radius, height/2-radius, 90),
                          (-width/2+radius, -height/2+radius, 180),
                          (width/2-radius, -height/2+radius, 270)]:
        for step in range(13):
            a = math.radians(start + step * 90 / 12)
            perimeter.append((cx + radius * math.cos(a), cy + radius * math.sin(a)))
    vertices = []
    for w in [-depth/2, depth/2]:
        for u, v in perimeter:
            xyz = [0, 0, 0]
            for axis, value in zip(axes, (u, v, w)):
                xyz[axis] = value
            vertices.append(xyz)
    count = len(perimeter)
    faces = [tuple(range(count-1, -1, -1)), tuple(range(count, count*2))]
    faces += [(i, (i+1) % count, (i+1) % count + count, i + count) for i in range(count)]
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    obj.location = pos
    recalc(obj)
    return obj


def cylinder(name, radius, depth, pos, axis='Z'):
    bpy.ops.mesh.primitive_cylinder_add(vertices=64, radius=radius, depth=depth, location=pos)
    obj = bpy.context.object
    obj.name = name
    if axis == 'Y':
        obj.rotation_euler.x = math.pi / 2
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    return obj


def boolean(obj, cutter, operation='DIFFERENCE'):
    bpy.context.view_layer.objects.active = obj
    modifier = obj.modifiers.new(operation, 'BOOLEAN')
    modifier.operation = operation
    modifier.solver = 'EXACT'
    modifier.object = cutter
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    bpy.data.objects.remove(cutter, do_unlink=True)


def finish(obj, mat, group=parts):
    # Booleans may leave an empty material slot at index 0.
    obj.data.materials.clear()
    obj.data.materials.append(mat)
    for polygon in obj.data.polygons:
        polygon.material_index = 0
        polygon.use_smooth = len(polygon.vertices) == 4
    group.append(obj)
    return obj


# Head: screw-on front, rear shell, removable black face insert.
hw, hd, hh = E['head']
hz = E['head_center'][2]
wall = E['wall']
front_y, split_y = -hd/2, -hd/2 + 4.5
rear = rounded('01_head_rear', (hw, hd-4.5, hh), (0, 2.25, hz), 9)
boolean(rear, rounded('Head cavity', (hw-2*wall, hd+4, hh-2*wall),
                     (0, -wall-2, hz), 9-wall))
boolean(rear, cylinder('Neck socket', E['neck_bore']/2 + 0.4, 10, (0, 0, hz-hh/2)))
for x in [-10, 10]:
    boolean(rear, cylinder('Head adapter fastening hole', 1.2, 10, (x, 0, hz-hh/2)))
boolean(rear, rounded('Touch roof relief', (24, 32, 5),
                     (0, 3, hz+hh/2-E['touch_roof']-2.5), 3, 'XY'))
for x in [-27.5, 27.5]:
    for z in [hz-23, hz+23]:
        boolean(rear, cylinder('Head closure boss', 3.5, 12, (x, split_y+6, z), 'Y'), 'UNION')
        boolean(rear, cylinder('Head pilot', E['closure_pilot_diameter']/2, 14,
                               (x, split_y+6, z), 'Y'))
finish(rear, ivory)
front = rounded('02_head_front', (hw, 4.5, hh), (0, front_y+2.25, hz), 9)
boolean(front, rounded('Screen aperture', (E['screen_aperture'], 12, E['screen_aperture']),
                      (0, front_y+2, hz), 1))
for x in [-27.5, 27.5]:
    for z in [hz-23, hz+23]:
        boolean(front, cylinder('Head screw clearance', E['closure_hole_diameter']/2,
                                10, (x, front_y+2, z), 'Y'))
lcd_pitch = SPEC['components']['display']['mount_pitch']
for x in [-lcd_pitch[0]/2, lcd_pitch[0]/2]:
    for z in [hz-lcd_pitch[1]/2, hz+lcd_pitch[1]/2]:
        boolean(front, cylinder('LCD standoff', 2.8, 3, (x, split_y+1.4, z), 'Y'), 'UNION')
        boolean(front, cylinder('LCD pilot', 0.9, 8, (x, split_y, z), 'Y'))
finish(front, ivory)
insert = rounded('03_face_insert', (52, 1.2, 48), (0, front_y-0.6, hz), 7)
boolean(insert, rounded('Insert window', (25, 6, 25), (0, front_y, hz), 1))
finish(insert, graphite)

# Body opens from the base. Sonar is on the chest, USB exits the rear.
bw, bd, bh = E['body']
bz = E['body_center'][2]
body = rounded('04_body_shell', (bw, bd, bh), (0, 0, bz), 12, 'XY')
boolean(body, rounded('Body cavity', (bw-2*wall, bd-2*wall, bh+6),
                     (0, 0, bz-wall-3), 12-wall, 'XY'))
boolean(body, rounded('Servo insertion slot', (24.2, 13.4, 14), (-5.5, 0, bz+bh/2), 1, 'XY'))
boolean(body, cylinder('Neck cable port', 3, 12, (12, 0, bz+bh/2)))
for x in [-E['sonar_pitch']/2, E['sonar_pitch']/2]:
    boolean(body, cylinder('Sonar aperture', E['sonar_hole_diameter']/2,
                           12, (x, -bd/2, 29), 'Y'))
boolean(body, rounded('Rear USB service opening', (14, 12, 9), (0, bd/2, 12), 2))
for x in [-27, 27]:
    for y in [-25, 25]:
        boolean(body, cylinder('Base closure boss', 3.5, 10, (x, y, 9)), 'UNION')
        boolean(body, cylinder('Base pilot', E['closure_pilot_diameter']/2, 12, (x, y, 9)))
# Broad servo-tab lands, with provisional slots rather than a claimed exact screw pattern.
for x in [-22, 11]:
    boolean(body, rounded('Servo tab land', (8, 20, 4), (x, 0, 54), 1, 'XY'), 'UNION')
    boolean(body, rounded('Servo tab slot', (2.4, 7, 8), (x, 0, 54), 1, 'XY'))
finish(body, ivory)
base = rounded('05_base_cover', (bw, bd, 4), (0, 0, 2), 12, 'XY')
for x in [-27, 27]:
    for y in [-25, 25]:
        boolean(base, cylinder('Base screw clearance', E['closure_hole_diameter']/2, 8, (x, y, 2)))
# Rails support an adhesive/strap-mounted controller; no invented PCB mounting holes.
for x in [-16.5, 16.5]:
    boolean(base, rounded('Controller rail', (3, 44, 5), (x, 3, 6), 0.7, 'XY'), 'UNION')
finish(base, rubber)
neck = cylinder('06_neck_horn_adapter', 9, 18, (0, 0, 67))
boolean(neck, cylinder('Adapter hollow', E['neck_bore']/2, 22, (0, 0, 67)))
# A top flange connects the hollow post to the head; supplied servo horn remains required.
boolean(neck, cylinder('Adapter flange', 14, 3, (0, 0, 74.5)), 'UNION')
boolean(neck, cylinder('Cable bore through flange', E['neck_bore']/2, 24, (0, 0, 67)))
# Attach the original horn below this plate; never print a guessed servo spline.
boolean(neck, cylinder('Horn attachment floor', 9, 3, (0, 0, 59.5)), 'UNION')
boolean(neck, cylinder('Original horn center screw access', 1.6, 6, (0, 0, 59.5)))
for x in [-6.5, 6.5]:
    boolean(neck, rounded('Adjustable horn screw slot', (2.4, 4, 7), (x, 0, 59.5), 1, 'XY'))
boolean(neck, rounded('Side cable entrance', (8, 7, 6), (8, 0, 67), 1))
for x in [-10, 10]:
    boolean(neck, cylinder('Head adapter screw', 1.2, 7, (x, 0, 74.5)))
finish(neck, orange)

# Hardware envelopes are reference solids, never printed parts.
finish(rounded('REF_Waveshare_15867_PCB', (45, 1.6, 31), (0, -13.9, hz), 1), green, guides)
finish(rounded('REF_SG90_case', (23, 12.2, 29), (-5.5, 0, 41), 1, 'XY'), blue, guides)
finish(rounded('REF_SG90_tabs', (32.3, 12.2, 2.5), (-5.5, 0, 53), 1, 'XY'), blue, guides)
finish(rounded('REF_DevKitC_PCB', (27.94, 48.26, 1.6), (0, 3, 9.8), 1, 'XY'), green, guides)
finish(rounded('REF_DevKitC_components', (24, 40, 6), (0, 3, 13.6), 1, 'XY'), graphite, guides)
finish(rounded('REF_DFR0030_touch', (22, 30, 1.6), (0, 3, 132.2), 1, 'XY'), green, guides)
finish(rounded('REF_HCSR04_PCB', (45, 1.6, 20), (0, -20.5, 29), 1), green, guides)
for x in [-E['sonar_pitch']/2, E['sonar_pitch']/2]:
    finish(cylinder('Sonar can reference', 8, 12, (x, -27.4, 29), 'Y'), metal, visual)
    finish(cylinder('Sonar grille visual', 7.2, 0.3, (x, -33.55, 29), 'Y'), graphite, visual)
finish(cylinder('Touch marker visual', 6, 0.15, (0, 3, 134.08)), orange, visual)

# The face is a texture of the firmware rasterizer, at the actual 23.4 mm active size.
screen = rounded('LCD active area 23.4mm', (23.4, 0.05, 23.4), (0, -22.07, hz), 0.1)
texture = bpy.data.materials.new('Firmware idle screen')
texture.use_nodes = True
nodes = texture.node_tree.nodes
shader = nodes.get('Principled BSDF')
image = nodes.new('ShaderNodeTexImage')
image.image = bpy.data.images.load(str(ROOT.parent / 'docs' / 'eye-preview.png'))
image.image.pack()
texture.node_tree.links.new(image.outputs['Color'], shader.inputs['Base Color'])
texture.node_tree.links.new(image.outputs['Color'], shader.inputs['Emission Color'])
shader.inputs['Emission Strength'].default_value = 0.8
shader.inputs['Roughness'].default_value = 0.28
uv = screen.data.uv_layers.new(name='Firmware preview first pose')
for poly in screen.data.polygons:
    for index in poly.loop_indices:
        vertex = screen.data.vertices[screen.data.loops[index].vertex_index].co
        uv.data[index].uv = ((vertex.x / 23.4 + 0.5) / 5,
                             (vertex.z / 23.4 + 0.5) * 240 / 274)
finish(screen, texture, visual)

# Reject bad CAD before publishing files. STL has no units: export raw mm deliberately.
report = {'revision': SPEC['revision'], 'units': 'mm', 'parts': []}
for obj in parts:
    recalc(obj)
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bad = sum(not edge.is_manifold for edge in bm.edges)
    volume = bm.calc_volume(signed=True)
    bm.free()
    if bad or volume <= 0:
        raise RuntimeError(f'{obj.name}: nonmanifold={bad}, volume={volume}')
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.wm.stl_export(filepath=str(OUT / f'{obj.name}.stl'), export_selected_objects=True,
                          apply_modifiers=True, use_scene_unit=False, global_scale=1)
    report['parts'].append({'file': f'{obj.name}.stl', 'nonmanifold_edges': bad,
                            'volume_mm3': round(volume, 3)})

assembly = bpy.data.objects.new('VIBECOM_mm_to_meters', None)
bpy.context.collection.objects.link(assembly)
for obj in parts + visual + guides:
    obj.parent = assembly
assembly.scale = (0.001, 0.001, 0.001)
for obj in guides:
    obj.hide_render = True
bpy.ops.object.select_all(action='DESELECT')
for obj in [assembly] + parts + visual:
    obj.select_set(True)
bpy.context.view_layer.objects.active = assembly
bpy.ops.export_scene.gltf(filepath=str(OUT / 'vibecom.glb'), export_format='GLB', use_selection=True)

# Soft studio renders are generated from these meshes, never AI image stand-ins.
scene.render.engine = 'BLENDER_EEVEE'
scene.render.resolution_x = 1400
scene.render.resolution_y = 1400
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = 'PNG'
scene.render.film_transparent = False
scene.world.color = (0.42, 0.42, 0.42)
scene.view_settings.view_transform = 'AgX'
scene.view_settings.exposure = -1.2
bpy.ops.mesh.primitive_plane_add(size=200, location=(0, 0, -0.0005))
floor = bpy.context.object
floor.name = 'Studio floor (not a part)'
floor.data.materials.append(material('Studio sand', (0.68, 0.64, 0.57), 0.8))


def aim(obj, target):
    obj.rotation_euler = (Vector(target) - obj.location).to_track_quat('-Z', 'Y').to_euler()


for name, location, power, size in [('Key', (0.12, -0.18, 0.30), 12, 0.22),
                                     ('Fill', (-0.16, -0.06, 0.16), 7, 0.18),
                                     ('Rim', (0.06, 0.18, 0.25), 15, 0.16)]:
    data = bpy.data.lights.new(name, 'AREA')
    data.energy, data.shape, data.size = power, 'DISK', size
    light = bpy.data.objects.new(name, data)
    bpy.context.collection.objects.link(light)
    light.location = location
    aim(light, (0, 0, 0.07))
camera_data = bpy.data.cameras.new('Camera')
camera = bpy.data.objects.new('Camera', camera_data)
bpy.context.collection.objects.link(camera)
camera.location = (0.19, -0.30, 0.19)
camera_data.type = 'ORTHO'
camera_data.ortho_scale = 0.205
camera_data.clip_start = 0.001
camera_data.clip_end = 20
aim(camera, (0, 0, 0.067))
scene.camera = camera
scene.render.filepath = str(OUT / 'vibecom-assembled.png')
for workspace_screen in bpy.data.screens:
    for area in workspace_screen.areas:
        if area.type == 'VIEW_3D':
            space = area.spaces.active
            space.clip_start = 0.001
            space.clip_end = 10
            space.region_3d.view_location = Vector((0, 0, 0.067))
            space.region_3d.view_distance = 0.28
            space.region_3d.view_rotation = camera.rotation_euler.to_quaternion()
            space.shading.type = 'MATERIAL'
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'vibecom.blend'))
bpy.ops.render.render(write_still=True)

original = {obj: obj.location.copy() for obj in parts + visual + guides}
assembly.location.z = 0.027
for obj in [rear, front, insert, screen] + [o for o in guides if '15867' in o.name or 'DFR0030' in o.name]:
    obj.location.z += 30
rear.location.y += 23
front.location.y -= 30
insert.location.y -= 43
screen.location.y -= 35
base.location.z -= 20
neck.location.z += 10
for obj in guides:
    obj.hide_render = False
camera_data.ortho_scale = 0.255
aim(camera, (0, -0.005, 0.10))
scene.render.filepath = str(OUT / 'vibecom-exploded.png')
bpy.ops.render.render(write_still=True)
for obj, location in original.items():
    obj.location = location
assembly.location.z = 0
for obj in guides:
    obj.hide_render = True
report['source_sha256'] = {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
                            for name in ['reference.json', 'build_model.py', '../docs/eye-preview.png']}
(OUT / 'validation.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print('VIBECOM: exported 6 checked STL parts, GLB, editable BLEND and 2 previews.')
