#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Catalog Gate 3 Bodies F11: Extract body records from src/game/modeldata/robot.c
into base/bodies.pdbase JSON archive.

Companion design doc: context/audits/catalog-gate3-bodies-data-2026-05-02.md
                      Sections C, D, F (.pdbase format + manager schema +
                      F11 step).

Usage:
    python3 devtools/extract_bodies_pdbase.py \\
        --robot src/game/modeldata/robot.c \\
        --mplayer src/game/mplayer/mplayer.c \\
        --basereg port/src/assetcatalog_base.c \\
        --constants src/include/constants.h \\
        --output base/bodies.pdbase

Algorithm (mirrors extract_heads_pdbase.py with body-side filtering):
  1. Parse `g_HeadsAndBodies[]` from robot.c, capturing each positional
     initializer.
  2. Parse `g_MpBodies[]` from mplayer.c to build mp_idx -> bodynum.
  3. Parse `s_BaseBodies[]` from assetcatalog_base.c to build
     mp_idx -> slug.
  4. For each entry, classify:
     - If bodynum is referenced by g_MpBodies AND has a slug in
       s_BaseBodies, emit `base:<slug>` (named MP body).
     - If unk00_01 == 0 AND filenum != 0 AND not BODY_TESTCHR AND not
       already covered as a named MP body, emit `base:sp_body_<bodynum>`.
     - If unk00_01 == 1 AND not in g_MpBodies, skip (it's a head; the
       heads extractor handles it).
     The integrated-head bodies (DRCARROLL has unk00_01==1 and IS in
     g_MpBodies as mp_idx 62) get the named branch and carry their
     unk00_01 flag forward so the body.c warning gate still fires
     correctly.
  5. Emit base/bodies.pdbase JSON with manifest envelope:
       { "pdbase_version": 1, "type": "bodies", "bodies": [ ... ] }

Determinism: same source bytes -> same output bytes. The script is
idempotent.
"""

import argparse
import json
import os
import re
import sys


# S593g-followup (2026-05-02): bodynums whose body model has the head
# geometry baked in. The original game's data only flags Dr Caroll
# (bodynum 107) with unk00_01==1, but the PC port's body.c warning
# gate (src/game/body.c:416-417) treats unk00_01 as the
# integrated-head indicator. To make Skedar / EyeSpy / MiniSkedar /
# SkedarKing match the PC port's existing menu code (mpBodyHasIntegratedHead
# at src/game/mplayer/setup.c:2457 explicitly lists "Dr Caroll, Eye Spy,
# Skedar, etc.") and to suppress the head_canon=NULL warning flood that
# saturated the log during the swarm 256-bot cycle, override unk00_01=1
# at emission time for these bodynums.
#
# Without this override Skedar spawns produce 256 head_canon WARNING lines
# per cycle change; with 256 fopen/fwrite/fclose calls in one frame the
# log writer stalls and crashes the spawn flood (Mike's 2026-05-02
# playtest log ended abruptly at 02:15.50 mid-flood).
INTEGRATED_HEAD_BODYNUMS = {
    92,   # Skedar       (FILE_CSKEDAR)
    108,  # EyeSpy       (FILE_CEYESPY,  named sp_body_108)
    123,  # MiniSkedar   (FILE_CMINISKEDAR, named sp_body_123)
    147,  # SkedarKing   (FILE_CSKEDARKING, named sp_body_147)
}

RE_GHEADSANDBODIES_OPEN = re.compile(r"struct headorbody g_HeadsAndBodies\[\] = \{")
RE_GMPBODIES_OPEN = re.compile(r"struct mpbody g_MpBodies\[\] = \{")
RE_S_BASEBODIES_OPEN = re.compile(r"\}\s+s_BaseBodies\[\]\s*=\s*\{")
RE_INDEX_COMMENT = re.compile(r"/\*\s*0x([0-9a-fA-F]+)\s*\*/")
RE_VERSION_BLOCK = re.compile(
    r"#if\s+VERSION\s*>=\s*VERSION_NTSC_1_0(.*?)#else(.*?)#endif",
    re.DOTALL)
RE_VERSION_JPN_GUARD = re.compile(
    r"#if\s+VERSION\s*!=\s*VERSION_JPN_FINAL(.*?)#endif",
    re.DOTALL)


def extract_block(text, open_pat):
    m = open_pat.search(text)
    if not m:
        return None
    start = m.end()
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
    while True:
        m = RE_VERSION_BLOCK.search(block_text)
        if not m:
            break
        ntsc_branch, _pre1_branch = m.group(1), m.group(2)
        block_text = block_text[:m.start()] + ntsc_branch + block_text[m.end():]
    while True:
        m = RE_VERSION_JPN_GUARD.search(block_text)
        if not m:
            break
        body = m.group(1)
        block_text = block_text[:m.start()] + body + block_text[m.end():]
    return block_text


def parse_initializer_rows(block_text):
    rows = []
    pos = 0
    while pos < len(block_text):
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
        inner = block_text[start:end]
        look_start = max(0, pos - 80)
        prefix = block_text[look_start:pos]
        m = RE_INDEX_COMMENT.search(prefix)
        idx_hint = int(m.group(1), 16) if m else None
        rows.append((idx_hint, inner))
        pos = end + 1
    return rows


def split_initializer_fields(inner):
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
    cleaned = []
    block_re = re.compile(r"/\*.*?\*/", re.DOTALL)
    line_re = re.compile(r"//.*$", re.MULTILINE)
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


def parse_float_or_int(s):
    s = s.strip().rstrip("f").rstrip("F")
    try:
        return float(s)
    except ValueError:
        try:
            return int(s, 0)
        except ValueError:
            return s


def parse_g_headsandbodies(robot_path):
    text = open(robot_path, "r", encoding="utf-8").read()
    block = extract_block(text, RE_GHEADSANDBODIES_OPEN)
    if block is None:
        raise SystemExit("g_HeadsAndBodies[] not found in " + robot_path)
    block = select_ntsc(block)
    rows = parse_initializer_rows(block)

    # Field order per struct headorbody (src/include/types.h:3109):
    #   ismale:1, unk00_01:1, canvaryheight:1, type:3, height:8,
    #   filenum, scale, animscale, modeldef, handfilenum
    bodynum = 0
    entries = []
    for idx_hint, inner in rows:
        fields = split_initializer_fields(inner)
        if len(fields) < 10:
            sys.stderr.write(
                "warning: g_HeadsAndBodies[%d] has %d fields (expected 10), skipping\n"
                % (bodynum, len(fields)))
            bodynum += 1
            continue
        entry = {
            "bodynum": bodynum,
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
        if idx_hint is not None and idx_hint != bodynum:
            sys.stderr.write(
                "warning: index hint /*0x%x*/ != actual bodynum %d\n"
                % (idx_hint, bodynum))
        entries.append(entry)
        bodynum += 1
    return entries


def parse_g_mpbodies(mplayer_path):
    """Parse g_MpBodies[] into a list of (mp_idx, bodynum_symbol)."""
    text = open(mplayer_path, "r", encoding="utf-8").read()
    block = extract_block(text, RE_GMPBODIES_OPEN)
    if block is None:
        raise SystemExit("g_MpBodies[] not found in " + mplayer_path)
    block = select_ntsc(block)
    rows = parse_initializer_rows(block)

    pairs = []
    for mp_idx, (_idx_hint, inner) in enumerate(rows):
        fields = split_initializer_fields(inner)
        if len(fields) < 1:
            continue
        body_sym = fields[0].strip()
        pairs.append((mp_idx, body_sym))
    return pairs


def parse_s_basebodies(basereg_path):
    """Parse s_BaseBodies[] into mp_idx -> slug mapping."""
    text = open(basereg_path, "r", encoding="utf-8").read()
    block = extract_block(text, RE_S_BASEBODIES_OPEN)
    if block is None:
        raise SystemExit("s_BaseBodies[] not found in " + basereg_path)
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


def body_symbol_to_int(sym, body_constants):
    if isinstance(sym, int):
        return sym
    return body_constants.get(sym)


def build_body_constants(constants_path):
    """Pre-build a HEAD_* / BODY_* -> int table by scanning constants.h."""
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
                if val in table:
                    table[name] = table[val]
                    progress = True
    return table


# BODY_TESTCHR enum constant. Defined in src/include/constants.h.
BODY_TESTCHR_NAME = "BODY_TESTCHR"


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--robot", required=True)
    p.add_argument("--mplayer", required=True)
    p.add_argument("--basereg", required=True)
    p.add_argument("--constants", required=True)
    p.add_argument("--output", required=True)
    args = p.parse_args()

    body_constants = build_body_constants(args.constants)
    if not body_constants:
        sys.stderr.write("warning: no BODY_*/HEAD_* constants resolved from %s\n"
                         % args.constants)

    body_testchr_int = body_constants.get(BODY_TESTCHR_NAME)

    entries = parse_g_headsandbodies(args.robot)
    mp_pairs = parse_g_mpbodies(args.mplayer)
    base_slugs = parse_s_basebodies(args.basereg)

    # Build bodynum -> slug from mp_idx -> bodynum + mp_idx -> slug.
    bodynum_to_slug = {}
    bodynum_in_mp = set()
    for mp_idx, body_sym in mp_pairs:
        body_int = body_symbol_to_int(body_sym, body_constants)
        if body_int is None:
            sys.stderr.write(
                "warning: unresolved BODY symbol '%s' at mp_idx %d\n"
                % (body_sym, mp_idx))
            continue
        bodynum_in_mp.add(body_int)
        if mp_idx in base_slugs:
            bodynum_to_slug[body_int] = base_slugs[mp_idx]

    # Build the JSON body records.
    body_records = []
    for e in entries:
        bn = e["bodynum"]
        if e.get("filenum") in (0, "0", None):
            continue  # skip the trailing sentinel row at index 0x97
        if body_testchr_int is not None and bn == body_testchr_int:
            continue  # dev placeholder

        # Classify: named MP body OR SP fallback body.
        slug = bodynum_to_slug.get(bn)
        if slug:
            cid = "base:%s" % slug
        else:
            unk = e.get("unk00_01", 0)
            if unk == 1 or unk == "1":
                # head slot, not a body (heads.pdbase handles it). The
                # INTEGRATED_HEAD_BODYNUMS override below does not apply
                # here because the SP-only branch already filters
                # canonical heads; the override only re-flags bodies
                # we KEEP, not ones we drop.
                continue
            cid = "base:sp_body_%d" % bn

        # S593g-followup unk00_01 override: align the PC port's
        # data with the body.c integrated-head invariant. See the
        # INTEGRATED_HEAD_BODYNUMS docblock at the top of this file.
        unk_raw = e["unk00_01"]
        unk_value = int(unk_raw) if not isinstance(unk_raw, str) else unk_raw
        if bn in INTEGRATED_HEAD_BODYNUMS:
            unk_value = 1

        rec = {
            "id": cid,
            "bodynum": bn,
            "ismale": int(e["ismale"]) if not isinstance(e["ismale"], str) else e["ismale"],
            "unk00_01": unk_value,
            "canvaryheight": int(e["canvaryheight"]) if not isinstance(e["canvaryheight"], str) else e["canvaryheight"],
            "type": e["type"],
            "height": e["height"],
            "filenum": e["filenum"],
            "scale": e["scale"],
            "animscale": e["animscale"],
            "handfilenum": e["handfilenum"],
        }
        body_records.append(rec)

    # Sort by bodynum for deterministic output.
    body_records.sort(key=lambda r: r["bodynum"])

    out = {
        "pdbase_version": 1,
        "type": "bodies",
        "bodies": body_records,
    }

    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    with open(args.output, "w", encoding="utf-8", newline="\n") as f:
        json.dump(out, f, indent=2, ensure_ascii=False)
        f.write("\n")
    sys.stderr.write("wrote %d body records to %s\n" % (len(body_records), args.output))


if __name__ == "__main__":
    main()
