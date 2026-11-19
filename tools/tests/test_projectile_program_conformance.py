#!/usr/bin/env python3
"""Tests for the archive validator's projectile and entity graph checks
(context/designs/modding/projectile-events.md, slices 2 to 5).

A projectile or entity archive's behavior.graph.json is v1 (the launch and
deploy data alone) or v2 (the same data plus a program of event.*, gate.* and
og.* nodes joined by execution edges). The validator applies the rules the
game's runner compiles a program with, so an archive that passes here
registers in the game, and one the game would refuse fails here.

Run: python -B tools/tests/test_projectile_program_conformance.py -v
It needs the repository's example mod and archive validator.
"""

from __future__ import annotations

import copy
import io
import json
import sys
import unittest
import zipfile
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
REPO = TOOLS.parent
sys.path.insert(0, str(TOOLS))
import asset_archive_conformance as conf  # noqa: E402

EXAMPLE = REPO / "examples" / "modding" / "typed-pdxxx-basic"

# What the extractor writes for a base projectile with a fuse: the v1 data
# nodes, then the fuse wired to what goes off (the node keeps the id the
# cases below name).
V2_PROJECTILE = {
    "schema": "pd.projectile_graph.v2",
    "asset_id": "example:tri_projectile",
    "graph_id": "tri_projectile",
    "nodes": [
        {"id": "spawn_state", "kind": "projectile.spawn_state", "params": {}},
        {"id": "motion", "kind": "projectile.motion", "params": {}},
        {"id": "fuse", "kind": "event.projectile.timer_expired"},
        {"id": "fuse_expired", "kind": "og.projectile.explode"},
    ],
    "edges": [
        {"from": "spawn_state", "to": "motion"},
        {"from": "fuse", "output": "out", "to": "fuse_expired", "input": "exec"},
    ],
    "exports": [{"name": "main", "node": "motion"}],
}


def program_errors(graph: dict) -> list[str]:
    return conf.validate_projectile_graph_program("x behavior.graph.json", graph)


def graph_errors(graph: dict, ext: str = ".pdprojectile") -> list[str]:
    return conf.validate_projectile_entity_graph("x", ext, json.dumps(graph))


def edited(**changes) -> dict:
    graph = copy.deepcopy(V2_PROJECTILE)
    graph.update(changes)
    return graph


def with_node(node: dict, *edges: dict) -> dict:
    graph = copy.deepcopy(V2_PROJECTILE)
    graph["nodes"].append(node)
    graph["edges"].extend(edges)
    return graph


def with_nodes(nodes: list, edges: list) -> dict:
    graph = copy.deepcopy(V2_PROJECTILE)
    graph["nodes"].extend(nodes)
    graph["edges"].extend(edges)
    return graph


class Schemas(unittest.TestCase):
    def test_the_two_schemas_of_each_family(self):
        self.assertEqual(conf.PROJECTILE_GRAPH_SCHEMAS[".pdprojectile"],
                         ("pd.projectile_graph.v1", "pd.projectile_graph.v2"))
        self.assertEqual(conf.PROJECTILE_GRAPH_SCHEMAS[".pdentity"],
                         ("pd.entity_graph.v1", "pd.entity_graph.v2"))
        self.assertEqual(graph_errors(V2_PROJECTILE), [])
        self.assertEqual(graph_errors(edited(schema="pd.entity_graph.v2"), ".pdentity"), [])
        # The other family's schema, or an unknown version, is refused.
        for schema, ext in (("pd.entity_graph.v2", ".pdprojectile"),
                            ("pd.projectile_graph.v2", ".pdentity"),
                            ("pd.projectile_graph.v3", ".pdprojectile"),
                            (None, ".pdprojectile")):
            errors = graph_errors(edited(schema=schema), ext)
            self.assertEqual(len(errors), 1, errors)
            self.assertIn("schema must be", errors[0])

    def test_a_v1_graph_is_data_only(self):
        v1 = edited(schema="pd.projectile_graph.v1")
        errors = graph_errors(v1)
        self.assertEqual(len(errors), 2, errors)
        self.assertTrue(all("is a program node: a program needs schema pd.projectile_graph.v2" in e
                            for e in errors), errors)
        v1["nodes"] = v1["nodes"][:2]
        v1["edges"] = v1["edges"][:1]
        self.assertEqual(graph_errors(v1), [])

    def test_text_that_is_not_one_object(self):
        self.assertIn("is invalid JSON", conf.validate_projectile_entity_graph("x", ".pdprojectile", "{")[0])
        self.assertIn("is not one JSON object",
                      conf.validate_projectile_entity_graph("x", ".pdprojectile", "[]")[0])


class Programs(unittest.TestCase):
    def test_the_events_and_modules_a_projectile_program_has(self):
        self.assertEqual(len(conf.PROJECTILE_GRAPH_MODULES), 13)
        self.assertEqual(len(conf.PROJECTILE_GRAPH_EVENTS), 15)
        self.assertEqual(conf.PROJECTILE_GRAPH_EVENT_MODULES["event.projectile.thrown"],
                         ("og.projectile.deploy_sentry",))
        self.assertEqual(conf.PROJECTILE_GRAPH_EVENT_MODULES["event.projectile.sentry_aimed"],
                         ("og.projectile.sentry_fire",))
        for gate in ("gate.projectile.fuse_armed", "gate.projectile.trigger_close",
                     "gate.projectile.trigger_character", "gate.projectile.damage_not_blast"):
            self.assertIn(gate, conf.PROJECTILE_GRAPH_GATES)
        self.assertEqual(conf.PROJECTILE_GRAPH_EVENT_MODULES["event.projectile.timer_expired"],
                         ("og.projectile.explode", "og.projectile.nbomb_storm"))
        for event in ("event.projectile.proximity", "event.projectile.remote_signal",
                      "event.projectile.damaged"):
            self.assertEqual(conf.PROJECTILE_GRAPH_EVENT_MODULES[event], ("og.projectile.spend_fuse",))
        # Each event applies its own modules: wired to one of them, valid.
        for event, modules in conf.PROJECTILE_GRAPH_EVENT_MODULES.items():
            self.assertIn(event, conf.PROJECTILE_GRAPH_EVENTS)
            for module in modules:
                self.assertIn(module, conf.PROJECTILE_GRAPH_MODULES)
                graph = with_nodes([{"id": "more", "kind": event}, {"id": "chosen", "kind": module}],
                                   [{"from": "more", "to": "chosen", "input": "exec"}])
                self.assertEqual(program_errors(graph), [], (event, module))
        # Every event may stand unwired (a record an author has not used yet).
        for kind in conf.PROJECTILE_GRAPH_EVENTS:
            self.assertEqual(program_errors(with_node({"id": "more", "kind": kind})), [], kind)

    def test_a_module_wired_where_it_does_nothing(self):
        # A record applies no module; a rule applies only its own.
        for event, module in (("event.projectile.spawned", "og.projectile.stick"),
                              ("event.projectile.hit_target", "og.projectile.spend_fuse"),
                              ("event.projectile.bounced", "og.projectile.stick"),
                              ("event.projectile.contact", "og.projectile.spend_fuse"),
                              ("event.projectile.landed", "og.projectile.explode"),
                              ("event.projectile.damaged", "og.projectile.explode"),
                              ("event.projectile.proximity", "og.projectile.nbomb_storm"),
                              ("event.projectile.remote_signal", "og.projectile.stick"),
                              ("event.projectile.thrown", "og.projectile.sentry_fire"),
                              ("event.projectile.sentry_aimed", "og.projectile.deploy_sentry"),
                              ("event.projectile.contact", "og.projectile.deploy_sentry"),
                              ("event.projectile.timer_expired", "og.projectile.land_knife")):
            graph = with_nodes([{"id": "more", "kind": event}, {"id": "chosen", "kind": module}],
                               [{"from": "more", "to": "chosen", "input": "exec"}])
            errors = program_errors(graph)
            self.assertEqual(len(errors), 1, (event, module, errors))
            self.assertIn("%s 'chosen' has no effect on %s ('more')" % (module, event), errors[0])
        # Reached through a gate, the same.
        graph = with_nodes([{"id": "rest", "kind": "event.projectile.came_to_rest"},
                            {"id": "armed", "kind": "gate.projectile.fuse_armed"},
                            {"id": "spend", "kind": "og.projectile.spend_fuse"},
                            {"id": "stick", "kind": "og.projectile.stick"}],
                           [{"from": "rest", "to": "armed", "input": "exec"},
                            {"from": "armed", "output": "pass", "to": "spend", "input": "exec"},
                            {"from": "armed", "output": "fail", "to": "stick", "input": "exec"}])
        errors = program_errors(graph)
        self.assertEqual(len(errors), 1, errors)
        self.assertIn("og.projectile.stick 'stick' has no effect on event.projectile.came_to_rest", errors[0])

    def test_events_of_other_graphs_are_refused(self):
        for kind in ("event.pickup.touched", "event.mission.completed", "event.projectile.unknown",
                     "event.vehicle.tick"):
            errors = program_errors(with_node({"id": "other", "kind": kind}))
            self.assertEqual(len(errors), 1, (kind, errors))
            self.assertIn("is not a projectile event", errors[0])
        # And a projectile event in a mission or level graph is refused there.
        for level in (False, True):
            errors = conf.validate_mission_graph_execution(
                "x", {"nodes": [{"id": "p", "kind": "event.projectile.spawned"}], "edges": []},
                level=level, rule_events=())
            self.assertTrue(any("belongs to a projectile or entity graph" in e for e in errors), errors)

    def test_modules_and_gates(self):
        self.assertIn("native module 'og.door.open' does not exist",
                      program_errors(with_node({"id": "m", "kind": "og.door.open"}))[0])
        self.assertIn("gate kind 'gate.door.native_unlocked' is not executable",
                      program_errors(with_node({"id": "g", "kind": "gate.door.native_unlocked"}))[0])
        # The runner's own gates compile in any program.
        self.assertEqual(program_errors(with_node(
            {"id": "g", "kind": "gate.mission.phase", "phase": "active"},
            {"from": "fuse", "to": "g", "input": "exec"},
            {"from": "g", "output": "pass", "to": "fuse_expired", "input": "exec"})), [])
        self.assertIn("needs phase", program_errors(with_node({"id": "g", "kind": "gate.mission.phase"}))[0])
        self.assertIn("needs status",
                      program_errors(with_node({"id": "g", "kind": "gate.objective.status"}))[0])

    def test_a_gate_wired_where_it_cannot_be_answered(self):
        # The original's proximity grenade: within 250 units, and a character.
        grenade = with_nodes(
            [{"id": "proximity", "kind": "event.projectile.proximity"},
             {"id": "close", "kind": "gate.projectile.trigger_close"},
             {"id": "character", "kind": "gate.projectile.trigger_character"},
             {"id": "spend", "kind": "og.projectile.spend_fuse"}],
            [{"from": "proximity", "output": "out", "to": "close", "input": "exec"},
             {"from": "close", "output": "pass", "to": "character", "input": "exec"},
             {"from": "character", "output": "pass", "to": "spend", "input": "exec"}])
        self.assertEqual(program_errors(grenade), [])
        # The original's homing rocket: damage that is not a blast's.
        rocket = with_nodes(
            [{"id": "damaged", "kind": "event.projectile.damaged"},
             {"id": "direct", "kind": "gate.projectile.damage_not_blast"},
             {"id": "spend", "kind": "og.projectile.spend_fuse"}],
            [{"from": "damaged", "output": "out", "to": "direct", "input": "exec"},
             {"from": "direct", "output": "pass", "to": "spend", "input": "exec"}])
        self.assertEqual(program_errors(rocket), [])
        # A gate reads what its event carries; anywhere else it could never pass.
        for event, gate in (("event.projectile.damaged", "gate.projectile.trigger_close"),
                            ("event.projectile.remote_signal", "gate.projectile.trigger_character"),
                            ("event.projectile.proximity", "gate.projectile.damage_not_blast"),
                            ("event.projectile.timer_expired", "gate.projectile.damage_not_blast"),
                            ("event.projectile.contact", "gate.projectile.trigger_close")):
            graph = with_nodes([{"id": "more", "kind": event}, {"id": "asked", "kind": gate}],
                               [{"from": "more", "output": "out", "to": "asked", "input": "exec"}])
            errors = program_errors(graph)
            self.assertEqual(len(errors), 1, (event, gate, errors))
            self.assertIn("%s 'asked' cannot be answered at %s ('more')" % (gate, event), errors[0])
        # fuse_armed reads the projectile itself: any event.
        for event in conf.PROJECTILE_GRAPH_EVENTS:
            graph = with_nodes([{"id": "more", "kind": event},
                                {"id": "armed", "kind": "gate.projectile.fuse_armed"}],
                               [{"from": "more", "output": "out", "to": "armed", "input": "exec"}])
            self.assertEqual(program_errors(graph), [], event)
        # A context gate nothing reaches is not asked anywhere.
        self.assertEqual(program_errors(with_node(
            {"id": "idle", "kind": "gate.projectile.trigger_close"})), [])

    def test_the_explosion_an_explode_names(self):
        def explode(**fields):
            graph = copy.deepcopy(V2_PROJECTILE)
            graph["nodes"][3] = dict(graph["nodes"][3], **fields)
            return program_errors(graph)

        self.assertEqual(conf.PROJECTILE_GRAPH_EXPLOSION_TYPES, 26)
        self.assertEqual(explode(), [])
        # The Super Dragon's grenade (21) and the Dragon's self-destruct (23).
        for number in (1, 13, 17, 21, 23, 25):
            self.assertEqual(explode(explosion_type=number), [], number)
        for bad in (0, 26, -1, 21.5, "21", True, None, [21]):
            errors = explode(explosion_type=bad)
            self.assertEqual(len(errors), 1, (bad, errors))
            self.assertIn("og.projectile.explode 'fuse_expired' needs explosion_type from 1 to 25, or none",
                          errors[0])
        # Only an explode reads the field.
        graph = with_nodes([{"id": "more", "kind": "event.projectile.damaged"},
                            {"id": "spend", "kind": "og.projectile.spend_fuse", "explosion_type": 99}],
                           [{"from": "more", "output": "out", "to": "spend", "input": "exec"}])
        self.assertEqual(program_errors(graph), [])

    def test_node_rules(self):
        self.assertIn("is used twice",
                      program_errors(with_node({"id": "fuse", "kind": "event.projectile.ended"}))[0])
        self.assertIn("needs a non-empty id and kind",
                      program_errors(with_node({"kind": "event.projectile.ended"}))[0])
        self.assertIn("has no objective to filter", program_errors(with_node(
            {"id": "e", "kind": "event.projectile.ended", "objective": "any"}))[0])
        # A data node may repeat an id the program uses: it is not the program.
        self.assertEqual(program_errors(with_node({"id": "fuse", "kind": "projectile.note"})), [])
        self.assertIn("has no nodes array", program_errors({"schema": "pd.projectile_graph.v2"})[0])

    def test_edge_rules(self):
        def one(*edges):
            return program_errors(edited(edges=list(edges)))

        self.assertIn("names a missing node",
                      one({"from": "fuse", "to": "motion", "input": "exec"})[0])
        self.assertIn("names a missing node",
                      one({"from": "spawn_state", "to": "fuse_expired", "input": "exec"})[0])
        self.assertIn("input must be \"exec\"",
                      one({"from": "fuse", "to": "fuse_expired", "input": "data"})[0])
        self.assertIn("output must be \"out\"",
                      one({"from": "fuse", "output": "pass", "to": "fuse_expired", "input": "exec"})[0])
        self.assertIn("is not a gate or a native module",
                      one({"from": "fuse_expired", "to": "fuse", "input": "exec"})[0])
        self.assertIn("has an output but is not an execution edge",
                      one({"from": "fuse", "output": "out", "to": "fuse_expired"})[0])
        self.assertIn("needs from and to", one({"from": "fuse"})[0])
        # A gate's edges name pass or fail.
        gated = with_node({"id": "g", "kind": "gate.mission.phase", "phase": "active"},
                          {"from": "g", "to": "fuse_expired", "input": "exec"})
        self.assertIn("needs output pass or fail", program_errors(gated)[0])
        # Structural edges of the data are not the program's.
        self.assertEqual(one({"from": "spawn_state", "to": "motion"},
                             {"from": "motion", "to": "nowhere"}), [])
        self.assertIn("edges must be an array", program_errors(edited(edges={}))[0])


class Archives(unittest.TestCase):
    def rebuilt(self, relative: str, graph_edit) -> bytes:
        """The example archive with its behavior.graph.json edited."""
        buffer = io.BytesIO()
        with zipfile.ZipFile(EXAMPLE / relative) as src, \
                zipfile.ZipFile(buffer, "w", compression=zipfile.ZIP_DEFLATED) as dst:
            for name in src.namelist():
                data = src.read(name)
                if name == "behavior.graph.json":
                    data = (json.dumps(graph_edit(json.loads(data.decode("utf-8"))), indent=2) + "\n").encode("utf-8")
                dst.writestr(name, data)
        return buffer.getvalue()

    def graph_messages(self, data: bytes, ext: str) -> list[str]:
        result = conf.validate_archive_bytes(data, "example" + ext, ext, recurse=True,
                                             known_catalog_ids=set(conf.collect_source_catalog_ids()))
        # The graph's own findings. An archive checked alone does not know the
        # ids its neighbours in the example mod declare; those findings are
        # the tree check's.
        return [e for e in result.errors
                if "behavior.graph.json" in e and "unknown catalog ID reference" not in e]

    def test_example_archives(self):
        for relative, ext in (("projectiles/tri_projectile.pdprojectile", ".pdprojectile"),
                              ("entities/tri_entity.pdentity", ".pdentity")):
            with self.subTest(archive=relative):
                # As stored: a v1 graph, valid.
                self.assertEqual(self.graph_messages((EXAMPLE / relative).read_bytes(), ext), [])
                v2 = conf.PROJECTILE_GRAPH_SCHEMAS[ext][1]

                def wired(graph, v2=v2):
                    graph = dict(graph, schema=v2)
                    graph["nodes"] = graph["nodes"] + V2_PROJECTILE["nodes"][2:]
                    graph["edges"] = graph["edges"] + V2_PROJECTILE["edges"][1:]
                    return graph

                # The same data with the fuse wired, as v2: valid.
                self.assertEqual(self.graph_messages(self.rebuilt(relative, wired), ext), [])

                # A program the runner refuses fails the archive.
                def broken(graph):
                    graph = wired(graph)
                    graph["nodes"] = graph["nodes"] + [{"id": "boom", "kind": "og.projectile.vanish"}]
                    return graph

                messages = self.graph_messages(self.rebuilt(relative, broken), ext)
                self.assertEqual(len(messages), 1, messages)
                self.assertIn("native module 'og.projectile.vanish' does not exist", messages[0])

                # Program nodes in a v1 graph: the game would not read them.
                def stale(graph):
                    return dict(wired(graph), schema=conf.PROJECTILE_GRAPH_SCHEMAS[ext][0])

                messages = self.graph_messages(self.rebuilt(relative, stale), ext)
                self.assertEqual(len(messages), 2, messages)


if __name__ == "__main__":
    unittest.main()
