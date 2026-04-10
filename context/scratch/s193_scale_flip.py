#!/usr/bin/env python3
"""
S193 Phase 2: mechanical ×1.5 transform of pdguiScale(X.Yf) literals.

Context: pdgui_scaling.h baseline was flipped from 720p to 1080p.  Every
existing pdguiScale(N.0f) call was written against the 720p baseline, so
to preserve visual behavior at every resolution the literal must be
multiplied by 1080/720 = 1.5.

Rules:
- Only matches calls of the form pdguiScale(LITERAL) where LITERAL is a
  plain float literal (digits + optional decimal + optional 'f' suffix).
  Calls containing variables or expressions are left alone by design.
- pdgui_scaling.h is skipped (it defines the helper and has its own
  semantics).
- pdgui_menu_matchsetup.cpp.retired is skipped (retired file).
- Results rounded to 2 decimals, trailing .00 stripped so the output
  looks clean.

Dry-run mode prints a diff summary.  Apply mode writes back in place.
"""
import argparse
import re
import sys
from pathlib import Path

LITERAL_RE = re.compile(
    r"pdguiScale\(\s*([0-9]+(?:\.[0-9]+)?)f?\s*\)",
    re.ASCII,
)

SKIP_FILES = {
    "pdgui_scaling.h",
    "pdgui_menu_matchsetup.cpp.retired",
}


def convert(match: re.Match) -> str:
    val = float(match.group(1))
    new_val = val * 1.5
    # Round to 4 decimals and strip trailing zeros / point.
    s = f"{new_val:.4f}".rstrip("0").rstrip(".")
    if "." not in s:
        s = s + ".0"
    return f"pdguiScale({s}f)"


def process_file(path: Path, apply: bool) -> tuple[int, list[tuple[int, str, str]]]:
    text = path.read_text(encoding="utf-8")
    diffs: list[tuple[int, str, str]] = []
    count = 0

    def line_replacer(line: str) -> str:
        nonlocal count
        if "pdguiScale(" not in line:
            return line

        def sub(m: re.Match) -> str:
            nonlocal count
            count += 1
            old = m.group(0)
            new = convert(m)
            return new

        return LITERAL_RE.sub(sub, line)

    new_lines: list[str] = []
    for i, line in enumerate(text.splitlines(keepends=True), start=1):
        new_line = line_replacer(line)
        if new_line != line:
            diffs.append((i, line.rstrip("\n"), new_line.rstrip("\n")))
        new_lines.append(new_line)

    new_text = "".join(new_lines)

    if apply and new_text != text:
        path.write_text(new_text, encoding="utf-8")

    return count, diffs


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true", help="Write back")
    parser.add_argument("paths", nargs="+", help="Files to process")
    args = parser.parse_args()

    total = 0
    for p in args.paths:
        path = Path(p)
        if path.name in SKIP_FILES:
            print(f"SKIP: {path}")
            continue
        count, diffs = process_file(path, args.apply)
        total += count
        print(f"{'APPLY' if args.apply else 'DRY'}: {path}  {count} call(s)")
        for ln, old, new in diffs:
            old_s = old.strip()
            new_s = new.strip()
            if old_s != new_s:
                print(f"  L{ln}: {old_s}")
                print(f"    -> {new_s}")

    print(f"\nTOTAL: {total} pdguiScale() literal transformations")
    return 0


if __name__ == "__main__":
    sys.exit(main())
