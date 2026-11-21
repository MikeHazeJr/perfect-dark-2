"""Record effective changed inputs to the supported canonical Grid build."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess


ROOTS = (
    "CMakeLists.txt", ".gitmodules", ".build-settings.json", "cmake/", "ld/",
    "include/", "src/", "port/", "third_party/", "dev-mods/", "devtools/",
    "tools/", "tests/", "fonts/",
)
BGFX = tuple(f"third_party/bgfx/{name}" for name in ("bx", "bimg", "bgfx"))
GIT = r"C:\Program Files\Git\cmd\git.exe"


def capture(repo: Path) -> dict:
    def git(*args: str) -> bytes:
        return subprocess.check_output(
            [GIT, "--no-optional-locks", "-c", f"safe.directory={repo.as_posix()}", *args],
            cwd=repo,
        )

    def filenames(raw: bytes) -> set[str]:
        return {part.decode("utf-8") for part in raw.split(b"\0") if part}

    changed = filenames(git("diff", "--ignore-submodules=all", "--name-only", "-z", "HEAD", "--", *ROOTS))
    changed.update(filenames(git("ls-files", "--others", "--exclude-standard", "-z", "--", *ROOTS)))
    settings = repo / ".build-settings.json"
    if settings.is_file():
        changed.add(".build-settings.json")
    hashes = {}
    for name in sorted(changed):
        if name.lower().startswith("tools/workbench/data/"):
            continue
        path = repo / name
        if path.is_file():
            with path.open("rb") as stream:
                hashes[name] = hashlib.file_digest(stream, "sha256").hexdigest()
        else:
            hashes[name] = None
    submodules = git("submodule", "status", "--", *BGFX).decode("utf-8").splitlines()
    if len(submodules) != len(BGFX) or any(not row.startswith(" ") for row in submodules):
        raise RuntimeError("Expected all three BGFX dependencies at their pinned revisions")
    return {
        "repository": str(repo),
        "head": git("rev-parse", "HEAD").decode("ascii").strip(),
        "branch": git("branch", "--show-current").decode("utf-8").strip(),
        "bgfx_submodules": submodules,
        "dirty_inputs_sha256": hashes,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("repo", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    manifest = capture(args.repo.resolve(strict=True))
    args.output.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"{args.output.name}: {manifest['head']}, {len(manifest['dirty_inputs_sha256'])} exact dirty inputs")


if __name__ == "__main__":
    main()
