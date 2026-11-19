#!/usr/bin/env python3
"""Tests for the archive validator's level rule wiring checks
(context/designs/modding/object-events.md, "Levels authored before slice 5").

A level graph carries the level's own rules as event nodes: the pickup and
door rules (slice 4) and the lift and glass rules (slice 5). A level of the
current format wires all seven. A level a mod authored before slice 5 names
the format it was written in (the arena.ini scenario_graph_cache kind before
"_liftglass") and still loads: it is held to the pickup and door rules and to
everything else its kind promised, not to the lift and glass rules. A base
arena always names the current kind.

Run: python -B tools/tests/test_level_rules_conformance.py -v
It needs the repository's example level, its stored dev-mods levels and the
archive validator.
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
import level_archive as la  # noqa: E402

LIFT_AND_GLASS_RULES = ("event.lift.called", "event.lift.idle", "event.lift.arrived",
                        "event.glass.damaged")
LIFT_AND_GLASS_PREFIXES = ("object.rules.lift.", "object.rules.glass.")
STORED_LEVELS = ("blank_room", "model_room", "grid_two_rooms")
KIND_MESSAGE = "arena.ini must declare scenario_graph_cache = "


def without_nodes(graph: dict, prefixes: tuple[str, ...]) -> dict:
    """The graph less every node whose id starts with one of the prefixes,
    and less every edge and link that names one."""
    out = copy.deepcopy(graph)
    gone = {node["id"] for node in out["nodes"]
            if isinstance(node.get("id"), str) and node["id"].startswith(prefixes)}
    out["nodes"] = [node for node in out["nodes"] if node.get("id") not in gone]
    for key in ("edges", "links"):
        out[key] = [edge for edge in out.get(key) or []
                    if edge.get("from") not in gone and edge.get("to") not in gone]
    return out


def stored_level(name: str) -> bytes:
    return (REPO / "dev-mods" / name / (name + ".pdarena")).read_bytes()


def arena_kind(arena: bytes) -> str:
    with zipfile.ZipFile(io.BytesIO(arena)) as outer:
        return conf.descriptor_value(outer.read("arena.ini").decode("utf-8"), "scenario_graph_cache")


def rewritten(arena: bytes, kind: str | None = None, catalog_id: str | None = None,
              graph_edit=None) -> bytes:
    """The arena with its arena.ini kind or catalog id replaced, or with the
    level graph of the scenario it embeds edited. Nothing else is touched, so
    hashes the archive records about the changed members go stale: a caller
    looks for one message, not for a clean result."""
    entries = []
    with zipfile.ZipFile(io.BytesIO(arena)) as outer:
        for name in outer.namelist():
            data = outer.read(name)
            if name == "arena.ini":
                text = data.decode("utf-8")
                if kind is not None:
                    text, count = re.subn(r"(?m)^scenario_graph_cache = .*$",
                                          "scenario_graph_cache = " + kind, text)
                    assert count == 1, count
                if catalog_id is not None:
                    text, count = re.subn(r"(?m)^catalog_id = .*$", "catalog_id = " + catalog_id, text)
                    assert count == 1, count
                data = text.encode("utf-8")
            elif name.endswith(".pdscenario") and graph_edit is not None:
                inner_entries = []
                with zipfile.ZipFile(io.BytesIO(data)) as inner:
                    for inner_name in inner.namelist():
                        member = inner.read(inner_name)
                        if inner_name == "level.graph.json":
                            graph = graph_edit(json.loads(member.decode("utf-8")))
                            member = (json.dumps(graph, indent=2) + "\n").encode("utf-8")
                        inner_entries.append((inner_name, member))
                data = la.zip_bytes(inner_entries)
            entries.append((name, data))
    return la.zip_bytes(entries)


def strip_lift_and_glass(graph: dict) -> dict:
    return without_nodes(graph, LIFT_AND_GLASS_PREFIXES)


class LevelGraphRules(unittest.TestCase):
    def setUp(self):
        self.graph, self.kind = la.read_example()

    def test_the_formats(self):
        current = conf.ARENA_SCENARIO_GRAPH_CACHE_KIND
        self.assertTrue(current.endswith("_ruleevents_liftglass"), current[-48:])
        self.assertEqual(conf.ARENA_SCENARIO_GRAPH_CACHE_KINDS_STILL_LOADED,
                         (current[:-len("_liftglass")],))
        self.assertNotIn(current, conf.ARENA_SCENARIO_GRAPH_CACHE_KINDS_STILL_LOADED)
        self.assertEqual(conf.LEVEL_RULE_EVENTS_BEFORE_LIFTGLASS,
                         ("event.pickup.touched", "event.door.use", "event.door.approach"))
        self.assertEqual(conf.MISSION_GRAPH_RULE_EVENTS,
                         conf.LEVEL_RULE_EVENTS_BEFORE_LIFTGLASS + LIFT_AND_GLASS_RULES)
        # Every older kind still loaded keeps the other promises of its kind.
        self.assertTrue(conf.arena_kind_promises_ai_edges(current))
        for older in conf.ARENA_SCENARIO_GRAPH_CACHE_KINDS_STILL_LOADED:
            self.assertTrue(conf.arena_kind_promises_ai_edges(older))
        self.assertFalse(conf.arena_kind_promises_ai_edges(None))
        self.assertFalse(conf.arena_kind_promises_ai_edges("pdscenario_some_other_kind"))

    def test_example_level_wires_every_rule(self):
        self.assertEqual(self.kind, conf.ARENA_SCENARIO_GRAPH_CACHE_KIND)
        self.assertEqual(conf.validate_mission_graph_execution("example", self.graph, level=True), [])
        kinds = {node.get("kind") for node in self.graph["nodes"]}
        for rule in conf.MISSION_GRAPH_RULE_EVENTS:
            self.assertIn(rule, kinds)

    def test_level_without_lift_and_glass_wiring(self):
        older = strip_lift_and_glass(self.graph)
        kinds = {node.get("kind") for node in older["nodes"]}
        for rule in LIFT_AND_GLASS_RULES:
            self.assertNotIn(rule, kinds)
        # A level of the current format must wire them: one error each.
        errors = conf.validate_mission_graph_execution("older", older, level=True)
        self.assertEqual(len(errors), len(LIFT_AND_GLASS_RULES), errors)
        for rule in LIFT_AND_GLASS_RULES:
            self.assertTrue(any("handles no %s:" % rule in error for error in errors), (rule, errors))
        # A level of the older format need not.
        self.assertEqual(
            conf.validate_mission_graph_execution(
                "older", older, level=True, rule_events=conf.LEVEL_RULE_EVENTS_BEFORE_LIFTGLASS),
            [])

    def test_pickup_and_door_rules_stay_required(self):
        for prefix, rules in (("object.rules.pickup.", ("event.pickup.touched",)),
                              ("object.rules.door.", ("event.door.use", "event.door.approach"))):
            graph = without_nodes(self.graph, (prefix,))
            for rule_events in (None, conf.LEVEL_RULE_EVENTS_BEFORE_LIFTGLASS):
                errors = conf.validate_mission_graph_execution("unwired", graph, level=True,
                                                               rule_events=rule_events)
                self.assertEqual(len(errors), len(rules), (prefix, rule_events, errors))
                for rule in rules:
                    self.assertTrue(any("handles no %s:" % rule in error for error in errors),
                                    (rule, errors))

    def test_an_event_node_without_an_execution_edge_is_not_a_handler(self):
        graph = copy.deepcopy(self.graph)
        graph["edges"] = [edge for edge in graph["edges"] if edge.get("from") != "object.rules.glass.damaged"]
        errors = conf.validate_mission_graph_execution("unwired", graph, level=True)
        self.assertTrue(any("handles no event.glass.damaged:" in error for error in errors), errors)


class StoredLevels(unittest.TestCase):
    def test_stored_levels_pass(self):
        accepted = (conf.ARENA_SCENARIO_GRAPH_CACHE_KIND,) + conf.ARENA_SCENARIO_GRAPH_CACHE_KINDS_STILL_LOADED
        for name in STORED_LEVELS:
            with self.subTest(level=name):
                arena = stored_level(name)
                self.assertIn(arena_kind(arena), accepted)
                self.assertEqual(la.conformance(arena, name + ".pdarena"), [])

    def test_the_kind_an_arena_may_name(self):
        arena = stored_level("blank_room")
        current = conf.ARENA_SCENARIO_GRAPH_CACHE_KIND
        older = conf.ARENA_SCENARIO_GRAPH_CACHE_KINDS_STILL_LOADED[0]

        def kind_refused(data: bytes) -> bool:
            return any(KIND_MESSAGE in error for error in la.conformance(data, "level.pdarena"))

        # An arena a mod authored: the current kind or an older one still loaded.
        self.assertFalse(kind_refused(rewritten(arena, kind=current)))
        self.assertFalse(kind_refused(rewritten(arena, kind=older)))
        # Any other kind is refused, and so is a kind cut short.
        self.assertTrue(kind_refused(rewritten(arena, kind="pdscenario_some_other_kind")))
        self.assertTrue(kind_refused(rewritten(arena, kind=older[:-len("_ruleevents")])))
        # A base arena names the current kind: an install re-extracts it otherwise.
        self.assertTrue(kind_refused(rewritten(arena, kind=older, catalog_id="base:arena_blank_room")))
        self.assertFalse(kind_refused(rewritten(arena, kind=current, catalog_id="base:arena_blank_room")))

    def test_the_rules_follow_the_kind_the_arena_names(self):
        current = conf.ARENA_SCENARIO_GRAPH_CACHE_KIND
        older = conf.ARENA_SCENARIO_GRAPH_CACHE_KINDS_STILL_LOADED[0]
        for name in STORED_LEVELS:
            with self.subTest(level=name):
                arena = stored_level(name)

                def unwired(data: bytes) -> list[str]:
                    return [rule for rule in conf.MISSION_GRAPH_RULE_EVENTS
                            if any("handles no %s:" % rule in error
                                   for error in la.conformance(data, name + ".pdarena"))]

                # Without the lift and glass wiring: fine for the older
                # format, four errors for the current one.
                self.assertEqual(unwired(rewritten(arena, kind=older, graph_edit=strip_lift_and_glass)), [])
                self.assertEqual(unwired(rewritten(arena, kind=current, graph_edit=strip_lift_and_glass)),
                                 list(LIFT_AND_GLASS_RULES))
                # The pickup rule is required of both.
                def strip_pickup(graph: dict) -> dict:
                    return without_nodes(graph, ("object.rules.pickup.",))
                for kind in (older, current):
                    self.assertIn("event.pickup.touched",
                                  unwired(rewritten(arena, kind=kind, graph_edit=strip_pickup)))


if __name__ == "__main__":
    unittest.main()
