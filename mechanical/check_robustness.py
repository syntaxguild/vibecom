"""Additional digital stress checks; no percentage or physical-fit certification."""
import bpy
import hashlib
import json
import math
import sys
from pathlib import Path
from mathutils import Matrix, Vector
ROOT=Path(__file__).resolve().parent
# Reuse the exact audit geometry loader and Boolean measurement, without
# executing the existing audit suite or rewriting its report.
namespace={'__file__':str(ROOT/'audit_assembly.py')}
source=(ROOT/'audit_assembly.py').read_text(encoding='utf-8')
exec(compile(source[:source.index('parts = [name')],str(ROOT/'audit_assembly.py'),'exec'),namespace)
objects=namespace['objects']; overlap=namespace['overlap']


def cylinder(name,radius,depth,center,axis='Z'):
    bpy.ops.mesh.primitive_cylinder_add(vertices=48,radius=radius,depth=depth,location=center)
    obj=bpy.context.object;obj.name=name
    if axis=='Y':obj.rotation_euler.x=math.pi/2
    bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
    return obj


pairs=[('REF_DevKitC_PCB','04_body_shell'),
       ('REF_DevKitC_header_12.3','08_electronics_tray'),
       ('REF_Pololu_3415_PCB','04_body_shell'),
       ('REF_SG90_case','04_body_shell')]
tolerance=[]
for first,second in pairs:
    obj=objects[first]
    for amount in [0.2,0.4]:
        for axis in range(3):
            for sign in [-1,1]:
                shift=[0,0,0];shift[axis]=amount*sign
                obj.location=shift
                tolerance.append({'part':first,'against':second,'shift_mm':shift,
                                  'overlap_mm3':overlap(obj,objects[second])})
                obj.location=(0,0,0)

motion=[]
pivot=Vector((0,-4,0))
for yaw in range(-60,61,10):
    for downward in [0,0.4]:
        m=Matrix.Translation(pivot+Vector((0,0,-downward)))@Matrix.Rotation(math.radians(yaw),4,'Z')@Matrix.Translation(-pivot)
        volume=0
        for name in ['01_head_rear','02_head_front','03_face_insert']:
            obj=objects[name];obj.matrix_world=m
            volume+=overlap(obj,objects['04_body_shell'])+overlap(obj,objects['07_body_rear'])
            obj.matrix_world=Matrix.Identity(4)
        motion.append({'yaw_deg':yaw,'head_downward_mm':downward,'overlap_mm3':round(volume,4)})

# Closure heads only: candidate 4 mm diameter / 2 mm high, not a selected SKU.
# Test both final space and entry along a straight screwdriver axis.
spec=json.loads((ROOT/'reference.json').read_text())
closures=[]
electronics=[obj for name,obj in objects.items() if name.startswith('REF_')]
for x in [-spec['assembly']['base_boss_x'],spec['assembly']['base_boss_x']]:
    for y in [-spec['assembly']['base_boss_y'],spec['assembly']['base_boss_y']]:
        head=cylinder('Candidate base screw head',2,2,(x,y,6.2))
        final=overlap(head,objects['05_base_cover'])
        entry=[]
        for z in [i/2 for i in range(-4,13)]+[6.2]:
            head.location.z=z
            entry.append({'head_center_z':z,'overlap_mm3':overlap(head,objects['05_base_cover'])})
        bpy.data.objects.remove(head,do_unlink=True)
        shaft=cylinder('Candidate M2x8 shaft',1,8,(x,y,11.2))
        electrical=sum(overlap(shaft,obj) for obj in electronics)
        bpy.data.objects.remove(shaft,do_unlink=True)
        closures.append({'kind':'base','xy':[x,y],'candidate_head_final_overlap_mm3':final,
                         'candidate_shaft_electronics_overlap_mm3':round(electrical,4),'head_insertion':entry})
for x in [-spec['assembly']['head_closure_x'],spec['assembly']['head_closure_x']]:
    for z in [spec['enclosure']['head_center'][2]-spec['assembly']['head_closure_z_offset'],spec['enclosure']['head_center'][2]+spec['assembly']['head_closure_z_offset']]:
        shaft=cylinder('Candidate M2x16 shaft',1,16,(x,15,z),'Y')
        electrical=sum(overlap(shaft,obj) for obj in electronics)
        bpy.data.objects.remove(shaft,do_unlink=True)
        closures.append({'kind':'head_rear','xz':[x,z],'candidate_shaft_electronics_overlap_mm3':round(electrical,4)})

# New removable tray and rear-body closures: test the declared candidate shafts
# against electronics and the candidate heads against non-threaded printed parts.
for x in [-spec['assembly']['tray_mount_x'],spec['assembly']['tray_mount_x']]:
    head=cylinder('Tray head reservation',2,2,(x,spec['assembly']['tray_mount_y'],11.3))
    head_overlap=sum(overlap(head,objects[name]) for name in ['04_body_shell','07_body_rear','08_electronics_tray'])
    electrical=sum(overlap(head,obj) for obj in electronics)
    bpy.data.objects.remove(head,do_unlink=True)
    shaft=cylinder('Tray M2x5 shaft',1,5,(x,spec['assembly']['tray_mount_y'],7.8))
    electrical+=sum(overlap(shaft,obj) for obj in electronics)
    bpy.data.objects.remove(shaft,do_unlink=True)
    closures.append({'kind':'tray','xy':[x,spec['assembly']['tray_mount_y']],
                     'candidate_head_final_overlap_mm3':round(head_overlap,4),
                     'candidate_shaft_electronics_overlap_mm3':round(electrical,4)})
for x in [-spec['assembly']['body_service_mount_x'],spec['assembly']['body_service_mount_x']]:
    z=spec['assembly']['body_service_mount_z']
    head=cylinder('Rear service head reservation',2,2,(x,24.5,z),'Y')
    head_overlap=overlap(head,objects['07_body_rear'])
    bpy.data.objects.remove(head,do_unlink=True)
    shaft=cylinder('Rear service M2x25 shaft',1,25,(x,11,z),'Y')
    electrical=sum(overlap(shaft,obj) for obj in electronics)
    bpy.data.objects.remove(shaft,do_unlink=True)
    closures.append({'kind':'body_rear','xz':[x,z],'candidate_head_final_overlap_mm3':head_overlap,
                     'candidate_shaft_electronics_overlap_mm3':round(electrical,4)})

report={'revision':spec['revision'],'blend_sha256':hashlib.sha256((ROOT/'exports/vibecom.blend').read_bytes()).hexdigest(),
        'script_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'audit_loader_sha256':hashlib.sha256((ROOT/'audit_assembly.py').read_bytes()).hexdigest(),
        'relative_position_stress':tolerance,'head_motion':motion,'closure_screws':closures,
        'limits':['Axis shifts are sensitivity scenarios, not measured printer error distributions',
                  'Twelve candidate closure positions; LCD/servo/adapter nuts and actual horn remain unmodelled',
                  'No wire, torque, wear, heat or touch sensitivity testing',
                  'Passing test counts are not a probability of successful assembly']}
# Pushing the ToF PCB forwards beyond its front seat is a deliberate hard-stop
# test. It is not an allowed assembly position or a simulated printing error.
for row in tolerance:
    row['intentional_front_stop_contact'] = (row['part']=='REF_Pololu_3415_PCB' and row['shift_mm']==[0,-0.4,0])
report['digital_checks_passed'] = (
    all(row['overlap_mm3']<.01 or row['intentional_front_stop_contact'] for row in tolerance)
    and all(row['overlap_mm3']<.01 for row in motion)
    and all(row['candidate_shaft_electronics_overlap_mm3']<.01 for row in closures)
    and all(row.get('candidate_head_final_overlap_mm3',0)<.01 for row in closures)
    and all(pose['overlap_mm3']<.01 for row in closures for pose in row.get('head_insertion',[])))
(ROOT/'audit/robustness.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print('POSITION STRESS FAILURES',[(r['part'],r['shift_mm'],r['overlap_mm3']) for r in tolerance if r['overlap_mm3']>=.01])
print('HEAD MOTION FAILURES',[r for r in motion if r['overlap_mm3']>=.01])
print('CLOSURES',len(closures),'candidate closure positions checked; details in robustness.json')

if '--strict' in sys.argv and not report['digital_checks_passed']:
    raise RuntimeError('Additional digital checks failed; inspect robustness.json')
