import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
TOOL_PATH = ROOT / "tools" / "base_content_coverage.py"
SPEC = importlib.util.spec_from_file_location("base_content_coverage", TOOL_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


class BaseContentCoverageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.matrix = MODULE.build_matrix(ROOT)

    def _write_ordinary_observation(self, directory, stem):
        directory = Path(directory)
        log_path = directory / f"{stem}.log"
        evidence_path = directory / f"{stem}.evidence.json"
        observation_path = directory / f"{stem}.observation.json"
        log_path.write_text("smoke log\n", encoding="utf-8")
        evidence_path.write_text("{\"passed\": true}\n", encoding="utf-8")
        observation = {
            "runner": MODULE.SUPPORTED_SMOKE_RUNNER,
            "execution_mode": "ordinary-client",
            "proof_class": "ordinary-gameplay",
            "status": "pass",
            "exit_code": 0,
            "assertions_total": 1,
            "assertions_met": 1,
            "timed_out": False,
            "crashed": False,
            "log_path": log_path.name,
            "evidence_paths": [evidence_path.name],
        }
        observation_path.write_text(json.dumps(observation, sort_keys=True), encoding="utf-8")
        return observation, observation_path

    def _make_ordinary_receipt(self, directory, cell_id, binary, stem):
        observation, observation_path = self._write_ordinary_observation(directory, stem)
        return MODULE.make_receipt(
            self.matrix,
            cell_id,
            observation,
            binary,
            artifact_base=Path(directory),
            observation_path=observation_path,
        )

    def test_live_source_dimensions_and_inventory(self):
        matrix = self.matrix
        self.assertEqual(matrix["contract"]["expected_counts"], {
            "solo": 63,
            "combat_simulator": 108,
            "challenges": 120,
            "network": 126,
            "total": 417,
        })
        self.assertEqual(len(matrix["cells"]), 417)
        self.assertEqual(len(matrix["derived"]["solo_perfect_dark"]), 21)
        self.assertEqual(len(matrix["arena_inventory"]), 47)
        self.assertEqual(matrix["contract"]["standard_arena_count"], 18)
        self.assertTrue(any(row["category"] == "Bonus" for row in matrix["arena_inventory"]))
        self.assertTrue(any(row["category"] == "Random" for row in matrix["arena_inventory"]))

    def test_checked_matrix_reproduces_fresh_source_matrix(self):
        checked = MODULE._require_matrix(MODULE.read_json(ROOT / "tools" / "base-content-coverage" / "matrix-v1.json"))
        fresh = MODULE.build_matrix(ROOT)
        self.assertEqual(checked["matrix_fingerprint"], fresh["matrix_fingerprint"])
        self.assertEqual(MODULE._matrix_digest_payload(checked), MODULE._matrix_digest_payload(fresh))

    def test_all_cells_preserve_index_domains(self):
        for cell in self.matrix["cells"]:
            self.assertEqual(set(cell["indices"]), {"stage_table_index", "solo_stage_index", "stagenum"})
        solo = [cell for cell in self.matrix["cells"] if cell["family"] == "solo"]
        network = [cell for cell in self.matrix["cells"] if cell["family"] == "network"]
        combat = [cell for cell in self.matrix["cells"] if cell["family"] == "combat-simulator"]
        challenges = [cell for cell in self.matrix["cells"] if cell["family"] == "challenge"]
        self.assertEqual({cell["indices"]["solo_stage_index"] for cell in solo}, set(range(21)))
        self.assertEqual({cell["dimensions"]["difficulty"] for cell in solo}, {"agent", "special-agent", "perfect-agent"})
        self.assertEqual({cell["dimensions"]["mode"] for cell in network}, {"co-op", "counter-op"})
        self.assertEqual({cell["dimensions"]["scenario"] for cell in combat}, {
            "combat", "hold-the-briefcase", "hacker-central", "pop-a-cap", "king-of-the-hill", "capture-the-case"
        })
        self.assertEqual({cell["dimensions"]["player_count"] for cell in challenges}, {1, 2, 3, 4})
        self.assertTrue(all(cell["indices"]["stage_table_index"] is None for cell in challenges))

    def test_matrix_and_shards_are_deterministic_and_non_overlapping(self):
        second = MODULE.build_matrix(ROOT)
        self.assertEqual(self.matrix["matrix_fingerprint"], second["matrix_fingerprint"])
        self.assertEqual(self.matrix["cells"], second["cells"])
        shard_plan = MODULE.make_shard_plan(self.matrix, Path("tools/base-content-coverage/matrix-v1.json"), 0, 7)
        self.assertEqual(shard_plan["schema"], MODULE.WORK_PLAN_SCHEMA)
        self.assertEqual(shard_plan["status"], "not_executable")
        self.assertEqual(shard_plan["plan_kind"], "deterministic-coverage-work-plan")
        self.assertEqual(shard_plan["gameplay_commands"], [])
        self.assertEqual(len(shard_plan["receipt_commands"]), 60)
        self.assertTrue(all(command["receipt_command"][0] == "python" for command in shard_plan["receipt_commands"]))
        self.assertTrue(all(command["gameplay_command"] is None for command in shard_plan["receipt_commands"]))
        self.assertTrue(all(not Path(command["receipt_command"][0]).is_absolute() for command in shard_plan["receipt_commands"]))
        self.assertFalse(any("Users" in token or "AppData" in token for command in shard_plan["receipt_commands"] for token in command["receipt_command"]))
        self.assertTrue(all(
            cell["runner_fixture_status"] == "missing"
            and cell["execution_status"] == "not_executable_until_fixture_exists"
            and cell["runner_fixture"] is None
            for cell in shard_plan["cells"]
        ))
        absolute_plan = MODULE.make_shard_plan(self.matrix, ROOT / "tools/base-content-coverage/matrix-v1.json", 0, 7)
        self.assertFalse(any(str(ROOT) in token for command in absolute_plan["receipt_commands"] for token in command["receipt_command"]))
        shards = [MODULE.shard_cells(self.matrix, index, 7) for index in range(7)]
        self.assertEqual(sum(len(shard) for shard in shards), 417)
        ids = [cell["cell_id"] for shard in shards for cell in shard]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertEqual(set(ids), {cell["cell_id"] for cell in self.matrix["cells"]})

    def test_static_accelerated_timeout_and_crash_cannot_promote(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "pd.exe"
            binary.write_bytes(b"test binary")
            ordinary = self._make_ordinary_receipt(directory, "solo.000", binary, "solo.000")
            static = MODULE.make_receipt(
                self.matrix,
                "solo.001",
                {
                    "execution_mode": "static",
                    "proof_class": "static",
                    "status": "pass",
                    "exit_code": 0,
                },
                binary,
            )
            timeout = MODULE.make_receipt(
                self.matrix,
                "solo.002",
                {
                    "execution_mode": "ordinary-client",
                    "proof_class": "ordinary-gameplay",
                    "status": "not_run",
                    "timed_out": True,
                    "crashed": False,
                },
                binary,
            )
            crash = MODULE.make_receipt(
                self.matrix,
                "solo.003",
                {
                    "execution_mode": "ordinary-client",
                    "proof_class": "ordinary-gameplay",
                    "status": "fail",
                    "timed_out": False,
                    "crashed": True,
                    "crash_signature": "SMOKE_CRASH: test",
                },
                binary,
            )
            aggregate = MODULE.aggregate(self.matrix, [ordinary, static, timeout, crash], artifact_base=Path(directory))
            self.assertEqual(aggregate["ordinary_gameplay_pass_count"], 1)
            self.assertFalse(aggregate["ordinary_gameplay_complete"])
            self.assertEqual(aggregate["status"], "partial")
            self.assertEqual(
                [row["classification"] for row in aggregate["receipts"]],
                ["ordinary_gameplay_pass", "static", "timeout", "crash"],
            )

    def test_resume_and_overlap_rejection(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "pd.exe"
            binary.write_bytes(b"test binary")
            receipt = self._make_ordinary_receipt(directory, "challenge.119", binary, "challenge.119")
            pending = MODULE.pending_plan(self.matrix, [receipt], artifact_base=Path(directory))
            self.assertEqual(pending["pending_cell_count"], 416)
            self.assertIn("challenge.119", pending["completed_cell_ids"])
            with self.assertRaises(MODULE.CoverageError):
                MODULE.aggregate(self.matrix, [receipt, receipt])

            other_binary = Path(directory) / "pd-other.exe"
            other_binary.write_bytes(b"different binary")
            other_receipt = self._make_ordinary_receipt(directory, "challenge.118", other_binary, "challenge.118")
            with self.assertRaises(MODULE.CoverageError):
                MODULE.aggregate(self.matrix, [receipt, other_receipt])

    def test_ordinary_pass_requires_canonical_runner_and_retained_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "pd.exe"
            binary.write_bytes(b"test binary")
            valid, observation_path = self._write_ordinary_observation(directory, "strict")
            accepted = MODULE.make_receipt(
                self.matrix,
                "solo.004",
                valid,
                binary,
                artifact_base=Path(directory),
                observation_path=observation_path,
            )
            self.assertEqual(accepted["classification"], "ordinary_gameplay_pass")
            for field, value in (
                ("runner", "spoofed-runner.ps1"),
                ("evidence_paths", []),
                ("log_path", ""),
                ("assertions_total", 0),
                ("assertions_met", 0),
            ):
                observation = dict(valid)
                observation[field] = value
                rejected = MODULE.make_receipt(self.matrix, "solo.004", observation, binary)
                self.assertNotEqual(rejected["classification"], "ordinary_gameplay_pass", field)

            for artifact_path in (".", "missing-artifact.json"):
                observation = dict(valid)
                observation["evidence_paths"] = [artifact_path]
                invalid_path = Path(directory) / f"invalid-{artifact_path.replace('.', 'dot')}.observation.json"
                invalid_path.write_text(json.dumps(observation, sort_keys=True), encoding="utf-8")
                with self.assertRaises(MODULE.CoverageError):
                    MODULE.make_receipt(
                        self.matrix,
                        "solo.004",
                        observation,
                        binary,
                        artifact_base=Path(directory),
                        observation_path=invalid_path,
                    )

            MODULE.validate_receipt(self.matrix, accepted, artifact_base=Path(directory))
            log_path = Path(directory) / valid["log_path"]
            log_contents = log_path.read_bytes()
            log_path.unlink()
            with self.assertRaises(MODULE.CoverageError):
                MODULE.validate_receipt(self.matrix, accepted, artifact_base=Path(directory))
            log_path.write_bytes(log_contents)
            log_path.write_bytes(b"mutated smoke log\n")
            with self.assertRaises(MODULE.CoverageError):
                MODULE.validate_receipt(self.matrix, accepted, artifact_base=Path(directory))

    def test_malformed_receipt_json_is_a_coverage_error(self):
        with tempfile.TemporaryDirectory() as directory:
            malformed = Path(directory) / "bad.receipt.json"
            malformed.write_text("{not-json", encoding="utf-8")
            with self.assertRaises(MODULE.CoverageError):
                MODULE.load_receipts(self.matrix, [malformed])

    def test_accepted_resume_and_merge_require_and_recheck_artifact_base(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            binary = directory / "pd.exe"
            binary.write_bytes(b"test binary")
            receipt = self._make_ordinary_receipt(directory, "solo.005", binary, "accepted")
            receipt_path = directory / "accepted.receipt.json"
            receipt_path.write_text(json.dumps(receipt, sort_keys=True), encoding="utf-8")
            matrix_path = ROOT / "tools" / "base-content-coverage" / "matrix-v1.json"
            for command in ("resume", "merge"):
                out_path = directory / f"{command}.json"
                self.assertEqual(
                    MODULE.main([
                        command,
                        "--matrix", str(matrix_path),
                        "--receipts", str(receipt_path),
                        "--out", str(out_path),
                    ]),
                    2,
                )
                self.assertEqual(
                    MODULE.main([
                        command,
                        "--matrix", str(matrix_path),
                        "--receipts", str(receipt_path),
                        "--artifact-base", str(directory),
                        "--out", str(out_path),
                    ]),
                    0,
                )

            (directory / "accepted.log").write_text("mutated\n", encoding="utf-8")
            self.assertEqual(
                MODULE.main([
                    "merge",
                    "--matrix", str(matrix_path),
                    "--receipts", str(receipt_path),
                    "--artifact-base", str(directory),
                    "--out", str(directory / "mutated-merge.json"),
                ]),
                2,
            )
            (directory / "accepted.evidence.json").unlink()
            self.assertEqual(
                MODULE.main([
                    "resume",
                    "--matrix", str(matrix_path),
                    "--receipts", str(receipt_path),
                    "--artifact-base", str(directory),
                    "--out", str(directory / "missing-resume.json"),
                ]),
                2,
            )

    def test_unknown_source_revision_fails_closed(self):
        with mock.patch.object(MODULE, "_source_revision", return_value="unknown"):
            with self.assertRaises(MODULE.CoverageError):
                MODULE.build_matrix(ROOT)
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "pd.exe"
            binary.write_bytes(b"test binary")
            matrix = json.loads(json.dumps(self.matrix))
            matrix["source"]["revision"] = "unknown"
            with self.assertRaises(MODULE.CoverageError):
                MODULE.make_receipt(matrix, "solo.006", {"status": "not_run"}, binary)

    def test_receipt_source_and_matrix_fingerprints_are_required(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "pd.exe"
            binary.write_bytes(b"test binary")
            receipt = self._make_ordinary_receipt(directory, "network.000", binary, "fingerprint")
            changed = dict(receipt)
            changed["source_fingerprint"] = "WRONG"
            with self.assertRaises(MODULE.CoverageError):
                MODULE.validate_receipt(self.matrix, changed)


if __name__ == "__main__":
    unittest.main()
