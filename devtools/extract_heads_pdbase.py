#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Catalog Gate 3 F11: Extract head records from src/game/modeldata/robot.c
into base/heads.pdbase JSON archive.

Companion design doc: context/audits/catalog-gate3-heads-data-2026-05-01.md
                      Sections C, D, F (.pdbase format + manager schema +
                      F11 step).

Usage:
    python3 devtools/extract_heads_pdbase.py \\
        --robot src/game/modeldata/robot.c \\
        --mplayer src/game/mplayer/mplayer.c \\
        --basereg port/src/assetcatalog_base.c \\
        --output base/heads.pdbase

Algorithm:
  1. Parse `g_HeadsAndBodies[]` from robot.c, capturing each positional
     initializer. Filter to HEAD entries (`unk00_01 == 1`).
  2. Parse `g_MpHeads[]` from mplayer.c to build mp_idx -> headnum.
  3. Parse `s_BaseHeads[]` from assetcatalog_base.c to build
     mp_idx -> slug.
  4. For each HEAD entry, emit a JSON record:
       { id, headnum, ismale, unk00_01, type, height, filenum,
         scale, animscale }
     - id is "base:<slug>" if the head has an mp_idx + slug entry, else
       "base:sp_head_<headnum>" (mirrors the catalog SP-fallback).
     - `type` and `filenum` stay as symbolic enum names per Path B.
  5. Emit base/heads.pdbase JSON with manifest envelope:
       { "pdbase_version": 1, "type": "heads", "heads": [ ... ] }

Determinism: same source bytes -> same output bytes. The script is
idempotent.
"""

import argparse
import json
import os
import re
import sys


RE_GHEADSANDBODIES_OPEN = re.compile(r"struct headorbody g_HeadsAndBodies\[\] = \{")
RE_GMPHEADS_OPEN = re.compile(r"struct mphead g_MpHeads\[\] = \{")
RE_S_BASEHEADS_OPEN = re.compile(r"\}\s+s_BaseHeads\[\]\s*=\s*\{")
RE_INDEX_COMMENT = re.compile(r"/\*\s*0x([0-9a-fA-F]+)\s*\*/")
RE_VERSION_BLOCK = re.compile(
    r"#if\s+VERSION\s*>=\s*VERSION_NTSC_1_0(.*?)#else(.*?)#endif",
    re.DOTALL)
RE_VERSION_JPN_GUARD = re.compile(
    r"#if\s+VERSION\s*!=\s*VERSION_JPN_FINAL(.*?)#endif",
    re.DOTALL)


def extract_block(text, open_pat):
    """Locate the opening pattern, return the substring between matching {}."""
    m = open_pat.search(text)
    if not m:
        return None
    start = m.end()  # position right after the opening `{`
    depth = 1
    pos = start
    while pos < len(text) and depth > 0:
        c = text[pos]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return text[start:pos]
        pos += 1
    return None


def select_ntsc(block_text):
    """Pick NTSC branch from #if VERSION >= VERSION_NTSC_1_0 / #else /
    #endif blocks; drop the #else side. Drop JPN_FINAL #if blocks
    entirely (we don't ship JP)."""
    # Replace VERSION >= VERSION_NTSC_1_0 with the NTSC branch.
    while True:
        m = RE_VERSION_BLOCK.search(block_text)
        if not m:
            break
        ntsc_branch, _pre1_branch = m.group(1), m.group(2)
        block_text = block_text[:m.start()] + ntsc_branch + block_text[m.end():]
    # Drop VERSION != VERSION_JPN_FINAL guards by keeping the body
    # (we are not building JP). The body is line(s) between #if and #endif.
    while True:
        m = RE_VERSION_JPN_GUARD.search(block_text)
        if not m:
            break
        body = m.group(1)
        block_text = block_text[:m.start()] + body + block_text[m.end():]
    return block_text


def parse_initializer_rows(block_text):
    """Yield each top-level `{ ... }` row inside the array body. Returns a
    list of (index_comment_or_None, raw_inner)."""
    rows = []
    pos = 0
    while pos < len(block_text):
        # Skip whitespace + line/block comments + index comment
        while pos < len(block_text):
            c = block_text[pos]
            if c.isspace() or c == ",":
                pos += 1
                continue
            if block_text[pos:pos+2] == "//":
                end = block_text.find("\n", pos)
                pos = end + 1 if end >= 0 else len(block_text)
                continue
            if block_text[pos:pos+2] == "/*":
                # Could be the index marker we want to keep
                end = block_text.find("*/", pos)
                # We do not capture the index here; we read it just before
                # the next `{`. Skip the comment.
                pos = end + 2 if end >= 0 else len(block_text)
                continue
            break
        if pos >= len(block_text):
            break
        if block_text[pos] != "{":
            # Could be a stray identifier; advance a char to avoid infinite loop.
            pos += 1
            continue
        # Find matching closing brace.
        depth = 1
        start = pos + 1
        end = start
        while end < len(block_text) and depth > 0:
            ch = block_text[end]
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    break
            end += 1
        inner = block_text[start:end]
        # Look back for /*0xNN*/ comment that decorates this row.
        look_start = max(0, pos - 80)
        prefix = block_text[look_start:pos]
        m = RE_INDEX_COMMENT.search(prefix)
        idx_hint = int(m.group(1), 16) if m else None
        rows.append((idx_hint, inner))
        pos = end + 1
    return rows


def split_initializer_fields(inner):
    """Split a single initializer's inner text into top-level
    comma-separated fields. Skips commas inside nested braces / parens
    (none expected in heads, but defensive)."""
    fields = []
    depth = 0
    start = 0
    pos = 0
    while pos < len(inner):
        c = inner[pos]
        if c == "(" or c == "{" or c == "[":
            depth += 1
        elif c == ")" or c == "}" or c == "]":
            depth -= 1
        elif c == "," and depth == 0:
            fields.append(inner[start:pos].strip())
            start = pos + 1
        elif inner[pos:pos+2] == "/*":
            end = inner.find("*/", pos)
            pos = end + 2 if end >= 0 else len(inner)
            continue
        elif inner[pos:pos+2] == "//":
            end = inner.find("\n", pos)
            pos = end + 1 if end >= 0 else len(inner)
            continue
        pos += 1
    tail = inner[start:].strip()
    if tail:
        fields.append(tail)
    # Strip embedded /*...*/ comments from each field (e.g. the leading
    # /*0x00*/ index marker that decorates each row's first field).
    cleaned = []
    block_re = re.compile(r"/\*.*?\*/", re.DOTALL)
    line_re = re.compile(r"//.*$", re.MULTILINE)
    for f in fields:
        f = block_re.sub("", f)
        f = line_re.sub("", f)
        cleaned.append(f.strip())
    return cleaned


def parse_int_or_symbol(s):
    """Return int if s is a numeric literal, else the symbolic name."""
    s = s.strip()
    try:
        return int(s, 0)
    except ValueError:
        return s


def parse_float_or_int(s):
    """Return float for a numeric literal, supporting f-suffix."""
    s = s.strip().rstrip("f").rstrip("F")
    try:
        return float(s)
    except ValueError:
        try:
            return int(s, 0)
        except ValueError:
            return s


def parse_g_headsandbodies(robot_path):
    """Parse g_HeadsAndBodies[] into a list of dicts indexed by headnum."""
    text = open(robot_path, "r", encoding="utf-8").read()
    block = extract_block(text, RE_GHEADSANDBODIES_OPEN)
    if block is None:
        raise SystemExit("g_HeadsAndBodies[] not found in " + robot_path)
    block = select_ntsc(block)
    rows = parse_initializer_rows(block)

    # Field order per struct headorbody (src/include/types.h:3109):
    #   ismale:1, unk00_01:1, canvaryheight:1, type:3, height:8,
    #   filenum, scale, animscale, modeldef, handfilenum
    # The C initializer uses 10 positional fields (modeldef = 0 always).
    headnum = 0
    entries = []
    for idx_hint, inner in rows:
        fields = split_initializer_fields(inner)
        if len(fields) < 10:
            # Unexpected shape -- log and skip.
            sys.stderr.write(
                "warning: g_HeadsAndBodies[%d] has %d fields (expected 10), skipping\n"
                % (headnum, len(fields)))
            headnum += 1
            continue
        entry = {
            "headnum": headnum,
            "ismale": parse_int_or_symbol(fields[0]),
            "unk00_01": parse_int_or_symbol(fields[1]),
            "canvaryheight": parse_int_or_symbol(fields[2]),
            "type": parse_int_or_symbol(fields[3]),
            "height": parse_int_or_symbol(fields[4]),
            "filenum": parse_int_or_symbol(fields[5]),
            "scale": parse_float_or_int(fields[6]),
            "animscale": parse_float_or_int(fields[7]),
            # fields[8] = modeldef (always 0 in initializer)
            "handfilenum": parse_int_or_symbol(fields[9]),
        }
        if idx_hint is not None and idx_hint != headnum:
            sys.stderr.write(
                "warning: index hint /*0x%x*/ != actual headnum %d\n"
                % (idx_hint, headnum))
        entries.append(entry)
        headnum += 1
    return entries


def parse_g_mpheads(mplayer_path):
    """Parse g_MpHeads[] into a list of (mp_idx, headnum_symbol)."""
    text = open(mplayer_path, "r", encoding="utf-8").read()
    block = extract_block(text, RE_GMPHEADS_OPEN)
    if block is None:
        raise SystemExit("g_MpHeads[] not found in " + mplayer_path)
    block = select_ntsc(block)
    rows = parse_initializer_rows(block)

    pairs = []
    for mp_idx, (_idx_hint, inner) in enumerate(rows):
        fields = split_initializer_fields(inner)
        if len(fields) < 1:
            continue
        head_sym = fields[0].strip()
        pairs.append((mp_idx, head_sym))
    return pairs


def parse_s_baseheads(basereg_path):
    """Parse s_BaseHeads[] into mp_idx -> slug mapping."""
    text = open(basereg_path, "r", encoding="utf-8").read()
    block = extract_block(text, RE_S_BASEHEADS_OPEN)
    if block is None:
        raise SystemExit("s_BaseHeads[] not found in " + basereg_path)
    rows = parse_initializer_rows(block)
    mapping = {}
    for _idx_hint, inner in rows:
        fields = split_initializer_fields(inner)
        if len(fields) < 2:
            continue
        try:
            mp_idx = int(fields[0].strip(), 0)
        except ValueError:
            continue
        slug = fields[1].strip().strip('"')
        mapping[mp_idx] = slug
    return mapping


def head_symbol_to_int(sym, head_constants):
    """HEAD_FOO -> integer. Walks an inline table built from the
    HEAD_*  #define lines extracted from constants.h."""
    if isinstance(sym, int):
        return sym
    return head_constants.get(sym)


def build_head_constants(constants_path):
    """Pre-build a HEAD_* / BODY_* -> int table by scanning constants.h.
    The file uses both decimal and hex literals, sometimes referencing
    other already-defined constants (BODY_X = 0x5c). Iterate to fixed
    point so forward refs resolve."""
    text = open(constants_path, "r", encoding="utf-8").read()
    pat = re.compile(r"#define\s+(HEAD_\w+|BODY_\w+)\s+(.+?)\s*(?:/\*.*?\*/|//.*)?$",
                     re.MULTILINE)
    raw = {}
    for m in pat.finditer(text):
        name, value = m.group(1), m.group(2).strip()
        raw[name] = value
    table = {}
    progress = True
    while progress:
        progress = False
        for name, val in list(raw.items()):
            if name in table:
                continue
            try:
                table[name] = int(val, 0)
                progress = True
            except ValueError:
                # Try identifier alias.
                if val in table:
                    table[name] = table[val]
                    progress = True
    return table


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--robot", required=True)
    p.add_argument("--mplayer", required=True)
    p.add_argument("--basereg", required=True)
    p.add_argument("--constants", required=True)
    p.add_argument("--output", required=True)
    args = p.parse_args()

    head_constants = build_head_constants(args.constants)
    if not head_constants:
        sys.stderr.write("warning: no HEAD_* constants resolved from %s\n" % args.constants)

    entries = parse_g_headsandbodies(args.robot)
    mp_pairs = parse_g_mpheads(args.mplayer)
    base_slugs = parse_s_baseheads(args.basereg)

    # Build headnum -> slug from mp_idx -> headnum + mp_idx -> slug.
    headnum_to_slug = {}
    for mp_idx, head_sym in mp_pairs:
        if mp_idx not in base_slugs:
            continue
        head_int = head_symbol_to_int(head_sym, head_constants)
        if head_int is None:
            sys.stderr.write(
                "warning: unresolved HEAD symbol '%s' at mp_idx %d\n"
                % (head_sym, mp_idx))
            continue
        headnum_to_slug[head_int] = base_slugs[mp_idx]

    # Build the JSON head records. Filter to HEAD entries (unk00_01 == 1).
    head_records = []
    for e in entries:
        if e.get("filenum") in (0, "0", None):
            continue  # skip the trailing sentinel row at index 0x97
        unk = e.get("unk00_01", 0)
        if not (unk == 1 or unk == "1"):
            continue  # body slot, not a head
        slug = headnum_to_slug.get(e["headnum"])
        if slug:
            cid = "base:%s" % slug
        else:
            cid = "base:sp_head_%d" % e["headnum"]
        rec = {
            "id": cid,
            "headnum": e["headnum"],
            "ismale": int(e["ismale"]) if not isinstance(e["ismale"], str) else e["ismale"],
            "unk00_01": int(e["unk00_01"]) if not isinstance(e["unk00_01"], str) else e["unk00_01"],
            "type": e["type"],
            "height": e["height"],
            "filenum": e["filenum"],
            "scale": e["scale"],
            "animscale": e["animscale"],
        }
        head_records.append(rec)

    # Sort by headnum for deterministic output.
    head_records.sort(key=lambda r: r["headnum"])

    out = {
        "pdbase_version": 1,
        "type": "heads",
        "heads": head_records,
    }

    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    with open(args.output, "w", encoding="utf-8", newline="\n") as f:
        json.dump(out, f, indent=2, ensure_ascii=False)
        f.write("\n")
    sys.stderr.write("wrote %d head records to %s\n" % (len(head_records), args.output))


if __name__ == "__main__":
    main()
