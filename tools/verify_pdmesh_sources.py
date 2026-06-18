#!/usr/bin/env python3
"""Validate public .pdmesh source geometry without launching the game."""

from __future__ import annotations

import argparse
import base64
import binascii
import json
import math
import struct
import sys
import zipfile
from collections import Counter
from collections.abc import Iterable
from dataclasses import dataclass, field
from pathlib import Path


DIRECT_MODEL_SOURCES = ("model.obj", "model.gltf", "model.glb")
GLTF_COMPONENT_SIZES = {
    5120: 1,
    5121: 1,
    5122: 2,
    5123: 2,
    5125: 4,
    5126: 4,
}
GLTF_TYPE_COUNTS = {
    "SCALAR": 1,
    "VEC2": 2,
    "VEC3": 3,
    "VEC4": 4,
}


@dataclass
class MeshStats:
    archives: int = 0
    zipped: int = 0
    directories: int = 0
    obj: int = 0
    gltf: int = 0
    glb: int = 0
    vertices: int = 0
    texcoords: int = 0
    triangles: int = 0
    zero_triangle_archives: int = 0
    quantized_triangle_collapse: int = 0
    hierarchy_archives: int = 0
    nodes: int = 0
    parts: int = 0
    faces: int = 0
    render_commands: int = 0
    matrix_commands: int = 0
    materials: int = 0
    formats: Counter[str] = field(default_factory=Counter)


@dataclass(frozen=True)
class HierarchyInfo:
    node_count: int
    part_count: int
    face_count: int
    render_command_count: int
    matrix_command_count: int


class Archive:
    def __init__(self, path: Path) -> None:
        self.path = path
        self._zip: zipfile.ZipFile | None = None
        self._names: list[str] | None = None

    def __enter__(self) -> "Archive":
        if self.path.is_file():
            self._zip = zipfile.ZipFile(self.path)
        return self

    def __exit__(self, *_args: object) -> None:
        if self._zip is not None:
            self._zip.close()

    @property
    def is_dir(self) -> bool:
        return self.path.is_dir()

    def names(self) -> list[str]:
        if self._names is not None:
            return self._names
        if self._zip is not None:
            self._names = [n.replace("\\", "/") for n in self._zip.namelist()]
            return self._names
        root = self.path
        self._names = [
            p.relative_to(root).as_posix()
            for p in sorted(root.rglob("*"))
            if p.is_file()
        ]
        return self._names

    def read(self, name: str) -> bytes:
        if self._zip is not None:
            return self._zip.read(name)
        return (self.path / name).read_bytes()


def parse_ini(text: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("[") or line.startswith("#") or line.startswith(";"):
            continue
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip()
    return values


def int_field(values: dict[str, object] | dict[str, str], key: str) -> int | None:
    raw = values.get(key)
    if raw is None or isinstance(raw, bool):
        return None
    try:
        return int(raw)
    except (TypeError, ValueError):
        return None


def quantize_s16(value: float) -> int | None:
    if not math.isfinite(value):
        return None
    rounded = math.floor(value + 0.5) if value >= 0.0 else math.ceil(value - 0.5)
    if rounded < -32768 or rounded > 32767:
        return None
    return int(rounded)


def resolve_obj_index(raw: int, count: int) -> int | None:
    if raw == 0 or count <= 0:
        return None
    index = raw - 1 if raw > 0 else count + raw
    if index < 0 or index >= count:
        return None
    return index


def triangle_collapsed(a: tuple[int, int, int],
                       b: tuple[int, int, int],
                       c: tuple[int, int, int]) -> bool:
    ab = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
    ac = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
    cross = (
        ab[1] * ac[2] - ab[2] * ac[1],
        ab[2] * ac[0] - ab[0] * ac[2],
        ab[0] * ac[1] - ab[1] * ac[0],
    )
    return a == b or a == c or b == c or cross == (0, 0, 0)


def read_json(archive: Archive, name: str, label: str,
              errors: list[str]) -> dict[str, object]:
    try:
        value = json.loads(archive.read(name).decode("utf-8"))
    except KeyError:
        errors.append(f"{label} is missing {name}")
        return {}
    except (json.JSONDecodeError, UnicodeDecodeError) as exc:
        errors.append(f"{label} {name} is invalid JSON: {exc}")
        return {}
    if not isinstance(value, dict):
        errors.append(f"{label} {name} must be a JSON object")
        return {}
    return value


def parse_obj_info(label: str, archive: Archive, source: str,
                   stats: MeshStats, errors: list[str]) -> int:
    vertices: list[tuple[int, int, int]] = []
    texcoords: list[tuple[int, int]] = []
    triangles = 0
    collapsed = 0
    materials: set[str] = set()
    try:
        text = archive.read(source).decode("utf-8", errors="replace")
    except KeyError:
        errors.append(f"{label} is missing {source}")
        return 0

    for line_no, line in enumerate(text.splitlines(), start=1):
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        parts = stripped.split()
        if parts[0] == "v":
            if len(parts) < 4:
                errors.append(f"{label} {source} line {line_no} has incomplete vertex")
                return triangles
            try:
                coords = [float(parts[1]), float(parts[2]), float(parts[3])]
            except ValueError:
                errors.append(f"{label} {source} line {line_no} has non-numeric vertex")
                return triangles
            q = [quantize_s16(value) for value in coords]
            if any(value is None for value in q):
                errors.append(
                    f"{label} {source} line {line_no} vertex cannot quantize to "
                    "integer_native_boundary s16 coordinates"
                )
                return triangles
            vertices.append((q[0], q[1], q[2]))  # type: ignore[arg-type]
        elif parts[0] == "vt":
            if len(parts) < 3:
                errors.append(f"{label} {source} line {line_no} has incomplete texcoord")
                return triangles
            try:
                u = float(parts[1])
                v = float(parts[2])
            except ValueError:
                errors.append(f"{label} {source} line {line_no} has non-numeric texcoord")
                return triangles
            qs = quantize_s16(u * 32.0)
            qt = quantize_s16((1.0 - v) * 32.0)
            if qs is None or qt is None:
                errors.append(
                    f"{label} {source} line {line_no} texcoord cannot quantize to "
                    "integer_native_boundary s16 UV units"
                )
                return triangles
            texcoords.append((qs, qt))
        elif parts[0] == "usemtl" and len(parts) >= 2:
            materials.add(parts[1])
        elif parts[0] == "f":
            if len(parts) < 4:
                errors.append(f"{label} {source} line {line_no} face needs at least three vertices")
                return triangles
            face_indices: list[int] = []
            for token in parts[1:]:
                raw_vertex = token.split("/", 1)[0]
                try:
                    resolved = resolve_obj_index(int(raw_vertex), len(vertices))
                except ValueError:
                    resolved = None
                if resolved is None:
                    errors.append(f"{label} {source} line {line_no} has invalid face index")
                    return triangles
                face_indices.append(resolved)
            for index in range(2, len(face_indices)):
                a = vertices[face_indices[0]]
                b = vertices[face_indices[index - 1]]
                c = vertices[face_indices[index]]
                triangles += 1
                if triangle_collapsed(a, b, c):
                    collapsed += 1

    if not vertices:
        errors.append(f"{label} {source} has no vertices")
    stats.obj += 1
    stats.vertices += len(vertices)
    stats.texcoords += len(texcoords)
    stats.triangles += triangles
    stats.quantized_triangle_collapse += collapsed
    stats.materials += len(materials)
    if triangles == 0:
        stats.zero_triangle_archives += 1
    return triangles


def gltf_json_and_bin(data: bytes) -> tuple[dict[str, object], bytes]:
    if len(data) >= 12 and data[:4] == b"glTF":
        version, total_length = struct.unpack_from("<II", data, 4)
        if version != 2 or total_length > len(data):
            raise ValueError("invalid GLB header")
        offset = 12
        gltf_json: dict[str, object] | None = None
        bin_chunk = b""
        while offset + 8 <= total_length:
            chunk_length, chunk_type = struct.unpack_from("<II", data, offset)
            offset += 8
            chunk = data[offset:offset + chunk_length]
            offset += (chunk_length + 3) & ~3
            if chunk_type == 0x4E4F534A:
                value = json.loads(chunk.decode("utf-8").rstrip("\0 "))
                if not isinstance(value, dict):
                    raise ValueError("glTF JSON chunk is not an object")
                gltf_json = value
            elif chunk_type == 0x004E4942:
                bin_chunk = chunk
        if gltf_json is None:
            raise ValueError("missing GLB JSON chunk")
        return gltf_json, bin_chunk

    gltf = json.loads(data.decode("utf-8"))
    if not isinstance(gltf, dict):
        raise ValueError("glTF root is not an object")
    buffers = gltf.get("buffers")
    if not isinstance(buffers, list) or not buffers:
        raise ValueError("missing embedded glTF buffer")
    first = buffers[0]
    if not isinstance(first, dict):
        raise ValueError("glTF buffer 0 is not an object")
    uri = first.get("uri")
    if not isinstance(uri, str) or not uri.startswith("data:") or "," not in uri:
        raise ValueError("glTF external binary buffers are not allowed")
    return gltf, base64.b64decode(uri.split(",", 1)[1], validate=True)


def accessor_view(gltf: dict[str, object], bin_chunk: bytes, accessor_index: int,
                  expected_component: int | None = None,
                  expected_type: str | None = None
                  ) -> tuple[dict[str, object], bytes, int, int]:
    accessors = gltf.get("accessors")
    views = gltf.get("bufferViews")
    if not isinstance(accessors, list) or not isinstance(views, list):
        raise ValueError("missing glTF accessors or bufferViews")
    if accessor_index < 0 or accessor_index >= len(accessors):
        raise ValueError("accessor out of range")
    accessor = accessors[accessor_index]
    if not isinstance(accessor, dict):
        raise ValueError("accessor is not an object")
    component = accessor.get("componentType")
    accessor_type = accessor.get("type")
    if expected_component is not None and component != expected_component:
        raise ValueError(f"accessor must use componentType {expected_component}")
    if expected_type is not None and accessor_type != expected_type:
        raise ValueError(f"accessor must be {expected_type}")
    if component not in GLTF_COMPONENT_SIZES or accessor_type not in GLTF_TYPE_COUNTS:
        raise ValueError("unsupported accessor component/type")
    view_index = accessor.get("bufferView")
    if not isinstance(view_index, int) or view_index < 0 or view_index >= len(views):
        raise ValueError("invalid accessor bufferView")
    view = views[view_index]
    if not isinstance(view, dict):
        raise ValueError("bufferView is not an object")
    if view.get("buffer", 0) != 0:
        raise ValueError("only buffer 0 is supported")
    component_size = GLTF_COMPONENT_SIZES[component]  # type: ignore[index]
    component_count = GLTF_TYPE_COUNTS[accessor_type]  # type: ignore[index]
    element_size = component_size * component_count
    stride = view.get("byteStride", element_size)
    if not isinstance(stride, int) or stride < element_size:
        raise ValueError("invalid bufferView stride")
    count = accessor.get("count")
    if not isinstance(count, int) or count < 0:
        raise ValueError("invalid accessor count")
    view_offset = int(view.get("byteOffset", 0))
    view_length = int(view.get("byteLength", len(bin_chunk) - view_offset))
    accessor_offset = int(accessor.get("byteOffset", 0))
    if min(view_offset, view_length, accessor_offset) < 0:
        raise ValueError("negative glTF buffer offset")
    if view_offset + view_length > len(bin_chunk):
        raise ValueError("bufferView overruns BIN chunk")
    if count > 0 and accessor_offset + (count - 1) * stride + element_size > view_length:
        raise ValueError("accessor overruns bufferView")
    start = view_offset + accessor_offset
    return accessor, bin_chunk[start:view_offset + view_length], stride, element_size


def accessor_float_values(gltf: dict[str, object], bin_chunk: bytes,
                          accessor_index: int, expected_type: str
                          ) -> list[tuple[float, ...]]:
    accessor, data, stride, element_size = accessor_view(
        gltf, bin_chunk, accessor_index, 5126, expected_type
    )
    count = int(accessor.get("count", 0))
    component_count = GLTF_TYPE_COUNTS[expected_type]
    values: list[tuple[float, ...]] = []
    for index in range(count):
        offset = index * stride
        if offset + element_size > len(data):
            raise ValueError("accessor read exceeds buffer")
        values.append(struct.unpack_from("<" + "f" * component_count, data, offset))
    return values


def gltf_index_at(data: bytes, offset: int, component: int) -> int:
    if component == 5121:
        return data[offset]
    if component == 5123:
        return struct.unpack_from("<H", data, offset)[0]
    if component == 5125:
        return struct.unpack_from("<I", data, offset)[0]
    raise ValueError("indices must be unsigned scalar")


def parse_gltf_info(label: str, archive: Archive, source: str,
                    stats: MeshStats, errors: list[str]) -> int:
    try:
        gltf, bin_chunk = gltf_json_and_bin(archive.read(source))
    except (KeyError, ValueError, json.JSONDecodeError, UnicodeDecodeError,
            binascii.Error, struct.error) as exc:
        errors.append(f"{label} {source} is invalid glTF/GLB mesh source: {exc}")
        return 0

    meshes = gltf.get("meshes")
    if not isinstance(meshes, list) or not meshes:
        errors.append(f"{label} {source} must contain at least one mesh")
        return 0
    triangles = 0
    collapsed = 0
    material_indices: set[int] = set()

    for mesh_index, mesh in enumerate(meshes):
        if not isinstance(mesh, dict):
            errors.append(f"{label} {source} mesh {mesh_index} must be an object")
            continue
        primitives = mesh.get("primitives")
        if not isinstance(primitives, list):
            errors.append(f"{label} {source} mesh {mesh_index} missing primitives")
            continue
        for primitive_index, primitive in enumerate(primitives):
            if not isinstance(primitive, dict):
                errors.append(f"{label} {source} primitive {primitive_index} must be an object")
                continue
            if primitive.get("mode", 4) != 4:
                errors.append(f"{label} {source} primitive {primitive_index} must use triangle mode")
                continue
            attributes = primitive.get("attributes")
            if not isinstance(attributes, dict) or not isinstance(attributes.get("POSITION"), int):
                errors.append(f"{label} {source} primitive {primitive_index} missing POSITION")
                continue
            if isinstance(primitive.get("material"), int):
                material_indices.add(int(primitive["material"]))
            try:
                positions = accessor_float_values(
                    gltf, bin_chunk, int(attributes["POSITION"]), "VEC3"
                )
                qpos = []
                for vertex_index, coords in enumerate(positions):
                    q = [quantize_s16(value) for value in coords[:3]]
                    if any(value is None for value in q):
                        errors.append(
                            f"{label} {source} POSITION vertex {vertex_index} cannot "
                            "quantize to integer_native_boundary s16 coordinates"
                        )
                        return triangles
                    qpos.append((q[0], q[1], q[2]))  # type: ignore[arg-type]
                for texcoord_name in ("TEXCOORD_0", "TEXCOORD_1"):
                    tex_accessor = attributes.get(texcoord_name)
                    if not isinstance(tex_accessor, int):
                        continue
                    for uv_index, (u, v) in enumerate(
                            accessor_float_values(gltf, bin_chunk, tex_accessor, "VEC2")):
                        if quantize_s16(u * 32.0) is None or quantize_s16((1.0 - v) * 32.0) is None:
                            errors.append(
                                f"{label} {source} {texcoord_name} vertex {uv_index} "
                                "cannot quantize to integer_native_boundary s16 UV units"
                            )
                            return triangles
            except (ValueError, struct.error) as exc:
                errors.append(f"{label} {source} primitive {primitive_index} has invalid attributes: {exc}")
                continue

            face_indices: list[int] = []
            indices = primitive.get("indices")
            if isinstance(indices, int):
                try:
                    accessor, data, stride, _ = accessor_view(gltf, bin_chunk, indices, None, "SCALAR")
                    component = int(accessor.get("componentType", 0))
                    if component not in {5121, 5123, 5125}:
                        raise ValueError("indices must be unsigned scalar")
                    count = int(accessor.get("count", 0))
                    if count % 3 != 0:
                        raise ValueError("index count is not a multiple of three")
                    for index in range(count):
                        value = gltf_index_at(data, index * stride, component)
                        if value < 0 or value >= len(qpos):
                            raise ValueError("index value out of range")
                        face_indices.append(value)
                except (ValueError, struct.error) as exc:
                    errors.append(f"{label} {source} primitive {primitive_index} has invalid indices: {exc}")
                    continue
            else:
                if len(qpos) % 3 != 0:
                    errors.append(f"{label} {source} primitive {primitive_index} unindexed vertices are not triangles")
                    continue
                face_indices = list(range(len(qpos)))

            for index in range(0, len(face_indices), 3):
                a = qpos[face_indices[index]]
                b = qpos[face_indices[index + 1]]
                c = qpos[face_indices[index + 2]]
                triangles += 1
                if triangle_collapsed(a, b, c):
                    collapsed += 1
            stats.vertices += len(qpos)
            if isinstance(attributes.get("TEXCOORD_0"), int):
                stats.texcoords += len(qpos)

    if source.endswith(".glb"):
        stats.glb += 1
    else:
        stats.gltf += 1
    stats.triangles += triangles
    stats.quantized_triangle_collapse += collapsed
    stats.materials += len(material_indices)
    if triangles == 0:
        stats.zero_triangle_archives += 1
    return triangles


def validate_hierarchy(label: str, archive: Archive, names: set[str],
                       stats: MeshStats, errors: list[str]) -> HierarchyInfo | None:
    if "model.nodes.json" not in names:
        return None
    for member in ("model.parts.json", "model.faces.json", "model.render.json"):
        if member not in names:
            errors.append(f"{label} has model.nodes.json but missing required {member}")
            return None
    nodes_root = read_json(archive, "model.nodes.json", label, errors)
    parts_root = read_json(archive, "model.parts.json", label, errors)
    faces_root = read_json(archive, "model.faces.json", label, errors)
    render_root = read_json(archive, "model.render.json", label, errors)
    nodes = nodes_root.get("nodes")
    parts = parts_root.get("parts")
    faces = faces_root.get("faces")
    commands = render_root.get("commands")
    if not isinstance(nodes, list) or not nodes:
        errors.append(f"{label} model.nodes.json must contain a non-empty nodes array")
        return None
    if not isinstance(parts, list):
        errors.append(f"{label} model.parts.json must contain a parts array")
        return None
    if not isinstance(faces, list):
        errors.append(f"{label} model.faces.json must contain a faces array")
        return None
    if not isinstance(commands, list) or not commands:
        errors.append(f"{label} model.render.json must contain a non-empty commands array")
        return None

    for index, node in enumerate(nodes):
        if not isinstance(node, dict):
            errors.append(f"{label} model.nodes.json node {index} is not an object")
            return None
        if node.get("id") != index:
            errors.append(f"{label} model.nodes.json node ids must be contiguous")
            return None
        parent = node.get("parent")
        if not isinstance(parent, int) or parent >= len(nodes):
            errors.append(f"{label} model.nodes.json node {index} has invalid parent {parent!r}")
            return None
    for index, part in enumerate(parts):
        if not isinstance(part, dict):
            errors.append(f"{label} model.parts.json part {index} is not an object")
            return None
        node = part.get("node")
        if node == -1 and part.get("node_unresolved") is True:
            continue
        if not isinstance(node, int) or node < 0 or node >= len(nodes):
            errors.append(f"{label} model.parts.json part {index} has invalid node {node!r}")
            return None

    seen_faces: set[int] = set()
    for index, face in enumerate(faces):
        if not isinstance(face, dict):
            errors.append(f"{label} model.faces.json face {index} is not an object")
            return None
        face_index = face.get("face_index")
        matrix_index = face.get("matrix_index")
        if not isinstance(face_index, int) or face_index < 0 or face_index >= len(faces):
            errors.append(f"{label} model.faces.json row {index} has invalid face_index {face_index!r}")
            return None
        if face_index in seen_faces:
            errors.append(f"{label} model.faces.json duplicates face_index {face_index}")
            return None
        seen_faces.add(face_index)
        if not isinstance(matrix_index, int) or matrix_index < 0:
            errors.append(f"{label} model.faces.json row {index} has invalid matrix_index {matrix_index!r}")
            return None

    tri_faces: list[int] = []
    matrix_commands = 0
    for index, command in enumerate(commands):
        if not isinstance(command, dict):
            errors.append(f"{label} model.render.json command {index} is not an object")
            return None
        name = command.get("command")
        if name not in {"mtx", "pop", "material", "tri"}:
            errors.append(f"{label} model.render.json command {index} has invalid command {name!r}")
            return None
        if name == "mtx":
            matrix_commands += 1
        if name in {"mtx", "tri"} and "matrix_index" in command:
            matrix_index = command.get("matrix_index")
            if not isinstance(matrix_index, int) or matrix_index < 0:
                errors.append(f"{label} model.render.json command {index} has invalid matrix_index {matrix_index!r}")
                return None
        if name == "tri":
            face_index = command.get("face_index")
            if not isinstance(face_index, int) or face_index < 0 or face_index >= len(faces):
                errors.append(f"{label} model.render.json tri command {index} has invalid face_index {face_index!r}")
                return None
            tri_faces.append(face_index)
    if len(tri_faces) != len(faces) or len(set(tri_faces)) != len(faces):
        errors.append(
            f"{label} model.render.json must reference every model.faces.json face exactly once"
        )
        return None

    stats.hierarchy_archives += 1
    stats.nodes += len(nodes)
    stats.parts += len(parts)
    stats.faces += len(faces)
    stats.render_commands += len(commands)
    stats.matrix_commands += matrix_commands
    return HierarchyInfo(
        node_count=len(nodes),
        part_count=len(parts),
        face_count=len(faces),
        render_command_count=len(commands),
        matrix_command_count=matrix_commands,
    )


def choose_geometry_source(names: set[str], ini: dict[str, str],
                           manifest: dict[str, object]) -> str | None:
    candidates: list[object] = [
        manifest.get("geometry"),
        manifest.get("model_file"),
        ini.get("geometry_file"),
        ini.get("model_file"),
        ini.get("model"),
    ]
    for candidate in candidates:
        if isinstance(candidate, str) and candidate in names and candidate in DIRECT_MODEL_SOURCES:
            return candidate
    for name in DIRECT_MODEL_SOURCES:
        if name in names:
            return name
    return None


def validate_one(path: Path, stats: MeshStats, args: argparse.Namespace,
                 errors: list[str]) -> None:
    label = str(path)
    with Archive(path) as archive:
        names = set(archive.names())
        if archive.is_dir:
            stats.directories += 1
        else:
            stats.zipped += 1
        if "mesh.ini" not in names:
            errors.append(f"{label} is missing mesh.ini")
            return
        ini = parse_ini(archive.read("mesh.ini").decode("utf-8", errors="replace"))
        manifest = read_json(archive, "_meta/manifest.json", label, errors) if "_meta/manifest.json" in names else {}
        source = choose_geometry_source(names, ini, manifest)
        if source is None:
            errors.append(f"{label} must contain and declare one of {DIRECT_MODEL_SOURCES}")
            return
        source_format = str(manifest.get("source_format") or ini.get("source_format") or "").strip()
        if source_format:
            stats.formats[source_format] += 1
        stats.archives += 1
        if source == "model.obj":
            triangles = parse_obj_info(label, archive, source, stats, errors)
        else:
            triangles = parse_gltf_info(label, archive, source, stats, errors)

        has_hierarchy = "model.nodes.json" in names
        if args.require_generated_hierarchy and not has_hierarchy:
            errors.append(f"{label} is missing generated model.nodes.json hierarchy source")
        hierarchy = validate_hierarchy(label, archive, names, stats, errors)

        actual_counts = {
            "triangle_count": triangles,
            "node_count": hierarchy.node_count if hierarchy else None,
            "part_count": hierarchy.part_count if hierarchy else None,
            "face_count": hierarchy.face_count if hierarchy else None,
            "render_command_count": hierarchy.render_command_count if hierarchy else None,
            "matrix_command_count": hierarchy.matrix_command_count if hierarchy else None,
        }
        for key, actual in actual_counts.items():
            declared = int_field(manifest, key)
            if declared is None:
                declared = int_field(ini, key)
            if declared is None:
                continue
            if actual is not None and declared != actual:
                errors.append(f"{label} {key} {declared} does not match decoded {actual}")


def iter_pdmesh_inputs(paths: Iterable[Path]) -> Iterable[Path]:
    seen: set[Path] = set()
    for path in paths:
        if path.is_dir() and path.name.endswith(".pdmesh"):
            if path not in seen:
                seen.add(path)
                yield path
            continue
        if path.is_file() and (path.suffix == ".pdmesh" or path.name.endswith(".pdmesh.zip")):
            if path not in seen:
                seen.add(path)
                yield path
            continue
        if path.is_dir():
            for found in sorted(path.rglob("*")):
                if found.is_dir() and found.name.endswith(".pdmesh"):
                    if found not in seen:
                        seen.add(found)
                        yield found
                elif found.is_file() and (found.suffix == ".pdmesh" or found.name.endswith(".pdmesh.zip")):
                    if found not in seen:
                        seen.add(found)
                        yield found


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Validate .pdmesh source geometry and hierarchy without launching the game."
    )
    parser.add_argument("paths", nargs="+", type=Path)
    parser.add_argument("--require-any", action="store_true",
                        help="fail if no .pdmesh archives are found")
    parser.add_argument("--require-generated-hierarchy", action="store_true",
                        help="require model.nodes.json/model.parts.json/model.faces.json/model.render.json")
    parser.add_argument("--max-errors", type=int, default=20)
    args = parser.parse_args(argv)

    stats = MeshStats()
    errors: list[str] = []
    inputs = list(iter_pdmesh_inputs(args.paths))
    if args.require_any and not inputs:
        errors.append("no .pdmesh archives found")

    for path in inputs:
        try:
            validate_one(path, stats, args, errors)
        except (OSError, zipfile.BadZipFile, UnicodeDecodeError, ValueError) as exc:
            errors.append(f"{path} could not be read as a .pdmesh archive: {exc}")
        if len(errors) >= args.max_errors:
            break

    if errors:
        for error in errors[:args.max_errors]:
            print(f"ERROR: {error}", file=sys.stderr)
        remaining = len(errors) - args.max_errors
        if remaining > 0:
            print(f"ERROR: ... {remaining} more errors", file=sys.stderr)
        return 1

    formats = ",".join(f"{k}={v}" for k, v in sorted(stats.formats.items())) or "none"
    print(
        "pdmesh source contract ok: "
        f"archives={stats.archives} zip={stats.zipped} dirs={stats.directories} "
        f"obj={stats.obj} gltf={stats.gltf} glb={stats.glb} "
        f"vertices={stats.vertices} texcoords={stats.texcoords} triangles={stats.triangles} "
        f"zero_triangle={stats.zero_triangle_archives} "
        f"quantized_triangle_collapse={stats.quantized_triangle_collapse} "
        f"hierarchy={stats.hierarchy_archives} nodes={stats.nodes} parts={stats.parts} "
        f"faces={stats.faces} render_commands={stats.render_commands} "
        f"matrix_commands={stats.matrix_commands} materials={stats.materials} "
        f"formats={formats}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
