"""Check paired four-actor Skedar Swarm evidence without running the game."""
import argparse
import gzip
import hashlib
import json
import math
from pathlib import Path
import re
import struct

MARKER = b"SWARM.BASELINE: "
SLOTS = set(range(4))
TIMES = (35000, 50000)
ACTOR_FIELDS = (
    "source_row", "generation", "chrnum", "prop_index", "position", "rooms",
    "team", "body", "model_scale", "radius", "height", "yaw", "animation",
    "animation_frame", "support_up",
)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def distinct_keys(pairs):
    obj = {}
    for key, value in pairs:
        require(key not in obj, f"duplicate JSON field {key}")
        obj[key] = value
    return obj


def reject_constant(value):
    raise ValueError(f"invalid JSON constant {value}")


def parse(data):
    return json.loads(data, object_pairs_hook=distinct_keys, parse_constant=reject_constant)


def integer(value, minimum=0):
    return type(value) is int and value >= minimum


def finite(value):
    return type(value) in (float, int) and math.isfinite(value)


def vector(value):
    return isinstance(value, list) and len(value) == 3 and all(map(finite, value))


def path_name(name):
    return re.split(r"[/\\]", name)[-1]


def artifact(root, item):
    path = (root / item["path"]).resolve(strict=True)
    require(path.is_file() and path.is_relative_to(root), f"unsafe artifact path {path.name}")
    with path.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    require(item["sha256"] == digest, f"artifact SHA-256 differs: {path.name}")
    return path, digest


def bmp_dimensions(path):
    with path.open("rb") as stream:
        header = stream.read(54)
    require(len(header) == 54 and header[:2] == b"BM", f"invalid BMP: {path.name}")
    size, = struct.unpack_from("<I", header, 2)
    offset, dib, width, height, planes, bpp, compression = struct.unpack_from("<IIiiHHI", header, 10)
    require(
        (offset, dib, planes, bpp, compression) == (54, 40, 1, 24, 0)
        and width > 0 and height > 0,
        f"unexpected native BMP layout: {path.name}",
    )
    require(
        size == 54 + ((width * 3 + 3) & ~3) * height == path.stat().st_size,
        f"invalid native BMP size: {path.name}",
    )
    return width, height


def read_log(path):
    records, requests, captures, exits = [], [], [], []
    require(path.stat().st_size <= 64 * 1024 * 1024, "native log exceeds 64 MiB")
    with path.open("rb") as stream:
        for number, line in enumerate(stream, 1):
            if MARKER in line:
                data = line.partition(MARKER)[2].strip()
                require(len(data) <= 2048 and len(records) < 4096, "baseline records exceed bounds")
                records.append((number, parse(data)))
            request = re.search(rb"SMOKE: screenshot at_ms=(\d+) -> (.+?)\s*$", line)
            if request:
                requests.append((number, int(request[1]), path_name(request[2].decode())))
            capture = re.search(rb"SMOKE\.GLSHOT: captured (\d+)x(\d+) -> (.+?)\s*$", line)
            if capture:
                captures.append((number, (int(capture[1]), int(capture[2])), path_name(capture[3].decode())))
            exit_event = re.search(rb"SMOKE: (scripted exit at_ms=\d+|result=\S+)", line)
            if exit_event:
                exits.append((number, exit_event[1]))
    return records, requests, captures, exits


def actor_row(event):
    row = {key: event[key] for key in ACTOR_FIELDS}
    for key in ("source_row", "chrnum", "prop_index", "team", "animation"):
        require(integer(row[key]), f"invalid actor {key}")
    require(integer(row["generation"], 1), "invalid actor generation")
    require(isinstance(row["body"], str) and row["body"], "missing actor body")
    require(vector(row["position"]) and vector(row["support_up"]), "invalid actor pose vectors")
    require(math.isclose(math.hypot(*row["support_up"]), 1, abs_tol=1e-4), "invalid actor support basis")
    rooms = row["rooms"]
    require(isinstance(rooms, list) and rooms and all(map(integer, rooms)), "invalid actor rooms")
    require(len(set(rooms)) == len(rooms), "duplicate actor rooms")
    for key in ("model_scale", "radius", "height"):
        require(finite(row[key]) and row[key] > 0, f"invalid actor {key}")
    require(finite(row["yaw"]) and finite(row["animation_frame"]), "invalid actor animation pose")
    return row


def identity(slot, row):
    return [slot, row["generation"], row["chrnum"], row["prop_index"]]


def verify_events(records, mode, captures, exit_line):
    require(records, "missing baseline records")
    _, start = records[0]
    require(
        start["event"] == "start" and start["mode"] == mode
        and start["map"] == "base:mp_skedar" and start["count"] == 4
        and integer(start["seed"]) and vector(start["player_anchor"]),
        "invalid paired benchmark start",
    )
    epoch = start["stage_epoch"]
    require(integer(epoch, 1), "missing stage epoch")
    rows, dispatches, population_lines = {}, {}, []
    ready = initialized = False
    last_serial = 0
    for line, event in records[1:]:
        kind = event["event"]
        require(event["stage_epoch"] == epoch, f"benchmark stage changed at line {line}")
        if kind == "spawn":
            slot = event["slot"]
            require(
                not ready and type(slot) is int and slot in SLOTS and slot not in rows,
                f"unexpected actor replacement or slot at line {line}",
            )
            rows[slot] = actor_row(event)
            continue
        if kind == "ready":
            require(not ready and set(rows) == SLOTS and line < captures[0][0], "invalid actor readiness")
            ready = True
            continue
        expected = [identity(slot, rows[slot]) for slot in sorted(rows)]
        if kind == "population":
            require(ready and event["actors"] == expected, "live benchmark population changed")
            population_lines.append(line)
            continue
        require(
            kind in ("gpu_init", "gpu_dispatch", "gpu_readback", "gpu_apply")
            and mode == "GPU_FULL" and ready,
            f"unexpected backend or lifecycle event {kind} at line {line}",
        )
        require(event["gl_error"] == 0, f"GL error at line {line}")
        if kind == "gpu_init":
            require(
                not initialized and event["compiled"] is True and event["linked"] is True,
                "missing native shader compile or link",
            )
            initialized = True
            continue
        serial = event["serial"]
        require(initialized and integer(serial, 1), "invalid GPU initialization or sequence")
        if kind == "gpu_dispatch":
            require(
                serial > last_serial and event["mode"] == "GPU_FULL" and event["count"] == 4,
                "invalid GPU_FULL dispatch order or mode",
            )
            inputs = event["inputs"]
            require(
                isinstance(inputs, list) and len(inputs) == 4
                and all(isinstance(row, list) and len(row) == 6 and all(map(finite, row)) for row in inputs),
                "invalid GPU gathered position or support rows",
            )
            if last_serial == 0:
                require(
                    all(math.dist(data[:3], rows[slot]["position"]) <= 1e-3 for slot, data in enumerate(inputs)),
                    "first GPU dispatch did not consume the prepared positions",
                )
            dispatches[serial] = {"path": None, "applied": set(), "motion": False, "last_apply": None}
            last_serial = serial
            continue
        require(serial in dispatches, "GPU result has no matching dispatch")
        batch = dispatches[serial]
        if kind == "gpu_readback":
            require(not batch["path"], "duplicate GPU readback")
            require(event["path"] in ("fenced", "blocking"), "unknown native GPU readback path")
            require(event["markers"] == [serial] * 4, "shader result did not echo the dispatch serial")
            require(event["actors"] == expected, "GPU readback actor identity differs")
            batch["path"] = event["path"]
        elif kind == "gpu_apply":
            slot = event["slot"]
            require(
                batch["path"] and type(slot) is int and slot in SLOTS
                and slot not in batch["applied"] and event["actor"] == expected[slot],
                "GPU application identity or ordering differs",
            )
            before, requested, actual = (event[key] for key in ("before", "requested", "actual"))
            require(all(map(vector, (before, requested, actual))), "invalid GPU positions")
            require(event["committed"] is True, "actor commit was not witnessed")
            require(math.dist(requested, actual) <= 1e-3, "actual GPU position differs from request")
            batch["motion"] |= math.dist(before, actual) > 1e-3
            batch["applied"].add(slot)
            batch["last_apply"] = line
    require(ready, "missing four-actor readiness")
    for key in ("source_row", "chrnum", "prop_index"):
        require(len({row[key] for row in rows.values()}) == 4, f"actors share {key}")
    first_capture, second_capture = (capture[0] for capture in captures)
    windows = ((0, first_capture), (first_capture, second_capture), (second_capture, exit_line))
    require(
        all(any(start < line < end for line in population_lines) for start, end in windows),
        "missing live population witness before, between or after native captures",
    )
    qualified = {
        serial: batch for serial, batch in dispatches.items()
        if batch["applied"] == SLOTS
    }
    for start_line, end_line in windows:
        require(
            mode == "CPU" or any(
                start_line < batch["last_apply"] < end_line and (start_line != 0 or batch["motion"])
                for batch in qualified.values()
            ),
            "no matching GPU dispatch, native readback and four-actor commit in a capture window",
        )
    return start["seed"], start["player_anchor"], rows, {
        serial: batch["path"] for serial, batch in qualified.items()
    }


def managed_recipe(path):
    with gzip.open(path, "rb") as stream:
        data = stream.read(16 * 1024 * 1024 + 1)
    require(len(data) <= 16 * 1024 * 1024, "managed recipe exceeds 16 MiB")
    return parse(data)


def verify_recipe_member(recipe, path, digest):
    require(
        sum(
            path_name(name) == path.name and entry["sha256"] == digest
            for name, entry in recipe["changed"].items()
        ) == 1,
        f"managed recipe does not bind {path.name}",
    )


def verify_run(directory, mode):
    root = directory.resolve(strict=True)
    manifest = parse((root / "manifest.json").read_bytes())
    require(manifest["schema"] == 1 and manifest["mode"] == mode, "unexpected manifest")
    require(manifest["run_id"] and manifest["queue_id"], "missing run or queue identity")
    require(re.fullmatch(r"[a-f0-9]{40}", manifest["source_commit"]), "invalid frozen source commit")
    artifacts = {
        key: artifact(root, manifest[key])
        for key in ("client", "arena", "scenario", "source_manifest", "fixture", "receipt", "seed", "log")
    }
    recipe = managed_recipe(artifacts["receipt"][0])
    require(
        recipe["schema"] == 1 and recipe["complete"] is True
        and recipe["id"] == manifest["run_id"]
        and recipe["definition"]["sha256"] == artifacts["fixture"][1]
        and recipe["provenance"]["git_head"] == manifest["source_commit"],
        "managed recipe does not match the run, fixture and source commit",
    )
    seed_catalog = managed_recipe(artifacts["seed"][0])
    canonical_seed = json.dumps(seed_catalog, sort_keys=True, separators=(",", ":")).encode()
    require(hashlib.sha256(canonical_seed).hexdigest() == recipe["seed"], "managed seed differs")
    for key in ("client", "arena", "scenario"):
        name = manifest[key]["install_path"]
        installed = recipe["changed"].get(name, seed_catalog.get(name))
        require(
            name not in recipe["deleted"] and installed and installed["sha256"] == artifacts[key][1],
            f"managed install does not bind {key}",
        )
    receipt = recipe["outcome"]
    fixture_name = parse(artifacts["fixture"][0].read_bytes())["scenario_name"]
    require(
        receipt["name"] == fixture_name and receipt["passed"] is True
        and type(receipt["exit_code"]) is int and receipt["exit_code"] == 0
        and not receipt["launch_error"],
        "expected one passing smoke receipt with clean exit",
    )
    closeout = receipt["launch_closeout"]
    require(
        integer(closeout["process_id"], 1) and closeout["ownership_established"] is True
        and closeout["verified"] is True and not closeout["cleanup_errors"],
        "native actor closeout was not verified",
    )
    verify_recipe_member(recipe, *artifacts["log"])
    records, requests, captures, exits = read_log(artifacts["log"][0])
    require(
        [event for _, event in exits] == [b"scripted exit at_ms=60000", b"result=scripted_exit"]
        and records and records[-1][0] < exits[0][0],
        "expected one native scripted exit at 60s",
    )
    require([time for _, time, _ in requests] == list(TIMES), "expected capture requests at 35s and 50s")
    require(len(captures) == len(manifest["captures"]) == 2, "expected two native captures")
    capture_paths, capture_hashes = [], []
    for binding, request, capture in zip(manifest["captures"], requests, captures, strict=True):
        path, digest = artifact(root, binding)
        request_line, time, name = request
        actual_line, dimensions, actual_name = capture
        require(
            request_line < actual_line < exits[0][0]
            and binding["at_ms"] == time and name == actual_name == path.name
            and dimensions == bmp_dimensions(path),
            "native capture provenance differs",
        )
        verify_recipe_member(recipe, path, digest)
        capture_paths.append(path)
        capture_hashes.append(digest)
    require(len(set(capture_paths)) == len(capture_paths), "run reused a capture path")
    seed, anchor, actors, qualified = verify_events(records, mode, captures, exits[0][0])
    return {
        "run_id": manifest["run_id"],
        "queue_id": manifest["queue_id"],
        "source_commit": manifest["source_commit"],
        "inputs": {key: artifacts[key][1] for key in ("client", "arena", "scenario", "source_manifest")},
        "seed": seed,
        "anchor": anchor,
        "actors": actors,
        "captures": capture_hashes,
        "qualified_gpu_readbacks": qualified,
    }


def verify_pair(cpu_path, gpu_path):
    cpu = verify_run(cpu_path, "CPU")
    gpu = verify_run(gpu_path, "GPU_FULL")
    require(cpu["run_id"] != gpu["run_id"], "CPU and GPU require distinct run IDs")
    for key in ("source_commit", "inputs", "seed", "anchor", "actors"):
        require(cpu[key] == gpu[key], f"CPU and GPU {key} differs")
    return {
        "verdict": "evidence_consistent", "visual_inspection": "required",
        "provider_activation": "required", "cpu": cpu, "gpu": gpu,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cpu_run", type=Path)
    parser.add_argument("gpu_run", type=Path)
    args = parser.parse_args()
    try:
        result = verify_pair(args.cpu_run, args.gpu_run)
    except (ValueError, TypeError, KeyError, OSError, OverflowError, IndexError) as error:
        print(json.dumps({"verdict": "invalid_input", "reason": str(error)}))
        return 1
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
