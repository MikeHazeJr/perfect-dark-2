"""A level drawn in Blender, for tools/build_level_from_model.py.

Run with (a background Blender, so no open file is touched):
    blender --background --factory-startup --python blender_arena_example.py -- OUT.glb
then:
    python tools/build_level_from_model.py OUT.glb --id blender_room --name "Blender Room" --out DIR

It draws a small arena the way a level author would in Blender (Z up,
metres) and exports it with Blender's own glTF exporter (Y up, the
default): a floor, four walls facing in, a ceiling facing down (drawn, not
collided with), a platform that is a unit cube placed and scaled by its
object, a ramp and a mirrored copy of it (an object scaled -1 on X), a
banner named "nocollide_banner" (drawn, not collided with), a textured
material (a packed image, exported as PNG) and a plain one, and eight empties
named start_1 to start_8 on a ring, each turned so that its -Y axis (the way
an object faces in Blender's front view) points at the middle.

Checked 2026-11-17 with Blender 5.2.2: the builder made 24 collision
triangles (8 floors: the floor, the platform's top and both ramps), left out
the ceiling and the banner, kept both materials, set the start on the
platform at 150 (its top is at 100), and every start faced the middle.
"""
import math
import sys

import bmesh
import bpy

out = sys.argv[sys.argv.index("--") + 1]

# An empty scene (factory startup has a cube, a camera and a light).
for obj in list(bpy.data.objects):
    bpy.data.objects.remove(obj, do_unlink=True)

H, TALL, LIFT, RING = 10.0, 4.0, 1.0, 7.0


def mesh_object(name, quads, material, location=(0.0, 0.0, 0.0), scale=(1.0, 1.0, 1.0)):
    """Quads given counter-clockwise seen from the side they face."""
    mesh = bpy.data.meshes.new(name)
    bm = bmesh.new()
    for quad in quads:
        verts = [bm.verts.new(p) for p in quad]
        bm.faces.new(verts)
    bm.to_mesh(mesh)
    bm.free()
    mesh.uv_layers.new(name="UVMap")
    # Texture coordinates from the position (planar), so the texture repeats.
    uv = mesh.uv_layers.active.data
    for poly in mesh.polygons:
        n = poly.normal
        for li in poly.loop_indices:
            co = mesh.vertices[mesh.loops[li].vertex_index].co
            if abs(n.z) >= max(abs(n.x), abs(n.y)):
                uv[li].uv = (co.x * 0.5, co.y * 0.5)
            elif abs(n.x) >= abs(n.y):
                uv[li].uv = (co.y * 0.5, co.z * 0.5)
            else:
                uv[li].uv = (co.x * 0.5, co.z * 0.5)
    mesh.materials.append(material)
    obj = bpy.data.objects.new(name, mesh)
    obj.location = location
    obj.scale = scale
    bpy.context.scene.collection.objects.link(obj)
    return obj


def textured_material(name):
    image = bpy.data.images.new(name + "_image", 16, 16)
    pixels = []
    for y in range(16):
        for x in range(16):
            edge = min(x, y, 15 - x, 15 - y)
            v = 0.45 if edge < 1 else 0.8
            pixels += [v, v, v * 1.05, 1.0]
    image.pixels = pixels
    image.pack()
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = next(n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    tex = mat.node_tree.nodes.new("ShaderNodeTexImage")
    tex.image = image
    mat.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    return mat


def plain_material(name, rgba):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = next(n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf.inputs["Base Color"].default_value = rgba
    return mat


tiles = textured_material("Tiles")
trim = plain_material("Trim", (0.7, 0.3, 0.2, 1.0))

# Floor (faces +Z), four walls facing in, a ceiling facing down.
floor = [((-H, -H, 0), (H, -H, 0), (H, H, 0), (-H, H, 0))]
walls = [
    ((-H, H, 0), (H, H, 0), (H, H, TALL), (-H, H, TALL)),      # north wall at +Y, faces -Y
    ((H, -H, 0), (-H, -H, 0), (-H, -H, TALL), (H, -H, TALL)),  # south wall at -Y, faces +Y
    ((-H, -H, 0), (-H, H, 0), (-H, H, TALL), (-H, -H, TALL)),  # west wall at -X, faces +X
    ((H, H, 0), (H, -H, 0), (H, -H, TALL), (H, H, TALL)),      # east wall at +X, faces -X
]
# Fix each wall's winding to face the middle.
fixed = []
for q in walls:
    a, b, c = q[0], q[1], q[2]
    u = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
    v = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
    n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
    mid = ((a[0] + c[0]) / 2, (a[1] + c[1]) / 2)
    if n[0] * -mid[0] + n[1] * -mid[1] < 0:
        q = (q[0], q[3], q[2], q[1])
    fixed.append(q)
ceiling = [((-H, -H, TALL), (-H, H, TALL), (H, H, TALL), (H, -H, TALL))]
mesh_object("arena", floor + fixed, tiles)
mesh_object("ceiling", ceiling, trim)

# The platform: a unit cube's top and sides, placed and scaled by the object.
cube = [
    ((-0.5, -0.5, 0.5), (0.5, -0.5, 0.5), (0.5, 0.5, 0.5), (-0.5, 0.5, 0.5)),      # top, +Z
    ((-0.5, -0.5, -0.5), (0.5, -0.5, -0.5), (0.5, -0.5, 0.5), (-0.5, -0.5, 0.5)),  # -Y side
    ((0.5, 0.5, -0.5), (-0.5, 0.5, -0.5), (-0.5, 0.5, 0.5), (0.5, 0.5, 0.5)),      # +Y side
    ((0.5, -0.5, -0.5), (0.5, 0.5, -0.5), (0.5, 0.5, 0.5), (0.5, -0.5, 0.5)),      # +X side
    ((-0.5, 0.5, -0.5), (-0.5, -0.5, -0.5), (-0.5, -0.5, 0.5), (-0.5, 0.5, 0.5)),  # -X side
]
mesh_object("platform", cube, tiles, location=(6.5, 0.0, LIFT / 2.0), scale=(3.0, 4.0, LIFT))
# A ramp from x 2 up to the platform's edge at x 5, and the same mirrored to the west.
ramp = [((2.0, -2.0, 0.0), (5.0, -2.0, LIFT), (5.0, 2.0, LIFT), (2.0, 2.0, 0.0))]
mesh_object("ramp", ramp, tiles)
mesh_object("ramp_mirrored", ramp, tiles, scale=(-1.0, 1.0, 1.0))
# A banner along the north wall that is drawn and not collided with.
banner = [((-3.0, H - 0.05, 2.0), (-3.0, H - 0.05, 3.0), (3.0, H - 0.05, 3.0), (3.0, H - 0.05, 2.0))]
mesh_object("nocollide_banner", banner, trim)

# Eight starts on a ring, each turned so its -Y axis (Blender's front) faces the middle.
for k in range(8):
    angle = 2.0 * math.pi * k / 8
    x, y = RING * math.cos(angle), RING * math.sin(angle)
    z = (LIFT if k == 0 else 0.0) + 0.1        # start_1 is on the platform (x 5..8)
    empty = bpy.data.objects.new("start_%d" % (k + 1), None)
    empty.empty_display_type = "SINGLE_ARROW"
    empty.location = (x, y, z)
    empty.rotation_euler = (0.0, 0.0, math.atan2(-x, y))
    bpy.context.scene.collection.objects.link(empty)

bpy.ops.export_scene.gltf(filepath=out, export_format="GLB", export_yup=True, export_apply=True)
print("BLENDER ARENA: exported %s with %d objects" % (out, len(bpy.data.objects)))
