import io
import json
from pathlib import Path
import sys
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from asset_archive_conformance import (
    FAMILIES_NOT_YET_REQUIRED, TYPED_DESCRIPTORS, validate_archive_bytes,
)


def ai_row(list_id, index, opcode_name="end_list"):
    return {
        "ailist_ref": f"global_{list_id:04x}",
        "list_id": f"0x{list_id:04x}",
        "graph_node": f"scenario.ai.global_{list_id:04x}.command.{index:04d}",
        "command_index": index, "offset": index * 2, "opcode": "0x0004",
        "opcode_name": opcode_name, "operands": [],
        "model_catalog_id": "", "weapon_catalog_id": "",
        "body_catalog_id": "", "head_catalog_id": "",
    }


GOOD_INI = ("[ai_lists]\ncatalog_id = base:ai_global\n"
            "ai_lists_file = ai/global_ailists.json\n")


class GlobalAiListsConformanceTests(unittest.TestCase):
    """context/designs/modding/global-ai-lists.md: the .pdai family."""

    def bank(self, ini=GOOD_INI, rows=None):
        data = io.BytesIO()
        with zipfile.ZipFile(data, "w") as archive:
            if ini is not None:
                archive.writestr("ai.ini", ini)
            archive.writestr("ai/global_ailists.json", json.dumps({
                "schema": "pd2.scenario.ai.lists.v1",
                "rows": rows if rows is not None else [ai_row(0x0000, 0),
                                                       ai_row(0x002d, 0)],
            }))
            archive.writestr("_meta/manifest.json", json.dumps({
                "pd_kind": "ai_lists", "pd_schema_version": 1,
                "id": "base:ai_global"}))
        return validate_archive_bytes(data.getvalue(), "fixture.pdai", ".pdai").errors

    def test_family_is_known_but_not_yet_required_of_every_root(self):
        self.assertEqual(TYPED_DESCRIPTORS[".pdai"], "ai.ini")
        self.assertIn(".pdai", FAMILIES_NOT_YET_REQUIRED)

    def test_global_bank_passes(self):
        self.assertEqual(self.bank(), [])

    def test_rows_hold_only_global_list_ids(self):
        for list_id in (0x0401, 0x1000):
            with self.subTest(list_id=list_id):
                self.assertTrue(self.bank(rows=[ai_row(list_id, 0)]))

    def test_descriptor_names_the_bank_and_its_rows(self):
        for ini in (None,
                    GOOD_INI.replace("[ai_lists]", "[lang]"),
                    GOOD_INI.replace("base:ai_global", "ai_global"),
                    GOOD_INI.replace("ai/global_ailists.json", "rows.json")):
            with self.subTest(ini=ini):
                self.assertTrue(self.bank(ini=ini))

    def test_rows_keep_named_commands(self):
        self.assertTrue(self.bank(rows=[ai_row(0, 0, opcode_name="command")]))


if __name__ == "__main__":
    unittest.main()
