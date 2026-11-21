"""Synthetic controls for the read-only online evidence checker."""
import contextlib
from copy import deepcopy
import io
import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from verify_skedar_swarm_online import (
    ContractError, EMPTY_WORLD_SHA, load_json, main, verify_witness, world_digest,
)


def reordinal(data):
    for stream in ("host_events", "guest_events"):
        for n, event in enumerate(data[stream], 1):
            event["ordinal"] = n
    return data


def event_in(data, stream, kind, *, epoch=None, seq=None, attachment=None):
    for event in data[stream]:
        if (event["event"] == kind and (epoch is None or event.get("epoch") == epoch)
                and (seq is None or event.get("snapshot_seq") == seq)
                and (attachment is None or event.get("attachment") == attachment)):
            return event
    raise AssertionError(f"fixture lacks {stream} {kind} {epoch} {seq} {attachment}")


def actor(actor_id, generation, position, *, role="raider"):
    return {
        "actor": actor_id, "generation": generation, "role": role,
        "body": "base:sp_body_147" if role == "giant" else "base:skedar",
        "room": "tunnel-a", "position_q": position, "rotation_q": [0, 0, 0, 1000],
        "support_q": [0, 1000, 0], "animation": "base:skedar_run",
        "frame_q": 40, "scale_q": 3000 if role == "giant" else 1000,
    }


def witness():
    host, guest, publications = [], [], {}
    level, scenario = "fixture:skedar_keep", "fixture:three_tunnels"
    host_peer, guest_peer = "host-1", "guest-1"
    tunnels = ["north", "middle", "south"]
    visible = EMPTY_WORLD_SHA

    def emit(stream, event_type, **fields):
        stream.append({"ordinal": len(stream) + 1, "event": event_type, **fields})

    def stage_begin(epoch, attachment, *, begin_host=True):
        if begin_host:
            emit(host, "stage_prepare", epoch=epoch, level=level, scenario=scenario,
                 reason="initial" if epoch == 10 else "same_map_restart")
        emit(guest, "stage_prepare", epoch=epoch, attachment=attachment, level=level, scenario=scenario,
             observed_world_sha256=visible)
        emit(guest, "stage_ready", epoch=epoch, attachment=attachment, post_load=True,
             observed_world_sha256=visible)
        emit(host, "peer_ready", epoch=epoch, attachment=attachment, post_load=True)
        if begin_host:
            emit(host, "stage_active", epoch=epoch, post_load=True)

    def attach(attachment):
        emit(host, "peer_attached", attachment=attachment, peer=guest_peer)
        emit(guest, "connected", attachment=attachment, authority=host_peer)

    def retire(epoch, attachment, reason):
        nonlocal visible
        visible = EMPTY_WORLD_SHA
        emit(guest, "stage_retired", epoch=epoch, attachment=attachment, reason=reason,
             actors_absent=True, observed_world_sha256=visible)

    def detach(attachment, reason):
        emit(host, "peer_detached", attachment=attachment, peer=guest_peer, reason=reason)
        emit(guest, "disconnected", attachment=attachment, reason=reason, observed_world_sha256=visible)

    def publish(epoch, attachment, seq, kind, actors, generation=101, alive=True,
                closed=(), phase="defense", wave=1):
        payload = {
            "actors": actors, "phase": phase, "wave": wave, "closed_tunnels": list(closed),
            "player": {"peer": guest_peer, "generation": generation, "alive": alive},
        }
        publications[epoch, attachment, seq] = payload
        emit(host, "snapshot_published", epoch=epoch, attachment=attachment,
             snapshot_seq=seq, kind=kind, world=deepcopy(payload))

    def receipt(epoch, attachment, seq):
        return {
            "epoch": epoch, "attachment": attachment, "snapshot_seq": seq,
            "authority": host_peer, "received_world_sha256": world_digest(publications[epoch, attachment, seq]),
        }

    def apply(epoch, attachment, seq):
        nonlocal visible
        fields = receipt(epoch, attachment, seq)
        emit(guest, "snapshot_staged", **fields, observed_world_sha256=visible)
        # Receiver actor order is immaterial. Identity and fields must still match.
        applied = deepcopy(publications[epoch, attachment, seq])
        applied["actors"].reverse()
        applied["closed_tunnels"].reverse()
        emit(guest, "snapshot_commit", **fields, world=applied)
        visible = fields["received_world_sha256"]

    def reject(epoch, attachment, seq, reason):
        emit(guest, "snapshot_rejected", **receipt(epoch, attachment, seq),
             reason=reason, observed_world_sha256=visible)

    def player_spawn(epoch, attachment, generation):
        emit(host, "player_spawn", epoch=epoch, attachment=attachment,
             generation=generation, source="ordinary_spawn")

    def gameplay(epoch, attachment, seq, input_seq):
        fields = {"epoch": epoch, "attachment": attachment, "snapshot_seq": seq,
                  "input_seq": input_seq, "source": "ordinary_gameplay"}
        emit(host, "guest_input_accepted", **fields)
        emit(guest, "guest_input_observed", **fields)

    emit(host, "listen_started", peer=host_peer, mode="listen_host")
    emit(guest, "guest_started", peer=guest_peer, mode="ordinary_guest")
    attach(1)
    stage_begin(10, 1)
    player_spawn(10, 1, 101)
    left = actor("raider-a", 4, [100, 0, 0])
    right = actor("raider-b", 5, [200, 0, 0])
    publish(10, 1, 30, "baseline", [left, right])
    apply(10, 1, 30)
    moved = actor("raider-a", 4, [160, 10, 30])
    publish(10, 1, 31, "update", [moved, right])
    emit(guest, "snapshot_staged", **receipt(10, 1, 31), observed_world_sha256=visible)
    emit(guest, "snapshot_aborted", **receipt(10, 1, 31), observed_world_sha256=visible)
    publish(10, 1, 32, "update", [moved], closed=tunnels[:1])
    apply(10, 1, 32)
    reject(10, 1, 32, "duplicate")
    reject(10, 1, 31, "stale_sequence")
    emit(host, "player_death", epoch=10, attachment=1, generation=101, source="ordinary_combat")
    publish(10, 1, 33, "update", [moved], alive=False, closed=tunnels[:1])
    apply(10, 1, 33)
    retire(10, 1, "ordinary_leave")
    detach(1, "ordinary_leave")

    attach(2)
    stage_begin(10, 2, begin_host=False)
    publish(10, 2, 34, "baseline", [moved], alive=False, closed=tunnels[:1])
    apply(10, 2, 34)
    reject(10, 1, 33, "stale_attachment")
    player_spawn(10, 2, 102)
    publish(10, 2, 35, "update", [moved], generation=102, closed=tunnels, phase="draining")
    apply(10, 2, 35)
    gameplay(10, 2, 35, 7)
    king = actor("giant", 9, [300, 400, 500], role="giant")
    publish(10, 2, 36, "update", [king], generation=102, closed=tunnels, phase="finale")
    apply(10, 2, 36)
    king_moved = deepcopy(king)
    king_moved.update(position_q=[280, 390, 510], frame_q=50)
    publish(10, 2, 37, "update", [king_moved], generation=102, closed=tunnels, phase="finale")
    apply(10, 2, 37)
    publish(10, 2, 38, "update", [], generation=102, closed=tunnels, phase="victory")
    apply(10, 2, 38)

    emit(host, "stage_end", epoch=10, actors_absent=True)
    retire(10, 2, "stage_end")
    stage_begin(11, 2)
    player_spawn(11, 2, 103)
    publish(11, 2, 1, "baseline", [left], generation=103)
    reject(10, 2, 38, "stale_epoch")
    apply(11, 2, 1)
    gameplay(11, 2, 1, 8)
    emit(host, "stage_end", epoch=11, actors_absent=True)
    retire(11, 2, "stage_end")
    detach(2, "session_close")
    emit(host, "listen_stopped", actors_absent=True)
    emit(guest, "guest_stopped", actors_absent=True, observed_world_sha256=visible)
    return {
        "schema": 1, "origin": "synthetic", "level": level, "scenario": scenario,
        "host_peer": host_peer, "guest_peer": guest_peer, "tunnels": tunnels,
        "max_commit_gap": 4, "initial_wave": 1,
        "host_events": host, "guest_events": guest,
    }


def native_candidate(data):
    def process(log, number):
        return {
            "client_sha256": "1" * 64, "source_tree_sha256": "2" * 64,
            "public_arena_sha256": "3" * 64, "public_scenario_sha256": "4" * 64,
            "fixture_sha256": str(number) * 64, "managed_recipe_sha256": str(number + 1) * 64,
            "native_log_sha256": log * 64,
        }
    data.update(origin="native_candidate", provenance={
        "host": process("a", 5), "guest": process("b", 7), "runner_receipt_sha256": "c" * 64,
    })
    return data


class OnlineWitnessTests(unittest.TestCase):
    def reject(self, change, reason, *, candidate=False):
        data = witness()
        if candidate:
            native_candidate(data)
        change(data)
        with self.assertRaisesRegex(ContractError, reason):
            verify_witness(data, native_candidate=candidate)

    def test_synthetic_controls_show_scope_and_distinct_lifecycles(self):
        report = verify_witness(witness())
        self.assertEqual(report["verdict"], "contract_pass")
        self.assertEqual(report["origin"], "synthetic")
        self.assertFalse(report["native_gameplay_verified"])
        self.assertEqual(report["summary"]["epochs"], [10, 11])
        self.assertEqual(report["summary"]["attachments"], [1, 2])
        self.assertEqual(report["summary"]["actor_and_pose_commits"], 9)
        self.assertEqual(report["summary"]["observed_valid_publication_gaps"], 1)
        self.assertEqual(report["summary"]["host_correlated_gameplay_inputs"], 2)

    def test_native_candidate_shape_never_claims_native_qualification(self):
        report = verify_witness(native_candidate(witness()), native_candidate=True)
        self.assertFalse(report["native_gameplay_verified"])
        self.assertEqual(report["native_qualification"], "unverified")
        self.assertTrue(report["remaining_native_evidence"])

    def test_synthetic_rejected_by_native_candidate_route(self):
        with self.assertRaisesRegex(ContractError, "synthetic witness cannot"):
            verify_witness(witness(), native_candidate=True)

    def test_candidate_requires_independent_process_receipts(self):
        self.reject(lambda data: data["provenance"].pop("guest"), "separate process", candidate=True)

    def test_candidate_must_bind_exact_matching_public_source(self):
        self.reject(lambda data: data["provenance"]["guest"].update(public_arena_sha256="f" * 64),
                    "different client or authored source", candidate=True)

    def test_candidate_cannot_reuse_one_native_log(self):
        self.reject(lambda data: data["provenance"]["guest"].update(native_log_sha256="a" * 64),
                    "same process log", candidate=True)

    def test_guest_cannot_publish_authoritative_state(self):
        def inject(data):
            data["guest_events"][5]["event"] = "snapshot_published"
        self.reject(inject, "unknown guest event snapshot_published")

    def test_listen_authority_required(self):
        self.reject(lambda data: data["host_events"][0].update(mode="dedicated"),
                    "ordinary listen-host authority")

    def test_guest_stage_ready_is_required(self):
        def remove(data):
            events = data["guest_events"]
            events.remove(event_in(data, "guest_events", "stage_ready"))
            reordinal(data)
        self.reject(remove, "guest snapshot staging")

    def test_host_snapshot_requires_peer_rematching_after_rejoin(self):
        def remove(data):
            events = data["host_events"]
            events.remove(event_in(data, "host_events", "peer_ready", epoch=10, attachment=2))
            reordinal(data)
        self.reject(remove, "before stage and peer admission")

    def test_rejoin_cannot_commit_an_update_as_its_first_baseline(self):
        self.reject(lambda data: event_in(data, "host_events", "snapshot_published", seq=34).update(kind="update"),
                    "exactly one initial baseline")

    def test_commit_requires_a_matching_staged_snapshot(self):
        def remove(data):
            events = data["guest_events"]
            events.remove(event_in(data, "guest_events", "snapshot_staged", seq=30))
            reordinal(data)
        self.reject(remove, "without its matching staged")

    def test_partially_publishing_staged_or_aborted_world_is_rejected(self):
        for kind in ("snapshot_staged", "snapshot_aborted"):
            with self.subTest(kind=kind):
                self.reject(lambda data: event_in(data, "guest_events", kind, seq=31).update(
                    observed_world_sha256="f" * 64), "world changed outside snapshot commit")

    def test_commit_checks_room_body_rotation_support_frame_scale_and_identity(self):
        mutations = {
            "room": "tunnel-b", "body": "base:sp_body_147", "rotation_q": [1, 0, 0, 99],
            "position_q": [1, 2, 3], "support_q": [1, 2, 3], "frame_q": 41,
            "scale_q": 1001, "generation": 6, "animation": "base:skedar_jump",
        }
        for name, value in mutations.items():
            with self.subTest(field=name):
                self.reject(lambda data: event_in(data, "guest_events", "snapshot_commit", seq=32)
                            ["world"]["actors"][0].update({name: value}), "committed world differs")

    def test_guest_cannot_publish_a_different_tunnel_state(self):
        self.reject(lambda data: event_in(data, "guest_events", "snapshot_commit", seq=32)
                    ["world"].update(closed_tunnels=["middle"]), "committed world differs")

    def test_rejected_duplicate_must_leave_visible_state_unchanged(self):
        self.reject(lambda data: event_in(data, "guest_events", "snapshot_rejected", seq=32).update(
            observed_world_sha256="a" * 64), "world changed outside snapshot commit")

    def test_rejection_requires_a_valid_host_publication(self):
        self.reject(lambda data: event_in(data, "guest_events", "snapshot_rejected", seq=31).update(
            received_world_sha256="d" * 64), "not tied to a valid listen-host publication")

    def test_valid_old_epoch_is_distinct_from_duplicate(self):
        self.reject(lambda data: event_in(data, "guest_events", "snapshot_rejected", seq=38).update(
            reason="duplicate"), "wrong stale or duplicate outcome")

    def test_missing_reordered_control_cannot_qualify(self):
        def remove(data):
            events = data["guest_events"]
            events.remove(event_in(data, "guest_events", "snapshot_rejected", seq=31))
            reordinal(data)
        self.reject(remove, "missing unchanged-state duplicate")

    def test_missing_stale_attachment_control_cannot_qualify(self):
        def remove(data):
            events = data["guest_events"]
            old_attachment = event_in(data, "guest_events", "snapshot_rejected", epoch=10,
                                      seq=33, attachment=1)
            events.remove(old_attachment)
            reordinal(data)
        self.reject(remove, "missing unchanged-state duplicate, stale-attachment")

    def test_guest_sequence_gap_must_be_bounded(self):
        def skip(data):
            for kind in ("snapshot_staged", "snapshot_commit"):
                events = data["guest_events"]
                events.remove(event_in(data, "guest_events", kind, seq=32))
            for seq in (32, 31):
                events.remove(event_in(data, "guest_events", "snapshot_rejected", seq=seq))
            previous = event_in(data, "guest_events", "snapshot_commit", seq=30)["world"]
            event_in(data, "guest_events", "snapshot_staged", seq=33).update(
                observed_world_sha256=world_digest(previous))
            data["max_commit_gap"] = 2
            reordinal(data)
        self.reject(skip, "snapshot gap exceeds")

    def test_retired_swarm_generation_cannot_reappear(self):
        self.reject(lambda data: event_in(data, "host_events", "snapshot_published", seq=33)
                    ["world"]["actors"].append(actor("raider-b", 5, [200, 0, 0])),
                    "retired Swarm actor generation reappeared")

    def test_body_reference_is_immutable_within_actor_generation(self):
        self.reject(lambda data: event_in(data, "host_events", "snapshot_published", seq=32)
                    ["world"]["actors"][0].update(body="base:sp_body_147"),
                    "live actor generation changed body")

    def test_replay_rejects_nonmonotonic_host_snapshot_sequence(self):
        self.reject(lambda data: event_in(data, "host_events", "snapshot_published", seq=31).update(snapshot_seq=30),
                    "sequence is missing, repeated or reordered")

    def test_ordinary_death_cannot_be_replaced_by_a_synthetic_action(self):
        self.reject(lambda data: event_in(data, "host_events", "player_death").update(source="injected_kill"),
                    "authoritative guest death")

    def test_reused_guest_life_generation_cannot_become_alive_after_rejoin(self):
        self.reject(lambda data: event_in(data, "host_events", "player_spawn", attachment=2, epoch=10).update(generation=101),
                    "fresh live generation")

    def test_rejoin_must_show_host_correlated_input(self):
        def remove(data):
            events = data["guest_events"]
            events.remove(event_in(data, "guest_events", "guest_input_observed", epoch=10, attachment=2))
            reordinal(data)
        self.reject(remove, "no independently witnessed ordinary gameplay")

    def test_same_map_restart_rejects_old_epoch_reuse(self):
        self.reject(lambda data: event_in(data, "host_events", "stage_prepare", epoch=11).update(epoch=10),
                    "fresh same-map epoch")

    def test_initial_baseline_must_start_at_declared_wave(self):
        self.reject(lambda data: event_in(data, "host_events", "snapshot_published", epoch=10, seq=30)
                    ["world"].update(wave=2), "fresh epoch baseline")

    def test_restart_baseline_cannot_inherit_draining_phase(self):
        self.reject(lambda data: event_in(data, "host_events", "snapshot_published", epoch=11, seq=1)
                    ["world"].update(phase="draining", closed_tunnels=data["tunnels"]),
                    "fresh epoch baseline")

    def test_restart_baseline_cannot_inherit_closed_tunnel(self):
        self.reject(lambda data: event_in(data, "host_events", "snapshot_published", epoch=11, seq=1)
                    ["world"].update(closed_tunnels=["north"]), "fresh epoch baseline")

    def test_restart_baseline_must_reset_wave(self):
        self.reject(lambda data: event_in(data, "host_events", "snapshot_published", epoch=11, seq=1)
                    ["world"].update(wave=2), "fresh epoch baseline")

    def test_retire_must_observe_no_leftover_actors(self):
        self.reject(lambda data: event_in(data, "guest_events", "stage_retired", epoch=10, attachment=1).update(
            actors_absent=False), "without an empty world")

    def test_giant_cannot_precede_encounter_finale(self):
        self.reject(lambda data: event_in(data, "host_events", "snapshot_published", seq=35)
                    ["world"]["actors"].append(actor("giant", 9, [0, 0, 0], role="giant")),
                    "early, terminal or duplicate Giant")

    def test_successive_distinct_giant_lifetimes_cannot_share_one_epoch(self):
        for actor_id, generation in (("giant-replacement", 9), ("giant", 10)):
            with self.subTest(actor=actor_id, generation=generation):
                self.reject(lambda data: event_in(data, "host_events", "snapshot_published", epoch=10, seq=37)
                            ["world"]["actors"][0].update(actor=actor_id, generation=generation),
                            "more than one Giant lifetime")

    def test_parser_rejects_duplicate_keys_and_deep_input(self):
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "bad.json"
            path.write_text('{"schema":1,"schema":2}', encoding="utf-8")
            with self.assertRaisesRegex(ContractError, "duplicate JSON field"):
                load_json(path)
            path.write_text("[" * 25 + "0" + "]" * 25, encoding="utf-8")
            with self.assertRaisesRegex(ContractError, "nested too deeply"):
                load_json(path)

    def test_cli_replays_valid_input_and_rejects_unbound_native_request(self):
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "trace.json"
            path.write_text(json.dumps(witness()), encoding="utf-8")
            for mode, code in (([], 0), (["--native-candidate"], 1)):
                with self.subTest(mode=mode), contextlib.redirect_stdout(io.StringIO()) as output:
                    self.assertEqual(main([str(path), *mode]), code)
                    self.assertFalse(json.loads(output.getvalue())["native_gameplay_verified"])


if __name__ == "__main__":
    unittest.main()
