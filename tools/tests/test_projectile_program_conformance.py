#!/usr/bin/env python3
"""Tests for the archive validator's projectile and entity graph checks
(context/designs/modding/projectile-events.md, slices 2 to 6).

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
# cases below name), and the arm that runs the fuse (slice 6).
V2_PROJECTILE = {
    "schema": "pd.projectile_graph.v2",
    "asset_id": "example:tri_projectile",
    "graph_id": "tri_projectile",
    "nodes": [
        {"id": "spawn_state", "kind": "projectile.spawn_state", "params": {}},
        {"id": "motion", "kind": "projectile.motion", "params": {}},
        {"id": "fuse", "kind": "event.projectile.timer_expired"},
        {"id": "fuse_expired", "kind": "og.projectile.explode"},
        {"id": "armed", "kind": "event.projectile.armed"},
        {"id": "armed_arm_fuse", "kind": "og.projectile.arm_fuse"},
    ],
    "edges": [
        {"from": "spawn_state", "to": "motion"},
        {"from": "fuse", "output": "out", "to": "fuse_expired", "input": "exec"},
        {"from": "armed", "output": "out", "to": "armed_arm_fuse", "input": "exec"},
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
        self.assertEqual(len(errors), 4, errors)
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
        self.assertEqual(len(conf.PROJECTILE_GRAPH_MODULES), 20)
        self.assertEqual(len(conf.PROJECTILE_GRAPH_EVENTS), 16)
        # Slice 6: the arms, in the order weaponTick tests them.
        self.assertEqual(conf.PROJECTILE_GRAPH_EVENT_MODULES["event.projectile.armed"],
                         ("og.projectile.arm_fuse", "og.projectile.arm_wall_fuse",
                          "og.projectile.arm_struck_fuse", "og.projectile.arm_mine_fuse",
                          "og.projectile.arm_remote", "og.projectile.arm_proximity",
                          "og.projectile.arm_bolt"))
        self.assertEqual(conf.PROJECTILE_GRAPH_MODULES[13:],
                         conf.PROJECTILE_GRAPH_EVENT_MODULES["event.projectile.armed"])
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
                              ("event.projectile.armed", "og.projectile.explode"),
                              ("event.projectile.contact", "og.projectile.arm_fuse"),
                              ("event.projectile.spawned", "og.projectile.arm_proximity"),
                              ("event.projectile.timer_expired", "og.projectile.arm_remote"),
                              ("event.projectile.timer_expired", "og.projectile.land_knife")):
            graph = with_nodes([{"id": "more", "kind": event}, {"id": "chosen", "kind": module}],
                               [{"from": "more", "to": "chosen", "input": "exec"}])
            errors = program_errors(graph)
            self.assertEqual(len(errors), 1, (event, module, errors))
            self.assertIn("%s 'chosen' has no effect on %s ('more')" % (module, event), errors[0])
        # Reached through a gate, the same.
        graph = with_nodes([{"id": "rest", "kind": "event.projectile.came_to_rest"},
                            {"id": "live", "kind": "gate.projectile.fuse_armed"},
                            {"id": "spend", "kind": "og.projectile.spend_fuse"},
                            {"id": "stick", "kind": "og.projectile.stick"}],
                           [{"from": "rest", "to": "live", "input": "exec"},
                            {"from": "live", "output": "pass", "to": "spend", "input": "exec"},
                            {"from": "live", "output": "fail", "to": "stick", "input": "exec"}])
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
                                {"id": "live", "kind": "gate.projectile.fuse_armed"}],
                               [{"from": "more", "output": "out", "to": "live", "input": "exec"}])
            self.assertEqual(program_errors(graph), [], event)
        # A context gate nothing reaches is not asked anywhere.
        self.assertEqual(program_errors(with_node(
            {"id": "idle", "kind": "gate.projectile.trigger_close"})), [])

    def test_a_fuse_needs_an_arm(self):
        def without_arm(**changes):
            graph = copy.deepcopy(V2_PROJECTILE)
            graph["nodes"] = graph["nodes"][:4]
            graph["edges"] = graph["edges"][:2]
            graph.update(changes)
            return graph

        # A fuse wired and nothing armed: only an arm asks the fuse rule, so
        # the object would never go off. The game refuses the program.
        errors = program_errors(without_arm())
        self.assertEqual(len(errors), 1, errors)
        self.assertIn("wires event.projectile.timer_expired but event.projectile.armed reaches no arm "
                      "that runs a fuse", errors[0])
        self.assertEqual(len(graph_errors(without_arm())), 1)
        # An entity archive carries its item's fuse alone: the item's
        # projectile archive arms it.
        self.assertEqual(conf.validate_projectile_graph_program("x", without_arm(), entity=True), [])
        self.assertEqual(graph_errors(without_arm(schema="pd.entity_graph.v2"), ".pdentity"), [])
        # Each arm that runs a fuse will do; the bolt's arm runs none.
        for arm in conf.PROJECTILE_GRAPH_EVENT_MODULES["event.projectile.armed"]:
            graph = without_arm()
            graph["nodes"] += [{"id": "armed", "kind": "event.projectile.armed"},
                               {"id": "arm", "kind": arm}]
            graph["edges"] += [{"from": "armed", "output": "out", "to": "arm", "input": "exec"}]
            errors = program_errors(graph)
            if arm == "og.projectile.arm_bolt":
                self.assertNotIn(arm, conf.PROJECTILE_GRAPH_FUSE_ARMS)
                self.assertEqual(len(errors), 1, (arm, errors))
                self.assertIn("reaches no arm that runs a fuse", errors[0])
            else:
                self.assertIn(arm, conf.PROJECTILE_GRAPH_FUSE_ARMS)
                self.assertEqual(errors, [], arm)
        self.assertEqual(len(conf.PROJECTILE_GRAPH_FUSE_ARMS), 6)
        # An arm behind a gate counts: the program can reach it.
        graph = without_arm()
        graph["nodes"] += [{"id": "armed", "kind": "event.projectile.armed"},
                           {"id": "live", "kind": "gate.projectile.fuse_armed"},
                           {"id": "arm", "kind": "og.projectile.arm_fuse"}]
        graph["edges"] += [{"from": "armed", "output": "out", "to": "live", "input": "exec"},
                           {"from": "live", "output": "pass", "to": "arm", "input": "exec"}]
        self.assertEqual(program_errors(graph), [])
        # A fuse event nothing leaves decides nothing and needs no arm; nor
        # does a program with no fuse (a thrown knife: its contact alone).
        graph = without_arm()
        graph["edges"] = graph["edges"][:1]
        self.assertEqual(program_errors(graph), [])
        knife = without_arm()
        knife["nodes"] = knife["nodes"][:2] + [{"id": "contact", "kind": "event.projectile.contact"},
                                               {"id": "stick", "kind": "og.projectile.stick"}]
        knife["edges"] = knife["edges"][:1] + [{"from": "contact", "to": "stick", "input": "exec"}]
        self.assertEqual(program_errors(knife), [])
        # An arm with no fuse wired is a choice too (a bolt has none).
        bolt = without_arm()
        bolt["nodes"] = bolt["nodes"][:2] + [{"id": "armed", "kind": "event.projectile.armed"},
                                             {"id": "arm", "kind": "og.projectile.arm_bolt"}]
        bolt["edges"] = bolt["edges"][:1] + [{"from": "armed", "to": "arm", "input": "exec"}]
        self.assertEqual(program_errors(bolt), [])

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
                self.assertEqual(len(messages), 4, messages)


# held-weapon-events.md slices 2 and 3: a base held function graph as the
# extractor writes it (romextract_pdweapon.c, s_emitHeldProgramNodes and
# s_emitHeldProgramEdges): the record, then the original's idle rule, whose
# attack is the function's trigger and its use_ammo.
V2_HELD = {
    "schema": "pd.weapon_graph.v2",
    "asset_id": "base:test__weapon",
    "graph_id": "primary",
    "nodes": [
        {"id": "primary_trigger", "kind": "event.trigger_pressed", "subgraph": "primary",
         "params": {"mode": "primary", "trigger": "pressed"}},
        {"id": "primary_action", "kind": "fire.hitscan", "subgraph": "primary",
         "params": {"mode": "primary", "function_type": "shoot_single", "ammo_slot": 0}},
        {"id": "primary_pressed", "kind": "event.weapon.trigger_pressed"},
        {"id": "primary_held", "kind": "event.weapon.ready_while_held"},
        {"id": "primary_has_ammo", "kind": "gate.weapon.has_ammo", "rounds": 2},
        {"id": "primary_has_reserve", "kind": "gate.weapon.has_reserve"},
        {"id": "primary_attack", "kind": "og.weapon.fire_once"},
        {"id": "primary_dry_fire", "kind": "og.weapon.dry_fire"},
        {"id": "primary_clip_empty", "kind": "event.weapon.clip_empty"},
        {"id": "primary_reload", "kind": "og.weapon.reload"},
        {"id": "primary_use_ammo", "kind": "og.weapon.use_ammo", "rounds": 1},
    ],
    "edges": [
        {"from": "primary_trigger", "to": "primary_action"},
        {"from": "primary_pressed", "output": "out", "to": "primary_has_ammo", "input": "exec"},
        {"from": "primary_held", "output": "out", "to": "primary_has_ammo", "input": "exec"},
        {"from": "primary_has_ammo", "output": "pass", "to": "primary_attack", "input": "exec"},
        {"from": "primary_attack", "output": "out", "to": "primary_use_ammo", "input": "exec"},
        {"from": "primary_has_ammo", "output": "fail", "to": "primary_has_reserve", "input": "exec"},
        {"from": "primary_has_reserve", "output": "pass", "to": "primary_dry_fire", "input": "exec"},
        {"from": "primary_clip_empty", "output": "out", "to": "primary_reload", "input": "exec"},
    ],
    "exports": [{"name": "primary", "node": "primary_action"}],
}


def weapon_errors(graph: dict, member: str = "behavior/primary.graph.json") -> list[str]:
    return conf.validate_weapon_function_graph("x", member, json.dumps(graph))


class HeldWeaponPrograms(unittest.TestCase):
    def test_the_base_wiring_and_the_record_are_valid(self):
        self.assertEqual(weapon_errors(V2_HELD), [])
        v1 = copy.deepcopy(V2_HELD)
        v1["schema"] = "pd.weapon_graph.v1"
        v1["nodes"] = v1["nodes"][:2]
        v1["edges"] = v1["edges"][:1]
        self.assertEqual(weapon_errors(v1), [])

    def test_schemas_and_the_profile(self):
        self.assertIn("schema must be", weapon_errors(dict(V2_HELD, schema="pd.weapon_graph.v3"))[0])
        # The profile is the profile executor's own grammar.
        profile = {"schema": "pd.weapon_graph.v2", "profile": "held_single_shot.v1", "nodes": []}
        self.assertEqual(weapon_errors(profile), [])
        self.assertIn("profile must be", weapon_errors(dict(profile, profile="held_burst.v1"))[0])
        # A v1 graph's program nodes would never be read.
        stale = dict(copy.deepcopy(V2_HELD), schema="pd.weapon_graph.v1")
        messages = weapon_errors(stale)
        self.assertEqual(len(messages), 9, messages)
        self.assertTrue(all("a program needs schema pd.weapon_graph.v2" in m for m in messages))
        # A v2 function graph names its function.
        self.assertIn("graph_id", weapon_errors(dict(V2_HELD, graph_id="first"))[0])

    def test_events_modules_and_gates(self):
        graph = copy.deepcopy(V2_HELD)
        graph["nodes"].append({"id": "odd", "kind": "event.weapon.trigger_twice"})
        self.assertIn("is not a weapon event", weapon_errors(graph)[0])
        graph = copy.deepcopy(V2_HELD)
        graph["nodes"].append({"id": "fuse", "kind": "event.projectile.timer_expired"})
        self.assertIn("is not a weapon event", weapon_errors(graph)[0])
        graph = copy.deepcopy(V2_HELD)
        graph["nodes"].append({"id": "x", "kind": "og.weapon.vanish"})
        self.assertIn("native module 'og.weapon.vanish' does not exist", weapon_errors(graph)[0])
        graph = copy.deepcopy(V2_HELD)
        graph["nodes"].append({"id": "g", "kind": "gate.weapon.vanish"})
        self.assertIn("is not executable", weapon_errors(graph)[0])
        # A record's own gate node (gate.cooldown_ready) is record data.
        graph = copy.deepcopy(V2_HELD)
        graph["nodes"].append({"id": "cool", "kind": "gate.cooldown_ready", "params": {}})
        graph["edges"].append({"from": "primary_trigger", "to": "cool"})
        self.assertEqual(weapon_errors(graph), [])

    def test_a_module_wired_where_it_does_nothing(self):
        graph = copy.deepcopy(V2_HELD)
        for node in graph["nodes"]:
            if node["id"] == "primary_clip_empty":
                node["kind"] = "event.weapon.trigger_released"
        messages = weapon_errors(graph)
        self.assertEqual(len(messages), 1, messages)
        self.assertIn("og.weapon.reload 'primary_reload' has no effect on event.weapon.trigger_released",
                      messages[0])

    def test_settings(self):
        for rounds in (0, 256, "two", 2.5, True):
            with self.subTest(rounds=rounds):
                graph = copy.deepcopy(V2_HELD)
                graph["nodes"][4]["rounds"] = rounds
                self.assertIn("needs rounds from 1 to 255", weapon_errors(graph)[0])
        graph = copy.deepcopy(V2_HELD)
        graph["nodes"][4]["rounds"] = "$blast_rounds"
        self.assertEqual(weapon_errors(graph), [])  # the game checks the variable
        graph = copy.deepcopy(V2_HELD)
        graph["nodes"].append({"id": "s", "kind": "og.weapon.set_state", "key": "armed", "value": "yes"})
        graph["edges"].append({"from": "primary_clip_empty", "output": "out", "to": "s", "input": "exec"})
        self.assertIn("needs value true or false", weapon_errors(graph)[0])
        graph = copy.deepcopy(V2_HELD)
        graph["nodes"].append({"id": "g", "kind": "gate.weapon.state_is", "equals": True})
        self.assertIn("needs a key", weapon_errors(graph)[0])
        graph = copy.deepcopy(V2_HELD)
        for k in range(9):
            graph["nodes"].append({"id": f"s{k}", "kind": "og.weapon.set_state", "key": f"k{k}", "value": True})
        self.assertIn("more than 8 state keys", weapon_errors(graph)[0])

    def test_edge_rules(self):
        graph = copy.deepcopy(V2_HELD)
        graph["edges"][3]["output"] = "blocked"
        self.assertIn("needs output pass, fail or failed", weapon_errors(graph)[0])
        graph = copy.deepcopy(V2_HELD)
        graph["edges"].append({"from": "primary_trigger", "output": "out", "to": "primary_attack", "input": "exec"})
        # The record's own event node is record data: no execution edge starts there.
        self.assertIn("names a missing node", weapon_errors(graph)[0])
        graph = copy.deepcopy(V2_HELD)
        graph["edges"].append({"from": "primary_attack", "output": "done", "to": "primary_reload", "input": "exec"})
        self.assertIn("output must be \"out\"", weapon_errors(graph)[0])

    def test_a_program_node_cannot_take_a_record_nodes_id(self):
        graph = copy.deepcopy(V2_HELD)
        for node in graph["nodes"]:
            if node["id"] == "primary_attack":
                node["id"] = "primary_trigger"
        for edge in graph["edges"]:
            for end in ("from", "to"):
                if edge.get(end) == "primary_attack":
                    edge[end] = "primary_trigger"
        self.assertIn("node id 'primary_trigger' is used twice", weapon_errors(graph)[0])

    def test_triggers_and_their_settings(self):
        """held-weapon-events.md slice 3: fire once, burst and automatic start
        the attack, use_ammo spends; each setting is a value of its kind (a
        "$name" is the game's to check), and one left out is the record's."""
        def with_attack(node: dict) -> dict:
            graph = copy.deepcopy(V2_HELD)
            for idx, old in enumerate(graph["nodes"]):
                if old["id"] == "primary_attack":
                    graph["nodes"][idx] = dict(node, id="primary_attack")
            return graph

        good = [
            {"kind": "og.weapon.burst", "count": 3, "gap": 3},
            {"kind": "og.weapon.burst"},
            {"kind": "og.weapon.automatic", "rate": 750, "start_rate": 60.5, "spin_up": 88, "spin_down": 0,
             "burst": 3, "aimed_burst": False},
            {"kind": "og.weapon.automatic", "rate": "$fire_rate"},
            {"kind": "og.weapon.fire_once"},
        ]
        for node in good:
            with self.subTest(node=node):
                self.assertEqual(weapon_errors(with_attack(node)), [])
        bad = [
            ({"kind": "og.weapon.burst", "count": 1}, "needs count from 2 to 50"),
            ({"kind": "og.weapon.burst", "count": 51}, "needs count from 2 to 50"),
            ({"kind": "og.weapon.burst", "count": "3"}, "needs count from 2 to 50"),
            ({"kind": "og.weapon.burst", "count": True}, "needs count from 2 to 50"),
            ({"kind": "og.weapon.burst", "gap": 256}, "needs gap from 0 to 255"),
            ({"kind": "og.weapon.burst", "gap": 2.5}, "needs gap from 0 to 255"),
            ({"kind": "og.weapon.automatic", "rate": 0}, "needs rate in rounds per minute"),
            ({"kind": "og.weapon.automatic", "rate": 3601}, "needs rate in rounds per minute"),
            ({"kind": "og.weapon.automatic", "start_rate": "fast"}, "needs start_rate in rounds per minute"),
            ({"kind": "og.weapon.automatic", "rate": True}, "needs rate in rounds per minute"),
            ({"kind": "og.weapon.automatic", "spin_up": 10001}, "needs spin_up from 0 to 10000"),
            ({"kind": "og.weapon.automatic", "burst": -1}, "needs burst from 0 to 255"),
            ({"kind": "og.weapon.automatic", "aimed_burst": "no"}, "needs aimed_burst true or false"),
        ]
        for node, why in bad:
            with self.subTest(node=node):
                messages = weapon_errors(with_attack(node))
                self.assertEqual(len(messages), 1, messages)
                self.assertIn(why, messages[0])
        for rounds in (0, 256, "1", 1.5, False):
            with self.subTest(rounds=rounds):
                graph = copy.deepcopy(V2_HELD)
                graph["nodes"][-1]["rounds"] = rounds
                self.assertIn("og.weapon.use_ammo 'primary_use_ammo' needs rounds from 1 to 255",
                              weapon_errors(graph)[0])
        # A trigger, or use_ammo, on a record event does nothing.
        graph = copy.deepcopy(V2_HELD)
        graph["nodes"].append({"id": "late", "kind": "event.weapon.trigger_released"})
        graph["nodes"].append({"id": "late_burst", "kind": "og.weapon.burst"})
        graph["edges"].append({"from": "late", "output": "out", "to": "late_burst", "input": "exec"})
        self.assertIn("og.weapon.burst 'late_burst' has no effect on event.weapon.trigger_released",
                      weapon_errors(graph)[0])

    def test_shots_fit_their_function_and_their_settings(self):
        """held-weapon-events.md slice 4: the shot module of each attack
        family, fitting only its function's record type, with settings of
        their kinds."""
        def with_shot(node: dict, function_type: str = "shoot_single") -> dict:
            graph = copy.deepcopy(V2_HELD)
            graph["nodes"][1]["params"]["function_type"] = function_type
            graph["nodes"].append(dict(node, id="primary_shot"))
            graph["edges"].append({"from": "primary_use_ammo", "output": "out", "to": "primary_shot",
                                   "input": "exec"})
            return graph

        good = [
            ({"kind": "og.weapon.hitscan", "pellets": 6, "through_walls": False, "shield_damage": 1}, "shoot_single"),
            ({"kind": "og.weapon.hitscan", "through_walls": True, "shield_damage": 10}, "shoot_automatic"),
            ({"kind": "og.weapon.beam", "range": 300}, "shoot_automatic"),
            ({"kind": "og.weapon.launch", "item": "base:rocket"}, "shoot_projectile"),
            ({"kind": "og.weapon.launch", "item": "base:bolt", "item_function": "gun"}, "shoot_projectile"),
            ({"kind": "og.weapon.launch", "item": "own", "item_function": 2}, "shoot_projectile"),
            ({"kind": "og.weapon.throw", "give_up_gun": True}, "throw"),
            ({"kind": "og.weapon.melee"}, "melee"),
            ({"kind": "og.weapon.special", "action": "detonate"}, "special"),
            ({"kind": "og.weapon.mode_toggle", "mode": "cloak", "drain": 24}, "special"),
            ({"kind": "og.weapon.hitscan", "pellets": "$pellets"}, "shoot_single"),
        ]
        for node, function_type in good:
            with self.subTest(node=node):
                self.assertEqual(weapon_errors(with_shot(node, function_type)), [])
        bad = [
            ({"kind": "og.weapon.launch", "item": "base:rocket"}, "shoot_single", "does not fit this function"),
            ({"kind": "og.weapon.hitscan"}, "throw", "does not fit this function"),
            ({"kind": "og.weapon.mode_toggle"}, "melee", "does not fit this function"),
            ({"kind": "og.weapon.hitscan", "pellets": 33}, "shoot_single", "needs pellets from 1 to 32"),
            ({"kind": "og.weapon.hitscan", "shield_damage": 0}, "shoot_single", "needs shield_damage above 0"),
            ({"kind": "og.weapon.hitscan", "through_walls": "yes"}, "shoot_single", "needs through_walls true or false"),
            ({"kind": "og.weapon.beam", "range": 0}, "shoot_single", "needs range from 1 to 65536"),
            ({"kind": "og.weapon.launch", "item": "Rocket"}, "shoot_projectile", "needs item own or an item's catalog id"),
            ({"kind": "og.weapon.launch", "item_function": 3}, "shoot_projectile", "needs item_function gun, 0, 1 or 2"),
            ({"kind": "og.weapon.special", "action": "cloak"}, "special", "needs action shoot, launch"),
            ({"kind": "og.weapon.mode_toggle", "mode": "x"}, "special", "needs mode cloak"),
            ({"kind": "og.weapon.mode_toggle", "drain": -1}, "special", "needs drain above 0"),
        ]
        for node, function_type, why in bad:
            with self.subTest(node=node, function_type=function_type):
                messages = weapon_errors(with_shot(node, function_type))
                self.assertEqual(len(messages), 1, messages)
                self.assertIn(why, messages[0])
        # A shot on a record event does nothing.
        graph = copy.deepcopy(V2_HELD)
        graph["nodes"].append({"id": "late", "kind": "event.weapon.reload_done"})
        graph["nodes"].append({"id": "late_shot", "kind": "og.weapon.hitscan"})
        graph["edges"].append({"from": "late", "output": "out", "to": "late_shot", "input": "exec"})
        self.assertIn("og.weapon.hitscan 'late_shot' has no effect on event.weapon.reload_done", weapon_errors(graph)[0])

    def test_the_kinds_agree_with_the_runner_and_the_table(self):
        runner = (REPO / "port" / "src" / "mission_graph_runner.cpp").read_text(encoding="utf-8")
        rules = (REPO / "port" / "include" / "weapon_rules.h").read_text(encoding="utf-8")
        for kind in conf.WEAPON_GRAPH_EVENTS:
            with self.subTest(event=kind):
                self.assertIn('{ "' + kind + '", MISSION_GRAPH_EVENT_WEAPON_', runner)
                self.assertIn('"' + kind + '"', rules)
        for kind in conf.WEAPON_GRAPH_MODULES:
            with self.subTest(module=kind):
                self.assertIn('"' + kind + '"', rules)
        for kind in conf.WEAPON_GRAPH_GATES:
            with self.subTest(gate=kind):
                self.assertIn('"' + kind + '"', rules)
        for kind, settings in conf.WEAPON_GRAPH_SETTINGS.items():
            self.assertIn(kind, conf.WEAPON_GRAPH_MODULES)
            for key in settings:
                with self.subTest(module=kind, setting=key):
                    self.assertIn('_PARAM "' + key + '"', rules)


if __name__ == "__main__":
    unittest.main()
