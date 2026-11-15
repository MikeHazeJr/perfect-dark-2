"""Run Mike's October vehicle builder in a headless Blender and dump its geometry.

Read only towards the builder: it is executed from memory, bytecode writing is
off, nothing is saved, rendered or written next to it. Output: one JSON file
with, per mesh object, its world-space triangles and each triangle's material
name and base colour, plus every empty (helper) with its world location and
parent.

usage: blender -b --factory-startup --python blender_dump_vehicle.py -- <builder> <out.json>
"""
import json
import sys

sys.dont_write_bytecode = True

import importlib.util  # noqa: E402

import bpy  # noqa: E402


def main():
    argv = sys.argv[sys.argv.index("--") + 1:]
    builder_path, out_path = argv[0], argv[1]

    spec = importlib.util.spec_from_file_location("pd_vehicle_builder_v5_in_memory", builder_path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    scene = bpy.context.scene
    collection = bpy.data.collections.new("maian_vehicle_dump")
    scene.collection.children.link(collection)
    result = module.build(scene, collection)

    depsgraph = bpy.context.evaluated_depsgraph_get()
    depsgraph.update()

    def base_colour(material):
        if material is None:
            return [0.5, 0.5, 0.5]
        if material.use_nodes and material.node_tree:
            for node in material.node_tree.nodes:
                if node.type == "BSDF_PRINCIPLED":
                    value = node.inputs["Base Color"].default_value
                    return [float(value[0]), float(value[1]), float(value[2])]
        value = material.diffuse_color
        return [float(value[0]), float(value[1]), float(value[2])]

    objects = []
    empties = []
    for obj in collection.all_objects:
        if obj.type == "EMPTY":
            empties.append({
                "name": obj.name,
                "parent": obj.parent.name if obj.parent else None,
                "location": [float(c) for c in obj.matrix_world.translation],
            })
            continue
        if obj.type != "MESH":
            continue
        evaluated = obj.evaluated_get(depsgraph)
        mesh = evaluated.to_mesh()
        mesh.calc_loop_triangles()
        world = obj.matrix_world
        materials = [slot.material for slot in obj.material_slots]
        tris = []
        for tri in mesh.loop_triangles:
            corners = [world @ mesh.vertices[i].co for i in tri.vertices]
            material = materials[tri.material_index] if tri.material_index < len(materials) else None
            tris.append({
                "v": [[float(c.x), float(c.y), float(c.z)] for c in corners],
                "m": material.name if material else "",
            })
        palette = {}
        for material in materials:
            if material is not None:
                palette[material.name] = base_colour(material)
        objects.append({
            "name": obj.name,
            "parent": obj.parent.name if obj.parent else None,
            "hidden": bool(obj.hide_render),
            "palette": palette,
            "triangles": tris,
        })
        evaluated.to_mesh_clear()

    summary = None
    if isinstance(result, dict):
        summary = {k: (v if isinstance(v, (int, float, str, bool)) else str(type(v).__name__))
                   for k, v in result.items()}
    with open(out_path, "w", encoding="utf-8") as out:
        json.dump({"builder": builder_path, "result": summary,
                   "objects": objects, "empties": empties}, out)
    print("DUMP OK objects=%d empties=%d triangles=%d" % (
        len(objects), len(empties), sum(len(o["triangles"]) for o in objects)))


main()
