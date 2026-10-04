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

    def test_symbol_inventory_is_canonical_and_unique(self):
        self.assertGreaterEqual(len(guard.canonical_symbols()), 63)
        self.assertIn("CreateFileW", guard.canonical_symbols())


if __name__ == "__main__":
    unittest.main()
