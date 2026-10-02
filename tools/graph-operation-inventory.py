#!/usr/bin/env python3
"""Check the reviewed operation ledger and extract bounded lexical C evidence.

This is a source inventory, not a C parser, graph compiler or parity verifier.
No source or ledger is modified. Scan only files named by reviewed references.
"""
from __future__ import annotations

import argparse
import json
import re
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LEDGER = Path("context/designs/modding/graph-operation-inventory-2026-10-01.json")
REQUIRED = {
    "id", "cohort", "modules", "parameters", "inputs", "outputs", "state",
    "phases", "cancellation", "timing", "network_determinism", "catalog_refs",
    "lifetime_versioning", "migration", "evidence", "dependencies", "unresolved",
}


def mask_c(text: str) -> str:
    """Blank comments/literals while preserving offsets and newlines."""
    pattern = r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\[\s\S]|[^"\\])*"|\'(?:\\[\s\S]|[^\'\\])*\''
    return re.sub(pattern, lambda m: re.sub(r"[^\n]", " ", m.group()), text)


def functions(text: str) -> list[dict]:
    """Find conventional C/C++ free definitions; macros/classes need review."""
    clean = mask_c(text)
    pattern = re.compile(
        r"(?m)^[ \t]*(?:(?:extern[ \t]+)[ \t]*)?"
        r"[A-Za-z_][\w \t*]*?[ \t*]+([A-Za-z_]\w*)[ \t]*"
        r"\([^;{}]*\)[ \t\r\n]*\{"
    )
    result = []
    matches = [match for match in pattern.finditer(clean)
               if match.group(1) not in {"if", "for", "while", "switch"}]
    for index, match in enumerate(matches):
        start = clean.index("{", match.start(), match.end())
        limit = matches[index + 1].start() if index + 1 < len(matches) else len(clean)
        depth, end = 1, start + 1
        while end < limit and depth:
            depth += (clean[end] == "{") - (clean[end] == "}")
            end += 1
        boundary = "balanced lexical braces; conditional arms not evaluated"
        if depth:
            # Native version guards can put two alternative opening braces
            # before one shared closing brace. Keep both arms as lexical
            # evidence, bounded by the conventional column-zero function end.
            closings = list(re.finditer(r"(?m)^}[ \t]*$", clean[start:limit]))
            if not closings:
                continue
            end = start + closings[-1].end()
            boundary = "column-zero end; conditional arms have unbalanced lexical braces"
        result.append({"symbol": match.group(1), "line": text.count("\n", 0, match.start()) + 1,
                       "start": match.start(), "end": end, "body": clean[start:end], "boundary": boundary})
    return result


def source_path(root: Path, relative: str) -> Path:
    if not isinstance(relative, str) or "\\" in relative:
        raise ValueError("source path must be a relative slash path")
    candidate = (root / relative).resolve()
    if Path(relative).is_absolute() or not candidate.is_relative_to(root.resolve()):
        raise ValueError("source reference escapes repository")
    if candidate.suffix not in {".c", ".cpp", ".h"}:
        raise ValueError("source reference must name C/C++ source/header")
    return candidate


def inspect(ledger: dict, root: Path) -> tuple[list[str], dict]:
    errors, sources, observations, ids = [], {}, [], set()
    if not isinstance(ledger, dict):
        return ["ledger must be an object"], {}
    if ledger.get("schema") != "pd.graph_operation_inventory.v1":
        errors.append("unsupported inventory schema")
    operations = ledger.get("operations")
    if not isinstance(operations, list) or not operations:
        return errors + ["operations must be a nonempty array"], {}
    for operation in operations:
        if not isinstance(operation, dict):
            errors.append("operation must be an object")
            continue
        name = operation.get("id", "<missing>")
        if not isinstance(name, str):
            errors.append("operation id must be a string")
            continue
        if name in ids:
            errors.append(f"{name}: duplicate operation id")
        ids.add(name)
        missing = REQUIRED - operation.keys()
        if missing:
            errors.append(f"{name}: missing {', '.join(sorted(missing))}")
        for key in REQUIRED - {"id", "migration"}:
            if not operation.get(key):
                errors.append(f"{name}: empty {key}")
        migration = operation.get("migration", {})
        if not isinstance(migration, dict) or migration.get("status") not in {
            "native_only", "v1_parameter_adapter", "v2_custom_offline_partial", "dependency_inventory"
        }:
            errors.append(f"{name}: invalid migration status")
        parameters = operation.get("parameters", [])
        if not isinstance(parameters, list):
            errors.append(f"{name}: parameters must be an array")
            parameters = []
        for parameter in parameters:
            if not isinstance(parameter, dict) or not {"name", "type", "unit", "semantic_status"} <= parameter.keys():
                errors.append(f"{name}: parameter needs name/type/unit/semantic_status")
            elif parameter["semantic_status"] not in {"observed", "proposed", "unresolved"}:
                errors.append(f"{name}: invalid parameter semantic_status")
        evidence = operation.get("evidence", [])
        if not isinstance(evidence, list):
            errors.append(f"{name}: evidence must be an array")
            continue
        for reference in evidence:
            try:
                path, symbol = reference["path"], reference["symbol"]
                if not isinstance(symbol, str) or not re.fullmatch(r"[A-Za-z_]\w*", symbol):
                    raise ValueError("invalid source symbol")
                if path not in sources:
                    source = source_path(root, path).read_text(encoding="utf-8")
                    sources[path] = (source, functions(source))
                source, definitions = sources[path]
                matches = [entry for entry in definitions if entry["symbol"] == symbol]
                if len(matches) != 1:
                    raise ValueError(f"expected one definition, found {len(matches)}")
                entry = matches[0]
                body = entry["body"]
                for token in reference.get("requires", []):
                    if not isinstance(token, str) or not re.fullmatch(r"[A-Za-z_]\w*", token):
                        raise ValueError("requires must contain identifier tokens")
                    if not re.search(r"\b" + re.escape(token) + r"\b", body):
                        raise ValueError(f"reviewed token missing: {token}")
                observations.append({"operation": name, "path": path, "symbol": symbol,
                    "line": entry["line"],
                    "boundary": entry["boundary"],
                    "globals": sorted(set(re.findall(r"\bg_[A-Za-z_]\w*", body))),
                    "state_fields": sorted(set(re.findall(r"\b(?:hand|projectile|weapon|obj|ctrl)->([A-Za-z_]\w*)", body))),
                    "timing_tokens": sorted(set(re.findall(r"\b(?:TICKS|LVUPDATE\w*|lvupdate\w*|\w*(?:timer|frames|rpm)\w*)\b", body, re.I))),
                    "calls": sorted(set(re.findall(r"\b([A-Za-z_]\w*)\s*\(", body)) - {"if", "while", "for", "switch", "sizeof"})})
            except (KeyError, TypeError, OSError, ValueError) as exc:
                errors.append(f"{name}: invalid evidence {reference!r}: {exc}")
    # Callers are bounded to referenced source files; absence is not no callers.
    for entry in observations:
        callers = []
        for path, (source, definitions) in sources.items():
            for caller in definitions:
                for match in re.finditer(r"\b" + re.escape(entry["symbol"]) + r"\s*\(", caller["body"]):
                    callers.append({"path": path, "symbol": caller["symbol"],
                                    "definition_line": caller["line"]})
                    break
        entry["callers_in_reviewed_files"] = callers
    return errors, {"schema": "pd.graph_operation_observations.v1",
        "scope": "Lexical findings in ledger-referenced files only; not semantic or exhaustive call graph proof.",
        "source_files": sorted(sources), "definitions": observations,
        "counts": {"operations": len(operations), "references": len(observations),
                   "source_files": len(sources), "cohorts": dict(sorted(Counter(
                       operation.get("cohort", "invalid") for operation in operations if isinstance(operation, dict)).items()))}}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--ledger", type=Path, default=LEDGER)
    parser.add_argument("--scan", action="store_true", help="emit current bounded observations as JSON")
    args = parser.parse_args()
    try:
        path = args.ledger if args.ledger.is_absolute() else args.root / args.ledger
        ledger = json.loads(path.read_text(encoding="utf-8"))
        errors, report = inspect(ledger, args.root)
    except (OSError, ValueError, TypeError) as exc:
        print(f"FAIL: {exc}")
        return 1
    if errors:
        for error in errors:
            print(f"FAIL: {error}")
        return 1
    if args.scan:
        print(json.dumps(report, indent=2))
    else:
        print("PASS: " + json.dumps(report["counts"], sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
