"""Synthetic controls for the collected evidence assembler."""
import gzip
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory
import unittest

import assemble_skedar_swarm_run as assembler

HERE = Path(__file__).resolve().parent
BASE = HERE.parent
sys.path.insert(0, str(BASE))
from test_verify_skedar_swarm_pair import make_run

CPU_ID = "20261120T031800123456Z-skedar_swarm_baseline_cpu-a1b2c3d4"
GPU_ID = "20261120T032000123456Z-skedar_swarm_baseline_gpu-c3d4a1b2"
QUEUE = "q_20261120031700000_abcd1234"


def compressed(path, body):
    path.write_bytes(gzip.compress(json.dumps(body, sort_keys=True, separators=(",", ":")).encode(), mtime=0))


def bind(path):
    body = path.read_bytes()
    return {"bytes": len(body), "sha256": hashlib.sha256(body).hexdigest()}


class AssemblyTests(unittest.TestCase):
    def setUp(self):
        temporary = TemporaryDirectory(dir=HERE)
        self.addCleanup(temporary.cleanup)
        self.folder = Path(temporary.name)
        self.cpu = self.folder / "cpu"
        self.gpu = self.folder / "gpu"
        self.prepare(self.cpu, "CPU", CPU_ID)
        self.prepare(self.gpu, "GPU_FULL", GPU_ID)

    def prepare(self, root, mode, identifier):
        manifest = make_run(root, mode)
        recipe = json.loads(gzip.decompress((root / "recipe.json.gz").read_bytes()))
        seed = json.loads(gzip.decompress((root / "seed.json.gz").read_bytes()))
        fixture = json.loads((root / "fixture.json").read_bytes())
        paths = [event["path"] for event in fixture["input_sequence"] if event["type"] == "screenshot"]
        log = root / "native.log"
        rows = log.read_text(encoding="utf-8")
        final = []
        for binding, path in zip(manifest["captures"], paths, strict=True):
            (root / binding["path"]).rename(root / path)
            rows = rows.replace(binding["path"], path)
            final.append(path)
        log.write_text(rows, encoding="utf-8")
        recipe.update(
            id=identifier, completed="2026-11-20T03:19:00Z",
            retention={"retain_housekeeping": True, "operation_id": "1" * 32},
            definition=bind(root / "fixture.json"),
            changed={f"run/{name}": bind(root / name) for name in ["native.log", *final]},
        )
        for key in assembler.INSTALLED:
            seed[f"install/{key}"] = bind(root / key)
        seed_json = json.dumps(seed, sort_keys=True, separators=(",", ":")).encode()
        recipe["seed"] = hashlib.sha256(seed_json).hexdigest()
        compressed(root / "seed.json.gz", seed)
        compressed(root / "recipe.json.gz", recipe)
        (root / "source_manifest").write_text(json.dumps({
            "schema": 1, "source_commit": manifest["source_commit"],
            "client_sha256": bind(root / "client")["sha256"],
            "frozen_patch_sha256": assembler.PATCH_SHA256,
            "frozen_paths_sha256": assembler.FROZEN_PATHS,
        }), encoding="utf-8")
        (root / "manifest.json").unlink()

    def invoke(self, root, mode="CPU", write=False, *, overrides=None):
        kind, identifier = ("cpu", CPU_ID) if mode == "CPU" else ("gpu", GPU_ID)
        args = {
            "mode": mode, "run-id": identifier, "queue-id": QUEUE,
            "client": "client", "arena": "arena", "scenario": "scenario", "source-manifest": "source_manifest",
            "fixture": "fixture.json", "receipt": "recipe.json.gz", "seed": "seed.json.gz", "log": "native.log",
            "client-install-path": "install/client", "arena-install-path": "install/arena",
            "scenario-install-path": "install/scenario", "capture-35": f"skedar_swarm_baseline_{kind}-35s.bmp",
            "capture-50": f"skedar_swarm_baseline_{kind}-50s.bmp",
        }
        args.update(overrides or {})
        command = [sys.executable, "-B", str(HERE / "assemble_skedar_swarm_run.py"), str(root)]
        for key, value in args.items():
            command.extend((f"--{key}", str(value)))
        if write:
            command.append("--write")
        return subprocess.run(command, capture_output=True, text=True, check=False)

    def mutate_recipe(self, root, change):
        path = root / "recipe.json.gz"
        recipe = json.loads(gzip.decompress(path.read_bytes()))
        change(recipe)
        compressed(path, recipe)

    def assert_rejected(self, result, message):
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn(message, result.stderr)
        self.assertFalse((self.cpu / "manifest.json").exists())

    def test_complete_stdout_has_no_filesystem_side_effect(self):
        result = self.invoke(self.cpu)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["run_id"], CPU_ID)
        self.assertFalse((self.cpu / "manifest.json").exists())

    def test_both_manifests_feed_frozen_verifier_and_cannot_be_overwritten(self):
        for root, mode in ((self.cpu, "CPU"), (self.gpu, "GPU_FULL")):
            result = self.invoke(root, mode, write=True)
            self.assertEqual(result.returncode, 0, result.stderr)
        checker = assembler.load_verifier(BASE / "verify_skedar_swarm_pair.py")
        self.assertEqual(checker.verify_pair(self.cpu, self.gpu)["verdict"], "evidence_consistent")
        previous = (self.cpu / "manifest.json").read_bytes()
        refusal = self.invoke(self.cpu, write=True)
        self.assertEqual(refusal.returncode, 1)
        self.assertEqual((self.cpu / "manifest.json").read_bytes(), previous)

    def test_retained_receipt_is_required(self):
        self.mutate_recipe(self.cpu, lambda data: data["retention"].update(retain_housekeeping=False))
        self.assert_rejected(self.invoke(self.cpu, write=True), "retained managed recipe")

    def test_recipe_head_is_bound_to_the_binary_source_receipt(self):
        self.mutate_recipe(self.cpu, lambda data: data["provenance"].update(git_head="1" * 40))
        self.assert_rejected(self.invoke(self.cpu, write=True), "source receipt does not bind")

    def test_collected_asset_length_is_bound(self):
        self.mutate_recipe(self.cpu, lambda data: data["changed"].update(
            {"install/arena": dict(bind(self.cpu / "arena"), bytes=17)}))
        self.assert_rejected(self.invoke(self.cpu, write=True), "managed entry differs")

    def test_capture_file_must_MATCH_frozen_time(self):
        result = self.invoke(self.cpu, write=True, overrides={"capture-35": "skedar_swarm_baseline_cpu-50s.bmp"})
        self.assert_rejected(result, "distinct paths")

    def test_source_install_paths_cannot_escape(self):
        result = self.invoke(self.cpu, write=True, overrides={"client-install-path": "../client"})
        self.assert_rejected(result, "invalid managed install path")

    def test_frozen_verifier_refuses_missing_population_even_with_matching_log_hash(self):
        log = self.cpu / "native.log"
        lines = log.read_text().splitlines()
        log.write_text("\n".join(line for line in lines if '"event": "population"' not in line) + "\n")
        self.mutate_recipe(self.cpu, lambda data: data["changed"].update({"run/native.log": bind(log)}))
        self.assert_rejected(self.invoke(self.cpu, write=True), "live population")


if __name__ == "__main__":
    unittest.main(verbosity=2)
