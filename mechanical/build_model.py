"""Blender 5.2: editable Vibecom enclosure, STL parts, GLB and studio previews.

Run: blender --background --factory-startup --python mechanical/build_model.py
All CAD construction coordinates are millimeters; the saved assembly/GLB uses meters.
"""
import bpy
import bmesh
import hashlib
import json
import math
import sys
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parent
SPEC = json.loads((ROOT / 'reference.json').read_text(encoding='utf-8'))
E = SPEC['enclosure']
A = SPEC['assembly']
CHECK_ONLY = '--check-only' in sys.argv
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
    if name == 'Continuous black visor':
        shader.inputs['Specular IOR Level'].default_value = 0.2
    return mat


ivory = material('Warm ivory polymer', (0.72, 0.65, 0.53))
graphite = material('Continuous black visor', (0, 0, 0), 0.6)
rubber = material('Terracotta feet', (0.40, 0.19, 0.12), 0.65)
# Shared shell material makes the character read as one soft form.
blue = material('Reference servo blue', (0.025, 0.16, 0.48))
green = material('Reference PCB', (0.025, 0.20, 0.10))
optical = material('ToF optical windows', (0.045, 0.025, 0.055), 0.18)


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


def softbox(name, dims, pos, radius):
    bpy.ops.mesh.primitive_cube_add(size=1, location=pos)
    obj = bpy.context.object
    obj.name = name
    obj.dimensions = dims
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    bevel = obj.modifiers.new('Soft molded corners', 'BEVEL')
    bevel.width = radius
    bevel.segments = 12
    bpy.ops.object.modifier_apply(modifier=bevel.name)
    return obj


def paw(name, pos):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=48, ring_count=24, radius=1, location=pos)
    obj = bpy.context.object
    obj.name = name
    obj.scale = (10, 18, 5)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
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
        polygon.use_smooth = True
    smooth_edges(obj)
    group.append(obj)
    return obj


def smooth_edges(obj):
    # Preserve flat caps/rims while smoothing only the molded curves.
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bm.normal_update()
    for face in bm.faces:
        face.smooth = True
    for edge in bm.edges:
        edge.smooth = edge.is_manifold and edge.calc_face_angle() < math.radians(35)
    bm.to_mesh(obj.data)
    bm.free()


def clean_solid(obj):
    # Exact booleans can leave opposite, coincident triangles at a cut seam.
    # Remove only duplicate interface faces, then weld their shared boundary.
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.triangulate(bm, faces=list(bm.faces))
    bm.normal_update()
    by_position = {}
    for face in bm.faces:
        key = tuple(sorted(tuple(vertex.co) for vertex in face.verts))
        by_position.setdefault(key, []).append(face)
    duplicates = []
    for faces in by_position.values():
        if len(faces) > 1:
            duplicates.extend(faces if faces[0].normal.dot(faces[1].normal) < -0.99 else faces[1:])
    if duplicates:
        bmesh.ops.delete(bm, geom=duplicates, context='FACES_ONLY')
    bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=0.00001)
    loose = [edge for edge in bm.edges if not edge.link_faces]
    if loose:
        bmesh.ops.delete(bm, geom=loose, context='EDGES')
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    bm.to_mesh(obj.data)
    bm.free()
    smooth_edges(obj)


# Rounded companion: a larger pillow head, low bean body and two little paws.
hw, hd, hh = E['head']
hz = E['head_center'][2]
lz = hz + E['display_offset_z']
wall = E['wall']
hr = E['head_radius']
front_y, split_y = -hd/2, 12
head_bottom = hz-hh/2
body_top = E['body_center'][2]+E['body'][2]/2
joint_top = 54
mount_top = joint_top+wall
assert abs(head_bottom-body_top-E['head_body_gap']) < 1e-6
assert E['head_body_gap'] > 0
assert mount_top < lz-SPEC['components']['display']['pcb'][1]/2


def head_form(name, dims, radius, edge_radius):
    # A rounded frontal silhouette with a shallow edge roll keeps the chin
    # low across the front, hiding the rotating joint instead of exposing it.
    obj = rounded(name, dims, (0, 0, hz), radius)
    bpy.context.view_layer.objects.active = obj
    bevel = obj.modifiers.new('Rolled shell edge', 'BEVEL')
    bevel.width = edge_radius
    bevel.segments = 6
    bevel.limit_method = 'ANGLE'
    bpy.ops.object.modifier_apply(modifier=bevel.name)
    return obj


def hollow_head(name):
    obj = head_form(name, (hw, hd, hh), hr, 3)
    boolean(obj, head_form('Head cavity', (hw-2*wall, hd-2*wall, hh-2*wall),
                           hr-wall, 1))
    return obj


# Service seam and screws move to the back, leaving the character's face clean.
rear = hollow_head('01_head_rear')
boolean(rear, rounded('Remove front half', (200, 112, 200), (0, -44, hz), 0.01))
for x in [-31, 31]:
    for z in [hz-20, hz+20]:
        boolean(rear, cylinder('Rear screw clearance', E['closure_hole_diameter']/2,
                               26, (x, 18, z), 'Y'))
finish(rear, ivory)
front = hollow_head('02_head_front')
boolean(front, rounded('Open service rear', (200, 100, 200), (0, 62, hz), 0.01))
boolean(front, rounded('LCD opening', (42, 12, 33), (0, front_y, lz), 2))
# Recess the existing horn adapter into the lowered head. Only a running seam
# remains above the torso; the head's entire underside clears it during yaw.
boolean(front, cylinder('Recessed joint pocket', 14.5, 16, (0, -4, head_bottom)))
seat = cylinder('Internal joint seat', 17.5, mount_top-head_bottom,
                (0, -4, (mount_top+head_bottom)/2))
boolean(seat, cylinder('Seat clearance', 14.5, 24, (0, -4, 51)))
boolean(seat, rounded('Seat rear service clearance', (200, 100, 200),
                      (0, 61.8, hz), 0.01))
# Trim just the new seat; intersecting the whole shell with its coincident
# exterior would create ambiguous coplanar surfaces in the boolean solver.
boolean(seat, head_form('Keep mounting seat inside head', (hw, hd, hh), hr, 3), 'INTERSECT')
boolean(front, seat, 'UNION')
for x in [-10, 10]:
    boolean(front, rounded('Internal mounting bridge', (12, 6, wall),
                           (math.copysign(12, x), -4, joint_top+wall/2), 1, 'XY'), 'UNION')
    boolean(front, cylinder('Head adapter fastener', 1.2, 10, (x, -4, joint_top)))
boolean(front, cylinder('Cable clearance in mounting bridges', E['neck_bore']/2+0.4,
                        20, (0, -4, 51)))
# Touch board sits under a 1 mm plastic roof; no mechanical button is required.
boolean(front, rounded('Touch roof relief', (32, 24, 5),
                       (0, 0, hz+hh/2-E['touch_roof']-2.5), 3, 'XY'))
for x in [-31, 31]:
    for z in [hz-20, hz+20]:
        boolean(front, cylinder('Hidden closure boss', 4, 8, (x, 8, z), 'Y'), 'UNION')
        boolean(front, cylinder('Hidden pilot', E['closure_pilot_diameter']/2,
                                12, (x, 8, z), 'Y'))
lcd_pitch = SPEC['components']['display']['mount_pitch']
for x in [-lcd_pitch[0]/2, lcd_pitch[0]/2]:
    for z in [lz-lcd_pitch[1]/2, lz+lcd_pitch[1]/2]:
        boolean(front, cylinder('LCD standoff', 2.8, 3.4, (x, -19.4, z), 'Y'), 'UNION')
        boolean(front, cylinder('LCD pilot', 0.9, 8, (x, -19.4, z), 'Y'))
# Backlight pocket behind the smaller optical opening, leaving a front lip.
boolean(front, rounded('LCD backlight clearance', (48.63, 4.4, 35.4),
                       (0, -19.7, lz), 0.6))
finish(front, ivory)
insert = rounded('03_face_insert', (66, 0.8, 44), (0, front_y-0.4, lz), 12)
# Slight overlap masks the edge of the real 40.8 x 30.6 mm active area.
boolean(insert, rounded('Seamless visor window', (40.4, 6, 30.2), (0, front_y, lz), 1.5))
finish(insert, graphite)

bw, bd, bh = E['body']
bz = E['body_center'][2]
br = E['body_radius']
body = softbox('04_body_shell', (bw, bd, bh), (0, 0, bz), br)
boolean(body, softbox('Body cavity', (bw-2*wall, bd-2*wall, bh-2*wall),
                     (0, 0, bz), br-wall))
boolean(body, rounded('Servo insertion slot', (24.2, 13.4, 14), (-5.5, -4, 46), 1, 'XY'))
boolean(body, cylinder('Concealed joint cable port', 3, 12, (12, -4, 46)))
# Integral spacers meet the top of the lowered servo tabs at Z=41.6.
for x in A['servo_mount_x']:
    boolean(body, rounded('Servo tab roof spacer', (5.8, 7, 2.4),
                           (x, -4, 42.8), 0.6, 'XY'), 'UNION')
    boolean(body, rounded('Adjustable servo roof slot', (3.4, 2.4, 14),
                           (x, -4, 44), 1, 'XY'))
# Keep a 0.4 mm case-height envelope clear of the spacer's inner edges.
boolean(body, rounded('Servo case tolerance relief', (23.8, 13, 29.8),
                       (-5.5,-4,A['servo_case_z']), 1, 'XY'))
# Small open optical port: no printed grille or uncalibrated dark cover glass.
tof_z = E['tof_center_z']
pw, ph, pt = SPEC['components']['distance']['pcb']
boolean(body, rounded('ToF optical aperture', (E['tof_aperture'][0], 14, E['tof_aperture'][1]),
                       (0, -bd/2, tof_z), 1.5))
for x in [-7.2, 7.2]:
    boolean(body, rounded('ToF edge guide', (3, 5.5, 21.2), (x, -26, 26.6), 0.5), 'UNION')
    # Open towards the interior: raise the board through the base, then seat
    # it forwards. A captive vertical slot would obstruct this insertion path.
    boolean(body, rounded('ToF PCB edge channel', (1.5, 12, ph+0.8),
                           (math.copysign(6, x), -20.75, tof_z), 0.1))
# The DevKitC is now upright at the back; USB exits the right flank.
boolean(body, rounded('Side USB service opening', (14, 14, 11), (bw/2, 16, 25), 2))
for x in [-A['base_boss_x'], A['base_boss_x']]:
    for y in [-A['base_boss_y'], A['base_boss_y']]:
        rib = rounded('Internal closure rib', (5, 6, 7.5),
                      (x, math.copysign(24, y), 14.25), 0.5, 'XY')
        boolean(body,rib,'UNION')
        boolean(body, cylinder('Base closure boss', 3.5, 7.5, (x,y,14.25)), 'UNION')
        boolean(body, cylinder('Base locating pad clearance', 3.3, 4, (x,y,8.5)))
        boolean(body, cylinder('Base pilot', E['closure_pilot_diameter']/2, 12, (x, y, 13)))
boolean(body, rounded('Open base', (200, 200, 200), (0, 0, -92), 0.01, 'XY'))
finish(body, ivory)
base = softbox('05_base_cover', (bw, bd, bh), (0, 0, bz), br)
boolean(base, rounded('Keep base cap only', (200, 200, 200), (0, 0, 108), 0.01, 'XY'))
for x in [-13, 13]:
    boolean(base, paw('Little paw', (x, -17, 4)), 'UNION')
boolean(base, rounded('Flat soles', (200, 200, 100), (0, 0, -50), 0.01, 'XY'))
boolean(base, rounded('Keep paw tops below base seam', (200, 200, 200),
                       (0, 0, 108), 0.01, 'XY'))
# Mating pads keep each relocated screw supported below the rounded body rim.
for x in [-A['base_boss_x'], A['base_boss_x']]:
    for y in [-A['base_boss_y'], A['base_boss_y']]:
        pad = cylinder('Base fastener pad',3,2.4,(x,y,9))
        boolean(base,pad,'UNION')
        boolean(base, cylinder('Base screw head counterbore', 2.2, 20,
                               (x, y, -2.8)))
for x in [-A['base_boss_x'], A['base_boss_x']]:
    for y in [-A['base_boss_y'], A['base_boss_y']]:
        boolean(base, cylinder('Base screw clearance', E['closure_hole_diameter']/2,
                               18, (x, y, 5)))
for x in [-12, 12]:
    cradle = rounded('Upright PCB cradle', (8, 8, 6), (x, 16, 10), 1, 'XY')
    for boss_x in [-A['base_boss_x'], A['base_boss_x']]:
        boolean(cradle, cylinder('Rear closure clearance', 3.8, 10,
                                 (boss_x, A['base_boss_y'], 14.25)))
    boolean(base, cradle, 'UNION')
    boolean(base, rounded('PCB edge slot', (10, 2.4, 4), (x, 16, 12), 0.5, 'XY'))
# Reserve room for the lower pin header instead of burying it in the cradles.
boolean(base, rounded('Lower header and socket clearance', (49.06, 9.1, 3.94),
                       (0, 10.85, 12.3), 0.1))
# A short L-shaped support holds the ToF PCB vertically when the base is fitted.
boolean(base, rounded('ToF support upright', (8, 7, 9), (0, -16.5, 11.5), 0.5, 'XY'), 'UNION')
boolean(base, rounded('ToF support ledge', (10, 11, 2.4),
                       (0, -21, tof_z-ph/2-1.2), 0.4, 'XY'), 'UNION')
finish(base, rubber)
# Preserve the exported filename; this part is now concealed in the head.
neck = cylinder('06_neck_horn_adapter', 9, 8, (0, -4, 50))
boolean(neck, cylinder('Adapter hollow', E['neck_bore']/2, 12, (0, -4, 50)))
boolean(neck, cylinder('Adapter flange', 14, 3, (0, -4, 52.5)), 'UNION')
boolean(neck, cylinder('Cable bore through flange', E['neck_bore']/2, 14, (0, -4, 50)))
boolean(neck, cylinder('Horn attachment floor', 9, 3, (0, -4, 47.5)), 'UNION')
boolean(neck, cylinder('Original horn center screw', 1.6, 6, (0, -4, 47.5)))
for x in [-6.5, 6.5]:
    boolean(neck, rounded('Horn screw slot', (2.4, 4, 7), (x, -4, 47.5), 1, 'XY'))
boolean(neck, rounded('Side cable entrance', (8, 7, 4), (8, -4, 50), 1))
for x in [-10, 10]:
    boolean(neck, cylinder('Head adapter screw', 1.2, 7, (x, -4, 52.5)))
finish(neck, graphite)

# Purchased components are reference envelopes, excluded from the printable STLs.
finish(rounded('REF_Waveshare_17344_PCB', (58, 1.6, 35), (0, -16.9, lz), 1), green, guides)
finish(rounded('REF_SG90_case', (23, 12.2, 29), (-5.5, -4, A['servo_case_z']), 1, 'XY'), blue, guides)
finish(rounded('REF_SG90_tabs', (32.3, 12.2, 2.5), (-5.5, -4, A['servo_tab_z']), 1, 'XY'), blue, guides)
controller = rounded('REF_DevKitC_PCB', (48.26, 1.6, 27.94), (0, 16, 25), 1)
boolean(controller, rounded('RF antenna extension', (6.24, 1.6, 18),
                             (-27.05, 16, 25), 0.1), 'UNION')
finish(controller, green, guides)
for z in [12.3, 37.7]:
    finish(rounded('REF_DevKitC_header_' + str(z), (48.26, 8.5, 2.54),
                   (0, 10.95, z), 0.1), graphite, guides)
finish(rounded('REF_Waveshare_17344_connector_reservation', (8, 8, 20),
               (-25, -12.1, lz), 0.3), graphite, guides)
finish(rounded('REF_DFR0030_connector_reservation', (12, 8, 8),
               (0, 7, 93), 0.3), graphite, guides)
finish(rounded('REF_DevKitC_components', (40, 6, 24), (0, 12, 25), 1), blue, guides)
finish(rounded('REF_DFR0030_touch', (30, 22, 1.6), (0, 0, hz+hh/2-1.8), 1, 'XY'), green, guides)
finish(rounded('REF_Pololu_3415_PCB', (pw, pt, ph), (0, -26, tof_z), 0.3), green, guides)
finish(rounded('ToF sensor package reference', (4.9, 1.2, 2.5),
               (SPEC['components']['distance']['sensor_offset_x'], -26-pt/2-0.6, tof_z), 0.25), graphite, visual)
for x in [-1.25, 1.25]:
    finish(rounded('ToF optical window reference', (1.3, 0.05, 1.6),
                   (x+SPEC['components']['distance']['sensor_offset_x'], -26-pt/2-1.225, tof_z), 0.25), optical, visual)
touch_marker = rounded('Soft touch marker visual', (8, 3, 0.15),
                       (0, 0, hz+hh/2+0.075), 1.4, 'XY')
finish(touch_marker, ivory, visual)

# The face is a texture of the firmware rasterizer, at the actual 40.8 x 30.6 mm active size.
screen = rounded('LCD active area 40.8x30.6mm', (40.8, 0.05, 30.6), (0, front_y-0.72, lz), 0.1)
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
shader.inputs['Roughness'].default_value = 0.6
shader.inputs['Specular IOR Level'].default_value = 0.2
uv = screen.data.uv_layers.new(name='Firmware preview first pose')
for poly in screen.data.polygons:
    for index in poly.loop_indices:
        vertex = screen.data.vertices[screen.data.loops[index].vertex_index].co
        uv.data[index].uv = ((vertex.x / 40.8 + 0.5) / 5,
                             (vertex.z / 30.6 + 0.5) * 240 / 274)
finish(screen, texture, visual)

# Reject bad CAD before publishing files. STL has no units: export raw mm deliberately.
report = {'revision': SPEC['revision'], 'units': 'mm', 'parts': [],
          'joint': {'head_body_gap_mm': round(head_bottom-body_top, 3),
                    'adapter_flange_top_mm': joint_top,
                    'internal_mount_top_mm': mount_top,
                    'display_pcb_bottom_mm': lz-SPEC['components']['display']['pcb'][1]/2}}
for obj in parts:
    clean_solid(obj)
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
    if not CHECK_ONLY:
        bpy.ops.wm.stl_export(filepath=str(OUT / f'{obj.name}.stl'), export_selected_objects=True,
                              apply_modifiers=True, use_scene_unit=False, global_scale=1)
    report['parts'].append({'file': f'{obj.name}.stl', 'nonmanifold_edges': bad,
                            'volume_mm3': round(volume, 3)})

# Check mounts and mating parts at construction scale (mm), before parent scaling.
report['fit'] = []
tof_pcb = next(obj for obj in guides if obj.name == 'REF_Pololu_3415_PCB')
for first, second in [(body, base), (body, tof_pcb), (base, tof_pcb)]:
    probe = first.copy()
    probe.data = first.data.copy()
    bpy.context.collection.objects.link(probe)
    cutter = second.copy()
    cutter.data = second.data.copy()
    bpy.context.collection.objects.link(cutter)
    boolean(probe, cutter, 'INTERSECT')
    bm = bmesh.new()
    bm.from_mesh(probe.data)
    overlap = abs(bm.calc_volume(signed=True))
    bm.free()
    bpy.data.objects.remove(probe, do_unlink=True)
    if overlap >= 0.01:
        raise RuntimeError(f'{first.name}/{second.name}: overlap={overlap} mm3')
    report['fit'].append({'parts': [first.name, second.name], 'overlap_mm3': round(overlap, 6)})

# Conservatively probe +/-13.5 degrees in both axes from the package corners.
# This verifies the provisional aperture, not the real lens/header geometry.
blocked = 0
for x in [-2.45, 2.45]:
    for z in [tof_z-1.25, tof_z+1.25]:
        for dx in [-1, 0, 1]:
            for dz in [-1, 0, 1]:
                direction = Vector((dx*math.tan(math.radians(13.5)), -1,
                                    dz*math.tan(math.radians(13.5)))).normalized()
                origin = body.matrix_world.inverted() @ Vector((x+SPEC['components']['distance']['sensor_offset_x'], -26-pt/2-1.225, z))
                blocked += body.ray_cast(origin, direction, distance=10)[0]
if blocked:
    raise RuntimeError(f'ToF aperture blocks {blocked}/36 reference rays')
report['tof_aperture'] = {'clear_reference_rays': 36, 'aperture_mm': E['tof_aperture']}

if CHECK_ONLY:
    print(json.dumps(report, indent=2))
    sys.exit(0)

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
scene.render.engine = 'CYCLES'
scene.cycles.samples = 32
scene.cycles.use_denoising = True
scene.render.resolution_x = 1400
scene.render.resolution_y = 1400
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = 'PNG'
scene.render.film_transparent = False
scene.world.color = (0.42, 0.42, 0.42)
scene.view_settings.view_transform = 'AgX'
scene.view_settings.exposure = -0.8
bpy.ops.mesh.primitive_plane_add(size=200, location=(0, 0, -0.0005))
floor = bpy.context.object
floor.name = 'Studio floor (not a part)'
floor.data.materials.append(material('Studio eucalyptus', (0.24, 0.30, 0.29), 0.8))


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
camera.location = (0.12, -0.36, 0.13)
camera_data.type = 'ORTHO'
camera_data.ortho_scale = 0.155
camera_data.clip_start = 0.001
camera_data.clip_end = 20
aim(camera, (0, 0, (hz+hh/2)/2000))
scene.camera = camera
scene.render.filepath = str(OUT / 'vibecom-assembled.png')
for workspace_screen in bpy.data.screens:
    for area in workspace_screen.areas:
        if area.type == 'VIEW_3D':
            space = area.spaces.active
            space.clip_start = 0.001
            space.clip_end = 10
            space.region_3d.view_location = Vector((0, 0, (hz+hh/2)/2000))
            space.region_3d.view_distance = 0.28
            space.region_3d.view_rotation = camera.rotation_euler.to_quaternion()
            space.shading.type = 'MATERIAL'
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'vibecom.blend'))
bpy.ops.render.render(write_still=True)

# Show the same enclosure with the firmware's Happy pose as a second expression.
idle_uvs = [loop.uv.copy() for loop in screen.data.uv_layers.active.data]
for index, coordinate in enumerate(idle_uvs):
    screen.data.uv_layers.active.data[index].uv = (coordinate.x + 0.2, coordinate.y)
screen.data.update()
scene.render.filepath = str(OUT / 'vibecom-happy.png')
bpy.ops.render.render(write_still=True)
for index, coordinate in enumerate(idle_uvs):
    screen.data.uv_layers.active.data[index].uv = coordinate
screen.data.update()

original = {obj: obj.location.copy() for obj in parts + visual + guides}
assembly.location.z = 0.027
for obj in [rear, front, insert, screen, touch_marker] + [o for o in guides if '17344' in o.name or 'DFR0030' in o.name]:
    obj.location.z += 30
rear.location.y += 23
front.location.y -= 12
insert.location.y -= 34
screen.location.y -= 34
base.location.z -= 20
neck.location.z += 10
for obj in guides:
    obj.hide_render = False
camera_data.ortho_scale = 0.225
aim(camera, (0, -0.005, 0.083))
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
print('VIBECOM: exported 6 checked STL parts, GLB, editable BLEND and 3 previews.')
