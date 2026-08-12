#!/usr/bin/env python3
"""Generate V-009 same-destination Needler effect replacement PDCA fixtures."""

from __future__ import annotations

import argparse
import io
import json
import struct
import sys
import zipfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import build_needler_mod as needler  # noqa: E402


def replace_effect_audio_dependency(archive: bytes, catalog_id: str) -> bytes:
    entries: list[tuple[str, bytes]] = []
    with zipfile.ZipFile(io.BytesIO(archive), "r") as source:
        for name in source.namelist():
            data = source.read(name)
            if name == "effect.graph.json":
                graph = json.loads(data.decode("utf-8"))
                graph["nodes"][0]["params"]["audio_catalog_id"] = catalog_id
                data = needler.dumps_graph(graph).encode("utf-8")
            entries.append((name, data))
    return needler.build_archive_bytes(entries)


def write_pdca(path: Path, member: str, payload: bytes) -> None:
    member_bytes = member.encode("utf-8") + b"\0"
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as stream:
        stream.write(struct.pack("<IHH", 0x41434450, 1, len(member_bytes)))
        stream.write(member_bytes)
        stream.write(struct.pack("<I", len(payload)))
        stream.write(payload)


def write_list(path: Path, pdca_relative: str) -> None:
    path.write_text(
        f"{pdca_relative}|needler_effect|received|1\n",
        encoding="utf-8",
        newline="\n",
    )


def generate(install_dir: Path) -> None:
    relative_root = Path("social") / "needler-effect-replacement"
    output_root = install_dir / relative_root
    sfx = needler.build_pink_burst_sfx()
    cyan = needler.build_pink_burst_effect(
        sfx,
        burst_tint=(0.0, 1.0, 1.0, 1.0),
        spark_tint=(0.1, 0.8, 1.0, 1.0),
    )
    invalid = replace_effect_audio_dependency(
        cyan, "mod_needler:missing_replacement_sfx"
    )
    member = "effects/pink_burst.pdeffect"
    write_pdca(output_root / "valid.pdca", member, cyan)
    write_pdca(output_root / "invalid.pdca", member, invalid)
    write_list(
        output_root / "valid-list.txt",
        (relative_root / "valid.pdca").as_posix(),
    )
    write_list(
        output_root / "invalid-list.txt",
        (relative_root / "invalid.pdca").as_posix(),
    )
    print(
        "generated Needler replacement fixtures: valid cyan then invalid "
        "same-destination candidate"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install-dir", type=Path, required=True)
    args = parser.parse_args()
    generate(args.install_dir.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
