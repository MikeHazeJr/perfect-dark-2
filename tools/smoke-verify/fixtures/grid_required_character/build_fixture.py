"""Build the deterministic native Grid character archives from the Triangle example."""

from base64 import b64decode, b64encode
from copy import deepcopy
from io import BytesIO
import json
from pathlib import Path
from struct import pack_into, unpack_from
from zipfile import ZIP_STORED, ZipFile, ZipInfo


FIXTURES = Path(__file__).resolve().parent
SOURCE = FIXTURES.parents[3] / "examples/modding/typed-pdxxx-basic/meshes/tri_mesh.pdmesh"
MANIFEST = "_meta/manifest.json"
POSITION_NODE = 2
HEADSPOT_NODE = 23
HEADSPOT_PART = 4


def json_bytes(value: object) -> bytes:
    return (json.dumps(value, indent=2) + "\n").encode("utf-8")


def archive_bytes(members: dict[str, bytes]) -> bytes:
    result = BytesIO()
    with ZipFile(result, "w", compression=ZIP_STORED) as archive:
        for name, contents in members.items():
            entry = ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            entry.create_system = 0
            archive.writestr(entry, contents)
    return result.getvalue()


def mesh_bytes(identity: str, width: float, height: float, neck: bool,
               colour: tuple[float, float, float]) -> bytes:
    with ZipFile(SOURCE) as archive:
        members = {entry.filename: archive.read(entry) for entry in archive.infolist()}

    descriptor = members["mesh.ini"].decode("utf-8")
    if descriptor.count("[mesh]") != 1 or descriptor.count("catalog_id = example:tri_mesh") != 1:
        raise ValueError("Expected the checked-in Triangle mesh source")
    members["mesh.ini"] = (
        descriptor.replace("[mesh]", "[model]\nkind = mesh")
        .replace("catalog_id = example:tri_mesh", f"catalog_id = {identity}")
        .replace("model_scale = 1000", "model_scale = 1")
    ).encode("utf-8")
    metadata = json.loads(members[MANIFEST])
    metadata["catalog_id"] = identity
    metadata["model_scale"] = 1.0
    members[MANIFEST] = json_bytes(metadata)

    geometry = json.loads(members["model.gltf"])
    buffer = geometry["buffers"][0]
    prefix, data = buffer["uri"].split(",", 1)
    positions = bytearray(b64decode(data, validate=True))
    if unpack_from("<9f", positions) != (0, 0, 0, 1, 0, 0, 0, 1, 0):
        raise ValueError("Unexpected Triangle glTF positions")
    pack_into("<9f", positions, 0, -width, 0, 0, width, 0, 0, 0, height, 0)
    buffer["uri"] = prefix + "," + b64encode(positions).decode("ascii")
    accessor = geometry["accessors"][geometry["meshes"][0]["primitives"][0]["attributes"]["POSITION"]]
    accessor.update(min=[-width, 0, 0], max=[width, height, 0])
    geometry["materials"][0]["pbrMetallicRoughness"]["baseColorFactor"] = [*colour, 1.0]
    members["model.gltf"] = json_bytes(geometry)
    members["model.mtl"] = members["model.mtl"].replace(
        b"Kd 0.2 0.8 1.0", ("Kd " + " ".join(map(str, colour))).encode("ascii")
    )

    hierarchy = json.loads(members["model.nodes.json"])
    nodes = hierarchy["nodes"]
    nodes[0]["bounds"].update(xmin=-width, xmax=width, ymax=height)
    if neck:
        if len(nodes) != 2 or nodes[0]["type"] != POSITION_NODE:
            raise ValueError("Unexpected Triangle hierarchy")
        joint = deepcopy(nodes[0])
        joint.update(id=2, parent=0, partnum=-1, part=-1, mtx0=1)
        joint["position"]["y"] = height + 3
        headspot = deepcopy(joint)
        headspot.update(id=3, parent=2, type=HEADSPOT_NODE,
                        partnum=HEADSPOT_PART, part=HEADSPOT_PART)
        headspot["position"]["y"] = 0
        nodes.extend((joint, headspot))
        parts = json.loads(members["model.parts.json"])
        parts["parts"].append({"partnum": HEADSPOT_PART, "node": 3})
        members["model.parts.json"] = json_bytes(parts)
    members["model.nodes.json"] = json_bytes(hierarchy)
    return archive_bytes(members)


def character_archive(namespace: str, stem: str, kind: str,
                      colour: tuple[float, float, float]) -> bytes:
    identity = f"{namespace}:{stem}_{kind}"
    mesh_id = f"{identity}_mesh"
    is_body = kind == "body"
    descriptor = (
        f"[{kind}]\ncatalog_id = {identity}\ngrid_provenance = original\n"
        "rig_class = human_male_neck_standard\n"
        "type = HEADBODYTYPE_DEFAULT\nismale = 1\nheight = 180\n"
        "scale = 1\nanimscale = 1\nmodel_scale = 1\n"
        f"mesh_catalog_id = {mesh_id}\nmesh_archive = mesh.pdmesh\n"
    )
    members = {
        "mesh.pdmesh": mesh_bytes(mesh_id, 27 if is_body else 11, 65 if is_body else 21,
                                 is_body, colour),
        MANIFEST: json_bytes({"pd_schema_version": 1, "pd_kind": kind, "catalog_id": identity}),
    }
    if is_body:
        hand_id = f"{identity}_hand"
        descriptor += (
            f"display_name = Grid {stem.title()} Body\nunk00_01 = 0\n"
            f"hand_catalog_id = {hand_id}\nhand_archive = hand.pdmesh\n"
        )
        members["hand.pdmesh"] = mesh_bytes(hand_id, 4, 6, False, colour)
    return archive_bytes({f"{kind}.ini": descriptor.encode("utf-8"), **members})


def build() -> None:
    profiles = (
        ("host", "grid_character", "bot", (0.95, 0.48, 0.14), (0.24, 0.85, 0.95)),
        ("guest", "grid_character_seed", "offset", (0.50, 0.60, 0.44), (0.75, 0.66, 0.69)),
    )
    for profile, namespace, stem, body_colour, head_colour in profiles:
        for directory, kind, colour in (("bodies", "body", body_colour), ("heads", "head", head_colour)):
            path = FIXTURES / profile / directory / f"{stem}.pd{kind}"
            path.write_bytes(character_archive(namespace, stem, kind, colour))


if __name__ == "__main__":
    build()
