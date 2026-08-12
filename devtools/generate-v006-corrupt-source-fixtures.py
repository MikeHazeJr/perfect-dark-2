#!/usr/bin/env python3
"""Generate V-006 public-source corruption fixtures in an installed .pdmod."""

from __future__ import annotations

import argparse
import io
import json
import zipfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
EXAMPLE_ROOT = REPO_ROOT / "examples" / "modding" / "typed-pdxxx-basic"


def archive_bytes(entries: list[tuple[str, bytes]]) -> bytes:
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, payload in entries:
            info = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, payload)
    return output.getvalue()


def rewrite_example(
    relative_path: str,
    old_id: str,
    new_id: str,
    replacements: dict[str, bytes],
) -> bytes:
    entries: list[tuple[str, bytes]] = []
    with zipfile.ZipFile(EXAMPLE_ROOT / relative_path, "r") as source:
        for name in source.namelist():
            payload = source.read(name)
            if name.endswith(".ini") or name.endswith(".json"):
                payload = payload.replace(old_id.encode(), new_id.encode())
            payload = replacements.get(name, payload)
            entries.append((name, payload))
    return archive_bytes(entries)


def corrupt_mp3_voice() -> bytes:
    descriptor = """[voice]
catalog_id = v006:corrupt_mp3
name = V006 Corrupt MP3
audio_category = voice
format = MP3
source_format = MP3
file_path = sample.mp3
data_size = 16
duration_ms = 1000
source_index = -1
mapped_soundnum = -1
mp3_priority = 2
source_symbol = custom

[meta]
manifest = _meta/manifest.json
""".encode()
    manifest = json.dumps(
        {
            "schema": "pd.asset_archive.manifest.v1",
            "pd_kind": "voice",
            "catalog_id": "v006:corrupt_mp3",
            "format": "MP3",
            "source_format": "MP3",
            "data": "sample.mp3",
            "data_size": 16,
            "duration_ms": 1000,
            "source_index": -1,
            "mapped_soundnum": -1,
            "mp3_priority": 2,
            "source_symbol": "custom",
        },
        indent=2,
    ).encode() + b"\n"
    return archive_bytes(
        [
            ("voice.ini", descriptor),
            ("sample.mp3", b"not-an-mp3-file!"),
            ("_meta/manifest.json", manifest),
        ]
    )


def corrupt_vector_font() -> bytes:
    descriptor = """[font]
catalog_id = v006:corrupt_font
name = V006 Corrupt Vector Font
face = v006_corrupt
font_format = vector_sfnt
font_file = font.otf
source_segment = custom

[meta]
manifest = _meta/manifest.json
""".encode()
    manifest = json.dumps(
        {
            "schema": "pd.asset_archive.manifest.v1",
            "pd_kind": "font",
            "catalog_id": "v006:corrupt_font",
            "face": "v006_corrupt",
            "format": "vector_sfnt",
            "font_file": "font.otf",
            "source_segment": "custom",
        },
        indent=2,
    ).encode() + b"\n"
    return archive_bytes(
        [
            ("font.ini", descriptor),
            ("font.otf", b"not-an-sfnt-face"),
            ("_meta/manifest.json", manifest),
        ]
    )


def generate(install_dir: Path) -> None:
    assets = {
        "textures/corrupt_texture.pdtexture": rewrite_example(
            "textures/tri_texture.pdtexture", "example:tri_texture",
            "v006:corrupt_texture", {"texture.png": b"not-a-png"}),
        "fonts/corrupt_font.pdfont": corrupt_vector_font(),
        "animations/corrupt_animation.pdanim": rewrite_example(
            "animations/character_skeletal.pdanim", "example:character_skeletal",
            "v006:corrupt_animation", {"animation.gltf": b"{broken-json"}),
        "audio/sfx/corrupt_sfx.pdsfx": rewrite_example(
            "audio/sfx/tri_click.pdsfx", "example:tri_click",
            "v006:corrupt_sfx", {"sample.wav": b"not-a-wave"}),
        "audio/voice/corrupt_voice.pdvoice": rewrite_example(
            "audio/voice/tri_voice.pdvoice", "example:tri_voice",
            "v006:corrupt_voice", {
                "sample.wav": b"not-a-wave",
                "locales/en.wav": b"not-a-wave",
                "locales/fr.wav": b"not-a-wave",
            }),
        "audio/voice/corrupt_mp3.pdvoice": corrupt_mp3_voice(),
        "audio/music/corrupt_song.pdsong": rewrite_example(
            "audio/music/tri_song.pdsong", "example:tri_song",
            "v006:corrupt_song", {
                "sequence.mid": b"not-midi",
                "sequence.json": b"{broken-json",
            }),
    }
    mod_manifest = json.dumps(
        {
            "id": "v006_corrupt_sources",
            "name": "V006 Corrupt Public Sources",
            "version": "1.0.0",
            "author": "PD2 verification",
            "description": "Valid typed envelopes with deliberately corrupt selected public payloads.",
        },
        indent=2,
    ).encode() + b"\n"
    destination = install_dir / "mods" / "installed" / "v006_corrupt_sources.pdmod"
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(archive_bytes([("mod.json", mod_manifest)] + sorted(assets.items())))
    (install_dir / "mods-enabled.json").write_text(
        '[\n  "v006_corrupt_sources"\n]\n', encoding="utf-8", newline="\n")
    print(f"generated V-006 corrupt public-source fixture: {destination}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install-dir", type=Path, required=True)
    args = parser.parse_args()
    generate(args.install_dir.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
