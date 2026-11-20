#!/usr/bin/env python3
"""Check every extracted base scene against the ROM, room by room (B-1266, B-1267).

A base scenario's scene.glb is written from the room display lists of one of
the ROM's background files. This tool reads those display lists itself, the
way the extractor's walk does, and compares with what each scene.glb holds:
for every room, how many triangles the game draws with back faces culled and
how many from both sides. A scene must equal one background file exactly.

It exists because the scene export once left out every room whose display
data inflated past 8192 bytes, silently: up to two thirds of a stage's
triangles, in every single-player stage, for six months. The extractor now
fails instead of leaving a room out; this is the independent count.

The ROM is the player's own file and is only read. Nothing is written.

  python -B tools/verify_scene_rooms_against_rom.py --rom <pd.ntsc-final.z64> --data <install>/data/ntsc-final
  options: --rooms-only   compare triangle totals per room, not the cull split
                          (for a scene written before the cull mode was recorded)
           --verbose      one line per scene that passes, and the rooms that differ
Exit status 0 when every scene equals a background file, 1 otherwise.
"""

from __future__ import annotations

import argparse
import io
import json
import struct
import sys
import zipfile
import zlib
from pathlib import Path

# tools/extract: where the data segment and the file table are, by ROM.
ROM_LAYOUTS = {
    # name: (data segment offset, file table offset inside the inflated data segment)
    "ntsc-final": (0x39850, 0x28080),
    "ntsc-1.0": (0x39850, 0x28080),
}
BG_BASE = 0x0F000000
G_VTX, G_TRI4, G_CLEARGEOMETRYMODE, G_SETGEOMETRYMODE, G_ENDDL, G_TRI1 = 0x04, 0xB1, 0xB6, 0xB7, 0xB8, 0xBF
G_CULL_FRONT, G_CULL_BACK = 0x1000, 0x2000


def inflate(buf: bytes) -> bytes:
    """An rzip block: 0x11 0x73, three bytes of inflated size, raw deflate."""
    if buf[0:2] != b"\x11\x73":
        raise ValueError("not an rzip block")
    out = zlib.decompressobj(wbits=-15).decompress(buf[5:])
    if len(out) != int.from_bytes(buf[2:5], "big"):
        raise ValueError("rzip block does not inflate to its stated size")
    return out


def rom_background_files(rom: bytes, layout: str) -> dict[str, bytes]:
    data_offset, files_offset = ROM_LAYOUTS[layout]
    data = zlib.decompressobj(wbits=-15).decompress(rom[data_offset + 5:])
    offsets = []
    i = files_offset
    while True:
        off = int.from_bytes(data[i:i + 4], "big")
        if off == 0 and offsets:
            break
        offsets.append(off)
        i += 4
    table = offsets[-1]
    names = []
    i = table
    while True:
        off = int.from_bytes(rom[i:i + 4], "big")
        if off == 0 and names:
            break
        end = rom.index(b"\x00", table + off)
        names.append(rom[table + off:end].decode("ascii"))
        i += 4
    out = {}
    for index, name in enumerate(names):
        if name.startswith("bgdata/") and name.endswith(".seg") and index + 1 < len(offsets):
            out[name[len("bgdata/"):-len(".seg")]] = rom[offsets[index]:offsets[index + 1]]
    return out


def room_census(bg: bytes) -> dict[int, tuple[int, int]]:
    """room number -> (triangles with back faces culled, triangles drawn from both sides)."""
    primary_inflated, _section1, primary_stored = struct.unpack_from(">III", bg, 0)
    primary = inflate(bg[12:12 + primary_stored])
    shift = primary_inflated - primary_stored - 0xC
    rooms_at = struct.unpack_from(">I", primary, 4)[0] - BG_BASE
    table = []
    while True:
        value = struct.unpack_from(">I", primary, rooms_at + len(table) * 0x14)[0]
        if table and value == 0:
            break
        table.append(value)
    census: dict[int, tuple[int, int]] = {}
    for room in range(1, len(table) - 1):
        span = table[room + 1] - table[room]
        if table[room] == 0 or span <= 0:
            continue
        start = table[room] - BG_BASE - shift
        data = inflate(bg[start:start + ((span + 15) & ~15)])
        base = table[room]
        _vertices, _colours, opaque, translucent = struct.unpack_from(">IIII", data, 0)
        single = double = 0
        seen = set()

        def walk_list(offset: int) -> None:
            nonlocal single, double
            geometry = G_CULL_BACK
            valid = [False] * 16
            for _ in range(20000):
                if offset + 8 > len(data):
                    break
                w0, w1 = struct.unpack_from(">II", data, offset)
                op = w0 >> 24
                offset += 8
                if op == G_ENDDL:
                    break
                if op == G_SETGEOMETRYMODE:
                    geometry |= w1
                elif op == G_CLEARGEOMETRYMODE:
                    geometry &= ~w1
                elif op == G_VTX:
                    count = ((w0 >> 20) & 0xF) + 1
                    dest = (w0 >> 16) & 0xF
                    if dest + count <= 16:
                        for slot in range(dest, dest + count):
                            valid[slot] = True
                elif op in (G_TRI1, G_TRI4):
                    if op == G_TRI1:
                        corners = [(((w1 >> 16) & 0xFF) // 10, ((w1 >> 8) & 0xFF) // 10, (w1 & 0xFF) // 10)]
                    else:
                        corners = [((w1 >> (8 * s)) & 0xF, (w1 >> (8 * s + 4)) & 0xF, (w0 >> (4 * s)) & 0xF)
                                   for s in range(4)]
                    for a, b, c in corners:
                        if a > 15 or b > 15 or c > 15 or (a == b and b == c):
                            continue
                        if not (valid[a] and valid[b] and valid[c]):
                            continue
                        cull = geometry & (G_CULL_FRONT | G_CULL_BACK)
                        if cull == 0:
                            double += 1
                        elif cull != (G_CULL_FRONT | G_CULL_BACK):
                            single += 1

        def walk_blocks(pointer: int, depth: int = 0) -> None:
            while pointer and depth < 64:
                at = pointer - base
                if at < 0 or at + 0x14 > len(data) or at in seen:
                    return
                seen.add(at)
                kind = data[at]
                following, first = struct.unpack_from(">II", data, at + 4)
                if kind == 0:
                    if first:
                        walk_list(first - base)
                elif kind == 1:
                    if first:
                        walk_blocks(first, depth + 1)
                else:
                    return
                pointer = following

        walk_blocks(opaque)
        walk_blocks(translucent)
        if single or double:
            census[room] = (single, double)
    return census


COMPONENT = {5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2), 5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4)}
WIDTH = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}


def scene_census(glb: bytes) -> tuple[dict[int, tuple[int, int]], bool]:
    """room number -> (single-sided triangles, double-sided triangles), and
    whether the scene records cull modes (pd2_material.cull on its materials)."""
    magic, _version, total = struct.unpack_from("<4sII", glb, 0)
    if magic != b"glTF":
        raise ValueError("scene.glb is not a glTF binary")
    offset = 12
    doc = None
    binary = b""
    while offset < total:
        length, kind = struct.unpack_from("<I4s", glb, offset)
        chunk = glb[offset + 8:offset + 8 + length]
        if kind == b"JSON":
            doc = json.loads(chunk.decode("utf-8"))
        elif kind.startswith(b"BIN"):
            binary = chunk
        offset += 8 + length
    views, accessors, materials = doc.get("bufferViews", []), doc.get("accessors", []), doc.get("materials", [])

    def read(index: int) -> list[tuple]:
        accessor = accessors[index]
        view = views[accessor["bufferView"]]
        fmt, size = COMPONENT[accessor["componentType"]]
        width = WIDTH[accessor["type"]]
        stride = view.get("byteStride") or size * width
        start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
        return [struct.unpack_from("<" + fmt * width, binary, start + k * stride) for k in range(accessor["count"])]

    records_cull = bool(materials) and any(
        isinstance(m.get("extras", {}).get("pd2_material", {}).get("cull"), str) for m in materials)
    census: dict[int, list[int]] = {}
    for mesh in doc.get("meshes", []):
        for primitive in mesh.get("primitives", []):
            rooms = read(primitive["attributes"]["_PD_ROOM"])
            index = [t[0] for t in read(primitive["indices"])] if "indices" in primitive else list(range(len(rooms)))
            material = materials[primitive["material"]] if "material" in primitive else {}
            double = material.get("doubleSided") is True
            for t in range(0, len(index) - 2, 3):
                entry = census.setdefault(int(rooms[index[t]][0]), [0, 0])
                entry[1 if double else 0] += 1
    return {room: (v[0], v[1]) for room, v in census.items()}, records_cull


def scenes_of(data_dir: Path):
    """(label, scene.glb bytes) of every scenario archive and every scenario nested in an arena."""
    for path in sorted((data_dir / "scenarios").glob("*.pdscenario")):
        with zipfile.ZipFile(path) as archive:
            yield path.name, archive.read("scene.glb")
    for path in sorted((data_dir / "arenas").glob("*.pdarena")):
        with zipfile.ZipFile(path) as archive:
            for name in sorted(archive.namelist()):
                if name.lower().endswith(".pdscenario"):
                    with zipfile.ZipFile(io.BytesIO(archive.read(name))) as nested:
                        yield path.name + "::" + name.rsplit("/", 1)[-1], nested.read("scene.glb")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--data", required=True, type=Path, help="<install>/data/<rom id>")
    parser.add_argument("--layout", default="ntsc-final", choices=sorted(ROM_LAYOUTS))
    parser.add_argument("--rooms-only", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    backgrounds = {}
    for name, blob in rom_background_files(args.rom.read_bytes(), args.layout).items():
        backgrounds[name] = room_census(blob)
    print("ROM: %d background files, %d rooms with triangles, %d triangles" % (
        len(backgrounds), sum(len(c) for c in backgrounds.values()),
        sum(a + b for c in backgrounds.values() for a, b in c.values())))

    def key(census: dict[int, tuple[int, int]]):
        if args.rooms_only:
            return tuple(sorted((room, a + b) for room, (a, b) in census.items()))
        return tuple(sorted(census.items()))

    by_key: dict[tuple, list[str]] = {}
    for name, census in backgrounds.items():
        by_key.setdefault(key(census), []).append(name)

    scenes = failed = without_cull = 0
    for label, glb in scenes_of(args.data):
        scenes += 1
        census, records_cull = scene_census(glb)
        if not records_cull:
            without_cull += 1
        match = by_key.get(key(census))
        triangles = sum(a + b for a, b in census.values())
        if match and (records_cull or args.rooms_only):
            if args.verbose:
                print("ok    %-58s rooms %3d triangles %6d (two-sided %5d) = %s" % (
                    label, len(census), triangles, sum(b for _, b in census.values()), "/".join(match[:3])))
            continue
        failed += 1
        # Which background file is it closest to, by rooms in common?
        best = max(backgrounds, key=lambda n: (
            sum(1 for room, counts in census.items() if backgrounds[n].get(room) and
                sum(backgrounds[n][room]) == sum(counts)), -abs(len(backgrounds[n]) - len(census))))
        rom_census = backgrounds[best]
        missing = sorted(set(rom_census) - set(census))
        differ = sorted(room for room in set(rom_census) & set(census)
                        if (sum(rom_census[room]) != sum(census[room]) if args.rooms_only
                            else rom_census[room] != census[room]))
        extra = sorted(set(census) - set(rom_census))
        why = "does not record cull modes" if match and not records_cull else "equals no background file"
        print("FAIL  %-58s %s; nearest %s: scene %d triangles in %d rooms, ROM %d in %d; rooms missing %d (%d triangles), differing %d, not in the ROM %d" % (
            label, why, best, triangles, len(census), sum(a + b for a, b in rom_census.values()), len(rom_census),
            len(missing), sum(sum(rom_census[r]) for r in missing), len(differ), len(extra)))
        if args.verbose:
            for room in missing[:40]:
                print("        room %3d missing: ROM %d back-culled + %d two-sided" % (room, *rom_census[room]))
            for room in differ[:40]:
                print("        room %3d differs: scene %s, ROM %s" % (room, census[room], rom_census[room]))
    print("scenes: %d, failed: %d%s" % (scenes, failed,
                                        ", without cull modes: %d" % without_cull if without_cull else ""))
    return 1 if failed or not scenes else 0


if __name__ == "__main__":
    sys.exit(main())
