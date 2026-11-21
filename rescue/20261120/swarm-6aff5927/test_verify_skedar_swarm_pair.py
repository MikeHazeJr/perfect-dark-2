"""Synthetic regressions for the offline verifier. These never count as native evidence."""
from copy import deepcopy
import gzip
import hashlib
import json
from pathlib import Path
import struct
from tempfile import TemporaryDirectory
import unittest

from verify_skedar_swarm_pair import verify_pair

ROOT = Path(__file__).resolve().parent


def actor(slot):
    return dict(event="spawn", slot=slot, source_row=100 + slot, generation=1,
                chrnum=20 + slot, prop_index=30 + slot, position=[slot, 0, 0],
                rooms=[1], team=2, body="example:synthetic_body", model_scale=0.5,
                radius=8, height=32, yaw=0, animation=1, animation_frame=0,
                support_up=[0, 1, 0])


def events(mode):
    rows = [[slot, 1, 20 + slot, 30 + slot] for slot in range(4)]
    yield dict(event="start", mode=mode, map="base:mp_skedar", count=4, seed=123,
               player_anchor=[0, 0, 0])
    yield from map(actor, range(4))
    yield dict(event="ready")
    if mode == "GPU_FULL":
        yield dict(event="gpu_init", compiled=True, linked=True, gl_error=0)
        yield dict(event="gpu_dispatch", serial=10, count=4, mode="GPU_FULL",
                   inputs=[[slot, 0, 0, 0, 1, 0] for slot in range(4)], gl_error=0)
        yield dict(event="gpu_readback", serial=10, path="fenced", markers=[10] * 4,
                   actors=rows, gl_error=0)
        for slot, actor_id in enumerate(rows):
            yield dict(event="gpu_apply", serial=10, slot=slot, actor=actor_id,
                       committed=True, before=[slot, 0, 0], requested=[slot, 0, 1],
                       actual=[slot, 0, 1], gl_error=0)


def bmp(pixel):
    header = bytearray(54)
    header[:2] = b"BM"
    struct.pack_into("<I", header, 2, 58)
    struct.pack_into("<IIiiHHI", header, 10, 54, 40, 1, 1, 1, 24, 0)
    return header + bytes([pixel, pixel + 1, pixel + 2, 0])


def make_run(root, mode, changed=None, *, late_gpu=False, bad_closeout=False, omit_window=None):
    root.mkdir(exist_ok=True)
    manifest = dict(schema=1, mode=mode, run_id=mode, queue_id="synthetic",
                    source_commit="0" * 40)

    def bind(name, data):
        (root / name).write_bytes(data)
        return dict(path=name, sha256=hashlib.sha256(data).hexdigest())

    run_events = list(events(mode))
    for event in run_events:
        event["stage_epoch"] = 1
    original_batch = deepcopy(run_events[7:]) if mode == "GPU_FULL" else []
    if changed:
        changed(run_events)
    first_events = run_events[:6] if late_gpu else run_events
    gpu_batch = original_batch

    def batch_lines(serial):
        for event in gpu_batch:
            updated = dict(event, serial=serial)
            if updated["event"] == "gpu_readback":
                updated["markers"] = [serial] * 4
            yield event_line(updated)

    def event_line(event):
        return "SWARM.BASELINE: " + json.dumps(event)

    rows = [[slot, 1, 20 + slot, 30 + slot] for slot in range(4)]
    lines = list(map(event_line, first_events))
    manifest["captures"] = []
    for index, timer in enumerate((35000, 50000)):
        if index != omit_window:
            lines.append(event_line(dict(event="population", stage_epoch=1, actors=rows)))
        if index == 1 and not late_gpu:
            lines.extend(batch_lines(11))
        name = f"{mode}-{timer}.bmp"
        pixels = index * 10 + (5 if mode == "CPU" else 10)
        manifest["captures"].append(dict(bind(name, bmp(pixels)), at_ms=timer))
        lines.append(f"SMOKE: screenshot at_ms={timer} -> {name}")
        lines.append(f"SMOKE.GLSHOT: captured 1x1 -> C:\\dummy\\{name}")
    if late_gpu:
        lines.extend(map(event_line, run_events[6:]))
    lines.extend(batch_lines(12))
    if omit_window != 2:
        lines.append(event_line(dict(event="population", stage_epoch=1, actors=rows)))
    lines.extend(("SMOKE: scripted exit at_ms=60000", "SMOKE: result=scripted_exit"))
    manifest["log"] = bind("native.log", ("\n".join(lines) + "\n").encode())
    suffix = "cpu" if mode == "CPU" else "gpu"
    fixture = (ROOT / f"skedar_swarm_baseline_{suffix}.json").read_bytes()
    manifest["fixture"] = bind("fixture.json", fixture)
    closeout = dict(process_id=123, ownership_established=True, verified=not bad_closeout, cleanup_errors=[])
    outcome = dict(name=json.loads(fixture)["scenario_name"], passed=True, exit_code=0,
                   launch_error=None, launch_closeout=closeout)
    seed_catalog = {}
    for name in ("client", "arena", "scenario", "source_manifest"):
        manifest[name] = bind(name, name.encode())
        if name != "source_manifest":
            manifest[name]["install_path"] = f"install/{name}"
            seed_catalog[f"install/{name}"] = manifest[name]
    seed_json = json.dumps(seed_catalog, sort_keys=True, separators=(",", ":")).encode()
    manifest["seed"] = bind("seed.json.gz", gzip.compress(seed_json, mtime=0))
    recipe = dict(
        schema=1, complete=True, id=mode, definition=manifest["fixture"], outcome=outcome,
        seed=hashlib.sha256(seed_json).hexdigest(), deleted=[],
        provenance={"git_head": manifest["source_commit"]},
        changed={f"run/{item['path']}": item for item in [manifest["log"], *manifest["captures"]]},
    )
    manifest["receipt"] = bind("recipe.json.gz", gzip.compress(json.dumps(recipe).encode(), mtime=0))
    (root / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
    return manifest


class PairedEvidenceTests(unittest.TestCase):
    def setUp(self):
        temporary = TemporaryDirectory(dir=ROOT)
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        self.cpu, self.gpu = root / "cpu", root / "gpu"
        make_run(self.cpu, "CPU")
        make_run(self.gpu, "GPU_FULL")

    def reject_log_change(self, change, reason):
        make_run(self.gpu, "GPU_FULL", change)
        with self.assertRaisesRegex(ValueError, reason):
            verify_pair(self.cpu, self.gpu)

    def test_positive_pair_and_visual_gate(self):
        result = verify_pair(self.cpu, self.gpu)
        self.assertEqual(result["verdict"], "evidence_consistent")
        self.assertEqual(result["visual_inspection"], "required")
        self.assertEqual(result["gpu"]["qualified_gpu_readbacks"], {10: "fenced", 11: "fenced", 12: "fenced"})

    def test_missing_application(self):
        self.reject_log_change(lambda events: events.pop(), "four-actor commit")

    def test_missing_dispatch(self):
        self.reject_log_change(lambda events: events.pop(7), "no matching dispatch")

    def test_late_gpu_commit(self):
        make_run(self.gpu, "GPU_FULL", late_gpu=True)
        with self.assertRaisesRegex(ValueError, "in a capture window"):
            verify_pair(self.cpu, self.gpu)

    def test_missing_late_population(self):
        make_run(self.cpu, "CPU", omit_window=2)
        with self.assertRaisesRegex(ValueError, "live population"):
            verify_pair(self.cpu, self.gpu)

    def test_unverified_native_actor_closeout(self):
        make_run(self.gpu, "GPU_FULL", bad_closeout=True)
        with self.assertRaisesRegex(ValueError, "closeout"):
            verify_pair(self.cpu, self.gpu)

    def test_wrong_submitted_position(self):
        self.reject_log_change(lambda events: events[7]["inputs"][0].__setitem__(0, 15), "prepared positions")

    def test_submitted_pos_only_mode(self):
        self.reject_log_change(lambda events: events[7].update(mode="GPU_POS_ONLY"), "GPU_FULL dispatch")

    def test_wrong_stage_epoch(self):
        self.reject_log_change(lambda events: events[8].update(stage_epoch=2), "stage changed")

    def test_wrong_shader_marker(self):
        self.reject_log_change(lambda events: events[8]["markers"].__setitem__(0, 9), "echo")

    def test_stale_generation(self):
        self.reject_log_change(lambda events: events[9]["actor"].__setitem__(1, 2), "identity")

    def test_body_scale_mismatch(self):
        self.reject_log_change(lambda events: events[1].update(model_scale=0.6), "actors differs")

    def test_wrong_room(self):
        self.reject_log_change(lambda events: events[1].update(rooms=[2]), "actors differs")

    def test_invalid_pose(self):
        self.reject_log_change(lambda events: events[1].update(yaw=float("nan")), "invalid JSON")

    def test_reused_actor(self):
        self.reject_log_change(lambda events: events.insert(6, deepcopy(events[1])), "replacement")

    def test_unmoved_actor_batch(self):
        def cancel_motion(events):
            for event in events[9:]:
                event.update(actual=event["before"], requested=event["before"])
        self.reject_log_change(cancel_motion, "four-actor commit")

    def test_gl_failure(self):
        self.reject_log_change(lambda events: events[8].update(gl_error=1282), "GL error")

    def test_changed_source_bytes(self):
        (self.gpu / "arena").write_bytes(b"altered")
        with self.assertRaisesRegex(ValueError, "SHA-256 differs"):
            verify_pair(self.cpu, self.gpu)

    def test_missing_capture(self):
        (self.gpu / "GPU_FULL-35000.bmp").unlink()
        with self.assertRaises(FileNotFoundError):
            verify_pair(self.cpu, self.gpu)

    def test_fixture_schedule_and_no_cycle(self):
        for mode in ("cpu", "gpu"):
            fixture = json.loads((ROOT / f"skedar_swarm_baseline_{mode}.json").read_bytes())
            self.assertEqual(
                [(event["type"], event["at_ms"]) for event in fixture["input_sequence"]],
                [("wait_until", 0), ("screenshot", 35000), ("screenshot", 50000), ("exit", 60000)],
            )


if __name__ == "__main__":
    unittest.main(verbosity=2)
