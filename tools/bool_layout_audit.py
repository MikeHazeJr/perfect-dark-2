"""Audit how wide `bool` is at every declaration that uses it, in every translation unit.

Why this exists (SP-75 in context/systemic-bugs.md): project `bool` is a
macro, `#define bool s32` (src/include/types.h). In C, <stdbool.h> redefines it
as the 1-byte `_Bool`; in C++, `bool` is a 1-byte keyword unless the types.h
macro is in force. So one header declaration that says `bool` can mean 4 bytes
in one file and 1 byte in another, and nothing warns. On 2026-11-14 that gave
`struct netclient` two layouts and broke every listen-host match.

What it does, read-only:
  1. Finds every line of every project header that uses the token `bool`
     outside comments and strings (a "bool site"), and classifies it: struct or
     union member, function body (TU-local, harmless), or other file-scope
     declaration (prototype, extern variable, function definition, typedef).
     For file-scope declarations it also reads the declared name.
  2. Preprocesses every translation unit of the chosen CMake target with its
     exact flags plus `-E -dD`, follows the line markers, the `bool` macro and
     `#pragma push_macro("bool")` / `pop_macro("bool")`, and records what
     `bool` means at each bool site it passes, in headers and in the
     translation unit's own source: `s32` (4 bytes), `_Bool` (1 byte, C) or
     the C++ keyword (1 byte).
  3. Reports five kinds of problem:
     - mixed sites: a header declaration seen with a 4-byte `bool` in some
       files and a 1-byte `bool` in others (struct members change layout;
       prototypes, globals and typedefs change the calling or storage width);
     - mixed symbols: one function or global, by name, declared or defined with
       a 4-byte `bool` in one place and a 1-byte one in another. This catches
       what a header check cannot: a C++ file that declares a C function or
       array itself (`extern bool g_PlayersWithControl[];` in a .cpp file sees
       the 1-byte keyword, the C definition is `s32`), or a definition compiled
       with a flipped `bool`;
     - flips: a C file where `bool` changes from `s32` to `_Bool` outside any
       push_macro scope and project code that uses `bool` is then compiled with
       the 1-byte meaning. A flip that only covers vendored or system code and
       is undone before any project `bool` (for example net/netenet.h putting
       `s32` back right after the vendored enet.h) is listed as contained, not
       as a problem;
     - scope leaks: a header that pushes `bool` with push_macro and ends
       without popping it, so its meaning leaks into the including file;
     - preprocessing errors.

The deliberate pattern for a header whose structs are shared with C++ code
and must stay 1-byte in C (the fast3d gfx headers) is:

    #pragma push_macro("bool")
    #undef bool
    #ifndef __cplusplus
    #define bool _Bool
    #endif
    ... declarations ...
    #pragma pop_macro("bool")

GCC's `-E -dD` output does not show these pragmas: each becomes a blank line,
a pop also prints an `#undef bool` for the macro it removes, and the restored
definition is never printed. So the audit reads the pragma lines from the
sources and applies them when the preprocessed output reaches those lines.

Exit status is 0 when there is nothing to report, 1 otherwise, 2 on usage or
preprocessing errors. Write a JSON report with --json.

Usage:
  python tools/bool_layout_audit.py --compile-commands <build>/compile_commands.json
      [--target pd] [--jobs N] [--json report.json] [--allow-flip <path> ...]
      [--vendored <repo-relative prefix> ...]
"""

import argparse
import concurrent.futures
import json
import os
import re
import shlex
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER_EXTS = {".h", ".hpp", ".hh", ".inc", ".hxx"}
SKIP_DIRS = {".git", ".claude", "Build", "build", "dist", "_old", "node_modules"}
# Third-party code compiled into the targets. Its own lines may run under a
# flipped `bool`; project lines may not.
DEFAULT_VENDORED = ("port/external/", "port/include/external/", "port/fast3d/imgui/",
                    "port/fast3d/glad/")

LINE_MARKER = re.compile(r'^# (\d+) "((?:[^"\\]|\\.)*)"((?: \d)*)\s*$')
DEFINE_BOOL = re.compile(r"^\s*#\s*define\s+bool\s+(\S+)")
UNDEF_BOOL = re.compile(r"^\s*#\s*undef\s+bool\b")
PRAGMA_PUSH = re.compile(r'^\s*#\s*pragma\s+push_macro\s*\(\s*"bool"\s*\)')
PRAGMA_POP = re.compile(r'^\s*#\s*pragma\s+pop_macro\s*\(\s*"bool"\s*\)')
BOOL_TOKEN = re.compile(r"(?<![\w$])bool(?![\w$])")
SIZE_OF = {"s32": 4, "u32": 4, "int": 4, "_Bool": 1, "c++bool": 1, "u8": 1}
MAX_SAMPLE_LINES = 5
NO_INFO = ({}, frozenset(), frozenset(), {})

# Reading the declared name of one file-scope declaration line.
ATTRIBUTE = re.compile(r"__attribute__\s*\(\(.*?\)\)|__declspec\s*\([^)]*\)|\[\[.*?\]\]")
NOT_A_SYMBOL = re.compile(r"(?<![\w$])(typedef|static|using|template|friend|return|goto|case|"
                          r"default|operator)(?![\w$])")
FUNC_POINTER = re.compile(r"\(\s*\*+\s*(?:const\s+)?([A-Za-z_]\w*)\s*\)")
LAST_IDENT = re.compile(r"([A-Za-z_]\w*)\s*$")
TYPE_HEAD = re.compile(r"(?<![\w$])(struct|union|enum|class)(\s+\w+)?\s*$")
NAME_KEYWORDS = {"bool", "_Bool", "s32", "u32", "int", "void", "char", "const", "extern",
                 "inline", "unsigned", "signed", "struct", "enum", "union", "volatile",
                 "register", "auto", "if", "while", "for", "switch", "sizeof", "decltype",
                 "typeof", "__typeof__", "alignas", "_Alignas", "static_assert",
                 "_Static_assert", "noexcept", "constexpr", "class", "namespace"}


def norm(path):
    return os.path.normcase(os.path.normpath(str(path))).replace("\\", "/")


def strip_comments_and_strings(text):
    """Blank comments and string/char literals, keeping every newline."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join("\n" if ch == "\n" else " " for ch in text[i:j]))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(" " * (j - i))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def declared_name(text):
    """Best-effort name of the entity one file-scope declaration line declares.

    `text` has comments and strings blanked. Returns None for typedefs,
    internal-linkage (static) declarations, qualified C++ names and lines whose
    name cannot be read: none of those is a cross-file symbol.
    """
    s = ATTRIBUTE.sub(" ", text).strip()
    if not s or NOT_A_SYMBOL.search(s):
        return None
    cut = len(s)
    for ch in "(=[;,{":
        at = s.find(ch)
        if at >= 0:
            cut = min(cut, at)
    head = s[:cut]
    if "::" in head:
        return None
    if cut < len(s) and s[cut] == "{" and TYPE_HEAD.search(head):
        return None  # a struct/union/enum definition, not a symbol
    if cut < len(s) and s[cut] == "(":
        m = FUNC_POINTER.match(s, cut)
        if m:
            return m.group(1)
    m = LAST_IDENT.search(head)
    if m and m.group(1) not in NAME_KEYWORDS:
        return m.group(1)
    return None


def classify_code(code):
    """Classify every line of `code` that uses the token bool.

    `code` has comments and strings blanked. Returns (sites, names). sites maps
    line -> kind: 'member' (inside a struct/union/class body), 'body' (inside a
    function body), 'directive' (a preprocessor line), 'nsdecl' (a file-scope
    declaration inside a C++ namespace) or 'decl' (any other file-scope
    declaration: prototype, extern variable, function definition, typedef).
    names maps each 'decl' line whose entity has external linkage to its name;
    the lines of a multi-line declaration all get the name of its first line.
    """
    sites, names = {}, {}
    stack = []      # kinds of the braces that are open at the start of each line
    paren = 0       # open parentheses of the file-scope declaration in progress
    pending = None  # name of that declaration, None if it is not a symbol
    lines = code.split("\n")
    for idx, text in enumerate(lines, start=1):
        stripped = text.strip()
        directive = stripped.startswith("#")
        file_scope = not any(k in ("function", "aggregate", "initializer", "block") for k in stack)
        if file_scope and paren == 0 and stripped and not directive:
            pending = declared_name(text)
        if BOOL_TOKEN.search(text):
            if directive:
                sites[idx] = "directive"
            elif "function" in stack:
                sites[idx] = "body"
            elif "aggregate" in stack:
                sites[idx] = "member"
            elif "namespace" in stack:
                sites[idx] = "nsdecl"
            else:
                sites[idx] = "decl"
                if pending is not None and file_scope:
                    names[idx] = pending
        if file_scope and not directive:
            paren = max(0, paren + text.count("(") - text.count(")"))
        # update the stack with this line's braces
        for pos, ch in enumerate(text):
            if ch == "{":
                prefix = "\n".join(lines[max(0, idx - 6):idx - 1]) + "\n" + text[:pos]
                tail = prefix.rstrip()
                if re.search(r"(struct|union|class|enum)(\s+\w+)?(\s*:\s*[\w\s,:<>]+)?\s*$", tail):
                    stack.append("aggregate")
                elif re.search(r"\)\s*(const|noexcept|override|final|\s)*$", tail):
                    stack.append("function")
                elif tail.endswith("="):
                    stack.append("initializer")
                elif re.search(r"(?<![\w$])extern\s*$", tail):
                    # `extern "C" {` (the string literal is blanked)
                    stack.append("externc")
                elif re.search(r"(?<![\w$])namespace(\s+\w+)?\s*$", tail):
                    stack.append("namespace")
                else:
                    stack.append("block")
            elif ch == "}":
                if stack:
                    stack.pop()
    return sites, names


def scan_file(path):
    """Return (bool sites, push_macro lines, pop_macro lines, symbol names) of one file."""
    try:
        raw = Path(path).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return NO_INFO
    code = strip_comments_and_strings(raw)
    pushes, pops = set(), set()
    for idx, (raw_line, code_line) in enumerate(zip(raw.split("\n"), code.split("\n")), start=1):
        if not code_line.lstrip().startswith("#"):
            continue  # a pragma inside a comment
        if PRAGMA_PUSH.match(raw_line):
            pushes.add(idx)
        elif PRAGMA_POP.match(raw_line):
            pops.add(idx)
    sites, names = classify_code(code)
    return sites, frozenset(pushes), frozenset(pops), names


def classify_sites(path):
    """Return {line_number: kind} for every line of a file that uses the token bool."""
    return scan_file(path)[0]


def collect_header_info(root):
    """Map every project header to its scan_file() result when it has anything."""
    info = {}
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for name in filenames:
            if os.path.splitext(name)[1].lower() in HEADER_EXTS:
                path = os.path.join(dirpath, name)
                found = scan_file(path)
                if found[0] or found[1] or found[2]:
                    info[norm(path)] = found
    return info


def compile_rows(database, target):
    rows = json.loads(Path(database).read_text(encoding="utf-8-sig"))
    marker = "CMakeFiles/" + target + ".dir/"
    picked = []
    for row in rows:
        output = str(row.get("output", row.get("command", ""))).replace("\\", "/")
        if marker in output:
            picked.append(row)
    return picked


def preprocess_args(row):
    args = row.get("arguments") or shlex.split(row["command"], posix=False)
    out, skip = [], False
    for a in args:
        if skip:
            skip = False
            continue
        if a in ("-o", "-MF", "-MT", "-MQ"):
            skip = True
            continue
        if a in ("-c", "-MD", "-MMD"):
            continue
        out.append(a.strip('"'))
    return out + ["-E", "-dD", "-o", "-"]


def is_cxx(path):
    return os.path.splitext(path)[1].lower() in (".cpp", ".cc", ".cxx")


def rel(path, root_norm):
    return path[len(root_norm):].lstrip("/") if path.startswith(root_norm) else path


def audit_tu(row, headers, root_norm, vendored):
    source = Path(row["file"])
    if not source.is_absolute():
        source = Path(row["directory"]) / source
    tu = norm(source)
    cxx = is_cxx(tu)
    own = scan_file(source) if tu.startswith(root_norm) else NO_INFO
    proc = subprocess.run(preprocess_args(row), cwd=row["directory"], capture_output=True,
                          text=True, encoding="latin-1")
    if proc.returncode != 0:
        return tu, None, proc.stderr[-400:]

    def file_info(path):
        if path == tu:
            return own
        return headers.get(path, NO_INFO)

    def project_file(path):
        if not path.startswith(root_norm):
            return False
        r = rel(path, root_norm)
        return not any(r.startswith(v) for v in vendored)

    meaning = None    # what `bool` expands to right now (None: not a macro)
    baseline = None   # last meaning set outside any push_macro scope
    pushes = []       # open push_macro("bool") scopes
    last_pop = None   # (file, line) of the pop just applied
    cur_file = tu
    cur_line = 1
    stack = []        # include stack: (includer, line of the #include)
    seen = {}         # (header, line) -> meaning
    symbols = {}      # (name, file, line) -> meaning, for named declarations
    windows = []      # flips outside any scope, C only
    window = None     # the flip that is open now
    leaks = []        # headers that pushed bool and ended without popping it

    def origin():
        # The nearest file under the root on the include stack is the one that
        # pulled the flip in; its line is the #include line.
        for f, ln in reversed(stack):
            if f.startswith(root_norm):
                return f"{rel(f, root_norm)}:{ln}"
        return f"{rel(cur_file, root_norm)}:{cur_line}"

    def apply_pop():
        nonlocal meaning, window
        if not pushes:
            return
        meaning = pushes.pop()["meaning"]
        if not pushes and window is not None and meaning == "s32":
            window["closed_at"] = f"{rel(cur_file, root_norm)}:{cur_line}"
            window = None

    for text in proc.stdout.split("\n"):
        m = LINE_MARKER.match(text)
        if m:
            new_file = norm(m.group(2).encode("latin-1").decode("unicode_escape"))
            flags = m.group(3).split()
            if "1" in flags:
                stack.append((cur_file, cur_line))
            elif "2" in flags and stack:
                stack.pop()
                for p in pushes:
                    if p["level"] > len(stack) and not p["leaked"]:
                        p["leaked"] = True
                        leaks.append(f"{rel(p['file'], root_norm)}:{p['line']}")
            cur_file = new_file
            cur_line = int(m.group(1))
            continue
        sites, push_lines, pop_lines, names = file_info(cur_file)
        d = DEFINE_BOOL.match(text)
        if d:
            last_pop = None
            new = d.group(1)
            if not pushes:
                if not cxx and new == "_Bool" and baseline == "s32" and window is None:
                    window = {"origin": origin(), "closed_at": None, "project_lines": 0,
                              "first_lines": []}
                    windows.append(window)
                elif new == "s32" and window is not None:
                    window["closed_at"] = f"{rel(cur_file, root_norm)}:{cur_line}"
                    window = None
                baseline = new
            meaning = new
        elif UNDEF_BOOL.match(text):
            if cur_line in pop_lines:
                # -dD prints the macro a pop removes as an #undef on the pop's
                # line; the pop itself is applied at the pragma's blank line.
                if last_pop != (cur_file, cur_line):
                    apply_pop()
                    last_pop = (cur_file, cur_line)
            else:
                last_pop = None
                meaning = None
        elif cur_line in push_lines:
            last_pop = None
            pushes.append({"meaning": meaning, "level": len(stack), "leaked": False,
                           "file": cur_file, "line": cur_line})
        elif cur_line in pop_lines:
            if last_pop != (cur_file, cur_line):
                apply_pop()
                last_pop = (cur_file, cur_line)
        else:
            last_pop = None
            kind = sites.get(cur_line)
            if kind is not None:
                eff = meaning if meaning is not None else ("c++bool" if cxx else "undefined")
                if cur_file != tu:
                    seen[(cur_file, cur_line)] = eff
                name = names.get(cur_line)
                if name is not None and project_file(cur_file):
                    symbols[(name, cur_file, cur_line)] = eff
                if window is not None and kind != "directive" and project_file(cur_file):
                    window["project_lines"] += 1
                    sample = f"{rel(cur_file, root_norm)}:{cur_line}"
                    if len(window["first_lines"]) < MAX_SAMPLE_LINES \
                            and sample not in window["first_lines"]:
                        window["first_lines"].append(sample)
        cur_line += 1
    return tu, {"cxx": cxx, "seen": seen, "symbols": symbols, "windows": windows,
                "leaks": leaks}, None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--compile-commands", required=True)
    parser.add_argument("--target", default="pd")
    parser.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) - 2))
    parser.add_argument("--json")
    parser.add_argument("--allow-flip", action="append", default=[],
                        help="repo-relative C file allowed to flip bool (should stay empty)")
    parser.add_argument("--vendored", action="append", default=None,
                        help="repo-relative prefix of third-party code (default: "
                             + ", ".join(DEFAULT_VENDORED) + ")")
    parser.add_argument("--root", default=str(ROOT),
                        help="source tree whose headers are audited (tests use a temporary tree)")
    args = parser.parse_args()

    root = Path(args.root).resolve()
    root_norm = norm(root) + "/"
    vendored = tuple(norm(v).rstrip("/") + "/" for v in (args.vendored or DEFAULT_VENDORED))
    headers = collect_header_info(root)
    rows = compile_rows(args.compile_commands, args.target)
    if not rows:
        print("no translation units for target", args.target, file=sys.stderr)
        return 2

    results, errors = {}, {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(audit_tu, row, headers, root_norm, vendored) for row in rows]
        for fut in concurrent.futures.as_completed(futures):
            tu, res, err = fut.result()
            if err is not None:
                errors[rel(tu, root_norm)] = err
            else:
                results[rel(tu, root_norm)] = res

    by_site = {}
    by_symbol = {}
    for tu, res in results.items():
        for (header, line), eff in res["seen"].items():
            by_site.setdefault((header, line), {}).setdefault(eff, []).append(tu)
        for (name, path, line), eff in res["symbols"].items():
            where = f"{rel(path, root_norm)}:{line}"
            by_symbol.setdefault(name, {}).setdefault(eff, {}).setdefault(where, []).append(tu)

    mixed = []
    for (header, line), meanings in sorted(by_site.items()):
        kind = headers.get(header, NO_INFO)[0].get(line, "?")
        if kind in ("body", "directive"):
            continue
        sizes = {SIZE_OF.get(m, -1) for m in meanings}
        if len(sizes) > 1:
            src = ""
            try:
                src = Path(header).read_text(encoding="utf-8", errors="replace").split("\n")[line - 1].strip()
            except (OSError, IndexError):
                pass
            mixed.append({
                "header": rel(header, root_norm), "line": line, "kind": kind, "source": src,
                "meanings": {m: sorted(t) for m, t in meanings.items()},
            })

    mixed_symbols = []
    for name, meanings in sorted(by_symbol.items()):
        sizes = {SIZE_OF.get(m, -1) for m in meanings}
        if len(sizes) > 1:
            mixed_symbols.append({
                "symbol": name,
                "meanings": {m: {where: sorted(tus) for where, tus in sorted(places.items())}
                             for m, places in sorted(meanings.items())},
            })

    allowed = {norm(a).lstrip("/") for a in args.allow_flip}
    flips, contained = [], []
    for tu, res in sorted(results.items()):
        for w in res["windows"]:
            entry = {"tu": tu, "pulled_in_by": [w["origin"]], "closed_at": w["closed_at"],
                     "project_lines": w["project_lines"], "first_project_lines": w["first_lines"]}
            if w["project_lines"] and tu not in allowed:
                flips.append(entry)
            else:
                contained.append(entry)
    leaks = [{"tu": tu, "push": push} for tu, res in sorted(results.items()) for push in res["leaks"]]

    report = {
        "schema": "pd2.bool-layout-audit.v3",
        "target": args.target,
        "translation_units": len(results),
        "header_bool_sites": sum(len(v[0]) for v in headers.values()),
        "named_symbols": len(by_symbol),
        "mixed_sites": mixed,
        "mixed_symbols": mixed_symbols,
        "c_flips": flips,
        "contained_flips": contained,
        "scope_leaks": leaks,
        "errors": errors,
        "passed": not mixed and not mixed_symbols and not flips and not leaks and not errors,
    }
    if args.json:
        Path(args.json).write_text(json.dumps(report, indent=1), encoding="utf-8")

    print(f"bool layout audit, target {args.target}: {len(results)} translation units, "
          f"{report['header_bool_sites']} header bool sites, {len(by_symbol)} named symbols")
    print(f"  mixed-width header declarations: {len(mixed)}")
    for site in mixed:
        widths = ", ".join(f"{m} in {len(t)}" for m, t in sorted(site["meanings"].items()))
        print(f"    {site['header']}:{site['line']} [{site['kind']}] {site['source'][:90]}  ({widths})")
    print(f"  symbols declared or defined with two bool widths: {len(mixed_symbols)}")
    for sym in mixed_symbols:
        parts = []
        for m, places in sym["meanings"].items():
            listed = ", ".join(f"{where} ({len(tus)} files)" for where, tus in list(places.items())[:3])
            more = f" and {len(places) - 3} more" if len(places) > 3 else ""
            parts.append(f"{m}: {listed}{more}")
        print(f"    {sym['symbol']}  -- " + "; ".join(parts))
    print(f"  C files where project code runs with bool flipped from s32 to _Bool: {len(flips)}")
    for flip in flips:
        print(f"    {flip['tu']}  <- {', '.join(flip['pulled_in_by'])}"
              f"  ({flip['project_lines']} project lines, first {', '.join(flip['first_project_lines'])})")
    print(f"  contained flips (vendored or system code only, undone before project code): {len(contained)}")
    for flip in contained:
        where = f"undone at {flip['closed_at']}" if flip["closed_at"] else "vendored code to end of file"
        print(f"    {flip['tu']}  <- {', '.join(flip['pulled_in_by'])}  ({where})")
    print(f"  headers that push bool and do not pop it: {len(leaks)}")
    for leak in leaks:
        print(f"    {leak['push']}  (in {leak['tu']})")
    if errors:
        print(f"  preprocessing errors: {len(errors)}")
        for tu, err in list(errors.items())[:10]:
            print(f"    {tu}: {err.strip()[:200]}")
        return 2
    print("  result:", "PASS" if report["passed"] else "FAIL")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
