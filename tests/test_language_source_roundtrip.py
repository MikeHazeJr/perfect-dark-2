"""Focused dynamic tests of the production language parity verifier."""

import contextlib
import hashlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zipfile
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import check_language_source_roundtrip as verifier


def rzip(data, padded=True):
    stream = zlib.compressobj(wbits=-15)
    packed = b"\x11\x73" + len(data).to_bytes(3, "big") + stream.compress(data) + stream.flush()
    return packed + bytes((-len(packed)) % 16) if padded else packed


def native_fixture():
    # Four table slots: null, real empty string, Latin-1 cafe, trailing null.
    return struct.pack(">4I", 0, 16, 20, 0) + bytes(4) + b"caf\xe9\0" + bytes(7)


def public_fixture():
    return [{"index": 3, "text": None}, {"index": 1, "text": ""},
            {"index": 0, "text": None}, {"index": 2, "text": "caf\u00e9"}]


class LanguageRoundtripTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def archive(self, *, rows=None, native=None, raw=None, locale="en", rom="ntsc-final",
                suffix="E", source_path=None, overrides=None, ensure_ascii=False,
                filename="base_lang_gun_en.pdlang"):
        native = native_fixture() if native is None else native
        raw = rzip(native) if raw is None else raw
        rows = public_fixture() if rows is None else rows
        raw_dir = self.root / "data" / rom / "files"
        raw_dir.mkdir(parents=True, exist_ok=True)
        raw_path = raw_dir / f"Lgun{suffix}.bin"
        raw_path.write_bytes(raw)
        raw_path.with_name(raw_path.name + ".sha256").write_text(hashlib.sha256(raw).hexdigest() + "\n", encoding="ascii")
        catalog_id = "base:lang_gun_" + locale
        strings = json.dumps({"pd_kind": "language_strings", "pd_schema_version": 1,
                              "strings": rows}, ensure_ascii=ensure_ascii).encode("utf-8")
        manifest = {"pd_kind": "lang", "pd_schema_version": 1, "id": catalog_id,
                    "locale": locale, "data": "strings.json", "data_size": len(strings),
                    "source_data_size": len(raw), "decoded_source_size": len(native),
                    "string_count": len(rows), "source_bank": 38}
        provenance = {"schema": "pd2.asset.provenance.v1", "family": "lang", "id": catalog_id,
                      "source_path": source_path or raw_path.relative_to(self.root).as_posix()}
        ini = (f"[lang]\ncatalog_id = {catalog_id}\nlocale = {locale}\nstrings_file = strings.json\n"
               f"source_bank = 38\nstring_count = {len(rows)}\n").encode("utf-8")
        entries = {"lang.ini": ini, "strings.json": strings,
                   "_meta/manifest.json": json.dumps(manifest).encode(),
                   "_meta/provenance.json": json.dumps(provenance).encode()}
        if overrides:
            entries.update(overrides)
        hashes = []
        for name in ("lang.ini", "strings.json"):
            digest = hashlib.sha256(entries[name]).hexdigest()
            entries[f"_meta/{name}.sha256"] = (digest + "\n").encode()
            hashes.append({"path": name, "size": len(entries[name]), "sha256": digest})
        entries["_meta/hashes.json"] = json.dumps({"schema": "pd2.asset.hashes.v1", "entries": hashes}).encode()
        archive = self.root / "data" / rom / "lang" / filename
        archive.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as zf:
            for name, data in entries.items():
                zf.writestr(name, data)
        return archive, raw_path

    def check(self, rom="ntsc-final", locales=()):
        return verifier.verify_root(self.root, rom, locales)

    def test_actual_archive_provenance_hashes_and_complete_table_roundtrip(self):
        self.archive()
        receipt = self.check(locales=("en",))
        self.assertTrue(receipt["passed"], receipt)
        row = receipt["archives"][0]
        self.assertEqual(row["rows_compared"], 4)
        self.assertEqual(row["native_null_indexes"], [0, 3])
        self.assertEqual(row["native_empty_indexes"], [1])
        self.assertEqual(row["native_semantic_sha256"], row["public_semantic_sha256"])
        self.assertEqual(row["codec"], "latin-1")
        self.assertEqual(row["source_path"], "data/ntsc-final/files/LgunE.bin")
        self.assertFalse(receipt["translation_completeness_proven"])

    def test_literal_and_escaped_utf8_have_identical_semantics(self):
        self.archive(ensure_ascii=False)
        literal = self.check()["archives"][0]
        self.archive(ensure_ascii=True)
        escaped = self.check()["archives"][0]
        self.assertEqual(literal["status"], "passed")
        self.assertEqual(escaped["status"], "passed")
        self.assertNotEqual(literal["strings_sha256"], escaped["strings_sha256"])
        self.assertEqual(literal["public_semantic_sha256"], escaped["public_semantic_sha256"])

    def test_native_uncompressed_and_canonical_empty_banks(self):
        self.archive(raw=native_fixture())
        self.assertTrue(self.check()["passed"])
        self.archive(native=bytes(16), rows=[])
        receipt = self.check()
        self.assertTrue(receipt["passed"], receipt)
        self.assertEqual(receipt["archives"][0]["native_count"], 0)

    def test_null_empty_trailing_extent_and_text_changes_are_all_detected(self):
        changed = [
            [{"index": 0, "text": ""}, {"index": 1, "text": ""}, {"index": 2, "text": "caf\u00e9"}, {"index": 3, "text": None}],
            [{"index": 0, "text": None}, {"index": 1, "text": None}, {"index": 2, "text": "caf\u00e9"}, {"index": 3, "text": None}],
            [{"index": 0, "text": None}, {"index": 1, "text": ""}, {"index": 2, "text": "caf\u00e9"}],
            [{"index": 0, "text": None}, {"index": 1, "text": ""}, {"index": 2, "text": "changed"}, {"index": 3, "text": None}],
        ]
        for rows in changed:
            with self.subTest(rows=rows):
                self.archive(rows=rows)
                receipt = self.check()
                self.assertFalse(receipt["passed"])
                self.assertTrue(receipt["archives"][0]["mismatches"])

    def test_rzip_complete_stream_exact_length_and_alignment(self):
        native = native_fixture()
        packed = rzip(native, padded=False)
        self.assertEqual(verifier.decode_native_source(packed)[0], native)
        self.assertEqual(verifier.decode_native_source(rzip(native))[0], native)
        invalid = [b"\x11\x73", b"\x11\x73\0\0\0x", b"\x11\x72xx",
                   packed[:-1], packed[:2] + (len(native) - 1).to_bytes(3, "big") + packed[5:],
                   packed[:2] + (len(native) + 1).to_bytes(3, "big") + packed[5:],
                   packed + b"bad", rzip(native) + bytes(16), packed + packed]
        for data in invalid:
            with self.subTest(data=data):
                with self.assertRaises(verifier.ParityError):
                    verifier.decode_native_source(data)

    def test_native_malformed_offsets_unterminated_and_ambiguous_tables(self):
        invalid = [bytes(8), struct.pack(">4I", 0, 0, 0, 4) + bytes(8),
                   struct.pack(">2I", 6, 0) + bytes(8),
                   struct.pack(">2I", 8, 16) + bytes(8),
                   struct.pack(">2I", 8, 4) + bytes(8),
                   struct.pack(">2I", 8, 0) + b"unterminated"]
        for data in invalid:
            with self.subTest(data=data):
                with self.assertRaises(verifier.ParityError):
                    verifier.native_table(data)

    def test_strict_json_rejects_duplicate_keys_bad_indexes_utf8_and_scalars(self):
        invalid = [
            b'{"pd_kind":"language_strings","strings":[],"str\\u0069ngs":[]}',
            b'{"pd_kind":"language_strings","strings":[{"index":0,"text":"a"},{"index":0,"text":"b"}]}',
            b'{"pd_kind":"language_strings","strings":[{"index":1,"text":"gap"}]}',
            b'{"pd_kind":"language_strings","strings":[{"index":0.5,"text":"fraction"}]}',
            b'{"pd_kind":"language_strings","strings":[{"index":true,"text":"boolean"}]}',
            b'{"pd_kind":"language_strings","strings":[{"index":0,"text":"\\ud800"}]}',
            b'{"pd_kind":"language_strings","strings":[{"index":0,"text":"\\u3042"}]}',
            b'{"pd_kind":"language_strings","strings":[{"index":0,"text":"\xff"}]}',
            b'{"pd_kind":"language_strings","strings":[],"extra":NaN}',
            b'{"pd_kind":"language_strings","strings":[]} trailing',
        ]
        for data in invalid:
            with self.subTest(data=data):
                with self.assertRaises((ValueError, UnicodeError)):
                    verifier.source_table(data)

    def test_source_and_public_hash_mismatch_fail(self):
        _, source = self.archive()
        source.with_name(source.name + ".sha256").write_text("0" * 64 + "\n")
        self.assertIn("SHA-256 mismatch", self.check()["archives"][0]["error"])
        archive, _ = self.archive()
        with zipfile.ZipFile(archive) as zf:
            entries = {name: zf.read(name) for name in zf.namelist()}
        entries["_meta/strings.json.sha256"] = b"0" * 64
        with zipfile.ZipFile(archive, "w") as zf:
            for name, data in entries.items():
                zf.writestr(name, data)
        self.assertIn("SHA-256 mismatch", self.check()["archives"][0]["error"])

    def test_provenance_paths_never_escape_or_guess_numeric_identity(self):
        invalid = ["../outside.bin", "/data/ntsc-final/files/LgunE.bin", "C:/outside.bin",
                   "data/ntsc-final/files/../LgunE.bin", "data/other/files/LgunE.bin",
                   "data/ntsc-final/files/G_0026.bin", "data/ntsc-final/files/LgunE.bin:stream",
                   "data\\ntsc-final\\files\\LgunE.bin", "data/ntsc-final/files/%2e%2e.bin"]
        for source_path in invalid:
            with self.subTest(source_path=source_path):
                self.archive(source_path=source_path)
                self.assertFalse(self.check()["passed"])

    def test_counts_and_manifest_duplicate_keys_fail_before_parity(self):
        for ini in (b"[lang]\nsource_bank=38\nstring_count=4junk\n", b"[lang]\nsource_bank=38\nstring_count=513\n"):
            with self.assertRaises(verifier.ParityError):
                verifier.descriptor(ini)
        self.archive(overrides={"_meta/manifest.json": b'{"locale":"en","locale":"fr"}'})
        self.assertIn("duplicate", self.check()["archives"][0]["error"])

    def test_all_published_locales_and_explicit_selection_have_distinct_receipts(self):
        self.archive()
        self.archive(locale="fr", suffix="_str_fZ", filename="base_lang_gun_fr.pdlang")
        all_locales = self.check()
        self.assertTrue(all_locales["passed"], all_locales)
        self.assertEqual(all_locales["summary"]["passed"], 2)
        english = self.check(locales=("en",))
        self.assertTrue(english["passed"])
        self.assertEqual(english["summary"]["passed"], 1)
        self.assertEqual(english["summary"]["not_selected"], 1)
        self.assertEqual(english["summary"]["extracted_raw_candidates"], 2)
        self.assertFalse(self.check(locales=("de",))["passed"])

    def test_all_seven_active_name_variants_have_independent_semantic_receipts(self):
        for locale, suffix, slug in (
                ("en", "E", "en"), ("ja", "J", "ja"),
                ("en-GB", "P", "en_gb"), ("fr", "_str_f", "fr"),
                ("de", "_str_g", "de"), ("it", "_str_i", "it"),
                ("es", "_str_s", "es")):
            self.archive(locale=locale, suffix=suffix,
                         filename=f"base_lang_gun_{slug}.pdlang")
        receipt = self.check()
        self.assertTrue(receipt["passed"], receipt)
        self.assertEqual(receipt["summary"]["passed"], 7)
        self.assertEqual(receipt["summary"]["extracted_raw_candidates"], 7)
        self.assertEqual(set(receipt["summary"]["published_archives_by_locale"]),
                         {"en", "ja", "en-GB", "fr", "de", "it", "es"})
        self.assertTrue(all(row["native_semantic_sha256"] == row["public_semantic_sha256"]
                            for row in receipt["archives"]))

    def test_japanese_codec_is_explicitly_unsupported_only_in_japanese_rom(self):
        self.archive(locale="ja", suffix="J")
        self.assertTrue(self.check()["passed"])
        self.archive(locale="ja", suffix="J", rom="jpn-final")
        row = self.check(rom="jpn-final")["archives"][0]
        self.assertEqual(row["status"], "failed")
        self.assertEqual(row["codec"], "jpn-final-packed-unsupported")

    def test_duplicate_published_bank_locale_is_not_extra_coverage(self):
        self.archive()
        self.archive(filename="duplicate.pdlang")
        receipt = self.check()
        self.assertFalse(receipt["passed"])
        self.assertEqual(receipt["summary"]["failed"], 2)

    def test_archive_member_escape_and_duplicates_fail_closed(self):
        self.archive(overrides={"../outside.json": b"{}"})
        self.assertIn("relative source/member path", self.check()["archives"][0]["error"])
        self.archive(overrides={"STRINGS.JSON": b"{}"})
        self.assertIn("case-ambiguous", self.check()["archives"][0]["error"])

    def test_selected_inventory_gap_is_reported_without_translation_claim(self):
        _, source = self.archive()
        unreferenced = source.with_name("LoptionsE.bin")
        unreferenced.write_bytes(source.read_bytes())
        receipt = self.check(locales=("en",))
        self.assertTrue(receipt["passed"])
        self.assertEqual(receipt["summary"]["selected_locale_unreferenced_raw_candidates"], 1)
        self.assertEqual(receipt["selected_locale_unreferenced_raw_sources"], ["data/ntsc-final/files/LoptionsE.bin"])
        self.assertFalse(receipt["translation_completeness_proven"])
        self.assertFalse(verifier.verify_root(self.root, "ntsc-final", ("en",), 68)["passed"])

    def test_cli_writes_full_machine_receipt_and_returns_verdict(self):
        self.archive()
        receipt_path = self.root / "receipts" / "language.json"
        stdout = io.StringIO()
        with contextlib.redirect_stdout(stdout):
            code = verifier.main(["--root", str(self.root), "--rom-id", "ntsc-final", "--locale", "en",
                                  "--output", str(receipt_path)])
        self.assertEqual(code, 0)
        self.assertEqual(json.loads(stdout.getvalue()), json.loads(receipt_path.read_text(encoding="utf-8")))
        self.assertEqual(len(json.loads(stdout.getvalue())["archives"]), 1)


if __name__ == "__main__":
    unittest.main()
