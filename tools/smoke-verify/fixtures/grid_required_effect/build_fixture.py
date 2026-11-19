"""Generate the Grid smoke's public Triangle projectile and effect archives."""

import json
from io import BytesIO
from pathlib import Path
from zipfile import ZIP_STORED, ZipFile, ZipInfo


FIXTURE = Path(__file__).resolve().parent
EXAMPLES = FIXTURE.parents[3] / "examples" / "modding" / "typed-pdxxx-basic"
MANIFEST = "_meta/manifest.json"
PROJECTILE = "dependencies/assets/projectiles/primary.pdprojectile"
TEXTURE = "dependencies/assets/textures/impact.pdtexture"
EFFECT_ID = "grid_fx:impact"
TEXTURE_ID = "grid_fx:impact_tex"


def json_bytes(source: dict) -> bytes:
    return (json.dumps(source, indent=2) + "\n").encode("utf-8")


def read_members(source: Path | BytesIO) -> dict[str, bytes]:
    with ZipFile(source) as archive:
        return {entry.filename: archive.read(entry) for entry in archive.infolist()}


def archive_bytes(members: dict[str, bytes]) -> bytes:
    buffer = BytesIO()
    with ZipFile(buffer, "w", compression=ZIP_STORED) as archive:
        for name, contents in sorted(members.items()):
            entry = ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            entry.create_system = 0
            archive.writestr(entry, contents)
    return buffer.getvalue()


def build() -> None:
    texture = read_members(EXAMPLES / "textures" / "tri_texture.pdtexture")
    texture["texture.ini"] = (
        f"[texture]\ncatalog_id = {TEXTURE_ID}\nname = Grid Impact Texture\n\n"
        "[source]\ntexture_file = texture.png\n\n"
        f"[meta]\nmanifest = {MANIFEST}\n"
    ).encode("utf-8")
    texture[MANIFEST] = json_bytes({
        "schema": "pd.asset_archive.manifest.v1",
        "pd_kind": "texture",
        "catalog_id": TEXTURE_ID,
        "texture_file": "texture.png",
    })

    effect = {
        "effect.ini": (
            f"[effect]\ncatalog_id = {EFFECT_ID}\nname = Grid Impact\n"
            "schema = pd.effect_graph.v1\neffect_key = explosion\ntarget_key = world\n"
            "effect_file = effect.graph.json\nintensity = 1.0\ngrid_provenance = original\n"
        ).encode("utf-8"),
        "effect.graph.json": json_bytes({
            "schema": "pd.effect_graph.v1",
            "asset_id": EFFECT_ID,
            "nodes": [
                {"id": "impact", "kind": "effect.explosion", "params": {"explosion_class": "small"}},
                {"id": "particle", "kind": "effect.particle", "params": {
                    "texture_ref": TEXTURE_ID, "size": 24, "intensity": 0.75,
                }},
            ],
            "edges": [{"from": "impact", "to": "particle"}],
        }),
        MANIFEST: json_bytes({
            "schema": "pd.asset_archive.manifest.v1",
            "pd_kind": "effect",
            "catalog_id": EFFECT_ID,
            "effect_file": "effect.graph.json",
        }),
        TEXTURE: archive_bytes(texture),
    }

    weapon = read_members(EXAMPLES / "weapons" / "tri_weapon.pdweapon")
    projectile = read_members(BytesIO(weapon[PROJECTILE]))
    descriptor = projectile["projectile.ini"].decode("utf-8")
    before, marker, meta = descriptor.partition("\n[meta]\n")
    if not marker or "\ndeps =" in before:
        raise ValueError("Expected the Triangle projectile without a declared external effect")
    projectile["projectile.ini"] = f"{before}\ndeps = {EFFECT_ID}\n{marker}{meta}".encode("utf-8")
    manifest = json.loads(projectile[MANIFEST])
    manifest["deps"] = EFFECT_ID
    projectile[MANIFEST] = json_bytes(manifest)
    graph = json.loads(projectile["behavior.graph.json"])
    if graph["nodes"][0]["id"] != "motion":
        raise ValueError("Expected a native Triangle motion root")
    # Keep the shot alive long enough to reach a real room surface. No fuse
    # can dispatch this fixture's authored explosion before contact.
    graph["nodes"] = [
        {"id": "motion", "kind": "projectile.motion", "params": {
            "motion_kind": "powered", "powered": True, "speed": 60, "timer60": 360,
        }},
        {"id": "impact", "kind": "projectile.impact", "params": {
            "impact_filter": "background", "consume_on_hit": True, "stick_on_hit": False,
            "explosion_ref": EFFECT_ID,
        }},
    ]
    graph["edges"] = [{"from": "motion", "to": "impact"}]
    projectile["behavior.graph.json"] = json_bytes(graph)
    weapon[PROJECTILE] = archive_bytes(projectile)

    (FIXTURE / "impact.pdeffect").write_bytes(archive_bytes(effect))
    (FIXTURE / "tri_weapon.pdweapon").write_bytes(archive_bytes(weapon))


if __name__ == "__main__":
    build()
