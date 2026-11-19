"""pd.vehicle_graph.v1: the conformance tool's rules for a vehicle's behavior
graph and its behavior/variables.json match the runner's compile rules
(port/src/vehicle_graph_runner.cpp, [vehicle-graph] in pd-tests), and a
.pdvehicle pairs a v1 graph with physics v3 and a v2 behavior with physics v2."""
from pathlib import Path
import copy
import io
import json
import sys
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from asset_archive_conformance import (  # noqa: E402
    validate_vehicle_graph_execution,
    validate_vehicle_source_contract,
    validate_vehicle_variables,
)

CATALOG = "mod:vehicle_maian_hover"

VARIABLES = {
    "schema": "pd.vehicle_variables.v1",
    "boost_speed": 7.0,
    "boost_ramp": {"value": 60, "unit": "ticks60"},
    "overheat_after": {"value": 240, "unit": "ticks60"},
    "overheated_boost": 0.5,
    "steer_limit": 0.5,
    "steer_limit_time": {"value": 45, "unit": "ticks60"},
    "recover_time_stopped": {"value": 120, "unit": "ticks60"},
    "recover_speed_scale": 2.0,
    "cool_crystal_part": 140,
}


def node(node_id, kind, **params):
    out = {"id": node_id, "kind": kind}
    if params:
        out["params"] = params
    return out


def edge(src, dst, output="out"):
    return {"from": src, "output": output, "to": dst, "input": "exec"}


def maian_graph():
    return {
        "schema": "pd.vehicle_graph.v1",
        "catalog_id": CATALOG,
        "nodes": [
            node("mount", "event.vehicle.mount_requested"),
            node("dismount", "event.vehicle.dismount_requested"),
            node("tick", "event.vehicle.tick"),
            node("heat", "event.vehicle.heat_changed"),
            node("driver_free", "gate.vehicle.seat_free", seat="driver"),
            node("mount_driver", "og.vehicle.mount_seat", seat="driver"),
            node("dismount_driver", "og.vehicle.dismount_seat", seat="driver"),
            node("drive", "og.vehicle.hover_drive"),
            node("boost", "og.vehicle.boost_heat", boost_speed="$boost_speed",
                 ramp_time="$boost_ramp", overheat_after="$overheat_after",
                 overheated_boost="$overheated_boost", steer_limit="$steer_limit",
                 steer_limit_time="$steer_limit_time",
                 recover_time_stopped="$recover_time_stopped",
                 recover_speed_scale="$recover_speed_scale"),
            node("crystals_black", "gate.vehicle.heat_phase", phases=["overheated", "recovering"]),
            node("hide_seafoam", "og.vehicle.part_visible", part="$cool_crystal_part", visible=False),
            node("show_seafoam", "og.vehicle.part_visible", part="$cool_crystal_part", visible=True),
            node("turret_blocked", "gate.vehicle.heat_phase",
                 phases=["full", "overheated", "recovering"]),
            node("lock_turret", "og.vehicle.turret_lockout", reason="heat", locked=True),
            node("unlock_turret", "og.vehicle.turret_lockout", reason="heat", locked=False),
        ],
        "edges": [
            edge("mount", "driver_free"),
            edge("driver_free", "mount_driver", "pass"),
            edge("dismount", "dismount_driver"),
            edge("tick", "drive"),
            edge("tick", "boost"),
            edge("heat", "crystals_black"),
            edge("crystals_black", "hide_seafoam", "pass"),
            edge("crystals_black", "show_seafoam", "fail"),
            edge("heat", "turret_blocked"),
            edge("turret_blocked", "lock_turret", "pass"),
            edge("turret_blocked", "unlock_turret", "fail"),
        ],
    }


def base_graph():
    return {
        "schema": "pd.vehicle_graph.v1",
        "catalog_id": "base:vehicle_hoverbike",
        "nodes": [
            node("mount", "event.vehicle.mount_requested"),
            node("dismount", "event.vehicle.dismount_requested"),
            node("tick", "event.vehicle.tick"),
            node("driver_free", "gate.vehicle.seat_free", seat="driver"),
            node("mount_driver", "og.vehicle.mount_seat", seat="driver"),
            node("dismount_driver", "og.vehicle.dismount_seat", seat="driver"),
            node("drive", "og.vehicle.hover_drive"),
            node("throttle_boost", "og.vehicle.throttle_boost", boost_speed=5.0,
                 ramp_time={"value": 2400, "unit": "ticks240"}),
        ],
        "edges": [
            edge("mount", "driver_free"),
            edge("driver_free", "mount_driver", "pass"),
            edge("dismount", "dismount_driver"),
            edge("tick", "drive"),
            edge("tick", "throttle_boost"),
        ],
    }


def two_seat_graph():
    """Each seat mounts when it is free, either seat dismounts, and a rider
    may switch to the other seat when it is free (kTwoSeatGraph in
    tests/test_vehicle_graph_runner.cpp)."""
    return {
        "schema": "pd.vehicle_graph.v1",
        "catalog_id": CATALOG,
        "nodes": [
            node("mount", "event.vehicle.mount_requested"),
            node("dismount", "event.vehicle.dismount_requested"),
            node("switch", "event.vehicle.switch_seat_requested"),
            node("tick", "event.vehicle.tick"),
            node("mount_wants_driver", "gate.vehicle.seat", seat="driver"),
            node("driver_free", "gate.vehicle.seat_free", seat="driver"),
            node("gunner_free", "gate.vehicle.seat_free", seat="gunner"),
            node("mount_driver", "og.vehicle.mount_seat", seat="driver"),
            node("mount_gunner", "og.vehicle.mount_seat", seat="gunner"),
            node("leaves_driver", "gate.vehicle.seat", seat="driver"),
            node("dismount_driver", "og.vehicle.dismount_seat", seat="driver"),
            node("dismount_gunner", "og.vehicle.dismount_seat", seat="gunner"),
            node("switch_wants_gunner", "gate.vehicle.seat", seat="gunner"),
            node("gunner_free_for_switch", "gate.vehicle.seat_free", seat="gunner"),
            node("driver_free_for_switch", "gate.vehicle.seat_free", seat="driver"),
            node("switch_to_gunner", "og.vehicle.switch_seat", seat="gunner"),
            node("switch_to_driver", "og.vehicle.switch_seat", seat="driver"),
            node("drive", "og.vehicle.hover_drive"),
        ],
        "edges": [
            edge("mount", "mount_wants_driver"),
            edge("mount_wants_driver", "driver_free", "pass"),
            edge("mount_wants_driver", "gunner_free", "fail"),
            edge("driver_free", "mount_driver", "pass"),
            edge("gunner_free", "mount_gunner", "pass"),
            edge("dismount", "leaves_driver"),
            edge("leaves_driver", "dismount_driver", "pass"),
            edge("leaves_driver", "dismount_gunner", "fail"),
            edge("switch", "switch_wants_gunner"),
            edge("switch_wants_gunner", "gunner_free_for_switch", "pass"),
            edge("switch_wants_gunner", "driver_free_for_switch", "fail"),
            edge("gunner_free_for_switch", "switch_to_gunner", "pass"),
            edge("driver_free_for_switch", "switch_to_driver", "pass"),
            edge("tick", "drive"),
        ],
    }


def graph_errors(graph, variables=VARIABLES, catalog=CATALOG):
    var_errors = []
    parsed = validate_vehicle_variables("fixture", variables, var_errors) if variables else None
    return var_errors + validate_vehicle_graph_execution("fixture", graph, parsed, catalog)


def find(graph, node_id):
    return next(n for n in graph["nodes"] if n["id"] == node_id)


class VehicleGraphConformanceTests(unittest.TestCase):
    def test_the_maian_and_base_graphs_pass(self):
        self.assertEqual(graph_errors(maian_graph()), [])
        self.assertEqual(graph_errors(base_graph(), None, "base:vehicle_hoverbike"), [])

    def test_comments_change_nothing(self):
        # A graph, a node, an edge and a variables file may each carry a comment.
        graph = maian_graph()
        graph["comment"] = "what this vehicle does"
        for n in graph["nodes"]:
            n["comment"] = "why " + n["id"]
        for e in graph["edges"]:
            e["comment"] = "then"
        variables = dict(VARIABLES, comment="the numbers a modder tunes")
        self.assertEqual(graph_errors(graph, variables), [])
        # In a variables file it is not a variable.
        errors = []
        parsed = validate_vehicle_variables("x", variables, errors)
        self.assertEqual(errors, [])
        self.assertNotIn("comment", parsed)
        errors = []
        validate_vehicle_variables(
            "x", {"schema": "pd.vehicle_variables.v1", "comment": 3}, errors)
        self.assertTrue(any("comment must be a string" in e for e in errors), errors)

    def test_the_maian_files_in_dev_mods_pass(self):
        # The recipe's files name the template's catalog id; the in-game mod author rewrites it
        # to the mod's id in every text member it writes (cloneRewriteText), overrides included.
        folder = Path(__file__).resolve().parents[1] / "dev-mods" / "maian_hover_vehicle"
        text = (folder / "behavior.graph.json").read_text("utf-8")
        self.assertIn('"catalog_id": "example:tri_vehicle"', text)
        graph = json.loads(text.replace("example:tri_vehicle", CATALOG))
        variables = json.loads((folder / "behavior" / "variables.json").read_text("utf-8"))
        self.assertEqual(graph_errors(graph, variables), [])

    def test_broken_graphs_are_refused_like_the_runner(self):
        def edit(fn):
            graph = maian_graph()
            fn(graph)
            return graph

        cases = [
            ("schema", edit(lambda g: g.update(schema="pd2.vehicle.behavior.v2")),
             "must declare schema"),
            ("catalog", edit(lambda g: g.update(catalog_id="mod:other")), "catalog_id must match"),
            ("module", edit(lambda g: find(g, "drive").update(kind="og.vehicle.fly")),
             "native module 'og.vehicle.fly' does not exist"),
            ("event", edit(lambda g: find(g, "tick").update(kind="event.vehicle.honk")),
             "is not a vehicle event"),
            ("gate", edit(lambda g: find(g, "driver_free").update(kind="gate.vehicle.empty")),
             "is not executable"),
            ("seat", edit(lambda g: find(g, "driver_free")["params"].update(seat="pilot")),
             "must be \"driver\" or \"gunner\""),
            ("unknown param", edit(lambda g: find(g, "drive").update(params={"turbo": True})),
             "has no param 'turbo'"),
            ("missing param", edit(lambda g: find(g, "lock_turret")["params"].pop("locked")),
             "param 'locked' is missing"),
            ("unknown variable", edit(lambda g: find(g, "boost")["params"].update(ramp_time="$ramp")),
             "uses unknown variable '$ramp'"),
            ("phase", edit(lambda g: find(g, "crystals_black")["params"].update(phases=["black"])),
             "may name only"),
            ("twice", edit(lambda g: find(g, "crystals_black")["params"].update(
                phases=["full", "full"])), "names a phase twice"),
            ("reason", edit(lambda g: find(g, "lock_turret")["params"].update(reason="Heat!")),
             "must be 1 to 31"),
            ("duplicate id", edit(lambda g: find(g, "drive").update(id="tick")), "is used twice"),
            ("to event", edit(lambda g: g["edges"][0].update(to="tick")),
             "which is not a gate or a native module"),
            ("gate output", edit(lambda g: g["edges"][1].update(output="out")),
             "needs output pass or fail"),
            ("module output", edit(lambda g: g["edges"][3].update(output="pass")),
             "output must be \"out\""),
            ("no input", edit(lambda g: g["edges"][3].pop("input")),
             "has an output but is not an execution edge"),
            ("missing node", edit(lambda g: g["edges"][2].update(to="ghost")),
             "names a missing node"),
            ("drive on mount", edit(lambda g: g["edges"][1].update(to="drive")),
             "og.vehicle.hover_drive 'drive' cannot run on event.vehicle.mount_requested"),
            ("mount on tick", edit(lambda g: g["edges"][3].update(to="mount_driver")),
             "cannot run on event.vehicle.tick"),
            ("two drives", edit(lambda g: g["nodes"].append(node("drive2", "og.vehicle.hover_drive"))),
             "has one og.vehicle.hover_drive"),
            ("two heats", edit(lambda g: g["nodes"].append(
                dict(copy.deepcopy(find(g, "boost")), id="boost2"))),
             "has one og.vehicle.boost_heat"),
            ("graph comment", edit(lambda g: g.update(comment=7)),
             "behavior.graph.json comment must be a string"),
            ("node comment", edit(lambda g: find(g, "drive").update(comment=["a"])),
             "node 'drive' comment must be a string"),
            ("edge comment", edit(lambda g: g["edges"][0].update(comment=True)),
             "edge 0 comment must be a string"),
            ("two dismounts", edit(lambda g: (
                g["nodes"].append(node("dismount_again", "og.vehicle.dismount_seat", seat="driver")),
                g["edges"].append(edge("dismount", "dismount_again")))),
             "event.vehicle.dismount_requested reaches a second og.vehicle.dismount_seat for the "
             "driver seat"),
            ("two mounts on a gate's branches", edit(lambda g: (
                g["nodes"].append(node("mount_again", "og.vehicle.mount_seat", seat="driver")),
                g["edges"].append(edge("driver_free", "mount_again", "fail")))),
             "event.vehicle.mount_requested reaches a second og.vehicle.mount_seat for the "
             "driver seat"),
        ]
        for name, graph, expected in cases:
            with self.subTest(name=name):
                errors = graph_errors(graph)
                self.assertTrue(any(expected in e for e in errors), errors)

    def test_a_two_seat_graph_with_a_seat_switch_passes(self):
        self.assertEqual(graph_errors(two_seat_graph(), None), [])
        # A part or a lockout may run on a switch, as on any event.
        graph = two_seat_graph()
        graph["nodes"].append(node("lock", "og.vehicle.turret_lockout", reason="switching",
                                   locked=True))
        graph["edges"].append(edge("switch_to_driver", "lock"))
        self.assertEqual(graph_errors(graph, None), [])

    def test_seat_modules_run_only_on_their_own_request_once_per_seat(self):
        def edit(fn):
            graph = two_seat_graph()
            fn(graph)
            return graph

        def retarget(graph, src, dst, new_dst):
            for e in graph["edges"]:
                if e["from"] == src and e["to"] == dst:
                    e["to"] = new_dst
                    return
            raise AssertionError(f"no edge {src} -> {dst}")

        cases = [
            ("switch on mount", edit(lambda g: retarget(g, "gunner_free", "mount_gunner",
                                                        "switch_to_gunner")),
             "og.vehicle.switch_seat 'switch_to_gunner' cannot run on "
             "event.vehicle.mount_requested"),
            ("mount on switch", edit(lambda g: retarget(g, "gunner_free_for_switch",
                                                        "switch_to_gunner", "mount_gunner")),
             "og.vehicle.mount_seat 'mount_gunner' cannot run on "
             "event.vehicle.switch_seat_requested"),
            ("drive on switch", edit(lambda g: retarget(g, "driver_free_for_switch",
                                                        "switch_to_driver", "drive")),
             "og.vehicle.hover_drive 'drive' cannot run on event.vehicle.switch_seat_requested"),
            ("two switches on a gate's branches", edit(lambda g: (
                g["nodes"].append(node("switch_again", "og.vehicle.switch_seat", seat="driver")),
                g["edges"].append(edge("driver_free_for_switch", "switch_again", "fail")))),
             "event.vehicle.switch_seat_requested reaches a second og.vehicle.switch_seat for "
             "the driver seat"),
            ("seat name", edit(lambda g: find(g, "switch_to_gunner")["params"].update(
                seat="turret")), "must be \"driver\" or \"gunner\""),
            ("no seat", edit(lambda g: find(g, "switch_to_gunner").pop("params")),
             "param 'seat' is missing"),
        ]
        for name, graph, expected in cases:
            with self.subTest(name=name):
                errors = graph_errors(graph, None)
                self.assertTrue(any(expected in e for e in errors), errors)

    def test_variables_take_values_whole_with_the_runner_types(self):
        cases = [
            ("boost_speed", "fast", "param 'boost_speed' must be a number"),
            ("boost_ramp", 60, "param 'ramp_time' must be {"),
            ("boost_ramp", {"value": 0, "unit": "ticks60"}, "must be longer than 0"),
            ("overheated_boost", 1.5, "must be from 0 to 1"),
            ("recover_speed_scale", 0, "must be above 0"),
            ("cool_crystal_part", 140.5, "must be a whole number"),
            ("cool_crystal_part", -3, "must be from 0 to 32767"),
            ("overheat_after", {"value": 4, "unit": "seconds"}, "variable 'overheat_after' must be"),
        ]
        for key, value, expected in cases:
            with self.subTest(key=key, value=value):
                variables = copy.deepcopy(VARIABLES)
                variables[key] = value
                errors = graph_errors(maian_graph(), variables)
                self.assertTrue(any(expected in e for e in errors), errors)
        # A zero steering-limit time is fine; other names are allowed.
        variables = copy.deepcopy(VARIABLES)
        variables["steer_limit_time"] = {"value": 0, "unit": "ticks60"}
        variables["turret_regrow"] = {"value": 30, "unit": "ticks60"}
        self.assertEqual(graph_errors(maian_graph(), variables), [])

    def test_the_variables_file_rules(self):
        errors = []
        self.assertIsNone(validate_vehicle_variables("x", [], errors))
        self.assertTrue(errors)
        errors = []
        self.assertIsNone(validate_vehicle_variables(
            "x", {"schema": "pd.vehicle_variables.v2"}, errors))
        self.assertTrue(errors)
        errors = []
        validate_vehicle_variables("x", {"schema": "pd.vehicle_variables.v1", "Bad": 1}, errors)
        self.assertTrue(any("variable name 'Bad'" in e for e in errors), errors)
        # A catalog_id must be the vehicle's, as the runner refuses.
        errors = []
        validate_vehicle_variables(
            "x", {"schema": "pd.vehicle_variables.v1", "catalog_id": "mod:other"}, errors, CATALOG)
        self.assertTrue(any("is for 'mod:other', not" in e for e in errors), errors)
        errors = []
        validate_vehicle_variables(
            "x", {"schema": "pd.vehicle_variables.v1", "catalog_id": CATALOG}, errors, CATALOG)
        self.assertEqual(errors, [])


def pdvehicle(physics_schema, behavior, boost_keys, variables=None):
    """A minimal .pdvehicle zip for the source contract."""
    physics = {
        "schema": physics_schema, "catalog_id": CATALOG, "archetype": "hoverbike",
        "hover": [82, 1, 3, 0.0025, 0.1, 0.01, 0.02, 0.00002, 0.0006, 0.01, 0.02, 0.00002, 0.0006],
    }
    for key in ("turn_input_scale", "reverse_turn_gain", "steering_response_ntsc",
                "steering_response_pal", "turn_visual_scale", "input_response",
                "forward_input_scale", "lateral_input_scale", "lean_response", "forward_base",
                "forward_accel_gain", "reverse_base", "drag_ntsc", "drag_pal", "forward_thrust",
                "lateral_thrust", "forward_tilt", "lateral_tilt", "tilt_response_ntsc",
                "tilt_response_pal", "yaw_response_ntsc", "yaw_response_pal"):
        physics[key] = 0.5
    if boost_keys:
        physics["boost_speed"] = 5.0
        physics["boost_time_ticks60"] = 2400
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as zf:
        zf.writestr("vehicle.ini", f"[vehicle]\ncatalog_id = {CATALOG}\nname = X\n"
                    "model_file = model.gltf\nphysics_file = physics.json\n"
                    "behavior_graph = behavior.graph.json\n")
        zf.writestr("model.gltf", "{}")
        zf.writestr("physics.json", json.dumps(physics))
        zf.writestr("behavior.graph.json", json.dumps(behavior))
        if variables is not None:
            zf.writestr("behavior/variables.json", json.dumps(variables))
    buf.seek(0)
    return zipfile.ZipFile(buf)


def contract_errors(zf):
    errors = []
    validate_vehicle_source_contract("fixture", zf, set(zf.namelist()), errors)
    return errors


V2_BEHAVIOR = {"schema": "pd2.vehicle.behavior.v2", "catalog_id": CATALOG,
               "allow_mount": True, "allow_drive": True, "allow_dismount": True}


class VehicleSourcePairingTests(unittest.TestCase):
    def test_v1_with_physics_v3_passes_and_v2_is_retired(self):
        self.assertEqual(contract_errors(pdvehicle(
            "pd2.vehicle.physics.v3", maian_graph(), False, VARIABLES)), [])
        errors = contract_errors(pdvehicle("pd2.vehicle.physics.v2", V2_BEHAVIOR, True))
        self.assertTrue(any("pd2.vehicle.behavior.v2 and pd2.vehicle.physics.v2 are retired" in e
                            for e in errors), errors)

    def test_mixed_sources_are_refused(self):
        cases = [
            (pdvehicle("pd2.vehicle.physics.v2", maian_graph(), True, VARIABLES),
             "must declare schema pd2.vehicle.physics.v3"),
            (pdvehicle("pd2.vehicle.physics.v3", V2_BEHAVIOR, False),
             "pd2.vehicle.behavior.v2 and pd2.vehicle.physics.v2 are retired"),
            (pdvehicle("pd2.vehicle.physics.v3", maian_graph(), True, VARIABLES),
             "has no boost keys"),
            (pdvehicle("pd2.vehicle.physics.v2", V2_BEHAVIOR, True, VARIABLES),
             "is read only with a pd.vehicle_graph.v1"),
            (pdvehicle("pd2.vehicle.physics.v2", V2_BEHAVIOR, True),
             "must declare schema pd2.vehicle.physics.v3"),
        ]
        for zf, expected in cases:
            with self.subTest(expected=expected):
                errors = contract_errors(zf)
                self.assertTrue(any(expected in e for e in errors), errors)


if __name__ == "__main__":
    unittest.main()
