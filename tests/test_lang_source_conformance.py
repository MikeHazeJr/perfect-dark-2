import io
import json
from pathlib import Path
import sys
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from asset_archive_conformance import (
    validate_lang_strings_json, validate_lang_source_contract,
)


class LanguageSourceConformanceTests(unittest.TestCase):
    def source(self, rows):
        return json.dumps({"pd_kind": "language_strings", "pd_schema_version": 1,
                           "strings": rows}, ensure_ascii=False)

    def check_source(self, text, accepted):
        errors = []
        validate_lang_strings_json("fixture", text, errors)
        self.assertEqual(not errors, accepted, errors)

    def test_empty_null_and_empty_string_are_distinct_valid_sources(self):
        self.check_source(self.source([]), True)
        self.check_source(self.source([{"index": 1, "text": ""},
                                       {"index": 0, "text": None}]), True)

    def test_literal_and_escaped_unicode_agree(self):
        source = self.source([{"index": 0, "text": "caf\u00e9"}])
        self.check_source(source, True)
        self.check_source(source.replace("\u00e9", "\\u00e9"), True)
        self.check_source(self.source([{"index": 0, "text": "\u20ac"}]), False)
        self.check_source(self.source([{"index": 0, "text": "a\x00b"}]), False)

    def test_index_table_and_lexical_rejections(self):
        invalid = [
            [{"index": 0, "text": "a"}, {"index": 0, "text": "b"}],
            [{"index": 1, "text": "gap"}], [{"index": -1, "text": "bad"}],
            [{"index": 0.5, "text": "bad"}], [{"index": True, "text": "bad"}],
            [{"index": 0}], [{"index": 0, "text": False}],
        ]
        for rows in invalid:
            with self.subTest(rows=rows):
                self.check_source(self.source(rows), False)
        self.check_source(self.source([]) + " trailing", False)
        self.check_source('{"pd_kind":"language_strings","pd_schema_version":1,'
                          '"strings":[],"str\\u0069ngs":[]}', False)

    def test_declared_zero_must_match_empty_table(self):
        for count, rows, accepted in [(0, [], True),
                                     (0, [{"index": 0, "text": None}], False),
                                     (1, [], False)]:
            with self.subTest(count=count, rows=rows):
                data = io.BytesIO()
                with zipfile.ZipFile(data, "w") as archive:
                    archive.writestr("lang.ini", "locale=en\ncategory=system\n"
                                     f"source_bank=1\nstrings_file=strings.json\nstring_count={count}\n")
                    archive.writestr("_meta/manifest.json", json.dumps({
                        "locale": "en", "category": "system", "source_bank": 1,
                        "data": "strings.json", "string_count": count}))
                    archive.writestr("strings.json", self.source(rows))
                with zipfile.ZipFile(io.BytesIO(data.getvalue())) as archive:
                    errors = []
                    validate_lang_source_contract("fixture", archive, set(archive.namelist()), errors)
                self.assertEqual(not errors, accepted, errors)


if __name__ == "__main__":
    unittest.main()
