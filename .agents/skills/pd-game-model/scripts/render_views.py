"""Render labelled review views of a model for the pd-game-model skill.

Loads a .glb/.gltf (Tripo output in metres, or a PD glTF in model units: the
camera fits the model's bounds either way) in a headless Blender and writes one
PNG per view. Three looks:
  flat     - each material's base colour, no lighting: how the game draws glTF
             models (vertex colours with the shading baked in)
  studio   - Workbench studio lighting with textures, for reviewing Tripo output
  clay     - studio lighting in one grey: the shape alone (creases, silhouette)

Axes: glTF +Z is the model's front, +Y up, +X its left; Blender imports glTF
+Z as -Y and +Y as +Z, so the front faces Blender -Y.

usage: blender -b --factory-startup --python render_views.py -- <model> <out dir> [flat|studio|clay] [views]
views: comma list of front,rear,left,right,top,bottom,three_quarter,rear_three_quarter
       (default: all)
"""
import os
import sys

import bpy
from mathutils import Vector

VIEWS = {
    "front": (0.0, -1.0, 0.0),
    "rear": (0.0, 1.0, 0.0),
    "left": (1.0, 0.0, 0.0),
    "right": (-1.0, 0.0, 0.0),
    "top": (0.0, 0.0, 1.0),
    "bottom": (0.0, 0.0, -1.0),
    "three_quarter": (0.75, -1.0, 0.45),
    "rear_three_quarter": (-0.8, 1.0, 0.55),
}


def main():
    argv = sys.argv[sys.argv.index("--") + 1:]
    model, out_dir = argv[0], argv[1]
    look = argv[2] if len(argv) > 2 else "flat"
    wanted = argv[3].split(",") if len(argv) > 3 else list(VIEWS)
    os.makedirs(out_dir, exist_ok=True)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=model)
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    bpy.context.view_layer.update()

    for mat in bpy.data.materials:
        if mat.node_tree:
            for node in mat.node_tree.nodes:
                if node.type == "BSDF_PRINCIPLED":
                    c = node.inputs["Base Color"].default_value
                    mat.diffuse_color = (c[0], c[1], c[2], 1.0)

    lo = Vector((1e18, 1e18, 1e18))
    hi = Vector((-1e18, -1e18, -1e18))
    for obj in meshes:
        for corner in obj.bound_box:
            w = obj.matrix_world @ Vector(corner)
            lo = Vector((min(lo[i], w[i]) for i in range(3)))
            hi = Vector((max(hi[i], w[i]) for i in range(3)))
    centre = (lo + hi) / 2
    size = hi - lo
    span = max(size)
    print("bounds:", tuple(round(v, 4) for v in lo), tuple(round(v, 4) for v in hi))

    scene = bpy.context.scene
    try:
        scene.render.engine = "BLENDER_WORKBENCH"
    except TypeError as error:
        print("engine:", error)
    shading = scene.display.shading
    choices = {
        "flat": (("light", "FLAT"), ("color_type", "MATERIAL")),
        "studio": (("light", "STUDIO"), ("color_type", "TEXTURE")),
        "clay": (("light", "STUDIO"), ("color_type", "SINGLE")),
    }[look]
    for attr, value in choices + (("show_backface_culling", False),):
        try:
            setattr(shading, attr, value)
        except (TypeError, AttributeError) as error:
            print(attr, error)
    try:
        scene.display.render_aa = "16"
    except (TypeError, AttributeError):
        pass
    world = bpy.data.worlds.new("bg")
    world.color = (0.10, 0.11, 0.13)
    scene.world = world
    scene.render.resolution_x = 1600
    scene.render.resolution_y = 1000

    target = bpy.data.objects.new("target", None)
    scene.collection.objects.link(target)
    target.location = centre
    cam_data = bpy.data.cameras.new("cam")
    cam_data.lens = 50
    cam_data.clip_start = span * 0.01
    cam_data.clip_end = span * 50
    cam = bpy.data.objects.new("cam", cam_data)
    scene.collection.objects.link(cam)
    scene.camera = cam
    track = cam.constraints.new(type="TRACK_TO")
    track.target = target
    track.track_axis = "TRACK_NEGATIVE_Z"
    track.up_axis = "UP_Y"

    for name in wanted:
        d = Vector(VIEWS[name]).normalized()
        # Straight up or down: keep the model's front at the top of the image.
        track.up_axis = "UP_Y"
        cam.location = centre + d * span * 2.3
        if name in ("top", "bottom"):
            cam.location += Vector((0.0, span * 0.001, 0.0))
        scene.render.filepath = os.path.join(out_dir, name + ".png")
        bpy.ops.render.render(write_still=True)
        print("wrote", scene.render.filepath)


main()
