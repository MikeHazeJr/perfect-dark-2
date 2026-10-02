"""Tiny synthetic tests for ledger drift/path checks, independent of game builds."""
import copy
import importlib.util
import tempfile
import unittest
from pathlib import Path

SPEC = importlib.util.spec_from_file_location(
    "graph_inventory", Path(__file__).resolve().parents[1] / "graph-operation-inventory.py")
INVENTORY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(INVENTORY)


class InventoryTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        (self.root / "sample.c").write_text('''
// void missing(void) { g_Fake++; }
void action(int x)
{
    const char *fake = "} g_Fake fakeCall()";
    /* } wrong(); */
    hand->stateframes += x;
    g_Vars.lvupdate240++;
    realCall();
}
void caller(void) { action(1); }
''', encoding="utf-8")
        operation = {key: ["reviewed"] for key in INVENTORY.REQUIRED}
        operation.update(id="held.test", cohort="base_firing",
            parameters=[dict(name="delay", type="int", unit="ticks60", semantic_status="observed")],
            migration={"status": "native_only"},
            evidence=[dict(path="sample.c", symbol="action", requires=["stateframes"])])
        self.ledger = {"schema": "pd.graph_operation_inventory.v1", "operations": [operation]}

    def test_comments_strings_do_not_create_definitions_or_calls(self):
        errors, report = INVENTORY.inspect(self.ledger, self.root)
        self.assertEqual([], errors)
        observation = report["definitions"][0]
        self.assertEqual(["g_Vars"], observation["globals"])
        self.assertEqual(["realCall"], observation["calls"])
        self.assertEqual(["stateframes"], observation["state_fields"])
        self.assertEqual(["caller"], [x["symbol"] for x in observation["callers_in_reviewed_files"]])
        self.assertEqual(3, observation["line"])

    def test_moved_definition_updates_line(self):
        path = self.root / "sample.c"
        path.write_text("\n\n" + path.read_text(encoding="utf-8"), encoding="utf-8")
        errors, report = INVENTORY.inspect(self.ledger, self.root)
        self.assertEqual([], errors)
        self.assertEqual(5, report["definitions"][0]["line"])

    def test_removed_semantic_anchor_is_rejected(self):
        self.ledger["operations"][0]["evidence"][0]["requires"] = ["loadedammo"]
        errors, _ = INVENTORY.inspect(self.ledger, self.root)
        self.assertTrue(any("reviewed token missing: loadedammo" in error for error in errors))

    def test_missing_definition_rejected_even_when_comment_mentions_it(self):
        self.ledger["operations"][0]["evidence"][0]["symbol"] = "missing"
        errors, _ = INVENTORY.inspect(self.ledger, self.root)
        self.assertTrue(any("found 0" in error for error in errors))

    def test_escape_and_absolute_source_paths_rejected(self):
        for path in ["../sample.c", str((self.root / "sample.c").resolve()), "..\\sample.c"]:
            with self.subTest(path=path):
                self.ledger["operations"][0]["evidence"][0]["path"] = path
                errors, _ = INVENTORY.inspect(self.ledger, self.root)
                self.assertTrue(errors)

    def test_duplicate_id_missing_units_and_unknown_status_rejected(self):
        self.ledger["operations"].append(copy.deepcopy(self.ledger["operations"][0]))
        other = self.ledger["operations"][1]
        del other["parameters"][0]["unit"]
        other["migration"]["status"] = "validated"
        errors, _ = INVENTORY.inspect(self.ledger, self.root)
        self.assertTrue(any("duplicate" in error for error in errors))
        self.assertTrue(any("parameter needs" in error for error in errors))
        self.assertTrue(any("invalid migration" in error for error in errors))

    def test_duplicate_definition_is_not_silently_selected(self):
        with (self.root / "sample.c").open("a", encoding="utf-8") as stream:
            stream.write("\nvoid action(void) { realCall(); }\n")
        errors, _ = INVENTORY.inspect(self.ledger, self.root)
        self.assertTrue(any("found 2" in error for error in errors))

    def test_extern_cpp_definition(self):
        self.assertEqual("adapter", INVENTORY.functions('extern "C" int adapter(int x) { return x; }')[0]["symbol"])

    def test_alternative_preprocessor_braces_are_explicitly_bounded(self):
        definitions = INVENTORY.functions('''void native(void) {
#ifdef VERSION
    if (a) {
#else
    if (b) {
#endif
        first();
    }
}
void after(void) { second(); }
''')
        self.assertEqual(["native", "after"], [x["symbol"] for x in definitions])
        self.assertIn("unbalanced", definitions[0]["boundary"])
        self.assertNotIn("second", definitions[0]["body"])

    def test_non_object_ledger_rejected(self):
        self.assertEqual(["ledger must be an object"], INVENTORY.inspect([], self.root)[0])

    def test_else_if_is_not_a_definition_or_boundary(self):
        definitions = INVENTORY.functions('''void native(void) {
    if (a) { first(); }
    else if (b) { second(); }
}
void after(void) { third(); }
''')
        self.assertEqual(["native", "after"], [x["symbol"] for x in definitions])
        self.assertIn("second", definitions[0]["body"])


if __name__ == "__main__":
    unittest.main()
