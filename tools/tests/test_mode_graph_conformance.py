#!/usr/bin/env python3
"""Tests for the archive validator's mode program checks
(context/designs/modding/gamemode-graphs.md, slice 2).

A .pdgamemode carries mode.graph.json (pd.gamemode_graph.v1): event.mode.*
events wired to og.mode.* modules. The validator applies the rules the game
compiles and registers a mode program with (port/src/mode_rules.c and the
runner's mode domain), so an archive that passes here registers in the game,
and one the game would refuse fails here. A base archive must wire exactly
the original mode table.

Run: python -B tools/tests/test_mode_graph_conformance.py -v
"""

from __future__ import annotations

import copy
import io
import json
import re
import sys
import unittest
import zipfile
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
REPO = TOOLS.parent
sys.path.insert(0, str(TOOLS))
import asset_archive_conformance as conf  # noqa: E402

EXAMPLE = (REPO / "examples" / "modding" / "typed-pdxxx-basic" / "gamemodes"
           / "tri_gamemode.pdgamemode")
MODE_RULES_C = REPO / "port" / "src" / "mode_rules.c"
MPSCENARIO_ORDER = ("COMBAT", "HOLDTHEBRIEFCASE", "HACKERCENTRAL", "POPACAP",
                    "KINGOFTHEHILL", "CAPTURETHECASE")


def program(wiring: list[tuple[str, str]], catalog_id: str = "base:test") -> dict:
    """A mode graph with one event node per event and one module node per
    (event, module) pair, joined in order."""
    nodes, edges, events = [], [], {}
    for index, (event, module) in enumerate(wiring):
        if event not in events:
            events[event] = f"e{len(events)}"
            nodes.append({"id": events[event], "kind": event})
        nodes.append({"id": f"m{index}", "kind": module})
        edges.append({"from": events[event], "output": "out", "to": f"m{index}",
                      "input": "exec"})
    return {"schema": conf.MODE_GRAPH_SCHEMA, "catalog_id": catalog_id,
            "nodes": nodes, "edges": edges}


def native(mode_key: str) -> list[tuple[str, str]]:
    return sorted(conf.mode_graph_native_wiring(mode_key))


def errors(graph: dict, mode_key: str = "hold_the_briefcase",
           catalog_id: str = "base:test") -> list[str]:
    return conf.validate_mode_graph("x mode.graph.json", graph, mode_key, catalog_id)


def zip_errors(entries: dict[str, str]) -> list[str]:
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as zf:
        for name, data in entries.items():
            zf.writestr(name, data)
    buffer.seek(0)
    found: list[str] = []
    with zipfile.ZipFile(buffer) as zf:
        conf.validate_gamemode_source_contract("x.pdgamemode", zf, set(zf.namelist()), found)
    return found


class ModeGraphTableTests(unittest.TestCase):
    """The validator's tables are the game's (port/src/mode_rules.c)."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.source = MODE_RULES_C.read_text(encoding="utf-8")

    def test_events_match_the_game(self) -> None:
        events = re.findall(r'\{ "(event\.mode\.[a-z_]+)", MISSION_GRAPH_EVENT_MODE_[A-Z_]+ \}',
                            self.source)
        self.assertEqual(tuple(events), conf.MODE_GRAPH_EVENTS)

    def test_modules_and_their_events_match_the_game(self) -> None:
        # modeRulesSeamEvent: the event each seam's modules are chosen by.
        body = self.source[self.source.index("s32 modeRulesSeamEvent(s32 seam)"):]
        body = body[:body.index("\n}\n")]
        seam_event: dict[str, str] = {}
        pending: list[str] = []
        for line in body.splitlines():
            pending += re.findall(r"case MODE_SEAM_([A-Z_]+):", line)
            chosen = re.search(r"return MISSION_GRAPH_EVENT_MODE_([A-Z_]+);", line)
            if chosen:
                for seam in pending:
                    seam_event[seam] = "event.mode." + chosen.group(1).lower()
                pending = []
        self.assertEqual(len(seam_event), 14)

        modules = re.findall(
            r'\{ "(og\.mode\.[a-z]+\.[a-z_]+)", (MPSCENARIO_([A-Z]+)|MODE_RULES_DEFAULT), '
            r'MODE_SEAM_([A-Z_]+) \}', self.source)
        self.assertEqual(len(modules), 50)
        self.assertEqual(len(conf.MODE_GRAPH_MODULES), 50)
        self.assertEqual({kind for kind, *_ in modules}, set(conf.MODE_GRAPH_MODULES))
        names = list(conf.MODE_GRAPH_MODE_NAMES.values())
        for kind, owner, scenario, seam in modules:
            with self.subTest(kind=kind):
                self.assertIn(kind, conf.MODE_GRAPH_EVENT_MODULES[seam_event[seam]])
                name = "default" if owner == "MODE_RULES_DEFAULT" \
                    else names[MPSCENARIO_ORDER.index(scenario)]
                self.assertEqual(kind, f"og.mode.{name}.{seam.lower()}")
                self.assertEqual(conf.mode_graph_module_owner(kind), name)

    def test_mode_names_match_the_game(self) -> None:
        names = re.search(r"s_ModeNames\[MODE_RULES_MODE_COUNT\] = \{\s*(.*?)\s*\};",
                          self.source, re.S).group(1)
        self.assertEqual([None if item.strip() == "NULL" else item.strip().strip('"')
                          for item in names.split(",") if item.strip()],
                         list(conf.MODE_GRAPH_MODE_NAMES.values()))


class ModeGraphProgramTests(unittest.TestCase):

    def test_every_base_mode_passes_with_its_own_table(self) -> None:
        for mode_key in conf.MODE_GRAPH_MODE_NAMES:
            with self.subTest(mode=mode_key):
                self.assertEqual(errors(program(native(mode_key)), mode_key), [])

    def test_base_wiring_follows_the_original_dispatchers(self) -> None:
        self.assertEqual(native("combat"), [("event.mode.score", "og.mode.default.score"),
                                            ("event.mode.spawn", "og.mode.default.spawn")])
        ctc = native("capture_the_case")
        self.assertIn(("event.mode.spawn", "og.mode.ctc.spawn"), ctc)
        self.assertIn(("event.mode.spawn", "og.mode.default.spawn"), ctc)
        self.assertNotIn(("event.mode.score", "og.mode.default.score"), ctc)
        self.assertIn(("event.mode.chosen", "og.mode.koh.save"), native("king_of_the_hill"))
        self.assertEqual(len(native("hold_the_briefcase")), 11)

    def test_a_base_archive_is_held_to_the_table(self) -> None:
        wiring = native("hold_the_briefcase")
        short = [pair for pair in wiring if pair[1] != "og.mode.htb.hud"]
        found = errors(program(short))
        self.assertTrue(any("is not the original hold_the_briefcase table" in e and
                            "og.mode.htb.hud" in e for e in found), found)
        # A mod's archive may wire less (or the same modules differently).
        self.assertEqual(errors(program(short, "mod:htb"), catalog_id="mod:htb"), [])

    def test_a_module_on_an_event_that_does_not_choose_it_is_refused(self) -> None:
        graph = program([("event.mode.tick", "og.mode.htb.score"),
                         ("event.mode.spawn", "og.mode.default.spawn")], "mod:x")
        found = errors(graph, catalog_id="mod:x")
        self.assertTrue(any("og.mode.htb.score" in e and "has no effect on event.mode.tick" in e
                            for e in found), found)

    def test_both_branches_of_a_gate_count(self) -> None:
        graph = program([("event.mode.spawn", "og.mode.default.spawn")], "mod:x")
        graph["nodes"] += [{"id": "t", "kind": "event.mode.tick"},
                           {"id": "g", "kind": "gate.mission.phase", "phase": "active"},
                           {"id": "i", "kind": "og.mode.htb.init"}]
        graph["edges"] += [{"from": "t", "to": "g", "input": "exec"},
                           {"from": "g", "output": "fail", "to": "i", "input": "exec"}]
        self.assertTrue(any("og.mode.htb.init" in e and "has no effect" in e
                            for e in errors(graph, catalog_id="mod:x")))

    def test_another_modes_module_is_refused(self) -> None:
        graph = program([("event.mode.tick", "og.mode.koh.tick"),
                         ("event.mode.spawn", "og.mode.default.spawn")], "mod:x")
        self.assertTrue(any("is another mode's" in e
                            for e in errors(graph, "hold_the_briefcase", "mod:x")))
        self.assertEqual(errors(graph, "king_of_the_hill", "mod:x"), [])

    def test_a_mode_must_place_its_players(self) -> None:
        graph = program([("event.mode.tick", "og.mode.htb.tick")], "mod:x")
        self.assertTrue(any("event.mode.spawn reaches no spawn module" in e
                            for e in errors(graph, catalog_id="mod:x")))

    def test_other_events_modules_and_gates_are_refused(self) -> None:
        base = program([("event.mode.spawn", "og.mode.default.spawn")], "mod:x")
        cases = [
            ({"id": "q", "kind": "event.objective.status_changed"}, "is not a mode event"),
            ({"id": "q", "kind": "event.mode.unknown"}, "is not a mode event"),
            ({"id": "q", "kind": "event.mode.tick", "objective": "any"}, "has no objective to filter"),
            ({"id": "q", "kind": "og.mode.htb.fly"}, "does not exist"),
            ({"id": "q", "kind": "gate.projectile.fuse_armed"}, "is not executable"),
        ]
        for node, message in cases:
            with self.subTest(kind=node["kind"]):
                graph = copy.deepcopy(base)
                graph["nodes"].append(node)
                self.assertTrue(any(message in e for e in errors(graph, catalog_id="mod:x")),
                                errors(graph, catalog_id="mod:x"))

    def test_schema_and_catalog_id(self) -> None:
        graph = program(native("combat"), "base:combat")
        graph["schema"] = "pd.gamemode_graph.v0"
        self.assertTrue(any("schema must be" in e for e in errors(graph, "combat", "base:combat")))
        graph = program(native("combat"), "base:other")
        self.assertTrue(any("catalog_id must be" in e for e in errors(graph, "combat", "base:combat")))


class ModeArchiveTests(unittest.TestCase):

    def test_the_example_mode_passes(self) -> None:
        with zipfile.ZipFile(EXAMPLE) as zf:
            found: list[str] = []
            conf.validate_gamemode_source_contract("tri_gamemode", zf, set(zf.namelist()), found)
            self.assertEqual(found, [])
            graph = json.loads(zf.read("mode.graph.json").decode("utf-8"))
        self.assertEqual(graph["catalog_id"], "example:tri_gamemode")

    def test_a_mode_archive_needs_its_program(self) -> None:
        entries = dict(conf.GAMEMODE_SELFTEST_ENTRIES)
        self.assertEqual(zip_errors(entries), [])
        without = {k: v for k, v in entries.items() if k != "mode.graph.json"}
        self.assertTrue(any("must carry mode.graph.json" in e for e in zip_errors(without)))
        undeclared = dict(entries)
        undeclared["gamemode.ini"] = entries["gamemode.ini"].replace(
            "mode_graph_file = mode.graph.json\n", "")
        self.assertTrue(any("must declare mode_graph_file" in e for e in zip_errors(undeclared)))

    def test_the_archive_schema_requires_and_allows_the_program(self) -> None:
        source = (TOOLS / "asset_archive_conformance.py").read_text(encoding="utf-8")
        self.assertIn('required=["gamemode.ini", "rules.json", "mode.graph.json"]', source)
        self.assertIn('allowed=["gamemode.ini", "rules.json", "mode.graph.json"]', source)


if __name__ == "__main__":
    unittest.main()
