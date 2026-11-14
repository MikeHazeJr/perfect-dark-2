"""Adversarial source guard checks; fixtures are in memory, no game or cleanup."""

import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("guard", Path(__file__).resolve().parents[1] / "native_write_policy_guard.py")
guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guard)


class NativeWriteSourceGuardTests(unittest.TestCase):
    def test_unwrapped_and_dynamic_routes_fail_even_with_wrapped_fopen_present(self):
        unsupported, wrapped, _ = guard.scan_source('fopen(p,"wb"); CreateHardLinkW(a,b,0); GetProcAddress(m,n);', {"fopen"})
        self.assertEqual(unsupported, ["CreateHardLinkW", "GetProcAddress"])
        self.assertEqual(wrapped, ["fopen"])

    def test_comments_and_strings_do_not_create_escape_routes(self):
        self.assertEqual(guard.scan_source('/* _creat(a); */ // GetProcAddress(x);\n const char *s="NtWriteFile(a)";', {"fopen"}), ([], [], False))

    def test_cpp_filesystem_requires_review_but_stream_is_a_guarded_crt_route(self):
        unsupported, _, streams = guard.scan_source('std::ofstream f(p); std::filesystem::remove(p);', {"remove"})
        self.assertEqual(unsupported, ["C++ filesystem"])
        self.assertTrue(streams)

    def test_raw_import_bypasses_are_detected(self):
        for source in ['__imp_CreateFileW(a,b);', '__real_CreateFileW(a,b);', '__real___imp_CreateFileW(a,b);']:
            self.assertIn("raw wrapped API import", guard.scan_source(source, {"CreateFileW"})[0])

    def test_dynamic_function_pointer_and_review_drift_are_detected(self):
        self.assertEqual(guard.scan_source('auto lookup = &GetProcAddress;', set())[0], ["GetProcAddress"])
        original = 'GetProcAddress(module, "BCryptGenRandom");'
        changed = 'GetProcAddress(module, "WriteFile");'
        self.assertNotEqual(guard.review_fingerprint(original, ["GetProcAddress"]), guard.review_fingerprint(changed, ["GetProcAddress"]))
        self.assertEqual(guard.review_fingerprint(original + '\r\n', ["GetProcAddress"]), guard.review_fingerprint(original + '\n', ["GetProcAddress"]))

    def test_review_window_is_the_real_lines_around_the_call(self):
        # Twelve filler lines on each side keep the nine-line window clear of
        # anything that is added above the call.
        before = "".join("int v%d;\n" % i for i in range(12))
        after = "".join("int w%d;\n" % i for i in range(12))
        plain = before + 'h = LoadLibraryA("x.dll");\n' + after
        routes = ["LoadLibraryA"]
        base = guard.review_fingerprint(plain, routes)
        # A block comment far above the call spans lines the token blanker
        # removes. It must not move the window ...
        commented = "/* one\n * two\n * three\n */\n" + plain
        self.assertEqual(guard.review_fingerprint(commented, routes), base)
        # ... and growing or shrinking that comment must not change the pin.
        self.assertEqual(guard.review_fingerprint("/* one\n * two\n * three\n * four\n * five\n */\n" + plain, routes), base)
        # An edit four lines below the call is inside the window even when a
        # comment sits above; five lines below is outside it.
        self.assertNotEqual(guard.review_fingerprint(commented.replace("int w3;", "int w3 = 1;"), routes), base)
        self.assertEqual(guard.review_fingerprint(commented.replace("int w4;", "int w4 = 1;"), routes), base)
        self.assertNotEqual(guard.review_fingerprint(commented.replace("int v8;", "int v8 = 1;"), routes), base)
        self.assertEqual(guard.review_fingerprint(commented.replace("int v7;", "int v7 = 1;"), routes), base)

    def test_review_window_counts_only_newlines_as_line_ends(self):
        before = "".join("int v%d;\n" % i for i in range(12))
        after = "".join("int w%d;\n" % i for i in range(12))
        plain = before + 'h = LoadLibraryA("x.dll");\n' + after
        base = guard.review_fingerprint(plain, ["LoadLibraryA"])
        # Form feed, a lone carriage return and 0x85 (a comment character in
        # legacy latin-1 sources) are not line ends for the pin.
        for odd in ("// caf\x85 note\n", "\x0c\n", "int a;\rint b;\n"):
            self.assertEqual(guard.review_fingerprint(odd + plain, ["LoadLibraryA"]), base)

    def test_every_reviewed_pin_matches_this_checkout(self):
        wrapped = guard.canonical_symbols()
        for relative, review in guard.REVIEWED.items():
            source = (guard.ROOT / relative).read_bytes().decode("latin-1")
            unsupported, _, _ = guard.scan_source(source, wrapped)
            self.assertEqual(set(unsupported), set(review["routes"]), relative)
            self.assertEqual(guard.review_fingerprint(source, unsupported), review["sha256"], relative)

    def test_symbol_inventory_is_canonical_and_unique(self):
        self.assertGreaterEqual(len(guard.canonical_symbols()), 63)
        self.assertIn("CreateFileW", guard.canonical_symbols())


if __name__ == "__main__":
    unittest.main()
