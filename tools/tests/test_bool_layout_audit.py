"""Self-test for tools/bool_layout_audit.py against tiny synthetic trees.

The trees reproduce SP-75 in miniature:
  - projtypes.h is the project-style `#define bool s32`;
  - shared.h and after.h are project headers that say `bool`;
  - flip.h pulls <stdbool.h> into a C file (the bug);
  - gfxlike.h is the deliberate scoped 1-byte pattern of the fast3d headers
    (push_macro, _Bool in C, keyword in C++, pop_macro);
  - include/external/vend.h is vendored code that includes <stdbool.h>, and
    wrap.h puts `s32` back right after it, like net/netenet.h.

Needs gcc and g++ on PATH, like the rest of the native build.
"""

import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOL = Path(__file__).resolve().parents[1] / "bool_layout_audit.py"
HAVE_COMPILERS = shutil.which("gcc") is not None and shutil.which("g++") is not None

GUARD = "#ifndef {0}\n#define {0}\n{1}#endif\n"
SCOPE_OPEN = ('#pragma push_macro("bool")\n#undef bool\n'
              "#ifndef __cplusplus\n#define bool _Bool\n#endif\n")
SCOPE_CLOSE = '#pragma pop_macro("bool")\n'


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def database(root, units):
    rows = []
    for name in units:
        src = root / name
        compiler = "g++" if name.endswith(".cpp") else "gcc"
        std = "-std=c++20" if name.endswith(".cpp") else "-std=gnu11"
        rows.append({
            "directory": str(root),
            "file": str(src),
            "command": f'{compiler} {std} -I "{root / "include"}" -c "{src}" -o "{root / (name + ".o")}"',
            "output": f"CMakeFiles/fake.dir/{name}.o",
        })
    cc = root / "compile_commands.json"
    cc.write_text(json.dumps(rows), encoding="utf-8")
    return cc


def run_audit(root, cc, *extra):
    report = root / "report.json"
    if report.exists():
        report.unlink()
    proc = subprocess.run([sys.executable, "-B", str(TOOL), "--root", str(root),
                           "--compile-commands", str(cc), "--target", "fake",
                           "--jobs", "2", "--json", str(report), *extra],
                          capture_output=True, text=True)
    data = json.loads(report.read_text(encoding="utf-8")) if report.exists() else None
    return proc, data


@unittest.skipUnless(HAVE_COMPILERS, "gcc and g++ are required")
class BoolLayoutAuditTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="pd-bool-audit-"))
        inc = self.tmp / "include"
        write(inc / "projtypes.h", GUARD.format("PROJTYPES_H", "typedef int s32;\n#define bool s32\n"))
        write(inc / "after.h", GUARD.format("AFTER_H",
              '#ifdef __cplusplus\nextern "C" {\n#endif\n'
              "bool after_ready(void);\n"
              "#ifdef __cplusplus\n}\n#endif\n"))

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def output(self, proc):
        return proc.stdout + proc.stderr

    def make_shared_tree(self, fixed):
        inc = self.tmp / "include"
        member = "s32 flag;" if fixed else "bool flag;"
        proto = "s32 shared_ready(void);" if fixed else "bool shared_ready(void);"
        write(inc / "shared.h", GUARD.format("SHARED_H",
              "#ifdef __cplusplus\nextern \"C\" {\n#endif\n"
              "struct shared_state {\n\t" + member + "\n\tint count;\n};\n"
              + proto + "\n"
              "static inline int shared_local(int v)\n{\n\tbool b = v != 0;\n\treturn b;\n}\n"
              "#ifdef __cplusplus\n}\n#endif\n"))
        write(inc / "flip.h", GUARD.format("FLIP_H", "#include <stdbool.h>\n"))
        write(self.tmp / "a.c", '#include "projtypes.h"\n#include "shared.h"\n'
              "struct shared_state g_a;\n")
        flip = "" if fixed else '#include "flip.h"\n'
        write(self.tmp / "b.c", '#include "projtypes.h"\n' + flip + '#include "shared.h"\n'
              "struct shared_state g_b;\n")
        write(self.tmp / "c.cpp", "typedef int s32;\n" + '#include "shared.h"\n'
              "shared_state g_c;\n")
        return database(self.tmp, ["a.c", "b.c", "c.cpp"])

    def make_scoped_tree(self, pop):
        inc = self.tmp / "include"
        write(inc / "gfxlike.h", GUARD.format("GFXLIKE_H",
              SCOPE_OPEN
              + '#ifdef __cplusplus\nextern "C" {\n#endif\n'
              "struct gfx_settings {\n\tbool fullscreen;\n\tint width;\n};\n"
              "bool gfx_ready(void);\n"
              "#ifdef __cplusplus\n}\n#endif\n"
              + (SCOPE_CLOSE if pop else "")))
        write(self.tmp / "a.c", '#include "projtypes.h"\n#include "gfxlike.h"\n'
              '#include "after.h"\nstruct gfx_settings g_a;\nbool a_local;\n')
        write(self.tmp / "b.c", '#include "projtypes.h"\n#include "after.h"\nbool b_local;\n')
        write(self.tmp / "c.cpp", '#include "gfxlike.h"\ngfx_settings g_c;\n')
        return database(self.tmp, ["a.c", "b.c", "c.cpp"])

    def make_vendored_tree(self, restore):
        inc = self.tmp / "include"
        write(inc / "external" / "vend.h", GUARD.format("VEND_H",
              "#include <stdbool.h>\nbool vend_ok(void);\n"))
        write(inc / "wrap.h", GUARD.format("WRAP_H",
              '#include "external/vend.h"\n'
              + ("#undef bool\n#define bool s32\n" if restore else "")))
        write(self.tmp / "a.c", '#include "projtypes.h"\n#include "wrap.h"\n'
              '#include "after.h"\nbool a_local;\n')
        write(self.tmp / "b.c", '#include "projtypes.h"\n#include "after.h"\n')
        return database(self.tmp, ["a.c", "b.c"])

    def make_symbol_tree(self, cxx_sees_s32):
        # a.c defines a C array and function with the project s32 bool; the
        # C++ file declares them itself, as pdgui_menu_mainmenu.cpp did.
        write(self.tmp / "a.c", '#include "projtypes.h"\n'
              "bool g_flags[4];\n"
              "bool flag_ready(s32 index)\n{\n\treturn g_flags[index] != 0;\n}\n"
              "static bool a_local_helper(void)\n{\n\treturn 1;\n}\n")
        prefix = '#include "projtypes.h"\n' if cxx_sees_s32 else "typedef int s32;\n"
        write(self.tmp / "c.cpp", prefix +
              'extern "C" {\nextern bool g_flags[];\nbool flag_ready(s32 index);\n}\n'
              "static bool a_local_helper(void)\n{\n\treturn true;\n}\n"
              "int use(void) { return g_flags[1] + flag_ready(2) + a_local_helper(); }\n")
        return database(self.tmp, ["a.c", "c.cpp"])

    def test_reports_a_symbol_declared_with_two_widths(self):
        proc, data = run_audit(self.tmp, self.make_symbol_tree(cxx_sees_s32=False))
        self.assertEqual(proc.returncode, 1, self.output(proc))
        self.assertEqual(data["mixed_sites"], [])  # no header is involved
        symbols = {s["symbol"]: s for s in data["mixed_symbols"]}
        self.assertEqual(set(symbols), {"g_flags", "flag_ready"}, data["mixed_symbols"])
        flags = symbols["g_flags"]["meanings"]
        self.assertEqual(flags["s32"], {"a.c:2": ["a.c"]})
        self.assertEqual(flags["c++bool"], {"c.cpp:3": ["c.cpp"]})
        # The two static helpers share a name but are different functions.
        self.assertNotIn("a_local_helper", symbols)

    def test_symbols_with_one_width_pass(self):
        proc, data = run_audit(self.tmp, self.make_symbol_tree(cxx_sees_s32=True))
        self.assertEqual(proc.returncode, 0, self.output(proc))
        self.assertEqual(data["mixed_symbols"], [])
        self.assertGreaterEqual(data["named_symbols"], 2)

    def test_reports_mixed_widths_and_the_flip(self):
        proc, data = run_audit(self.tmp, self.make_shared_tree(fixed=False))
        self.assertEqual(proc.returncode, 1, self.output(proc))
        self.assertIsNotNone(data)
        mixed = {(m["header"], m["kind"]) for m in data["mixed_sites"]}
        self.assertIn(("include/shared.h", "member"), mixed)
        self.assertIn(("include/shared.h", "decl"), mixed)
        # The bool inside the static inline body is TU-local and not reported.
        self.assertNotIn(("include/shared.h", "body"), mixed)
        member = next(m for m in data["mixed_sites"] if m["kind"] == "member")
        self.assertIn("s32", member["meanings"])
        self.assertIn("_Bool", member["meanings"])
        self.assertIn("c++bool", member["meanings"])
        self.assertEqual({f["tu"] for f in data["c_flips"]}, {"b.c"})
        flip = data["c_flips"][0]
        self.assertTrue(any(p.startswith("include/flip.h:") for p in flip["pulled_in_by"]), flip)
        self.assertGreater(flip["project_lines"], 0, flip)
        self.assertTrue(any(p.startswith("include/shared.h:") for p in flip["first_project_lines"]), flip)
        self.assertIsNone(flip["closed_at"], flip)

    def test_fixed_tree_passes(self):
        proc, data = run_audit(self.tmp, self.make_shared_tree(fixed=True))
        self.assertEqual(proc.returncode, 0, self.output(proc))
        self.assertTrue(data["passed"])
        self.assertEqual(data["mixed_sites"], [])
        self.assertEqual(data["c_flips"], [])

    def test_scoped_one_byte_header_passes_and_restores_bool(self):
        proc, data = run_audit(self.tmp, self.make_scoped_tree(pop=True))
        self.assertEqual(proc.returncode, 0, self.output(proc))
        self.assertTrue(data["passed"], data)
        self.assertEqual(data["c_flips"], [])
        self.assertEqual(data["contained_flips"], [])
        self.assertEqual(data["scope_leaks"], [])

    def test_scope_that_is_never_popped_is_reported(self):
        proc, data = run_audit(self.tmp, self.make_scoped_tree(pop=False))
        self.assertEqual(proc.returncode, 1, self.output(proc))
        self.assertEqual({leak["tu"] for leak in data["scope_leaks"]}, {"a.c", "c.cpp"})
        self.assertTrue(all(leak["push"].startswith("include/gfxlike.h:")
                            for leak in data["scope_leaks"]), data["scope_leaks"])
        # after.h then reads as _Bool in a.c but s32 in b.c.
        after = [m for m in data["mixed_sites"] if m["header"] == "include/after.h"]
        self.assertEqual(len(after), 1, data["mixed_sites"])
        self.assertEqual(after[0]["meanings"].get("_Bool"), ["a.c"])
        self.assertEqual(after[0]["meanings"].get("s32"), ["b.c"])

    def test_vendored_flip_undone_before_project_code_is_contained(self):
        proc, data = run_audit(self.tmp, self.make_vendored_tree(restore=True),
                               "--vendored", "include/external/")
        self.assertEqual(proc.returncode, 0, self.output(proc))
        self.assertTrue(data["passed"], data)
        self.assertEqual(data["c_flips"], [])
        self.assertEqual(len(data["contained_flips"]), 1, data["contained_flips"])
        contained = data["contained_flips"][0]
        self.assertEqual(contained["tu"], "a.c")
        self.assertTrue(contained["pulled_in_by"][0].startswith("include/external/vend.h:"), contained)
        self.assertTrue(contained["closed_at"].startswith("include/wrap.h:"), contained)

    def test_vendored_flip_reaching_project_code_is_reported(self):
        proc, data = run_audit(self.tmp, self.make_vendored_tree(restore=False),
                               "--vendored", "include/external/")
        self.assertEqual(proc.returncode, 1, self.output(proc))
        self.assertEqual({f["tu"] for f in data["c_flips"]}, {"a.c"})
        lines = data["c_flips"][0]["first_project_lines"]
        self.assertTrue(any(p.startswith("include/after.h:") for p in lines), lines)
        self.assertTrue(any(p.startswith("a.c:") for p in lines), lines)


if __name__ == "__main__":
    unittest.main()
