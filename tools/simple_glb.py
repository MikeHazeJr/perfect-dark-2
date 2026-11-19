#!/usr/bin/env python3
"""simple_glb.py -- just enough of a glTF binary (.glb) writer to make small
models in code: meshes of triangles with texture coordinates, materials with
an embedded PNG or a plain colour, and nodes (meshes or empties) with a
translation, rotation and scale. It writes glTF's own terms: metres, +Y up,
triangles counter-clockwise seen from the front.

Used by tools/tests/test_level_from_model.py and by the Model Room example
(dev-mods/model_room/make_model_room.py), which feed
tools/build_level_from_model.py the way a model exported from Blender would.
"""

from __future__ import annotations

import json
import struct
import zlib


def png(pixels, width: int, height: int) -> bytes:
    """An RGB PNG from a flat list of (r, g, b), row by row."""
    rows = bytearray()
    for y in range(height):
        rows.append(0)  # filter type: none
        for x in range(width):
            rows += bytes(pixels[y * width + x])

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + chunk(b"IEND", b""))


def plain_png(rgb=(200, 120, 60), size: int = 4) -> bytes:
    return png([rgb] * (size * size), size, size)


def quad(a, b, c, d):
    """Two triangles for a-b-c-d given counter-clockwise seen from the front."""
    return [(a, b, c), (a, c, d)]


def box(x0, y0, z0, x1, y1, z1, top=True, bottom=False):
    """The faces of a box, each facing out: four sides, and the top and the
    bottom when asked for."""
    faces = (quad((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)) +    # +z
             quad((x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)) +    # -z
             quad((x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1)) +    # +x
             quad((x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)))     # -x
    if top:
        faces += quad((x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0))
    if bottom:
        faces += quad((x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1))
    return faces


class GlbWriter:
    def __init__(self, generator: str = "simple_glb"):
        self.doc = {"asset": {"version": "2.0", "generator": generator}, "scene": 0,
                    "scenes": [{"nodes": []}], "nodes": [], "meshes": [], "materials": [], "textures": [],
                    "images": [], "accessors": [], "bufferViews": [], "buffers": [{"byteLength": 0}]}
        self.blob = bytearray()

    def view(self, data: bytes) -> int:
        while len(self.blob) % 4:
            self.blob.append(0)
        self.doc["bufferViews"].append({"buffer": 0, "byteOffset": len(self.blob), "byteLength": len(data)})
        self.blob += data
        return len(self.doc["bufferViews"]) - 1

    def accessor(self, data: bytes, component: int, kind: str, count: int, **extra) -> int:
        self.doc["accessors"].append(dict({"bufferView": self.view(data), "componentType": component,
                                           "count": count, "type": kind}, **extra))
        return len(self.doc["accessors"]) - 1

    def material(self, name: str, colour, image: bytes | None) -> int:
        pbr = {"baseColorFactor": list(colour)}
        if image is not None:
            self.doc["images"].append({"mimeType": "image/png", "bufferView": self.view(image)})
            self.doc["textures"].append({"source": len(self.doc["images"]) - 1})
            pbr["baseColorTexture"] = {"index": len(self.doc["textures"]) - 1}
        self.doc["materials"].append({"name": name, "pbrMetallicRoughness": pbr})
        return len(self.doc["materials"]) - 1

    def mesh(self, name: str, triangles, material: int, uv_scale: float = 0.5) -> int:
        """Indexed. Texture coordinates come from the position: x and z on a
        face that is mostly level, the run along the face and y on one that
        is mostly upright, times `uv_scale` (texture repeats per metre)."""
        points, uvs, index = [], [], []
        for triangle in triangles:
            ux, uy, uz = triangle[1][0] - triangle[0][0], triangle[1][1] - triangle[0][1], triangle[1][2] - triangle[0][2]
            vx, vy, vz = triangle[2][0] - triangle[0][0], triangle[2][1] - triangle[0][1], triangle[2][2] - triangle[0][2]
            ny = abs(uz * vx - ux * vz)
            nx = abs(uy * vz - uz * vy)
            nz = abs(ux * vy - uy * vx)
            for point in triangle:
                index.append(len(points))
                points.append(point)
                if ny >= max(nx, nz):
                    uvs.append((point[0] * uv_scale, point[2] * uv_scale))
                elif nx >= nz:
                    uvs.append((point[2] * uv_scale, -point[1] * uv_scale))
                else:
                    uvs.append((point[0] * uv_scale, -point[1] * uv_scale))
        position = self.accessor(b"".join(struct.pack("<3f", *p) for p in points), 5126, "VEC3", len(points),
                                 min=[min(p[i] for p in points) for i in range(3)],
                                 max=[max(p[i] for p in points) for i in range(3)])
        uv = self.accessor(b"".join(struct.pack("<2f", *t) for t in uvs), 5126, "VEC2", len(points))
        component, fmt = (5123, "<H") if len(points) < 65536 else (5125, "<I")
        indices = self.accessor(b"".join(struct.pack(fmt, i) for i in index), component, "SCALAR", len(index))
        self.doc["meshes"].append({"name": name, "primitives": [
            {"attributes": {"POSITION": position, "TEXCOORD_0": uv}, "indices": indices, "material": material}]})
        return len(self.doc["meshes"]) - 1

    def node(self, name: str, mesh: int | None = None, root: bool = True, **transform) -> int:
        node = {"name": name}
        if mesh is not None:
            node["mesh"] = mesh
        node.update(transform)
        self.doc["nodes"].append(node)
        index = len(self.doc["nodes"]) - 1
        if root:
            self.doc["scenes"][0]["nodes"].append(index)
        return index

    def bytes(self) -> bytes:
        while len(self.blob) % 4:
            self.blob.append(0)
        self.doc["buffers"][0]["byteLength"] = len(self.blob)
        text = json.dumps(self.doc, separators=(",", ":")).encode("utf-8")
        text += b" " * ((4 - len(text) % 4) % 4)
        body = (struct.pack("<I4s", len(text), b"JSON") + text + struct.pack("<I4s", len(self.blob), b"BIN\x00") +
                bytes(self.blob))
        return struct.pack("<4sII", b"glTF", 2, 12 + len(body)) + body
