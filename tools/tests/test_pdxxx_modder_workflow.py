"""Manifest regression tests with no real output, packaging or cleanup for rejection."""
from contextlib import ExitStack, redirect_stderr, redirect_stdout
import importlib.util
import io
import json
from pathlib import Path
import sys
import unittest
from unittest import mock
import zipfile


def load_verifier(path):
    spec = importlib.util.spec_from_file_location("pdxxx_workflow_under_test", path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


VERIFIER = load_verifier(Path(__file__).resolve().parents[1] / "verify_pdxxx_modder_workflow.py")
VALID = {"id": "author.sample", "name": "Custom weapon", "version": "1.0"}


class MemoryManifest:
    def __init__(self, payload=b"", *, exists=True, read_error=None):
        self.payload = payload
        self.present = exists
        self.read_error = read_error
        self.reads = 0

    def exists(self):
        return self.present

    def read_text(self, *, encoding):
        if encoding != "utf-8":
            raise AssertionError("Manifest decoding must use UTF-8")
        self.reads += 1
        if self.read_error is not None:
            raise self.read_error
        return self.payload.decode(encoding)

    def __str__(self):
        return "<memory-source>/mod.json"


class MemorySource:
    def __init__(self, manifest):
        self.manifest = manifest

    def resolve(self):
        return self

    def __truediv__(self, name):
        if name != "mod.json":
            raise AssertionError("Unexpected source file read")
        return self.manifest

    def __str__(self):
        return "<memory-source>"


class ForbiddenOutput:
    def __bool__(self):
        raise AssertionError("Output must not be inspected before manifest acceptance")

    def mkdir(self, **kwargs):
        raise AssertionError("Output creation is prohibited")

    def resolve(self):
        raise AssertionError("Output inspection is prohibited")


def forbid(*args, **kwargs):
    raise AssertionError("A real scan/output/pack/extract/cleanup operation must never run")


def block_mutations(*, allow_memory_package=False):
    stack = ExitStack()
    names = ("typed_archives", "validate_archive", "verify_roundtrip", "extract_pdmod")
    if not allow_memory_package:
        names += ("package_pdmod",)
    for name in names:
        stack.enter_context(mock.patch.object(VERIFIER, name, side_effect=forbid))
    for name in ("mkdtemp", "NamedTemporaryFile"):
        stack.enter_context(mock.patch.object(VERIFIER.tempfile, name, side_effect=forbid))
    stack.enter_context(mock.patch.object(VERIFIER.shutil, "rmtree", side_effect=forbid))
    for name in ("mkdir", "unlink", "open", "read_bytes", "write_bytes", "write_text"):
        stack.enter_context(mock.patch.object(Path, name, side_effect=forbid))
    stack.enter_context(mock.patch("builtins.open", side_effect=forbid))
    if not allow_memory_package:
        stack.enter_context(mock.patch.object(VERIFIER.zipfile, "ZipFile", side_effect=forbid))
    return stack


class ManifestTests(unittest.TestCase):
    def reject(self, manifest, diagnostic, *, cli=False):
        source = MemorySource(manifest)
        output = ForbiddenOutput()
        stdout, stderr = io.StringIO(), io.StringIO()
        with block_mutations(), redirect_stdout(stdout), redirect_stderr(stderr):
            if cli:
                def path_argument(value):
                    if value == "source-input":
                        return source
                    if value == "untouched-output":
                        return output
                    raise AssertionError("Unexpected CLI path")
                with mock.patch.object(VERIFIER, "Path", side_effect=path_argument):
                    code = VERIFIER.main(["--source-root", "source-input",
                                          "--keep-output", "untouched-output"])
            else:
                code = VERIFIER.validate_workflow(source, output)
        self.assertEqual(1, code)
        self.assertEqual("", stdout.getvalue())
        self.assertIn("ERROR: ", stderr.getvalue())
        self.assertIn("mod.json", stderr.getvalue())
        self.assertIn(diagnostic, stderr.getvalue())
        self.assertNotIn("Traceback", stderr.getvalue())

    def test_non_object_manifests_refuse_before_any_output(self):
        for value in (None, False, True, 0, 42, 1.25, "mod", [], [VALID]):
            with self.subTest(value=value):
                self.reject(MemoryManifest(json.dumps(value).encode()), "JSON object")

    def test_required_fields_are_nonempty_strings_without_coercion(self):
        for field in VALID:
            for value in (None, False, True, 0, 1.25, [], {}, "", " ", "\t\n"):
                manifest = dict(VALID, **{field: value})
                with self.subTest(field=field, value=value):
                    self.reject(MemoryManifest(json.dumps(manifest).encode()),
                                "non-empty string " + field)

    def test_missing_required_fields_refuse_without_output(self):
        for field in VALID:
            manifest = dict(VALID)
            del manifest[field]
            with self.subTest(field=field):
                self.reject(MemoryManifest(json.dumps(manifest).encode()),
                            "non-empty string " + field)

    def test_malformed_json_is_a_diagnostic_and_no_output(self):
        for payload in (b"{", b'"unterminated', b'{"id":}'):
            with self.subTest(payload=payload):
                self.reject(MemoryManifest(payload), "unreadable JSON")

    def test_missing_manifest_never_scans_or_creates_output(self):
        manifest = MemoryManifest(exists=False)
        self.reject(manifest, "missing mod.json")
        self.assertEqual(0, manifest.reads)

    def test_unreadable_utf8_and_io_errors_are_diagnostics(self):
        self.reject(MemoryManifest(b"\xff"), "unreadable JSON")
        self.reject(MemoryManifest(read_error=PermissionError("read denied")),
                    "unreadable JSON")

    def test_cli_invalid_manifest_returns_one_before_packaging(self):
        for payload in (b"[]", b"null", b"{", b"\xff",
                        b'{"id":null,"name":false,"version":42}'):
            with self.subTest(payload=payload):
                self.reject(MemoryManifest(payload), "mod.json", cli=True)

    def test_valid_unicode_and_padded_text_are_accepted_unchanged(self):
        for metadata in (VALID, {"id": " author.sample ", "name": "\u6b66\u5668 \U0001f31f",
                                 "version": " 1.0 "}):
            payload = json.dumps(metadata, ensure_ascii=False).encode("utf-8")
            manifest = MemoryManifest(payload)
            errors = []
            with block_mutations():
                VERIFIER.validate_manifest(MemorySource(manifest), errors)
            self.assertEqual([], errors)
            self.assertEqual(payload, manifest.payload)
            self.assertEqual(1, manifest.reads)

    def test_valid_manifest_continues_to_existing_archive_path(self):
        class ScanningReached(Exception):
            pass
        stdout, stderr = io.StringIO(), io.StringIO()
        source = MemorySource(MemoryManifest(json.dumps(VALID).encode()))
        with block_mutations(), redirect_stdout(stdout), redirect_stderr(stderr), \
                mock.patch.object(VERIFIER, "typed_archives", side_effect=ScanningReached) as scan:
            with self.assertRaises(ScanningReached):
                VERIFIER.validate_workflow(source, ForbiddenOutput())
            scan.assert_called_once_with(source)
        self.assertEqual("", stderr.getvalue())

    def test_existing_packer_preserves_public_typed_archive_bytes(self):
        typed = io.BytesIO()
        with zipfile.ZipFile(typed, "w") as archive:
            archive.writestr("weapon.ini", b"[weapon]\nname=Edited public source\n")
            archive.writestr("weapon.graph.json", b'{"edited":"source","amount":7}\n')
        payloads = {"mod.json": json.dumps(VALID).encode(),
                    "weapons/custom.pdweapon": typed.getvalue()}

        class MemoryFile:
            def __init__(self, name, data):
                self.name, self.data = name, data
            def is_file(self):
                return True
            def __lt__(self, other):
                return self.name < other.name
            def relative_to(self, tree):
                return Path(self.name)
            def read_bytes(self):
                return self.data

        class MemoryTree:
            def rglob(self, pattern):
                if pattern != "*":
                    raise AssertionError("Unexpected input enumeration")
                return [MemoryFile(name, data) for name, data in payloads.items()]

        class MemoryZip(io.BytesIO):
            def exists(self):
                return False
            def unlink(self):
                raise AssertionError("Memory package must not delete an output")

        package = MemoryZip()
        with block_mutations(allow_memory_package=True):
            VERIFIER.package_pdmod(MemoryTree(), package)
            package.seek(0)
            with zipfile.ZipFile(package, "r") as archive:
                self.assertEqual(set(payloads), set(archive.namelist()))
                for name, payload in payloads.items():
                    self.assertEqual(payload, archive.read(name))
                    self.assertEqual(VERIFIER.ZIP_TIME, archive.getinfo(name).date_time)


if __name__ == "__main__":
    unittest.main()
