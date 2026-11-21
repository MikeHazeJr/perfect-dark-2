from hashlib import sha256
from pathlib import Path
import json
import subprocess
import sys

ROOT = Path(r"C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike")
GIT = r"C:\Program Files\Git\cmd\git.exe"
SCOPES = (
    "CMakeLists.txt", ".gitmodules", "cmake", "devtools", "include", "port", "src",
    "third_party", "tools/smoke-verify", "dev-mods/dev-mods.json",
)

def git(*args):
    return subprocess.run(
        [GIT, "--no-optional-locks", *args], cwd=ROOT, check=True,
        stdout=subprocess.PIPE,
    ).stdout

def digest(path):
    hasher = sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            hasher.update(block)
    return hasher.hexdigest()

paths = git("ls-files", "-z", "--cached", "--others", "--exclude-standard", "--", *SCOPES)
status = git("status", "--porcelain=v1", "-z", "--ignore-submodules=all", "--", *SCOPES)
indexed = git("ls-files", "--stage", "-z", "--", "third_party")
gitlinks = {}
for row in indexed.split(b"\x00"):
    if row.startswith(b"160000 "):
        meta, path = row.split(b"\t", 1)
        gitlinks[path.decode("utf-8")] = meta.split()[1].decode("ascii")
files = {}
for path in sorted(set(paths.decode("utf-8").split("\x00")) - {""}):
    if path in gitlinks:
        continue
    absolute = ROOT / path
    files[path] = digest(absolute) if absolute.is_file() else None
manifest = {
    "schema": 1,
    "checkout": str(ROOT),
    "git_head": git("rev-parse", "HEAD").decode("ascii").strip(),
    "origin_dev_local_ref": git("rev-parse", "refs/remotes/origin/dev").decode("ascii").strip(),
    "index_names": git("diff", "--cached", "--name-only", "-z").decode("utf-8").split("\x00")[:-1],
    "status": status.decode("utf-8").split("\x00")[:-1],
    "managed_scopes": SCOPES,
    "visible_source_files_sha256": files,
    "indexed_submodule_oids": gitlinks,
    "note": "Recorded canonical visible source bytes and index gitlinks. Nested dependency working trees and machine toolchain are separate evidence.",
}
output = Path(sys.argv[1])
body = json.dumps(manifest, sort_keys=True, indent=2) + "\n"
with output.open("x", encoding="utf-8", newline="\n") as stream:
    stream.write(body)
print(f"{output.name}: {len(files)} files, {len(gitlinks)} gitlinks, SHA-256 {sha256(body.encode()).hexdigest()}")
