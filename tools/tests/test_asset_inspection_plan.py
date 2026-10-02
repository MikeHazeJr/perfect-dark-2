"""Tiny frozen-metadata/capture-plan tests; never read real asset payloads."""
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("plan", Path(__file__).parents[1] / "asset-inspection-plan.py")
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)


class PlanTests(unittest.TestCase):
    def setUp(self):
        def entry(digest): return dict(sha256=digest * 64, bytes=12, mtime_ns=1)
        self.files = {"PerfectDark.exe": entry("b"), "data/a.pdcharacter": entry("a"),
                      "data/b.pdcharacter": entry("c"), "data/cache/compiled.bin": entry("d"),
                      "pd.ntsc-final.z64": entry("e")}
        self.seed = hashlib.sha256(m.canonical(self.files)).hexdigest()

    def plan(self, **changes):
        return m.build_plan(self.files, self.seed, "b" * 64, **changes)

    def test_top_level_only_all_verdicts_pending(self):
        plan = self.plan()
        self.assertEqual(len(plan["archives"]), 2)
        self.assertFalse(plan["member_inventory_complete"])
        self.assertFalse(plan["payload_blobs_reverified"])
        self.assertTrue(all(row["catalog_id"] is None and row["consumed_member_verdict"] == "pending" for row in plan["archives"]))

    def test_cache_and_rom_do_not_change_public_source_hash(self):
        first = self.plan()["generation"]["source_sha256"]
        self.files["data/cache/compiled.bin"]["sha256"] = "f" * 64
        self.files["pd.ntsc-final.z64"]["sha256"] = "f" * 64
        self.assertEqual(first, self.plan()["generation"]["source_sha256"])
        self.files["data/a.pdcharacter"]["sha256"] = "f" * 64
        self.assertNotEqual(first, self.plan()["generation"]["source_sha256"])

    def test_copy_mtime_is_not_source_identity(self):
        first = self.plan()["generation"]["source_sha256"]
        self.files["data/a.pdcharacter"]["mtime_ns"] = 77
        self.assertEqual(first, self.plan()["generation"]["source_sha256"])

    def test_package_containers_included_without_claiming_nested_inventory(self):
        first = self.plan()["generation"]["source_sha256"]
        self.files["data/mod.pdmod"] = dict(sha256="f" * 64, bytes=12, mtime_ns=1)
        plan = self.plan()
        self.assertEqual(plan["family_counts"]["container"], 1)
        self.assertNotEqual(first, plan["generation"]["source_sha256"])
        self.assertFalse(plan["member_inventory_complete"])

    def test_capture_limit_uses_complete_asset_view_groups(self):
        plan = self.plan(max_captures=3, views=("front", "rear"))
        self.assertEqual(len(plan["capture_plan"]["slots"]), 2)
        self.assertEqual(plan["capture_plan"]["omitted_assets"], 1)
        self.assertEqual(plan["capture_plan"]["capture_bytes_reserved"], 64 * 1024**2)

    def test_no_images_or_automatic_capture_claim(self):
        plan = self.plan(); sheet = m.contact_sheet(plan)
        self.assertNotIn("<img", sheet)
        self.assertIn("every slot pending", sheet)
        self.assertEqual(plan["capture_plan"]["captures_created"], 0)
        self.assertFalse(plan["capture_plan"]["automatic_capture_supported"])

    def test_labels_escape_html(self):
        self.files['data/<bad>.pdcharacter'] = self.files.pop("data/a.pdcharacter")
        sheet = m.contact_sheet(self.plan())
        self.assertIn("&lt;bad&gt;", sheet)
        self.assertNotIn("data/<bad>", sheet)

    def test_client_mismatch_rejected(self):
        self.files["PerfectDark.exe"]["sha256"] = "f" * 64
        with self.assertRaises(ValueError): self.plan()

    def test_reference_cannot_cross_client_or_seed(self):
        reference = dict(client_sha256="b" * 64, source_seed=self.seed, records=[{"id": "base:x"}])
        for key, value in (("client_sha256", "f" * 64), ("source_seed", "f" * 64)):
            with self.assertRaises(ValueError): self.plan(reference=reference | {key: value})

    def test_reference_never_promotes_archive_or_capture(self):
        reference = dict(client_sha256="B" * 64, source_seed=self.seed, passed=1, failed=0, untested=0,
                         records=[{"id": "base:x", "verdict": "PASS"}])
        plan = self.plan(reference=reference)
        self.assertEqual(plan["native_reference"]["passed"], 1)
        self.assertTrue(all(row["fidelity_verdict"] == "pending" for row in plan["archives"]))
        self.assertTrue(all(slot["catalog_id"] is None for slot in plan["capture_plan"]["slots"]))

    def test_duplicate_reference_identity_rejected(self):
        reference = dict(client_sha256="b" * 64, source_seed=self.seed,
                         records=[{"id": "base:x"}, {"id": "base:x"}])
        with self.assertRaises(ValueError): self.plan(reference=reference)

    def test_valid_seed_manifest_and_changed_identity(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "seed.json.gz"
            path.write_bytes(gzip.compress(m.canonical(self.files)))
            self.assertEqual(m.seed_files(path, self.seed), self.files)
            with self.assertRaises(ValueError): m.seed_files(path, "f" * 64)

    def test_unsafe_seed_path_rejected_even_with_matching_manifest_hash(self):
        for bad in ("../escape", "/absolute", "C:stream", "a\\b", "a//b"):
            files = {bad: dict(sha256="a" * 64, bytes=1, mtime_ns=1)}
            raw = m.canonical(files)
            with tempfile.TemporaryDirectory() as temp:
                path = Path(temp) / "seed.gz"; path.write_bytes(gzip.compress(raw))
                with self.assertRaises(ValueError): m.seed_files(path, hashlib.sha256(raw).hexdigest())

    def test_expansion_budget_rejects_compression_bomb(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "seed.gz"; path.write_bytes(gzip.compress(b" " * 4096))
            with self.assertRaises(ValueError): m.bounded_json(path, compressed=True, limit=128)

    def test_output_budget_refuses_without_creating_directory(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "proof"
            with self.assertRaises(ValueError): m.write_plan(output, self.plan(), max_bytes=1)
            self.assertFalse(output.exists())

    def test_output_preserves_existing_evidence(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "proof"
            m.write_plan(output, self.plan())
            before = (output / "preparation.json").read_bytes()
            with self.assertRaises(FileExistsError): m.write_plan(output, self.plan())
            self.assertEqual((output / "preparation.json").read_bytes(), before)

    def test_invalid_capture_bounds_and_views(self):
        for value in (0, 129, True):
            with self.assertRaises(ValueError): self.plan(max_captures=value)
        for views in ((), ("default", "default"), ("../escape",)):
            with self.assertRaises(ValueError): self.plan(views=views)
        with self.assertRaises(ValueError): self.plan(max_captures=1, views=("front", "rear"))


    def strict_ledger(self):
        return dict(generation=self.plan()['generation'], assets=[dict(archive_origin='data/a.pdcharacter',
            archive_sha256='a'*64,catalog_id='base:character_a',members=[{'path':'character.ini'}],
            load_verdict='pass',consumer_evidence=[{'artifacts':[{'kind':'native_trace'}]}])])

    def test_strict_ledger_reference_labels_exact_slots_without_promoting_metadata(self):
        plan = self.plan(strict_ledger=self.strict_ledger(), selected_origins=['data/a.pdcharacter'],views=('front','rear'))
        self.assertEqual(plan['capture_plan']['slots'][0]['catalog_id'],'base:character_a')
        self.assertEqual(plan['strict_ledger_reference']['reported_native_load_pass'],1)
        self.assertEqual(plan['archives'][0]['consumed_member_verdict'],'pending')
        self.assertEqual(plan['archives'][0]['fidelity_verdict'],'pending')
        self.assertTrue(all(s['image'] is None for s in plan['capture_plan']['slots']))

    def test_wrong_generation_archive_or_duplicate_strict_reference_rejected(self):
        ledger = self.strict_ledger(); ledger['generation']['binary_sha256']='f'*64
        with self.assertRaises(ValueError): self.plan(strict_ledger=ledger)
        ledger = self.strict_ledger(); ledger['assets'][0]['archive_sha256']='f'*64
        with self.assertRaises(ValueError): self.plan(strict_ledger=ledger)
        ledger = self.strict_ledger(); ledger['assets'] *= 2
        with self.assertRaises(ValueError): self.plan(strict_ledger=ledger)

    def test_selected_slot_paths_must_be_exact_unique_cohort_members(self):
        for selected in ([],['data/missing.pdcharacter'],['data/a.pdcharacter']*2,['pd.ntsc-final.z64']):
            with self.assertRaises(ValueError): self.plan(selected_origins=selected)


if __name__ == "__main__": unittest.main()
