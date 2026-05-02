#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Catalog Gate 3 Arenas F11: Extract MP arena records from
src/game/mplayer/setup.c + port/src/assetcatalog_base.c into
base/arenas.pdbase JSON archive.

Companion design doc:
    context/audits/catalog-gate3-arenas-data-2026-05-02.md
                  Sections C, D, F (.pdbase format + manager schema +
                  F11 step).

Usage:
    python3 devtools/extract_arenas_pdbase.py \\
        --setup src/game/mplayer/setup.c \\
        --basereg port/src/assetcatalog_base.c \\
        --output base/arenas.pdbase

Algorithm:
  1. Parse `g_MpArenas[]` from setup.c, capturing each positional
     initializer (stagenum_sym, requirefeature_int, name_langid_sym).
     Resolve VERSION_JPN_FINAL ternaries by picking the NTSC branch.
  2. Parse `s_ArenaNames[47]` from assetcatalog_base.c, capturing
     the per-arena slug string literal.
  3. Parse `s_ArenaGroupMap[5]` from assetcatalog_base.c, capturing
     `{first, count, category}` triples; expand into a per-arena
     category lookup.
  4. Compute load_mode per arena: ARENA_LOADMODE_CANVAS for the
     "Solo Missions" group (audit Section H.1 invariant); otherwise
     ARENA_LOADMODE_PLAYABLE.  Mirrors the registration code at
     port/src/assetcatalog_base.c:723-725.
  5. Emit a JSON record per arena:
       { id, arena_index, slug, category, stagenum, requirefeature,
         name_langid, load_mode }
     - id is "base:arena_<slug>" (arena_index is the runtime_index in
       the catalog row).
     - `stagenum`, `name_langid`, `load_mode` stay as symbolic enum
       names per Path B.
  6. Emit base/arenas.pdbase JSON with manifest envelope:
       { "pdbase_version": 1, "type": "arenas", "arenas": [ ... ] }

Determinism: same source bytes -> same output bytes. Idempotent.
"""

import argparse
import json
import os
import re
import sys


RE_GMPARENAS_OPEN = re.compile(r"struct mparena g_MpArenas\[\]\s*=\s*\{")
RE_S_ARENANAMES_OPEN = re.compile(
    r"static\s+const\s+char\s+\*const\s+s_ArenaNames\[\d+\]\s*=\s*\{")
RE_S_ARENAGROUPMAP_OPEN = re.compile(
    r"\}\s+s_ArenaGroupMap\[\]\s*=\s*\{")
RE_VERSION_JPN_TERNARY = re.compile(
    r"\(\s*VERSION\s*==\s*VERSION_JPN_FINAL\s*\?\s*[A-Z0-9_]+\s*:\s*([A-Z0-9_]+)\s*\)")


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


def select_ntsc_ternary(block_text):
    """Replace `(VERSION == VERSION_JPN_FINAL ? L_JP : L_NTSC)` with the
    NTSC branch.  We don't ship JP, so the JP side is dropped."""
    return RE_VERSION_JPN_TERNARY.sub(lambda m: m.group(1), block_text)


def parse_initializer_rows(block_text):
    """Yield each top-level `{ ... }` row inside the array body.
    Returns a list of inner strings."""
    rows = []
    pos = 0
    while pos < len(block_text):
        # Skip whitespace, commas, and comments.
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
                end = block_text.find("*/", pos)
                pos = end + 2 if end >= 0 else len(block_text)
                continue
            break
        if pos >= len(block_text):
            break
        if block_text[pos] != "{":
            pos += 1
            continue
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
        rows.append(block_text[start:end])
        pos = end + 1
    return rows


def split_initializer_fields(inner):
    """Split a single initializer's inner text into top-level
    comma-separated fields. Skips commas inside nested braces / parens
    so VERSION ternaries are kept together."""
    fields = []
    depth = 0
    start = 0
    pos = 0
    while pos < len(inner):
        c = inner[pos]
        if c in "({[":
            depth += 1
        elif c in ")}]":
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
    # Strip embedded comments.
    block_re = re.compile(r"/\*.*?\*/", re.DOTALL)
    line_re = re.compile(r"//.*$", re.MULTILINE)
    cleaned = []
    for f in fields:
        f = block_re.sub("", f)
        f = line_re.sub("", f)
        cleaned.append(f.strip())
    return cleaned


def parse_int_or_symbol(s):
    s = s.strip()
    try:
        return int(s, 0)
    except ValueError:
        return s


def parse_g_mparenas(setup_path):
    """Parse g_MpArenas[] into a list of dicts. Index in the list IS
    the arena_index."""
    text = open(setup_path, "r", encoding="utf-8").read()
    block = extract_block(text, RE_GMPARENAS_OPEN)
    if block is None:
        raise SystemExit("g_MpArenas[] not found in " + setup_path)
    block = select_ntsc_ternary(block)
    rows = parse_initializer_rows(block)
    entries = []
    for arena_index, inner in enumerate(rows):
        fields = split_initializer_fields(inner)
        if len(fields) < 3:
            sys.stderr.write(
                "warning: g_MpArenas[%d] has %d fields (expected 3), skipping\n"
                % (arena_index, len(fields)))
            continue
        entries.append({
            "arena_index": arena_index,
            "stagenum": parse_int_or_symbol(fields[0]),
            "requirefeature": parse_int_or_symbol(fields[1]),
            "name_langid": parse_int_or_symbol(fields[2]),
        })
    return entries


def parse_s_arenanames(basereg_path):
    """Parse s_ArenaNames[47] into list of slug strings (NULL slots
    become None)."""
    text = open(basereg_path, "r", encoding="utf-8").read()
    block = extract_block(text, RE_S_ARENANAMES_OPEN)
    if block is None:
        raise SystemExit("s_ArenaNames[] not found in " + basereg_path)
    # The contents are comma-separated string literals (or NULL).
    # Strip /* ... */ index comments first.
    block = re.sub(r"/\*.*?\*/", "", block, flags=re.DOTALL)
    block = re.sub(r"//.*$", "", block, flags=re.MULTILINE)
    raw_tokens = [t.strip() for t in block.split(",")]
    slugs = []
    for tok in raw_tokens:
        tok = tok.strip()
        if not tok:
            continue
        if tok == "NULL":
            slugs.append(None)
        elif tok.startswith('"') and tok.endswith('"'):
            slugs.append(tok[1:-1])
        else:
            sys.stderr.write(
                "warning: s_ArenaNames token not recognised: %r\n" % tok)
            slugs.append(None)
    return slugs


def parse_s_arenagroupmap(basereg_path):
    """Parse s_ArenaGroupMap[] into list of (first, count, category)."""
    text = open(basereg_path, "r", encoding="utf-8").read()
    block = extract_block(text, RE_S_ARENAGROUPMAP_OPEN)
    if block is None:
        raise SystemExit("s_ArenaGroupMap[] not found in " + basereg_path)
    rows = parse_initializer_rows(block)
    groups = []
    for inner in rows:
        fields = split_initializer_fields(inner)
        if len(fields) < 3:
            continue
        try:
            first = int(fields[0].strip(), 0)
            count = int(fields[1].strip(), 0)
        except ValueError:
            continue
        cat_raw = fields[2].strip()
        if cat_raw.startswith('"') and cat_raw.endswith('"'):
            category = cat_raw[1:-1]
        else:
            category = cat_raw
        groups.append((first, count, category))
    return groups


def category_for_arena_index(arena_index, group_map):
    for first, count, category in group_map:
        if first <= arena_index < first + count:
            return category
    return ""


def load_mode_for_category(category):
    """Mirror port/src/assetcatalog_base.c:723-725: Solo Missions group
    gets ARENA_LOADMODE_CANVAS so Grid sessions suppress chr / AI /
    cutscene side-effects. Everything else is ARENA_LOADMODE_PLAYABLE."""
    if category == "Solo Missions":
        return "ARENA_LOADMODE_CANVAS"
    return "ARENA_LOADMODE_PLAYABLE"


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--setup", required=True,
                   help="src/game/mplayer/setup.c")
    p.add_argument("--basereg", required=True,
                   help="port/src/assetcatalog_base.c")
    p.add_argument("--output", required=True,
                   help="base/arenas.pdbase")
    args = p.parse_args()

    arenas = parse_g_mparenas(args.setup)
    slugs = parse_s_arenanames(args.basereg)
    group_map = parse_s_arenagroupmap(args.basereg)

    if len(arenas) != len(slugs):
        sys.stderr.write(
            "warning: g_MpArenas has %d entries, s_ArenaNames has %d "
            "(should match)\n" % (len(arenas), len(slugs)))

    arena_records = []
    for entry in arenas:
        idx = entry["arena_index"]
        if idx < len(slugs) and slugs[idx] is not None:
            slug = slugs[idx]
        else:
            # NULL slot -- skip per the registration code's
            # `if (!s_ArenaNames[idx]) continue;` filter.
            continue
        category = category_for_arena_index(idx, group_map)
        load_mode = load_mode_for_category(category)
        rec = {
            "id": "base:arena_%s" % slug,
            "arena_index": idx,
            "slug": slug,
            "category": category,
            "stagenum": entry["stagenum"],
            "requirefeature": entry["requirefeature"]
            if isinstance(entry["requirefeature"], int)
            else 0,
            "name_langid": entry["name_langid"],
            "load_mode": load_mode,
        }
        arena_records.append(rec)

    arena_records.sort(key=lambda r: r["arena_index"])

    out = {
        "pdbase_version": 1,
        "type": "arenas",
        "arenas": arena_records,
    }

    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    with open(args.output, "w", encoding="utf-8", newline="\n") as f:
        json.dump(out, f, indent=2, ensure_ascii=False)
        f.write("\n")
    sys.stderr.write(
        "wrote %d arena records to %s\n"
        % (len(arena_records), args.output))


if __name__ == "__main__":
    main()
