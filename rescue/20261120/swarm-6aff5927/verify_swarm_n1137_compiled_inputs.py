from hashlib import file_digest, sha256
from pathlib import Path
from zipfile import ZipFile
import json
import sys

SOURCE_COMMIT = "1b29c009e5f9de558cbae2aab844fcd64e622761"
CLIENT_SHA = "e947f55f0fb4ec7cc06ca7ab0321b2d5ea7ee24924c8605aa87010fe337efdea"
LOG_SHA = "9bbea9aa3ea16ba26116fe84aa53445f0b17e7b4ddca2f15052304c941936dec"
PATCH_SHA = "3f25a29dfb89c4710b5d57ad3d492f9af9f31bd6efa8df0f774312b4ef3b9a43"
BUILD_QUEUE = "q_20261120030911032_c24a1e14"
BUILD_LOG = "_build-headless-20261119-220934-compile-client"
FROZEN = {
    "port/src/swarm_test.c": "ef0f0022bea291b1c4dda201fe33eb5d5b5f8c86d1f45b2aa2a3f4b9a722e2d3",
    "port/include/swarm_gpu.h": "a5a78b395747ddc889b08bd468f1229cb90030df1acaa1955a34dccd3525c82e",
    "port/fast3d/swarm_gpu.cpp": "7ebc741e1102d82cad9e4a1d0e85b222a00b14ce0d02e175342fba5052213d6b",
    "tools/smoke-verify/tests/skedar_swarm_baseline_cpu.json": "698a5ff9e3f1a177ba9a2c6c87c2ea1efac0020076ed4a2820c39cabdaa1f85c",
    "tools/smoke-verify/tests/skedar_swarm_baseline_gpu.json": "b6d8b4b289b0e7e01ac08174ee7ce4102336d4b20ad0f0623ce8c4997db7df54",
    "tools/smoke-verify/verify_skedar_swarm_pair.py": "3f99868e2db930cd2833c1df698bca4373f778c692f4243802684b7870854fd2",
}

def require(condition, reason):
    if not condition:
        raise ValueError(reason)

def digest(path):
    with path.open("rb") as stream:
        return file_digest(stream, "sha256").hexdigest()

def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))

repo, temp, output = map(Path, sys.argv[1:])
build = repo / ".claude/session-builds/jenkins-client-n1124"
peer = repo / ".claude/jenkins-compile-unblock-N1124-N1125"
binary = build / "PerfectDark.exe"
log = build / f"{BUILD_LOG}.out.log"
patch = temp / "swarm-native-baseline-final-92200bbd.patch"
require(digest(binary) == CLIENT_SHA, "exact client SHA differs")
require(digest(log) == LOG_SHA, "exact compile log SHA differs")
require((build / f"{BUILD_LOG}.exit").read_text().strip() == "0", "compile exit differs")
require(digest(patch) == PATCH_SHA, "frozen patch SHA differs")
for path, expected in FROZEN.items():
    require(digest(repo / path) == expected, f"frozen path differs: {path}")

observed_path = peer / "client-link-source-scoped-observed.json"
final_path = peer / "client-link-source-scoped-final.json"
observed, final = read(observed_path), read(final_path)
require(observed["head"] == final["head"] == SOURCE_COMMIT, "compile source HEAD differs")
sources = observed["dirty_compile_candidates"]
require(sources == final["dirty_compile_candidates"] and len(sources) == 89, "observed dirty build inputs differ")
swarm_sources = [path for path in FROZEN if path.startswith("port/")]
require(all(sources.get(path) == FROZEN[path] for path in swarm_sources), "compiled Swarm source hashes differ")

lines = log.read_text(encoding="utf-8", errors="replace").splitlines()
compiles = {}
cwd = repo.resolve().as_posix().lower()
for path in swarm_sources:
    if path.endswith(".h"):
        continue
    hits = [
        n for n, line in enumerate(lines, 1)
        if f" -c {cwd}/{path.lower()}" in line.lower() and " -DPD_DEV_BUILD=1 " in line
    ]
    require(len(hits) == 1, f"missing unique dev compile for {path}")
    require('#include "swarm_gpu.h"' in (repo / path).read_text(), f"missing frozen header include for {path}")
    compiles[path] = hits[0]
require(any(" -o PerfectDark.exe " in line for line in lines), "native executable link missing")
cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
require("CMAKE_BUILD_TYPE:STRING=RelWithDebInfo" in cache, "unexpected build type")
require(f"cmake_home_directory:internal={cwd}" in cache.lower(), "noncanonical CMake input")
pe = binary.read_bytes()
for marker in (b"--debug-swarm-paired-baseline", b"SWARM.BASELINE:", b"gpu_readback", b"gpu_apply"):
    require(marker in pe, f"compiled baseline marker missing: {marker!r}")

for backend, launch in (("cpu", "swarm_cpu"), ("gpu", "swarm_gpu")):
    fixture = read(repo / f"tools/smoke-verify/tests/skedar_swarm_baseline_{backend}.json")
    args = fixture["boot_args"]
    require("--renderer" not in args and "--renderer-parity" not in args, "fixture replaced default renderer")
    for flag in ("--portable", "--no-net", "--debug-swarm-paired-baseline"):
        require(args.count(flag) == 1, f"missing or repeated fixture flag: {flag}")
    for flag, value in (("--launch-scenario", launch), ("--debug-swarm-map", "base:mp_skedar")):
        require(args.count(flag) == 1 and args[args.index(flag) + 1] == value, f"wrong fixture argument: {flag}")

base = Path(r"D:\PD2-wt\shared-base-12")
arena = base / "data/ntsc-final/arenas/base_arena_mp_skedar.pdarena"
scenario = base / "data/ntsc-final/scenarios/base_scenario_mp_skedar.pdscenario"
arena_sha, scenario_sha = digest(arena), digest(scenario)
require(arena_sha == "651dc5413fa6acbaddb9426c088c8261e140a1a2e91cf65cb47aee8021faf638", "Base 12 arena differs")
require(scenario_sha == "4ec31051d29d87c54cc6d4546cff0813ccbd4063b81d6c574bfbd4e1450251f1", "Base 12 scenario differs")
with ZipFile(arena) as archive:
    nested = [name for name in archive.namelist() if name.lower().endswith(".pdscenario")]
    require(len(nested) == 1, "ambiguous nested Skedar source")
    require(sha256(archive.read(nested[0])).hexdigest() == scenario_sha, "loose and nested scenarios differ")

receipt = {
    "schema": 1, "source_commit": SOURCE_COMMIT, "client_sha256": CLIENT_SHA,
    "build_queue_id": BUILD_QUEUE, "compile_log_sha256": LOG_SHA,
    "compile_log_lines": compiles,
    "compile_source_observed_sha256": digest(observed_path),
    "compile_source_final_sha256": digest(final_path),
    "compile_dirty_input_count": len(sources),
    "frozen_patch_sha256": PATCH_SHA, "frozen_paths_sha256": FROZEN,
    "build_type": "RelWithDebInfo", "dev_build": True,
    "fixture_renderer": "classic_opengl_default",
    "base_12": {"root": str(base), "arena_sha256": arena_sha, "scenario_sha256": scenario_sha},
    "native_gameplay_verified": False,
}
with output.open("x", encoding="utf-8", newline="\n") as stream:
    stream.write(json.dumps(receipt, sort_keys=True, indent=2) + "\n")
print(f"{output.name} SHA-256 {digest(output)}")
