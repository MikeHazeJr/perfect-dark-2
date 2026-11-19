"""Every smoke launch must keep its game processes out of the real profile.

B-1124 (2026-11-14): tools/smoke-verify/run.ps1 passes --basedir/--savedir/
--moddir to each process only when a fixture sets
`"separate_process_installs": true`. A fixture whose processes share one
install and pass neither --portable nor --savedir runs with the game's default
save dir, which on Windows is the user's real profile
(%APPDATA%/perfectdark): it reads and rewrites pd.ini, playerstats.json and the
mod cache there. This test fails for any such multi-process fixture.

B-1203 (2026-11-16): the single-process path did NOT always pass --savedir, as
B-1124 assumed; only -ReuseInstallId did. 81 single-process fixtures (the local
multiplayer, menu, swarm, stage and vehicle smokes among them) ran against the
real profile. The runner now adds --portable to a single-process launch whose
boot_args name neither --portable nor --savedir; test_runner_isolates_single_process
pins that guard, so single-process fixtures need no flag of their own.

Run: python -B tools/tests/test_smoke_fixture_isolation.py
"""

import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tools" / "smoke-verify" / "tests"
RUNNER = ROOT / "tools" / "smoke-verify" / "run.ps1"
ISOLATING_ARGS = ("--portable", "--savedir")


def unisolated_processes(fixture):
    """Names of the processes of one fixture that would use the real profile."""
    processes = fixture.get("processes")
    if not isinstance(processes, list) or fixture.get("separate_process_installs") is True:
        return []
    bad = []
    for index, process in enumerate(processes):
        args = process.get("boot_args") or []
        if not any(arg in ISOLATING_ARGS for arg in args):
            bad.append(str(process.get("name", index)))
    return bad


class SmokeFixtureIsolationTests(unittest.TestCase):
    def test_rule_itself(self):
        shared = {"processes": [{"name": "host", "boot_args": ["--host"]},
                                {"name": "client", "boot_args": ["--portable"]}]}
        self.assertEqual(unisolated_processes(shared), ["host"])
        separate = dict(shared, separate_process_installs=True)
        self.assertEqual(unisolated_processes(separate), [])
        self.assertEqual(unisolated_processes({"input_sequence": []}), [])
        savedir = {"processes": [{"name": "a", "boot_args": ["--savedir", "x"]}]}
        self.assertEqual(unisolated_processes(savedir), [])

    def test_every_multi_process_fixture_is_isolated(self):
        paths = sorted(FIXTURES.glob("*.json"))
        self.assertGreater(len(paths), 50, "fixture folder not found")
        problems = []
        checked = 0
        for path in paths:
            try:
                fixture = json.loads(path.read_text(encoding="utf-8-sig"))
            except (OSError, ValueError) as exc:
                problems.append(f"{path.name}: unreadable ({exc})")
                continue
            if isinstance(fixture.get("processes"), list):
                checked += 1
            bad = unisolated_processes(fixture)
            if bad:
                problems.append(f"{path.name}: processes {', '.join(bad)} would use the real profile; "
                                "add --portable (shared install) or set separate_process_installs")
        self.assertGreater(checked, 10, "no multi-process fixtures found")
        self.assertEqual(problems, [], "\n".join(problems))

    def test_runner_isolates_single_process(self):
        """B-1203: after both strategy branches build the arguments, and before
        -ReuseInstallId replaces them, the runner adds --portable when the
        fixture's boot_args name neither --portable nor --savedir."""
        text = RUNNER.read_text(encoding="utf-8-sig").replace("\r\n", "\n")
        start = text.find("        $allArgs = @() + $bootArgs\n    }\n")
        self.assertGreater(start, 0, "single-process argument build not found in run.ps1")
        end = text.find("    if ($ReuseInstallId) {", start)
        self.assertGreater(end, start, "-ReuseInstallId branch not found after the argument build")
        guard = ("\n    if (-not (($bootArgs -contains '--portable') -or ($bootArgs -contains '--savedir'))) {\n"
                 "        $allArgs += @('--portable')\n")
        self.assertIn(guard, text[start:end],
                      "run.ps1 must add --portable to a single-process launch that names no save dir")


if __name__ == "__main__":
    sys.exit(unittest.main())
