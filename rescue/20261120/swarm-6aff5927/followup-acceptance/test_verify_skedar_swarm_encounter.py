"""Synthetic tests for the offline Skedar encounter trace contract."""
from copy import deepcopy
import io
import json
from pathlib import Path
import unittest

from verify_skedar_swarm_encounter import ContractError, MAX_JSON_DEPTH, load_json, verify

MATRIX_PATH = Path(__file__).with_name("fixtures") / "skedar_swarm_encounter_acceptance.json"
MATRIX = load_json(MATRIX_PATH)
EPOCH = 17


def trace():
    events = []

    def event(event_type, tick, **fields):
        events.append(dict(type=event_type, tick=tick, stage_epoch=EPOCH, **fields))

    def spawn(actor, tick, tunnel, wave):
        event("raider_spawn", tick, actor=actor, generation=1, wave=wave,
              tunnel=tunnel, radius=15, height=80, scale=0.5)

    def passage(actor, tick, tunnel, blocked=False):
        event("tunnel_passage", tick, actor=actor, generation=1, tunnel=tunnel,
              portal=f"portal-{tunnel}", start_side=-80, requested_side=80,
              episode_start_tick=tick, sweep_count=1, start_axis_dot=0,
              requested_axis_dot=0, resolved_axis_dot=0,
              resolved_side=-15 if blocked else 80,
              hit_blocker=f"gate-{tunnel}" if blocked else None)

    def target_witness(tick):
        return dict(target="player", target_generation=2, target_check_tick=tick,
                    target_alive=True, line_of_sight=True, hostile=True, distance=33,
                    validated_reach=50)

    def start_attack(actor, tick, attack_id, kind, **fields):
        event("attack_start", tick, actor=actor, generation=1, attack_id=attack_id,
              kind=kind, **target_witness(tick), **fields)

    def hit(actor, tick, attack_id):
        event("attack_hit", tick, actor=actor, generation=1, attack_id=attack_id,
              amount=4, **target_witness(tick))

    event("encounter_start", 0, level_id="original:skedar_defense",
          scenario_id="original:tunnel_scenario", authority="local")
    event("player_spawn", 0, player="player", player_generation=2)
    event("wave_start", 0, wave="scouts")
    spawn("scout", 0, "north", "scouts")
    event("actor_death", 1, actor="scout", generation=1, cause="player_weapon")
    event("wave_end", 1, wave="scouts", reason="finished")
    event("wave_start", 2, wave="assault")
    spawn("raider-1", 2, "north", "assault")
    spawn("raider-2", 3, "center", "assault")
    spawn("raider-3", 5, "east", "assault")
    passage("raider-1", 6, "north")
    passage("raider-2", 6, "center")
    passage("raider-3", 6, "east")
    start_attack("raider-1", 7, 1, "jump")
    start_attack("raider-2", 7, 2, "drop", launch_support="ceiling")
    hit("raider-1", 9, 1)
    event("attack_end", 10, actor="raider-1", generation=1, attack_id=1, result="landed")
    event("attack_end", 10, actor="raider-2", generation=1, attack_id=2, result="landed")
    for index, tunnel in enumerate(("north", "center", "east"), 1):
        tick = 10 + index
        event("tunnel_close", tick, tunnel=tunnel, portal=f"portal-{tunnel}",
              blocker=f"gate-{tunnel}", source="player_interact", collision_enabled=True,
              player="player", player_generation=2)
        passage(f"raider-{index}", tick, tunnel, blocked=True)
    event("wave_end", 14, wave="assault", reason="all_tunnels_closed")
    for index in range(1, 4):
        event("actor_death", 14 + index, actor=f"raider-{index}", generation=1, cause="player_weapon")
    event("finale_start", 18)
    event("giant_spawn", 19, actor="giant", generation=1, body_ref="base:sp_body_147", spawn_pad="giant-pad", room="arena",
          radius=50, height=240, scale=2.0, clearance_radius=60, clearance_height=290, health=50)
    start_attack("giant", 20, 3, "melee")
    event("giant_damage", 21, actor="giant", generation=1, source="player_weapon", player="player", player_generation=2,
          amount=30, remaining_health=20)
    hit("giant", 22, 3)
    event("attack_end", 23, actor="giant", generation=1, attack_id=3, result="resolved")
    event("giant_damage", 24, actor="giant", generation=1, source="player_weapon", player="player", player_generation=2,
          amount=21, remaining_health=0)
    event("actor_death", 25, actor="giant", generation=1, cause="player_weapon")
    event("encounter_complete", 26, result="victory")
    result = {
        "schema": 1,
        "origin": "synthetic",
        "level_id": "original:skedar_defense",
        "scenario_id": "original:tunnel_scenario",
        "stage_epoch": EPOCH,
        "events": events,
        "plan": {
            "tunnels": [dict(id=name, portal=f"portal-{name}", blocker=f"gate-{name}")
                        for name in ("north", "center", "east")],
            "waves": [
                dict(id="scouts", max_spawns=1, tunnels=["north"], delay_ticks=0),
                dict(id="assault", max_spawns=4, tunnels=["north", "center", "east"], delay_ticks=1),
            ],
            "limits": {
                "max_alive_raiders": 3,
                "spawn_interval_ticks": 1,
                "max_active_attacks": 2,
                "max_attack_starts_per_tick": 2,
                "max_airborne_attacks": 2,
                "max_melee": 1,
                "max_jumps": 1,
                "max_drops": 1,
                "max_attack_ticks": 8,
                "min_attack_cooldown_ticks": 9,
                "min_telegraph_ticks": 2,
            },
            "raider_attack_reach": {"melee": 50, "jump": 50, "drop": 50},
            "giant": {"body_ref": "base:sp_body_147", "spawn_pad": "giant-pad", "room": "arena",
                      "attack_reach": {"melee": 65}},
        },
    }
    return resequence(result)


def resequence(result):
    for number, event in enumerate(result["events"], 1):
        event["seq"] = number
    return result


def select(result, kind, occurrence=0):
    return [event for event in result["events"] if event["type"] == kind][occurrence]


def remove(result, kind, occurrence=0):
    result["events"].remove(select(result, kind, occurrence))
    return resequence(result)


def after(result, anchor, event):
    result["events"].insert(result["events"].index(anchor) + 1, event)
    return resequence(result)


def candidate(result):
    result["origin"] = "native_candidate"
    result["provenance"] = {
        **{f"{name}_sha256": "0" * 64 for name in (
            "source_tree", "client", "public_level", "public_scenario", "fixture",
            "native_log", "managed_recipe")},
        "run_id": "synthetic-provenance-shape",
        "queue_id": "q_synthetic",
    }
    return result


class BoundedInput:
    def __init__(self, payload):
        self.payload = payload

    def open(self, mode):
        assert mode == "rb"
        return io.BytesIO(self.payload)


class EncounterContractTests(unittest.TestCase):
    def reject(self, reason, mutate):
        result = trace()
        mutate(result)
        with self.assertRaisesRegex(ContractError, reason):
            verify(result, MATRIX)

    def test_complete_replays_only_semantics(self):
        verified = verify(trace(), MATRIX)
        self.assertEqual(verified["verdict"], "contract_pass")
        self.assertEqual(verified["native_qualification"], "unverified")
        self.assertFalse(verified["native_gameplay_verified"])
        self.assertEqual(verified["trace_summary"]["airborne_landings"], ["drop", "jump"])
        self.assertEqual(verified["trace_summary"]["waves_finished"], 2)
        self.assertEqual(set(verified["trace_summary"]["tunnels_closed_blocked"]), {"north", "center", "east"})
        self.assertEqual(sum(row["verdict"] == "requires_native_evidence" for row in verified["cases"]), 10)

    def test_synthetic_cannot_enter_native_route(self):
        with self.assertRaisesRegex(ContractError, "synthetic trace"):
            verify(trace(), MATRIX, native_candidate=True)

    def test_native_shape_never_certifies_native_run(self):
        result = verify(candidate(trace()), MATRIX, native_candidate=True)
        self.assertEqual(result["native_qualification"], "unverified")
        self.assertFalse(result["native_gameplay_verified"])

    def test_native_shape_without_receipts_rejected(self):
        result = trace()
        result["origin"] = "native_candidate"
        with self.assertRaisesRegex(ContractError, "native provenance"):
            verify(result, MATRIX)

    def test_matrix_cannot_omit_native_case(self):
        copied = deepcopy(MATRIX)
        copied["cases"].pop()
        with self.assertRaisesRegex(ContractError, "acceptance cases"):
            verify(trace(), copied)

    def test_same_blocker_cannot_close_distinct_tunnels(self):
        def duplicate(result):
            rows = result["plan"]["tunnels"]
            rows[1]["blocker"] = rows[0]["blocker"]
        self.reject("duplicate blocker", duplicate)

    def test_wrong_portal_rejected(self):
        self.reject("portal differs", lambda result: select(result, "tunnel_close").update(portal="portal-east"))

    def test_direct_objective_shortcut_rejected(self):
        self.reject("normal player input", lambda result: select(result, "tunnel_close").update(source="use_door"))

    def test_closed_route_body_penetration_rejected(self):
        self.reject("admitted a body", lambda result: select(result, "tunnel_passage", 3).update(resolved_side=-14))

    def test_closed_route_wrong_collider_rejected(self):
        self.reject("blocker collision", lambda result: select(result, "tunnel_passage", 3).update(hit_blocker="gate-east"))

    def test_undersized_open_crossing_rejected(self):
        self.reject("open tunnel", lambda result: select(result, "tunnel_passage").update(resolved_side=14))

    def test_missing_blocked_route_witness(self):
        self.reject("missing blocked", lambda result: remove(result, "tunnel_passage", 3))

    def test_wave_cannot_spawn_through_closed_tunnel(self):
        def append_spawn(result):
            close = select(result, "tunnel_close")
            spawn = dict(select(result, "raider_spawn", 2), tick=close["tick"], tunnel="north",
                         actor="unexpected-raider")
            after(result, close, spawn)
        self.reject("closed or unknown", append_spawn)

    def test_wave_cannot_spawn_worker_on_unassigned_route(self):
        self.reject("outside the authored wave", lambda result: select(result, "raider_spawn").update(tunnel="center"))

    def test_spawn_budget_is_checked(self):
        self.reject("wave spawn budget", lambda result: result["plan"]["waves"][1].update(max_spawns=2))

    def test_spawn_spacing_is_checked(self):
        self.reject("spawn interval", lambda result: select(result, "raider_spawn", 2).update(tick=2))

    def test_wave_cannot_advance_before_drain(self):
        self.reject("spawn budget and raider drain", lambda result: remove(result, "actor_death"))

    def test_authored_wave_delay_is_checked(self):
        self.reject("wave delay", lambda result: result["plan"]["waves"][1].update(delay_ticks=2))

    def test_population_bound_is_checked(self):
        self.reject("raider limit", lambda result: result["plan"]["limits"].update(max_alive_raiders=2))

    def test_early_finale_cannot_skip_surviving_raiders(self):
        def move_finale(result):
            finale = select(result, "finale_start")
            result["events"].remove(finale)
            after(result, select(result, "wave_end", 1), dict(finale, tick=14))
        self.reject("raider drain", move_finale)

    def test_duplicate_giant_rejected(self):
        def duplicate(result):
            giant = select(result, "giant_spawn")
            after(result, giant, dict(giant, actor="second-giant"))
        self.reject("more than once", duplicate)

    def test_closed_wall_aligned_capsule_needs_its_full_projected_extent(self):
        self.reject("closed tunnel", lambda result: select(result, "tunnel_passage", 3).update(
            resolved_axis_dot=1, resolved_side=-20))

    def test_open_crossing_can_span_multiple_native_sweeps(self):
        result = trace()
        select(result, "tunnel_passage").update(episode_start_tick=2, sweep_count=5,
                                             resolved_axis_dot=1, resolved_side=60)
        self.assertEqual(verify(result, MATRIX)["verdict"], "contract_pass")

    def test_closed_attempt_must_start_after_the_blocker_commits(self):
        self.reject("before closure", lambda result: select(result, "tunnel_passage", 3).update(
            episode_start_tick=10, sweep_count=2))

    def test_impact_rejects_the_player_generation_after_independent_death(self):
        def retire(result):
            after(result, select(result, "attack_start", 1), dict(type="player_death",
                  tick=8, stage_epoch=EPOCH, player="player", player_generation=2))
        self.reject("player generation is absent or stale", retire)

    def test_giant_spawn_must_match_authored_pad_and_room(self):
        for change in ({"room": "different-room"}, {"spawn_pad": "different-pad"}):
            with self.subTest(change=change):
                self.reject("spawn differs", lambda result: select(result, "giant_spawn").update(change))

    def test_attack_cannot_inflate_reach_past_its_authored_kind_and_role(self):
        self.reject("authored attack limit", lambda result: select(result, "attack_hit").update(
            validated_reach=500, distance=400))
        self.reject("authored attack limit", lambda result: select(result, "attack_start", 2).update(
            kind="jump"))

    def test_a_wall_drop_alone_does_not_prove_a_ceiling_drop(self):
        self.reject("completed jump or ceiling drop", lambda result: select(result, "attack_start", 1).update(
            launch_support="wall"))

    def test_giant_visual_only_scaling_rejected(self):
        self.reject("collider and model scale", lambda result: select(result, "giant_spawn").update(radius=15))

    def test_giant_must_fit_at_spawn(self):
        self.reject("spawn clearance", lambda result: select(result, "giant_spawn").update(clearance_height=200))

    def test_giant_damage_shortcut_rejected(self):
        self.reject("bypassed live player combat", lambda result: select(result, "giant_damage").update(source="debug_kill"))

    def test_giant_health_must_follow_damage(self):
        self.reject("health does not match", lambda result: select(result, "giant_damage").update(remaining_health=1))

    def test_attack_checks_target_generation_age_range_and_los(self):
        variants = [
            ({"generation": 2}, "stale or absent actor"),
            ({"target_generation": 3}, "stale or different target"),
            ({"target_check_tick": 8}, "fresh live hostile"),
            ({"target_alive": False}, "fresh live hostile"),
            ({"line_of_sight": False}, "fresh live hostile"),
            ({"hostile": False}, "fresh live hostile"),
            ({"distance": 51}, "validated reach"),
        ]
        for changes, reason in variants:
            with self.subTest(changes=changes):
                self.reject(reason, lambda result: select(result, "attack_hit").update(changes))

    def test_duplicate_hit_rejected(self):
        def duplicate(result):
            hit = select(result, "attack_hit")
            after(result, hit, dict(hit))
        self.reject("duplicate damage", duplicate)

    def test_active_attack_cap_is_checked(self):
        self.reject("active attack limit", lambda result: result["plan"]["limits"].update(
            max_active_attacks=1, max_airborne_attacks=1, max_attack_starts_per_tick=1))

    def test_attack_starts_per_tick_are_bounded(self):
        self.reject("per-tick attack start limit", lambda result: result["plan"]["limits"].update(
            max_attack_starts_per_tick=1))

    def test_attack_needs_a_fresh_target_at_begin(self):
        self.reject("fresh live hostile", lambda result: select(result, "attack_start").update(target_check_tick=6))

    def test_airborne_and_kind_caps_are_checked(self):
        self.reject("airborne attack limit", lambda result: result["plan"]["limits"].update(max_airborne_attacks=1))
        self.reject("concurrent jump limit", lambda result: select(result, "attack_start", 1).update(kind="jump"))

    def test_cooldown_between_attacks_is_checked(self):
        def repeat(result):
            end = select(result, "attack_end")
            after(result, end, dict(select(result, "attack_start"), attack_id=90, tick=end["tick"],
                                    target_check_tick=end["tick"]))
        self.reject("cooldown violated", repeat)

    def test_attack_lifetime_is_checked(self):
        def late_end(result):
            result["plan"]["limits"]["max_attack_ticks"] = 3
            select(result, "attack_end", 0)["tick"] = 11
            select(result, "attack_end", 1)["tick"] = 11
        self.reject("duration limit", late_end)

    def test_expired_attack_cannot_commit_damage(self):
        def late_hit(result):
            result["plan"]["limits"]["max_attack_ticks"] = 3
            select(result, "attack_hit").update(tick=10, target_check_tick=10)
        self.reject("impact exceeded its duration", late_hit)

    def test_telegraph_required_at_damage(self):
        def change_tick(result):
            select(result, "attack_hit").update(tick=8, target_check_tick=8)
        self.reject("before its configured telegraph", change_tick)

    def test_stale_epoch_rejected(self):
        self.reject("stale stage epoch", lambda result: select(result, "tunnel_close").update(stage_epoch=16))

    def test_event_order_and_end_are_checked(self):
        self.reject("missing or reordered", lambda result: select(result, "tunnel_close").update(seq=700))
        self.reject("terminal encounter result", lambda result: remove(result, "encounter_complete"))

    def test_loader_rejects_duplicates_and_non_json_constants(self):
        with self.assertRaisesRegex(ContractError, "duplicate JSON field"):
            load_json(BoundedInput(b'{"duplicate": 1, "duplicate": 2}'))
        with self.assertRaisesRegex(ContractError, "invalid JSON constant"):
            load_json(BoundedInput(b'{"number": NaN}'))

    def test_loader_rejects_unbounded_inputs(self):
        with self.assertRaisesRegex(ContractError, "exceeds"):
            load_json(BoundedInput(b" " * (4 * 1024 * 1024 + 1)))
        valid = b"[" * MAX_JSON_DEPTH + b"]" * MAX_JSON_DEPTH
        self.assertIsInstance(load_json(BoundedInput(valid)), list)
        with self.assertRaisesRegex(ContractError, "nesting"):
            load_json(BoundedInput(b"[" + valid + b"]"))

    def test_loader_depth_ignores_brackets_in_escaped_strings(self):
        value = "\\" + '"' + "[" * (MAX_JSON_DEPTH + 1)
        payload = json.dumps([value]).encode("utf-8")
        self.assertEqual(load_json(BoundedInput(payload)), [value])


if __name__ == "__main__":
    unittest.main()
