"""Shared conformance contract for standard archive-local glTF buffer source."""

from __future__ import annotations

import base64
from dataclasses import dataclass
import json
import math
import re
import struct
import zipfile
from urllib.parse import unquote_to_bytes


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate glTF JSON key: {key}")
        result[key] = value
    return result


def parse_document(data: bytes) -> dict:
    def reject_constant(value):
        raise ValueError(f"invalid JSON number: {value}")

    if not data or len(data) > 0x7fffffff:
        raise ValueError("glTF JSON document size is invalid")
    try:
        doc = json.loads(data.decode("utf-8"), object_pairs_hook=_unique_object,
                         parse_constant=reject_constant)
    except RecursionError as exc:
        raise ValueError("glTF JSON nesting is too deep") from exc
    if not isinstance(doc, dict):
        raise ValueError("glTF document must be an object")

    def check(value, depth=0):
        if depth > 64:
            raise ValueError("glTF JSON nesting is too deep")
        if isinstance(value, str):
            if "\x00" in value:
                raise ValueError("glTF JSON strings cannot contain NUL")
            value.encode("utf-8")  # Reject unpaired JSON Unicode surrogates.
        elif isinstance(value, float) and not math.isfinite(value):
            raise ValueError("nonfinite glTF JSON number")
        elif isinstance(value, dict):
            for key, child in value.items():
                check(key, depth + 1)
                check(child, depth + 1)
        elif isinstance(value, list):
            for child in value:
                check(child, depth + 1)

    check(doc)
    return doc


def _integer(value, minimum=0, maximum=0x7fffffff):
    if (type(value) not in (int, float) or (isinstance(value, float) and not math.isfinite(value))
            or value != int(value) or not minimum <= value <= maximum):
        raise ValueError("glTF buffer field is not a representable integer")
    return int(value)


def buffer_declaration(doc: dict, glb=False) -> tuple[str | None, int]:
    asset = doc.get("asset")
    if not isinstance(asset, dict) or asset.get("version") != "2.0":
        raise ValueError("glTF asset.version must be 2.0")
    buffers = doc.get("buffers")
    if not isinstance(buffers, list) or len(buffers) != 1:
        raise ValueError("exactly one glTF buffer is currently supported")
    buffer = buffers[0]
    if not isinstance(buffer, dict):
        raise ValueError("glTF buffer must be an object")
    length = _integer(buffer.get("byteLength"), 1)
    uri = buffer.get("uri")
    if glb:
        if "uri" in buffer:
            raise ValueError("GLB binary buffer must not declare a URI")
    elif not isinstance(uri, str) or not uri or "\x00" in uri:
        raise ValueError("text glTF buffer requires a URI")
    views = doc.get("bufferViews")
    if not isinstance(views, list) or not views:
        raise ValueError("glTF buffer requires nonempty bufferViews")
    for view in views:
        if not isinstance(view, dict) or _integer(view.get("buffer")) != 0:
            raise ValueError("glTF bufferView must reference buffer 0")
        offset = _integer(view.get("byteOffset", 0))
        size = _integer(view.get("byteLength"), 1)
        if offset + size > length:
            raise ValueError("glTF bufferView exceeds declared buffer")
        if "byteStride" in view and (_integer(view["byteStride"], 4, 252) % 4):
            raise ValueError("glTF byteStride must be a multiple of four")
        if "target" in view and _integer(view["target"]) not in (34962, 34963):
            raise ValueError("invalid glTF bufferView target")
    return uri, length


def resolve_buffer_member(source_member: str, uri: str) -> str:
    """Match modAssetGltfBufferPath: decode once, stay below source directory."""
    if not isinstance(uri, str) or not uri:
        raise ValueError("empty glTF buffer URI")
    if re.search(r"%(?![0-9a-fA-F]{2})", uri) or re.search(r"%(?:2f|5c)", uri, re.I):
        raise ValueError("malformed or separator-encoded glTF URI")
    value = unquote_to_bytes(uri).decode("utf-8")
    if any(ord(c) <= 31 or ord(c) == 127 or c in ':\\?#$*|<>"%' for c in value):
        raise ValueError("unsafe glTF buffer URI")
    segments = value.split("/")
    if any(not s or s in (".", "..") or s.endswith((".", " ")) for s in segments):
        raise ValueError("unsafe glTF buffer path segment")
    if any(re.fullmatch(r"(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?", s, re.I)
           for s in segments):
        raise ValueError("Windows device name in glTF buffer path")
    prefix = source_member.rsplit("/", 1)[0] + "/" if "/" in source_member else ""
    result = prefix + value
    if len(result.encode("utf-8")) >= 1024:
        raise ValueError("glTF buffer path exceeds provider capacity")
    return result


def load_buffer(doc: dict, source_member: str, archive=None) -> bytes:
    uri, declared = buffer_declaration(doc)
    if uri.startswith("data:"):
        header, separator, payload = uri.partition(",")
        if not separator or header not in (
                "data:application/octet-stream;base64",
                "data:application/gltf-buffer;base64"):
            raise ValueError("unsupported glTF data URI")
        data = base64.b64decode(payload, validate=True)
        if base64.b64encode(data).decode("ascii") != payload:
            raise ValueError("glTF data URI has noncanonical base64 padding")
    else:
        if archive is None:
            raise ValueError("glTF local buffer requires its source archive")
        member = resolve_buffer_member(source_member, uri)
        data = archive.read(member)
    if len(data) != declared:
        raise ValueError("glTF buffer size differs from declared byteLength")
    return data


def _file_reference_key(key: str, value: str) -> bool:
    """Match archive admission's decoded JSON file-reference classification."""
    lowered = value.lower()
    if not value or lowered.startswith(("data:", "http:", "https:")):
        return False
    if not any(c in value for c in (".", "/", "\\")):
        return False
    key = key.lower()
    if (key in ("catalog_id", "asset_id", "canonical_sha256", "schema", "type")
            or key.endswith(("_id", "_sha256"))):
        return False
    return key in ("uri", "archive", "entry", "file", "path", "data") or key.endswith(
        ("_file", "_path", "_graph", "_payloads", "_archive", "_uri"))


def _validate_archive_references(doc: dict, source_member: str, archive) -> None:
    """Only buffers[0].uri may reference raw binary source, even if also declared."""
    def visit(value, path=()):
        if isinstance(value, dict):
            for key, child in value.items():
                child_path = path + (key,)
                if key == "uri" and (not isinstance(child, str) or not child):
                    raise ValueError("glTF URI must be a nonempty string")
                if isinstance(child, str):
                    if child_path == ("buffers", 0, "uri"):
                        continue  # Declaration and exact buffer bytes are validated separately.
                    if key != "uri" and not _file_reference_key(key, child):
                        continue
                    if child.lower().startswith("data:"):
                        continue
                    member = resolve_buffer_member(source_member, child)
                    if re.search(r"\.bin(?=$|[./\\])", member, re.I):
                        raise ValueError(f"forbidden non-buffer glTF file reference: {child}")
                    archive.getinfo(member)  # Non-buffer references must also name present source.
                else:
                    visit(child, child_path)
        elif isinstance(value, list):
            for index, child in enumerate(value):
                visit(child, path + (index,))

    visit(doc)


def declared_archive_buffers(archive, errors: list[str], label: str) -> set[str]:
    """Only structurally declared, present, exact-size source buffers get an exception."""
    members: set[str] = set()
    for source in archive.namelist():
        if not source.lower().endswith(".gltf") or source.startswith("_meta/"):
            continue
        try:
            doc = parse_document(archive.read(source))
            asset = doc.get("asset")
            if not isinstance(asset, dict) or asset.get("version") != "2.0":
                raise ValueError("glTF asset.version must be 2.0")
            # Native animation extras may deliberately have no binary source.
            if "buffers" not in doc:
                if "bufferViews" in doc and doc["bufferViews"] != []:
                    raise ValueError("glTF bufferViews have no buffer")
                _validate_archive_references(doc, source, archive)
                continue
            uri, declared = buffer_declaration(doc)
            _validate_archive_references(doc, source, archive)
            if uri.startswith("data:"):
                load_buffer(doc, source, archive)
                continue
            member = resolve_buffer_member(source, uri)
            load_buffer(doc, source, archive)  # Verify actual decompressed bytes and ZIP integrity.
            members.add(member)
        except (ValueError, KeyError, UnicodeError, OverflowError, zipfile.BadZipFile) as exc:
            errors.append(f"{label} {source} has invalid glTF buffer source: {exc}")
    return members


@dataclass(frozen=True)
class SceneInstance:
    mesh_index: int
    node_index: int
    world: tuple[float, ...]
    mirrored: bool


@dataclass(frozen=True)
class ScenePlan:
    instances: tuple[SceneInstance, ...]
    mesh_count: int
    node_count: int
    selected_scene: int


def _identity_matrix():
    return [1.0 if i % 5 == 0 else 0.0 for i in range(16)]


def _scene_array(obj, key):
    if key not in obj:
        return None
    value = obj[key]
    if not isinstance(value, list) or len(value) > 0x7fffffff:
        raise ValueError(f"glTF {key} must be an array")
    return value


def _number_array(value, count):
    if not isinstance(value, list) or len(value) != count:
        raise ValueError("glTF transform array length is invalid")
    out = []
    for item in value:
        if type(item) not in (int, float):
            raise ValueError("glTF transform components must be finite numbers")
        try:
            item = float(item)
        except OverflowError as exc:
            raise ValueError("glTF transform component overflow") from exc
        if not math.isfinite(item):
            raise ValueError("glTF transform components must be finite numbers")
        out.append(item)
    return out


def _node_matrix(node):
    if "matrix" in node:
        if any(key in node for key in ("translation", "rotation", "scale")):
            raise ValueError("glTF node cannot combine matrix and TRS")
        matrix = _number_array(node["matrix"], 16)
        if (matrix[3], matrix[7], matrix[11], matrix[15]) != (0, 0, 0, 1):
            raise ValueError("glTF node matrix must be affine")
        return matrix
    translation = _number_array(node.get("translation", [0, 0, 0]), 3)
    scale = _number_array(node.get("scale", [1, 1, 1]), 3)
    rotation = _number_array(node.get("rotation", [0, 0, 0, 1]), 4)
    norm = 0.0
    for component in rotation:
        norm += component * component
    if not math.isfinite(norm) or abs(norm - 1) > 1e-4:
        raise ValueError("glTF node quaternion must have unit length")
    inverse = 1 / math.sqrt(norm)
    x, y, z, w = (component * inverse for component in rotation)
    out = _identity_matrix()
    out[0] = (1 - 2 * (y * y + z * z)) * scale[0]
    out[1] = (2 * (x * y + z * w)) * scale[0]
    out[2] = (2 * (x * z - y * w)) * scale[0]
    out[4] = (2 * (x * y - z * w)) * scale[1]
    out[5] = (1 - 2 * (x * x + z * z)) * scale[1]
    out[6] = (2 * (y * z + x * w)) * scale[1]
    out[8] = (2 * (x * z + y * w)) * scale[2]
    out[9] = (2 * (y * z - x * w)) * scale[2]
    out[10] = (1 - 2 * (x * x + y * y)) * scale[2]
    out[12:15] = translation
    if not all(math.isfinite(value) for value in out):
        raise ValueError("glTF node transform overflow")
    return out


def _matrix_multiply(parent, local):
    out = [0.0] * 16
    for column in range(4):
        for row in range(4):
            value = 0.0
            for k in range(4):
                value += parent[k * 4 + row] * local[column * 4 + k]
            if not math.isfinite(value):
                raise ValueError("glTF node world transform overflow")
            out[column * 4 + row] = value
    return out


def _scene_instance(mesh, node, world):
    linear = []
    for column in range(3):
        scale = max(abs(world[column * 4 + row]) for row in range(3))
        if scale == 0:
            return SceneInstance(mesh, node, tuple(world), False)
        linear.extend(world[column * 4 + row] / scale for row in range(3))
    determinant = (linear[0] * (linear[4] * linear[8] - linear[7] * linear[5]) -
                   linear[3] * (linear[1] * linear[8] - linear[7] * linear[2]) +
                   linear[6] * (linear[1] * linear[5] - linear[4] * linear[2]))
    return SceneInstance(mesh, node, tuple(world), determinant < 0)


def scene_plan(doc: dict) -> ScenePlan:
    """Match modAssetGltfScenePlanBuild: selected static instances, source units."""
    if not isinstance(doc, dict) or not isinstance(doc.get("asset"), dict) or doc["asset"].get("version") != "2.0":
        raise ValueError("glTF asset.version must be 2.0")
    meshes = _scene_array(doc, "meshes")
    nodes = _scene_array(doc, "nodes")
    scenes = _scene_array(doc, "scenes")
    mesh_count, node_count = len(meshes or []), len(nodes or [])
    if any(not isinstance(mesh, dict) for mesh in meshes or []):
        raise ValueError("glTF meshes must contain objects")
    parents = [-1] * node_count
    children = [[] for _ in range(node_count)]
    mesh_indices = [-1] * node_count
    local = []

    def reference(value, count):
        return _integer(value, 0, count - 1)

    for i, node in enumerate(nodes or []):
        if not isinstance(node, dict):
            raise ValueError("glTF nodes must contain objects")
        local.append(_node_matrix(node))
        if "mesh" in node:
            mesh_indices[i] = reference(node["mesh"], mesh_count)
        for child in _scene_array(node, "children") or []:
            child = reference(child, node_count)
            if parents[child] >= 0:
                raise ValueError("glTF node has duplicate child or multiple parents")
            parents[child] = i
            children[i].append(child)
    roots = [i for i in range(node_count) if parents[i] < 0]
    queue = roots.copy()
    for index in queue:
        queue.extend(children[index])
    if len(queue) != node_count:
        raise ValueError("glTF node cycle")

    selected = -1
    selected_index = 0
    if "scene" in doc:
        selected_index = reference(doc["scene"], len(scenes or []))
    if scenes:
        selected = selected_index
    selected_roots = []
    if scenes is not None:
        root_scene = [-1] * node_count
        for i, scene in enumerate(scenes):
            if not isinstance(scene, dict):
                raise ValueError("glTF scenes must contain objects")
            for index in _scene_array(scene, "nodes") or []:
                index = reference(index, node_count)
                if parents[index] >= 0 or root_scene[index] == i:
                    raise ValueError("glTF scene roots must be unique hierarchy roots")
                root_scene[index] = i
                if selected == i:
                    selected_roots.append(index)
    else:
        selected_roots = roots
    instances = []
    if scenes is None and nodes is None:
        instances = [_scene_instance(i, -1, _identity_matrix()) for i in range(mesh_count)]
    else:
        world = [None] * node_count
        pending = list(reversed(selected_roots))
        while pending:
            index = pending.pop()
            parent = world[parents[index]] if parents[index] >= 0 else _identity_matrix()
            world[index] = _matrix_multiply(parent, local[index])
            if mesh_indices[index] >= 0:
                instances.append(_scene_instance(mesh_indices[index], index, world[index]))
            pending.extend(reversed(children[index]))
    return ScenePlan(tuple(instances), mesh_count, node_count, selected)


def transform_position(instance: SceneInstance, source) -> tuple[float, float, float]:
    """Binary32 source and result with ordered binary64 intermediate arithmetic."""
    def f32(value):
        maximum = float.fromhex("0x1.fffffep+127")
        if not math.isfinite(value) or value < -maximum or value > maximum:
            raise ValueError("glTF position exceeds finite float runtime domain")
        try:
            value = struct.unpack("<f", struct.pack("<f", value))[0]
        except (OverflowError, struct.error) as exc:
            raise ValueError("glTF position exceeds float runtime domain") from exc
        if not math.isfinite(value):
            raise ValueError("glTF position must be finite")
        return value

    if len(source) != 3:
        raise ValueError("glTF position must have three components")
    source = [f32(value) for value in source]
    out = []
    for row in range(3):
        value = instance.world[12 + row]
        for k in range(3):
            value += instance.world[k * 4 + row] * source[k]
        out.append(f32(value))
    return tuple(out)
