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


REAL_ZIP = zipfile.ZipFile
REAL_VALIDATE_ARCHIVE = VERIFIER.validate_archive
REAL_TYPED_ARCHIVES = VERIFIER.typed_archives


class ArchiveFile:
    def __init__(self, name, data=b"", error=None):
        self.key, self.data, self.error = name, data, error
        self.name = Path(name).name
        self.suffix = Path(name).suffix

    def is_file(self):
        return True

    def __lt__(self, other):
        return self.key < other.key

    def relative_to(self, root):
        return Path(self.key)

    def read_bytes(self):
        if self.error:
            raise self.error
        return self.data

    def __str__(self):
        return self.key


class ArchiveTree(MemorySource):
    name = "memory-source"

    def __init__(self, archives):
        metadata = json.dumps(VALID).encode()
        super().__init__(MemoryManifest(metadata))
        self.files = [ArchiveFile("mod.json", metadata)] + archives

    def rglob(self, pattern):
        if pattern != "*":
            raise AssertionError("Unexpected scan pattern")
        return list(self.files)


def memory_archive(entries):
    stream = io.BytesIO()
    with REAL_ZIP(stream, "w") as archive:
        for name, data in entries.items():
            archive.writestr(name, data)
    return stream.getvalue()


def source_entries(ext):
    return {VERIFIER.TYPED_DESCRIPTORS[ext]: b"[source]\nid=test:edited\n",
            "_meta/manifest.json": b'{"id":"test:edited"}',
            "public.obj": b"v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"}


def all_families(*, change=None, omit=None):
    archives = []
    for ext in sorted(VERIFIER.TYPED_DESCRIPTORS):
        if ext == omit:
            continue
        entries = source_entries(ext)
        if change:
            change(ext, entries)
        archives.append(ArchiveFile("authored/edited" + ext, memory_archive(entries)))
    return archives


def memory_zip(source, mode="r", *args, **kwargs):
    if isinstance(source, ArchiveFile):
        if mode != "r":
            raise AssertionError("Authored sources are read-only")
        if source.error:
            raise source.error
        source = io.BytesIO(source.data)
    elif not isinstance(source, io.BytesIO):
        raise AssertionError("Real archive filesystem access prohibited")
    return REAL_ZIP(source, mode, *args, **kwargs)


def archive_preflight_guard(*, allow_memory_package=False):
    stack = block_mutations(allow_memory_package=allow_memory_package)
    stack.enter_context(mock.patch.object(VERIFIER, "typed_archives", wraps=REAL_TYPED_ARCHIVES))
    stack.enter_context(mock.patch.object(VERIFIER, "validate_archive", wraps=REAL_VALIDATE_ARCHIVE))
    stack.enter_context(mock.patch.object(VERIFIER.zipfile, "ZipFile", side_effect=memory_zip))
    return stack


class ArchivePreflightTests(unittest.TestCase):
    def reject(self, archives, diagnostic, *, retained=True):
        source = ArchiveTree(archives)
        stdout, stderr = io.StringIO(), io.StringIO()
        with archive_preflight_guard(), redirect_stdout(stdout), redirect_stderr(stderr):
            code = VERIFIER.validate_workflow(source, ForbiddenOutput() if retained else None)
        self.assertEqual(1, code)
        self.assertEqual("", stdout.getvalue())
        self.assertIn("ERROR: ", stderr.getvalue())
        self.assertIn(diagnostic, stderr.getvalue())
        return stderr.getvalue()

    def test_no_archives_refuse_for_retained_and_temporary_output(self):
        for retained in (True, False):
            with self.subTest(retained=retained):
                self.reject([], "no typed .pdxxx archives found", retained=retained)

    def test_missing_or_unreadable_selected_archive_is_a_diagnostic(self):
        for error in (FileNotFoundError("selected source vanished"), PermissionError("source read denied")):
            for retained in (True, False):
                with self.subTest(error=type(error).__name__, retained=retained):
                    self.reject([ArchiveFile("missing.pdweapon", error=error)], "unreadable typed archive", retained=retained)

    def test_corrupt_archive_refuses_before_output(self):
        for payload in (b"", b"not a ZIP", b"PK\x03\x04truncated"):
            for retained in (True, False):
                with self.subTest(payload=payload, retained=retained):
                    self.reject([ArchiveFile("broken.pdweapon", payload)], "not a zip-openable", retained=retained)

    def test_missing_descriptor_and_meta_manifest_refuse(self):
        for member, diagnostic in (("weapon.ini", "missing descriptor weapon.ini"),
                                   ("_meta/manifest.json", "missing _meta/manifest.json")):
            with self.subTest(member=member):
                def change(ext, entries):
                    if ext == ".pdweapon":
                        del entries[member]
                self.reject(all_families(change=change), diagnostic)

    def test_missing_family_refuses_after_real_archive_scan(self):
        for ext in (".pdweapon", ".pdmesh"):
            with self.subTest(ext=ext):
                diagnostic = self.reject(all_families(omit=ext), "missing typed family examples")
                self.assertIn(ext, diagnostic)

    def test_forbidden_public_bin_and_tsv_refuse(self):
        for suffix in (".bin", ".tsv"):
            with self.subTest(suffix=suffix):
                def change(ext, entries):
                    if ext == ".pdweapon":
                        entries["public/runtime" + suffix] = b"invalid opaque public payload"
                self.reject(all_families(change=change), "forbidden public source entry public/runtime" + suffix)

    def test_absent_editable_public_source_and_media_refuse(self):
        def empty_public(ext, entries):
            if ext == ".pdweapon":
                entries.clear()
                entries["_meta/manifest.json"] = b"{}"
        self.reject(all_families(change=empty_public), "no editable public source entries")
        def remove_media(ext, entries):
            del entries["public.obj"]
        self.reject(all_families(change=remove_media), "no standard DCC/media source entries found")

    def test_nested_corrupt_or_missing_descriptor_refuses_without_temp_cleanup(self):
        for payload, diagnostic in ((b"bad nested ZIP", "not a zip-openable"),
                                    (memory_archive({"_meta/manifest.json": b"{}", "public.obj": b"v 0 0 0\n"}), "missing descriptor mesh.ini")):
            with self.subTest(diagnostic=diagnostic):
                def change(ext, entries):
                    if ext == ".pdweapon":
                        entries["nested/child.pdmesh"] = payload
                errors = self.reject(all_families(change=change), diagnostic)
                self.assertIn("child.pdmesh", errors)

    def test_nested_member_crc_corruption_refuses_without_temp_cleanup(self):
        child = memory_archive(source_entries(".pdmesh"))
        entries = source_entries(".pdweapon")
        entries["nested/child.pdmesh"] = child
        damaged = bytearray(memory_archive(entries))
        with REAL_ZIP(io.BytesIO(damaged)) as archive:
            info = archive.getinfo("nested/child.pdmesh")
            offset = info.header_offset + 30 + len(info.filename.encode()) + len(info.extra)
        damaged[offset] ^= 1
        self.reject([ArchiveFile("crc.pdweapon", bytes(damaged))], "not a zip-openable")

    def test_archive_diagnostics_preserve_existing_eighty_error_limit(self):
        def change(ext, entries):
            if ext == ".pdweapon":
                for index in range(85):
                    entries[f"public/{index}.bin"] = b"x"
        stderr = self.reject(all_families(change=change), "5 more error(s)")
        self.assertEqual(81, len(stderr.splitlines()))

    def test_valid_all_family_nested_source_reaches_existing_package_boundary(self):
        child = memory_archive(source_entries(".pdmesh"))
        def change(ext, entries):
            if ext == ".pdweapon":
                entries["nested/child.pdmesh"] = child
                entries["_meta/private.bin"] = b"private unchanged metadata"
        source = ArchiveTree(all_families(change=change))
        package = io.BytesIO()
        package.exists = lambda: False
        package.unlink = forbid
        class Output:
            mkdir_calls = 0
            def mkdir(self, **kwargs):
                self.mkdir_calls += 1
            def resolve(self):
                return self
        output = Output()
        class BoundaryReached(Exception):
            pass
        def package_boundary(selected_source, selected_output, errors):
            self.assertIs(source, selected_source)
            self.assertIs(output, selected_output)
            self.assertEqual([], errors)
            VERIFIER.package_pdmod(selected_source, package)
            raise BoundaryReached
        stdout, stderr = io.StringIO(), io.StringIO()
        with archive_preflight_guard(allow_memory_package=True), redirect_stdout(stdout), redirect_stderr(stderr), \
                mock.patch.object(VERIFIER, "verify_roundtrip", side_effect=package_boundary):
            with self.assertRaises(BoundaryReached):
                VERIFIER.validate_workflow(source, output)
            stats, errors = VERIFIER.WorkflowStats(), []
            for archive in source.files[1:]:
                VERIFIER.validate_archive(archive, stats, errors)
            self.assertEqual([], errors)
            self.assertEqual(len(VERIFIER.TYPED_DESCRIPTORS) + 1, stats.archives)
            self.assertEqual(1, stats.nested_archives)
            self.assertEqual(set(VERIFIER.TYPED_DESCRIPTORS), stats.families)
        self.assertEqual("", stderr.getvalue())
        self.assertEqual(1, output.mkdir_calls)
        package.seek(0)
        with REAL_ZIP(package) as packed:
            self.assertEqual({file.key for file in source.files}, set(packed.namelist()))
            for file in source.files:
                self.assertEqual(file.data, packed.read(file.key))
                self.assertEqual(VERIFIER.ZIP_TIME, packed.getinfo(file.key).date_time)


if __name__ == "__main__":
    unittest.main()
