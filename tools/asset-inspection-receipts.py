#!/usr/bin/env python3
"""Convert bounded native member-read events; never infer reads from hydration logs."""
from __future__ import annotations

import argparse
from datetime import datetime
import hashlib
import importlib.util
import json
from pathlib import Path, PurePosixPath
import re

spec = importlib.util.spec_from_file_location("inspection_manifest", Path(__file__).with_name("asset-inspection-manifest.py"))
inspection = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inspection)

SHA = re.compile(r"[0-9a-f]{64}\Z")
TOKEN = re.compile(r"[A-Za-z0-9_.-]{1,80}\Z")
CATALOG = re.compile(r"[A-Za-z][\w.-]*:[\w./-]+\Z")
PRIMARY = {
    "modeldef_compile": ("mesh_source", {".obj", ".gltf", ".glb"}),
    "texture_load": ("image_source", {".png", ".tga", ".jpg", ".jpeg"}),
    "animation_decode": ("animation_source", {".gltf", ".glb"}),
    "audio_mix": ("audio_source", {".wav", ".ogg", ".mp3"}),
    "character_hydrate": ("character_policy", {".ini", ".json"}),
    "graph_execute": ("graph_source", {".json"}),
}
COMMON = ("run_id", "attempt_id", "catalog_id", "archive_origin", "archive_sha256", "generation", "operation")


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON key: " + key)
        result[key] = value
    return result


def loads(text):
    return json.loads(text, object_pairs_hook=unique_object)


def member_path(value):
    if not isinstance(value, str) or not value or len(value) > 1024:
        raise ValueError("invalid member path")
    if "\\" in value or ":" in value or any(ord(c) < 32 for c in value):
        raise ValueError("unsafe member path")
    parts = value.split("/")
    if value.startswith("/") or any(p in {"", ".", ".."} for p in parts):
        raise ValueError("unsafe member path")
    return value


def stamp(value):
    if not isinstance(value, str):
        raise ValueError("missing event timestamp")
    parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    if parsed.tzinfo is None:
        raise ValueError("event timestamp requires timezone")
    return parsed


def generation_valid(value):
    return isinstance(value, dict) and set(value) == {"binary_sha256", "source_sha256"} and all(
        isinstance(v, str) and SHA.fullmatch(v) for v in value.values())


def native_texture(value):
    keys = {"native_slot", "width", "height", "rgba_bytes", "rgba_sha256"}
    if not isinstance(value, dict) or set(value) != keys or any(
            type(value[k]) is not int for k in keys - {"rgba_sha256"}):
        raise ValueError("invalid retained native texture witness")
    if not 0 <= value["native_slot"] < 65535 or not 1 <= value["width"] <= 255 or not 1 <= value["height"] <= 255:
        raise ValueError("native texture identity/dimensions outside carrier range")
    if value["rgba_bytes"] != value["width"] * value["height"] * 4 or not SHA.fullmatch(str(value["rgba_sha256"])):
        raise ValueError("invalid retained RGBA byte witness")
    return value


def dependency_bindings(value, dependencies):
    if not isinstance(value, list) or len(value) > 256:
        raise ValueError("invalid native dependency bindings")
    seen = set()
    for binding in value:
        if not isinstance(binding, dict) or set(binding) != {"catalog_id", "native_source_closure_sha256", "native_slot"}:
            raise ValueError("invalid native dependency binding fields")
        identity = binding["catalog_id"]
        if not isinstance(identity, str) or identity not in dependencies or identity in seen:
            raise ValueError("undeclared/duplicate native texture binding")
        if not SHA.fullmatch(str(binding["native_source_closure_sha256"])) or type(binding["native_slot"]) is not int or not 0 <= binding["native_slot"] < 65535:
            raise ValueError("invalid native texture binding identity")
        seen.add(identity)
    return value


def selected_coverage(traces, catalog_id, origin, archive_hash, files=None):
    """Link native parent bindings to same-run retained texture witnesses; no draw promotion."""
    roots = [t for t in traces if t["catalog_id"] == catalog_id and t["archive_origin"] == origin
             and t["archive_sha256"] == archive_hash and t["operation"] == "modeldef_compile"]
    if len(roots) != 1 or roots[0]["load_verdict"] != "pass":
        raise ValueError("expected exactly one successful selected native model trace")
    root = roots[0]
    if root.get('native_completed') is not True:
        raise ValueError('selected native model lacks its completed consumer witness')
    dependencies = root.get("pending_dependencies", [])
    bindings = {b["catalog_id"]: b for b in dependency_bindings(root.get("dependency_bindings", []), dependencies)}
    leaves = [t for t in traces if t is not root]
    for leaf in leaves:
        if leaf.get('native_completed') is not True:
            raise ValueError('native dependency lacks its completed consumer witness')
        if leaf["catalog_id"] not in dependencies or leaf["operation"] != "texture_load" or not leaf["archive_origin"].lower().endswith(".pdtexture"):
            raise ValueError("native stream contains an unselected dependency")
        if leaf["run_id"] != root["run_id"] or leaf["event_stream_sha256"] != root["event_stream_sha256"]:
            raise ValueError("dependency comes from a different native run")
        if leaf["generation"] != root["generation"]:
            raise ValueError("dependency comes from a different native generation")
        if "native_texture" in leaf:
            native_texture(leaf["native_texture"])
    if files is not None:
        for trace in traces:
            if files.get(trace["archive_origin"], {}).get("sha256") != trace["archive_sha256"]:
                raise ValueError("native archive does not match the frozen source seed")
    coverage = []
    for identity in dependencies:
        binding = bindings.get(identity)
        matches = [t for t in leaves if t["catalog_id"] == identity and t["load_verdict"] == "pass"
                   and binding and t.get("native_source_closure_sha256") == binding["native_source_closure_sha256"]
                   and t.get("native_texture", {}).get("native_slot") == binding["native_slot"]]
        coverage.append(dict(catalog_id=identity, verdict="pass" if matches else "pending",
                             native_attempts=[t["attempt_id"] for t in matches]))
    complete = bool(dependencies) and all(row["verdict"] == "pass" for row in coverage)
    if bindings and (set(bindings) != set(dependencies) or not complete):
        raise ValueError("declared native model texture bindings lack complete matching dependency receipts")
    return dict(catalog_id=catalog_id, archive_origin=origin, dependencies=coverage,
                dependency_sources="pass" if complete else "pending", native_draw="pending", fidelity="pending")


def convert(events, generation, stream_sha256, max_attempts=256, max_members=2048):
    """Return reviewed-contract traces and pending attempts, not native proof by itself."""
    if not generation_valid(generation) or not SHA.fullmatch(stream_sha256):
        raise ValueError("invalid frozen generation/event-stream hash")
    attempts = {}
    for line, event in enumerate(events, 1):
        if not isinstance(event, dict) or event.get("schema") != "pd2.native-asset-event.v1":
            raise ValueError("unsupported native member event at line " + str(line))
        if not all(TOKEN.fullmatch(str(event.get(k, ""))) for k in ("run_id", "attempt_id")):
            raise ValueError("invalid native attempt identity")
        if not CATALOG.fullmatch(str(event.get("catalog_id", ""))):
            raise ValueError("invalid catalog identity")
        if not SHA.fullmatch(str(event.get("archive_sha256", ""))):
            raise ValueError("raw typed archive hash required")
        origin = event.get("archive_origin")
        if not isinstance(origin, str) or len(origin) > 4096:
            raise ValueError("selected public archive origin required")
        chain = origin.split("::")
        for archive in chain:
            member_path(archive)
            if not inspection.family(archive) and PurePosixPath(archive).suffix.lower() not in inspection.CONTAINERS:
                raise ValueError("selected origin must identify typed archive boundaries")
        if not inspection.family(chain[-1]):
            raise ValueError("selected origin must terminate at a public typed archive")
        if event.get("generation") != generation or event.get("operation") not in PRIMARY:
            raise ValueError("mixed generation or unsupported native operation")
        when = stamp(event.get("timestamp"))
        key = (event["run_id"], event["attempt_id"])
        kind = event.get("event")
        if kind == "begin":
            if key in attempts or len(attempts) >= max_attempts:
                raise ValueError("duplicate attempt or attempt budget exceeded")
            if event.get("provider") != "FileProvider":
                raise ValueError("public FileProvider required")
            attempts[key] = dict(begin=event, time=when, members={}, roles={}, sizes={}, lines=[line], end=None)
            continue
        if key not in attempts:
            raise ValueError("event without native begin")
        attempt = attempts[key]
        if attempt["end"] is not None or any(event.get(k) != attempt["begin"].get(k) for k in COMMON):
            raise ValueError("closed attempt or cross-asset event")
        if when < attempt["time"]:
            raise ValueError("native event time regressed")
        attempt["time"] = when
        attempt["lines"].append(line)
        if kind == "member":
            path = member_path(event.get("member"))
            digest, size, role = event.get("sha256"), event.get("bytes"), event.get("source_role")
            if event.get("provider") != "FileProvider" or not isinstance(digest, str) or not SHA.fullmatch(digest):
                raise ValueError("member must attest actual FileProvider bytes")
            if type(size) is not int or size <= 0 or not isinstance(role, str) or not TOKEN.fullmatch(role):
                raise ValueError("invalid native member size/role")
            if path in attempt["members"]:
                if attempt["members"][path] != digest or attempt["roles"][path] != role or attempt["sizes"][path] != size:
                    raise ValueError("member changed within native attempt")
            elif len(attempt["members"]) >= max_members:
                raise ValueError("native member budget exceeded")
            attempt["members"][path] = digest
            attempt["roles"][path] = role
            attempt["sizes"][path] = size
        elif kind == "end":
            if event.get("rom_fallback") is not False or event.get("native_completed") is not True:
                raise ValueError("fallback or missing native completion witness")
            if event.get("load_verdict") not in {"pass", "fail"}:
                raise ValueError("invalid native terminal verdict")
            checks = event.get("checks")
            if not isinstance(checks, dict) or not all(isinstance(k, str) and TOKEN.fullmatch(k)
                    and isinstance(v, str) and v in {"pass", "fail", "pending"} for k, v in checks.items()):
                raise ValueError("invalid native family checks")
            if checks.get("native_draw") == "pass":
                raise ValueError("source consumption operations do not attest native draw provenance")
            attempt["end"] = event
        else:
            raise ValueError("unknown native member event")
    traces, pending = [], []
    for attempt in attempts.values():
        begin, end = attempt["begin"], attempt["end"]
        role, suffixes = PRIMARY[begin["operation"]]
        consumed = {p: h for p, h in attempt["members"].items() if not p.startswith("_meta/")}
        primary = any(attempt["roles"][p] == role and PurePosixPath(p).suffix.lower() in suffixes for p in consumed)
        if end is None or not primary:
            pending.append({k: begin[k] for k in COMMON} | {
                "reason": "missing native terminal event" if end is None else "missing consumed activating source member"})
            continue
        trace = {k: begin[k] for k in COMMON}
        trace.update(schema="pd2.native-asset-consumer.v1", consumer="native_catalog_provider",
                     rom_fallback=False, timestamp=end["timestamp"], load_verdict=end["load_verdict"],
                     checks=end["checks"], consumed_members=consumed,
                     consumed_source_roles={p: attempt["roles"][p] for p in consumed},
                     native_completed=True, event_stream_sha256=stream_sha256,
                     event_lines=attempt["lines"])
        if "native_source_closure_sha256" in end:
            closure = end["native_source_closure_sha256"]
            if not isinstance(closure, str) or not SHA.fullmatch(closure):
                raise ValueError("invalid native source closure hash")
            trace["native_source_closure_sha256"] = closure
        trace["consumed_member_sizes"] = {p: attempt["sizes"][p] for p in consumed}
        dependencies = end.get("pending_dependencies", [])
        if not isinstance(dependencies, list) or len(dependencies) > max_members or any(
                not isinstance(p, str) or not CATALOG.fullmatch(p) for p in dependencies) or len(set(dependencies)) != len(dependencies):
            raise ValueError("invalid pending native dependencies")
        if dependencies and end["checks"].get("dependency_sources") == "pass":
            raise ValueError("pending dependencies cannot attest complete dependency sources")
        trace["pending_dependencies"] = dependencies
        if "native_texture" in end:
            if begin["operation"] != "texture_load" or "native_source_closure_sha256" not in trace:
                raise ValueError("native texture witness requires its retained texture source generation")
            trace["native_texture"] = native_texture(end["native_texture"])
        if "dependency_bindings" in end:
            if begin["operation"] != "modeldef_compile":
                raise ValueError("texture dependency bindings require a native parent model")
            trace["dependency_bindings"] = dependency_bindings(end["dependency_bindings"], dependencies)
        traces.append(trace)
    return traces, pending


def read_events(path, max_bytes=16 * 1024**2, max_events=50000):
    with Path(path).open("rb") as source:
        raw = source.read(max_bytes + 1)
    if len(raw) > max_bytes:
        raise ValueError("native event byte budget exceeded")
    lines = raw.decode("utf-8-sig").splitlines()
    if not lines or len(lines) > max_events or any(not line.strip() for line in lines):
        raise ValueError("empty event stream/line or event count budget exceeded")
    return raw, [loads(line) for line in lines]


def write_bundle(output, raw, traces, pending, max_bytes=32 * 1024**2, selection=None):
    output = Path(output)
    stream_hash = hashlib.sha256(raw).hexdigest()
    files = {"native-events.jsonl": raw}
    receipts = []
    for number, trace in enumerate(traces):
        if trace.get("event_stream_sha256") != stream_hash:
            raise ValueError("trace/event-stream hash mismatch")
        name = "trace-%06d.json" % number
        files[name] = (json.dumps(trace, indent=2) + "\n").encode()
        receipts.append({k: trace[k] for k in ("catalog_id", "archive_origin", "archive_sha256", "generation", "consumer",
                                              "rom_fallback", "timestamp", "load_verdict", "checks")} | {
            "pending_dependencies": trace.get("pending_dependencies", []),
            "artifacts": [{"path": name, "kind": "native_trace", "sha256": hashlib.sha256(files[name]).hexdigest()},
                          {"path": "native-events.jsonl", "kind": "native_events", "sha256": stream_hash}],
            "defects": [] if trace["load_verdict"] == "pass" else ["native consumer reported failure"],
        })
    envelope = {"schema": "pd2.asset-receipt-envelope.v1", "receipts": receipts, "pending": pending,
                "producer_gap": "Converter validates attribution contract; native emission/producer review is separately required."}
    if selection is not None:
        envelope["selection"] = selection
    files["receipts.json"] = (json.dumps(envelope, indent=2) + "\n").encode()
    if max_bytes <= 0 or sum(map(len, files.values())) > max_bytes:
        raise ValueError("receipt output byte budget exceeded; no output created")
    output.mkdir(parents=True, exist_ok=False)
    for name, payload in files.items():
        with (output / name).open("xb") as target:
            target.write(payload)
    return envelope


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("events", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--binary-sha256", required=True)
    parser.add_argument("--source-sha256", required=True)
    parser.add_argument("--selected-catalog-id")
    parser.add_argument("--selected-origin")
    parser.add_argument("--selected-archive-sha256")
    parser.add_argument("--frozen-seed", type=Path)
    parser.add_argument("--seed-id")
    args = parser.parse_args()
    try:
        raw, events = read_events(args.events)
        traces, pending = convert(events, dict(binary_sha256=args.binary_sha256, source_sha256=args.source_sha256),
                                  hashlib.sha256(raw).hexdigest())
        selection = None
        selected = (args.selected_catalog_id, args.selected_origin, args.selected_archive_sha256, args.frozen_seed, args.seed_id)
        if any(selected):
            if not all(selected): raise ValueError("selected native coverage requires exact root and frozen seed controls")
            planner = inspection.load_helper('selected_seed_metadata', Path(__file__).with_name('asset-inspection-plan.py'))
            files = planner.seed_files(args.frozen_seed, args.seed_id)
            if planner.source_set_hash(files) != args.source_sha256 or files.get('PerfectDark.exe', {}).get('sha256') != args.binary_sha256:
                raise ValueError("selected native coverage has a different frozen generation")
            selection = selected_coverage(traces, args.selected_catalog_id, args.selected_origin,
                                          args.selected_archive_sha256, files)
        result = write_bundle(args.output, raw, traces, pending, selection=selection)
    except (OSError, ValueError, TypeError) as error:
        parser.exit(1, str(error) + "\n")
    print(json.dumps({"receipts": len(result["receipts"]), "pending": len(result["pending"])}))
    return 2 if pending or not traces else 0


if __name__ == "__main__":
    raise SystemExit(main())
