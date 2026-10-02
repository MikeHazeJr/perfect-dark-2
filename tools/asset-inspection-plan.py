#!/usr/bin/env python3
"""Prepare frozen archive index and labeled pending capture slots from seed metadata only."""
from __future__ import annotations

import argparse
from collections import Counter
import gzip
import hashlib
import html
import importlib.util
import json
from pathlib import Path
import re

spec = importlib.util.spec_from_file_location("inspection_manifest", Path(__file__).with_name("asset-inspection-manifest.py"))
inspection = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inspection)
SHA = re.compile(r"[0-9a-f]{64}\Z")


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode()


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate seed/ledger key")
        result[key] = value
    return result


def bounded_json(path, compressed=False, limit=16 * 1024**2):
    if Path(path).stat().st_size > limit:
        raise ValueError("metadata input byte budget exceeded")
    opener = gzip.open if compressed else open
    with opener(path, "rb") as source:
        raw = source.read(limit + 1)
    if len(raw) > limit:
        raise ValueError("expanded metadata byte budget exceeded")
    return json.loads(raw.decode("utf-8-sig"), object_pairs_hook=unique_object)


def seed_files(path, identity):
    if not SHA.fullmatch(identity):
        raise ValueError("invalid frozen seed identity")
    files = bounded_json(path, compressed=True)
    if not isinstance(files, dict) or not files or len(files) > 50000:
        raise ValueError("invalid seed file count")
    if hashlib.sha256(canonical(files)).hexdigest() != identity:
        raise ValueError("frozen seed manifest identity mismatch")
    for name, entry in files.items():
        if not isinstance(name, str) or not name or len(name) > 1024 or "\\" in name or ":" in name:
            raise ValueError("unsafe seed path")
        if name.startswith("/") or any(p in {"", ".", ".."} for p in name.split("/")):
            raise ValueError("unsafe seed path")
        if not isinstance(entry, dict) or set(entry) != {"sha256", "bytes", "mtime_ns"}:
            raise ValueError("invalid seed entry")
        if not isinstance(entry["sha256"], str) or not SHA.fullmatch(entry["sha256"]):
            raise ValueError("invalid seed digest")
        if any(type(entry[k]) is not int or entry[k] < 0 for k in ("bytes", "mtime_ns")):
            raise ValueError("invalid seed size/mtime")
    return files


def source_set_hash(files):
    entries = [{"path": path, "archive_sha256": entry["sha256"], "bytes": entry["bytes"]}
               for path, entry in sorted(files.items())
               if inspection.family(path) or Path(path).suffix.lower() in inspection.CONTAINERS]
    return hashlib.sha256(b"pd2.public-archive-set.v1\0" + canonical(entries)).hexdigest()


def build_plan(files, seed, binary_sha256, cohort="character", max_captures=12, views=("default",), reference=None,
               strict_ledger=None, selected_origins=None):
    if not SHA.fullmatch(binary_sha256) or files.get("PerfectDark.exe", {}).get("sha256") != binary_sha256:
        raise ValueError("frozen client hash mismatch")
    if cohort not in inspection.FAMILIES or type(max_captures) is not int or not 1 <= max_captures <= 128:
        raise ValueError("invalid cohort/capture bound")
    if not views or len(views) > 4 or len(set(views)) != len(views) or any(
            not isinstance(v, str) or not re.fullmatch(r"[A-Za-z0-9_-]{1,32}", v) for v in views):
        raise ValueError("invalid requested view labels")
    if max_captures < len(views):
        raise ValueError("capture bound cannot fit one complete asset view group")
    archives = []
    for path, entry in sorted(files.items()):
        kind = inspection.family(path)
        if not kind and Path(path).suffix.lower() in inspection.CONTAINERS:
            kind = "container"
        if kind:
            archives.append(dict(path=path, family=kind, category="container" if kind == "container" else inspection.category(kind),
                                 archive_sha256=entry["sha256"], bytes=entry["bytes"],
                                 catalog_id=None, identity_verdict="pending_descriptor_or_provider_trace",
                                 consumed_member_verdict="pending", fidelity_verdict="pending"))
    if not archives:
        raise ValueError("seed contains no public typed archives")
    source_hash = source_set_hash(files)
    strict_reference = None
    if strict_ledger is not None:
        if strict_ledger.get('generation') != dict(binary_sha256=binary_sha256, source_sha256=source_hash):
            raise ValueError('strict ledger belongs to a different native generation')
        records = strict_ledger.get('assets')
        if not isinstance(records, list) or len(records) > 512:
            raise ValueError('strict ledger reference exceeds the bounded cohort')
        indexed = {row['path']: row for row in archives}
        seen = set()
        for record in records:
            origin = record.get('archive_origin')
            if origin not in indexed or origin in seen or record.get('archive_sha256') != indexed[origin]['archive_sha256']:
                raise ValueError('strict ledger archive identity missing, duplicate or mismatched')
            seen.add(origin)
            identity = record.get('catalog_id')
            if identity and (not isinstance(identity, str) or not inspection.CATALOG.fullmatch(identity)):
                raise ValueError('invalid strict descriptor catalog identity')
            indexed[origin].update(catalog_id=identity, identity_verdict='retained_descriptor_reference',
                strict_ledger_reference=dict(member_count=len(record.get('members', [])),
                    reported_load_verdict=record.get('load_verdict', 'pending'),
                    reported_dependency_sources=record.get('dependency_sources_verdict', 'pending'),
                    artifact_count=sum(len(r.get('artifacts', [])) for r in record.get('consumer_evidence', []))))
        strict_reference = dict(scope='Retained same-generation ledger reference only; metadata preparation does not reverify payloads, native artifacts or fidelity.',
            referenced_archives=len(seen), reported_native_load_pass=sum(r.get('load_verdict') == 'pass' for r in records),
            reported_dependency_source_pass=sum(r.get('dependency_sources_verdict') == 'pass' for r in records),
            other_top_level_archives=len(archives)-len(seen), fidelity_verdict='pending')
    candidates = [row for row in archives if row["family"] == cohort]
    if selected_origins is not None:
        selected = set(selected_origins)
        if not 1 <= len(selected) <= 8 or len(selected) != len(selected_origins) or not selected <= {r['path'] for r in candidates}:
            raise ValueError('select one to eight unique exact archive paths in the requested cohort')
        candidates = [row for row in candidates if row['path'] in selected]
    slots = []
    for row in candidates:
        if len(slots) + len(views) > max_captures:
            break
        for view in views:
            slots.append(dict(slot_id="capture-%04d" % len(slots), asset_path=row["path"],
                              catalog_id=row['catalog_id'], archive_sha256=row["archive_sha256"], family=cohort,
                              requested_view=view, status="pending_capture_and_native_identity",
                              image=None, image_sha256=None, checks={"native_draw": "pending", "visual_review": "pending"}))
    native_reference = None
    if reference is not None:
        if reference.get("client_sha256", "").lower() != binary_sha256 or reference.get("source_seed") != seed:
            raise ValueError("operational reference belongs to different client/seed")
        records = reference.get("records")
        if not isinstance(records, list) or len(records) > 512:
            raise ValueError("invalid operational reference size")
        if len({r.get("id") for r in records}) != len(records):
            raise ValueError("duplicate operational reference identity")
        native_reference = dict(scope="Hydration only; no source-member/capture promotion or filename-to-ID inference.",
                                passed=reference.get("passed"), failed=reference.get("failed"), untested=reference.get("untested"),
                                client_sha256=binary_sha256, source_seed=seed, log=reference.get("log"),
                                records=records)
    return dict(schema="pd2.asset-inspection-preparation.v1", source_seed=seed,
                generation=dict(binary_sha256=binary_sha256, source_sha256=source_hash),
                source_hash_domain="pd2.public-archive-set.v1 NUL + canonical sorted path/archive_sha256/bytes list for typed/package archives; excludes native caches, binary and ROM.",
                metadata_only=True, payload_blobs_reverified=False, member_inventory_complete=False,
                inventory_scope="Top-level typed file identities from retained seed; nested members/catalog IDs not inventoried.",
                archives=archives, family_counts=dict(Counter(row["family"] for row in archives)),
                native_reference=native_reference,
                strict_ledger_reference=strict_reference,
                capture_plan=dict(cohort=cohort, cohort_archives=len(candidates), requested_views=list(views),
                                  selected_assets=len(slots) // len(views), slots=slots,
                                  capture_bytes_reserved=len(slots) * 32 * 1024**2,
                                  omitted_assets=len(candidates) - len(slots) // len(views),
                                  automatic_capture_supported=False,
                                  prerequisite="Native identity/read trace and supported selector/camera path, then parent window and authorized executor.",
                                  runtime_started=False, captures_created=0))


def contact_sheet(plan):
    escape = html.escape
    cards = []
    for slot in plan["capture_plan"]["slots"]:
        cards.append('<article><div class="pending">Pending capture</div><h2>' + escape(slot["slot_id"]) +
                     ' · ' + escape(slot["requested_view"]) + '</h2><p>' + escape(slot["asset_path"]) +
                     '</p><code>' + escape(slot["archive_sha256"]) + '</code><p>Catalog: ' +
                     escape(slot['catalog_id'] or 'pending native/descriptor identity') +
                     '</p><p>Native draw and visual review pending.</p></article>')
    return ('<!doctype html><html lang="en"><meta charset="utf-8"><title>PD asset capture preparation</title>'
            '<style>body{font:15px system-ui;margin:32px;background:#101722;color:#eef3fa}main{display:grid;'
            'grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:16px}article{border:1px solid #52647b;padding:16px;'
            'overflow-wrap:anywhere}h2{font-size:16px}.pending{height:180px;display:grid;place-items:center;background:#202c3d}'
            'code{font-size:11px}p{line-height:1.5}</style><h1>Capture preparation: every slot pending</h1>'
            '<p>Metadata only. No images, native member proofs or visual approvals were created.</p><p>Frozen client: <code>' +
            escape(plan["generation"]["binary_sha256"]) + '</code></p><p>Source seed: <code>' +
            escape(plan["source_seed"]) + '</code></p><main>' + ''.join(cards) + '</main></html>\n')


def write_plan(output, plan, max_bytes=8 * 1024**2):
    files = {"preparation.json": (json.dumps(plan, indent=2) + "\n").encode(),
             "contact-sheet-pending.html": contact_sheet(plan).encode()}
    if max_bytes <= 0 or sum(map(len, files.values())) > max_bytes:
        raise ValueError("preparation output budget exceeded; no output created")
    Path(output).mkdir(parents=True, exist_ok=False)
    for name, content in files.items():
        with (Path(output) / name).open("xb") as target:
            target.write(content)
    return sum(map(len, files.values()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("seed_manifest", type=Path)
    parser.add_argument("--seed-id", required=True)
    parser.add_argument("--binary-sha256", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--character-ledger", type=Path)
    parser.add_argument("--strict-ledger", type=Path)
    parser.add_argument("--selected-origin", action="append")
    parser.add_argument("--cohort", choices=inspection.FAMILIES, default="character")
    parser.add_argument("--max-captures", type=int, default=12)
    parser.add_argument("--view", action="append", default=None)
    args = parser.parse_args()
    try:
        files = seed_files(args.seed_manifest, args.seed_id)
        reference = bounded_json(args.character_ledger, limit=4 * 1024**2) if args.character_ledger else None
        ledger = bounded_json(args.strict_ledger, limit=4 * 1024**2) if args.strict_ledger else None
        plan = build_plan(files, args.seed_id, args.binary_sha256.lower(), args.cohort,
                          args.max_captures, args.view or ("default",), reference, ledger, args.selected_origin)
        if args.strict_ledger:
            plan['strict_ledger_reference'].update(ledger_sha256=hashlib.sha256(args.strict_ledger.read_bytes()).hexdigest(),
                                                   ledger_path=str(args.strict_ledger))
        if args.character_ledger:
            plan["native_reference"]["ledger_sha256"] = hashlib.sha256(args.character_ledger.read_bytes()).hexdigest()
            plan["native_reference"]["ledger_path"] = str(args.character_ledger)
        size = write_plan(args.output, plan)
    except (OSError, ValueError, TypeError, AttributeError) as error:
        parser.exit(1, str(error) + "\n")
    print(json.dumps({"top_level_archives": len(plan["archives"]), "capture_slots": len(plan["capture_plan"]["slots"]),
                      "output_bytes": size, "metadata_only": True, "member_inventory_complete": False}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
