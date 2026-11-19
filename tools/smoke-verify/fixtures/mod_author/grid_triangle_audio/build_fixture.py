"""Generate the Grid audio smoke's public-source Triangle Weapon template."""

import json
from pathlib import Path
from zipfile import ZIP_STORED, ZipFile, ZipInfo


FIXTURES = Path(__file__).resolve().parent
EXAMPLES = FIXTURES.parents[4] / "examples" / "modding" / "typed-pdxxx-basic"
CLICK_MEMBER = "dependencies/assets/audio/fire.pdsfx"
MANIFEST_MEMBER = "_meta/manifest.json"


def build() -> None:
    template = EXAMPLES / "weapons" / "tri_weapon.pdweapon"
    with ZipFile(template) as archive:
        members = {entry.filename: archive.read(entry) for entry in archive.infolist()}

    descriptor = members["weapon.ini"].decode("utf-8")
    before, meta, after = descriptor.partition("\n[meta]\n")
    manifest = json.loads(members[MANIFEST_MEMBER])
    if not meta or "fire_sound_archive" in manifest or CLICK_MEMBER in members:
        raise ValueError("Expected the original Triangle template without an authored fire sound")

    members["weapon.ini"] = (
        f"{before}\nfire_sound_archive = {CLICK_MEMBER}\n{meta}{after}"
    ).encode("utf-8")
    manifest["fire_sound_archive"] = CLICK_MEMBER
    members[MANIFEST_MEMBER] = (json.dumps(manifest, indent=2) + "\n").encode("utf-8")
    members[CLICK_MEMBER] = (EXAMPLES / "audio" / "sfx" / "tri_click.pdsfx").read_bytes()

    with ZipFile(FIXTURES / template.name, "w", compression=ZIP_STORED) as archive:
        for name, contents in members.items():
            entry = ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            entry.create_system = 0
            archive.writestr(entry, contents)


if __name__ == "__main__":
    build()
