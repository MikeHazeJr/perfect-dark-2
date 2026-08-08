#!/usr/bin/env python3
"""Build T-ASSETS-022 native runtime harness inputs from extracted AR34.

The output is disposable test data under the requested directory: one valid
custom archive, one release-valid archive with an unresolved animation sound
catalog ID, one archive with a corrupt late animation, and a pipe-delimited
plan consumed by the production-linked client harness.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
from pathlib import Path
import zipfile


NESTED_EXTS = (".pdanim", ".pdsfx", ".pdvoice", ".pdui")
TEXT_EXTS = (".json", ".ini", ".txt", ".sha256")


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_zip(data: bytes) -> dict[str, bytes]:
    with zipfile.ZipFile(io.BytesIO(data), "r") as zf:
        return {name: zf.read(name) for name in zf.namelist() if not name.endswith("/")}


def write_zip(entries: dict[str, bytes]) -> bytes:
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for name, data in entries.items():
            info = zipfile.ZipInfo(name, (2026, 8, 8, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            zf.writestr(info, data)
    return out.getvalue()


def repair_metadata(entries: dict[str, bytes]) -> None:
    for name in list(entries):
        if name.startswith("_meta/") and name.endswith(".sha256"):
            public = name[len("_meta/") : -len(".sha256")]
            if public in entries:
                entries[name] = (sha(entries[public]) + "\n").encode()

    for meta_name in ("_meta/hashes.json", "_meta/inventory.json"):
        if meta_name not in entries:
            continue
        doc = json.loads(entries[meta_name].decode("utf-8"))
        for row in doc.get("entries", []):
            path = row.get("path")
            if path in entries:
                row["sha256"] = sha(entries[path])
                row["size"] = len(entries[path])
        entries[meta_name] = (json.dumps(doc, indent=2) + "\n").encode()


def transform(data: bytes, namespace: str, missing_audio: bool = False) -> bytes:
    entries = read_zip(data)
    for name, raw in list(entries.items()):
        if name.lower().endswith(NESTED_EXTS):
            entries[name] = transform(
                raw,
                namespace,
                missing_audio and name.lower().endswith(".pdanim"),
            )
        elif name.lower().endswith(TEXT_EXTS):
            text = raw.decode("utf-8")
            text = text.replace("base:", f"{namespace}:")
            if missing_audio:
                text = text.replace(
                    f"{namespace}:sfx_unlabeled_cf",
                    f"{namespace}:missing_audio",
                )
            entries[name] = text.encode("utf-8")
    repair_metadata(entries)
    return write_zip(entries)


def nested_ids(data: bytes) -> list[str]:
    ids: list[str] = []
    for name, raw in read_zip(data).items():
        if not name.lower().endswith(NESTED_EXTS):
            continue
        for leaf, value in read_zip(raw).items():
            if not leaf.lower().endswith(".ini") or leaf.startswith("_meta/"):
                continue
            for line in value.decode("utf-8").splitlines():
                if line.strip().startswith(("catalog_id =", "id =")):
                    ids.append(line.split("=", 1)[1].strip())
                    break
            if ids and ids[-1].split(":", 1)[0] in value.decode("utf-8"):
                break
    return list(dict.fromkeys(ids))


def corrupt_late_animation(data: bytes) -> bytes:
    entries = read_zip(data)
    animations = sorted(name for name in entries if name.lower().endswith(".pdanim"))
    if not animations:
        raise RuntimeError("source weapon has no nested animation")
    victim = animations[-1]
    entries[victim] = entries[victim][:-40]
    repair_metadata(entries)
    return write_zip(entries)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", default="Build/data/ntsc-final/weapons/base_ar34.pdweapon")
    parser.add_argument("--output", default=".claude/weapon-nested-runtime-harness")
    args = parser.parse_args()
    source = Path(args.source).resolve()
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    raw = source.read_bytes()

    valid = transform(raw, "harness_ok")
    unresolved = transform(raw, "harness_missing", missing_audio=True)
    corrupt_source = transform(raw, "harness_corrupt")
    corrupt_absent = ",".join(nested_ids(corrupt_source))
    corrupt = corrupt_late_animation(corrupt_source)
    valid_path = output / "valid.pdweapon"
    unresolved_path = output / "unresolved.pdweapon"
    corrupt_path = output / "corrupt.pdweapon"
    valid_path.write_bytes(valid)
    unresolved_path.write_bytes(unresolved)
    corrupt_path.write_bytes(corrupt)

    unresolved_absent = ",".join(nested_ids(unresolved))
    plan = output / "plan.txt"
    plan.write_text(
        "\n".join(
            [
                f"accept|harness_ok:ar34|{valid_path}|harness_ok:sfx_unlabeled_cf|harness_ok:invanim_ar34_reload",
                f"reject|harness_missing:ar34|{unresolved_path}|{unresolved_absent}",
                f"reject|harness_corrupt:ar34|{corrupt_path}|{corrupt_absent}",
            ]
        )
        + "\n",
        encoding="utf-8",
    )
    print(plan)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
