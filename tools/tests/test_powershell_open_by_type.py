"""B-1283 / SP-100: no tracked PowerShell may open a file by its type.

On this project's Windows machines PATH puts msys directories ahead of
System32, and they hold an extensionless bash script named `cmd`. PowerShell
cannot execute it, so a bare `cmd ...` statement hands it to the Windows shell,
which opens it with the program registered for extensionless files. On
2026-11-21 that started a 3D-printing slicer about 260 times and crashed Claude.
Mike's rule: launch programs only by full .exe path, never open a file or folder
by its type.

This test refuses, in tracked .ps1/.psm1 code (comments ignored):
  * a bare `cmd` invocation (`cmd /c ...`); `cmd.exe` or `$env:ComSpec` are fine;
  * `Invoke-Item` and its alias `ii`.
It also pins the build prelude's `cmd` alias to System32's cmd.exe.
"""
import re
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SKIP_PREFIXES = ("context/_old/", "_old/")

BARE_CMD = re.compile(r"(?<![\w.$\\/:-])cmd\s+/[A-Za-z]")
INVOKE_ITEM = re.compile(r"\bInvoke-Item\b", re.IGNORECASE)
II_ALIAS = re.compile(r"(?:^|[;|{(]|&)\s*ii\s+\S", re.IGNORECASE)


def tracked_powershell():
    out = subprocess.run(["git", "ls-files", "--", "*.ps1", "*.psm1"], cwd=ROOT,
                         capture_output=True, text=True, check=True).stdout
    return [p for p in out.splitlines() if p and not p.startswith(SKIP_PREFIXES)]


def code_lines(text):
    """Yield (line number, code) with # comments and <# #> blocks removed.
    A '#' inside a quoted string is rare in these scripts; when one appears the
    rest of that line is skipped, which can only hide code, never invent it."""
    in_block = False
    for number, line in enumerate(text.splitlines(), 1):
        code = line
        if in_block:
            end = code.find("#>")
            if end < 0:
                continue
            code = code[end + 2:]
            in_block = False
        start = code.find("<#")
        if start >= 0:
            end = code.find("#>", start + 2)
            if end < 0:
                in_block = True
                code = code[:start]
            else:
                code = code[:start] + code[end + 2:]
        hash_at = code.find("#")
        if hash_at >= 0:
            code = code[:hash_at]
        if code.strip():
            yield number, code


class PowerShellOpenByTypeTests(unittest.TestCase):
    def test_no_bare_cmd_or_invoke_item_in_tracked_powershell(self):
        findings = []
        for relative in tracked_powershell():
            path = ROOT / relative
            try:
                text = path.read_text(encoding="utf-8-sig", errors="replace")
            except OSError:
                continue
            for number, code in code_lines(text):
                for label, pattern in (("bare cmd", BARE_CMD), ("Invoke-Item", INVOKE_ITEM), ("ii alias", II_ALIAS)):
                    if pattern.search(code):
                        findings.append(f"{relative}:{number}: {label}: {code.strip()[:120]}")
        self.assertEqual(findings, [], "launch programs by full .exe path (B-1283, SP-100):\n" + "\n".join(findings))

    def test_patterns_catch_the_incident_and_spare_full_paths(self):
        self.assertTrue(BARE_CMD.search('$links = cmd /c "dir /s /b /al $t"'))
        self.assertTrue(BARE_CMD.search('    cmd /c "rmdir /s /q `"$t`"" | Out-Null'))
        self.assertTrue(BARE_CMD.search('& cmd /C exit'))
        self.assertFalse(BARE_CMD.search('& cmd.exe /c "attrib -R x"'))
        self.assertFalse(BARE_CMD.search('& $env:ComSpec /c "attrib -R x"'))
        self.assertFalse(BARE_CMD.search('$cmd /c'))
        self.assertTrue(II_ALIAS.search('ii $folder'))
        self.assertFalse(II_ALIAS.search('$taxii = 1'))
        self.assertEqual([n for n, _ in code_lines("a\n# cmd /c x\n<#\ncmd /c y\n#>\nb # cmd /c z\n")], [1, 6])

    def test_build_prelude_aliases_cmd_to_system32(self):
        prelude = (ROOT / "devtools/_build-env-prelude.ps1").read_text(encoding="utf-8")
        self.assertIn("$systemCmd = Join-Path $env:SystemRoot 'System32\\cmd.exe'", prelude)
        self.assertIn("Set-Alias -Name cmd -Value $systemCmd -Scope Script -Force", prelude)


if __name__ == "__main__":
    unittest.main()
