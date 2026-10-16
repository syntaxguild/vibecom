"""Render the portfolio cover from the shipped Blender assembly (Blender 5.2)."""
import bpy
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'mechanical/exports/vibecom.blend'))
scene=bpy.context.scene
scene.render.resolution_x=1920
scene.render.resolution_y=1080
scene.cycles.samples=48
camera=scene.camera
camera.data.ortho_scale=0.24
camera.data.shift_x=-0.19
face=bpy.data.objects['LCD active area 40.8x30.6mm']
for loop in face.data.uv_layers.active.data:
    loop.uv.x += 0.2
ink=bpy.data.materials.new('Portfolio type')
ink.use_nodes=True
shader=ink.node_tree.nodes.get('Principled BSDF')
shader.inputs['Base Color'].default_value=(0.035,0.065,0.060,1)
shader.inputs['Emission Color'].default_value=(0.035,0.065,0.060,1)
shader.inputs['Emission Strength'].default_value=1
for text,size,y in [('VIBECOM',0.016,0.020),
                     ('A little presence.\nA lot of personality.',0.0056,-0.001),
                     ('ESP32  /  INTERACTIVE ROBOTICS  /  3D DESIGN',0.0026,-0.036),
                     ('v0.6.1 / Digital prototype',0.003,-0.047)]:
    data=bpy.data.curves.new('Cover typography','FONT')
    data.body=text;data.size=size;data.space_line=1.25
    obj=bpy.data.objects.new('Cover typography',data)
    bpy.context.collection.objects.link(obj)
    obj.parent=camera;obj.location=(-0.151,y,-0.20)
    obj.data.materials.append(ink)
    obj.visible_shadow=False
scene.render.filepath=str(ROOT/'docs/assets/vibecom-cover.png')
bpy.ops.render.render(write_still=True)
