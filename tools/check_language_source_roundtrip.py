#!/usr/bin/env python3
"""Compare extracted .pdlang source with its provenance-named native input.

Example: python tools/check_language_source_roundtrip.py --root <fresh-run-root>
    --rom-id ntsc-final --locale en --output language-roundtrip.json

The root contains data/<rom-id>/{files,lang}. Omit --locale to check every
published locale. This is extraction/source parity, not runtime or glyph proof.
"""

from __future__ import annotations

import argparse
from collections import Counter
import configparser
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import sys
import zipfile
import zlib

from asset_gltf_source import parse_document as read_strict_json
from asset_archive_conformance import validate_lang_strings_json


LIMIT = 0x7fffffff  # Same signed provider boundary as lang_source.cpp.
MAX_STRINGS = 512
SOURCE_NAME = re.compile(r"L(?P<bank>[A-Za-z0-9_]+?)(?P<suffix>_str_[fgis]Z?|[EJP])\.bin\Z")
SUFFIX_LOCALES = {"E": "en", "P": "en-GB", "J": "ja",
                  "_str_f": "fr", "_str_g": "de", "_str_i": "it", "_str_s": "es"}


class ParityError(ValueError):
    pass


def integer(value, low=0, high=MAX_STRINGS):
    if (type(value) not in (int, float) or not math.isfinite(value)
            or value != int(value) or not low <= value <= high):
        raise ParityError(f"expected integer in {low}..{high}, got {value!r}")
    return int(value)


def locale_tag(value):
    if not isinstance(value, str) or not re.fullmatch(r"[A-Za-z]{2,3}(?:-[A-Za-z0-9]{2,8})*", value):
        raise ParityError("invalid locale tag")
    aliases = {"jp": "ja", "gb": "en-GB", "en-gb": "en-GB"}
    return aliases.get(value.lower(), value.lower())


def safe_relative(value):
    if not isinstance(value, str) or not value or len(value.encode("utf-8")) >= 1024:
        raise ParityError("invalid relative source/member path")
    if any(ord(c) < 32 or ord(c) == 127 or c in '\\:%?*|<>"' for c in value):
        raise ParityError("unsafe relative source/member path")
    parts = value.split("/")
    if any(not p or p in (".", "..") or p.endswith((".", " ")) for p in parts):
        raise ParityError("escaping or ambiguous relative source/member path")
    if any(re.fullmatch(r"(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?", p, re.I) for p in parts):
        raise ParityError("device name in source/member path")
    return parts


def resolve_source(root, rom_id, value):
    parts = safe_relative(value)
    if len(parts) != 4 or parts[:3] != ["data", rom_id, "files"] or not SOURCE_NAME.fullmatch(parts[3]):
        raise ParityError("provenance source_path is not a language file in the selected ROM files directory")
    path = root.joinpath(*parts).resolve(strict=True)
    if not path.is_relative_to(root) or not path.is_file():
        raise ParityError("provenance source_path escapes the fresh root or is not a file")
    return path


def read_bounded(path):
    size = path.stat().st_size
    if not 0 < size <= LIMIT:
        raise ParityError("file size outside provider extent")
    data = path.read_bytes()
    if len(data) != size:
        raise ParityError("source changed while reading")
    return data


def sha(data):
    return hashlib.sha256(data).hexdigest()


def check_hash(data, sidecar, label):
    if not re.fullmatch(rb"[0-9a-fA-F]{64}(?:\r?\n)?", sidecar):
        raise ParityError(f"{label} malformed SHA-256 sidecar")
    actual = sha(data)
    if sidecar.strip().decode("ascii").lower() != actual:
        raise ParityError(f"{label} SHA-256 mismatch")
    return actual


def decode_native_source(data):
    """src/lib/rzip_c.c and tools/assetmgr/assetmgr.py:zip/write_object.

    1173 + 24-bit BE decoded length + raw DEFLATE. ELF source sections have
    16-byte alignment/zero fill; an unpadded stream is also accepted.
    """
    if data.startswith(b"\x11\x72"):
        raise ParityError("RZIP1172 is outside this verifier's declared-size contract")
    if data.startswith(b"\x11\x73"):
        if len(data) < 6:
            raise ParityError("truncated RZIP1173 header or stream")
        declared = int.from_bytes(data[2:5], "big")
        if not declared:
            raise ParityError("RZIP1173 declares zero decoded bytes")
        stream = zlib.decompressobj(-15)
        try:
            native = stream.decompress(data[5:], declared + 1)
        except zlib.error as exc:
            raise ParityError(f"invalid RZIP1173 DEFLATE: {exc}") from exc
        if len(native) != declared or not stream.eof or stream.unconsumed_tail:
            raise ParityError("RZIP1173 truncated stream or declared decoded-size mismatch")
        tail = stream.unused_data
        if tail and (any(tail) or len(tail) > 15 or len(data) % 16):
            raise ParityError("RZIP1173 has nonzero or non-alignment trailing bytes")
        return native, {"container": "rzip1173", "declared_size": declared,
                        "alignment_padding": len(tail)}
    return data, {"container": "native", "declared_size": None, "alignment_padding": 0}


def native_table(data):
    """Independently recover complete native extent; never trust authored count.

    The first nonnull string starts immediately after mklang's offset table.
    The canonical empty bank is exactly sixteen zero bytes. Other all-zero
    extents cannot prove their table length and fail closed.
    """
    if not 4 <= len(data) <= LIMIT:
        raise ParityError("native language extent is invalid")
    boundary = next(((pos, struct.unpack_from(">I", data, pos)[0])
                  for pos in range(0, min(len(data) // 4, MAX_STRINGS) * 4, 4)
                  if struct.unpack_from(">I", data, pos)[0]), None)
    if boundary is None:
        if data == bytes(16):
            return []
        raise ParityError("all-null native table extent is ambiguous")
    pos, first = boundary
    if first <= pos or first % 4 or not 1 <= first // 4 <= MAX_STRINGS or first >= len(data):
        raise ParityError("native table boundary is invalid")
    count = first // 4
    offsets = struct.unpack_from(f">{count}I", data)
    rows = []
    for index, offset in enumerate(offsets):
        if offset == 0:
            rows.append(None)
            continue
        if offset < first or offset >= len(data) or offset % 4:
            raise ParityError(f"native row {index} has invalid string offset")
        end = data.find(b"\0", offset)
        if end < 0:
            raise ParityError(f"native row {index} has no terminating NUL")
        rows.append(data[offset:end].decode("latin-1"))
    return rows


def source_table(data):
    text = data.decode("utf-8-sig", errors="strict")
    errors = []
    validate_lang_strings_json("roundtrip", text, errors)
    if errors:
        raise ParityError("; ".join(errors))
    doc = read_strict_json(text.encode("utf-8"))
    rows = doc["strings"]
    ordered = [None] * len(rows)
    for row in rows:
        ordered[int(row["index"])] = row["text"]
    return ordered


def descriptor(data):
    parser = configparser.ConfigParser(interpolation=None, strict=True)
    parser.read_string(data.decode("utf-8-sig", errors="strict"))
    if parser.defaults() or "lang" not in parser:
        raise ParityError("lang.ini requires an explicit [lang] section without inherited defaults")
    values = dict(parser["lang"])
    for key in ("source_bank", "string_count"):
        if not re.fullmatch(r"[0-9]+", values.get(key, "")):
            raise ParityError(f"lang.ini {key} requires a complete decimal integer")
    values["source_bank"] = integer(int(values["source_bank"]), 1, 68)
    values["string_count"] = integer(int(values["string_count"]))
    return values


def member(zf, name):
    info = zf.getinfo(name)
    if info.is_dir() or not 0 < info.file_size <= LIMIT:
        raise ParityError(f"archive member extent invalid: {name}")
    return zf.read(info)


def semantic_hash(rows):
    return sha(json.dumps(rows, ensure_ascii=False, separators=(",", ":")).encode("utf-8"))


def verify_archive(root, rom_id, archive, selected_locales):
    result = {"archive": archive.relative_to(root).as_posix(), "status": "failed", "selected": True}
    try:
        if not archive.resolve(strict=True).is_relative_to(root):
            raise ParityError("archive escapes fresh root")
        result["archive_sha256"] = sha(read_bounded(archive))
        with zipfile.ZipFile(archive) as zf:
            seen = set()
            for info in zf.infolist():
                safe_relative(info.filename[:-1] if info.is_dir() else info.filename)
                name = info.filename.casefold()
                if name in seen:
                    raise ParityError("duplicate or case-ambiguous ZIP member")
                seen.add(name)
            manifest = read_strict_json(member(zf, "_meta/manifest.json").removeprefix(b"\xef\xbb\xbf"))
            provenance = read_strict_json(member(zf, "_meta/provenance.json").removeprefix(b"\xef\xbb\xbf"))
            locale = locale_tag(manifest.get("locale"))
            result.update(locale=locale, catalog_id=manifest.get("id"), source_path=provenance.get("source_path"))
            if selected_locales and locale not in selected_locales:
                result.update(status="not_selected", selected=False)
                return result
            if (manifest.get("pd_kind") != "lang" or integer(manifest.get("pd_schema_version"), 1, 1) != 1
                    or not isinstance(manifest.get("id"), str) or not manifest["id"]):
                raise ParityError("invalid language manifest identity/schema")
            if provenance.get("family") != "lang" or provenance.get("id") != manifest["id"]:
                raise ParityError("provenance identity differs from manifest")
            source = resolve_source(root, rom_id, provenance.get("source_path"))
            suffix = SOURCE_NAME.fullmatch(source.name)["suffix"].removesuffix("Z")
            if locale != SUFFIX_LOCALES[suffix]:
                raise ParityError("declared locale differs from provenance source suffix")
            result["codec"] = "jpn-final-packed-unsupported" if rom_id == "jpn-final" and suffix == "J" else "latin-1"
            if result["codec"] != "latin-1":
                raise ParityError("Japanese packed glyph codec is not implemented by this verifier")
            raw = read_bounded(source)
            sidecar = source.with_name(source.name + ".sha256").resolve(strict=True)
            if not sidecar.is_relative_to(root):
                raise ParityError("source hash sidecar escapes fresh root")
            result["source_sha256"] = check_hash(raw, read_bounded(sidecar), "native source")
            result["source_hash_basis"] = "extracted raw-file sidecar; provenance records path, not source digest"
            native, container = decode_native_source(raw)
            result.update(container, source_size=len(raw), decoded_source_size=len(native), decoded_source_sha256=sha(native))
            original = native_table(native)
            public_bytes = member(zf, "strings.json")
            public = source_table(public_bytes)
            result["strings_sha256"] = check_hash(public_bytes, member(zf, "_meta/strings.json.sha256"), "strings.json")
            ini_bytes = member(zf, "lang.ini")
            check_hash(ini_bytes, member(zf, "_meta/lang.ini.sha256"), "lang.ini")
            ini = descriptor(ini_bytes)
            if (ini.get("catalog_id") != manifest["id"] or locale_tag(ini.get("locale")) != locale
                    or ini.get("strings_file") != "strings.json" or manifest.get("data") != "strings.json"):
                raise ParityError("descriptor/source binding differs from manifest")
            bank = integer(manifest.get("source_bank"), 1, 68)
            if ini["source_bank"] != bank:
                raise ParityError("descriptor/manifest bank mismatch")
            result["bank"] = bank
            declared = integer(manifest.get("string_count"))
            if declared != ini["string_count"] or declared != len(public):
                raise ParityError("descriptor/manifest/public string count mismatch")
            for key, actual in (("source_data_size", len(raw)), ("decoded_source_size", len(native)), ("data_size", len(public_bytes))):
                if integer(manifest.get(key), 0, LIMIT) != actual:
                    raise ParityError(f"manifest {key} mismatch")
            hashes = read_strict_json(member(zf, "_meta/hashes.json"))
            entries = hashes.get("entries")
            if not isinstance(entries, list):
                raise ParityError("archive source hashes table is missing")
            for name, data in (("strings.json", public_bytes), ("lang.ini", ini_bytes)):
                matches = [row for row in entries if isinstance(row, dict) and row.get("path") == name]
                if (len(matches) != 1 or matches[0].get("sha256") != sha(data)
                        or integer(matches[0].get("size"), 0, LIMIT) != len(data)):
                    raise ParityError(f"hashes.json {name} identity/size/hash mismatch")
            result.update(native_count=len(original), public_count=len(public),
                          native_null_indexes=[i for i, value in enumerate(original) if value is None],
                          public_null_indexes=[i for i, value in enumerate(public) if value is None],
                          native_empty_indexes=[i for i, value in enumerate(original) if value == ""],
                          public_empty_indexes=[i for i, value in enumerate(public) if value == ""],
                          native_semantic_sha256=semantic_hash(original), public_semantic_sha256=semantic_hash(public),
                          rows_compared=max(len(original), len(public)))
            missing = {"missing_row": True}
            result["mismatches"] = [{"index": i, "native": original[i] if i < len(original) else missing,
                                      "public": public[i] if i < len(public) else missing}
                                     for i in range(max(len(original), len(public)))
                                     if i >= len(original) or i >= len(public) or original[i] != public[i]]
            if result["mismatches"]:
                raise ParityError("native/public table semantics differ")
            result["status"] = "passed"
    except (OSError, ValueError, KeyError, OverflowError, configparser.Error, zipfile.BadZipFile, RuntimeError) as exc:
        result["error"] = str(exc)
    return result


def verify_root(root, rom_id, locales=(), expected_archives=None):
    root = Path(root).resolve(strict=True)
    if not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", rom_id):
        raise ParityError("invalid ROM ID")
    selected = {locale_tag(value) for value in locales}
    lang_dir = root / "data" / rom_id / "lang"
    files_dir = root / "data" / rom_id / "files"
    for directory in (lang_dir, files_dir):
        if not directory.resolve(strict=True).is_relative_to(root) or not directory.is_dir():
            raise ParityError("language/files directory escapes fresh root or is missing")
    archives = sorted(lang_dir.glob("*.pdlang"), key=lambda p: p.name)
    results = [verify_archive(root, rom_id, archive, selected) for archive in archives]
    identities = Counter((row.get("bank"), row.get("locale")) for row in results if row["status"] == "passed")
    for row in results:
        if row["status"] == "passed" and identities[(row["bank"], row["locale"])] > 1:
            row.update(status="failed", error="duplicate published bank/locale identity")
    referenced = {row["source_path"] for row in results if isinstance(row.get("source_path"), str)}
    inventory = []
    for path in sorted(files_dir.glob("L*.bin"), key=lambda p: p.name):
        match = SOURCE_NAME.fullmatch(path.name)
        if not match:
            continue
        if not path.resolve(strict=True).is_relative_to(root):
            raise ParityError("raw inventory candidate escapes fresh root")
        source_path = path.relative_to(root).as_posix()
        inventory.append({"source_path": source_path, "bank_name_from_filename": match["bank"],
                          "locale_from_filename": SUFFIX_LOCALES[match["suffix"].removesuffix("Z")],
                          "size": path.stat().st_size, "referenced_by_archive": source_path in referenced})
    statuses = Counter(row["status"] for row in results)
    selected_count = sum(row["selected"] for row in results)
    errors = [] if selected_count else ["no archives matched the selected locale scope"]
    if expected_archives is not None and selected_count != expected_archives:
        errors.append(f"selected archive count {selected_count} differs from expected {expected_archives}")
    unreferenced_selected = [row["source_path"] for row in inventory if not row["referenced_by_archive"]
                             and (not selected or row["locale_from_filename"] in selected)]
    return {"schema": "pd2.language-source-roundtrip.v1", "root": str(root), "rom_id": rom_id,
            "selected_locales": sorted(selected), "scope": "selected published archive source parity; not runtime or glyph proof",
            "passed": not errors and statuses["failed"] == 0, "errors": errors,
            "summary": {"archives_discovered": len(results), "archives_selected": selected_count,
                        "passed": statuses["passed"], "failed": statuses["failed"], "not_selected": statuses["not_selected"],
                        "extracted_raw_candidates": len(inventory), "referenced_raw_candidates": sum(row["referenced_by_archive"] for row in inventory),
                        "selected_locale_unreferenced_raw_candidates": len(unreferenced_selected),
                        "raw_candidates_by_locale": dict(sorted(Counter(row["locale_from_filename"] for row in inventory).items())),
                        "published_archives_by_locale": dict(sorted(Counter(row.get("locale", "unknown") for row in results).items()))},
            "inventory_basis": "extracted file names only; not authoritative ROM enumeration or translation-completeness proof",
            "translation_completeness_proven": False, "selected_locale_unreferenced_raw_sources": unreferenced_selected,
            "archives": results, "raw_inventory": inventory}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True, help="fresh run directory containing data/<rom-id>")
    parser.add_argument("--rom-id", required=True)
    parser.add_argument("--locale", action="append", default=[], help="repeat to select locales; default all published")
    parser.add_argument("--expected-archives", type=int, help="fail if the selected archive count differs (e.g. 68 English banks)")
    parser.add_argument("--output", type=Path, help="machine-readable receipt; also printed to stdout")
    args = parser.parse_args(argv)
    if args.expected_archives is not None and args.expected_archives < 1:
        parser.error("--expected-archives must be positive")
    if args.output and args.output.resolve().is_relative_to((args.root / "data").resolve()):
        parser.error("--output must be outside the input data directory")
    try:
        receipt = verify_root(args.root, args.rom_id, args.locale, args.expected_archives)
    except (OSError, ValueError) as exc:
        receipt = {"schema": "pd2.language-source-roundtrip.v1", "passed": False, "errors": [str(exc)], "archives": []}
    # ASCII JSON escapes also make redirected Windows console output lossless.
    encoded = json.dumps(receipt, ensure_ascii=True, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded, encoding="utf-8")
    sys.stdout.write(encoded)
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
