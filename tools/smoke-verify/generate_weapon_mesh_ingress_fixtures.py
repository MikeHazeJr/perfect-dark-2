"""Stage portable production weapon-nested harness fixtures in an isolated install.

Run only inside the parent's coordinated smoke phase. Public OBJ/INI/JSON
fixtures are constructed independently of scanner implementation. No ROM input.
"""
import argparse
import base64
import struct
import wave
import io
import json
from pathlib import Path
import zipfile


def archive(members):
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w", zipfile.ZIP_STORED) as target:
        for name, data in members.items():
            info = zipfile.ZipInfo(name, (2026, 9, 6, 0, 0, 0))
            target.writestr(info, data.encode() if isinstance(data, str) else data)
    return output.getvalue()


def mesh(catalog_id, edge=3, *, section="model", selected="selected.obj", malformed=False,
         missing_geometry=False, missing_id=False, default_geometry=False):
    geometry = "model.obj" if default_geometry else selected
    descriptor = f"[{section}]\nkind = mesh\n"
    if not missing_id:
        descriptor += f"catalog_id = {catalog_id}\n"
    if not default_geometry:
        descriptor += f"geometry_file = {geometry}\n"
    members = {"mesh.ini": descriptor}
    if not missing_geometry:
        members[geometry] = "not a model\n" if malformed else (
            f"v 0 0 0\nv {edge} 0 0\nv 0 {edge} 0\nf 1 2 3\n"
        )
    # A valid decoy ensures selection cannot accidentally default to model.obj.
    if geometry != "model.obj":
        members["model.obj"] = "v 0 0 0\nv 19 0 0\nv 0 19 0\nf 1 2 3\n"
    return archive(members)


def graph(owner, family, nodes, exports=None):
    result = {"schema": f"pd.{family}_graph.v1", "asset_id": owner,
              "graph_id": "mesh_ingress_proof", "nodes": nodes}
    if exports is not None:
        result["exports"] = exports
    return json.dumps(result) + "\n"


def weapon(owner, *, variant="normal", unused=0, slots_exhaust=False):
    projectile_id = owner + "_projectile"
    selected_id = owner + "_visual"
    visual_options = {
        "malformed_geometry": {"malformed": True},
        "missing_geometry": {"missing_geometry": True},
        "anonymous": {"missing_id": True},
        "bad_section": {"section": "audio"},
        "legacy_section_default": {"section": "mesh", "default_geometry": True},
    }.get(variant, {})
    mesh_id = "foreign:visual" if variant == "foreign" else selected_id
    visual = mesh(mesh_id, **visual_options)
    nested = {"dependencies/assets/models/visual.pdmesh": visual}
    if variant == "malformed_zip":
        nested["dependencies/assets/models/visual.pdmesh"] = b"not a ZIP"
    if variant in {"shared_equal", "shared_divergent"}:
        nested["dependencies/assets/models/other.pdmesh"] = (
            visual if variant == "shared_equal" else mesh(selected_id, edge=4)
        )
    model_ids = [selected_id]
    for i in range(unused):
        child_id = owner + f"_extra_{i:02d}"
        nested[f"dependencies/assets/models/extra_{i:02d}.pdmesh"] = mesh(child_id)
        model_ids.append(child_id)
    if slots_exhaust:
        for i in range(31):
            child_id = owner + f"_slot_{i:02d}"
            nested[f"dependencies/assets/models/slot_{i:02d}.pdmesh"] = mesh(child_id)
            model_ids.append(child_id)
    selected_refs = model_ids if slots_exhaust else [selected_id]
    nested["projectile.ini"] = (
        f"[projectile]\ncatalog_id = {projectile_id}\nbehavior_graph = behavior.graph.json\n"
        "model_file = dependencies/assets/models/visual.pdmesh\n"
    )
    nested["behavior.graph.json"] = graph(projectile_id, "projectile", [
        {"id": f"spawn_{i}", "kind": "projectile.spawn_state",
         "params": {"model_catalog_id": child_id}}
        for i, child_id in enumerate(selected_refs)
    ])
    held_selected = owner + "_held_selected"
    held_decoy = owner + "_held_decoy"
    members = {
        "weapon.ini": (f"[weapon]\ncatalog_id = {owner}\n"
                       "behavior_graph = behavior.graph.json\n"
                       "model_file = dependencies/assets/models/held_selected.pdmesh\n"),
        "behavior.graph.json": graph(owner, "weapon", [
            {"id": "fire", "kind": "fire.hitscan", "params": {"mode": "primary"}},
        ], [{"name": "primary", "node": "fire"}]),
        # Deliberately put the wrong held model first in physical ZIP order.
        "dependencies/assets/models/held_decoy.pdmesh": mesh(held_decoy, edge=19),
        "dependencies/assets/models/held_selected.pdmesh": mesh(held_selected),
        "dependencies/assets/projectiles/round.pdprojectile": archive(nested),
    }
    if variant == "missing_held":
        del members["dependencies/assets/models/held_selected.pdmesh"]
    absent = [held_decoy, held_selected, projectile_id, *model_ids]
    return archive(members), selected_id, projectile_id, absent


def command_json(commands):
    commands = commands + [{"command": "end"}]
    return json.dumps({"source_format": "weapon_animation_commands", "name": "Reload",
                      "command_count": len(commands), "commands": commands})


def command_import(output, kind):
    namespace = f"cmdimport_{kind}"
    ids = {name: f"{namespace}:{name}" for name in ("parent", "left", "right", "clip", "sound", "late")}
    samples = io.BytesIO()
    with wave.open(samples, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(22050)
        wav.writeframes(struct.pack("<16h", *range(16)))
    clip_buffer = struct.pack("<8f", 0, 1, 0, 0, 0, 0, 1, 0)
    gltf = json.dumps({"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "root"}], "buffers": [{"byteLength": 32,
            "uri": "data:application/octet-stream;base64," + base64.b64encode(clip_buffer).decode()}],
        "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 8},
                        {"buffer": 0, "byteOffset": 8, "byteLength": 24}],
        "accessors": [{"bufferView": 0, "componentType": 5126, "count": 2, "type": "SCALAR", "min": [0], "max": [1]},
                      {"bufferView": 1, "componentType": 5126, "count": 2, "type": "VEC3"}],
        "animations": [{"samplers": [{"input": 0, "output": 1, "interpolation": "LINEAR"}],
                        "channels": [{"sampler": 0, "target": {"node": 0, "path": "translation"}}]}]})
    for variant in ("good", "late", "cycle"):
        sources = {
            "parent": [{"command": "include_animation", "slot": 1, "animation": ids["left"]},
                       {"command": "include_animation", "slot": 0, "animation": ids["right"]},
                       {"command": "play_character_animation", "animation": ids["clip"], "direction": 0, "speed": 10000},
                       {"command": "play_sound", "slot": 0, "sound": ids["sound"]}],
            "left": [{"command": "wait_ticks", "slot": 1, "ticks": 11 if variant == "good" else 91}],
            "right": [{"command": "wait_ticks", "slot": 1, "ticks": 22}],
        }
        if variant == "cycle":
            sources["right"] = [{"command": "include_animation", "slot": 0, "animation": ids["parent"]}]
        units = {}
        for name, commands in sources.items():
            units[name] = {"animation.ini": f"[animation]\ncatalog_id = {ids[name]}\nname = Reload\ncategory = weapon_animation\ncommands_file = commands.json\n",
                           "commands.json": command_json(commands)}
        units["clip"] = {"animation.ini": f"[animation]\ncatalog_id = {ids['clip']}\ncategory = character_animation\nanimation_file = animation.gltf\nframe_count = 2\n",
                         "animation.gltf": gltf}
        units["sound"] = {"sound.ini": f"[sfx]\ncatalog_id = {ids['sound']}\ncategory = sfx\nfile_path = sample.wav\n",
                          "sample.wav": samples.getvalue()}
        if variant == "late":
            # Valid syntax, missing native dependency: fails at Commit after earlier sources compiled.
            units["late"] = {"animation.ini": f"[animation]\ncatalog_id = {ids['late']}\ncategory = weapon_animation\ncommands_file = commands.json\n",
                             "commands.json": command_json([{"command": "play_sound", "slot": 0, "sound": "absent:sound"}])}
        members = {}
        for name, files in units.items():
            if kind == "typed":
                location = f"audio/sfx/{name}.pdsfx" if name == "sound" else f"animations/{name}.pdanim"
                members[location] = archive(files)
            else:
                # Root parent descriptor is visited before audio/animation subdirectories.
                prefix = "" if kind == "folder" and name == "parent" else (f"audio/sfx/{name}/" if name == "sound" else f"animations/{name}/")
                for leaf, data in files.items():
                    members[prefix + leaf] = data
        target = output / f"{kind}_{variant}"
        if kind in {"loose", "typed"}:
            target.with_suffix(".pdmod").write_bytes(archive(members))
        else:
            for member, data in members.items():
                path = target / member
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data.encode() if isinstance(data, str) else data)
    return f"command_import|{namespace}|./asset-mesh-ingress/{kind}|{kind}"


def generation_source(output):
    folder = output / "generation"
    folder.mkdir(exist_ok=True)
    (folder / "animation.ini").write_text("[animation]\ncatalog_id = genproof:clip\ncategory = character_animation\nframe_count = 2\nanimation_file = animation.gltf\n", encoding="utf-8")
    (folder / "frames.bin").write_bytes(struct.pack("<8f", 0, 1, 0, 0, 0, 0, 300, 0))
    (folder / "animation.gltf").write_text(json.dumps({
        "asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "root"}], "buffers": [{"uri": "frames.bin", "byteLength": 32}],
        "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 8}, {"buffer": 0, "byteOffset": 8, "byteLength": 24}],
        "accessors": [{"bufferView": 0, "componentType": 5126, "count": 2, "type": "SCALAR", "min": [0], "max": [1]},
                      {"bufferView": 1, "componentType": 5126, "count": 2, "type": "VEC3"}],
        "animations": [{"samplers": [{"input": 0, "output": 1, "interpolation": "LINEAR"}],
                        "channels": [{"sampler": 0, "target": {"node": 0, "path": "translation"}}]}]}), encoding="utf-8")
    return "animation_generation|genproof:clip|./asset-mesh-ingress/generation|unused"


def audio_generation_source(output):
    folder = output / "audio-generation"
    folder.mkdir(exist_ok=True)
    (folder / "sound.ini").write_text("[sfx]\ncatalog_id = audiogenproof:sound\ncategory = sfx\nfile_path = sample.wav\nhas_loop = true\nloop_start_samples = 2\nloop_end_samples = 4\nloop_count = 1\nhas_envelope = true\nattack_volume = 127\ndecay_volume = 64\n", encoding="utf-8")
    with wave.open(str(folder / "sample.wav"), "wb") as sample:
        sample.setnchannels(2)
        sample.setsampwidth(2)
        sample.setframerate(22050)
        sample.writeframes(struct.pack("<32h", *([1000, 2000] * 16)))
    return "audio_generation|audiogenproof:sound|./asset-mesh-ingress/audio-generation|unused"


def command_generation_source(output):
    command_import(output, "generation")
    folder = output / "generation_good"
    (folder / "behavior.graph.json").write_text('{"schema":"pd.weapon_graph.v2","profile":"held_single_shot.v1",\n"asset_id":"cmdimport_generation:weapon","graph_id":"primary","mode":"primary",\n"nodes":[{"id":"press","kind":"event.trigger_pressed"},\n{"id":"shot","kind":"fire.hitscan","module":"og.fire.hitscan","module_version":1,\n"params":{"mode":"primary","function_type":"shoot_single","ammo_slot":0,"flags":0,\n"damage":1.5,"spread":2,"recoil_anim_unk24":4,"recoil_anim_unk25":8,\n"recoil_anim_unk26":-1,"recoil_anim_unk27":-1,"recoverytime_ticks60":12,\n"recoildist":20,"recoilangle":30,"slidemax":4,"impactforce":5,\n"duration_ticks60":7,"penetration":2,\n"noisesettings":{"minradius":1,"maxradius":100,"incradius":3,"decbasespeed":2,"decremspeed":8},\n"recoilsettings":{"xrange":4,"yrange":5,"zrange":6},\n"fire_animation":"cmdimport_generation:parent","shoot_sound_catalog_id":"cmdimport_generation:sound"}}],\n"edges":[{"from":"press","output":"exec","to":"shot","input":"exec"}],\n"exports":[{"name":"trigger_pressed","node":"press"}]}', encoding="utf-8")
    # A diamond proves one shared child is linked exactly once per generation.
    (folder / "animations/left/commands.json").write_text(command_json([
        {"command": "wait_ticks", "slot": 1, "ticks": 11},
        {"command": "include_animation", "slot": 0, "animation": "cmdimport_generation:right"}]), encoding="utf-8")
    clip = folder / "animations/clip/animation.gltf"
    document = json.loads(clip.read_text(encoding="utf-8"))
    document["buffers"][0]["uri"] = "frames.bin"
    clip.write_text(json.dumps(document), encoding="utf-8")
    (clip.parent / "frames.bin").write_bytes(struct.pack("<8f", 0, 1, 0, 0, 0, 0, 300, 0))
    with wave.open(str(folder / "audio/sfx/sound/sample.wav"), "wb") as sample:
        sample.setnchannels(2)
        sample.setsampwidth(2)
        sample.setframerate(22050)
        sample.writeframes(struct.pack("<32h", *([1000, 2000] * 16)))
    return "command_generation|cmdimport_generation:parent|./asset-mesh-ingress/generation_good|unused"


def model_inputs_source(output):
    folder = output / "model-inputs"
    folder.mkdir(exist_ok=True)
    (folder / "_meta").mkdir(exist_ok=True)
    (folder / "mesh.ini").write_text("[model]\ncatalog_id = modelinputs:mesh\nkind = mesh\ngeometry_file = model.obj\nmodel_scale = 2\nskeleton_symbol = SKEL_BASIC\n", encoding="utf-8")
    (folder / "_meta/manifest.json").write_text(json.dumps({"model_scale": 99, "skeleton_symbol": "SKEL_CHR"}), encoding="utf-8")
    (folder / "model.obj").write_text("mtllib model.mtl\nv 0 0 0\nv 2 0 0\nv 0 2 0\nvt 0 0\nvt 1 0\nvt 0 1\nusemtl paint\nf 1/1 2/2 3/3\n", encoding="utf-8")
    (folder / "model.mtl").write_text("newmtl paint\nmap_Kd sample.tga\n", encoding="utf-8")
    header = bytearray(18)
    header[2] = 2
    header[12:16] = struct.pack("<HH", 2, 2)
    header[16:18] = bytes([32, 0x28])
    (folder / "sample.tga").write_bytes(header + bytes([0, 0, 255, 255]) * 4)
    return "model_inputs|modelinputs:mesh|./asset-mesh-ingress/model-inputs|unused"


def texture_generation_source(output):
    folder = output / "texture-generation"
    folder.mkdir(exist_ok=True)
    for name in ("model.obj", "model.mtl", "mesh.ini", "sample.tga"):
        (folder / name).write_bytes((output / "model-inputs" / name).read_bytes())
    (folder / "texture.ini").write_text("[texture]\ncatalog_id = texgen:paint\ntexture_file = sample.tga\nsurface_type = wood\nsound_surface_type = metal\ntile_column_offset = 1\ntile_row_offset = 1\nmask_s_reduction = 1\nmask_t_reduction = 0\n", encoding="utf-8")
    return "texture_generation|texgen:paint|./asset-mesh-ingress/texture-generation|unused"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--install-dir", type=Path, required=True)
    parser.add_argument("--include-base-falcon2", action="store_true")
    args = parser.parse_args()
    args.output = args.install_dir.resolve() / "asset-mesh-ingress"
    args.output.mkdir(parents=True, exist_ok=True)
    lines = ["# Source activation/native record proof; no firing or rendering claim."]
    specs = [
        ("deep", "normal", 0, "mesh_accept"),
        ("many", "normal", 10, "mesh_accept"),
        ("equal", "shared_equal", 0, "mesh_accept"),
        ("legacy", "legacy_section_default", 0, "mesh_accept"),
        ("malformed_obj", "malformed_geometry", 0, "mesh_load_reject"),
        ("missing_obj", "missing_geometry", 0, "reject"),
        ("missing_held", "missing_held", 0, "reject"),
        ("anonymous", "anonymous", 0, "reject"),
        ("foreign", "foreign", 0, "reject"),
        ("wrong_section", "bad_section", 0, "reject"),
        ("bad_zip", "malformed_zip", 0, "reject"),
        ("divergent", "shared_divergent", 0, "reject"),
        ("slots", "normal", 0, "mesh_slots_reject"),
    ]
    for name, variant, unused, mode in specs:
        owner = f"meshproof:{name}"
        data, selected, payload, absent = weapon(owner, variant=variant, unused=unused,
                                                slots_exhaust=mode == "mesh_slots_reject")
        path = (args.output / f"{name}.pdweapon").resolve()
        path.write_bytes(data)
        portable_path = f"./asset-mesh-ingress/{name}.pdweapon"
        if mode in {"reject", "mesh_slots_reject"}:
            lines.append(f"{mode}|{owner}|{portable_path}|{','.join(absent)}")
        else:
            lines.append(f"{mode}|{owner}|{portable_path}|{selected}|{payload}|3,{3 + unused}")
    if args.include_base_falcon2:
        lines.append(f"mesh_base|base:falcon2|./data/ntsc-final/weapons/base_falcon2.pdweapon|"
                     "base:model_falcon2_hi|base:model_falcon2lod_lo")
    command_dir = args.output / "commands"
    command_dir.mkdir(exist_ok=True)
    command_sources = {
        "left": [{"command": "wait_ticks", "slot": 1, "ticks": 1}],
        "right": [{"command": "wait_ticks", "slot": 1, "ticks": 2}],
        "replaced": [{"command": "wait_ticks", "slot": 1, "ticks": 3}],
        "rejected": [{"command": "play_character_animation", "animation": "missing:clip", "direction": 0, "speed": 10000}],
        "cycle": [{"command": "include_animation", "slot": 0, "animation": "commandproof:parent"}],
        "order_left": [{"command": "include_animation", "slot": 0, "animation": "command_b:reload"}],
        "order_right": [{"command": "include_animation", "slot": 0, "animation": "command_a:reload"}],
        "parent": [{"command": "include_animation", "slot": 1, "animation": "command_a:reload"},
                   {"command": "include_animation", "slot": 0, "animation": "command_b:reload"}],
    }
    for name, commands in command_sources.items():
        commands = commands + [{"command": "end"}]
        (command_dir / f"{name}.json").write_text(json.dumps({"source_format": "weapon_animation_commands",
            "name": "Reload", "command_count": len(commands), "commands": commands}), encoding="utf-8")
    lines.append("command_source|commandproof:parent|./asset-mesh-ingress/commands|unused")
    for kind in ("folder", "loose", "typed"):
        lines.append(command_import(args.output, kind))
    lines.append(generation_source(args.output))
    lines.append(audio_generation_source(args.output))
    lines.append(command_generation_source(args.output))
    lines.append(model_inputs_source(args.output))
    lines.append("model_generation|modelgen:mesh|./asset-mesh-ingress/model-inputs|unused")
    lines.append(texture_generation_source(args.output))
    lines.append("texture_runtime|texgen:paint|./asset-mesh-ingress/texture-generation|unused")
    (args.output / "plan.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
