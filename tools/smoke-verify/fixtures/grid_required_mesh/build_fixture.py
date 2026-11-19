"""Build the original, standalone prop mesh used by the mixed Grid smoke."""

from base64 import b64decode, b64encode
import json
from pathlib import Path
import struct
from zipfile import ZIP_STORED, ZipFile, ZipInfo


FIXTURES = Path(__file__).resolve().parent
SOURCE = FIXTURES.parents[3] / "examples/modding/typed-pdxxx-basic/meshes/tri_mesh.pdmesh"
MODEL_ID = "grid_required_mesh:tri_required"


def json_bytes(value: object) -> bytes:
    return (json.dumps(value, indent=2) + "\n").encode("utf-8")


def build() -> None:
    with ZipFile(SOURCE) as archive:
        members = {entry.filename: archive.read(entry) for entry in archive.infolist()}

    descriptor = members["mesh.ini"].decode("utf-8")
    identity = "catalog_id = example:tri_mesh"
    if descriptor.count(identity) != 1 or "grid_provenance" in descriptor:
        raise ValueError("Expected the original Triangle mesh descriptor")
    members["mesh.ini"] = descriptor.replace(
        identity, f"catalog_id = {MODEL_ID}\nname = Amber Triangle Prop\ngrid_provenance = original"
    ).encode("utf-8")
    manifest = json.loads(members["_meta/manifest.json"])
    manifest["catalog_id"] = MODEL_ID
    members["_meta/manifest.json"] = json_bytes(manifest)

    gltf = json.loads(members["model.gltf"])
    buffer = gltf["buffers"][0]
    prefix, encoded = buffer["uri"].split(",", 1)
    geometry = bytearray(b64decode(encoded, validate=True))
    if struct.unpack_from("<9f", geometry) != (0, 0, 0, 1, 0, 0, 0, 1, 0):
        raise ValueError("Expected the Triangle positions in the public glTF buffer")
    struct.pack_into("<9f", geometry, 0, 0, 0, 0, 32, 0, 0, 0, 24, 0)
    buffer["uri"] = prefix + "," + b64encode(geometry).decode("ascii")
    gltf["materials"][0]["pbrMetallicRoughness"]["baseColorFactor"] = [1.0, 0.63, 0.12, 1.0]
    members["model.gltf"] = json_bytes(gltf)
    nodes = json.loads(members["model.nodes.json"])
    nodes["nodes"][0]["bounds"].update(xmax=32.0, ymax=24.0)
    members["model.nodes.json"] = json_bytes(nodes)
    members["model.mtl"] = members["model.mtl"].replace(b"Kd 0.2 0.8 1.0", b"Kd 1.0 0.63 0.12")

    with ZipFile(FIXTURES / "tri_required.pdmesh", "w", compression=ZIP_STORED) as archive:
        for name, contents in members.items():
            entry = ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            entry.create_system = 0
            archive.writestr(entry, contents)


if __name__ == "__main__":
    build()
