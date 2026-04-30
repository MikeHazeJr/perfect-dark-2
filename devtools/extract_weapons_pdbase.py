#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
S484 F11: Extract weapon records + animation scripts from src/game/invitems.c
into base/weapons.pdbase JSON archive.

Companion design doc: context/designs/catalog/catalog-full-pipeline-weapons.md
                      Section C / D (.pdbase format) and Section J (Path B
                      data-driven animations decision, 2026-04-30).

Usage:
    python3 devtools/extract_weapons_pdbase.py \
        --invitems src/game/invitems.c \
        --constants src/include/constants.h \
        --gunscript src/include/gunscript.h \
        --types src/include/types.h \
        --output base/weapons.pdbase

Algorithm overview:
  1. Build a constant table from constants.h by scanning #define IDENTIFIER VALUE
     lines. VALUE may be int literal (decimal/hex), bitwise expression of
     already-defined identifiers, or a single identifier alias. Iterate to
     fixed point so forward refs resolve.
  2. Tokenize invitems.c into a flat token stream with comment + #if blocks
     resolved (NTSC, VERSION>=VERSION_NTSC_1_0 path is selected; PAL_FINAL,
     JPN_FINAL branches are dropped).
  3. Walk top-level definitions matching the patterns
     `struct TYPE NAME [\\[\\]] = { ... };` and capture the raw initializer
     bytes for each. Build a symbol table NAME -> (TYPE, init).
  4. For each guncmd[] array (animation), parse the gunscript_X(args) macro
     calls and convert to JSON opcode arrays.
  5. For each weapon record, parse the positional initializer using the
     known struct weapon schema and emit JSON with sub-records inlined
     (functions, ammos, aimsettings, noisesettings, recoilsettings,
     gunviscmds[], modelpartvisibility[]). Cross-references between weapon
     records and animations stay as named string refs.
  6. Walk g_Weapons[] and emit each weapon entry; emit g_AibotWeaponPreferences
     as the bot_pref sub-struct on each weapon.

The emitted JSON schema:
  {
    "pdbase_version": 1,
    "type": "weapons",
    "animations": [ { "id": "<name>", "opcodes": [["mnemonic", arg, ...], ...] }, ... ],
    "weapons":    [ { "id": "base:<slug>", "weapon_id": <int>,
                       ... per-weapon fields ... }, ... ]
  }

The script is deterministic: same source -> same output bytes.
"""

import argparse
import json
import os
import re
import sys
from collections import OrderedDict


# ---------------------------------------------------------------------------
# Constant table builder
# ---------------------------------------------------------------------------

# Patterns
RE_DEFINE = re.compile(r"^\s*#define\s+([A-Za-z_]\w*)\s+(.+?)\s*(?:/\*.*?\*/|//.*)?$")
RE_DEFINE_FN = re.compile(r"^\s*#define\s+([A-Za-z_]\w*)\s*\(")
RE_INT_LIT = re.compile(r"^(0[xX][0-9a-fA-F]+|\d+)([uU]?[lL]?[lL]?)$")


def parse_int_literal(tok):
    """Parse a C integer literal -> int. Returns None on failure."""
    m = RE_INT_LIT.match(tok.strip())
    if not m:
        return None
    raw = m.group(1)
    if raw.lower().startswith("0x"):
        return int(raw, 16)
    return int(raw)


def evaluate_expr(expr, table):
    """Evaluate a C constant expression using the constant table.

    Supports: integer literals (dec/hex), identifiers (resolved via table),
    parentheses, bitwise OR / AND / XOR / NOT, shifts, +/-/*/, unary minus.
    Returns int on success, None on failure.
    """
    expr = expr.strip()
    if not expr:
        return None

    # Strip outer parens repeatedly (e.g. "((A | B))")
    while expr.startswith("(") and expr.endswith(")"):
        # Verify the outer parens match
        depth = 0
        balanced_outer = True
        for i, c in enumerate(expr):
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
                if depth == 0 and i != len(expr) - 1:
                    balanced_outer = False
                    break
        if balanced_outer:
            expr = expr[1:-1].strip()
        else:
            break

    # Substitute identifiers in expr with their values from the table.
    def repl_ident(m):
        ident = m.group(0)
        if ident in table:
            return str(table[ident])
        return ident

    expr_subbed = re.sub(r"\b[A-Za-z_]\w*\b", repl_ident, expr)
    # Clean integer suffixes
    expr_subbed = re.sub(r"\b(0[xX][0-9a-fA-F]+|\d+)[uUlL]+\b",
                         lambda m: m.group(1), expr_subbed)
    # Convert C-style operators to Python (mostly compatible, but ensure unary)
    # Keep | & ^ ~ << >> + - * / as-is.
    try:
        # Disallow anything other than digits, hex, operators, parens, whitespace.
        if not re.fullmatch(r"[\d\sxXa-fA-F+\-*/&|^~()<>]+", expr_subbed):
            return None
        # eval is safe here because we've validated the character set.
        return int(eval(expr_subbed, {"__builtins__": {}}, {}))
    except Exception:
        return None


def build_constant_table(constants_h_path, extra_paths=()):
    """Scan constants.h (and any extra files) for #define X V lines.

    Returns OrderedDict ident -> int. Values that don't resolve are skipped.
    """
    table = OrderedDict()
    paths = [constants_h_path] + list(extra_paths)
    raw_defines = []  # (ident, value_str)

    for path in paths:
        if not os.path.isfile(path):
            continue
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
        for line in lines:
            # Skip function-like macros entirely (not constants).
            if RE_DEFINE_FN.match(line):
                continue
            m = RE_DEFINE.match(line)
            if not m:
                continue
            ident = m.group(1)
            value = m.group(2)
            # Strip trailing comment fragments inadvertently captured.
            value = re.sub(r"//.*$", "", value).strip()
            value = re.sub(r"/\*.*?\*/", "", value).strip()
            if not value:
                continue
            raw_defines.append((ident, value))

    # Iterate to fixed point.
    progress = True
    while progress:
        progress = False
        unresolved = []
        for ident, value in raw_defines:
            if ident in table:
                continue
            v = evaluate_expr(value, table)
            if v is not None:
                table[ident] = v
                progress = True
            else:
                unresolved.append((ident, value))
        raw_defines = unresolved
    return table


# ---------------------------------------------------------------------------
# Source preprocessor: strip comments, resolve #if blocks
# ---------------------------------------------------------------------------

def strip_comments(src):
    """Remove // line and /* block */ comments. Preserves string literals."""
    out = []
    i = 0
    n = len(src)
    while i < n:
        c = src[i]
        if c == '"':
            j = i + 1
            while j < n and src[j] != '"':
                if src[j] == '\\' and j + 1 < n:
                    j += 2
                else:
                    j += 1
            out.append(src[i:j + 1])
            i = j + 1
        elif c == "'":
            j = i + 1
            while j < n and src[j] != "'":
                if src[j] == '\\' and j + 1 < n:
                    j += 2
                else:
                    j += 1
            out.append(src[i:j + 1])
            i = j + 1
        elif c == "/" and i + 1 < n and src[i + 1] == "/":
            j = src.find("\n", i)
            if j == -1:
                j = n
            out.append("\n")  # preserve line count
            i = j
        elif c == "/" and i + 1 < n and src[i + 1] == "*":
            j = src.find("*/", i + 2)
            if j == -1:
                j = n
            else:
                j += 2
            # Preserve newlines in the comment for line accounting.
            out.append(re.sub(r"[^\n]", " ", src[i:j]))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def resolve_ifdefs(src, defs):
    """Resolve #if VERSION >= VERSION_NTSC_1_0 (and similar) blocks.

    `defs` is the constant table; we evaluate the conditional expression
    using it. Supported forms:
        #if VERSION >= VERSION_NTSC_1_0
        #if VERSION == VERSION_JPN_FINAL
        #ifdef X
        #ifndef X
    Branches: #else, #endif. Nesting supported.
    """
    out_lines = []
    stack = []  # list of (active, parent_active)
    cur_active = True

    for line in src.split("\n"):
        s = line.strip()
        if s.startswith("#if "):
            cond_str = s[len("#if "):].strip()
            cond = eval_pp_expr(cond_str, defs)
            new_active = cur_active and bool(cond)
            stack.append(("if", cur_active, cond))
            cur_active = new_active
            out_lines.append("")  # placeholder
        elif s.startswith("#ifdef "):
            ident = s[len("#ifdef "):].strip()
            cond = ident in defs
            new_active = cur_active and bool(cond)
            stack.append(("if", cur_active, cond))
            cur_active = new_active
            out_lines.append("")
        elif s.startswith("#ifndef "):
            ident = s[len("#ifndef "):].strip()
            cond = ident not in defs
            new_active = cur_active and bool(cond)
            stack.append(("if", cur_active, cond))
            cur_active = new_active
            out_lines.append("")
        elif s == "#else":
            assert stack, "#else with no #if"
            kind, parent, cond = stack[-1]
            stack[-1] = (kind, parent, not cond)
            cur_active = parent and (not cond)
            out_lines.append("")
        elif s.startswith("#elif "):
            assert stack, "#elif with no #if"
            kind, parent, cond_so_far = stack[-1]
            new_cond = bool(eval_pp_expr(s[len("#elif "):].strip(), defs))
            taken = parent and (not cond_so_far) and new_cond
            stack[-1] = (kind, parent, cond_so_far or new_cond)
            cur_active = taken
            out_lines.append("")
        elif s == "#endif":
            assert stack, "#endif with no #if"
            stack.pop()
            cur_active = stack[-1][1] if stack else True
            # Note: if stack is non-empty and we're inside an outer block whose
            # active flag was true, we need to recompute cur_active based on
            # all stacked active flags.
            cur_active = True
            for (_, parent, cond) in stack:
                if not (parent and cond):
                    cur_active = False
                    break
            out_lines.append("")
        else:
            out_lines.append(line if cur_active else "")
    return "\n".join(out_lines)


def eval_pp_expr(expr, defs):
    """Evaluate a preprocessor #if expression. Returns bool/int."""
    # Substitute identifiers
    def repl(m):
        ident = m.group(0)
        if ident in defs:
            return str(defs[ident])
        return "0"
    expr_subbed = re.sub(r"\b[A-Za-z_]\w*\b", repl, expr)
    # C-style comparison operators are compatible with Python.
    try:
        if not re.fullmatch(r"[\d\sxXa-fA-F+\-*/&|^~()<>=!]+", expr_subbed):
            return False
        return int(bool(eval(expr_subbed, {"__builtins__": {}}, {})))
    except Exception:
        return False


# ---------------------------------------------------------------------------
# Tokenizer for struct initializer bodies
# ---------------------------------------------------------------------------

TOKEN_RE = re.compile(
    r"""
    (?P<NUMBER>(?:0[xX][0-9a-fA-F]+|\d+(?:\.\d*)?(?:[eE][+\-]?\d+)?(?:[fFlLuU]+)?|\.\d+(?:[eE][+\-]?\d+)?[fFlLuU]?))
    |(?P<IDENT>[A-Za-z_]\w*)
    |(?P<STRING>"(?:[^"\\]|\\.)*")
    |(?P<PUNCT>[{}\[\](),;&|^~+\-*/<>!=?:.])
    """,
    re.VERBOSE,
)


def tokenize(src):
    """Tokenize a C source fragment. Returns list of (kind, text)."""
    out = []
    for m in TOKEN_RE.finditer(src):
        kind = m.lastgroup
        text = m.group(0)
        out.append((kind, text))
    return out


# ---------------------------------------------------------------------------
# Top-level definition extractor
# ---------------------------------------------------------------------------

# Match top-level: [optional type [*?]] NAME[optional []] = { ... };
RE_DEF_HEAD = re.compile(
    r"^\s*((?:static\s+)?(?:struct\s+\w+|f32|s32|s16|s8|u32|u16|u8|int|float))\s*(\*\s*)?"
    r"([A-Za-z_]\w*)\s*(\[\s*\])?\s*=\s*",
    re.MULTILINE,
)


def find_definitions(src):
    """Find every top-level `TYPE NAME[?] = {body};` definition.

    Returns list of dicts: {type, name, is_array, init, span}.
    """
    out = []
    pos = 0
    while True:
        m = RE_DEF_HEAD.search(src, pos)
        if not m:
            break
        type_str = m.group(1)
        is_pointer = m.group(2) is not None
        name = m.group(3)
        is_array = m.group(4) is not None
        # Find the opening brace, then the matching close.
        i = m.end()
        # Skip whitespace until '{'
        while i < len(src) and src[i].isspace():
            i += 1
        if i >= len(src) or src[i] != "{":
            pos = m.end()
            continue
        depth = 0
        j = i
        while j < len(src):
            c = src[j]
            if c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    break
            elif c == '"':
                # Skip string literal
                k = j + 1
                while k < len(src) and src[k] != '"':
                    if src[k] == "\\" and k + 1 < len(src):
                        k += 2
                    else:
                        k += 1
                j = k
            elif c == "'":
                k = j + 1
                while k < len(src) and src[k] != "'":
                    if src[k] == "\\" and k + 1 < len(src):
                        k += 2
                    else:
                        k += 1
                j = k
            j += 1
        body = src[i + 1:j]  # content between braces
        # Skip past closing brace + optional ; + whitespace
        end = j + 1
        while end < len(src) and src[end].isspace():
            end += 1
        if end < len(src) and src[end] == ";":
            end += 1
        out.append({
            "type": type_str.strip(),
            "is_pointer": is_pointer,
            "name": name,
            "is_array": is_array,
            "init": body,
            "span": (m.start(), end),
        })
        pos = end
    return out


# ---------------------------------------------------------------------------
# Initializer parser (positional + nested)
# ---------------------------------------------------------------------------

class InitNode:
    """A parsed initializer element. Either:
       - {'kind': 'value', 'tokens': [tokens]}   (a raw value expression)
       - {'kind': 'group', 'children': [InitNode]}  (a {...} nested init)
    """
    __slots__ = ("kind", "tokens", "children")

    def __init__(self, kind, tokens=None, children=None):
        self.kind = kind
        self.tokens = tokens or []
        self.children = children or []


def parse_initializer(tokens, start=0):
    """Parse a {body} initializer starting at `start`. Returns (children, next_idx).

    Top-level call: pass tokens that EXCLUDE the outer braces (the body).
    Recursive call: pass tokens INCLUDING the next `{` and we'll consume to `}`.
    """
    children = []
    cur = []
    i = start
    n = len(tokens)
    while i < n:
        kind, text = tokens[i]
        if kind == "PUNCT" and text == "{":
            if cur:
                children.append(InitNode("value", tokens=cur))
                cur = []
            sub, i = parse_initializer(tokens, i + 1)
            children.append(InitNode("group", children=sub))
        elif kind == "PUNCT" and text == "}":
            if cur:
                children.append(InitNode("value", tokens=cur))
                cur = []
            return children, i + 1
        elif kind == "PUNCT" and text == ",":
            if cur:
                children.append(InitNode("value", tokens=cur))
                cur = []
            i += 1
        else:
            cur.append((kind, text))
            i += 1
    if cur:
        children.append(InitNode("value", tokens=cur))
    return children, i


def parse_top_initializer(body):
    """Parse a top-level body string (no surrounding braces). Returns list of InitNode."""
    toks = tokenize(body)
    children, _ = parse_initializer(toks, 0)
    return children


# ---------------------------------------------------------------------------
# Value evaluator: turns a value-token-list into a Python value
# ---------------------------------------------------------------------------

class ValueRef:
    """Marker for a symbol reference (with or without leading &)."""
    __slots__ = ("name", "is_address")

    def __init__(self, name, is_address):
        self.name = name
        self.is_address = is_address

    def __repr__(self):
        return ("&" if self.is_address else "") + self.name


def eval_value_tokens(tokens, table):
    """Evaluate a tokenized C value expression.

    Returns one of:
      - int / float
      - None for NULL
      - ValueRef for &Symbol or Symbol (where Symbol is not a constant)
      - tuple of strings ('expr', text) for unhandled cases (debug)
    """
    # Strip surrounding whitespace / casts: handle simple `(uintptr_t)X` as just X.
    # Drop leading `(IDENT)` cast.
    if (len(tokens) >= 4
            and tokens[0] == ("PUNCT", "(")
            and tokens[1][0] == "IDENT"
            and tokens[2] == ("PUNCT", ")")):
        tokens = tokens[3:]

    # NULL literal
    if len(tokens) == 1 and tokens[0] == ("IDENT", "NULL"):
        return None

    # Single number literal
    if len(tokens) == 1 and tokens[0][0] == "NUMBER":
        return parse_number(tokens[0][1])

    # Negative number
    if (len(tokens) == 2
            and tokens[0] == ("PUNCT", "-")
            and tokens[1][0] == "NUMBER"):
        v = parse_number(tokens[1][1])
        if isinstance(v, int):
            return -v
        return -v

    # & Symbol (address-of)
    if (len(tokens) == 2
            and tokens[0] == ("PUNCT", "&")
            and tokens[1][0] == "IDENT"):
        return ValueRef(tokens[1][1], is_address=True)

    # Bare Symbol (could be a constant or a symbol ref)
    if len(tokens) == 1 and tokens[0][0] == "IDENT":
        ident = tokens[0][1]
        if ident in table:
            return table[ident]
        return ValueRef(ident, is_address=False)

    # Bitwise / arithmetic expression — evaluate via constant table.
    expr = tokens_to_expr(tokens)
    v = evaluate_expr(expr, table)
    if v is not None:
        return v

    # Unhandled — return as debug expr.
    return ("expr", expr)


def parse_number(tok):
    """Parse a C number literal (int or float). Strip suffix."""
    s = re.sub(r"[fFlLuU]+$", "", tok)
    if s.lower().startswith("0x"):
        return int(s, 16)
    if "." in s or "e" in s or "E" in s:
        return float(s)
    return int(s)


def tokens_to_expr(tokens):
    """Reconstitute a C expression string from tokens (joined by spaces)."""
    return " ".join(text for _, text in tokens)


# ---------------------------------------------------------------------------
# Schema for each struct type
# ---------------------------------------------------------------------------

# Each struct schema is an ordered list of (field_name, type) pairs.
# The position in the flat positional initializer corresponds to this order.
# For arrays, the schema lists each element separately as e.g. `functions[0]`.
SCHEMA_NOISESETTINGS = [
    ("minradius", "f32"),
    ("maxradius", "f32"),
    ("incradius", "f32"),
    ("decbasespeed", "f32"),
    ("decremspeed", "f32"),
]
SCHEMA_RECOILSETTINGS = [
    ("xrange", "f32"),
    ("yrange", "f32"),
    ("zrange", "f32"),
    ("unk0c", "f32"),
    ("unk10", "u8"),
]
SCHEMA_INVAIMSETTINGS = [
    ("zoomfov", "f32"),
    ("guntransup", "f32"),
    ("guntransdown", "f32"),
    ("guntransside", "f32"),
    ("aimdamppal", "f32"),
    ("aimdamp", "f32"),
    ("tracktype", "u32_bf"),
    ("unk18_04", "u32_bf"),
    ("flags", "u32"),
]
SCHEMA_INVENTORY_AMMO = [
    ("type", "u32"),
    ("casingeject", "u32"),
    ("clipsize", "s16"),
    ("reload_animation", "guncmd_ref"),
    ("flags", "u8"),
]

# struct weaponfunc base:
SCHEMA_WEAPONFUNC_BASE = [
    ("type", "s32"),
    ("name", "u16"),
    ("unk06", "u8"),
    ("ammoindex", "s8"),
    ("noisesettings", "noisesettings_ref"),
    ("fire_animation", "guncmd_ref"),
    ("flags", "u32"),
]
# struct weaponfunc_shoot extends base:
SCHEMA_WEAPONFUNC_SHOOT_EXT = [
    ("recoilsettings", "recoilsettings_ref"),
    ("recoverytime60", "s8"),
    ("damage", "f32"),
    ("spread", "f32"),
    ("unk24", "s8"),
    ("unk25", "s8"),
    ("unk26", "s8"),
    ("unk27", "s8"),
    ("recoildist", "f32"),
    ("recoilangle", "f32"),
    ("slidemax", "f32"),
    ("impactforce", "f32"),
    ("duration60", "u8"),
    ("shootsound", "u16"),
    ("penetration", "u8"),
]
SCHEMA_WEAPONFUNC_SHOOTAUTO_EXT = [
    ("initialrpm", "f32"),
    ("maxrpm", "f32"),
    ("vibrationstart", "f32_ptr"),
    ("vibrationmax", "f32_ptr"),
    ("turretaccel", "s8"),
    ("turretdecel", "s8"),
]
SCHEMA_WEAPONFUNC_SHOOTPROJECTILE_EXT = [
    ("projectilemodelnum", "s32"),
    ("unk44", "u32"),
    ("scale", "f32"),
    ("speed", "s32"),
    ("unk50", "f32"),
    ("traveldist", "s32"),
    ("timer60", "s32"),
    ("reflectangle", "f32"),
    ("soundnum", "s16"),
]
SCHEMA_WEAPONFUNC_THROW_EXT = [
    ("projectilemodelnum", "s32"),
    ("activatetime60", "s16"),
    ("recoverytime60", "s32"),
    ("damage", "f32"),
]
SCHEMA_WEAPONFUNC_MELEE_EXT = [
    ("damage", "f32"),
    ("range", "f32"),
    ("unk1c", "u32"),
    ("unk20", "u32"),
    ("unk24", "u32"),
    ("unk28", "f32"),
    ("unk2c", "f32"),
    ("unk30", "f32"),
    ("unk34", "f32"),
    ("unk38", "f32"),
    ("unk3c", "f32"),
    ("unk40", "f32"),
    ("unk44", "f32"),
    ("unk48", "u32"),
]
SCHEMA_WEAPONFUNC_SPECIAL_EXT = [
    ("specialfunc", "s32"),
    ("recoverytime60", "s32"),
    ("soundnum", "u16"),
]
SCHEMA_WEAPONFUNC_DEVICE_EXT = [
    ("device", "u32"),
]

SCHEMA_WEAPON = [
    ("hi_model", "u16"),
    ("lo_model", "u16"),
    ("equip_animation", "guncmd_ref"),
    ("unequip_animation", "guncmd_ref"),
    ("pritosec_animation", "guncmd_ref"),
    ("sectopri_animation", "guncmd_ref"),
    ("functions[0]", "weaponfunc_ref"),
    ("functions[1]", "weaponfunc_ref"),
    ("ammos[0]", "ammo_ref"),
    ("ammos[1]", "ammo_ref"),
    ("aimsettings", "aimsettings_ref"),
    ("muzzlez", "f32"),
    ("posx", "f32"),
    ("posy", "f32"),
    ("posz", "f32"),
    ("sway", "f32"),
    ("gunviscmds", "gunviscmd_array_ref"),
    ("partvisibility", "modelpartvisibility_array_ref"),
    ("shortname", "u16"),
    ("name", "u16"),
    ("manufacturer", "u16"),
    ("description", "u16"),
    ("flags", "u32"),
]

SCHEMA_AIBOTPREF = [
    ("unk00", "u8"),
    ("unk01", "u8"),
    ("unk02", "u8"),
    ("unk03", "u8"),
    ("haspriammogoal", "u16_bf"),
    ("hassecammogoal", "u16_bf"),
    ("pridistconfig", "u16_bf"),
    ("secdistconfig", "u16_bf"),
    ("targetammopri", "u16"),
    ("targetammosec", "u16"),
    ("criticalammopri", "u16"),
    ("criticalammosec", "u16"),
    ("reloaddelay", "u16_bf"),
    ("allowpartialreloaddelay", "u16_bf"),
]


# Map from declared C type to the "shoot ext" schema variant for funcs:
WEAPONFUNC_TYPE_SCHEMAS = {
    "weaponfunc": SCHEMA_WEAPONFUNC_BASE,
    "weaponfunc_shootsingle": SCHEMA_WEAPONFUNC_BASE + SCHEMA_WEAPONFUNC_SHOOT_EXT,
    "weaponfunc_shootauto": (SCHEMA_WEAPONFUNC_BASE + SCHEMA_WEAPONFUNC_SHOOT_EXT
                             + SCHEMA_WEAPONFUNC_SHOOTAUTO_EXT),
    "weaponfunc_shootprojectile": (SCHEMA_WEAPONFUNC_BASE + SCHEMA_WEAPONFUNC_SHOOT_EXT
                                   + SCHEMA_WEAPONFUNC_SHOOTPROJECTILE_EXT),
    "weaponfunc_throw": SCHEMA_WEAPONFUNC_BASE + SCHEMA_WEAPONFUNC_THROW_EXT,
    "weaponfunc_melee": SCHEMA_WEAPONFUNC_BASE + SCHEMA_WEAPONFUNC_MELEE_EXT,
    "weaponfunc_special": SCHEMA_WEAPONFUNC_BASE + SCHEMA_WEAPONFUNC_SPECIAL_EXT,
    "weaponfunc_device": SCHEMA_WEAPONFUNC_BASE + SCHEMA_WEAPONFUNC_DEVICE_EXT,
}


# ---------------------------------------------------------------------------
# Per-record emitters (called after the symbol table is built)
# ---------------------------------------------------------------------------

def flatten_init(node, out):
    """Flatten an InitNode tree into a list of values (skipping group structure)."""
    if node.kind == "value":
        out.append(("value", node.tokens))
    else:  # group
        for child in node.children:
            flatten_init(child, out)


def map_schema(children, schema, table):
    """Map a list of InitNode (top-level) onto a schema using flat-flattening rules.

    Returns OrderedDict of field_name -> Python value.
    """
    flat = []
    for child in children:
        flatten_init(child, flat)
    result = OrderedDict()
    for i, (fname, ftype) in enumerate(schema):
        if i >= len(flat):
            result[fname] = 0  # missing field, default zero
            continue
        kind, tokens = flat[i]
        result[fname] = eval_value_tokens(tokens, table)
    return result


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------

def parse_enum_header(path):
    """Parse C `enum NAME { IDENT [= V], ... };` blocks from a header.

    Returns dict ident -> int. The first ident defaults to 0; subsequent
    idents without explicit values increment by 1 from the previous."""
    if not os.path.isfile(path):
        return {}
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        src = f.read()
    src = strip_comments(src)
    out = {}
    # Find each `enum NAME { ... };` block.
    for m in re.finditer(r"\benum\s+\w*\s*\{([^}]*)\}\s*;", src, re.DOTALL):
        body = m.group(1)
        cur_value = 0
        for raw_entry in body.split(","):
            entry = raw_entry.strip()
            if not entry:
                continue
            em = re.match(r"^\s*([A-Za-z_]\w*)\s*(?:=\s*(.+?))?\s*$", entry)
            if not em:
                continue
            ident = em.group(1)
            expr = em.group(2)
            if expr:
                v = evaluate_expr(expr, out)
                if v is None:
                    # Try with what we have so far + raw int
                    try:
                        v = int(expr, 0)
                    except Exception:
                        v = cur_value
                cur_value = v
            out[ident] = cur_value
            cur_value += 1
    return out


def emit_enum_lookup_c(path, table_name, entries, header_guard, includes):
    """Emit a C source file with a {name, value} lookup table sorted by name.

    Layout: a static const struct array, plus a public lookup function
    s32 NAME(const char *name).
    """
    sorted_entries = sorted(entries.items())
    parts = []
    parts.append("/* This file is GENERATED by devtools/extract_weapons_pdbase.py.\n"
                 " * Do not edit by hand. Re-run the extractor when the source\n"
                 " * enums change. */\n\n")
    parts.append("#include <stddef.h>\n")
    parts.append("#include <string.h>\n")
    for inc in includes:
        parts.append("#include \"%s\"\n" % inc)
    parts.append("\n")
    parts.append("typedef struct { const char *name; int value; } pdbase_enum_entry_t;\n\n")
    parts.append("static const pdbase_enum_entry_t %s[] = {\n" % table_name)
    for name, value in sorted_entries:
        parts.append("    { \"%s\", %d },\n" % (name, value))
    parts.append("};\n\n")
    parts.append("static const size_t %s_count = sizeof(%s) / sizeof(%s[0]);\n\n"
                 % (table_name, table_name, table_name))
    return "".join(parts)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--invitems", required=True)
    ap.add_argument("--constants", required=True)
    ap.add_argument("--gunscript", required=True)
    ap.add_argument("--types", required=True)
    ap.add_argument("--botinv", required=True,
                    help="src/game/botinv.c for g_AibotWeaponPreferences[]")
    ap.add_argument("--output", required=True)
    ap.add_argument("--enum-tables-out", required=False,
                    help="Optional: emit C lookup tables for ANIM_*, SFX_*, "
                         "FILE_*, L_GUN_* enum families to this .c path.")
    ap.add_argument("--enum-anim-header", required=False,
                    help="src/generated/<rom>/animations.h for ANIM_* enum")
    ap.add_argument("--enum-sequences-header", required=False,
                    help="src/generated/<rom>/sequences.h for SFX_* enum")
    ap.add_argument("--enum-lang-gun-header", required=False,
                    help="src/generated/<rom>/lang/gun.h for L_GUN_* enum")
    ap.add_argument("--enum-files-header", required=False,
                    help="header containing FILE_* enum (typically files.h)")
    args = ap.parse_args()

    # Build constant table from constants.h, gunscript.h, types.h, and any
    # other headers we end up needing. The script's correctness depends on
    # GUNCMD_*, INVENTORYFUNCTYPE_*, AMMOTYPE_*, CASING_*, MODELPART_*,
    # ANIM_*, SFX_*, FILE_*, BOTDISTCFG_*, SIGHTTRACKTYPE_*, INVAIMFLAG_*,
    # WEAPONFLAG_*, FUNCFLAG_*, L_GUN_* all resolving from constants.h.
    const_table = build_constant_table(args.constants,
                                       extra_paths=[args.gunscript])
    # Inject VERSION_NTSC_1_0 etc. since these gate #if blocks.
    const_table.setdefault("VERSION_NTSC_FINAL", 1)
    const_table.setdefault("VERSION_NTSC_1_0", 2)  # match CMake VERSION=2
    const_table.setdefault("VERSION_PAL_FINAL", 3)
    const_table.setdefault("VERSION_JPN_FINAL", 4)
    const_table.setdefault("VERSION", 2)

    # Load invitems.c
    with open(args.invitems, "r", encoding="utf-8", errors="replace") as f:
        src = f.read()

    # Strip comments + resolve #if blocks against const_table.
    src_stripped = strip_comments(src)
    src_resolved = resolve_ifdefs(src_stripped, const_table)

    # Find all top-level definitions.
    defs = find_definitions(src_resolved)

    # Build symbol table: name -> def
    sym = OrderedDict()
    for d in defs:
        sym[d["name"]] = d

    # ---- Emit animations
    animations = []
    for d in defs:
        if d["type"] == "struct guncmd" and d["is_array"]:
            animations.append(emit_animation(d, const_table))

    # ---- Build aibotpref table from botinv.c
    botinv_prefs = load_aibotweaponpreferences(args.botinv, const_table)

    # ---- Emit weapons
    weapons = []
    g_weapons_def = sym.get("g_Weapons")
    if g_weapons_def is None:
        sys.stderr.write("ERROR: g_Weapons[] not found in invitems.c\n")
        sys.exit(2)
    g_weapons_children = parse_top_initializer(g_weapons_def["init"])
    seen_ids = {}  # slug -> count
    for idx, child in enumerate(g_weapons_children):
        ref = eval_value_tokens(child.tokens if child.kind == "value"
                                else _flatten_to_tokens(child), const_table)
        if not isinstance(ref, ValueRef):
            sys.stderr.write(
                "WARN: g_Weapons[%d] is not a symbol ref (got %r)\n" % (idx, ref))
            continue
        weap_name = ref.name
        weap_def = sym.get(weap_name)
        if weap_def is None:
            sys.stderr.write(
                "WARN: g_Weapons[%d] points to unknown symbol '%s'\n"
                % (idx, weap_name))
            continue
        # Build a unique catalog id per slot: append _slotNN when the same
        # invitem_* symbol appears more than once in g_Weapons[] (e.g.
        # invitem_keycard at 8 slots, invitem_hammer at 4 slots).
        slug = re.sub(r"[^a-z0-9_]", "_", weap_name.lower())
        slug = re.sub(r"^invitem_", "", slug)
        seen_ids.setdefault(slug, 0)
        seen_ids[slug] += 1
        if seen_ids[slug] == 1:
            cat_id = "base:%s" % slug
        else:
            cat_id = "base:%s_slot%d" % (slug, idx)
        weapons.append(emit_weapon(idx, weap_name, weap_def, sym, const_table,
                                   botinv_prefs, cat_id))

    # ---- Output
    out_data = OrderedDict([
        ("pdbase_version", 1),
        ("type", "weapons"),
        ("animations", animations),
        ("weapons", weapons),
    ])
    os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)
    with open(args.output, "w", encoding="utf-8", newline="\n") as f:
        json.dump(out_data, f, indent=2, separators=(",", ": "),
                  default=_json_default)
        f.write("\n")
    sys.stderr.write("OK: wrote %s (%d animations, %d weapons)\n"
                     % (args.output, len(animations), len(weapons)))

    # Optional: emit enum lookup tables for the loader.
    if args.enum_tables_out:
        # Enum-style identifiers come from `enum NAME { IDENT, ... };` blocks.
        anim_enum = parse_enum_header(args.enum_anim_header) if args.enum_anim_header else {}
        sfx_enum  = parse_enum_header(args.enum_sequences_header) if args.enum_sequences_header else {}
        lang_enum = parse_enum_header(args.enum_lang_gun_header) if args.enum_lang_gun_header else {}
        file_enum = parse_enum_header(args.enum_files_header) if args.enum_files_header else {}

        # #define-style identifiers come from build_constant_table (constants.h
        # + gunscript.h scan). Pull FILE_*, ANIM_*, SFX_*, L_GUN_* from there too
        # since some headers (e.g. files.h) use `#define` rather than `enum`.
        anim_table = dict(anim_enum)
        sfx_table = dict(sfx_enum)
        lang_table = dict(lang_enum)
        file_table = dict(file_enum)
        for k, v in const_table.items():
            if k.startswith("ANIM_") and k not in anim_table:
                anim_table[k] = v
            elif k.startswith("SFX_") and k not in sfx_table:
                sfx_table[k] = v
            elif k.startswith("L_GUN_") and k not in lang_table:
                lang_table[k] = v
            elif k.startswith("FILE_") and k not in file_table:
                file_table[k] = v
        # Also pick up FILE_* from the dedicated header even if it's #define-based.
        if args.enum_files_header and os.path.isfile(args.enum_files_header):
            file_const_table = build_constant_table(args.enum_files_header)
            for k, v in file_const_table.items():
                if k.startswith("FILE_") and k not in file_table:
                    file_table[k] = v

        chunks = [
            "/* Generated enum lookup tables for the .pdbase loader.\n"
            " * Source: devtools/extract_weapons_pdbase.py\n"
            " *\n"
            " * Tables are sorted by name for binary-search lookups; the\n"
            " * loader does linear scans instead since loading is one-time\n"
            " * at startup and the tables (~3-5k entries total) fit easily\n"
            " * in cache. Switch to bsearch if it ever shows up in profiles. */\n\n",
            "#include <stddef.h>\n",
            "#include <string.h>\n",
            "#include <PR/ultratypes.h>\n",
            "#include \"loader_pdbase_enums.h\"\n\n",
            "typedef struct { const char *name; s32 value; } pdbase_enum_entry_t;\n\n",
        ]

        def emit_table(table_name, entries):
            sorted_entries = sorted(entries.items())
            out = ["static const pdbase_enum_entry_t %s[] = {\n" % table_name]
            for name, value in sorted_entries:
                out.append("    { \"%s\", %d },\n" % (name, value))
            out.append("};\n")
            out.append("static const size_t %s_count = sizeof(%s) / sizeof(%s[0]);\n\n"
                       % (table_name, table_name, table_name))
            return "".join(out)

        chunks.append(emit_table("k_AnimEnum",  anim_table))
        chunks.append(emit_table("k_SfxEnum",   sfx_table))
        chunks.append(emit_table("k_LangEnum",  lang_table))
        chunks.append(emit_table("k_FileEnum",  file_table))

        # Lookup helper.
        chunks.append(
            "static s32 lookup_enum(const pdbase_enum_entry_t *table,\n"
            "                       size_t count,\n"
            "                       const char *name,\n"
            "                       s32 fallback)\n"
            "{\n"
            "    if (name == NULL) return fallback;\n"
            "    for (size_t i = 0; i < count; i++) {\n"
            "        if (strcmp(table[i].name, name) == 0) {\n"
            "            return table[i].value;\n"
            "        }\n"
            "    }\n"
            "    return fallback;\n"
            "}\n\n"
            "s32 loaderPdbaseResolveAnimEnum(const char *name, s32 fallback)\n"
            "{ return lookup_enum(k_AnimEnum, k_AnimEnum_count, name, fallback); }\n\n"
            "s32 loaderPdbaseResolveSfxEnum(const char *name, s32 fallback)\n"
            "{ return lookup_enum(k_SfxEnum, k_SfxEnum_count, name, fallback); }\n\n"
            "s32 loaderPdbaseResolveLangEnum(const char *name, s32 fallback)\n"
            "{ return lookup_enum(k_LangEnum, k_LangEnum_count, name, fallback); }\n\n"
            "s32 loaderPdbaseResolveFileEnum(const char *name, s32 fallback)\n"
            "{ return lookup_enum(k_FileEnum, k_FileEnum_count, name, fallback); }\n"
        )

        os.makedirs(os.path.dirname(args.enum_tables_out) or ".", exist_ok=True)
        with open(args.enum_tables_out, "w", encoding="utf-8", newline="\n") as f:
            f.write("".join(chunks))
        sys.stderr.write(
            "OK: wrote %s (anim=%d sfx=%d lang=%d file=%d)\n"
            % (args.enum_tables_out, len(anim_table), len(sfx_table),
               len(lang_table), len(file_table)))


def _flatten_to_tokens(node):
    """Convert an InitNode group with one value child to that child's tokens."""
    if node.kind == "value":
        return node.tokens
    if len(node.children) == 1 and node.children[0].kind == "value":
        return node.children[0].tokens
    return []


def _json_default(o):
    if isinstance(o, ValueRef):
        return ("&" if o.is_address else "") + o.name
    raise TypeError("Cannot serialize %r" % (o,))


# ---------------------------------------------------------------------------
# Animation emitter
# ---------------------------------------------------------------------------

# Map GUNCMD_* type values to (mnemonic, [arg-format]).
# Fields in struct guncmd: { u8 type; u8 unk01; u16 unk02; intptr_t unk04 }
# arg-format describes what JSON args we extract from (unk01, unk02, unk04).
GUNCMD_DECODE = {
    0: ("end", []),
    1: ("showpart", [("keyframe", "unk02"), ("part", "unk04")]),
    2: ("hidepart", [("keyframe", "unk02"), ("part", "unk04")]),
    3: ("waitforzreleased", [("keyframe", "unk02")]),
    4: ("waittime", [("keyframe", "unk02"), ("time", "unk04")]),
    5: ("playsound", [("keyframe", "unk02"), ("sound", "unk04")]),
    6: ("include", [("unk1", "unk01"), ("address", "unk04_anim_ref")]),
    7: ("random", [("probability", "unk02"), ("address", "unk04_anim_ref")]),
    8: ("repeatuntilfull", [("triggerkey", "unk02"),
                            ("dontloop_gototrigger", "unk04_packed_dontloop")]),
    9: ("popoutsackofpills", [("unk1", "unk02")]),
    10: ("playanimation", [("animation", "unk02"),
                           ("direction_speed", "unk04_packed_dirspeed")]),
    11: ("setsoundspeed", [("keyframe", "unk02"), ("speed", "unk04")]),
}


# Map gunscript_X mnemonic -> ordered list of arg names (matches the macro
# parameter list in src/include/gunscript.h verbatim).
GUNSCRIPT_MACROS = {
    "gunscript_end":               ("end", []),
    "gunscript_showpart":          ("showpart", ["keyframe", "part"]),
    "gunscript_hidepart":          ("hidepart", ["keyframe", "part"]),
    "gunscript_waitforzreleased":  ("waitforzreleased", ["keyframe"]),
    "gunscript_waittime":          ("waittime", ["keyframe", "time"]),
    "gunscript_playsound":         ("playsound", ["keyframe", "sound"]),
    "gunscript_include":           ("include", ["unk1", "address"]),
    "gunscript_random":            ("random", ["probability", "address"]),
    "gunscript_repeatuntilfull":   ("repeatuntilfull",
                                    ["triggerkey", "dontloop", "gototrigger"]),
    "gunscript_popoutsackofpills": ("popoutsackofpills", ["unk1"]),
    "gunscript_playanimation":     ("playanimation",
                                    ["animation", "direction", "speed"]),
    "gunscript_setsoundspeed":     ("setsoundspeed", ["keyframe", "speed"]),
}


# Macros that take an animation symbol as the address arg (becomes a string
# reference in JSON instead of an integer).
GUNSCRIPT_ANIM_REF_ARGS = {
    "gunscript_include":  {"address"},
    "gunscript_random":   {"address"},
}


RE_GUNSCRIPT_CALL = re.compile(r"gunscript_([A-Za-z_]\w*)\s*(\(([^()]*)\))?")


def split_macro_args(arg_str):
    """Split a comma-separated arg list, respecting parens (none expected here)."""
    parts = []
    depth = 0
    cur = []
    for c in arg_str:
        if c == "(":
            depth += 1
            cur.append(c)
        elif c == ")":
            depth -= 1
            cur.append(c)
        elif c == "," and depth == 0:
            parts.append("".join(cur).strip())
            cur = []
        else:
            cur.append(c)
    if cur:
        last = "".join(cur).strip()
        if last:
            parts.append(last)
    return parts


def emit_animation(def_, const_table):
    """Decode a `struct guncmd NAME[] = { ... };` definition into a JSON record.

    The body uses gunscript_X(args) macros (defined in src/include/gunscript.h)
    rather than raw struct field tuples. We parse each macro call directly.
    """
    name = def_["name"]
    body = def_["init"]
    opcodes = []
    for m in RE_GUNSCRIPT_CALL.finditer(body):
        full_name = "gunscript_" + m.group(1)
        spec = GUNSCRIPT_MACROS.get(full_name)
        if spec is None:
            opcodes.append(["unknown_macro", full_name])
            continue
        mnemonic, arg_names = spec
        arg_list_str = m.group(3) or ""
        raw_args = split_macro_args(arg_list_str)
        # Resolve each arg via the constant table.
        opcode = [mnemonic]
        anim_ref_set = GUNSCRIPT_ANIM_REF_ARGS.get(full_name, set())
        for arg_name, raw in zip(arg_names, raw_args):
            if arg_name in anim_ref_set:
                # The arg is a reference to another animation symbol.
                # Use the bare symbol name as the JSON value.
                ident = raw.strip()
                opcode.append(ident)
            else:
                v = evaluate_expr(raw, const_table)
                if v is None:
                    # Try interpreting as a bare symbol.
                    sym_match = re.fullmatch(r"\s*([A-Za-z_]\w*)\s*", raw)
                    if sym_match:
                        opcode.append(sym_match.group(1))
                    else:
                        opcode.append({"_unresolved": raw})
                else:
                    opcode.append(v)
        opcodes.append(opcode)
    return OrderedDict([("id", name), ("opcodes", opcodes)])


def _decode_arg(src_field, unk01, unk02, unk04):
    """Decode a single arg from its source field, applying any packing rules."""
    if src_field == "unk01":
        return unk01 if isinstance(unk01, int) else unk01
    if src_field == "unk02":
        return unk02 if isinstance(unk02, int) else unk02
    if src_field == "unk04":
        return unk04 if isinstance(unk04, int) else unk04
    if src_field == "unk04_anim_ref":
        # unk04 is a pointer to another guncmd[]. Already a ValueRef in our
        # model. Strip the leading & and leave the bare name.
        if isinstance(unk04, ValueRef):
            return unk04.name
        return unk04
    if src_field == "unk04_packed_dontloop":
        # (dontloop << 16) | gototrigger
        if isinstance(unk04, int):
            return [unk04 >> 16, unk04 & 0xFFFF]
        return unk04
    if src_field == "unk04_packed_dirspeed":
        # (direction << 16) | speed
        if isinstance(unk04, int):
            return [unk04 >> 16, unk04 & 0xFFFF]
        return unk04
    return None


# ---------------------------------------------------------------------------
# Weapon emitter
# ---------------------------------------------------------------------------

def emit_weapon(idx, name, weap_def, sym, const_table, botinv_prefs,
                catalog_id):
    """Emit a single weapon record as a JSON dict."""
    children = parse_top_initializer(weap_def["init"])
    fields = map_schema(children, SCHEMA_WEAPON, const_table)

    # Resolve sub-records (functions, ammos, aimsettings) by following refs.
    funcs = []
    for fkey in ("functions[0]", "functions[1]"):
        funcs.append(_resolve_weaponfunc(fields.get(fkey), sym, const_table))
    ammos = []
    for akey in ("ammos[0]", "ammos[1]"):
        ammos.append(_resolve_ammo(fields.get(akey), sym, const_table))
    aim = _resolve_aimsettings(fields.get("aimsettings"), sym, const_table)
    gunviscmds = _resolve_gunviscmd_array(fields.get("gunviscmds"), sym, const_table)
    partvis = _resolve_partvis_array(fields.get("partvisibility"), sym, const_table)

    # Animation refs become bare strings.
    def anim_ref(v):
        if isinstance(v, ValueRef):
            return v.name
        if v is None:
            return None
        return v

    out = OrderedDict()
    out["id"] = catalog_id
    out["weapon_id"] = idx
    out["symbol"] = name  # original C symbol name, for diagnostics
    out["hi_model"] = fields.get("hi_model")
    out["lo_model"] = fields.get("lo_model")
    out["equip_animation"] = anim_ref(fields.get("equip_animation"))
    out["unequip_animation"] = anim_ref(fields.get("unequip_animation"))
    out["pritosec_animation"] = anim_ref(fields.get("pritosec_animation"))
    out["sectopri_animation"] = anim_ref(fields.get("sectopri_animation"))
    out["functions"] = funcs
    out["ammos"] = ammos
    out["aimsettings"] = aim
    out["muzzlez"] = fields.get("muzzlez")
    out["posx"] = fields.get("posx")
    out["posy"] = fields.get("posy")
    out["posz"] = fields.get("posz")
    out["sway"] = fields.get("sway")
    out["gunviscmds"] = gunviscmds
    out["partvisibility"] = partvis
    out["shortname"] = fields.get("shortname")
    out["name"] = fields.get("name")
    out["manufacturer"] = fields.get("manufacturer")
    out["description"] = fields.get("description")
    out["flags"] = fields.get("flags")
    # Bot AI preference
    out["bot_pref"] = botinv_prefs.get(idx, None)
    return out


def _resolve_weaponfunc(ref, sym, const_table):
    if ref is None:
        return None
    if not isinstance(ref, ValueRef):
        return ref  # raw
    target = sym.get(ref.name)
    if target is None:
        return {"_unresolved": ref.name}
    type_str = target["type"].replace("struct ", "")
    schema = WEAPONFUNC_TYPE_SCHEMAS.get(type_str, SCHEMA_WEAPONFUNC_BASE)
    children = parse_top_initializer(target["init"])
    fields = map_schema(children, schema, const_table)
    out = OrderedDict()
    out["_symbol"] = ref.name
    out["_struct"] = type_str
    for k, v in fields.items():
        if k == "noisesettings":
            out[k] = _resolve_noisesettings(v, sym, const_table)
        elif k == "recoilsettings":
            out[k] = _resolve_recoilsettings(v, sym, const_table)
        elif k == "fire_animation":
            out[k] = v.name if isinstance(v, ValueRef) else v
        elif k in ("vibrationstart", "vibrationmax"):
            out[k] = _resolve_f32_array(v, sym, const_table)
        else:
            out[k] = v
    return out


def _resolve_ammo(ref, sym, const_table):
    if ref is None:
        return None
    if not isinstance(ref, ValueRef):
        return ref
    target = sym.get(ref.name)
    if target is None:
        return {"_unresolved": ref.name}
    children = parse_top_initializer(target["init"])
    fields = map_schema(children, SCHEMA_INVENTORY_AMMO, const_table)
    out = OrderedDict()
    out["_symbol"] = ref.name
    for k, v in fields.items():
        if k == "reload_animation":
            out[k] = v.name if isinstance(v, ValueRef) else v
        else:
            out[k] = v
    return out


def _resolve_aimsettings(ref, sym, const_table):
    if ref is None:
        return None
    if not isinstance(ref, ValueRef):
        return ref
    target = sym.get(ref.name)
    if target is None:
        return {"_unresolved": ref.name}
    children = parse_top_initializer(target["init"])
    fields = map_schema(children, SCHEMA_INVAIMSETTINGS, const_table)
    out = OrderedDict()
    out["_symbol"] = ref.name
    for k, v in fields.items():
        out[k] = v
    return out


def _resolve_noisesettings(ref, sym, const_table):
    if ref is None:
        return None
    if not isinstance(ref, ValueRef):
        return ref
    target = sym.get(ref.name)
    if target is None:
        return {"_unresolved": ref.name}
    children = parse_top_initializer(target["init"])
    fields = map_schema(children, SCHEMA_NOISESETTINGS, const_table)
    out = OrderedDict()
    out["_symbol"] = ref.name
    for k, v in fields.items():
        out[k] = v
    return out


def _resolve_recoilsettings(ref, sym, const_table):
    if ref is None:
        return None
    if not isinstance(ref, ValueRef):
        return ref
    target = sym.get(ref.name)
    if target is None:
        return {"_unresolved": ref.name}
    children = parse_top_initializer(target["init"])
    fields = map_schema(children, SCHEMA_RECOILSETTINGS, const_table)
    out = OrderedDict()
    out["_symbol"] = ref.name
    for k, v in fields.items():
        out[k] = v
    return out


GUNVISCMD_MACROS = {
    "gunviscmd_end":               ("end", []),
    "gunviscmd_sethidden":         ("sethidden", ["modelpart"]),
    "gunviscmd_checkupgrade":      ("checkupgrade", ["upgrade", "operator", "modelpart"]),
    "gunviscmd_checkinlefthand":   ("checkinlefthand", ["operator", "modelpart"]),
    "gunviscmd_checkinrighthand":  ("checkinrighthand", ["operator", "modelpart"]),
}

RE_GUNVISCMD_CALL = re.compile(r"gunviscmd_([A-Za-z_]\w*)\s*(\(([^()]*)\))?")


def _resolve_gunviscmd_array(ref, sym, const_table):
    if ref is None:
        return None
    if not isinstance(ref, ValueRef):
        return ref
    target = sym.get(ref.name)
    if target is None:
        return {"_unresolved": ref.name}
    body = target["init"]
    out = []
    for m in RE_GUNVISCMD_CALL.finditer(body):
        full_name = "gunviscmd_" + m.group(1)
        spec = GUNVISCMD_MACROS.get(full_name)
        if spec is None:
            out.append(["unknown_macro", full_name])
            continue
        mnemonic, arg_names = spec
        arg_list_str = m.group(3) or ""
        raw_args = split_macro_args(arg_list_str)
        opcode = [mnemonic]
        for arg_name, raw in zip(arg_names, raw_args):
            v = evaluate_expr(raw, const_table)
            if v is None:
                sym_match = re.fullmatch(r"\s*([A-Za-z_]\w*)\s*", raw)
                if sym_match:
                    opcode.append(sym_match.group(1))
                else:
                    opcode.append({"_unresolved": raw})
            else:
                opcode.append(v)
        out.append(opcode)
    return out


def _resolve_partvis_array(ref, sym, const_table):
    if ref is None:
        return None
    if not isinstance(ref, ValueRef):
        return ref
    target = sym.get(ref.name)
    if target is None:
        return {"_unresolved": ref.name}
    children = parse_top_initializer(target["init"])
    out = []
    for child in children:
        if child.kind != "group":
            continue
        flat = []
        for sub in child.children:
            flatten_init(sub, flat)
        vals = [eval_value_tokens(tokens, const_table) for _, tokens in flat]
        out.append(vals)
    return out


def _resolve_f32_array(ref, sym, const_table):
    if ref is None:
        return None
    if not isinstance(ref, ValueRef):
        return ref
    target = sym.get(ref.name)
    if target is None:
        return {"_unresolved": ref.name}
    children = parse_top_initializer(target["init"])
    out = []
    for child in children:
        if child.kind != "value":
            continue
        v = eval_value_tokens(child.tokens, const_table)
        out.append(v)
    return out


# ---------------------------------------------------------------------------
# Bot AI preferences loader
# ---------------------------------------------------------------------------

def load_aibotweaponpreferences(botinv_path, const_table):
    """Parse g_AibotWeaponPreferences[] from botinv.c and return idx -> dict."""
    if not os.path.isfile(botinv_path):
        sys.stderr.write("WARN: botinv.c not found at %s\n" % botinv_path)
        return {}
    with open(botinv_path, "r", encoding="utf-8", errors="replace") as f:
        src = f.read()
    src_stripped = strip_comments(src)
    src_resolved = resolve_ifdefs(src_stripped, const_table)
    defs = find_definitions(src_resolved)
    target = None
    for d in defs:
        if d["name"] == "g_AibotWeaponPreferences" and d["is_array"]:
            target = d
            break
    if target is None:
        sys.stderr.write("WARN: g_AibotWeaponPreferences[] not found\n")
        return {}
    children = parse_top_initializer(target["init"])
    out = {}
    for idx, child in enumerate(children):
        if child.kind != "group":
            continue
        sub_children = child.children
        fields = map_schema(sub_children, SCHEMA_AIBOTPREF, const_table)
        out[idx] = OrderedDict(fields)
    return out


if __name__ == "__main__":
    main()
