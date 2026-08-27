#!/usr/bin/env python3
"""Normalize the checked-in typed .pdxxx example archives.

The examples intentionally contain real zip-openable typed archives, not loose
folders. This script rewrites the sample archives whose public layout depends
on other sample archives so the nested dependency copies stay in the same clean
shape as the top-level samples.
"""

from __future__ import annotations

import hashlib
import io
import json
import math
import struct
import zipfile
from collections.abc import Iterable
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1] / "examples" / "modding" / "typed-pdxxx-basic"
ZIP_TIME = (1980, 1, 1, 0, 0, 0)
# Two scenario cache-kinds, mirroring the extractor (see asset_archive_conformance):
# an arena embeds the full rendered scenario (stamp tracks _alphablend/_cipalfix_b943),
# a mission carries only a scenario-dependency stamp. Keep in lockstep with:
#   ARENA   -> ROMEXTRACT_PDSCENARIO_FAST_CACHE_KIND  (port/src/romextract_pdarena.c)
#   MISSION -> PDMETA_SCENARIO_DEP_CACHE_KIND         (port/src/romextract_pdmeta.c)
ARENA_SCENARIO_GRAPH_CACHE_KIND = (
    "pdscenario_scene_glb_clean_public_v99_standalone_backfill_collision_obj_collision_flags_json_room_lights_json_dccuv_rsptexscale_texshift_samplerwrap_untextured_uvbound_color0_alphamask_alphablend_quip_shuffle_graph_portals_json_objects_json_setup_fields_json_ai_lists_json_ai_command_graph_navhashes_objectives_spawns_volumes_pads_paths_json_navtables_json_cipalfix_b943_iafix_b945"
)
MISSION_SCENARIO_DEP_CACHE_KIND = (
    "pdscenario_scene_glb_clean_public_v99_standalone_backfill_collision_obj_collision_flags_json_room_lights_json_dccuv_rsptexscale_texshift_samplerwrap_untextured_uvbound_color0_alphamask_quip_shuffle_graph_portals_json_objects_json_setup_fields_json_ai_lists_json_ai_command_graph_navhashes_objectives_spawns_volumes_pads_paths_json_navtables_json"
)


def archive(rel: str) -> Path:
    return ROOT / rel


def read_entry(rel: str, entry: str) -> bytes:
    with zipfile.ZipFile(archive(rel), "r") as zf:
        return zf.read(entry)


def read_entry_or_none(rel: str, entry: str) -> bytes | None:
    try:
        return read_entry(rel, entry)
    except KeyError:
        return None


def read_archive(rel: str) -> bytes:
    return archive(rel).read_bytes()


def write_archive(rel: str, entries: Iterable[tuple[str, bytes | str]]) -> None:
    path = archive(rel)
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for name, data in entries:
            if isinstance(data, str):
                data = data.encode("utf-8")
            info = zipfile.ZipInfo(name, ZIP_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            zf.writestr(info, data)


def archive_bytes(entries: Iterable[tuple[str, bytes | str]]) -> bytes:
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for name, data in entries:
            if isinstance(data, str):
                data = data.encode("utf-8")
            info = zipfile.ZipInfo(name, ZIP_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            zf.writestr(info, data)
    return buffer.getvalue()


def manifest(kind: str, catalog_id: str) -> str:
    return (
        "{\n"
        "  \"schema\": \"pd.asset_archive.manifest.v1\",\n"
        f"  \"pd_kind\": \"{kind}\",\n"
        f"  \"catalog_id\": \"{catalog_id}\"\n"
        "}\n"
    )


def manifest_with(kind: str, catalog_id: str, fields: dict[str, object]) -> str:
    data: dict[str, object] = {
        "schema": "pd.asset_archive.manifest.v1",
        "pd_kind": kind,
        "catalog_id": catalog_id,
    }
    data.update(fields)
    return json.dumps(data, indent=2) + "\n"


def prop_manifest(catalog_id: str) -> str:
    return (
        "{\n"
        "  \"schema\": \"pd.asset_archive.manifest.v1\",\n"
        "  \"pd_kind\": \"prop\",\n"
        f"  \"catalog_id\": \"{catalog_id}\",\n"
        "  \"model_file\": \"model.gltf\",\n"
        "  \"prop_file\": \"prop.json\",\n"
        "  \"behavior_graph\": \"behavior.graph.json\"\n"
        "}\n"
    )


def vehicle_manifest(catalog_id: str) -> str:
    return (
        "{\n"
        "  \"schema\": \"pd.asset_archive.manifest.v1\",\n"
        "  \"pd_kind\": \"vehicle\",\n"
        f"  \"catalog_id\": \"{catalog_id}\",\n"
        "  \"model_file\": \"model.gltf\",\n"
        "  \"physics_file\": \"physics.json\",\n"
        "  \"behavior_graph\": \"behavior.graph.json\"\n"
        "}\n"
    )


def effect_manifest(catalog_id: str) -> str:
    return (
        "{\n"
        "  \"schema\": \"pd.asset_archive.manifest.v1\",\n"
        "  \"pd_kind\": \"effect\",\n"
        f"  \"catalog_id\": \"{catalog_id}\",\n"
        "  \"effect_file\": \"effect.graph.json\",\n"
        "  \"timeline_file\": \"timeline.json\"\n"
        "}\n"
    )


def material_manifest(catalog_id: str) -> str:
    return (
        "{\n"
        "  \"schema\": \"pd.asset_archive.manifest.v1\",\n"
        "  \"pd_kind\": \"material\",\n"
        f"  \"catalog_id\": \"{catalog_id}\",\n"
        "  \"material_file\": \"material.json\",\n"
        "  \"texture_archive\": \"dependencies/assets/texture/tri_texture.pdtexture\",\n"
        "  \"effect_archive\": \"dependencies/assets/effects/tri_effect.pdeffect\"\n"
        "}\n"
    )


def theme_manifest(catalog_id: str) -> str:
    return (
        "{\n"
        "  \"schema\": \"pd.asset_archive.manifest.v1\",\n"
        "  \"pd_kind\": \"theme\",\n"
        f"  \"catalog_id\": \"{catalog_id}\"\n"
        "}\n"
    )


def character_manifest(catalog_id: str) -> str:
    return (
        "{\n"
        "  \"schema\": \"pd.asset_archive.manifest.v1\",\n"
        "  \"pd_kind\": \"character\",\n"
        f"  \"catalog_id\": \"{catalog_id}\",\n"
        "  \"body\": \"example:tri_body\",\n"
        "  \"head\": \"example:tri_head\",\n"
        "  \"body_archive\": \"dependencies/assets/body/tri_body.pdbody\",\n"
        "  \"head_archive\": \"dependencies/assets/head/tri_head.pdhead\",\n"
        "  \"portrait_file\": \"portrait.png\"\n"
        "}\n"
    )


def audio_wav_descriptor(kind: str, catalog_id: str, name: str,
                         category: str, duration_ms: int,
                         sample_size: int, decoded_sample_count: int,
                         extra: str = "") -> str:
    return (
        f"; {catalog_id} - self-contained {category} asset\n"
        f"[{kind}]\n"
        f"catalog_id = {catalog_id}\n"
        f"name = {name}\n"
        f"audio_category = {category}\n"
        f"duration_ms = {duration_ms}\n"
        "format = WAV_PCM16\n"
        "source_format = WAV_PCM16\n"
        "sample_rate_hz = 44100\n"
        "file_path = sample.wav\n"
        f"data_size = {sample_size}\n"
        f"source_data_size = {sample_size}\n"
        f"decoded_sample_count = {decoded_sample_count}\n"
        "loop_start_samples = 0\n"
        "loop_end_samples = 0\n"
        "loop_count = 0\n"
        "has_loop = false\n"
        "sample_pan = 64\n"
        "sample_volume = 127\n"
        "key_min = 0\n"
        "key_max = 127\n"
        "key_base = 60\n"
        "key_detune = 0\n"
        "velocity_min = 0\n"
        "velocity_max = 127\n"
        "has_envelope = false\n"
        "attack_time_us = 0\n"
        "decay_time_us = 0\n"
        "release_time_us = 0\n"
        "attack_volume = 127\n"
        "decay_volume = 127\n"
        "sound_flags = 0\n"
        "source_index = -1\n"
        "source_offset = 0\n"
        "source_symbol = custom\n"
        f"{extra}"
        "\n[meta]\n"
        "manifest = _meta/manifest.json\n"
    )


def arena_manifest(catalog_id: str) -> str:
    return (
        "{\n"
        "  \"schema\": \"pd.asset_archive.manifest.v1\",\n"
        "  \"pd_kind\": \"arena\",\n"
        f"  \"catalog_id\": \"{catalog_id}\",\n"
        "  \"scenario\": \"example:tri_scenario\",\n"
        "  \"scenario_archive\": \"dependencies/assets/scenarios/tri_scenario.pdscenario\"\n"
        "}\n"
    )


def head_manifest(catalog_id: str) -> str:
    return (
        "{\n"
        "  \"schema\": \"pd.asset_archive.manifest.v1\",\n"
        "  \"pd_kind\": \"head\",\n"
        f"  \"catalog_id\": \"{catalog_id}\",\n"
        "  \"mesh_archive\": \"mesh.pdmesh\"\n"
        "}\n"
    )


def body_manifest(catalog_id: str) -> str:
    return (
        "{\n"
        "  \"schema\": \"pd.asset_archive.manifest.v1\",\n"
        "  \"pd_kind\": \"body\",\n"
        f"  \"catalog_id\": \"{catalog_id}\",\n"
        "  \"mesh_archive\": \"mesh.pdmesh\",\n"
        "  \"hand_archive\": \"hand.pdmesh\"\n"
        "}\n"
    )


def audio_wav_manifest(kind: str, catalog_id: str, sample_size: int,
                       decoded_sample_count: int) -> str:
    return (
        "{\n"
        "  \"schema\": \"pd.asset_archive.manifest.v1\",\n"
        f"  \"pd_kind\": \"{kind}\",\n"
        f"  \"catalog_id\": \"{catalog_id}\",\n"
        "  \"format\": \"WAV_PCM16\",\n"
        "  \"source_format\": \"WAV_PCM16\",\n"
        "  \"sample_rate_hz\": 44100,\n"
        "  \"data\": \"sample.wav\",\n"
        f"  \"data_size\": {sample_size},\n"
        f"  \"source_data_size\": {sample_size},\n"
        f"  \"decoded_sample_count\": {decoded_sample_count},\n"
        "  \"loop_start_samples\": 0,\n"
        "  \"loop_end_samples\": 0,\n"
        "  \"loop_count\": 0,\n"
        "  \"has_loop\": false,\n"
        "  \"sample_pan\": 64,\n"
        "  \"sample_volume\": 127,\n"
        "  \"sound_flags\": 0,\n"
        "  \"source_index\": -1,\n"
        "  \"source_offset\": 0,\n"
        "  \"key_min\": 0,\n"
        "  \"key_max\": 127,\n"
        "  \"key_base\": 60,\n"
        "  \"key_detune\": 0,\n"
        "  \"velocity_min\": 0,\n"
        "  \"velocity_max\": 127,\n"
        "  \"has_envelope\": false,\n"
        "  \"attack_time_us\": 0,\n"
        "  \"decay_time_us\": 0,\n"
        "  \"release_time_us\": 0,\n"
        "  \"attack_volume\": 127,\n"
        "  \"decay_volume\": 127,\n"
        "  \"source_symbol\": \"custom\"\n"
        "}\n"
    )


def pcm16_mono_wav(sample_rate_hz: int = 44100,
                   frame_count: int = 44) -> bytes:
    samples = bytearray()
    for i in range(frame_count):
        phase = (i / max(frame_count, 1)) * math.pi * 2.0
        value = int(math.sin(phase) * 12000.0)
        samples.extend(struct.pack("<h", value))

    data_size = len(samples)
    return (
        b"RIFF" +
        struct.pack("<I", 36 + data_size) +
        b"WAVE" +
        b"fmt " +
        struct.pack(
            "<IHHIIHH",
            16,
            1,
            1,
            sample_rate_hz,
            sample_rate_hz * 2,
            2,
            16,
        ) +
        b"data" +
        struct.pack("<I", data_size) +
        bytes(samples)
    )


def song_sequence_mid() -> bytes:
    return (
        b"MThd"
        b"\x00\x00\x00\x06"
        b"\x00\x00"
        b"\x00\x01"
        b"\x00\x60"
        b"MTrk"
        b"\x00\x00\x00\x04"
        b"\x00\xff\x2f\x00"
    )


def song_sequence_json() -> str:
    return json.dumps({
        "schema": "pd2.song.sequence.v1",
        "events": [
            {
                "tick": 0,
                "track": 0,
                "type": "track_end",
            },
        ],
    }, indent=2) + "\n"


def song_sequence_descriptor(catalog_id: str, midi_size: int,
                             events_size: int) -> str:
    return (
        "; tri_song.pdsong - self-contained sequenced music asset\n"
        "[music]\n"
        f"catalog_id = {catalog_id}\n"
        "name = Triangle Song\n"
        "audio_category = music\n"
        "duration_ms = 1\n"
        "source_format = MIDI\n"
        "format = MIDI\n"
        "music_file = sequence.mid\n"
        "midi_file = sequence.mid\n"
        "events_file = sequence.json\n"
        f"midi_size = {midi_size}\n"
        f"events_size = {events_size}\n"
        "division = 96\n"
        "event_count = 1\n"
        "loops = false\n"
        "source_index = -1\n"
        "source_offset = 0\n"
        "\n[meta]\n"
        "manifest = _meta/manifest.json\n"
    )


def song_sequence_manifest(catalog_id: str, midi_size: int,
                           events_size: int) -> str:
    return (
        "{\n"
        "  \"schema\": \"pd.asset_archive.manifest.v1\",\n"
        "  \"pd_kind\": \"song\",\n"
        f"  \"catalog_id\": \"{catalog_id}\",\n"
        "  \"source_format\": \"MIDI\",\n"
        "  \"format\": \"MIDI\",\n"
        "  \"midi\": \"sequence.mid\",\n"
        "  \"events\": \"sequence.json\",\n"
        f"  \"midi_size\": {midi_size},\n"
        f"  \"events_size\": {events_size},\n"
        "  \"division\": 96,\n"
        "  \"event_count\": 1,\n"
        "  \"loops\": false,\n"
        "  \"source_index\": -1,\n"
        "  \"source_offset\": 0\n"
        "}\n"
    )


def font_glyphs_pgm() -> bytes:
    return b"P5\n1 1\n255\n\xff"


def font_metrics_json() -> str:
    return json.dumps({
        "pd_kind": "font_metrics",
        "pd_schema_version": 1,
        "glyphs_file": "glyphs.pgm",
        "atlas": {
            "width": 1,
            "height": 1,
            "cell_width": 1,
            "cell_height": 1,
        },
        "glyphs": [
            {
                "index": 65,
                "char": "A",
                "baseline": 0,
                "height": 1,
                "width": 1,
                "kerning_index": 0,
                "atlas_x": 0,
                "atlas_y": 0,
            },
        ],
    }, indent=2) + "\n"


def font_bitmap_descriptor(catalog_id: str, glyph_size: int,
                           metrics_size: int) -> str:
    return (
        "; tri_font.pdfont - self-contained bitmap font asset\n"
        "[font]\n"
        f"catalog_id = {catalog_id}\n"
        "name = Triangle Font\n"
        "face = triangle\n"
        "font_format = bitmap_ci4_atlas\n"
        "font_file = glyphs.pgm\n"
        "glyphs_file = glyphs.pgm\n"
        "metrics_file = font.metrics.json\n"
        f"glyphs_size = {glyph_size}\n"
        f"metrics_size = {metrics_size}\n"
        "character_count = 1\n"
        "source_segment = custom\n"
        "\n[meta]\n"
        "manifest = _meta/manifest.json\n"
    )


def font_bitmap_manifest(catalog_id: str, glyph_size: int,
                         metrics_size: int) -> str:
    return (
        "{\n"
        "  \"schema\": \"pd.asset_archive.manifest.v1\",\n"
        "  \"pd_kind\": \"font\",\n"
        f"  \"catalog_id\": \"{catalog_id}\",\n"
        "  \"face\": \"triangle\",\n"
        "  \"format\": \"bitmap_ci4_atlas\",\n"
        "  \"glyphs\": \"glyphs.pgm\",\n"
        "  \"metrics\": \"font.metrics.json\",\n"
        f"  \"glyphs_size\": {glyph_size},\n"
        f"  \"metrics_size\": {metrics_size},\n"
        "  \"character_count\": 1,\n"
        "  \"source_segment\": \"custom\"\n"
        "}\n"
    )


def sha256_hex(text: str | bytes) -> str:
    if isinstance(text, str):
        text = text.encode("utf-8")
    return hashlib.sha256(text).hexdigest()


def scenario_pads_json(rel: str) -> bytes:
    return read_entry(rel, "pads.json")


def scenario_spawns_json(rel: str) -> bytes:
    return read_entry(rel, "spawns.json")


def scenario_volumes_json(rel: str) -> bytes:
    return read_entry(rel, "volumes.json")


def scenario_paths_json(rel: str) -> bytes:
    return read_entry(rel, "navigation/paths.json")


def scenario_objects_json(rel: str) -> bytes:
    return read_entry(rel, "objects.json")


def count_json_rows(text: str | bytes) -> int:
    if isinstance(text, bytes):
        text = text.decode("utf-8", errors="replace")
    parsed = json.loads(text)
    rows = parsed.get("rows") if isinstance(parsed, dict) else None
    return len(rows) if isinstance(rows, list) else 0


def empty_rows_json(schema: str) -> str:
    return json.dumps({
        "schema": schema,
        "rows": [],
    }, indent=2) + "\n"


def update_animation(rel: str, catalog_id: str, name: str, category: str) -> None:
    animation = read_entry(rel, "animation.gltf")
    write_archive(rel, [
        ("animation.ini",
         f"; {Path(rel).name} - self-contained animation asset\n"
         "[animation]\n"
         f"catalog_id = {catalog_id}\n"
         f"name = {name}\n"
         "frame_count = 2\n"
         f"category = {category}\n"
         "\n[source]\n"
         "animation_file = animation.gltf\n"
         "\n[meta]\n"
         "manifest = _meta/manifest.json\n"),
        ("animation.gltf", animation),
        ("_meta/manifest.json", manifest_with("animation", catalog_id, {
            "category": category,
            "frame_count": 2,
            "animation": "animation.gltf",
            "runtime_source": "animation.gltf",
            "animation_file": "animation.gltf",
        })),
    ])


def update_audio_examples() -> None:
    sfx_sample = pcm16_mono_wav()
    voice_sample = pcm16_mono_wav()
    voice_fr_sample = pcm16_mono_wav(frame_count=88)
    sfx_count = 44
    voice_count = 44
    write_archive("audio/sfx/tri_click.pdsfx", [
        ("sound.ini", audio_wav_descriptor(
            "sfx",
            "example:tri_click",
            "Triangle Click",
            "sfx",
            1,
            len(sfx_sample),
            sfx_count,
        )),
        ("sample.wav", sfx_sample),
        ("_meta/manifest.json", audio_wav_manifest(
            "sfx",
            "example:tri_click",
            len(sfx_sample),
            sfx_count,
        )),
    ])
    write_archive("audio/voice/tri_voice.pdvoice", [
        ("voice.ini", audio_wav_descriptor(
            "voice",
            "example:tri_voice",
            "Triangle Voice",
            "voice",
            1,
            len(voice_sample),
            voice_count,
            extra=(
                "\n[voice]\n"
                "actor = example\n"
                "transcript = triangle\n"
                "language = en\n"
                "context = creator example\n"
                "subtitle_file = subtitle.json\n"
                "fallback_locale = en\n"
                "locale_en_file = locales/en.wav\n"
                "locale_fr_file = locales/fr.wav\n"
            ),
        )),
        ("sample.wav", voice_sample),
        ("locales/en.wav", voice_sample),
        ("locales/fr.wav", voice_fr_sample),
        ("subtitle.json", json.dumps({
            "schema": "pd.voice_subtitle.v1",
            "default": "Triangle voice line",
            "en": "Localized triangle voice line",
            "fr": "Ligne vocale triangle localisee",
        }, indent=2, ensure_ascii=False) + "\n"),
        ("_meta/manifest.json", audio_wav_manifest(
            "voice",
            "example:tri_voice",
            len(voice_sample),
            voice_count,
        )),
    ])


def update_song_examples() -> None:
    catalog_id = "example:tri_song"
    midi = song_sequence_mid()
    events = song_sequence_json()
    write_archive("audio/music/tri_song.pdsong", [
        ("music.ini", song_sequence_descriptor(
            catalog_id,
            len(midi),
            len(events.encode("utf-8")),
        )),
        ("sequence.mid", midi),
        ("sequence.json", events),
        ("_meta/manifest.json", song_sequence_manifest(
            catalog_id,
            len(midi),
            len(events.encode("utf-8")),
        )),
    ])


def update_font_examples() -> None:
    catalog_id = "example:tri_font"
    glyphs = font_glyphs_pgm()
    metrics = font_metrics_json()
    metrics_bytes = metrics.encode("utf-8")
    write_archive("fonts/tri_font.pdfont", [
        ("font.ini", font_bitmap_descriptor(
            catalog_id,
            len(glyphs),
            len(metrics_bytes),
        )),
        ("glyphs.pgm", glyphs),
        ("font.metrics.json", metrics),
        ("_meta/manifest.json", font_bitmap_manifest(
            catalog_id,
            len(glyphs),
            len(metrics_bytes),
        )),
    ])


def lang_strings_json() -> str:
    return json.dumps({
        "pd_kind": "language_strings",
        "pd_schema_version": 1,
        "strings": [
            {"index": 0, "key": "triangle", "text": "Triangle"},
            {"index": 1, "key": "archive", "text": "Archive"},
        ],
    }, indent=2) + "\n"


def lang_descriptor(catalog_id: str, strings_size: int,
                    string_count: int) -> str:
    return (
        "; tri_lang.pdlang - self-contained language asset\n"
        "[lang]\n"
        f"catalog_id = {catalog_id}\n"
        "locale = en\n"
        "category = system\n"
        "strings_file = strings.json\n"
        f"data_size = {strings_size}\n"
        f"string_count = {string_count}\n"
        "source_bank = 1\n"
        "source_symbol = custom\n"
        "\n[meta]\n"
        "manifest = _meta/manifest.json\n"
    )


def lang_manifest(catalog_id: str, strings_size: int,
                  string_count: int) -> str:
    return (
        "{\n"
        "  \"schema\": \"pd.asset_archive.manifest.v1\",\n"
        "  \"pd_kind\": \"lang\",\n"
        f"  \"catalog_id\": \"{catalog_id}\",\n"
        "  \"locale\": \"en\",\n"
        "  \"category\": \"system\",\n"
        "  \"data\": \"strings.json\",\n"
        f"  \"data_size\": {strings_size},\n"
        f"  \"string_count\": {string_count},\n"
        "  \"source_bank\": 1,\n"
        "  \"source_symbol\": \"custom\"\n"
        "}\n"
    )


def update_lang_examples() -> None:
    catalog_id = "example:tri_lang"
    strings = lang_strings_json()
    strings_size = len(strings.encode("utf-8"))
    string_count = 2
    write_archive("lang/tri_lang.pdlang", [
        ("lang.ini", lang_descriptor(catalog_id, strings_size, string_count)),
        ("strings.json", strings),
        ("_meta/manifest.json",
         lang_manifest(catalog_id, strings_size, string_count)),
    ])


def update_scenario() -> bytes:
    rel = "scenarios/tri_scenario.pdscenario"
    kept = {
        "scene.glb": read_entry(rel, "scene.glb"),
        "pads.json": scenario_pads_json(rel),
        "spawns.json": scenario_spawns_json(rel),
        "volumes.json": scenario_volumes_json(rel),
        "navigation/paths.json": scenario_paths_json(rel),
        "objects.json": scenario_objects_json(rel),
        "_meta/generated-collision.json": read_entry(rel, "_meta/generated-collision.json"),
    }
    collision_obj = (
        "# Perfect Dark 2 example collision override\n"
        "o collision_floor\n"
        "g room_0\n"
        "v 0 0 0\n"
        "v 100 0 0\n"
        "v 0 0 100\n"
        "f 1 2 3\n"
    )
    navigation_ini = (
        "[navigation]\n"
        "source = scene.glb\n"
        "collision_source = collision.obj\n"
        "generator = deterministic.surface_graph.v1\n"
        "supports_walk = true\n"
        "supports_jump = true\n"
        "supports_drop = true\n"
        "supports_wall = true\n"
        "supports_ceiling = true\n"
        "pads_file = pads.json\n"
        "spawns_file = spawns.json\n"
        "volumes_file = volumes.json\n"
        "waypoints_file = navigation/waypoints.json\n"
        "waygroups_file = navigation/waygroups.json\n"
        "covers_file = navigation/covers.json\n"
        "paths_file = navigation/paths.json\n"
        "portals_file = portals.json\n"
        "generated_cache = _meta/generated-navmesh.json\n"
    )
    portals_json = json.dumps({
        "schema": "pd2.scenario.portals.v1",
        "rows": [],
    }, indent=2) + "\n"
    waypoints_json = empty_rows_json("pd2.scenario.waypoints.v1")
    waygroups_json = empty_rows_json("pd2.scenario.waygroups.v1")
    covers_json = empty_rows_json("pd2.scenario.covers.v1")
    paths_json = json.dumps({
        "schema": "pd2.scenario.paths.v1",
        "rows": [
            {
                "path_ref": "path_0000",
                "flags": "0x00",
                "pads": ["pad_0000"],
            },
        ],
    }, indent=2) + "\n"
    navmesh_meta_json = (
        "{\n"
        "  \"schema\": \"pd2.generated.navmesh.v1\",\n"
        "  \"scenario\": \"example:tri_scenario\",\n"
        "  \"derived_from\": \"scene.glb\",\n"
        "  \"inputs\": [\"scene.glb\", \"collision.obj\", \"navigation.ini\", \"portals.json\", \"pads.json\", \"spawns.json\", \"volumes.json\", \"navigation/waypoints.json\", \"navigation/waygroups.json\", \"navigation/covers.json\", \"navigation/paths.json\"],\n"
        "  \"generator\": \"deterministic.surface_graph.v1\",\n"
        "  \"capabilities\": [\"walk\", \"jump\", \"drop\", \"wall\", \"ceiling\"],\n"
        f"  \"source_counts\": {{ \"pads\": {count_json_rows(kept['pads.json'])}, \"volumes\": {count_json_rows(kept['volumes.json'])}, \"waypoints\": {count_json_rows(waypoints_json)}, \"waygroups\": {count_json_rows(waygroups_json)}, \"covers\": {count_json_rows(covers_json)}, \"paths\": {count_json_rows(paths_json)} }},\n"
        f"  \"source_hashes\": {{ \"scene.glb\": \"{sha256_hex(kept['scene.glb'])}\", \"collision.obj\": \"{sha256_hex(collision_obj)}\", \"navigation.ini\": \"{sha256_hex(navigation_ini)}\", \"portals.json\": \"{sha256_hex(portals_json)}\", \"pads.json\": \"{sha256_hex(kept['pads.json'])}\", \"spawns.json\": \"{sha256_hex(kept['spawns.json'])}\", \"volumes.json\": \"{sha256_hex(kept['volumes.json'])}\", \"navigation/waypoints.json\": \"{sha256_hex(waypoints_json)}\", \"navigation/waygroups.json\": \"{sha256_hex(waygroups_json)}\", \"navigation/covers.json\": \"{sha256_hex(covers_json)}\", \"navigation/paths.json\": \"{sha256_hex(paths_json)}\" }},\n"
        "  \"cache_only\": true\n"
        "}\n"
    )
    setup_fields_json = json.dumps({
        "schema": "pd2.scenario.setup.fields.v1",
        "rows": [
            {
                "record_id": "setup_0000",
                "kind": "prop",
                "field": "command.order",
                "type": "s32",
                "value": "0",
                "catalog_id": "",
                "ref_record_id": "",
            },
            {
                "record_id": "briefing_0000",
                "kind": "briefing",
                "field": "command.order",
                "type": "s32",
                "value": "1",
                "catalog_id": "",
                "ref_record_id": "",
            },
            {
                "record_id": "briefing_0000",
                "kind": "briefing",
                "field": "briefing.kind",
                "type": "s32",
                "value": "0",
                "catalog_id": "",
                "ref_record_id": "",
            },
            {
                "record_id": "briefing_0000",
                "kind": "briefing",
                "field": "briefing.text_token",
                "type": "u32_hex",
                "value": "0x00000000",
                "catalog_id": "",
                "ref_record_id": "",
            },
            {
                "record_id": "setup_0000",
                "kind": "prop",
                "field": "base.pad",
                "type": "pad_ref",
                "value": "pad_0000",
                "catalog_id": "",
                "ref_record_id": "",
            },
            {
                "record_id": "setup_0000",
                "kind": "prop",
                "field": "base.flags",
                "type": "u32_hex",
                "value": "0x00000000",
                "catalog_id": "",
                "ref_record_id": "",
            },
        ],
    }, indent=2) + "\n"
    objectives_json = (
        "{\n"
        "  \"schema\": \"pd2.scenario.objectives.v1\",\n"
        "  \"rows\": [\n"
        "    { \"objective_id\": \"objective_0000\", \"kind\": \"objective\", \"text_token\": \"objective_text_primary\", \"difficulty_mask\": \"all\", \"graph_node\": \"level.objective.0000\", \"operand_kind\": \"objective\", \"target_ref\": \"\", \"target_record_ref\": \"\", \"pad_ref\": \"\", \"state_ref\": \"\", \"match_value\": \"\", \"initial_status\": \"\" }\n"
        "  ]\n"
        "}\n"
    )
    ai_lists_json = json.dumps({
        "schema": "pd2.scenario.ai.lists.v1",
        "rows": [
            {
                "ailist_ref": "ailist_0000",
                "list_id": "0x0000",
                "graph_node": "scenario.ai.ailist_0000.command.0000",
                "command_index": 0,
                "offset": 0,
                "opcode": "0x0004",
                "opcode_name": "end",
                "operands": [],
                "model_catalog_id": "",
                "weapon_catalog_id": "",
                "body_catalog_id": "",
                "head_catalog_id": "",
            },
        ],
    }, indent=2) + "\n"
    level_graph_json = (
        "{\n"
        "  \"schema\": \"pd2.level.graph.v1\",\n"
        "  \"scenario\": \"example:tri_scenario\",\n"
        "  \"source\": \"scene.glb\",\n"
        "  \"kind\": \"mp\",\n"
        "  \"tables\": {\n"
        "    \"portals\": \"portals.json\",\n"
        "    \"pads\": \"pads.json\",\n"
        "    \"spawns\": \"spawns.json\",\n"
        "    \"volumes\": \"volumes.json\",\n"
        "    \"objects\": \"objects.json\",\n"
        "    \"setup_fields\": \"setup.fields.json\",\n"
        "    \"ai_lists\": \"ai/ailists.json\",\n"
        "    \"objectives\": \"objectives.json\",\n"
        "    \"waypoints\": \"navigation/waypoints.json\",\n"
        "    \"waygroups\": \"navigation/waygroups.json\",\n"
        "    \"covers\": \"navigation/covers.json\",\n"
        "    \"paths\": \"navigation/paths.json\"\n"
        "  },\n"
        "  \"nodes\": [\n"
        "    { \"id\": \"scenario.load\", \"kind\": \"event.scenario.load\" },\n"
        "    { \"id\": \"source.scene\", \"kind\": \"scenario.scene.source\", \"file\": \"scene.glb\" },\n"
        "    { \"id\": \"scenario.portals\", \"kind\": \"scenario.portals.source\", \"source\": \"portals.json\", \"portals\": 0 },\n"
        "    { \"id\": \"scenario.pads\", \"kind\": \"scenario.pads.source\", \"source\": \"pads.json\", \"pads\": 1 },\n"
        "    { \"id\": \"navigation.paths\", \"kind\": \"scenario.navigation.paths.source\", \"source\": \"navigation/paths.json\", \"paths\": 1 },\n"
        "    { \"id\": \"scenario.ai.lists\", \"kind\": \"scenario.ai.lists.source\", \"source\": \"ai/ailists.json\", \"lists\": 1 },\n"
        "    { \"id\": \"scenario.ai.ailist_0000.command.0000\", \"kind\": \"scenario.ai.command\", \"source\": \"ai/ailists.json\", \"ailist_ref\": \"ailist_0000\", \"list_id\": \"0x0000\", \"command_index\": 0, \"offset\": 0, \"opcode\": \"0x0004\", \"opcode_name\": \"end\", \"semantic_kind\": \"scenario.ai.control.end\" },\n"
        "    { \"id\": \"scenario.global.settings\", \"kind\": \"scenario.global.settings.source\", \"scenario\": \"example:tri_scenario\", \"source\": \"scenario.ini\", \"scene\": \"scene.glb\", \"collision\": \"collision.obj\", \"navigation\": \"navigation.ini\", \"pads\": 1, \"volumes\": 1 },\n"
        "    { \"id\": \"scenario.ai.action.set_list\", \"kind\": \"scenario.ai.action.set_list\", \"source\": \"ai/ailists.json\", \"target\": \"interpreter.ailist\", \"opcode\": \"0x0005\" },\n"
        "    { \"id\": \"scenario.ai.action.set_return_list\", \"kind\": \"scenario.ai.action.set_return_list\", \"source\": \"ai/ailists.json\", \"target\": \"chr.aireturnlist\", \"opcode\": \"0x0006\" },\n"
        "    { \"id\": \"scenario.ai.action.set_shot_list\", \"kind\": \"scenario.ai.action.set_shot_list\", \"source\": \"ai/ailists.json\", \"target\": \"chr.aishotlist\", \"opcode\": \"0x0007\" },\n"
        "    { \"id\": \"scenario.ai.action.return_list\", \"kind\": \"scenario.ai.action.return_list\", \"source\": \"ai/ailists.json\", \"target\": \"interpreter.ailist\", \"opcode\": \"0x0008\" },\n"
        "    { \"id\": \"scenario.ai.action.stop\", \"kind\": \"scenario.ai.action.stop\", \"source\": \"ai/ailists.json\", \"target\": \"chr.motion_state\", \"opcode\": \"0x0009\" },\n"
        "    { \"id\": \"scenario.ai.action.kneel\", \"kind\": \"scenario.ai.action.kneel\", \"source\": \"ai/ailists.json\", \"target\": \"chr.posture\", \"opcode\": \"0x000a\" },\n"
        "    { \"id\": \"scenario.ai.action.surrender\", \"kind\": \"scenario.ai.action.surrender\", \"source\": \"ai/ailists.json\", \"target\": \"chr.lifecycle\", \"opcode\": \"0x0024\" },\n"
        "    { \"id\": \"scenario.ai.action.fade_out\", \"kind\": \"scenario.ai.action.fade_out\", \"source\": \"ai/ailists.json\", \"target\": \"chr.lifecycle\", \"opcode\": \"0x0025\" },\n"
        "    { \"id\": \"scenario.ai.action.remove_chr\", \"kind\": \"scenario.ai.action.remove_chr\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.visibility\", \"opcode\": \"0x0026\" },\n"
        "    { \"id\": \"scenario.ai.action.try_sidestep\", \"kind\": \"scenario.ai.action.try_sidestep\", \"source\": \"ai/ailists.json\", \"target\": \"chr.combat_evasion\", \"opcode\": \"0x000f\" },\n"
        "    { \"id\": \"scenario.ai.action.try_jump_out\", \"kind\": \"scenario.ai.action.try_jump_out\", \"source\": \"ai/ailists.json\", \"target\": \"chr.combat_evasion\", \"opcode\": \"0x0010\" },\n"
        "    { \"id\": \"scenario.ai.action.try_run_sideways\", \"kind\": \"scenario.ai.action.try_run_sideways\", \"source\": \"ai/ailists.json\", \"target\": \"chr.combat_movement\", \"opcode\": \"0x0011\" },\n"
        "    { \"id\": \"scenario.ai.action.try_attack_walk\", \"kind\": \"scenario.ai.action.try_attack_walk\", \"source\": \"ai/ailists.json\", \"target\": \"chr.attack_state\", \"opcode\": \"0x0012\" },\n"
        "    { \"id\": \"scenario.ai.action.try_attack_run\", \"kind\": \"scenario.ai.action.try_attack_run\", \"source\": \"ai/ailists.json\", \"target\": \"chr.attack_state\", \"opcode\": \"0x0013\" },\n"
        "    { \"id\": \"scenario.ai.action.try_attack_roll\", \"kind\": \"scenario.ai.action.try_attack_roll\", \"source\": \"ai/ailists.json\", \"target\": \"chr.attack_state\", \"opcode\": \"0x0014\" },\n"
        "    { \"id\": \"scenario.ai.action.try_attack_stand\", \"kind\": \"scenario.ai.action.try_attack_stand\", \"source\": \"ai/ailists.json\", \"target\": \"chr.attack_state\", \"opcode\": \"0x0015\" },\n"
        "    { \"id\": \"scenario.ai.action.try_attack_kneel\", \"kind\": \"scenario.ai.action.try_attack_kneel\", \"source\": \"ai/ailists.json\", \"target\": \"chr.attack_state\", \"opcode\": \"0x0016\" },\n"
        "    { \"id\": \"scenario.ai.action.try_attack_lie\", \"kind\": \"scenario.ai.action.try_attack_lie\", \"source\": \"ai/ailists.json\", \"target\": \"chr.attack_state\", \"opcode\": \"0x01ba\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_attack_locked\", \"kind\": \"scenario.ai.condition.if_attack_locked\", \"source\": \"ai/ailists.json\", \"target\": \"chr.attack_flags\", \"opcode\": \"0x00f0\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_attacking\", \"kind\": \"scenario.ai.condition.if_attacking\", \"source\": \"ai/ailists.json\", \"target\": \"chr.attack_state\", \"opcode\": \"0x00f1\" },\n"
        "    { \"id\": \"scenario.ai.action.try_modify_attack\", \"kind\": \"scenario.ai.action.try_modify_attack\", \"source\": \"ai/ailists.json\", \"target\": \"chr.attack_state\", \"opcode\": \"0x0017\" },\n"
        "    { \"id\": \"scenario.ai.action.face_entity\", \"kind\": \"scenario.ai.action.face_entity\", \"source\": \"ai/ailists.json\", \"target\": \"chr.facing\", \"opcode\": \"0x0018\" },\n"
        "    { \"id\": \"scenario.ai.action.apply_gset_damage\", \"kind\": \"scenario.ai.action.apply_gset_damage\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.damage\", \"opcode\": \"0x0019\" },\n"
        "    { \"id\": \"scenario.ai.action.chr_damage_chr\", \"kind\": \"scenario.ai.action.chr_damage_chr\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.damage\", \"opcode\": \"0x001a\" },\n"
        "    { \"id\": \"scenario.ai.condition.consider_grenade_throw\", \"kind\": \"scenario.ai.condition.consider_grenade_throw\", \"source\": \"ai/ailists.json\", \"target\": \"chr.grenade_throw_decision\", \"opcode\": \"0x001b\" },\n"
        "    { \"id\": \"scenario.ai.action.drop_item\", \"kind\": \"scenario.ai.action.drop_item\", \"source\": \"ai/ailists.json\", \"target\": \"chr.inventory_drop\", \"opcode\": \"0x001c\" },\n"
        "    { \"id\": \"scenario.ai.action.try_run_from_target\", \"kind\": \"scenario.ai.action.try_run_from_target\", \"source\": \"ai/ailists.json\", \"target\": \"chr.target_movement\", \"opcode\": \"0x002a\", \"speed\": \"run\" },\n"
        "    { \"id\": \"scenario.ai.action.try_jog_to_target_prop\", \"kind\": \"scenario.ai.action.try_jog_to_target_prop\", \"source\": \"ai/ailists.json\", \"target\": \"chr.target_prop_movement\", \"opcode\": \"0x002b\", \"speed\": \"jog\" },\n"
        "    { \"id\": \"scenario.ai.action.try_walk_to_target_prop\", \"kind\": \"scenario.ai.action.try_walk_to_target_prop\", \"source\": \"ai/ailists.json\", \"target\": \"chr.target_prop_movement\", \"opcode\": \"0x002c\", \"speed\": \"walk\" },\n"
        "    { \"id\": \"scenario.ai.action.try_run_to_target_prop\", \"kind\": \"scenario.ai.action.try_run_to_target_prop\", \"source\": \"ai/ailists.json\", \"target\": \"chr.target_prop_movement\", \"opcode\": \"0x002d\", \"speed\": \"run\" },\n"
        "    { \"id\": \"scenario.ai.action.try_go_to_cover_prop\", \"kind\": \"scenario.ai.action.try_go_to_cover_prop\", \"source\": \"ai/ailists.json\", \"target\": \"chr.cover_prop_movement\", \"opcode\": \"0x002e\" },\n"
        "    { \"id\": \"scenario.ai.action.try_jog_to_chr\", \"kind\": \"scenario.ai.action.try_jog_to_chr\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.movement\", \"opcode\": \"0x002f\", \"speed\": \"jog\" },\n"
        "    { \"id\": \"scenario.ai.action.try_walk_to_chr\", \"kind\": \"scenario.ai.action.try_walk_to_chr\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.movement\", \"opcode\": \"0x0030\", \"speed\": \"walk\" },\n"
        "    { \"id\": \"scenario.ai.action.try_run_to_chr\", \"kind\": \"scenario.ai.action.try_run_to_chr\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.movement\", \"opcode\": \"0x0031\", \"speed\": \"run\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_can_hear_alarm\", \"kind\": \"scenario.ai.condition.if_can_hear_alarm\", \"source\": \"ai/ailists.json\", \"target\": \"global.alarm\", \"opcode\": \"0x0039\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_patrolling\", \"kind\": \"scenario.ai.condition.if_patrolling\", \"source\": \"ai/ailists.json\", \"target\": \"chr.patrol_state\", \"opcode\": \"0x0023\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_alarm_active\", \"kind\": \"scenario.ai.condition.if_alarm_active\", \"source\": \"ai/ailists.json\", \"target\": \"global.alarm\", \"opcode\": \"0x003a\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_gas_active\", \"kind\": \"scenario.ai.condition.if_gas_active\", \"source\": \"ai/ailists.json\", \"target\": \"global.gas\", \"opcode\": \"0x003b\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_hears_target\", \"kind\": \"scenario.ai.condition.if_hears_target\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.audio\", \"opcode\": \"0x003c\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_saw_injury\", \"kind\": \"scenario.ai.condition.if_saw_injury\", \"source\": \"ai/ailists.json\", \"target\": \"chr.perception.injury\", \"opcode\": \"0x003d\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_saw_death\", \"kind\": \"scenario.ai.condition.if_saw_death\", \"source\": \"ai/ailists.json\", \"target\": \"chr.perception.death\", \"opcode\": \"0x003e\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_los_to_target\", \"kind\": \"scenario.ai.condition.if_los_to_target\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.line_of_sight\", \"opcode\": \"0x003f\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_los_to_attack_target\", \"kind\": \"scenario.ai.condition.if_los_to_attack_target\", \"source\": \"ai/ailists.json\", \"target\": \"attack_target.line_of_sight\", \"opcode\": \"0x017a\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_target_nearly_in_sight\", \"kind\": \"scenario.ai.condition.if_target_nearly_in_sight\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.nearly_in_sight\", \"opcode\": \"0x0040\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_nearly_in_targets_sight\", \"kind\": \"scenario.ai.condition.if_nearly_in_targets_sight\", \"source\": \"ai/ailists.json\", \"target\": \"chr.nearly_in_targets_sight\", \"opcode\": \"0x0041\" },\n"
        "    { \"id\": \"scenario.ai.action.set_pad_preset_to_pad_on_route_to_target\", \"kind\": \"scenario.ai.action.set_pad_preset_to_pad_on_route_to_target\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"paths\": \"navigation/paths.json\", \"target\": \"chr.padpreset1\", \"opcode\": \"0x0042\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_saw_target_recently\", \"kind\": \"scenario.ai.condition.if_saw_target_recently\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.recent_sight\", \"opcode\": \"0x0043\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_heard_target_recently\", \"kind\": \"scenario.ai.condition.if_heard_target_recently\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.recent_audio\", \"opcode\": \"0x0044\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_los_to_chr\", \"kind\": \"scenario.ai.condition.if_los_to_chr\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.line_of_sight\", \"opcode\": \"0x0045\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_never_been_on_screen\", \"kind\": \"scenario.ai.condition.if_never_been_on_screen\", \"source\": \"ai/ailists.json\", \"target\": \"chr.screen_history\", \"opcode\": \"0x0046\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_on_screen\", \"kind\": \"scenario.ai.condition.if_on_screen\", \"source\": \"ai/ailists.json\", \"target\": \"chr.screen_state\", \"opcode\": \"0x0047\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_chr_in_on_screen_room\", \"kind\": \"scenario.ai.condition.if_chr_in_on_screen_room\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.room_visibility\", \"opcode\": \"0x0048\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_room_is_on_screen\", \"kind\": \"scenario.ai.condition.if_room_is_on_screen\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"pad.room_visibility\", \"opcode\": \"0x0049\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_target_aiming_at_me\", \"kind\": \"scenario.ai.condition.if_target_aiming_at_me\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.aim\", \"opcode\": \"0x004a\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_near_miss\", \"kind\": \"scenario.ai.condition.if_near_miss\", \"source\": \"ai/ailists.json\", \"target\": \"chr.near_miss_latch\", \"opcode\": \"0x004b\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_sees_suspicious_item\", \"kind\": \"scenario.ai.condition.if_sees_suspicious_item\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.suspicious_visibility\", \"opcode\": \"0x004c\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_target_in_fov_left\", \"kind\": \"scenario.ai.condition.if_target_in_fov_left\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.fov_left\", \"opcode\": \"0x004d\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_check_fov_with_target\", \"kind\": \"scenario.ai.condition.if_check_fov_with_target\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.fov\", \"opcode\": \"0x004e\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_target_out_of_fov_left\", \"kind\": \"scenario.ai.condition.if_target_out_of_fov_left\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.fov_left\", \"opcode\": \"0x004f\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_target_in_fov\", \"kind\": \"scenario.ai.condition.if_target_in_fov\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.fov\", \"opcode\": \"0x0050\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_target_out_of_fov\", \"kind\": \"scenario.ai.condition.if_target_out_of_fov\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.fov\", \"opcode\": \"0x0051\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_distance_to_target_less_than\", \"kind\": \"scenario.ai.condition.if_distance_to_target_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.distance\", \"opcode\": \"0x0052\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_distance_to_target_greater_than\", \"kind\": \"scenario.ai.condition.if_distance_to_target_greater_than\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.distance\", \"opcode\": \"0x0053\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_chr_distance_to_pad_less_than\", \"kind\": \"scenario.ai.condition.if_chr_distance_to_pad_less_than\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"target_chr.pad_distance\", \"opcode\": \"0x0054\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_chr_distance_to_pad_greater_than\", \"kind\": \"scenario.ai.condition.if_chr_distance_to_pad_greater_than\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"target_chr.pad_distance\", \"opcode\": \"0x0055\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_distance_to_chr_less_than\", \"kind\": \"scenario.ai.condition.if_distance_to_chr_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.distance\", \"opcode\": \"0x0056\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_distance_to_chr_greater_than\", \"kind\": \"scenario.ai.condition.if_distance_to_chr_greater_than\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.distance\", \"opcode\": \"0x0057\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_any_chr_near_self\", \"kind\": \"scenario.ai.condition.if_any_chr_near_self\", \"source\": \"ai/ailists.json\", \"target\": \"chr.preset_nearby\", \"opcode\": \"0x0058\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_distance_from_target_to_pad_less_than\", \"kind\": \"scenario.ai.condition.if_distance_from_target_to_pad_less_than\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"target_chr.pad_distance\", \"opcode\": \"0x0059\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_distance_from_target_to_pad_greater_than\", \"kind\": \"scenario.ai.condition.if_distance_from_target_to_pad_greater_than\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"target_chr.pad_distance\", \"opcode\": \"0x005a\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_chr_in_room\", \"kind\": \"scenario.ai.condition.if_chr_in_room\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"target_chr.room\", \"opcode\": \"0x005b\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_target_in_room\", \"kind\": \"scenario.ai.condition.if_target_in_room\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"target_chr.room\", \"opcode\": \"0x005c\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_chr_has_object\", \"kind\": \"scenario.ai.condition.if_chr_has_object\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"player.inventory\", \"opcode\": \"0x005d\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_weapon_thrown\", \"kind\": \"scenario.ai.condition.if_weapon_thrown\", \"source\": \"ai/ailists.json\", \"target\": \"weapon.landed\", \"opcode\": \"0x005e\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_weapon_thrown_on_object\", \"kind\": \"scenario.ai.condition.if_weapon_thrown_on_object\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.child_weapon\", \"opcode\": \"0x005f\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_chr_has_weapon_equipped\", \"kind\": \"scenario.ai.condition.if_chr_has_weapon_equipped\", \"source\": \"ai/ailists.json\", \"target\": \"player.weapon\", \"opcode\": \"0x0060\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_gun_unclaimed\", \"kind\": \"scenario.ai.condition.if_gun_unclaimed\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"weapon.claim\", \"opcode\": \"0x0061\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_object_healthy\", \"kind\": \"scenario.ai.condition.if_object_healthy\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.health\", \"opcode\": \"0x0062\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_chr_activated_object\", \"kind\": \"scenario.ai.condition.if_chr_activated_object\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.activation_latch\", \"opcode\": \"0x0063\" },\n"
        "    { \"id\": \"scenario.ai.action.obj_interact\", \"kind\": \"scenario.ai.action.obj_interact\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.interaction\", \"opcode\": \"0x0065\" },\n"
        "    { \"id\": \"scenario.ai.action.destroy_object\", \"kind\": \"scenario.ai.action.destroy_object\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.destroyed_state\", \"opcode\": \"0x0066\" },\n"
        "    { \"id\": \"scenario.ai.action.drop_object_from_chr\", \"kind\": \"scenario.ai.action.drop_object_from_chr\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.parent\", \"opcode\": \"0x0067\" },\n"
        "    { \"id\": \"scenario.ai.action.chr_drop_items\", \"kind\": \"scenario.ai.action.chr_drop_items\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"chr.concealed_items\", \"opcode\": \"0x0068\" },\n"
        "    { \"id\": \"scenario.ai.action.chr_drop_weapon\", \"kind\": \"scenario.ai.action.chr_drop_weapon\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"chr.weapon_inventory\", \"opcode\": \"0x0069\" },\n"
        "    { \"id\": \"scenario.ai.action.give_object_to_chr\", \"kind\": \"scenario.ai.action.give_object_to_chr\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.owner\", \"opcode\": \"0x006a\" },\n"
        "    { \"id\": \"scenario.ai.action.object_move_to_pad\", \"kind\": \"scenario.ai.action.object_move_to_pad\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"pads\": \"pads.json\", \"target\": \"object.transform\", \"opcode\": \"0x006b\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_waypoint_within_quadrant\", \"kind\": \"scenario.ai.condition.if_waypoint_within_quadrant\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"navigation\": \"navigation/waypoints.json\", \"target\": \"chr.padpreset1\", \"opcode\": \"0x0075\" },\n"
        "    { \"id\": \"scenario.ai.action.set_pad_preset_to_target_quadrant\", \"kind\": \"scenario.ai.action.set_pad_preset_to_target_quadrant\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"navigation\": \"navigation/waypoints.json\", \"target\": \"chr.padpreset1\", \"opcode\": \"0x0076\" },\n"
        "    { \"id\": \"scenario.ai.action.chr_do_animation\", \"kind\": \"scenario.ai.action.chr_do_animation\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.animation\", \"opcode\": \"0x000b\" },\n"
        "    { \"id\": \"scenario.ai.action.be_surprised_one_hand\", \"kind\": \"scenario.ai.action.be_surprised_one_hand\", \"source\": \"ai/ailists.json\", \"target\": \"chr.reaction\", \"opcode\": \"0x000d\" },\n"
        "    { \"id\": \"scenario.ai.action.be_surprised_look_around\", \"kind\": \"scenario.ai.action.be_surprised_look_around\", \"source\": \"ai/ailists.json\", \"target\": \"chr.reaction\", \"opcode\": \"0x000e\" },\n"
        "    { \"id\": \"scenario.ai.action.be_surprised_surrender\", \"kind\": \"scenario.ai.action.be_surprised_surrender\", \"source\": \"ai/ailists.json\", \"target\": \"chr.reaction\", \"opcode\": \"0x00ff\" },\n"
        "    { \"id\": \"scenario.ai.action.random\", \"kind\": \"scenario.ai.action.random\", \"source\": \"ai/ailists.json\", \"target\": \"chr.random\", \"opcode\": \"0x0036\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_random_less_than\", \"kind\": \"scenario.ai.condition.if_random_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"chr.random\", \"opcode\": \"0x0037\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_random_greater_than\", \"kind\": \"scenario.ai.condition.if_random_greater_than\", \"source\": \"ai/ailists.json\", \"target\": \"chr.random\", \"opcode\": \"0x0038\" },\n"
        "    { \"id\": \"scenario.ai.action.print\", \"kind\": \"scenario.ai.action.print\", \"source\": \"ai/ailists.json\", \"target\": \"debug.console\", \"opcode\": \"0x00b5\" },\n"
        "    { \"id\": \"scenario.ai.action.noop\", \"kind\": \"scenario.ai.action.noop\", \"source\": \"ai/ailists.json\", \"target\": \"interpreter.offset\", \"opcodes\": [\"0x0091\", \"0x00d8\", \"0x00d9\", \"0x00db\", \"0x0100\", \"0x0101\", \"0x010d\", \"0x016c\", \"0x01bb\"] },\n"
        "    { \"id\": \"scenario.ai.action.set_punch_dodge_list\", \"kind\": \"scenario.ai.action.set_punch_dodge_list\", \"source\": \"ai/ailists.json\", \"target\": \"chr.aipunchdodgelist\", \"opcode\": \"0x01c1\" },\n"
        "    { \"id\": \"scenario.ai.action.set_shooting_at_me_list\", \"kind\": \"scenario.ai.action.set_shooting_at_me_list\", \"source\": \"ai/ailists.json\", \"target\": \"chr.aishootingatmelist\", \"opcode\": \"0x01c2\" },\n"
        "    { \"id\": \"scenario.ai.action.set_dark_room_list\", \"kind\": \"scenario.ai.action.set_dark_room_list\", \"source\": \"ai/ailists.json\", \"target\": \"chr.aidarkroomlist\", \"opcode\": \"0x01c3\" },\n"
        "    { \"id\": \"scenario.ai.action.set_player_dead_list\", \"kind\": \"scenario.ai.action.set_player_dead_list\", \"source\": \"ai/ailists.json\", \"target\": \"chr.aiplayerdeadlist\", \"opcode\": \"0x01c4\" },\n"
        "    { \"id\": \"scenario.ai.action.jog_to_pad\", \"kind\": \"scenario.ai.action.jog_to_pad\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"opcode\": \"0x001d\", \"speed\": \"jog\" },\n"
        "    { \"id\": \"scenario.ai.action.go_to_pad_preset\", \"kind\": \"scenario.ai.action.go_to_pad_preset\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"opcode\": \"0x001e\", \"pad\": \"chr.padpreset1\" },\n"
        "    { \"id\": \"scenario.ai.action.walk_to_pad\", \"kind\": \"scenario.ai.action.walk_to_pad\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"opcode\": \"0x001f\", \"speed\": \"walk\" },\n"
        "    { \"id\": \"scenario.ai.action.run_to_pad\", \"kind\": \"scenario.ai.action.run_to_pad\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"opcode\": \"0x0020\", \"speed\": \"run\" },\n"
        "    { \"id\": \"scenario.ai.action.set_path\", \"kind\": \"scenario.ai.action.set_path\", \"source\": \"ai/ailists.json\", \"paths\": \"navigation/paths.json\", \"opcode\": \"0x0021\" },\n"
        "    { \"id\": \"scenario.ai.action.start_patrol\", \"kind\": \"scenario.ai.action.start_patrol\", \"source\": \"ai/ailists.json\", \"paths\": \"navigation/paths.json\", \"opcode\": \"0x0022\" },\n"
        "    { \"id\": \"scenario.ai.action.try_start_alarm\", \"kind\": \"scenario.ai.action.try_start_alarm\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"global.alarm\", \"opcode\": \"0x0027\" },\n"
        "    { \"id\": \"scenario.ai.action.activate_alarm\", \"kind\": \"scenario.ai.action.activate_alarm\", \"source\": \"ai/ailists.json\", \"target\": \"global.alarm\", \"opcode\": \"0x0028\" },\n"
        "    { \"id\": \"scenario.ai.action.deactivate_alarm\", \"kind\": \"scenario.ai.action.deactivate_alarm\", \"source\": \"ai/ailists.json\", \"target\": \"global.alarm\", \"opcode\": \"0x0029\" },\n"
        "    { \"id\": \"scenario.ai.action.set_morale\", \"kind\": \"scenario.ai.action.set_morale\", \"source\": \"ai/ailists.json\", \"target\": \"chr.morale\", \"operation\": \"set\", \"opcode\": \"0x0084\" },\n"
        "    { \"id\": \"scenario.ai.action.add_morale\", \"kind\": \"scenario.ai.action.add_morale\", \"source\": \"ai/ailists.json\", \"target\": \"chr.morale\", \"operation\": \"add\", \"opcode\": \"0x0085\" },\n"
        "    { \"id\": \"scenario.ai.action.chr_add_morale\", \"kind\": \"scenario.ai.action.chr_add_morale\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.morale\", \"operation\": \"add\", \"opcode\": \"0x0086\" },\n"
        "    { \"id\": \"scenario.ai.action.subtract_morale\", \"kind\": \"scenario.ai.action.subtract_morale\", \"source\": \"ai/ailists.json\", \"target\": \"chr.morale\", \"operation\": \"subtract\", \"opcode\": \"0x0087\" },\n"
        "    { \"id\": \"scenario.ai.action.set_alertness\", \"kind\": \"scenario.ai.action.set_alertness\", \"source\": \"ai/ailists.json\", \"target\": \"chr.alertness\", \"operation\": \"set\", \"opcode\": \"0x008a\" },\n"
        "    { \"id\": \"scenario.ai.action.add_alertness\", \"kind\": \"scenario.ai.action.add_alertness\", \"source\": \"ai/ailists.json\", \"target\": \"chr.alertness\", \"operation\": \"add\", \"opcode\": \"0x008b\" },\n"
        "    { \"id\": \"scenario.ai.action.chr_add_alertness\", \"kind\": \"scenario.ai.action.chr_add_alertness\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.alertness\", \"operation\": \"add\", \"opcode\": \"0x008c\" },\n"
        "    { \"id\": \"scenario.ai.action.subtract_alertness\", \"kind\": \"scenario.ai.action.subtract_alertness\", \"source\": \"ai/ailists.json\", \"target\": \"chr.alertness\", \"operation\": \"subtract\", \"opcode\": \"0x008d\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_num_arghs_less_than\", \"kind\": \"scenario.ai.condition.if_num_arghs_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"chr.recovery_reactions\", \"opcode\": \"0x007d\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_num_arghs_greater_than\", \"kind\": \"scenario.ai.condition.if_num_arghs_greater_than\", \"source\": \"ai/ailists.json\", \"target\": \"chr.recovery_reactions\", \"opcode\": \"0x007e\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_num_close_arghs_less_than\", \"kind\": \"scenario.ai.condition.if_num_close_arghs_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"chr.close_recovery_reactions\", \"opcode\": \"0x007f\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_num_close_arghs_greater_than\", \"kind\": \"scenario.ai.condition.if_num_close_arghs_greater_than\", \"source\": \"ai/ailists.json\", \"target\": \"chr.close_recovery_reactions\", \"opcode\": \"0x0080\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_chr_health_greater_than\", \"kind\": \"scenario.ai.condition.if_chr_health_greater_than\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.health\", \"opcode\": \"0x0081\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_chr_health_less_than\", \"kind\": \"scenario.ai.condition.if_chr_health_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.health\", \"opcode\": \"0x0082\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_chr_shield_less_than\", \"kind\": \"scenario.ai.condition.if_chr_shield_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.shield\", \"opcode\": \"0x010f\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_chr_shield_greater_than\", \"kind\": \"scenario.ai.condition.if_chr_shield_greater_than\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.shield\", \"opcode\": \"0x0110\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_injured\", \"kind\": \"scenario.ai.condition.if_injured\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.injury_latch\", \"opcode\": \"0x0083\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_shield_damaged\", \"kind\": \"scenario.ai.condition.if_shield_damaged\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.shield_damage_latch\", \"opcode\": \"0x0168\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_morale_less_than\", \"kind\": \"scenario.ai.condition.if_morale_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"chr.morale\", \"opcode\": \"0x0088\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_morale_less_than_random\", \"kind\": \"scenario.ai.condition.if_morale_less_than_random\", \"source\": \"ai/ailists.json\", \"target\": \"chr.morale_random\", \"opcode\": \"0x0089\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_alertness\", \"kind\": \"scenario.ai.condition.if_alertness\", \"source\": \"ai/ailists.json\", \"target\": \"chr.alertness\", \"opcode\": \"0x008e\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_chr_alertness_less_than\", \"kind\": \"scenario.ai.condition.if_chr_alertness_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.alertness\", \"opcode\": \"0x008f\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_alertness_less_than_random\", \"kind\": \"scenario.ai.condition.if_alertness_less_than_random\", \"source\": \"ai/ailists.json\", \"target\": \"chr.alertness_random\", \"opcode\": \"0x0090\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_idle\", \"kind\": \"scenario.ai.condition.if_idle\", \"source\": \"ai/ailists.json\", \"target\": \"chr.action_state\", \"opcode\": \"0x000c\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_stopped\", \"kind\": \"scenario.ai.condition.if_stopped\", \"source\": \"ai/ailists.json\", \"target\": \"chr.motion_state\", \"opcode\": \"0x0032\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_chr_dead\", \"kind\": \"scenario.ai.condition.if_chr_dead\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.life_state\", \"opcode\": \"0x0033\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_chr_death_animation_finished\", \"kind\": \"scenario.ai.condition.if_chr_death_animation_finished\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.death_animation\", \"opcode\": \"0x0034\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_chr_knocked_out\", \"kind\": \"scenario.ai.condition.if_chr_knocked_out\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.knockout_state\", \"opcode\": \"0x017b\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_can_see_target\", \"kind\": \"scenario.ai.condition.if_can_see_target\", \"source\": \"ai/ailists.json\", \"target\": \"chr.target_visibility\", \"opcode\": \"0x0035\" },\n"
        "    { \"id\": \"scenario.ai.action.increase_squadron_alertness\", \"kind\": \"scenario.ai.action.increase_squadron_alertness\", \"source\": \"ai/ailists.json\", \"target\": \"squadron.alertness\", \"operation\": \"increase\", \"opcode\": \"0x0131\" },\n"
        "    { \"id\": \"scenario.ai.action.set_hear_distance\", \"kind\": \"scenario.ai.action.set_hear_distance\", \"source\": \"ai/ailists.json\", \"target\": \"chr.hearing_scale\", \"opcode\": \"0x0092\" },\n"
        "    { \"id\": \"scenario.ai.action.set_view_distance\", \"kind\": \"scenario.ai.action.set_view_distance\", \"source\": \"ai/ailists.json\", \"target\": \"chr.vision_range\", \"opcode\": \"0x0093\" },\n"
        "    { \"id\": \"scenario.ai.action.set_grenade_probability\", \"kind\": \"scenario.ai.action.set_grenade_probability\", \"source\": \"ai/ailists.json\", \"target\": \"chr.grenade_probability\", \"opcode\": \"0x0094\" },\n"
        "    { \"id\": \"scenario.ai.action.set_chr_num\", \"kind\": \"scenario.ai.action.set_chr_num\", \"source\": \"ai/ailists.json\", \"target\": \"chr.number\", \"opcode\": \"0x0095\" },\n"
        "    { \"id\": \"scenario.ai.action.set_max_damage\", \"kind\": \"scenario.ai.action.set_max_damage\", \"source\": \"ai/ailists.json\", \"target\": \"chr.max_damage\", \"opcode\": \"0x0096\" },\n"
        "    { \"id\": \"scenario.ai.action.add_health\", \"kind\": \"scenario.ai.action.add_health\", \"source\": \"ai/ailists.json\", \"target\": \"chr.health\", \"operation\": \"add\", \"opcode\": \"0x0097\" },\n"
        "    { \"id\": \"scenario.ai.action.set_shield\", \"kind\": \"scenario.ai.action.set_shield\", \"source\": \"ai/ailists.json\", \"target\": \"chr.shield\", \"opcode\": \"0x010e\" },\n"
        "    { \"id\": \"scenario.ai.action.set_reaction_speed\", \"kind\": \"scenario.ai.action.set_reaction_speed\", \"source\": \"ai/ailists.json\", \"target\": \"chr.speed_rating\", \"opcode\": \"0x0098\" },\n"
        "    { \"id\": \"scenario.ai.action.set_recovery_speed\", \"kind\": \"scenario.ai.action.set_recovery_speed\", \"source\": \"ai/ailists.json\", \"target\": \"chr.recovery_rating\", \"opcode\": \"0x0099\" },\n"
        "    { \"id\": \"scenario.ai.action.set_accuracy\", \"kind\": \"scenario.ai.action.set_accuracy\", \"source\": \"ai/ailists.json\", \"target\": \"chr.accuracy_rating\", \"opcode\": \"0x009a\" },\n"
        "    { \"id\": \"scenario.ai.action.set_dodge_rating\", \"kind\": \"scenario.ai.action.set_dodge_rating\", \"source\": \"ai/ailists.json\", \"target\": \"chr.dodge_rating\", \"opcode\": \"0x01c6\" },\n"
        "    { \"id\": \"scenario.ai.action.set_unarmed_dodge_rating\", \"kind\": \"scenario.ai.action.set_unarmed_dodge_rating\", \"source\": \"ai/ailists.json\", \"target\": \"chr.unarmed_dodge_rating\", \"opcode\": \"0x01c7\" },\n"
        "    { \"id\": \"scenario.ai.action.set_flag\", \"kind\": \"scenario.ai.action.set_flag\", \"source\": \"ai/ailists.json\", \"target\": \"chr.flags\", \"opcode\": \"0x009b\" },\n"
        "    { \"id\": \"scenario.ai.action.unset_flag\", \"kind\": \"scenario.ai.action.unset_flag\", \"source\": \"ai/ailists.json\", \"target\": \"chr.flags\", \"opcode\": \"0x009c\" },\n"
        "    { \"id\": \"scenario.ai.action.if_has_flag\", \"kind\": \"scenario.ai.action.if_has_flag\", \"source\": \"ai/ailists.json\", \"target\": \"chr.flags\", \"opcode\": \"0x009d\" },\n"
        "    { \"id\": \"scenario.ai.action.chr_set_flag\", \"kind\": \"scenario.ai.action.chr_set_flag\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.flags\", \"opcode\": \"0x009e\" },\n"
        "    { \"id\": \"scenario.ai.action.chr_unset_flag\", \"kind\": \"scenario.ai.action.chr_unset_flag\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.flags\", \"opcode\": \"0x009f\" },\n"
        "    { \"id\": \"scenario.ai.action.if_chr_has_flag\", \"kind\": \"scenario.ai.action.if_chr_has_flag\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.flags\", \"opcode\": \"0x00a0\" },\n"
        "    { \"id\": \"scenario.ai.action.set_stage_flag\", \"kind\": \"scenario.ai.action.set_stage_flag\", \"source\": \"ai/ailists.json\", \"target\": \"mission.stage_flags\", \"opcode\": \"0x00a1\" },\n"
        "    { \"id\": \"scenario.ai.action.unset_stage_flag\", \"kind\": \"scenario.ai.action.unset_stage_flag\", \"source\": \"ai/ailists.json\", \"target\": \"mission.stage_flags\", \"opcode\": \"0x00a2\" },\n"
        "    { \"id\": \"scenario.ai.action.if_stage_flag_eq\", \"kind\": \"scenario.ai.action.if_stage_flag_eq\", \"source\": \"ai/ailists.json\", \"target\": \"mission.stage_flags\", \"opcode\": \"0x00a3\" },\n"
        "    { \"id\": \"scenario.ai.action.set_chrflag\", \"kind\": \"scenario.ai.action.set_chrflag\", \"source\": \"ai/ailists.json\", \"target\": \"chr.chrflags\", \"opcode\": \"0x00a4\" },\n"
        "    { \"id\": \"scenario.ai.action.unset_chrflag\", \"kind\": \"scenario.ai.action.unset_chrflag\", \"source\": \"ai/ailists.json\", \"target\": \"chr.chrflags\", \"opcode\": \"0x00a5\" },\n"
        "    { \"id\": \"scenario.ai.action.if_has_chrflag\", \"kind\": \"scenario.ai.action.if_has_chrflag\", \"source\": \"ai/ailists.json\", \"target\": \"chr.chrflags\", \"opcode\": \"0x00a6\" },\n"
        "    { \"id\": \"scenario.ai.action.chr_set_chrflag\", \"kind\": \"scenario.ai.action.chr_set_chrflag\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.chrflags\", \"opcode\": \"0x00a7\" },\n"
        "    { \"id\": \"scenario.ai.action.chr_unset_chrflag\", \"kind\": \"scenario.ai.action.chr_unset_chrflag\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.chrflags\", \"opcode\": \"0x00a8\" },\n"
        "    { \"id\": \"scenario.ai.action.if_chr_has_chrflag\", \"kind\": \"scenario.ai.action.if_chr_has_chrflag\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.chrflags\", \"opcode\": \"0x00a9\" },\n"
        "    { \"id\": \"scenario.ai.action.set_obj_flag\", \"kind\": \"scenario.ai.action.set_obj_flag\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.flags\", \"opcodes\": [\"0x00aa\", \"0x00ad\", \"0x0118\"] },\n"
        "    { \"id\": \"scenario.ai.action.unset_obj_flag\", \"kind\": \"scenario.ai.action.unset_obj_flag\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.flags\", \"opcodes\": [\"0x00ab\", \"0x00ae\", \"0x0119\"] },\n"
        "    { \"id\": \"scenario.ai.action.if_obj_has_flag\", \"kind\": \"scenario.ai.action.if_obj_has_flag\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.flags\", \"opcodes\": [\"0x00ac\", \"0x00af\", \"0x011a\"] },\n"
        "    { \"id\": \"scenario.ai.action.open_door\", \"kind\": \"scenario.ai.action.open_door\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"door.mode\", \"opcode\": \"0x006c\" },\n"
        "    { \"id\": \"scenario.ai.action.close_door\", \"kind\": \"scenario.ai.action.close_door\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"door.mode\", \"opcode\": \"0x006d\" },\n"
        "    { \"id\": \"scenario.ai.action.if_door_state\", \"kind\": \"scenario.ai.action.if_door_state\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"door.mode\", \"opcode\": \"0x006e\" },\n"
        "    { \"id\": \"scenario.ai.action.if_object_is_door\", \"kind\": \"scenario.ai.action.if_object_is_door\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.type\", \"opcode\": \"0x006f\" },\n"
        "    { \"id\": \"scenario.ai.action.lock_door\", \"kind\": \"scenario.ai.action.lock_door\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"door.keyflags\", \"opcode\": \"0x0070\" },\n"
        "    { \"id\": \"scenario.ai.action.unlock_door\", \"kind\": \"scenario.ai.action.unlock_door\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"door.keyflags\", \"opcode\": \"0x0071\" },\n"
        "    { \"id\": \"scenario.ai.action.if_door_locked\", \"kind\": \"scenario.ai.action.if_door_locked\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"door.keyflags\", \"opcode\": \"0x0072\" },\n"
        "    { \"id\": \"scenario.ai.action.if_lift_stationary\", \"kind\": \"scenario.ai.action.if_lift_stationary\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"pads\": \"pads.json\", \"target\": \"lift.motion\", \"opcode\": \"0x0188\" },\n"
        "    { \"id\": \"scenario.ai.action.lift_go_to_stop\", \"kind\": \"scenario.ai.action.lift_go_to_stop\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"pads\": \"pads.json\", \"target\": \"lift.target_level\", \"opcode\": \"0x0189\" },\n"
        "    { \"id\": \"scenario.ai.action.if_lift_at_stop\", \"kind\": \"scenario.ai.action.if_lift_at_stop\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"pads\": \"pads.json\", \"target\": \"lift.current_level\", \"opcode\": \"0x018a\" },\n"
        "    { \"id\": \"scenario.ai.action.activate_lift\", \"kind\": \"scenario.ai.action.activate_lift\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"pads\": \"pads.json\", \"target\": \"lift.registration\", \"opcode\": \"0x018d\" },\n"
        "    { \"id\": \"scenario.ai.action.if_using_lift\", \"kind\": \"scenario.ai.action.if_using_lift\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"pads\": \"pads.json\", \"target\": \"chr.lift\", \"opcode\": \"0x01a5\" },\n"
        "    { \"id\": \"scenario.ai.action.configure_rain\", \"kind\": \"scenario.ai.action.configure_rain\", \"source\": \"ai/ailists.json\", \"globals\": \"scenario.ini\", \"target\": \"weather.rain\", \"opcode\": \"0x018b\" },\n"
        "    { \"id\": \"scenario.ai.action.configure_snow\", \"kind\": \"scenario.ai.action.configure_snow\", \"source\": \"ai/ailists.json\", \"globals\": \"scenario.ini\", \"target\": \"weather.snow\", \"opcode\": \"0x01b6\" },\n"
        "    { \"id\": \"scenario.ai.action.switch_to_alt_sky\", \"kind\": \"scenario.ai.action.switch_to_alt_sky\", \"source\": \"ai/ailists.json\", \"target\": \"sky.transition\", \"opcode\": \"0x00f2\" },\n"
        "    { \"id\": \"scenario.ai.action.set_wind_speed\", \"kind\": \"scenario.ai.action.set_wind_speed\", \"source\": \"ai/ailists.json\", \"target\": \"sky.wind_speed\", \"opcode\": \"0x01b2\" },\n"
        "    { \"id\": \"scenario.ai.action.set_lights\", \"kind\": \"scenario.ai.action.set_lights\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"room.lights\", \"opcode\": \"0x0102\" },\n"
        "    { \"id\": \"scenario.ai.action.set_room_flag\", \"kind\": \"scenario.ai.action.set_room_flag\", \"source\": \"ai/ailists.json\", \"scene\": \"scene.glb\", \"target\": \"room.flags\", \"opcode\": \"0x01d4\" },\n"
        "    { \"id\": \"scenario.ai.action.show_cutscene_chrs\", \"kind\": \"scenario.ai.action.show_cutscene_chrs\", \"source\": \"ai/ailists.json\", \"target\": \"chr.cutscene_visibility\", \"opcode\": \"0x01d5\" },\n"
        "    { \"id\": \"scenario.ai.action.configure_environment\", \"kind\": \"scenario.ai.action.configure_environment\", \"source\": \"ai/ailists.json\", \"globals\": \"scenario.ini\", \"scene\": \"scene.glb\", \"target\": \"environment.room_global_audio\", \"opcode\": \"0x01d6\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_distance_to_target2_less_than\", \"kind\": \"scenario.ai.condition.if_distance_to_target2_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"chr.target_distance2\", \"opcode\": \"0x01d7\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_distance_to_target2_greater_than\", \"kind\": \"scenario.ai.condition.if_distance_to_target2_greater_than\", \"source\": \"ai/ailists.json\", \"target\": \"chr.target_distance2\", \"opcode\": \"0x01d8\" },\n"
        "    { \"id\": \"scenario.ai.action.speak\", \"kind\": \"scenario.ai.action.speak\", \"source\": \"ai/ailists.json\", \"target\": \"chr.subtitle_audio\", \"opcode\": \"0x00cd\" },\n"
        "    { \"id\": \"scenario.ai.action.play_sound\", \"kind\": \"scenario.ai.action.play_sound\", \"source\": \"ai/ailists.json\", \"target\": \"audio.channel\", \"opcode\": \"0x00ce\" },\n"
        "    { \"id\": \"scenario.ai.action.assign_sound\", \"kind\": \"scenario.ai.action.assign_sound\", \"source\": \"ai/ailists.json\", \"target\": \"audio.marker\", \"opcode\": \"0x017c\" },\n"
        "    { \"id\": \"scenario.ai.action.audio_mute_channel\", \"kind\": \"scenario.ai.action.audio_mute_channel\", \"source\": \"ai/ailists.json\", \"target\": \"audio.channel\", \"opcode\": \"0x00d3\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_channel_free\", \"kind\": \"scenario.ai.condition.if_channel_free\", \"source\": \"ai/ailists.json\", \"target\": \"audio.channel\", \"opcode\": \"0x0138\" },\n"
        "    { \"id\": \"scenario.ai.action.set_object_sound_volume\", \"kind\": \"scenario.ai.action.set_object_sound_volume\", \"source\": \"ai/ailists.json\", \"target\": \"audio.channel.volume\", \"opcode\": \"0x00d1\" },\n"
        "    { \"id\": \"scenario.ai.action.set_object_sound_volume_by_distance\", \"kind\": \"scenario.ai.action.set_object_sound_volume_by_distance\", \"source\": \"ai/ailists.json\", \"target\": \"audio.channel.volume\", \"opcode\": \"0x00d2\" },\n"
        "    { \"id\": \"scenario.ai.action.set_object_sound_playing\", \"kind\": \"scenario.ai.action.set_object_sound_playing\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.audio\", \"opcode\": \"0x00cf\" },\n"
        "    { \"id\": \"scenario.ai.action.play_repeating_sound_from_object\", \"kind\": \"scenario.ai.action.play_repeating_sound_from_object\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.audio.repeating\", \"opcode\": \"0x016b\" },\n"
        "    { \"id\": \"scenario.ai.action.play_sound_from_entity\", \"kind\": \"scenario.ai.action.play_sound_from_entity\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"entity.audio\", \"opcode\": \"0x0179\" },\n"
        "    { \"id\": \"scenario.ai.action.play_repeating_sound_from_pad\", \"kind\": \"scenario.ai.action.play_repeating_sound_from_pad\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"pad.audio.repeating\", \"opcode\": \"0x00d0\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_object_sound_volume_less_than\", \"kind\": \"scenario.ai.condition.if_object_sound_volume_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"audio.channel.volume\", \"opcode\": \"0x00d4\" },\n"
        "    { \"id\": \"scenario.ai.action.play_sound_from_prop\", \"kind\": \"scenario.ai.action.play_sound_from_prop\", \"source\": \"ai/ailists.json\", \"target\": \"prop.audio\", \"opcode\": \"0x01d9\" },\n"
        "    { \"id\": \"scenario.ai.action.play_temporary_primary_track\", \"kind\": \"scenario.ai.action.play_temporary_primary_track\", \"source\": \"ai/ailists.json\", \"target\": \"music.primary\", \"opcode\": \"0x01da\" },\n"
        "    { \"id\": \"scenario.ai.action.play_x_track\", \"kind\": \"scenario.ai.action.play_x_track\", \"source\": \"ai/ailists.json\", \"target\": \"music.x_track\", \"opcode\": \"0x00f9\" },\n"
        "    { \"id\": \"scenario.ai.action.stop_x_track\", \"kind\": \"scenario.ai.action.stop_x_track\", \"source\": \"ai/ailists.json\", \"target\": \"music.x_track\", \"opcode\": \"0x00fa\" },\n"
        "    { \"id\": \"scenario.ai.action.play_track_isolated\", \"kind\": \"scenario.ai.action.play_track_isolated\", \"source\": \"ai/ailists.json\", \"target\": \"music.isolated\", \"opcode\": \"0x015b\" },\n"
        "    { \"id\": \"scenario.ai.action.play_default_tracks\", \"kind\": \"scenario.ai.action.play_default_tracks\", \"source\": \"ai/ailists.json\", \"target\": \"music.default\", \"opcode\": \"0x015c\" },\n"
        "    { \"id\": \"scenario.ai.action.play_cutscene_track\", \"kind\": \"scenario.ai.action.play_cutscene_track\", \"source\": \"ai/ailists.json\", \"target\": \"music.cutscene\", \"opcode\": \"0x017d\" },\n"
        "    { \"id\": \"scenario.ai.action.stop_cutscene_track\", \"kind\": \"scenario.ai.action.stop_cutscene_track\", \"source\": \"ai/ailists.json\", \"target\": \"music.cutscene\", \"opcode\": \"0x017e\" },\n"
        "    { \"id\": \"scenario.ai.action.play_temporary_track\", \"kind\": \"scenario.ai.action.play_temporary_track\", \"source\": \"ai/ailists.json\", \"target\": \"music.ambient_temporary\", \"opcode\": \"0x017f\" },\n"
        "    { \"id\": \"scenario.ai.action.stop_ambient_track\", \"kind\": \"scenario.ai.action.stop_ambient_track\", \"source\": \"ai/ailists.json\", \"target\": \"music.ambient_temporary\", \"opcode\": \"0x0180\" },\n"
        "    { \"id\": \"scenario.ai.action.chr_draw_weapon\", \"kind\": \"scenario.ai.action.chr_draw_weapon\", \"source\": \"ai/ailists.json\", \"target\": \"player.weapon\", \"opcode\": \"0x00ec\" },\n"
        "    { \"id\": \"scenario.ai.action.chr_draw_weapon_in_cutscene\", \"kind\": \"scenario.ai.action.chr_draw_weapon_in_cutscene\", \"source\": \"ai/ailists.json\", \"target\": \"player.weapon.cutscene\", \"opcode\": \"0x00ed\" },\n"
        "    { \"id\": \"scenario.ai.action.set_player_force_speed\", \"kind\": \"scenario.ai.action.set_player_force_speed\", \"source\": \"ai/ailists.json\", \"target\": \"player.force_speed\", \"opcode\": \"0x00ee\" },\n"
        "    { \"id\": \"scenario.ai.action.chr_set_invincible\", \"kind\": \"scenario.ai.action.chr_set_invincible\", \"source\": \"ai/ailists.json\", \"target\": \"player.invincible\", \"opcode\": \"0x00f3\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_player_is_invincible\", \"kind\": \"scenario.ai.condition.if_player_is_invincible\", \"source\": \"ai/ailists.json\", \"target\": \"player.invincible\", \"opcode\": \"0x00f8\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_chr_has_no_gun\", \"kind\": \"scenario.ai.condition.if_chr_has_no_gun\", \"source\": \"ai/ailists.json\", \"target\": \"chr.weapon_state\", \"opcode\": \"0x016f\" },\n"
        "    { \"id\": \"scenario.ai.action.chr_delete_weapon\", \"kind\": \"scenario.ai.action.chr_delete_weapon\", \"source\": \"ai/ailists.json\", \"target\": \"chr.weapon_inventory\", \"opcode\": \"0x00e9\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_trigger_shot_list\", \"kind\": \"scenario.ai.condition.if_trigger_shot_list\", \"source\": \"ai/ailists.json\", \"target\": \"chr.shot_list_latch\", \"opcode\": \"0x00fd\" },\n"
        "    { \"id\": \"scenario.ai.action.end_level\", \"kind\": \"scenario.ai.action.end_level\", \"source\": \"ai/ailists.json\", \"target\": \"mission.flow\", \"opcode\": \"0x00dc\" },\n"
        "    { \"id\": \"scenario.ai.action.end_cutscene\", \"kind\": \"scenario.ai.action.end_cutscene\", \"source\": \"ai/ailists.json\", \"target\": \"player.cutscene\", \"opcode\": \"0x00dd\" },\n"
        "    { \"id\": \"scenario.ai.action.warp_jo_to_pad\", \"kind\": \"scenario.ai.action.warp_jo_to_pad\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"player.warp\", \"opcode\": \"0x00de\" },\n"
        "    { \"id\": \"scenario.ai.action.warp_jo_to_tag\", \"kind\": \"scenario.ai.action.warp_jo_to_tag\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"player.warp\", \"opcode\": \"0x00df\" },\n"
        "    { \"id\": \"scenario.ai.action.revoke_control\", \"kind\": \"scenario.ai.action.revoke_control\", \"source\": \"ai/ailists.json\", \"target\": \"player.control\", \"opcode\": \"0x00e0\" },\n"
        "    { \"id\": \"scenario.ai.action.grant_control\", \"kind\": \"scenario.ai.action.grant_control\", \"source\": \"ai/ailists.json\", \"target\": \"player.control\", \"opcode\": \"0x00e1\" },\n"
        "    { \"id\": \"scenario.ai.action.player_fade_in\", \"kind\": \"scenario.ai.action.player_fade_in\", \"source\": \"ai/ailists.json\", \"target\": \"player.fade\", \"opcode\": \"0x00e3\" },\n"
        "    { \"id\": \"scenario.ai.action.players_fade_out\", \"kind\": \"scenario.ai.action.players_fade_out\", \"source\": \"ai/ailists.json\", \"target\": \"player.fade\", \"opcode\": \"0x00e4\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_colour_fade_complete\", \"kind\": \"scenario.ai.condition.if_colour_fade_complete\", \"source\": \"ai/ailists.json\", \"target\": \"player.fade\", \"opcode\": \"0x00e5\" },\n"
        "    { \"id\": \"scenario.ai.action.prepare_warp_orbit\", \"kind\": \"scenario.ai.action.prepare_warp_orbit\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"player.warp\", \"opcode\": \"0x00f4\" },\n"
        "    { \"id\": \"scenario.ai.action.begin_warp_latch\", \"kind\": \"scenario.ai.action.begin_warp_latch\", \"source\": \"ai/ailists.json\", \"target\": \"player.warp_latch\", \"opcode\": \"0x00f5\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_warp_latch_complete\", \"kind\": \"scenario.ai.condition.if_warp_latch_complete\", \"source\": \"ai/ailists.json\", \"target\": \"player.warp_latch\", \"opcode\": \"0x00f6\" },\n"
        "    { \"id\": \"scenario.ai.action.set_camera_animation\", \"kind\": \"scenario.ai.action.set_camera_animation\", \"source\": \"ai/ailists.json\", \"target\": \"player.camera_animation\", \"opcode\": \"0x0111\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_in_cutscene\", \"kind\": \"scenario.ai.condition.if_in_cutscene\", \"source\": \"ai/ailists.json\", \"target\": \"player.cutscene\", \"opcode\": \"0x0113\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_cutscene_button_pressed\", \"kind\": \"scenario.ai.condition.if_cutscene_button_pressed\", \"source\": \"ai/ailists.json\", \"target\": \"player.cutscene\", \"opcode\": \"0x0174\" },\n"
        "    { \"id\": \"scenario.ai.action.reorient_for_cutscene_stop\", \"kind\": \"scenario.ai.action.reorient_for_cutscene_stop\", \"source\": \"ai/ailists.json\", \"target\": \"player.cutscene\", \"opcode\": \"0x0175\" },\n"
        "    { \"id\": \"scenario.ai.action.spawn_chr_at_pad\", \"kind\": \"scenario.ai.action.spawn_chr_at_pad\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"chr.spawn\", \"opcode\": \"0x00c6\" },\n"
        "    { \"id\": \"scenario.ai.action.spawn_chr_at_chr\", \"kind\": \"scenario.ai.action.spawn_chr_at_chr\", \"source\": \"ai/ailists.json\", \"target\": \"chr.spawn\", \"opcode\": \"0x00c7\" },\n"
        "    { \"id\": \"scenario.ai.action.try_equip_weapon\", \"kind\": \"scenario.ai.action.try_equip_weapon\", \"source\": \"ai/ailists.json\", \"target\": \"chr.weapon_inventory\", \"opcode\": \"0x00c8\" },\n"
        "    { \"id\": \"scenario.ai.action.try_equip_hat\", \"kind\": \"scenario.ai.action.try_equip_hat\", \"source\": \"ai/ailists.json\", \"target\": \"chr.hat\", \"opcode\": \"0x00c9\" },\n"
        "    { \"id\": \"scenario.ai.action.set_obj_image\", \"kind\": \"scenario.ai.action.set_obj_image\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.monitor_image\", \"opcode\": \"0x00da\" },\n"
        "    { \"id\": \"scenario.ai.action.object_do_animation\", \"kind\": \"scenario.ai.action.object_do_animation\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.animation\", \"opcode\": \"0x0112\" },\n"
        "    { \"id\": \"scenario.ai.action.set_door_open\", \"kind\": \"scenario.ai.action.set_door_open\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"door.open_state\", \"opcode\": \"0x00e8\" },\n"
        "    { \"id\": \"scenario.ai.action.duplicate_chr\", \"kind\": \"scenario.ai.action.duplicate_chr\", \"source\": \"ai/ailists.json\", \"target\": \"chr.clone\", \"opcode\": \"0x00ca\" },\n"
        "    { \"id\": \"scenario.ai.action.enable_chr\", \"kind\": \"scenario.ai.action.enable_chr\", \"source\": \"ai/ailists.json\", \"target\": \"chr.enabled\", \"opcode\": \"0x0114\" },\n"
        "    { \"id\": \"scenario.ai.action.disable_chr\", \"kind\": \"scenario.ai.action.disable_chr\", \"source\": \"ai/ailists.json\", \"target\": \"chr.enabled\", \"opcode\": \"0x0115\" },\n"
        "    { \"id\": \"scenario.ai.action.enable_obj\", \"kind\": \"scenario.ai.action.enable_obj\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.enabled\", \"opcode\": \"0x0116\" },\n"
        "    { \"id\": \"scenario.ai.action.disable_obj\", \"kind\": \"scenario.ai.action.disable_obj\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.enabled\", \"opcode\": \"0x0117\" },\n"
        "    { \"id\": \"scenario.ai.action.chr_move_to_pad\", \"kind\": \"scenario.ai.action.chr_move_to_pad\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"chr.transform\", \"opcode\": \"0x00e2\" },\n"
        "    { \"id\": \"scenario.ai.action.chr_set_team\", \"kind\": \"scenario.ai.action.chr_set_team\", \"source\": \"ai/ailists.json\", \"target\": \"chr.team\", \"opcode\": \"0x010b\" },\n"
        "    { \"id\": \"scenario.ai.action.damage_chr_by_amount\", \"kind\": \"scenario.ai.action.damage_chr_by_amount\", \"source\": \"ai/ailists.json\", \"target\": \"chr.damage\", \"opcode\": \"0x016e\" },\n"
        "    { \"id\": \"scenario.ai.action.do_preset_animation\", \"kind\": \"scenario.ai.action.do_preset_animation\", \"source\": \"ai/ailists.json\", \"target\": \"chr.animation\", \"opcode\": \"0x01a3\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_player_chr_portal_distance_less_than\", \"kind\": \"scenario.ai.condition.if_player_chr_portal_distance_less_than\", \"source\": \"ai/ailists.json\", \"scene\": \"scene.glb\", \"target\": \"player_chr.portal_distance\", \"opcode\": \"0x01aa\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_chr_reposition_valid\", \"kind\": \"scenario.ai.condition.if_chr_reposition_valid\", \"source\": \"ai/ailists.json\", \"scene\": \"scene.glb\", \"target\": \"chr.reposition\", \"opcode\": \"0x01b4\" },\n"
        "    { \"id\": \"scenario.ai.action.do_gun_command\", \"kind\": \"scenario.ai.action.do_gun_command\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"scene\": \"scene.glb\", \"target\": \"chr.gunprop.command\", \"opcode\": \"0x0170\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_distance_to_gun_less_than\", \"kind\": \"scenario.ai.condition.if_distance_to_gun_less_than\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"scene\": \"scene.glb\", \"target\": \"chr.gunprop.distance\", \"opcode\": \"0x0171\" },\n"
        "    { \"id\": \"scenario.ai.action.recover_gun\", \"kind\": \"scenario.ai.action.recover_gun\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"scene\": \"scene.glb\", \"target\": \"chr.inventory.weapon\", \"opcode\": \"0x0172\" },\n"
        "    { \"id\": \"scenario.ai.action.chr_copy_properties\", \"kind\": \"scenario.ai.action.chr_copy_properties\", \"source\": \"ai/ailists.json\", \"target\": \"chr.properties\", \"opcode\": \"0x0173\" },\n"
        "    { \"id\": \"scenario.ai.action.player_auto_walk\", \"kind\": \"scenario.ai.action.player_auto_walk\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"player.autowalk\", \"opcode\": \"0x0177\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_player_auto_walk_finished\", \"kind\": \"scenario.ai.condition.if_player_auto_walk_finished\", \"source\": \"ai/ailists.json\", \"target\": \"player.autowalk\", \"opcode\": \"0x0178\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_obj_in_room\", \"kind\": \"scenario.ai.condition.if_obj_in_room\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"pads\": \"pads.json\", \"scene\": \"scene.glb\", \"target\": \"object.room\", \"opcode\": \"0x00ef\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_player_looking_at_object\", \"kind\": \"scenario.ai.condition.if_player_looking_at_object\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"scene\": \"scene.glb\", \"target\": \"player.view.object\", \"opcode\": \"0x0181\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_target_is_player\", \"kind\": \"scenario.ai.condition.if_target_is_player\", \"source\": \"ai/ailists.json\", \"target\": \"chr.target.type\", \"opcode\": \"0x0183\" },\n"
        "    { \"id\": \"scenario.ai.action.chr_kill\", \"kind\": \"scenario.ai.action.chr_kill\", \"source\": \"ai/ailists.json\", \"target\": \"character.state\", \"opcode\": \"0x01db\" },\n"
        "    { \"id\": \"scenario.ai.action.remove_weapon_from_inventory\", \"kind\": \"scenario.ai.action.remove_weapon_from_inventory\", \"source\": \"ai/ailists.json\", \"target\": \"player.inventory\", \"opcode\": \"0x01dc\" },\n"
        "    { \"id\": \"scenario.ai.action.clear_inventory\", \"kind\": \"scenario.ai.action.clear_inventory\", \"source\": \"ai/ailists.json\", \"target\": \"player.inventory\", \"opcode\": \"0x01ae\" },\n"
        "    { \"id\": \"scenario.ai.action.release_object\", \"kind\": \"scenario.ai.action.release_object\", \"source\": \"ai/ailists.json\", \"target\": \"player.carry_state\", \"opcode\": \"0x01ad\" },\n"
        "    { \"id\": \"scenario.ai.action.chr_grab_object\", \"kind\": \"scenario.ai.action.chr_grab_object\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"scene\": \"scene.glb\", \"target\": \"player.grab_object\", \"opcode\": \"0x01af\" },\n"
        "    { \"id\": \"scenario.ai.action.toggle_p1p2\", \"kind\": \"scenario.ai.action.toggle_p1p2\", \"source\": \"ai/ailists.json\", \"target\": \"player.assignment\", \"opcode\": \"0x01b3\" },\n"
        "    { \"id\": \"scenario.ai.action.chr_set_p1p2\", \"kind\": \"scenario.ai.action.chr_set_p1p2\", \"source\": \"ai/ailists.json\", \"target\": \"player.assignment\", \"opcode\": \"0x01b5\" },\n"
        "    { \"id\": \"scenario.ai.action.chr_set_cloaked\", \"kind\": \"scenario.ai.action.chr_set_cloaked\", \"source\": \"ai/ailists.json\", \"target\": \"chr.cloak\", \"opcode\": \"0x01b7\" },\n"
        "    { \"id\": \"scenario.ai.action.set_autogun_target_team\", \"kind\": \"scenario.ai.action.set_autogun_target_team\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"autogun.target_team\", \"opcode\": \"0x01b8\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_objective_complete\", \"kind\": \"scenario.ai.condition.if_objective_complete\", \"source\": \"ai/ailists.json\", \"mission\": \"mission.graph.json\", \"target\": \"mission.objective_status\", \"opcode\": \"0x0073\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_objective_failed\", \"kind\": \"scenario.ai.condition.if_objective_failed\", \"source\": \"ai/ailists.json\", \"mission\": \"mission.graph.json\", \"target\": \"mission.objective_status\", \"opcode\": \"0x0074\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_all_objectives_complete\", \"kind\": \"scenario.ai.condition.if_all_objectives_complete\", \"source\": \"ai/ailists.json\", \"mission\": \"mission.graph.json\", \"target\": \"mission.objectives_complete\", \"opcode\": \"0x00f7\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_difficulty_less_than\", \"kind\": \"scenario.ai.condition.if_difficulty_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"mission.difficulty\", \"opcode\": \"0x0077\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_difficulty_greater_than\", \"kind\": \"scenario.ai.condition.if_difficulty_greater_than\", \"source\": \"ai/ailists.json\", \"target\": \"mission.difficulty\", \"opcode\": \"0x0078\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_stage_timer_less_than\", \"kind\": \"scenario.ai.condition.if_stage_timer_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"mission.stage_timer\", \"opcode\": \"0x0079\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_stage_timer_greater_than\", \"kind\": \"scenario.ai.condition.if_stage_timer_greater_than\", \"source\": \"ai/ailists.json\", \"target\": \"mission.stage_timer\", \"opcode\": \"0x007a\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_stage_id_less_than\", \"kind\": \"scenario.ai.condition.if_stage_id_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"mission.stage_id\", \"opcode\": \"0x007b\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_stage_id_greater_than\", \"kind\": \"scenario.ai.condition.if_stage_id_greater_than\", \"source\": \"ai/ailists.json\", \"target\": \"mission.stage_id\", \"opcode\": \"0x007c\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_num_players_less_than\", \"kind\": \"scenario.ai.condition.if_num_players_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"game.local_player_count\", \"opcode\": \"0x00ea\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_kill_count_greater_than\", \"kind\": \"scenario.ai.condition.if_kill_count_greater_than\", \"source\": \"ai/ailists.json\", \"target\": \"mission.kill_count\", \"opcode\": \"0x00fc\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_num_knocked_out_chrs\", \"kind\": \"scenario.ai.condition.if_num_knocked_out_chrs\", \"source\": \"ai/ailists.json\", \"target\": \"match.knockout_count\", \"opcode\": \"0x01ab\" },\n"
        "    { \"id\": \"scenario.ai.action.kill_bond\", \"kind\": \"scenario.ai.action.kill_bond\", \"source\": \"ai/ailists.json\", \"mission\": \"mission.graph.json\", \"target\": \"player.bond.dead\", \"opcode\": \"0x00fe\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_pouncebits_eq\", \"kind\": \"scenario.ai.condition.if_pouncebits_eq\", \"source\": \"ai/ailists.json\", \"target\": \"chr.pouncebits\", \"opcode\": \"0x01bc\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_training_pc_holographed\", \"kind\": \"scenario.ai.condition.if_training_pc_holographed\", \"source\": \"ai/ailists.json\", \"target\": \"training.pc_hologram\", \"opcode\": \"0x01bd\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_player_using_device\", \"kind\": \"scenario.ai.condition.if_player_using_device\", \"source\": \"ai/ailists.json\", \"target\": \"player.device_state\", \"opcode\": \"0x01be\" },\n"
        "    { \"id\": \"scenario.ai.action.chr_begin_or_end_teleport\", \"kind\": \"scenario.ai.action.chr_begin_or_end_teleport\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"player.teleport_state\", \"opcode\": \"0x01bf\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_chr_teleport_full_white\", \"kind\": \"scenario.ai.condition.if_chr_teleport_full_white\", \"source\": \"ai/ailists.json\", \"target\": \"player.teleport_state\", \"opcode\": \"0x01c0\" },\n"
        "    { \"id\": \"scenario.ai.action.chr_set_cutscene_weapon\", \"kind\": \"scenario.ai.action.chr_set_cutscene_weapon\", \"source\": \"ai/ailists.json\", \"target\": \"chr.cutscene_weapon\", \"opcode\": \"0x01ca\" },\n"
        "    { \"id\": \"scenario.ai.action.fade_screen\", \"kind\": \"scenario.ai.action.fade_screen\", \"source\": \"ai/ailists.json\", \"target\": \"screen.fade\", \"opcode\": \"0x01cb\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_fade_complete\", \"kind\": \"scenario.ai.condition.if_fade_complete\", \"source\": \"ai/ailists.json\", \"target\": \"screen.fade\", \"opcode\": \"0x01cc\" },\n"
        "    { \"id\": \"scenario.ai.action.set_chr_hudpiece_visible\", \"kind\": \"scenario.ai.action.set_chr_hudpiece_visible\", \"source\": \"ai/ailists.json\", \"target\": \"chr.hudpiece\", \"opcode\": \"0x01cd\" },\n"
        "    { \"id\": \"scenario.ai.action.set_passive_mode\", \"kind\": \"scenario.ai.action.set_passive_mode\", \"source\": \"ai/ailists.json\", \"target\": \"player.weapon_passive_mode\", \"opcode\": \"0x01ce\" },\n"
        "    { \"id\": \"scenario.ai.action.chr_set_firing_in_cutscene\", \"kind\": \"scenario.ai.action.chr_set_firing_in_cutscene\", \"source\": \"ai/ailists.json\", \"target\": \"chr.weapon_firing\", \"opcode\": \"0x01cf\" },\n"
        "    { \"id\": \"scenario.ai.action.set_portal_flag\", \"kind\": \"scenario.ai.action.set_portal_flag\", \"source\": \"ai/ailists.json\", \"scene\": \"scene.glb\", \"target\": \"portal.flags\", \"opcode\": \"0x01d0\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_music_event_queue_is_empty\", \"kind\": \"scenario.ai.condition.if_music_event_queue_is_empty\", \"source\": \"ai/ailists.json\", \"target\": \"music.event_queue\", \"opcode\": \"0x01dd\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_coop_mode\", \"kind\": \"scenario.ai.condition.if_coop_mode\", \"source\": \"ai/ailists.json\", \"target\": \"game.mode\", \"opcode\": \"0x01de\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_chr_same_floor_distance_to_pad_less_than\", \"kind\": \"scenario.ai.condition.if_chr_same_floor_distance_to_pad_less_than\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"chr.pad_same_floor_distance\", \"opcode\": \"0x01df\" },\n"
        "    { \"id\": \"scenario.ai.action.remove_references_to_chr\", \"kind\": \"scenario.ai.action.remove_references_to_chr\", \"source\": \"ai/ailists.json\", \"target\": \"chr.references\", \"opcode\": \"0x01e0\" },\n"
        "    { \"id\": \"scenario.ai.action.chr_toggle_model_part\", \"kind\": \"scenario.ai.action.chr_toggle_model_part\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"chr.model_part.visibility\", \"opcode\": \"0x018c\" },\n"
        "    { \"id\": \"scenario.ai.action.obj_set_model_part_visible\", \"kind\": \"scenario.ai.action.obj_set_model_part_visible\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.model_part.visibility\", \"opcode\": \"0x01d1\" },\n"
        "    { \"id\": \"scenario.ai.action.if_obj_health_less_than\", \"kind\": \"scenario.ai.action.if_obj_health_less_than\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.damage\", \"opcode\": \"0x019e\" },\n"
        "    { \"id\": \"scenario.ai.action.set_obj_health\", \"kind\": \"scenario.ai.action.set_obj_health\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"object.damage\", \"opcode\": \"0x019f\" },\n"
        "    { \"id\": \"scenario.ai.action.set_chr_special_death_animation\", \"kind\": \"scenario.ai.action.set_chr_special_death_animation\", \"source\": \"ai/ailists.json\", \"target\": \"chr.special_death_animation\", \"opcode\": \"0x01a0\" },\n"
        "    { \"id\": \"scenario.ai.action.set_room_to_search\", \"kind\": \"scenario.ai.action.set_room_to_search\", \"source\": \"ai/ailists.json\", \"scene\": \"scene.glb\", \"target\": \"chr.room_to_search\", \"opcode\": \"0x01a1\" },\n"
        "    { \"id\": \"scenario.ai.action.chr_set_hidden_flag\", \"kind\": \"scenario.ai.action.chr_set_hidden_flag\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.hidden\", \"opcode\": \"0x011b\" },\n"
        "    { \"id\": \"scenario.ai.action.chr_unset_hidden_flag\", \"kind\": \"scenario.ai.action.chr_unset_hidden_flag\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.hidden\", \"opcode\": \"0x011c\" },\n"
        "    { \"id\": \"scenario.ai.action.if_chr_has_hidden_flag\", \"kind\": \"scenario.ai.action.if_chr_has_hidden_flag\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.hidden\", \"opcode\": \"0x011d\" },\n"
        "    { \"id\": \"scenario.ai.action.set_savefile_flag\", \"kind\": \"scenario.ai.action.set_savefile_flag\", \"source\": \"ai/ailists.json\", \"target\": \"savefile.flags\", \"opcode\": \"0x0190\" },\n"
        "    { \"id\": \"scenario.ai.action.unset_savefile_flag\", \"kind\": \"scenario.ai.action.unset_savefile_flag\", \"source\": \"ai/ailists.json\", \"target\": \"savefile.flags\", \"opcode\": \"0x0191\" },\n"
        "    { \"id\": \"scenario.ai.action.if_savefile_flag_set\", \"kind\": \"scenario.ai.action.if_savefile_flag_set\", \"source\": \"ai/ailists.json\", \"target\": \"savefile.flags\", \"opcode\": \"0x0192\" },\n"
        "    { \"id\": \"scenario.ai.action.if_savefile_flag_unset\", \"kind\": \"scenario.ai.action.if_savefile_flag_unset\", \"source\": \"ai/ailists.json\", \"target\": \"savefile.flags\", \"opcode\": \"0x0193\" },\n"
        "    { \"id\": \"scenario.ai.action.restart_timer\", \"kind\": \"scenario.ai.action.restart_timer\", \"source\": \"ai/ailists.json\", \"target\": \"chr.timer\", \"opcode\": \"0x00b6\" },\n"
        "    { \"id\": \"scenario.ai.action.reset_timer\", \"kind\": \"scenario.ai.action.reset_timer\", \"source\": \"ai/ailists.json\", \"target\": \"chr.timer\", \"opcode\": \"0x00b7\" },\n"
        "    { \"id\": \"scenario.ai.action.pause_timer\", \"kind\": \"scenario.ai.action.pause_timer\", \"source\": \"ai/ailists.json\", \"target\": \"chr.timer\", \"opcode\": \"0x00b8\" },\n"
        "    { \"id\": \"scenario.ai.action.resume_timer\", \"kind\": \"scenario.ai.action.resume_timer\", \"source\": \"ai/ailists.json\", \"target\": \"chr.timer\", \"opcode\": \"0x00b9\" },\n"
        "    { \"id\": \"scenario.ai.action.if_timer_stopped\", \"kind\": \"scenario.ai.action.if_timer_stopped\", \"source\": \"ai/ailists.json\", \"target\": \"chr.timer\", \"opcode\": \"0x00ba\" },\n"
        "    { \"id\": \"scenario.ai.action.if_timer_greater_than_random\", \"kind\": \"scenario.ai.action.if_timer_greater_than_random\", \"source\": \"ai/ailists.json\", \"target\": \"chr.timer\", \"opcode\": \"0x00bb\" },\n"
        "    { \"id\": \"scenario.ai.action.if_timer_less_than\", \"kind\": \"scenario.ai.action.if_timer_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"chr_or_hovercar.timer\", \"opcode\": \"0x00bc\" },\n"
        "    { \"id\": \"scenario.ai.action.if_timer_greater_than\", \"kind\": \"scenario.ai.action.if_timer_greater_than\", \"source\": \"ai/ailists.json\", \"target\": \"chr_or_hovercar.timer\", \"opcode\": \"0x00bd\" },\n"
        "    { \"id\": \"scenario.ai.action.show_countdown_timer\", \"kind\": \"scenario.ai.action.show_countdown_timer\", \"source\": \"ai/ailists.json\", \"target\": \"hud.countdown\", \"opcode\": \"0x00be\" },\n"
        "    { \"id\": \"scenario.ai.action.hide_countdown_timer\", \"kind\": \"scenario.ai.action.hide_countdown_timer\", \"source\": \"ai/ailists.json\", \"target\": \"hud.countdown\", \"opcode\": \"0x00bf\" },\n"
        "    { \"id\": \"scenario.ai.action.set_countdown_timer\", \"kind\": \"scenario.ai.action.set_countdown_timer\", \"source\": \"ai/ailists.json\", \"target\": \"hud.countdown\", \"opcode\": \"0x00c0\" },\n"
        "    { \"id\": \"scenario.ai.action.stop_countdown_timer\", \"kind\": \"scenario.ai.action.stop_countdown_timer\", \"source\": \"ai/ailists.json\", \"target\": \"hud.countdown\", \"opcode\": \"0x00c1\" },\n"
        "    { \"id\": \"scenario.ai.action.start_countdown_timer\", \"kind\": \"scenario.ai.action.start_countdown_timer\", \"source\": \"ai/ailists.json\", \"target\": \"hud.countdown\", \"opcode\": \"0x00c2\" },\n"
        "    { \"id\": \"scenario.ai.action.if_countdown_timer_stopped\", \"kind\": \"scenario.ai.action.if_countdown_timer_stopped\", \"source\": \"ai/ailists.json\", \"target\": \"hud.countdown\", \"opcode\": \"0x00c3\" },\n"
        "    { \"id\": \"scenario.ai.action.if_countdown_timer_less_than\", \"kind\": \"scenario.ai.action.if_countdown_timer_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"hud.countdown\", \"opcode\": \"0x00c4\" },\n"
        "    { \"id\": \"scenario.ai.action.if_countdown_timer_greater_than\", \"kind\": \"scenario.ai.action.if_countdown_timer_greater_than\", \"source\": \"ai/ailists.json\", \"target\": \"hud.countdown\", \"opcode\": \"0x00c5\" },\n"
        "    { \"id\": \"scenario.ai.action.show_hudmsg\", \"kind\": \"scenario.ai.action.show_hudmsg\", \"source\": \"ai/ailists.json\", \"target\": \"hud.message\", \"opcode\": \"0x00cb\" },\n"
        "    { \"id\": \"scenario.ai.action.show_hudmsg_top_middle\", \"kind\": \"scenario.ai.action.show_hudmsg_top_middle\", \"source\": \"ai/ailists.json\", \"target\": \"hud.subtitle\", \"opcode\": \"0x00cc\" },\n"
        "    { \"id\": \"scenario.ai.action.show_hudmsg_middle\", \"kind\": \"scenario.ai.action.show_hudmsg_middle\", \"source\": \"ai/ailists.json\", \"target\": \"hud.message.middle\", \"opcode\": \"0x01a4\" },\n"
        "    { \"id\": \"scenario.ai.action.hovercar_begin_path\", \"kind\": \"scenario.ai.action.hovercar_begin_path\", \"source\": \"ai/ailists.json\", \"paths\": \"navigation/paths.json\", \"target\": \"vehicle.path\", \"opcode\": \"0x00d5\" },\n"
        "    { \"id\": \"scenario.ai.action.set_vehicle_speed\", \"kind\": \"scenario.ai.action.set_vehicle_speed\", \"source\": \"ai/ailists.json\", \"target\": \"vehicle.speed\", \"opcode\": \"0x00d6\" },\n"
        "    { \"id\": \"scenario.ai.action.set_rotor_speed\", \"kind\": \"scenario.ai.action.set_rotor_speed\", \"source\": \"ai/ailists.json\", \"target\": \"vehicle.rotor_speed\", \"opcode\": \"0x00d7\" },\n"
        "    { \"id\": \"scenario.ai.action.chr_explosions\", \"kind\": \"scenario.ai.action.chr_explosions\", \"source\": \"ai/ailists.json\", \"target\": \"player.explosions\", \"opcode\": \"0x00fb\" },\n"
        "    { \"id\": \"scenario.ai.action.set_tinted_glass_enabled\", \"kind\": \"scenario.ai.action.set_tinted_glass_enabled\", \"source\": \"ai/ailists.json\", \"target\": \"scene.tinted_glass\", \"opcode\": \"0x0157\" },\n"
        "    { \"id\": \"scenario.ai.action.hovercopter_fire_rocket\", \"kind\": \"scenario.ai.action.hovercopter_fire_rocket\", \"source\": \"ai/ailists.json\", \"target\": \"vehicle.rocket\", \"opcode\": \"0x0167\" },\n"
        "    { \"id\": \"scenario.ai.action.chr_adjust_motion_blur\", \"kind\": \"scenario.ai.action.chr_adjust_motion_blur\", \"source\": \"ai/ailists.json\", \"target\": \"chr.motion_blur\", \"opcode\": \"0x016d\" },\n"
        "    { \"id\": \"scenario.ai.action.punch_or_kick\", \"kind\": \"scenario.ai.action.punch_or_kick\", \"source\": \"ai/ailists.json\", \"target\": \"chr.melee\", \"opcode\": \"0x0182\" },\n"
        "    { \"id\": \"scenario.ai.action.set_target_to_eyespy_if_in_sight\", \"kind\": \"scenario.ai.action.set_target_to_eyespy_if_in_sight\", \"source\": \"ai/ailists.json\", \"target\": \"chr.target.eyespy\", \"opcode\": \"0x0187\" },\n"
        "    { \"id\": \"scenario.ai.action.mini_skedar_try_pounce\", \"kind\": \"scenario.ai.action.mini_skedar_try_pounce\", \"source\": \"ai/ailists.json\", \"target\": \"chr.pounce\", \"opcode\": \"0x018e\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_object_distance_to_pad_less_than\", \"kind\": \"scenario.ai.condition.if_object_distance_to_pad_less_than\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"pads\": \"pads.json\", \"target\": \"object.pad_distance\", \"opcode\": \"0x018f\" },\n"
        "    { \"id\": \"scenario.ai.action.avoid\", \"kind\": \"scenario.ai.action.avoid\", \"source\": \"ai/ailists.json\", \"target\": \"chr.avoidance\", \"opcode\": \"0x01c5\" },\n"
        "    { \"id\": \"scenario.ai.action.title_init_mode\", \"kind\": \"scenario.ai.action.title_init_mode\", \"source\": \"ai/ailists.json\", \"target\": \"title.mode\", \"opcode\": \"0x01c8\" },\n"
        "    { \"id\": \"scenario.ai.action.try_exit_title\", \"kind\": \"scenario.ai.action.try_exit_title\", \"source\": \"ai/ailists.json\", \"target\": \"title.exit\", \"opcode\": \"0x01c9\" },\n"
        "    { \"id\": \"scenario.ai.action.chr_emit_sparks\", \"kind\": \"scenario.ai.action.chr_emit_sparks\", \"source\": \"ai/ailists.json\", \"target\": \"chr.sparks\", \"opcode\": \"0x01d2\" },\n"
        "    { \"id\": \"scenario.ai.action.set_dr_caroll_images\", \"kind\": \"scenario.ai.action.set_dr_caroll_images\", \"source\": \"ai/ailists.json\", \"target\": \"chr.dr_caroll_images\", \"opcode\": \"0x01d3\" },\n"
        "    { \"id\": \"scenario.ai.action.say_quip\", \"kind\": \"scenario.ai.action.say_quip\", \"source\": \"ai/ailists.json\", \"target\": \"chr.quip\", \"opcode\": \"0x0130\" },\n"
        "    { \"id\": \"scenario.ai.action.say_ci_staff_quip\", \"kind\": \"scenario.ai.action.say_ci_staff_quip\", \"source\": \"ai/ailists.json\", \"target\": \"chr.ci_staff_quip\", \"opcode\": \"0x01a2\" },\n"
        "    { \"id\": \"scenario.ai.action.shuffle_ruins_pillars\", \"kind\": \"scenario.ai.action.shuffle_ruins_pillars\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"setup.tags\", \"opcode\": \"0x01b1\" },\n"
        "    { \"id\": \"scenario.ai.action.shuffle_pelagic_switches\", \"kind\": \"scenario.ai.action.shuffle_pelagic_switches\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"setup.tags\", \"opcode\": \"0x01b9\" },\n"
        "    { \"id\": \"scenario.ai.action.set_action\", \"kind\": \"scenario.ai.action.set_action\", \"source\": \"ai/ailists.json\", \"target\": \"chr.myaction\", \"opcode\": \"0x0132\" },\n"
        "    { \"id\": \"scenario.ai.action.set_team_orders\", \"kind\": \"scenario.ai.action.set_team_orders\", \"source\": \"ai/ailists.json\", \"target\": \"squadron.orders\", \"opcode\": \"0x0133\" },\n"
        "    { \"id\": \"scenario.ai.action.retreat\", \"kind\": \"scenario.ai.action.retreat\", \"source\": \"ai/ailists.json\", \"target\": \"chr.navigation\", \"opcode\": \"0x0136\" },\n"
        "    { \"id\": \"scenario.ai.action.find_cover\", \"kind\": \"scenario.ai.action.find_cover\", \"source\": \"ai/ailists.json\", \"covers\": \"navigation/covers.json\", \"target\": \"chr.cover\", \"opcode\": \"0x0121\" },\n"
        "    { \"id\": \"scenario.ai.action.find_cover_within_dist\", \"kind\": \"scenario.ai.action.find_cover_within_dist\", \"source\": \"ai/ailists.json\", \"covers\": \"navigation/covers.json\", \"target\": \"chr.cover\", \"opcode\": \"0x0122\" },\n"
        "    { \"id\": \"scenario.ai.action.find_cover_outside_dist\", \"kind\": \"scenario.ai.action.find_cover_outside_dist\", \"source\": \"ai/ailists.json\", \"covers\": \"navigation/covers.json\", \"target\": \"chr.cover\", \"opcode\": \"0x0123\" },\n"
        "    { \"id\": \"scenario.ai.action.go_to_cover\", \"kind\": \"scenario.ai.action.go_to_cover\", \"source\": \"ai/ailists.json\", \"covers\": \"navigation/covers.json\", \"target\": \"chr.navigation\", \"opcode\": \"0x0124\" },\n"
        "    { \"id\": \"scenario.ai.action.check_cover_out_of_sight\", \"kind\": \"scenario.ai.action.check_cover_out_of_sight\", \"source\": \"ai/ailists.json\", \"covers\": \"navigation/covers.json\", \"target\": \"chr.cover.visibility\", \"opcode\": \"0x0125\" },\n"
        "    { \"id\": \"scenario.ai.action.orbit_target\", \"kind\": \"scenario.ai.action.orbit_target\", \"source\": \"ai/ailists.json\", \"target\": \"chr.navigation\", \"opcode\": \"0x0139\" },\n"
        "    { \"id\": \"scenario.ai.action.set_chr_preset_to_unalerted_teammate\", \"kind\": \"scenario.ai.action.set_chr_preset_to_unalerted_teammate\", \"source\": \"ai/ailists.json\", \"target\": \"chr.chrpreset\", \"opcode\": \"0x013a\" },\n"
        "    { \"id\": \"scenario.ai.action.set_squadron\", \"kind\": \"scenario.ai.action.set_squadron\", \"source\": \"ai/ailists.json\", \"target\": \"chr.squadron\", \"opcode\": \"0x013b\" },\n"
        "    { \"id\": \"scenario.ai.action.face_cover\", \"kind\": \"scenario.ai.action.face_cover\", \"source\": \"ai/ailists.json\", \"covers\": \"navigation/covers.json\", \"target\": \"chr.cover\", \"opcode\": \"0x013c\" },\n"
        "    { \"id\": \"scenario.ai.action.danger_cover\", \"kind\": \"scenario.ai.action.danger_cover\", \"source\": \"ai/ailists.json\", \"covers\": \"navigation/covers.json\", \"target\": \"chr.cover\", \"opcode\": \"0x013e\" },\n"
        "    { \"id\": \"scenario.ai.action.release_cover\", \"kind\": \"scenario.ai.action.release_cover\", \"source\": \"ai/ailists.json\", \"covers\": \"navigation/covers.json\", \"target\": \"chr.cover\", \"opcode\": \"0x012f\" },\n"
        "    { \"id\": \"scenario.ai.action.rebuild_teams\", \"kind\": \"scenario.ai.action.rebuild_teams\", \"source\": \"ai/ailists.json\", \"target\": \"team.index\", \"opcode\": \"0x0145\" },\n"
        "    { \"id\": \"scenario.ai.action.rebuild_squadrons\", \"kind\": \"scenario.ai.action.rebuild_squadrons\", \"source\": \"ai/ailists.json\", \"target\": \"squadron.index\", \"opcode\": \"0x0146\" },\n"
        "    { \"id\": \"scenario.ai.action.chr_set_listening\", \"kind\": \"scenario.ai.action.chr_set_listening\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.listening\", \"opcode\": \"0x0148\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_chr_not_talking\", \"kind\": \"scenario.ai.condition.if_chr_not_talking\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.talk_state\", \"opcode\": \"0x01a7\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_orders\", \"kind\": \"scenario.ai.condition.if_orders\", \"source\": \"ai/ailists.json\", \"target\": \"chr.orders\", \"opcode\": \"0x0134\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_has_orders\", \"kind\": \"scenario.ai.condition.if_has_orders\", \"source\": \"ai/ailists.json\", \"target\": \"chr.orders\", \"opcode\": \"0x0135\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_chr_in_squadron_doing_action\", \"kind\": \"scenario.ai.condition.if_chr_in_squadron_doing_action\", \"source\": \"ai/ailists.json\", \"target\": \"squadron.myaction\", \"opcode\": \"0x0137\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_chr_listening\", \"kind\": \"scenario.ai.condition.if_chr_listening\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.listening\", \"opcode\": \"0x0149\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_not_listening\", \"kind\": \"scenario.ai.condition.if_not_listening\", \"source\": \"ai/ailists.json\", \"target\": \"chr.listening\", \"opcode\": \"0x014b\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_chr_injured_target\", \"kind\": \"scenario.ai.condition.if_chr_injured_target\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.injured_target_latch\", \"opcode\": \"0x0165\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_action\", \"kind\": \"scenario.ai.condition.if_action\", \"source\": \"ai/ailists.json\", \"target\": \"chr.myaction\", \"opcode\": \"0x0166\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_chr_ammo_quantity_less_than\", \"kind\": \"scenario.ai.condition.if_chr_ammo_quantity_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"player.ammo\", \"opcode\": \"0x00eb\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_chr_target\", \"kind\": \"scenario.ai.condition.if_chr_target\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.target\", \"opcode\": \"0x0108\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_compare_chr_presets_team\", \"kind\": \"scenario.ai.condition.if_compare_chr_presets_team\", \"source\": \"ai/ailists.json\", \"target\": \"chr.preset.team\", \"opcode\": \"0x010c\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_human\", \"kind\": \"scenario.ai.condition.if_human\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.race\", \"opcode\": \"0x011e\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_skedar\", \"kind\": \"scenario.ai.condition.if_skedar\", \"source\": \"ai/ailists.json\", \"target\": \"target_chr.race\", \"opcode\": \"0x011f\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_prop_preset_blocking_sight_to_target\", \"kind\": \"scenario.ai.condition.if_prop_preset_blocking_sight_to_target\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"chr.proppreset1.line_of_sight\", \"opcode\": \"0x0103\" },\n"
        "    { \"id\": \"scenario.ai.action.remove_object_at_prop_preset\", \"kind\": \"scenario.ai.action.remove_object_at_prop_preset\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"chr.proppreset1\", \"opcode\": \"0x0104\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_prop_preset_height_less_than\", \"kind\": \"scenario.ai.condition.if_prop_preset_height_less_than\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"chr.proppreset1.height\", \"opcode\": \"0x0105\" },\n"
        "    { \"id\": \"scenario.ai.action.set_target\", \"kind\": \"scenario.ai.action.set_target\", \"source\": \"ai/ailists.json\", \"target\": \"chr.target\", \"opcode\": \"0x0106\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_presets_target_is_not_my_target\", \"kind\": \"scenario.ai.condition.if_presets_target_is_not_my_target\", \"source\": \"ai/ailists.json\", \"target\": \"chr.preset.target\", \"opcode\": \"0x0107\" },\n"
        "    { \"id\": \"scenario.ai.action.set_chr_preset_to_chr_near_self\", \"kind\": \"scenario.ai.action.set_chr_preset_to_chr_near_self\", \"source\": \"ai/ailists.json\", \"target\": \"chr.chrpreset1\", \"opcode\": \"0x0109\" },\n"
        "    { \"id\": \"scenario.ai.action.set_chr_preset_to_chr_near_pad\", \"kind\": \"scenario.ai.action.set_chr_preset_to_chr_near_pad\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"target\": \"chr.chrpreset1\", \"opcode\": \"0x010a\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_dangerous_object_nearby\", \"kind\": \"scenario.ai.condition.if_dangerous_object_nearby\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"chr.danger\", \"opcode\": \"0x013d\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_heli_weapons_armed\", \"kind\": \"scenario.ai.condition.if_heli_weapons_armed\", \"source\": \"ai/ailists.json\", \"target\": \"vehicle.weapons\", \"opcode\": \"0x013f\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_hoverbot_next_step\", \"kind\": \"scenario.ai.condition.if_hoverbot_next_step\", \"source\": \"ai/ailists.json\", \"target\": \"vehicle.nextstep\", \"opcode\": \"0x0140\" },\n"
        "    { \"id\": \"scenario.ai.action.shuffle_investigation_terminals\", \"kind\": \"scenario.ai.action.shuffle_investigation_terminals\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"target\": \"setup.tags\", \"opcode\": \"0x0141\" },\n"
        "    { \"id\": \"scenario.ai.action.set_pad_preset_to_investigation_terminal\", \"kind\": \"scenario.ai.action.set_pad_preset_to_investigation_terminal\", \"source\": \"ai/ailists.json\", \"objects\": \"objects.json\", \"pads\": \"pads.json\", \"target\": \"chr.padpreset1\", \"opcode\": \"0x0142\" },\n"
        "    { \"id\": \"scenario.ai.action.heli_arm_weapons\", \"kind\": \"scenario.ai.action.heli_arm_weapons\", \"source\": \"ai/ailists.json\", \"target\": \"vehicle.weapons\", \"opcode\": \"0x0143\" },\n"
        "    { \"id\": \"scenario.ai.action.heli_unarm_weapons\", \"kind\": \"scenario.ai.action.heli_unarm_weapons\", \"source\": \"ai/ailists.json\", \"target\": \"vehicle.weapons\", \"opcode\": \"0x0144\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_safety2_less_than\", \"kind\": \"scenario.ai.condition.if_safety2_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"chr.safety.weapon_support\", \"opcode\": \"0x0120\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_player_using_cmp_or_ar34\", \"kind\": \"scenario.ai.condition.if_player_using_cmp_or_ar34\", \"source\": \"ai/ailists.json\", \"target\": \"player.weapon\", \"opcode\": \"0x0126\" },\n"
        "    { \"id\": \"scenario.ai.condition.detect_enemy_on_same_floor\", \"kind\": \"scenario.ai.condition.detect_enemy_on_same_floor\", \"source\": \"ai/ailists.json\", \"scene\": \"scene.glb\", \"target\": \"chr.target.scan_same_floor\", \"opcode\": \"0x0127\" },\n"
        "    { \"id\": \"scenario.ai.condition.detect_enemy\", \"kind\": \"scenario.ai.condition.detect_enemy\", \"source\": \"ai/ailists.json\", \"scene\": \"scene.glb\", \"target\": \"chr.target.scan\", \"opcode\": \"0x0128\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_safety_less_than\", \"kind\": \"scenario.ai.condition.if_safety_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"chr.safety.support\", \"opcode\": \"0x0129\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_target_moving_slowly\", \"kind\": \"scenario.ai.condition.if_target_moving_slowly\", \"source\": \"ai/ailists.json\", \"target\": \"chr.target.motion\", \"opcode\": \"0x012a\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_target_moving_closer\", \"kind\": \"scenario.ai.condition.if_target_moving_closer\", \"source\": \"ai/ailists.json\", \"target\": \"chr.target.motion\", \"opcode\": \"0x012b\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_target_moving_away\", \"kind\": \"scenario.ai.condition.if_target_moving_away\", \"source\": \"ai/ailists.json\", \"target\": \"chr.target.motion\", \"opcode\": \"0x012c\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_squadron_is_dead\", \"kind\": \"scenario.ai.condition.if_squadron_is_dead\", \"source\": \"ai/ailists.json\", \"target\": \"squadron.alive\", \"opcode\": \"0x0147\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_true\", \"kind\": \"scenario.ai.condition.if_true\", \"source\": \"ai/ailists.json\", \"target\": \"ai.branch\", \"opcode\": \"0x014a\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_num_chrs_in_squadron_greater_than\", \"kind\": \"scenario.ai.condition.if_num_chrs_in_squadron_greater_than\", \"source\": \"ai/ailists.json\", \"target\": \"squadron.count\", \"opcode\": \"0x0152\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_natural_anim\", \"kind\": \"scenario.ai.condition.if_natural_anim\", \"source\": \"ai/ailists.json\", \"target\": \"chr.naturalanim\", \"opcode\": \"0x0169\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_y\", \"kind\": \"scenario.ai.condition.if_y\", \"source\": \"ai/ailists.json\", \"target\": \"chr.position.y\", \"opcode\": \"0x016a\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_sound_timer\", \"kind\": \"scenario.ai.condition.if_sound_timer\", \"source\": \"ai/ailists.json\", \"target\": \"chr.soundtimer\", \"opcode\": \"0x0186\" },\n"
        "    { \"id\": \"scenario.ai.condition.if_target_y_difference_less_than\", \"kind\": \"scenario.ai.condition.if_target_y_difference_less_than\", \"source\": \"ai/ailists.json\", \"target\": \"chr.target.position.y\", \"opcode\": \"0x01a6\" },\n"
        "    { \"id\": \"scenario.ai.action.try_attack_amount\", \"kind\": \"scenario.ai.action.try_attack_amount\", \"source\": \"ai/ailists.json\", \"target\": \"chr.attack_state\", \"opcode\": \"0x0184\" },\n"
        "    { \"id\": \"scenario.ai.action.set_chr_preset\", \"kind\": \"scenario.ai.action.set_chr_preset\", \"source\": \"ai/ailists.json\", \"opcode\": \"0x00b0\" },\n"
        "    { \"id\": \"scenario.ai.action.set_chr_target\", \"kind\": \"scenario.ai.action.set_chr_target\", \"source\": \"ai/ailists.json\", \"opcode\": \"0x00b1\" },\n"
        "    { \"id\": \"scenario.ai.action.set_pad_preset\", \"kind\": \"scenario.ai.action.set_pad_preset\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"opcode\": \"0x00b2\" },\n"
        "    { \"id\": \"scenario.ai.action.chr_set_pad_preset\", \"kind\": \"scenario.ai.action.chr_set_pad_preset\", \"source\": \"ai/ailists.json\", \"pads\": \"pads.json\", \"opcode\": \"0x00b3\" },\n"
        "    { \"id\": \"scenario.ai.action.chr_copy_pad_preset\", \"kind\": \"scenario.ai.action.chr_copy_pad_preset\", \"source\": \"ai/ailists.json\", \"pad\": \"source_chr.padpreset1\", \"opcode\": \"0x00b4\" },\n"
        "    { \"id\": \"trigger.volume.0000\", \"kind\": \"scenario.trigger.volume.source\", \"table\": \"volumes.json\", \"volume\": \"volume_pad_0000\", \"pad\": \"pad_0000\" }\n"
        "  ],\n"
        "  \"links\": [\n"
        "    { \"from\": \"scenario.load\", \"to\": \"source.scene\" },\n"
        "    { \"from\": \"source.scene\", \"to\": \"scenario.portals\" },\n"
        "    { \"from\": \"scenario.load\", \"to\": \"scenario.pads\" },\n"
        "    { \"from\": \"source.scene\", \"to\": \"navigation.paths\" },\n"
        "    { \"from\": \"scenario.load\", \"to\": \"scenario.ai.lists\" },\n"
        "    { \"from\": \"scenario.load\", \"to\": \"scenario.global.settings\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_list\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_return_list\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_shot_list\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.return_list\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.stop\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.kneel\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.surrender\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.fade_out\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.remove_chr\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_sidestep\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_jump_out\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_run_sideways\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_attack_walk\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_attack_run\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_attack_roll\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_attack_stand\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_attack_kneel\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_attack_lie\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_attack_locked\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_attacking\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_modify_attack\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.face_entity\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.apply_gset_damage\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_damage_chr\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.consider_grenade_throw\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.drop_item\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_run_from_target\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_jog_to_target_prop\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_walk_to_target_prop\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_run_to_target_prop\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_go_to_cover_prop\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_jog_to_chr\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_walk_to_chr\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_run_to_chr\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_can_hear_alarm\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_patrolling\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_alarm_active\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_gas_active\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_hears_target\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_saw_injury\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_saw_death\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_los_to_target\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_los_to_attack_target\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_target_nearly_in_sight\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_nearly_in_targets_sight\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_pad_preset_to_pad_on_route_to_target\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_saw_target_recently\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_heard_target_recently\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_los_to_chr\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_never_been_on_screen\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_on_screen\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_in_on_screen_room\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_room_is_on_screen\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_target_aiming_at_me\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_near_miss\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_sees_suspicious_item\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_target_in_fov_left\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_check_fov_with_target\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_target_out_of_fov_left\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_target_in_fov\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_target_out_of_fov\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_distance_to_target_less_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_distance_to_target_greater_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_distance_to_pad_less_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_distance_to_pad_greater_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_distance_to_chr_less_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_distance_to_chr_greater_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_any_chr_near_self\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_distance_from_target_to_pad_less_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_distance_from_target_to_pad_greater_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_in_room\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_target_in_room\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_has_object\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_weapon_thrown\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_weapon_thrown_on_object\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_has_weapon_equipped\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_gun_unclaimed\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_object_healthy\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_activated_object\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.obj_interact\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.destroy_object\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.drop_object_from_chr\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_drop_items\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_drop_weapon\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.give_object_to_chr\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.object_move_to_pad\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_waypoint_within_quadrant\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_pad_preset_to_target_quadrant\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.condition.if_chr_distance_to_pad_less_than\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.condition.if_chr_distance_to_pad_greater_than\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.condition.if_distance_from_target_to_pad_less_than\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.condition.if_distance_from_target_to_pad_greater_than\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.condition.if_chr_in_room\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.condition.if_target_in_room\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.condition.if_chr_has_object\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.condition.if_weapon_thrown_on_object\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.condition.if_gun_unclaimed\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.condition.if_object_healthy\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.object_move_to_pad\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.condition.if_chr_activated_object\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.obj_interact\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.destroy_object\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.drop_object_from_chr\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.chr_drop_items\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.chr_drop_weapon\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.give_object_to_chr\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.object_move_to_pad\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.condition.if_waypoint_within_quadrant\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.set_pad_preset_to_target_quadrant\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_do_animation\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.be_surprised_one_hand\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.be_surprised_look_around\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.be_surprised_surrender\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.random\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_random_less_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_random_greater_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.print\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.noop\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_punch_dodge_list\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_shooting_at_me_list\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_dark_room_list\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_player_dead_list\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.jog_to_pad\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.go_to_pad_preset\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.walk_to_pad\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.run_to_pad\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_path\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.start_patrol\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_start_alarm\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.activate_alarm\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.deactivate_alarm\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_morale\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.add_morale\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_add_morale\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.subtract_morale\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_alertness\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.add_alertness\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_add_alertness\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.subtract_alertness\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_num_arghs_less_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_num_arghs_greater_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_num_close_arghs_less_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_num_close_arghs_greater_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_health_greater_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_health_less_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_shield_less_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_shield_greater_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_injured\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_shield_damaged\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_morale_less_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_morale_less_than_random\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_alertness\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_alertness_less_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_alertness_less_than_random\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_idle\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_stopped\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_dead\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_death_animation_finished\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_knocked_out\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_can_see_target\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.increase_squadron_alertness\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_hear_distance\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_view_distance\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_grenade_probability\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_chr_num\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_max_damage\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.add_health\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_shield\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_reaction_speed\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_recovery_speed\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_accuracy\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_dodge_rating\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_unarmed_dodge_rating\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_flag\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.unset_flag\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_has_flag\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_set_flag\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_unset_flag\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_chr_has_flag\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_stage_flag\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.unset_stage_flag\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_stage_flag_eq\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_chrflag\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.unset_chrflag\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_has_chrflag\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_set_chrflag\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_unset_chrflag\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_chr_has_chrflag\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_obj_flag\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.unset_obj_flag\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_obj_has_flag\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.open_door\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.close_door\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_door_state\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_object_is_door\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.lock_door\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.unlock_door\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_door_locked\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_lift_stationary\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.lift_go_to_stop\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_lift_at_stop\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.activate_lift\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_using_lift\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.configure_rain\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.configure_snow\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.switch_to_alt_sky\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_wind_speed\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_lights\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_room_flag\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.show_cutscene_chrs\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.configure_environment\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_distance_to_target2_less_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_distance_to_target2_greater_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.speak\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.play_sound\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.assign_sound\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.audio_mute_channel\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_channel_free\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_object_sound_volume\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_object_sound_volume_by_distance\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_object_sound_playing\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.play_repeating_sound_from_object\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.play_sound_from_entity\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.play_repeating_sound_from_pad\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_object_sound_volume_less_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.play_sound_from_prop\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.play_temporary_primary_track\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.play_x_track\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.stop_x_track\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.play_track_isolated\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.play_default_tracks\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.play_cutscene_track\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.stop_cutscene_track\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.play_temporary_track\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.stop_ambient_track\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_draw_weapon\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_draw_weapon_in_cutscene\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_player_force_speed\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_set_invincible\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_player_is_invincible\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_has_no_gun\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_delete_weapon\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_trigger_shot_list\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.end_level\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.end_cutscene\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.warp_jo_to_pad\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.warp_jo_to_pad\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.warp_jo_to_tag\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.warp_jo_to_tag\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.revoke_control\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.grant_control\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.player_fade_in\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.players_fade_out\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_colour_fade_complete\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.prepare_warp_orbit\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.prepare_warp_orbit\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.begin_warp_latch\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_warp_latch_complete\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_camera_animation\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_in_cutscene\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_cutscene_button_pressed\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.reorient_for_cutscene_stop\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.spawn_chr_at_pad\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.spawn_chr_at_pad\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.spawn_chr_at_chr\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_equip_weapon\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_equip_hat\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_obj_image\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.set_obj_image\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.object_do_animation\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.object_do_animation\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_door_open\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.set_door_open\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.duplicate_chr\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.enable_chr\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.disable_chr\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.enable_obj\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.enable_obj\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.disable_obj\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.disable_obj\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_move_to_pad\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.chr_move_to_pad\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_set_team\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.damage_chr_by_amount\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.do_preset_animation\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_player_chr_portal_distance_less_than\" },\n"
        "    { \"from\": \"source.scene\", \"to\": \"scenario.ai.condition.if_player_chr_portal_distance_less_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_reposition_valid\" },\n"
        "    { \"from\": \"source.scene\", \"to\": \"scenario.ai.condition.if_chr_reposition_valid\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.do_gun_command\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_distance_to_gun_less_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.recover_gun\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.do_gun_command\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.condition.if_distance_to_gun_less_than\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.recover_gun\" },\n"
        "    { \"from\": \"source.scene\", \"to\": \"scenario.ai.action.do_gun_command\" },\n"
        "    { \"from\": \"source.scene\", \"to\": \"scenario.ai.condition.if_distance_to_gun_less_than\" },\n"
        "    { \"from\": \"source.scene\", \"to\": \"scenario.ai.action.recover_gun\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_copy_properties\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.player_auto_walk\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.player_auto_walk\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_player_auto_walk_finished\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_obj_in_room\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.condition.if_obj_in_room\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.condition.if_obj_in_room\" },\n"
        "    { \"from\": \"source.scene\", \"to\": \"scenario.ai.condition.if_obj_in_room\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_player_looking_at_object\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.condition.if_player_looking_at_object\" },\n"
        "    { \"from\": \"source.scene\", \"to\": \"scenario.ai.condition.if_player_looking_at_object\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_target_is_player\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_kill\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.remove_weapon_from_inventory\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.clear_inventory\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.release_object\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_grab_object\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.chr_grab_object\" },\n"
        "    { \"from\": \"source.scene\", \"to\": \"scenario.ai.action.chr_grab_object\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.toggle_p1p2\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_set_p1p2\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_set_cloaked\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_autogun_target_team\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.set_autogun_target_team\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_objective_complete\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_objective_failed\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_all_objectives_complete\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_difficulty_less_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_difficulty_greater_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_stage_timer_less_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_stage_timer_greater_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_stage_id_less_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_stage_id_greater_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_num_players_less_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_kill_count_greater_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_num_knocked_out_chrs\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.kill_bond\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_pouncebits_eq\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_training_pc_holographed\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_player_using_device\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_begin_or_end_teleport\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.chr_begin_or_end_teleport\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_teleport_full_white\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_set_cutscene_weapon\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.fade_screen\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_fade_complete\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_chr_hudpiece_visible\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_passive_mode\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_set_firing_in_cutscene\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_portal_flag\" },\n"
        "    { \"from\": \"source.scene\", \"to\": \"scenario.ai.action.set_portal_flag\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_music_event_queue_is_empty\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_coop_mode\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_same_floor_distance_to_pad_less_than\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.condition.if_chr_same_floor_distance_to_pad_less_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.remove_references_to_chr\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_toggle_model_part\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.obj_set_model_part_visible\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_obj_health_less_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_obj_health\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_chr_special_death_animation\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_room_to_search\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_set_hidden_flag\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_unset_hidden_flag\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_chr_has_hidden_flag\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_savefile_flag\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.unset_savefile_flag\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_savefile_flag_set\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_savefile_flag_unset\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.restart_timer\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.reset_timer\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.pause_timer\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.resume_timer\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_timer_stopped\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_timer_greater_than_random\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_timer_less_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_timer_greater_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.show_countdown_timer\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.hide_countdown_timer\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_countdown_timer\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.stop_countdown_timer\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.start_countdown_timer\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_countdown_timer_stopped\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_countdown_timer_less_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.if_countdown_timer_greater_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.show_hudmsg\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.show_hudmsg_middle\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.show_hudmsg_top_middle\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.hovercar_begin_path\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_vehicle_speed\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_rotor_speed\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_explosions\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_tinted_glass_enabled\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.hovercopter_fire_rocket\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_adjust_motion_blur\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.punch_or_kick\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_target_to_eyespy_if_in_sight\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.mini_skedar_try_pounce\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_object_distance_to_pad_less_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.avoid\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.title_init_mode\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_exit_title\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_emit_sparks\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_dr_caroll_images\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.say_quip\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.say_ci_staff_quip\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.shuffle_ruins_pillars\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.shuffle_pelagic_switches\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.shuffle_ruins_pillars\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.shuffle_pelagic_switches\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_action\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_team_orders\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.retreat\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.find_cover\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.find_cover_within_dist\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.find_cover_outside_dist\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.go_to_cover\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.check_cover_out_of_sight\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.orbit_target\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_chr_preset_to_unalerted_teammate\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_squadron\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.face_cover\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.danger_cover\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.release_cover\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.rebuild_teams\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.rebuild_squadrons\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_set_listening\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_not_talking\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_orders\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_has_orders\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_in_squadron_doing_action\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_listening\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_not_listening\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_injured_target\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_action\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_ammo_quantity_less_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_chr_target\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_compare_chr_presets_team\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_human\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_skedar\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_prop_preset_blocking_sight_to_target\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.remove_object_at_prop_preset\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_prop_preset_height_less_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_target\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_presets_target_is_not_my_target\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_chr_preset_to_chr_near_self\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_chr_preset_to_chr_near_pad\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_dangerous_object_nearby\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_heli_weapons_armed\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_hoverbot_next_step\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.shuffle_investigation_terminals\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_pad_preset_to_investigation_terminal\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.heli_arm_weapons\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.heli_unarm_weapons\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_safety2_less_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_player_using_cmp_or_ar34\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.detect_enemy_on_same_floor\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.detect_enemy\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_safety_less_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_target_moving_slowly\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_target_moving_closer\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_target_moving_away\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_squadron_is_dead\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_true\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_num_chrs_in_squadron_greater_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_natural_anim\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_y\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_sound_timer\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.condition.if_target_y_difference_less_than\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.condition.if_prop_preset_blocking_sight_to_target\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.remove_object_at_prop_preset\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.condition.if_prop_preset_height_less_than\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.set_chr_preset_to_chr_near_pad\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.condition.if_dangerous_object_nearby\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.shuffle_investigation_terminals\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.set_pad_preset_to_investigation_terminal\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.set_pad_preset_to_investigation_terminal\" },\n"
        "    { \"from\": \"source.scene\", \"to\": \"scenario.ai.condition.detect_enemy_on_same_floor\" },\n"
        "    { \"from\": \"source.scene\", \"to\": \"scenario.ai.condition.detect_enemy\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.condition.if_object_distance_to_pad_less_than\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.condition.if_object_distance_to_pad_less_than\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.try_attack_amount\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_chr_preset\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_chr_target\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_pad_preset\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_set_pad_preset\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.chr_copy_pad_preset\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.jog_to_pad\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.go_to_pad_preset\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.walk_to_pad\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.run_to_pad\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.set_pad_preset_to_pad_on_route_to_target\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.set_pad_preset\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.chr_set_pad_preset\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.try_start_alarm\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.set_lights\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.if_lift_stationary\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.lift_go_to_stop\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.if_lift_at_stop\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.activate_lift\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.if_using_lift\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.condition.if_room_is_on_screen\" },\n"
        "    { \"from\": \"scenario.global.settings\", \"to\": \"scenario.ai.action.configure_rain\" },\n"
        "    { \"from\": \"scenario.global.settings\", \"to\": \"scenario.ai.action.configure_snow\" },\n"
        "    { \"from\": \"scenario.global.settings\", \"to\": \"scenario.ai.action.configure_environment\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.chr_toggle_model_part\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.obj_set_model_part_visible\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.if_obj_health_less_than\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.set_obj_health\" },\n"
        "    { \"from\": \"source.scene\", \"to\": \"scenario.ai.action.set_room_to_search\" },\n"
        "    { \"from\": \"source.scene\", \"to\": \"scenario.ai.action.set_room_flag\" },\n"
        "    { \"from\": \"source.scene\", \"to\": \"scenario.ai.action.configure_environment\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.set_obj_flag\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.unset_obj_flag\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.if_obj_has_flag\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.open_door\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.close_door\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.if_door_state\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.if_object_is_door\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.lock_door\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.unlock_door\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.if_door_locked\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.if_lift_stationary\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.lift_go_to_stop\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.if_lift_at_stop\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.activate_lift\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.action.if_using_lift\" },\n"
        "    { \"from\": \"setup.tables\", \"to\": \"scenario.ai.condition.if_sees_suspicious_item\" },\n"
        "    { \"from\": \"navigation.paths\", \"to\": \"scenario.ai.action.set_path\" },\n"
        "    { \"from\": \"navigation.paths\", \"to\": \"scenario.ai.action.start_patrol\" },\n"
        "    { \"from\": \"navigation.paths\", \"to\": \"scenario.ai.action.set_pad_preset_to_pad_on_route_to_target\" },\n"
        "    { \"from\": \"navigation.paths\", \"to\": \"scenario.ai.action.hovercar_begin_path\" },\n"
        "    { \"from\": \"source.scene\", \"to\": \"scenario.ai.action.find_cover\" },\n"
        "    { \"from\": \"source.scene\", \"to\": \"scenario.ai.action.find_cover_within_dist\" },\n"
        "    { \"from\": \"source.scene\", \"to\": \"scenario.ai.action.find_cover_outside_dist\" },\n"
        "    { \"from\": \"source.scene\", \"to\": \"scenario.ai.action.go_to_cover\" },\n"
        "    { \"from\": \"source.scene\", \"to\": \"scenario.ai.action.check_cover_out_of_sight\" },\n"
        "    { \"from\": \"source.scene\", \"to\": \"scenario.ai.action.face_cover\" },\n"
        "    { \"from\": \"source.scene\", \"to\": \"scenario.ai.action.danger_cover\" },\n"
        "    { \"from\": \"source.scene\", \"to\": \"scenario.ai.action.release_cover\" },\n"
        "    { \"from\": \"navigation.generate\", \"to\": \"scenario.ai.condition.if_waypoint_within_quadrant\" },\n"
        "    { \"from\": \"navigation.generate\", \"to\": \"scenario.ai.action.set_pad_preset_to_target_quadrant\" },\n"
        "    { \"from\": \"scenario.load\", \"to\": \"trigger.volume.0000\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.ailist_0000.command.0000\" }\n"
        "  ],\n"
        "  \"counts\": { \"rooms\": 2, \"triangles\": 1, \"portals\": 0, \"pads\": 1, \"volumes\": 1, \"objects\": 1, \"objectives\": 1, \"ai_lists\": 1, \"ai_commands\": 1, \"waypoints\": 0, \"waygroups\": 0, \"covers\": 0, \"paths\": 1 }\n"
        "}\n"
    )
    scenario_ini = (
        "; tri_scenario.pdscenario - source-first scenario asset\n"
        "[scenario]\n"
        "catalog_id = example:tri_scenario\n"
        "name = Triangle Scenario\n"
        "mode = mp|solo\n"
        "source_room_count = 2\n"
        "scene_file = scene.glb\n"
        "scene_format = GLB\n"
        "runtime_source_file = scene.glb\n"
        "collision_source = collision.obj\n"
        "collision_fallback = override\n"
        "pads_file = pads.json\n"
        "spawns_file = spawns.json\n"
        "volumes_file = volumes.json\n"
        "waypoints_file = navigation/waypoints.json\n"
        "waygroups_file = navigation/waygroups.json\n"
        "covers_file = navigation/covers.json\n"
        "paths_file = navigation/paths.json\n"
        "portals_file = portals.json\n"
        "objects_file = objects.json\n"
        "setup_fields_file = setup.fields.json\n"
        "ai_lists_file = ai/ailists.json\n"
        "objectives_file = objectives.json\n"
        "navigation_file = navigation.ini\n"
        "level_graph_file = level.graph.json\n"
        "\n[meta]\n"
        "manifest = _meta/manifest.json\n"
    )
    scenario_manifest = (
        "{\n"
        "  \"pd_kind\": \"scenario\",\n"
        "  \"pd_schema_version\": 1,\n"
        "  \"id\": \"example:tri_scenario\",\n"
        "  \"room_count\": 2,\n"
        "  \"scene\": \"scene.glb\",\n"
        "  \"scene_format\": \"GLB\",\n"
        "  \"runtime_source\": \"scene.glb\",\n"
        "  \"collision_source\": \"collision.obj\",\n"
        "  \"collision_fallback\": \"override\",\n"
        "  \"portals\": \"portals.json\",\n"
        "  \"portal_count\": 0,\n"
        "  \"navigation\": \"navigation.ini\",\n"
        "  \"level_graph\": \"level.graph.json\",\n"
        "  \"pads\": \"pads.json\",\n"
        "  \"spawns\": \"spawns.json\",\n"
        "  \"volumes\": \"volumes.json\",\n"
        "  \"waypoints\": \"navigation/waypoints.json\",\n"
        "  \"waygroups\": \"navigation/waygroups.json\",\n"
        "  \"covers\": \"navigation/covers.json\",\n"
        "  \"paths\": \"navigation/paths.json\",\n"
        "  \"objects\": \"objects.json\",\n"
        "  \"setup_fields\": \"setup.fields.json\",\n"
        "  \"ai_lists\": \"ai/ailists.json\",\n"
        "  \"objectives\": \"objectives.json\"\n"
        "}\n"
    )
    write_archive(rel, [
        ("scenario.ini", scenario_ini),
        ("scene.glb", kept["scene.glb"]),
        ("collision.obj", collision_obj),
        ("portals.json", portals_json),
        ("pads.json", kept["pads.json"]),
        ("spawns.json", kept["spawns.json"]),
        ("volumes.json", kept["volumes.json"]),
        ("navigation/waypoints.json", waypoints_json),
        ("navigation/waygroups.json", waygroups_json),
        ("navigation/covers.json", covers_json),
        ("navigation/paths.json", paths_json),
        ("objects.json", kept["objects.json"]),
        ("setup.fields.json", setup_fields_json),
        ("ai/ailists.json", ai_lists_json),
        ("objectives.json", objectives_json),
        ("navigation.ini", navigation_ini),
        ("level.graph.json", level_graph_json),
        ("_meta/generated-collision.json", kept["_meta/generated-collision.json"]),
        ("_meta/generated-navmesh.json", navmesh_meta_json),
        ("_meta/manifest.json", scenario_manifest),
    ])
    return read_archive(rel)


def make_tri_mesh_archive(model_scale: float | None = None) -> bytes:
    rel = "meshes/tri_mesh.pdmesh"
    gltf = json.loads(read_entry(rel, "model.gltf").decode("utf-8"))
    gltf.pop("images", None)
    gltf.pop("textures", None)
    gltf["materials"] = [{
        "name": "TriangleMaterial",
        "pbrMetallicRoughness": {
            "baseColorFactor": [0.2, 0.8, 1.0, 1.0],
            "metallicFactor": 0.0,
            "roughnessFactor": 0.8,
        },
    }]
    for mesh in gltf.get("meshes", []):
        for primitive in mesh.get("primitives", []):
            primitive["material"] = 0

    model_mtl = (
        "# tri_mesh.pdmesh material sidecar\n"
        "newmtl TriangleMaterial\n"
        "Kd 0.2 0.8 1.0\n"
        "d 1.0\n"
    )
    model_nodes = {
        "schema": "pd2.mesh.nodes.v1",
        "nodes": [
            {
                "id": 0,
                "parent": -1,
                "type": 2,
                "partnum": 0,
                "part": 0,
                "mtx0": 0,
                "mtx1": -1,
                "mtx2": -1,
                "position": {"x": 0.0, "y": 0.0, "z": 0.0},
                "drawdist": 0.0,
                "target": -1,
                "group": "-",
                "render_mtx": 0,
                "mcount": 1,
                "hitpart": 0,
                "bounds": {
                    "xmin": 0.0,
                    "xmax": 1.0,
                    "ymin": 0.0,
                    "ymax": 1.0,
                    "zmin": 0.0,
                    "zmax": 0.0,
                },
                "distance": {"near": 0.0, "far": 0.0},
                "reorder": {
                    "pivot": {"x": 0.0, "y": 0.0, "z": 0.0},
                    "axis": {"x": 0.0, "y": 1.0, "z": 0.0},
                    "target_a": -1,
                    "target_b": -1,
                    "side": 0,
                },
            },
            {
                "id": 1,
                "parent": 0,
                "type": 24,
                "partnum": -1,
                "part": -1,
                "mtx0": 0,
                "mtx1": -1,
                "mtx2": -1,
                "position": {"x": 0.0, "y": 0.0, "z": 0.0},
                "drawdist": 0.0,
                "target": -1,
                "group": "-",
                "render_mtx": 0,
                "mcount": 1,
                "hitpart": 0,
                "bounds": {
                    "xmin": 0.0,
                    "xmax": 1.0,
                    "ymin": 0.0,
                    "ymax": 1.0,
                    "zmin": 0.0,
                    "zmax": 0.0,
                },
                "distance": {"near": 0.0, "far": 0.0},
                "reorder": {
                    "pivot": {"x": 0.0, "y": 0.0, "z": 0.0},
                    "axis": {"x": 0.0, "y": 1.0, "z": 0.0},
                    "target_a": -1,
                    "target_b": -1,
                    "side": 0,
                },
            },
        ],
    }
    model_parts = {
        "schema": "pd2.mesh.parts.v1",
        "parts": [
            {"partnum": 0, "node": 0},
        ],
    }
    model_faces = {
        "schema": "pd2.mesh.faces.v1",
        "faces": [
            {"face_index": 0, "matrix_index": 0},
        ],
    }
    model_render = {
        "schema": "pd2.mesh.render.v1",
        "pd_kind": "mesh_render_commands",
        "pd_schema_version": 1,
        "commands": [
            {
                "group": "-",
                "command": "tri",
                "face_index": 0,
                "matrix_index": 0,
            },
        ],
    }

    mesh_ini = (
        "; tri_mesh.pdmesh - self-contained editable mesh asset\n"
        "[mesh]\n"
        "catalog_id = example:tri_mesh\n"
        "model_file = model.gltf\n"
        "material_file = model.mtl\n"
        "hierarchy_file = model.nodes.json\n"
        "parts_file = model.parts.json\n"
        "faces_file = model.faces.json\n"
        "render_stream_file = model.render.json\n"
    )
    manifest_fields: dict[str, object] = {
        "geometry": "model.gltf",
        "model_file": "model.gltf",
        "material_file": "model.mtl",
        "hierarchy_file": "model.nodes.json",
        "parts_file": "model.parts.json",
        "faces_file": "model.faces.json",
        "render_stream_file": "model.render.json",
    }
    if model_scale is not None:
        mesh_ini += f"model_scale = {model_scale:.9g}\n"
        manifest_fields["model_scale"] = model_scale
    mesh_ini += (
        "\n[meta]\n"
        "manifest = _meta/manifest.json\n"
    )

    return archive_bytes([
        ("mesh.ini",
         mesh_ini),
        ("model.gltf", json.dumps(gltf, indent=2) + "\n"),
        ("model.mtl", model_mtl),
        ("model.nodes.json", json.dumps(model_nodes, indent=2) + "\n"),
        ("model.parts.json", json.dumps(model_parts, indent=2) + "\n"),
        ("model.faces.json", json.dumps(model_faces, indent=2) + "\n"),
        ("model.render.json", json.dumps(model_render, indent=2) + "\n"),
        ("_meta/manifest.json", manifest_with("mesh", "example:tri_mesh",
                                               manifest_fields)),
    ])


def update_mesh() -> bytes:
    rel = "meshes/tri_mesh.pdmesh"
    archive(rel).write_bytes(make_tri_mesh_archive())
    return read_archive(rel)


def update_texture() -> bytes:
    rel = "textures/tri_texture.pdtexture"
    texture = read_entry(rel, "texture.png")
    write_archive(rel, [
        ("texture.ini",
         "; tri_texture.pdtexture - self-contained texture asset\n"
         "[texture]\n"
         "catalog_id = example:tri_texture\n"
         "name = Triangle Texture\n"
         "\n[source]\n"
         "texture_file = texture.png\n"
         "\n[meta]\n"
         "manifest = _meta/manifest.json\n"),
        ("texture.png", texture),
        ("_meta/manifest.json", manifest_with("texture", "example:tri_texture", {
            "texture_file": "texture.png",
        })),
    ])
    return read_archive(rel)


def update_head_and_body(mesh_bytes: bytes) -> tuple[bytes, bytes]:
    body_mesh_bytes = make_tri_mesh_archive(1000.0)
    write_archive("heads/tri_head.pdhead", [
        ("head.ini",
         "; tri_head.pdhead - focused head asset with typed mesh dependency\n"
         "[head]\n"
         "catalog_id = example:tri_head\n"
         "rig_class = human_male_neck_standard\n"
         "mesh_archive = mesh.pdmesh\n"
         "\n[meta]\n"
         "manifest = _meta/manifest.json\n"),
        ("mesh.pdmesh", body_mesh_bytes),
        ("_meta/manifest.json", head_manifest("example:tri_head")),
    ])
    write_archive("bodies/tri_body.pdbody", [
        ("body.ini",
         "; tri_body.pdbody - focused body asset with typed mesh dependencies\n"
         "[body]\n"
         "catalog_id = example:tri_body\n"
         "display_name = Triangle Body\n"
         "rig_class = human_male_neck_standard\n"
         "mesh_archive = mesh.pdmesh\n"
         "hand_archive = hand.pdmesh\n"
         "\n[meta]\n"
         "manifest = _meta/manifest.json\n"),
        ("mesh.pdmesh", body_mesh_bytes),
        ("hand.pdmesh", body_mesh_bytes),
        ("_meta/manifest.json", body_manifest("example:tri_body")),
    ])
    return read_archive("heads/tri_head.pdhead"), read_archive("bodies/tri_body.pdbody")


def update_character(head_bytes: bytes, body_bytes: bytes) -> None:
    portrait = read_entry("characters/tri_character.pdcharacter", "portrait.png")
    write_archive("characters/tri_character.pdcharacter", [
        ("character.ini",
         "; tri_character.pdcharacter - assembler asset with typed body/head dependencies\n"
         "[character]\n"
         "catalog_id = example:tri_character\n"
         "display_name = Triangle Character\n"
         "body_asset = example:tri_body\n"
         "head_asset = example:tri_head\n"
         "body_archive = dependencies/assets/body/tri_body.pdbody\n"
         "head_archive = dependencies/assets/head/tri_head.pdhead\n"
         "portrait_file = portrait.png\n"
         "\n[meta]\n"
         "manifest = _meta/manifest.json\n"),
        ("portrait.png", portrait),
        ("dependencies/assets/body/tri_body.pdbody", body_bytes),
        ("dependencies/assets/head/tri_head.pdhead", head_bytes),
        ("_meta/manifest.json", character_manifest("example:tri_character")),
    ])


def update_arena(scenario_bytes: bytes) -> None:
    write_archive("arenas/tri_arena.pdarena", [
        ("arena.ini",
         "; tri_arena.pdarena - multiplayer wrapper around a typed scenario dependency\n"
         "[arena]\n"
         "catalog_id = example:tri_arena\n"
         "load_mode = ARENA_LOADMODE_PLAYABLE\n"
         "scenario = example:tri_scenario\n"
         "scenario_archive = dependencies/assets/scenarios/tri_scenario.pdscenario\n"
         f"scenario_graph_cache = {ARENA_SCENARIO_GRAPH_CACHE_KIND}\n"
         "\n[meta]\n"
         "manifest = _meta/manifest.json\n"),
        ("dependencies/assets/scenarios/tri_scenario.pdscenario", scenario_bytes),
        ("_meta/manifest.json", arena_manifest("example:tri_arena")),
    ])


def update_projectile_entity() -> None:
    projectile_graph = (
        "{\n"
        "  \"schema\": \"pd.projectile_graph.v1\",\n"
        "  \"asset_id\": \"example:tri_projectile\",\n"
        "  \"graph_id\": \"projectile\",\n"
        "  \"nodes\": [\n"
        "    { \"id\": \"motion\", \"kind\": \"projectile.motion\", \"params\": { \"motion_kind\": \"authored\", \"speed\": 18.0, \"timer60\": 120 } },\n"
        "    { \"id\": \"transition\", \"kind\": \"projectile.transition_to_entity\", \"params\": { \"entity_ref\": \"example:tri_entity\", \"when\": \"at_rest\", \"transfer_owner\": true, \"transfer_position\": true } }\n"
        "  ],\n"
        "  \"edges\": [],\n"
        "  \"exports\": [ { \"name\": \"main\", \"node\": \"motion\" } ]\n"
        "}\n"
    )
    write_archive("projectiles/tri_projectile.pdprojectile", [
        ("projectile.ini",
         "; tri_projectile.pdprojectile - self-contained projectile asset\n"
         "[projectile]\n"
         "catalog_id = example:tri_projectile\n"
         "name = Triangle Projectile\n"
         "speed = 18.0\n"
         "behavior_graph = behavior.graph.json\n"
         "\n[meta]\n"
         "manifest = _meta/manifest.json\n"),
        ("behavior.graph.json", projectile_graph),
        ("_meta/manifest.json", manifest_with("projectile", "example:tri_projectile", {
            "behavior_graph": "behavior.graph.json",
        })),
    ])

    entity_bindings = read_entry("entities/tri_entity.pdentity", "bindings.json")
    entity_graph = (
        "{\n"
        "  \"schema\": \"pd.entity_graph.v1\",\n"
        "  \"asset_id\": \"example:tri_entity\",\n"
        "  \"graph_id\": \"entity\",\n"
        "  \"nodes\": [\n"
        "    { \"id\": \"armed\", \"kind\": \"entity.armed_explosive\", \"params\": { \"archetype\": \"deployed_triangle\", \"activation_time60\": 30, \"recovery_time60\": 15, \"delete_on_detonate\": true } },\n"
        "    { \"id\": \"cleanup\", \"kind\": \"entity.owner_cleanup\", \"params\": { \"max_active_per_owner\": 1 } }\n"
        "  ],\n"
        "  \"edges\": [],\n"
        "  \"exports\": [ { \"name\": \"main\", \"node\": \"armed\" } ]\n"
        "}\n"
    )
    entity_composition = read_entry("entities/tri_entity.pdentity", "composition.json")
    write_archive("entities/tri_entity.pdentity", [
        ("entity.ini",
         "; tri_entity.pdentity - self-contained entity asset\n"
         "[entity]\n"
         "catalog_id = example:tri_entity\n"
         "name = Triangle Entity\n"
         "bindings_file = bindings.json\n"
         "behavior_graph = behavior.graph.json\n"
         "composition_file = composition.json\n"
         "\n[meta]\n"
         "manifest = _meta/manifest.json\n"),
        ("bindings.json", entity_bindings),
        ("behavior.graph.json", entity_graph),
        ("composition.json", entity_composition),
        ("_meta/manifest.json", manifest_with("entity", "example:tri_entity", {
            "bindings_file": "bindings.json",
            "behavior_graph": "behavior.graph.json",
            "composition_file": "composition.json",
        })),
    ])


def update_weapon(mesh_bytes: bytes) -> None:
    projectile = read_archive("projectiles/tri_projectile.pdprojectile")
    entity = read_archive("entities/tri_entity.pdentity")
    reticle = read_archive("ui/tri_reticle.pdui")
    primary_graph = (
        "{\n"
        "  \"schema\": \"pd.weapon_graph.v1\",\n"
        "  \"asset_id\": \"example:tri_weapon\",\n"
        "  \"graph_id\": \"primary\",\n"
        "  \"nodes\": [\n"
        "    { \"id\": \"trigger_primary\", \"kind\": \"event.trigger_pressed\", \"params\": { \"mode\": \"primary\" } },\n"
        "    { \"id\": \"spawn_projectile\", \"kind\": \"spawn.fired_projectile\", \"params\": { \"mode\": \"primary\", \"function_type\": \"shoot_projectile\", \"projectile_ref\": \"example:tri_projectile\" } }\n"
        "  ],\n"
        "  \"edges\": [ { \"from\": \"trigger_primary\", \"to\": \"spawn_projectile\" } ],\n"
        "  \"exports\": [ { \"name\": \"primary\", \"node\": \"spawn_projectile\" } ]\n"
        "}\n"
    )
    secondary_graph = (
        "{\n"
        "  \"schema\": \"pd.weapon_graph.v1\",\n"
        "  \"asset_id\": \"example:tri_weapon\",\n"
        "  \"graph_id\": \"secondary\",\n"
        "  \"nodes\": [\n"
        "    { \"id\": \"trigger_secondary\", \"kind\": \"event.trigger_pressed\", \"params\": { \"mode\": \"secondary\" } },\n"
        "    { \"id\": \"deploy_entity\", \"kind\": \"spawn.thrown_physical\", \"params\": { \"mode\": \"secondary\", \"function_type\": \"throw\", \"entity_ref\": \"example:tri_entity\", \"payload_ref\": \"example:tri_entity\" } }\n"
        "  ],\n"
        "  \"edges\": [ { \"from\": \"trigger_secondary\", \"to\": \"deploy_entity\" } ],\n"
        "  \"exports\": [ { \"name\": \"secondary\", \"node\": \"deploy_entity\" } ]\n"
        "}\n"
    )
    write_archive("weapons/tri_weapon.pdweapon", [
        ("weapon.ini",
         "; tri_weapon.pdweapon - clean weapon asset with typed dependency closure\n"
         "[weapon]\n"
         "catalog_id = example:tri_weapon\n"
         "name = Triangle Weapon\n"
         "dual_wieldable = false\n"
         "model_file = dependencies/assets/models/weapon.pdmesh\n"
         "primary_graph = behavior/primary.graph.json\n"
         "secondary_graph = behavior/secondary.graph.json\n"
         "settings_file = behavior/settings.json\n"
         "variables_file = behavior/variables.json\n"
         "shared_context_file = behavior/shared-context.json\n"
         "presentation_file = bindings/presentation.json\n"
         "reticle_archive = dependencies/assets/ui/reticle.pdui\n"
         "primary_projectile_archive = dependencies/assets/projectiles/primary.pdprojectile\n"
         "deployed_entity_archive = dependencies/assets/entities/deployed.pdentity\n"
         "\n[meta]\n"
         "manifest = _meta/manifest.json\n"),
        ("behavior/primary.graph.json", primary_graph),
        ("behavior/secondary.graph.json", secondary_graph),
        ("behavior/settings.json",
         "{\n  \"schema\": \"pd.weapon_settings.v1\",\n  \"fire_cadence\": { \"value\": 25, \"unit\": \"centiseconds\" }\n}\n"),
        ("behavior/variables.json",
         "{\n  \"schema\": \"pd.weapon_variables.v1\",\n  \"variables\": []\n}\n"),
        ("behavior/shared-context.json",
         "{\n  \"schema\": \"pd.weapon.shared_context.v1\",\n  \"contexts\": [\"owner_player\", \"owner_team\", \"weapon_instance\", \"damage_credit_player\"]\n}\n"),
        ("bindings/presentation.json",
         "{\n  \"schema\": \"pd.weapon.presentation.v1\",\n  \"crosshair\": \"example:tri_reticle\",\n  \"zoom_fov\": 45.0\n}\n"),
        ("dependencies/assets/models/weapon.pdmesh", mesh_bytes),
        ("dependencies/assets/ui/reticle.pdui", reticle),
        ("dependencies/assets/projectiles/primary.pdprojectile", projectile),
        ("dependencies/assets/entities/deployed.pdentity", entity),
        ("_meta/manifest.json", manifest_with("weapon", "example:tri_weapon", {
            "model_file": "dependencies/assets/models/weapon.pdmesh",
            "primary_graph": "behavior/primary.graph.json",
            "secondary_graph": "behavior/secondary.graph.json",
            "settings_file": "behavior/settings.json",
            "variables_file": "behavior/variables.json",
            "shared_context_file": "behavior/shared-context.json",
            "presentation_file": "bindings/presentation.json",
            "reticle_archive": "dependencies/assets/ui/reticle.pdui",
            "primary_projectile_archive": "dependencies/assets/projectiles/primary.pdprojectile",
            "deployed_entity_archive": "dependencies/assets/entities/deployed.pdentity",
        })),
    ])


def update_botprofile() -> None:
    profile = (
        "{\n"
        "  \"schema\": \"pd2.botprofile.v2\",\n"
        "  \"catalog_id\": \"example:tri_botprofile\",\n"
        "  \"type_key\": \"general\",\n"
        "  \"difficulty_key\": \"normal\",\n"
        "  \"target_body\": \"example:tri_body\",\n"
        "  \"requirefeature\": 0\n"
        "}\n"
    )
    write_archive("botprofiles/tri_botprofile.pdbotprofile", [
        ("botprofile.ini",
         "[bot_profile]\n"
         "catalog_id = example:tri_botprofile\n"
         "type_key = general\n"
         "difficulty_key = normal\n"
         "target_body = example:tri_body\n"
         "requirefeature = 0\n"
         "profile_file = profile.json\n"),
        ("profile.json", profile),
        ("_meta/manifest.json", manifest_with("botprofile", "example:tri_botprofile", {
            "type_key": "general",
            "difficulty_key": "normal",
            "body": -1,
            "name_langid": 0,
            "requirefeature": 0,
            "target_body": "example:tri_body",
            "profile_file": "profile.json",
        })),
    ])


def update_effect() -> None:
    graph = read_entry("effects/tri_effect.pdeffect", "effect.graph.json")
    timeline = (
        "{\n"
        "  \"schema\": \"pd2.effect.timeline.v1\",\n"
        "  \"tracks\": [\n"
        "    { \"time\": 0.0, \"property\": \"intensity\", \"value\": 0.0 },\n"
        "    { \"time\": 0.25, \"property\": \"intensity\", \"value\": 0.75 }\n"
        "  ]\n"
        "}\n"
    )
    write_archive("effects/tri_effect.pdeffect", [
        ("effect.ini",
         "[effect]\n"
         "catalog_id = example:tri_effect\n"
         "name = Triangle Glow\n"
         "effect_key = glow\n"
         "target_key = weapon\n"
         "effect_file = effect.graph.json\n"
         "timeline_file = timeline.json\n"
         "shader_id = classic_glow\n"
         "intensity = 0.75\n"),
        ("effect.graph.json", graph),
        ("timeline.json", timeline),
        ("_meta/manifest.json", effect_manifest("example:tri_effect")),
    ])


def update_material() -> None:
    texture = read_archive("textures/tri_texture.pdtexture")
    effect = read_archive("effects/tri_effect.pdeffect")
    material_json = (
        "{\n"
        "  \"schema\": \"pd2.material.v1\",\n"
        "  \"catalog_id\": \"example:tri_material\",\n"
        "  \"name\": \"Triangle Material\",\n"
        "  \"shading_model\": \"classic_lit\",\n"
        "  \"base_color\": [0.8, 0.9, 1.0, 1.0],\n"
        "  \"texture_slots\": [\n"
        "    { \"name\": \"base_color\", \"archive\": \"dependencies/assets/texture/tri_texture.pdtexture\" }\n"
        "  ],\n"
        "  \"effect\": \"dependencies/assets/effects/tri_effect.pdeffect\",\n"
        "  \"emissive\": false,\n"
        "  \"roughness\": 0.45,\n"
        "  \"metallic\": 0.25\n"
        "}\n"
    )
    write_archive("materials/tri_material.pdmaterial", [
        ("material.ini",
         "; tri_material.pdmaterial - self-contained material asset\n"
         "[material]\n"
         "catalog_id = example:tri_material\n"
         "name = Triangle Material\n"
         "shader = pd2_unlit\n"
         "material_file = material.json\n"
         "texture_archive = dependencies/assets/texture/tri_texture.pdtexture\n"
         "effect_archive = dependencies/assets/effects/tri_effect.pdeffect\n"
         "\n[meta]\n"
         "manifest = _meta/manifest.json\n"),
        ("material.json", material_json),
        ("dependencies/assets/texture/tri_texture.pdtexture", texture),
        ("dependencies/assets/effects/tri_effect.pdeffect", effect),
        ("_meta/manifest.json", material_manifest("example:tri_material")),
    ])


def update_theme() -> None:
    # Keep this example on the exact schema consumed by
    # pdgui_theme_loader.cpp. The former accent/chrome keys looked plausible
    # but were ignored by the production parser.
    theme_json = (
        "{\n"
        "  \"schema\": \"pd2.theme.v1\",\n"
        "  \"catalog_id\": \"example:tri_theme\",\n"
        "  \"name\": \"Triangle Theme\",\n"
        "  \"author\": \"Perfect Dark 2 example pack\",\n"
        "  \"version\": \"1\",\n"
        "  \"palette\": {\n"
        "    \"dialog_border1\": \"66ccffff\",\n"
        "    \"dialog_titlebg\": \"102030ff\",\n"
        "    \"dialog_border2\": \"99ddffff\",\n"
        "    \"dialog_titlefg\": \"ffffffff\",\n"
        "    \"dialog_bodybg\": \"081018e8\",\n"
        "    \"item_unfocused\": \"b8d8e8ff\",\n"
        "    \"item_disabled\": \"607080ff\",\n"
        "    \"item_focused_inner\": \"204860ff\",\n"
        "    \"checkbox_checked\": \"66ccffff\",\n"
        "    \"item_focused_outer\": \"99ddffff\",\n"
        "    \"listgroup_headerbg\": \"183040ff\",\n"
        "    \"listgroup_headerfg\": \"d8f4ffff\",\n"
        "    \"title_glow\": \"66ccffff\"\n"
        "  },\n"
        "  \"textures\": { \"dialog_background\": \"example:tri_reticle\" },\n"
        "  \"menuStyle\": \"example:tri_reticle\",\n"
        "  \"font\": \"example:tri_theme_font\",\n"
        "  \"sounds\": {\n"
        "    \"swipe\": \"example:tri_click\", \"open\": \"example:tri_click\",\n"
        "    \"focus\": \"example:tri_click\", \"select\": \"example:tri_click\",\n"
        "    \"error\": \"example:tri_click\", \"toggle_on\": \"example:tri_click\",\n"
        "    \"toggle_off\": \"example:tri_click\", \"subfocus\": \"example:tri_click\",\n"
        "    \"keyboard_focus\": \"example:tri_click\", \"cancel\": \"example:tri_click\",\n"
        "    \"success\": \"example:tri_click\"\n"
        "  },\n"
        "  \"menuMusic\": \"example:tri_song\",\n"
        "  \"scanline\": { \"enabled\": true, \"alpha\": 0.8, \"interval\": 2 },\n"
        "  \"textGlow\": { \"enabled\": true, \"intensity\": 0.6, \"color\": \"66ccffff\" },\n"
        "  \"nineslices\": [{ \"id\": \"example:tri_reticle\", \"left\": 2, \"right\": 2, \"top\": 2, \"bottom\": 2, \"edgeMode\": \"stretch\", \"centerMode\": \"tile\" }],\n"
        "  \"caustics\": [{ \"elementId\": \"example:tri_reticle\", \"textureId\": \"example:tri_reticle\", \"frameCount\": 1, \"speed\": 1, \"opacity\": 0.4, \"scale\": 1, \"blendMode\": \"screen\" }],\n"
        "  \"borderEffects\": [{ \"elementId\": \"example:tri_reticle\", \"maskTextureId\": \"example:tri_reticle\", \"opacity\": 0.5, \"blendMode\": \"additive\", \"tintColor\": \"66ccffff\", \"scrollX\": 0, \"scrollY\": 0 }],\n"
        "  \"fontShadow\": { \"offsetX\": 1, \"offsetY\": 1, \"color\": \"00000080\" },\n"
        "  \"fontGlow\": { \"radius\": 2, \"intensity\": 0.6, \"color\": \"66ccffff\", \"passes\": 2 }\n"
        "}\n"
    )
    ui = read_archive("ui/tri_reticle.pdui")
    font_bytes = (ROOT.parents[2] / "fonts" / "Menus" /
                  "Handel Gothic Regular" / "Handel Gothic Regular.otf").read_bytes()
    font = archive_bytes([
        ("font.ini",
         "[font]\n"
         "catalog_id = example:tri_theme_font\n"
         "name = Triangle Theme Font\n"
         "font_format = opentype\n"
         "font_file = font.otf\n"),
        ("font.otf", font_bytes),
        ("_meta/manifest.json", manifest_with("font", "example:tri_theme_font", {
            "font_file": "font.otf",
        })),
    ])
    audio = read_archive("audio/sfx/tri_click.pdsfx")
    music = read_archive("audio/music/tri_song.pdsong")
    write_archive("themes/tri_theme.pdtheme", [
        ("theme.ini",
         "[theme]\n"
         "catalog_id = example:tri_theme\n"
         "name = Triangle Theme\n"
         "theme_file = theme.json\n"
         "ui_archive = dependencies/assets/ui/tri_reticle.pdui\n"
         "font_archive = dependencies/assets/font/tri_theme_font.pdfont\n"
         "audio_archive = dependencies/assets/audio/tri_click.pdsfx\n"
         "music_archive = dependencies/assets/music/tri_song.pdsong\n"),
        ("theme.json", theme_json),
        ("dependencies/assets/ui/tri_reticle.pdui", ui),
        ("dependencies/assets/font/tri_theme_font.pdfont", font),
        ("dependencies/assets/audio/tri_click.pdsfx", audio),
        ("dependencies/assets/music/tri_song.pdsong", music),
        ("_meta/manifest.json", theme_manifest("example:tri_theme")),
    ])


def update_prop() -> None:
    model = read_entry("meshes/tri_mesh.pdmesh", "model.gltf")
    behavior = (
        "{\n"
        "  \"schema\": \"pd.prop_behavior.v1\",\n"
        "  \"asset_id\": \"example:tri_prop\",\n"
        "  \"graph_id\": \"runtime\",\n"
        "  \"nodes\": [\n"
        "    { \"id\": \"spawn\", \"kind\": \"event.spawn\", \"params\": {} },\n"
        "    { \"id\": \"health\", \"kind\": \"action.set_health\", \"params\": { \"value\": 125.0 } },\n"
        "    { \"id\": \"ready\", \"kind\": \"action.set_channel\", \"params\": { \"channel\": \"tri_prop_ready\", \"value\": true } },\n"
        "    { \"id\": \"tick\", \"kind\": \"event.tick\", \"params\": {} },\n"
        "    { \"id\": \"enabled\", \"kind\": \"condition.enabled\", \"params\": {} },\n"
        "    { \"id\": \"solid\", \"kind\": \"action.set_collision\", \"params\": { \"value\": true } }\n"
        "  ],\n"
        "  \"edges\": [\n"
        "    { \"from\": \"spawn\", \"to\": \"health\" },\n"
        "    { \"from\": \"health\", \"to\": \"ready\" },\n"
        "    { \"from\": \"tick\", \"to\": \"enabled\" },\n"
        "    { \"from\": \"enabled\", \"to\": \"solid\" }\n"
        "  ]\n"
        "}\n"
    )
    prop_source = (
        "{\n"
        "  \"schema\": \"pd2.prop.v2\",\n"
        "  \"catalog_id\": \"example:tri_prop\",\n"
        "  \"prop_key\": \"object\",\n"
        "  \"display_name\": \"Triangle Prop\",\n"
        "  \"health\": 125.0,\n"
        "  \"flags\": 0\n"
        "}\n"
    )
    write_archive("props/tri_prop.pdprop", [
        ("prop.ini",
         "[prop]\n"
         "catalog_id = example:tri_prop\n"
         "name = Triangle Prop\n"
         "prop_key = object\n"
         "model_file = model.gltf\n"
         "health = 125\n"
         "flags = 0\n"
         "prop_file = prop.json\n"
         "behavior_graph = behavior.graph.json\n"),
        ("model.gltf", model),
        ("prop.json", prop_source),
        ("behavior.graph.json", behavior),
        ("_meta/manifest.json", prop_manifest("example:tri_prop")),
    ])


def update_vehicle() -> None:
    model = read_entry("meshes/tri_mesh.pdmesh", "model.gltf")
    physics = (
        "{\n"
        "  \"schema\": \"pd2.vehicle.physics.v2\",\n"
        "  \"catalog_id\": \"example:tri_vehicle\",\n"
        "  \"archetype\": \"hoverbike\",\n"
        "  \"turn_input_scale\": 0.05,\n"
        "  \"reverse_turn_gain\": 0.6,\n"
        "  \"steering_response_ntsc\": 0.08,\n"
        "  \"steering_response_pal\": 0.09,\n"
        "  \"turn_visual_scale\": 10.0,\n"
        "  \"input_response\": 0.4,\n"
        "  \"forward_input_scale\": 0.7,\n"
        "  \"lateral_input_scale\": 0.3,\n"
        "  \"lean_response\": 4.0,\n"
        "  \"forward_base\": 0.25,\n"
        "  \"forward_accel_gain\": 0.75,\n"
        "  \"reverse_base\": 0.45,\n"
        "  \"drag_ntsc\": 0.96,\n"
        "  \"drag_pal\": 0.95,\n"
        "  \"forward_thrust\": 1.2,\n"
        "  \"lateral_thrust\": 0.8,\n"
        "  \"forward_tilt\": 0.2,\n"
        "  \"lateral_tilt\": 0.3,\n"
        "  \"tilt_response_ntsc\": 0.05,\n"
        "  \"tilt_response_pal\": 0.06,\n"
        "  \"yaw_response_ntsc\": 0.16,\n"
        "  \"yaw_response_pal\": 0.18,\n"
        "  \"boost_speed\": 7.0,\n"
        "  \"boost_time_ticks60\": 1800,\n"
        "  \"hover\": [82, 1, 3, 0.0025, 0.1, 0.01, 0.02, 0.00002, 0.0006, 0.01, 0.02, 0.00002, 0.0006]\n"
        "}\n"
    )
    behavior = (
        "{\n"
        "  \"schema\": \"pd2.vehicle.behavior.v2\",\n"
        "  \"catalog_id\": \"example:tri_vehicle\",\n"
        "  \"allow_mount\": true,\n"
        "  \"allow_drive\": true,\n"
        "  \"allow_dismount\": true\n"
        "}\n"
    )
    write_archive("vehicles/tri_vehicle.pdvehicle", [
        ("vehicle.ini",
         "[vehicle]\n"
         "catalog_id = example:tri_vehicle\n"
         "name = Triangle Hoverbike\n"
         "model_file = model.gltf\n"
         "physics_file = physics.json\n"
         "behavior_graph = behavior.graph.json\n"),
        ("model.gltf", model),
        ("physics.json", physics),
        ("behavior.graph.json", behavior),
        ("_meta/manifest.json", vehicle_manifest("example:tri_vehicle")),
    ])


def update_gamemode() -> None:
    rules = (
        "{\n"
        "  \"schema\": \"pd2.gamemode.rules.v2\",\n"
        "  \"catalog_id\": \"example:tri_gamemode\",\n"
        "  \"mode_key\": \"combat\",\n"
        "  \"name\": \"Triangle Combat\",\n"
        "  \"description\": \"Free-for-all combat using editable public rules.\",\n"
        "  \"players\": { \"min\": 1, \"max\": 8 },\n"
        "  \"teams\": { \"required\": false },\n"
        "  \"requirefeature\": 0\n"
        "}\n"
    )
    write_archive("gamemodes/tri_gamemode.pdgamemode", [
        ("gamemode.ini",
         "[gamemode]\n"
         "catalog_id = example:tri_gamemode\n"
         "name = Triangle Combat\n"
         "description = Free-for-all combat using editable public rules.\n"
         "mode_key = combat\n"
         "min_players = 1\n"
         "max_players = 8\n"
         "team_based = 0\n"
         "requirefeature = 0\n"
         "rules_file = rules.json\n"),
        ("rules.json", rules),
        ("_meta/manifest.json", manifest_with("gamemode", "example:tri_gamemode", {
            "name": "Triangle Combat",
            "description": "Free-for-all combat using editable public rules.",
            "mode_key": "combat",
            "min_players": 1,
            "max_players": 8,
            "team_based": 0,
            "requirefeature": 0,
            "rules_file": "rules.json",
        })),
    ])


def update_hud() -> None:
    layout = (
        "{\n"
        "  \"schema\": \"pd2.hud.layout.v1\",\n"
        "  \"catalog_id\": \"example:tri_hud\",\n"
        "  \"element\": \"ammo\",\n"
        "  \"visible\": true\n"
        "}\n"
    )
    texture = read_entry("hud/tri_hud.pdhud", "texture.png")
    write_archive("hud/tri_hud.pdhud", [
        ("hud.ini",
         "[hud]\n"
         "catalog_id = example:tri_hud\n"
         "name = Triangle HUD\n"
         "hud_key = ammo\n"
         "texture_file = texture.png\n"
         "layout_file = layout.json\n"),
        ("layout.json", layout),
        ("texture.png", texture),
        ("_meta/manifest.json", manifest_with("hud", "example:tri_hud", {
            "texture_file": "texture.png",
            "layout_file": "layout.json",
        })),
    ])


def update_ui() -> None:
    texture = read_entry("ui/tri_reticle.pdui", "texture.png")
    layout = (
        "{\n"
        "  \"schema\": \"pd2.ui.layout.v1\",\n"
        "  \"role\": \"reticle\",\n"
        "  \"texture\": \"texture.png\",\n"
        "  \"slots\": [\n"
        "    { \"id\": \"center\", \"rect\": [0, 0, 16, 16], \"anchor\": \"center\" }\n"
        "  ]\n"
        "}\n"
    )
    nineslice = (
        "[nineslice]\n"
        "left = 4\n"
        "right = 4\n"
        "top = 4\n"
        "bottom = 4\n"
        "edge_mode = stretch\n"
        "center_mode = stretch\n"
    )
    ui_manifest = (
        "{\n"
        "  \"schema\": \"pd.asset_archive.manifest.v1\",\n"
        "  \"pd_kind\": \"ui\",\n"
        "  \"catalog_id\": \"example:tri_reticle\",\n"
        "  \"texture_file\": \"texture.png\",\n"
        "  \"layout_file\": \"layout.json\",\n"
        "  \"nineslice_file\": \"nineslice.ini\",\n"
        "  \"texture\": {\n"
        "    \"name\": \"tri_reticle\",\n"
        "    \"file\": \"texture.png\",\n"
        "    \"width\": 16,\n"
        "    \"height\": 16,\n"
        "    \"format\": \"rgba32_top_down\",\n"
        f"    \"data_size\": {len(texture)},\n"
        "    \"nineslice\": {\n"
        "      \"left\": 4,\n"
        "      \"right\": 4,\n"
        "      \"top\": 4,\n"
        "      \"bottom\": 4,\n"
        "      \"edgeMode\": \"stretch\",\n"
        "      \"centerMode\": \"stretch\"\n"
        "    }\n"
        "  }\n"
        "}\n"
    )
    write_archive("ui/tri_reticle.pdui", [
        ("ui.ini",
         "; tri_reticle.pdui - self-contained UI texture/layout asset\n"
         "[ui]\n"
         "catalog_id = example:tri_reticle\n"
         "name = Triangle Reticle\n"
         "texture_file = texture.png\n"
         "layout_file = layout.json\n"
         "nineslice_file = nineslice.ini\n"
         "texture_name = tri_reticle\n"
         "width = 16\n"
         "height = 16\n"
         f"data_size = {len(texture)}\n"
         "nineslice_left = 4\n"
         "nineslice_right = 4\n"
         "nineslice_top = 4\n"
         "nineslice_bottom = 4\n"
         "nineslice_edge_mode = stretch\n"
         "nineslice_center_mode = stretch\n"
         "\n[meta]\n"
         "manifest = _meta/manifest.json\n"),
        ("texture.png", texture),
        ("layout.json", layout),
        ("nineslice.ini", nineslice),
        ("_meta/manifest.json", ui_manifest),
    ])


def update_skin() -> None:
    texture = read_entry("skins/tri_skin.pdskin", "texture.tga")
    material = read_archive("materials/tri_material.pdmaterial")
    texture_archive = read_archive("textures/tri_texture.pdtexture")
    swatches = (
        "{\n"
        "  \"schema\": \"pd2.skin.swatches.v1\",\n"
        "  \"swatches\": [\n"
        "    { \"name\": \"default\", \"rgba\": [1.0, 1.0, 1.0, 1.0] }\n"
        "  ]\n"
        "}\n"
    )
    skin_source = (
        "{\n"
        "  \"schema\": \"pd2.skin.v1\",\n"
        "  \"catalog_id\": \"example:tri_skin\",\n"
        "  \"target\": \"example:tri_body\",\n"
        "  \"material_slots\": [\n"
        "    { \"slot\": \"default\", \"material\": \"example:tri_material\" }\n"
        "  ]\n"
        "}\n"
    )
    skin_manifest = (
        "{\n"
        "  \"schema\": \"pd.asset_archive.manifest.v1\",\n"
        "  \"pd_kind\": \"skin\",\n"
        "  \"catalog_id\": \"example:tri_skin\",\n"
        "  \"target\": \"example:tri_body\",\n"
        "  \"skin_file\": \"skin.json\",\n"
        "  \"texture_file\": \"texture.tga\",\n"
        "  \"swatches_file\": \"swatches.json\",\n"
        "  \"material_archive\": \"dependencies/assets/material/tri_material.pdmaterial\",\n"
        "  \"texture_archive\": \"dependencies/assets/texture/tri_texture.pdtexture\"\n"
        "}\n"
    )
    write_archive("skins/tri_skin.pdskin", [
        ("skin.ini",
         "[skin]\n"
         "catalog_id = example:tri_skin\n"
         "name = Triangle Skin\n"
         "target = example:tri_body\n"
         "skin_file = skin.json\n"
         "texture_file = texture.tga\n"
         "swatches_file = swatches.json\n"
         "material_archive = dependencies/assets/material/tri_material.pdmaterial\n"
         "texture_archive = dependencies/assets/texture/tri_texture.pdtexture\n"),
        ("skin.json", skin_source),
        ("texture.tga", texture),
        ("swatches.json", swatches),
        ("dependencies/assets/material/tri_material.pdmaterial", material),
        ("dependencies/assets/texture/tri_texture.pdtexture", texture_archive),
        ("_meta/manifest.json", skin_manifest),
    ])


def rewrite_without_directory_entries(rel: str) -> None:
    with zipfile.ZipFile(archive(rel), "r") as zf:
        entries = []
        seen = set()
        for name in zf.namelist():
            if not name or name.endswith("/") or name in seen:
                continue
            entries.append((name, zf.read(name)))
            seen.add(name)
    write_archive(rel, entries)


def main() -> int:
    update_animation(
        "animations/weapon_idle.pdanim",
        "example:weapon_idle",
        "Weapon Idle",
        "weapon_animation",
    )
    update_animation(
        "animations/character_skeletal.pdanim",
        "example:character_skeletal",
        "Character Skeletal",
        "character_animation",
    )

    mesh_bytes = update_mesh()
    update_texture()
    scenario_bytes = update_scenario()
    head_bytes, body_bytes = update_head_and_body(mesh_bytes)
    update_character(head_bytes, body_bytes)
    update_arena(scenario_bytes)
    update_projectile_entity()
    update_botprofile()
    update_effect()
    update_material()
    update_prop()
    update_vehicle()
    update_gamemode()
    update_ui()
    update_hud()
    update_skin()
    update_audio_examples()
    update_song_examples()
    update_font_examples()
    update_lang_examples()
    update_theme()

    rewrite_without_directory_entries("skins/tri_skin.pdskin")
    rewrite_without_directory_entries("themes/tri_theme.pdtheme")
    update_weapon(mesh_bytes)
    scenario_bytes = read_archive("scenarios/tri_scenario.pdscenario")
    rewrite_without_directory_entries("missions/tri_mission.pdmission")
    with zipfile.ZipFile(archive("missions/tri_mission.pdmission"), "r") as zf:
        mission_entries = [(name, zf.read(name)) for name in zf.namelist()
                           if name not in {
                               "dependencies/assets/scenario/tri_scenario.pdscenario",
                               "dependencies/assets/scenarios/tri_scenario.pdscenario",
                               "objectives.tsv",
                               "briefing.tsv",
                               "objectives.json",
                               "briefing.json",
                           }]
    mission_ini = (
        "[mission]\n"
        "catalog_id = example:tri_mission\n"
        "scenario = example:tri_scenario\n"
        "scenario_archive = dependencies/assets/scenarios/tri_scenario.pdscenario\n"
        f"scenario_graph_cache = {MISSION_SCENARIO_DEP_CACHE_KIND}\n"
        "mission_graph_file = mission.graph.json\n"
        "objectives_file = objectives.json\n"
        "category = example\n"
    ).encode("utf-8")
    mission_graph = (
        "{\n"
        "  \"schema\": \"pd2.mission.graph.v1\",\n"
        "  \"catalog_id\": \"example:tri_mission\",\n"
        "  \"scenario_ref\": \"example:tri_scenario\",\n"
        "  \"nodes\": [\n"
        "    { \"id\": \"mission.load\", \"kind\": \"event.mission.load\", \"scenario\": \"example:tri_scenario\" },\n"
        "    { \"id\": \"mission.objectives\", \"kind\": \"mission.objectives.source\", \"file\": \"objectives.json\", \"scenario_table\": \"dependencies/assets/scenarios/tri_scenario.pdscenario::objectives.json\" },\n"
        "    { \"id\": \"mission.objective.0000\", \"kind\": \"mission.objective.source\", \"source_row\": \"objective_0000\", \"scenario_node\": \"level.objective.0000\", \"text_token\": \"objective_text_primary\", \"difficulty_mask\": \"all\", \"criteria\": \"objective\", \"operand_kind\": \"objective\", \"target_ref\": \"\", \"target_record_ref\": \"\", \"pad_ref\": \"\", \"state_ref\": \"\", \"match_value\": \"\", \"initial_status\": \"\" },\n"
        "    { \"id\": \"mission.phase.load\", \"kind\": \"mission.phase.source\", \"phase\": \"load\" },\n"
        "    { \"id\": \"mission.phase.active\", \"kind\": \"mission.phase.source\", \"phase\": \"active\" },\n"
        "    { \"id\": \"mission.phase.complete\", \"kind\": \"mission.phase.source\", \"phase\": \"complete\" },\n"
        "    { \"id\": \"mission.phase.failed\", \"kind\": \"mission.phase.source\", \"phase\": \"failed\" },\n"
        "    { \"id\": \"mission.phase.end\", \"kind\": \"mission.phase.source\", \"phase\": \"end\" },\n"
        "    { \"id\": \"mission.parity_backend\", \"kind\": \"mission.behavior.parity_backend\", \"module\": \"og.mission.example\" }\n"
        "  ],\n"
        "  \"edges\": [\n"
        "    { \"from\": \"mission.load\", \"to\": \"mission.objectives\" },\n"
        "    { \"from\": \"mission.objectives\", \"to\": \"mission.objective.0000\" },\n"
        "    { \"from\": \"mission.objectives\", \"to\": \"mission.parity_backend\" }\n"
        "  ]\n"
        "}\n"
    ).encode("utf-8")
    mission_objectives = (
        "{\n"
        "  \"schema\": \"pd2.mission.objectives.v1\",\n"
        "  \"rows\": [\n"
        "    { \"objective_id\": \"objective_0000\", \"kind\": \"objective\", \"text_token\": \"objective_text_primary\", \"difficulty_mask\": \"all\", \"graph_node\": \"mission.objective.0000\", \"scenario_source\": \"dependencies/assets/scenarios/tri_scenario.pdscenario::objectives.json#objective_0000\", \"operand_kind\": \"objective\", \"target_ref\": \"\", \"target_record_ref\": \"\", \"pad_ref\": \"\", \"state_ref\": \"\", \"match_value\": \"\", \"initial_status\": \"\" }\n"
        "  ]\n"
        "}\n"
    ).encode("utf-8")
    mission_manifest = (
        "{\n"
        "  \"schema\": \"pd.asset_archive.manifest.v1\",\n"
        "  \"pd_kind\": \"mission\",\n"
        "  \"catalog_id\": \"example:tri_mission\",\n"
        "  \"mission_graph_file\": \"mission.graph.json\",\n"
        "  \"scenario_archive\": \"dependencies/assets/scenarios/tri_scenario.pdscenario\",\n"
        "  \"objectives_file\": \"objectives.json\"\n"
        "}\n"
    ).encode("utf-8")
    mission_entries = [
        (name, mission_graph if name == "mission.graph.json"
         else mission_ini if name == "mission.ini"
         else mission_manifest if name == "_meta/manifest.json"
         else data)
        for name, data in mission_entries
    ]
    mission_entries.extend([
        ("objectives.json", mission_objectives),
    ])
    mission_entries.insert(4, (
        "dependencies/assets/scenarios/tri_scenario.pdscenario",
        scenario_bytes,
    ))
    write_archive("missions/tri_mission.pdmission", mission_entries)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
