"""Validate retained Swarm listen-host and guest observations offline.

The fixture exports two ordered, process-local event lists, host_events and
guest_events. Use ordinal (starting at one) within each stream, and correlate
by epoch, attachment and snapshot_seq across streams. No wall-clock ordering
or transport input is used. A host snapshot is an entire committed engine
projection. A guest snapshot_commit measures the same projection from the
applied world, not the staged transfer or GPU texture. Stage/abort/reject
events record a fresh observed_world_sha256 of the visible committed world.

The engine adapter supplies exact canonical quantized integers for effective
actor position, orientation, support, animation and scale. The inactive world
digest comes from {"actors":[],"phase":"inactive"}. Player lifecycle records
describe post-load engine lifetimes, with separate attachment and stage IDs.
The witness declares the initial wave for each fresh stage. This checker never
qualifies native provenance from an origin label or supplied hash.
"""
import argparse
from dataclasses import dataclass, field
from hashlib import sha256
import json
from pathlib import Path
import re

MAX_BYTES = 8 * 1024 * 1024
MAX_DEPTH = 24
MAX_EVENTS = 4096
MAX_SNAPSHOTS = 1024
MAX_ACTORS = 4096
MAX_TICK_VALUE = 2**32 - 1
MAX_GENERATION = 2**64 - 1
IDENTIFIER = re.compile(r"[A-Za-z0-9][A-Za-z0-9_./:-]{0,127}\Z")
SHA256 = re.compile(r"[0-9a-f]{64}\Z")
PHASE_ORDER = {"defense": 0, "draining": 1, "finale": 2, "victory": 3}
EMPTY_WORLD_SHA = sha256(b'{"actors":[],"phase":"inactive"}').hexdigest()
PROVENANCE = {
    "client_sha256", "source_tree_sha256", "public_arena_sha256",
    "public_scenario_sha256", "fixture_sha256", "managed_recipe_sha256",
    "native_log_sha256",
}


class ContractError(ValueError):
    pass


def require(condition, reason):
    if not condition:
        raise ContractError(reason)


def object_value(value, name):
    require(isinstance(value, dict), f"{name} must be an object")
    return value


def array(value, name, limit, minimum=0):
    require(isinstance(value, list) and minimum <= len(value) <= limit,
            f"{name} must have {minimum} to {limit} entries")
    return value


def identifier(value, name):
    require(isinstance(value, str) and IDENTIFIER.fullmatch(value), f"invalid {name}")
    return value


def ident(row, key):
    return identifier(row.get(key), key)


def integer(value, name, minimum, maximum):
    require(type(value) is int and minimum <= value <= maximum, f"invalid {name}")
    return value


def number(row, key, minimum=1, maximum=MAX_TICK_VALUE):
    return integer(row.get(key), key, minimum, maximum)


def vector(row, key, count):
    value = array(row.get(key), key, count, count)
    return [integer(entry, key, -(2**31), 2**31 - 1) for entry in value]


def flag(row, key):
    value = row.get(key)
    require(type(value) is bool, f"invalid {key}")
    return value


def digest_field(row, key):
    value = row.get(key)
    require(isinstance(value, str) and SHA256.fullmatch(value), f"invalid {key}")
    return value


def unique_object(pairs):
    result = {}
    for key, data in pairs:
        require(key not in result, f"duplicate JSON field {key}")
        result[key] = data
    return result


def invalid_constant(value):
    raise ContractError(f"invalid JSON constant {value}")


def load_json(path):
    with path.open("rb") as stream:
        data = stream.read(MAX_BYTES + 1)
    require(len(data) <= MAX_BYTES, "online witness exceeds the byte limit")
    text = data.decode("utf-8-sig")
    depth = 0
    in_string = escaped = False
    for char in text:
        if in_string:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
        elif char == '"':
            in_string = True
        elif char in "[{":
            depth += 1
            require(depth <= MAX_DEPTH, "online witness is nested too deeply")
        elif char in "]}":
            depth -= 1
            require(depth >= 0, "unexpected JSON closing delimiter")
    return json.loads(text, object_pairs_hook=unique_object, parse_constant=invalid_constant)


def world_projection(value):
    """Return the entire canonical engine state used in peer comparisons."""
    state = object_value(value, "world")
    require(set(state) == {"actors", "closed_tunnels", "player", "phase", "wave"},
            "world projection is incomplete or has unknown fields")
    phase = ident(state, "phase")
    require(phase in PHASE_ORDER, "invalid encounter phase")
    closed = [identifier(name, "tunnel ID")
              for name in array(state["closed_tunnels"], "closed tunnels", 3)]
    require(len(set(closed)) == len(closed), "duplicate closed tunnel")
    player = object_value(state["player"], "player")
    require(set(player) == {"peer", "generation", "alive"}, "invalid player projection")
    actors = []
    identities = set()
    for raw in array(state["actors"], "actors", MAX_ACTORS):
        row = object_value(raw, "actor")
        require(set(row) == {"actor", "generation", "role", "body", "room", "position_q",
                             "rotation_q", "support_q", "animation", "frame_q", "scale_q"},
                "actor projection is incomplete or has unknown fields")
        actor_id = ident(row, "actor")
        require(actor_id not in identities, "duplicate actor ID in committed world")
        identities.add(actor_id)
        role = ident(row, "role")
        require(role in {"raider", "giant"}, "invalid Swarm actor role")
        body = ident(row, "body")
        require(":" in body, "actor body must be a catalog ID")
        rotation = vector(row, "rotation_q", 4)
        support = vector(row, "support_q", 3)
        require(any(rotation) and any(support), "zero actor rotation or support projection")
        actors.append({
            "actor": actor_id, "generation": number(row, "generation", 1, MAX_GENERATION),
            "role": role, "body": body, "room": ident(row, "room"), "position_q": vector(row, "position_q", 3),
            "rotation_q": rotation, "support_q": support, "animation": ident(row, "animation"),
            "frame_q": number(row, "frame_q", 0), "scale_q": number(row, "scale_q"),
        })
    return {
        "actors": sorted(actors, key=lambda actor: actor["actor"]),
        "closed_tunnels": sorted(closed), "phase": phase, "wave": number(state, "wave", 0),
        "player": {"peer": ident(player, "peer"), "generation": number(player, "generation", 1, MAX_GENERATION),
                   "alive": flag(player, "alive")},
    }


def projection_digest(world):
    data = json.dumps(world, sort_keys=True, separators=(",", ":"), ensure_ascii=True)
    return sha256(data.encode("ascii")).hexdigest()


def world_digest(state):
    """Public utility for a fixture adapter that has measured world fields."""
    return projection_digest(world_projection(state))


def key_for(row):
    return number(row, "epoch"), number(row, "attachment"), number(row, "snapshot_seq")


def ordered_events(rows, name):
    for expected, row in enumerate(array(rows, name, MAX_EVENTS, 1), 1):
        event = object_value(row, f"{name} event {expected}")
        require(number(event, "ordinal") == expected, f"{name} event ordinals are not contiguous")
        yield expected, event


@dataclass
class Snapshot:
    kind: str
    world: dict
    world_hash: str


@dataclass
class Host:
    host_peer: str
    guest_peer: str
    level: str
    scenario: str
    tunnels: set
    max_gap: int
    initial_wave: int
    epoch: int = 0
    attachment: int = 0
    phase: str = "absent"
    listening: bool = False
    closed: bool = False
    committed_seq: int = 0
    generation: int = 0
    player_alive: bool = False
    attached: list = field(default_factory=list)
    detached: dict = field(default_factory=dict)
    epochs: list = field(default_factory=list)
    ended: set = field(default_factory=set)
    ready: set = field(default_factory=set)
    baselines: set = field(default_factory=set)
    deaths: set = field(default_factory=set)
    acknowledgments: set = field(default_factory=set)
    snapshots: dict = field(default_factory=dict)
    previous_world: dict | None = None
    generations: dict = field(default_factory=dict)
    giant_lifetime: tuple | None = None

    def life(self, row):
        require(self.phase == "active" and self.attachment > 0, "player lifecycle outside active stage")
        require(number(row, "epoch") == self.epoch and number(row, "attachment") == self.attachment,
                "player lifecycle uses a stale stage or attachment")
        return number(row, "generation", 1, MAX_GENERATION)

    def publish(self, row):
        key = key_for(row)
        epoch, attachment, seq = key
        binding = key[:2]
        require(self.phase == "active" and binding == (self.epoch, self.attachment)
                and binding in self.ready, "host published before stage and peer admission")
        require(len(self.snapshots) < MAX_SNAPSHOTS, "host snapshot trace exceeds its bound")
        require(not self.committed_seq or seq == self.committed_seq + 1,
                "host snapshot sequence is missing, repeated or reordered")
        kind = ident(row, "kind")
        require(kind == ("update" if binding in self.baselines else "baseline"),
                "host attachment needs exactly one initial baseline")
        world = world_projection(row.get("world"))
        require(world["player"] == {"peer": self.guest_peer, "generation": self.generation,
                                    "alive": self.player_alive} and self.generation > 0,
                "host snapshot disagrees with its independent guest lifecycle")
        require(set(world["closed_tunnels"]) <= self.tunnels, "host used an unknown tunnel")
        if not self.committed_seq:
            require(world["phase"] == "defense" and not world["closed_tunnels"]
                    and world["wave"] == self.initial_wave,
                    "fresh epoch baseline must start in defense with all tunnels open and the declared initial wave")
        require((len(world["closed_tunnels"]) == 3) == (world["phase"] != "defense"),
                "host phase disagrees with the three objective closures")
        giants = [(actor["actor"], actor["generation"])
                  for actor in world["actors"] if actor["role"] == "giant"]
        require(len(giants) <= 1 and (not giants or world["phase"] == "finale"),
                "host snapshot contains an early, terminal or duplicate Giant")
        if giants:
            require(self.giant_lifetime in (None, giants[0]),
                    "host published more than one Giant lifetime in the epoch")
            self.giant_lifetime = giants[0]
        if world["phase"] in {"finale", "victory"}:
            require(all(actor["role"] == "giant" for actor in world["actors"]),
                    "host finale contains a surviving raider")
        previous = self.previous_world
        live = {}
        if previous is not None:
            require(set(previous["closed_tunnels"]) <= set(world["closed_tunnels"])
                    and PHASE_ORDER[previous["phase"]] <= PHASE_ORDER[world["phase"]]
                    and previous["wave"] <= world["wave"], "host encounter regressed within the stage epoch")
            live = {(actor["actor"], actor["generation"]): (actor["body"], actor["role"])
                    for actor in previous["actors"]}
        for actor in world["actors"]:
            pair = actor["actor"], actor["generation"]
            old = self.generations.get(pair[0])
            require(old is None or pair[1] > old or (pair[1] == old and pair in live),
                    "retired Swarm actor generation reappeared")
            require(pair not in live or live[pair] == (actor["body"], actor["role"]),
                    "live actor generation changed body or role")
            self.generations[pair[0]] = pair[1]
        self.snapshots[key] = Snapshot(kind, world, projection_digest(world))
        self.baselines.add(binding)
        self.previous_world = world
        self.committed_seq = seq

    def observe(self, event, row):
        require(not self.closed, "events after host closed")
        if event == "listen_started":
            require(not self.listening and self.phase == "absent"
                    and ident(row, "peer") == self.host_peer
                    and row.get("mode") == "listen_host",
                    "missing singular ordinary listen-host authority")
            self.listening = True
            return
        require(self.listening, "event before listen start")
        if event == "peer_attached":
            attachment = number(row, "attachment")
            require(self.attachment == 0 and ident(row, "peer") == self.guest_peer
                    and len(self.attached) < 2 and (not self.attached or attachment > self.attached[-1]),
                    "host attachment is stale, duplicate or has the wrong guest")
            self.attached.append(attachment)
            self.attachment = attachment
        elif event == "peer_detached":
            require(number(row, "attachment") == self.attachment and ident(row, "peer") == self.guest_peer,
                    "host detached a different guest")
            reason = "ordinary_leave" if len(self.attached) == 1 else "session_close"
            require(row.get("reason") == reason, "host detach lacks the fixture's ordinary path")
            self.detached[self.attachment] = reason
            self.attachment = 0
        elif event == "stage_prepare":
            epoch = number(row, "epoch")
            reason = "same_map_restart" if self.epochs else "initial"
            require(len(self.epochs) < 2 and self.phase == "absent" and self.attachment > 0
                    and epoch > self.epoch and row.get("reason") == reason
                    and (ident(row, "level"), ident(row, "scenario")) == (self.level, self.scenario),
                    "stage preparation lacks a fresh same-map epoch or matching authored identity")
            self.epoch, self.phase, self.committed_seq = epoch, "preparing", 0
            self.epochs.append(epoch)
            self.previous_world = None
            self.generations.clear()
            self.giant_lifetime = None
        elif event == "peer_ready":
            binding = number(row, "epoch"), number(row, "attachment")
            require(self.phase in {"preparing", "active"} and self.attachment > 0
                    and binding == (self.epoch, self.attachment) and binding not in self.ready
                    and flag(row, "post_load"), "host observed invalid or duplicate peer readiness")
            self.ready.add(binding)
        elif event == "stage_active":
            require(self.phase == "preparing" and number(row, "epoch") == self.epoch
                    and (self.epoch, self.attachment) in self.ready and flag(row, "post_load"),
                    "listen host activated before both peers were ready")
            self.phase = "active"
        elif event == "player_spawn":
            generation = self.life(row)
            require(not self.player_alive and generation > self.generation
                    and row.get("source") == "ordinary_spawn", "player spawn lacks a fresh live generation")
            self.generation, self.player_alive = generation, True
        elif event == "player_death":
            generation = self.life(row)
            require(self.player_alive and generation == self.generation
                    and row.get("source") == "ordinary_combat", "host lacks authoritative guest death")
            self.player_alive = False
            self.deaths.add((self.epoch, self.attachment, generation))
        elif event == "snapshot_published":
            self.publish(row)
        elif event == "guest_input_accepted":
            key = key_for(row)
            witness = key + (number(row, "input_seq"),)
            snapshot = self.snapshots.get(key)
            require(self.phase == "active" and key[:2] == (self.epoch, self.attachment)
                    and snapshot is not None and snapshot.world["player"]["alive"] and self.player_alive
                    and snapshot.world["player"]["generation"] == self.generation
                    and row.get("source") == "ordinary_gameplay" and witness not in self.acknowledgments,
                    "host input acceptance is not bound to a live guest snapshot")
            self.acknowledgments.add(witness)
        elif event == "stage_end":
            require(self.phase == "active" and number(row, "epoch") == self.epoch
                    and flag(row, "actors_absent"), "stage ended without explicit authority teardown")
            self.phase, self.generation, self.player_alive = "absent", 0, False
            self.ended.add(self.epoch)
        elif event == "listen_stopped":
            require(self.phase == "absent" and self.attachment == 0
                    and flag(row, "actors_absent"), "listen host stopped before stage or peer teardown")
            self.closed = True
        else:
            raise ContractError(f"unknown host event {event}")


@dataclass
class Guest:
    host: Host
    started: bool = False
    closed: bool = False
    attachment: int = 0
    epoch: int = 0
    phase: str = "absent"
    committed_seq: int = 0
    visible_hash: str = EMPTY_WORLD_SHA
    pending: tuple | None = None
    connected: list = field(default_factory=list)
    ready: set = field(default_factory=set)
    commits: set = field(default_factory=set)
    deaths: set = field(default_factory=set)
    rejected: set = field(default_factory=set)
    inputs: set = field(default_factory=set)
    ended: set = field(default_factory=set)
    gaps: int = 0
    visible_raider: set = field(default_factory=set)
    visible_giant: bool = False
    victory: bool = False

    def no_visible_change(self, row):
        require(digest_field(row, "observed_world_sha256") == self.visible_hash,
                "guest visible world changed outside snapshot commit")

    def received(self, row):
        key = key_for(row)
        snapshot = self.host.snapshots.get(key)
        require(ident(row, "authority") == self.host.host_peer and snapshot is not None
                and digest_field(row, "received_world_sha256") == snapshot.world_hash,
                "received world is not tied to a valid listen-host publication")
        return key, snapshot

    def stage(self, row):
        key, snapshot = self.received(row)
        require(self.phase == "ready" and key[:2] == (self.epoch, self.attachment)
                and key[2] > self.committed_seq
                and self.pending is None, "invalid or overlapping guest snapshot staging")
        require(snapshot.kind == ("update" if self.committed_seq else "baseline"),
                "guest staged an update before its fresh baseline")
        self.no_visible_change(row)
        self.pending = key

    def commit(self, row):
        key, snapshot = self.received(row)
        require(key == self.pending and self.phase == "ready",
                "guest committed without its matching staged snapshot")
        observed = world_projection(row.get("world"))
        require(observed == snapshot.world, "guest committed world differs in actor, pose, or encounter state")
        if self.committed_seq:
            span = key[2] - self.committed_seq
            require(0 < span <= self.host.max_gap, "guest snapshot gap exceeds the fixture bound")
            self.gaps += span - 1
        else:
            require(key not in self.commits, "guest committed the same baseline more than once")
        self.pending = None
        self.committed_seq, self.visible_hash = key[2], snapshot.world_hash
        self.commits.add(key)
        player = observed["player"]
        if not player["alive"]:
            self.deaths.add(key[:2] + (player["generation"],))
        if any(actor["role"] == "raider" for actor in observed["actors"]):
            self.visible_raider.add(key[:2])
        self.visible_giant |= any(actor["role"] == "giant" for actor in observed["actors"])
        self.victory |= observed["phase"] == "victory"

    def reject(self, row):
        key, _ = self.received(row)
        require(self.phase == "ready", "guest rejection was outside a ready stage")
        epoch, attachment, seq = key
        if epoch < self.epoch:
            reason = "stale_epoch"
        elif epoch == self.epoch and attachment < self.attachment:
            reason = "stale_attachment"
        elif (epoch, attachment) == (self.epoch, self.attachment) and self.committed_seq:
            reason = "duplicate" if seq == self.committed_seq else "stale_sequence"
            require(seq <= self.committed_seq, "guest rejected a fresh snapshot as stale")
        else:
            raise ContractError("guest rejection was not an old or duplicate valid publication")
        require(row.get("reason") == reason, "guest reported the wrong stale or duplicate outcome")
        self.no_visible_change(row)
        self.rejected.add(reason)


    def observe(self, event, row):
        require(not self.closed, "events after guest closed")
        if event == "guest_started":
            require(not self.started and ident(row, "peer") == self.host.guest_peer
                    and row.get("mode") == "ordinary_guest", "missing singular ordinary guest")
            self.started = True
            return
        require(self.started, "event before guest start")
        if event == "connected":
            attachment = number(row, "attachment")
            index = len(self.connected)
            require(self.attachment == 0 and index < len(self.host.attached)
                    and attachment == self.host.attached[index] and ident(row, "authority") == self.host.host_peer,
                    "guest connected to a different authority or attachment")
            self.connected.append(attachment)
            self.attachment = attachment
        elif event == "stage_prepare":
            epoch, attachment = number(row, "epoch"), number(row, "attachment")
            require(self.phase == "absent" and self.attachment == attachment and (epoch, attachment) in self.host.ready
                    and epoch >= self.epoch and (epoch, attachment) not in self.ready
                    and (ident(row, "level"), ident(row, "scenario")) == (self.host.level, self.host.scenario),
                    "guest prepared an unbound stage or reused its retired stage")
            self.no_visible_change(row)
            self.epoch, self.phase = epoch, "preparing"
        elif event == "stage_ready":
            binding = number(row, "epoch"), number(row, "attachment")
            require(self.phase == "preparing" and binding == (self.epoch, self.attachment)
                    and flag(row, "post_load"), "guest readiness does not follow its prepared stage")
            self.no_visible_change(row)
            self.phase = "ready"
            self.ready.add(binding)
        elif event == "snapshot_staged":
            self.stage(row)
        elif event == "snapshot_commit":
            self.commit(row)
        elif event == "snapshot_aborted":
            key, _ = self.received(row)
            require(self.pending == key and self.phase == "ready",
                    "guest aborted a snapshot that was not staged")
            self.no_visible_change(row)
            self.pending = None
        elif event == "snapshot_rejected":
            self.reject(row)
        elif event == "guest_input_observed":
            key = key_for(row)
            witness = key + (number(row, "input_seq"),)
            require(key == (self.epoch, self.attachment, self.committed_seq)
                    and witness in self.host.acknowledgments and witness not in self.inputs
                    and row.get("source") == "ordinary_gameplay",
                    "guest resumed gameplay without the matching host input acceptance")
            self.inputs.add(witness)
        elif event == "stage_retired":
            binding = number(row, "epoch"), number(row, "attachment")
            reason = ident(row, "reason")
            authority_closed = (
                (reason == "ordinary_leave" and self.host.detached.get(self.attachment) == reason)
                or (reason == "stage_end" and self.epoch in self.host.ended)
            )
            require(self.phase == "ready" and binding == (self.epoch, self.attachment)
                    and self.pending is None and authority_closed and flag(row, "actors_absent")
                    and digest_field(row, "observed_world_sha256") == EMPTY_WORLD_SHA,
                    "guest retired a stage without an empty world and authority teardown")
            self.ended.add(binding)
            self.phase, self.committed_seq, self.visible_hash = "absent", 0, EMPTY_WORLD_SHA
        elif event == "disconnected":
            attachment = number(row, "attachment")
            require(self.phase == "absent" and self.attachment == attachment
                    and row.get("reason") == self.host.detached.get(attachment),
                    "guest disconnected with a live stage or wrong authority reason")
            self.no_visible_change(row)
            self.attachment = 0
        elif event == "guest_stopped":
            require(self.phase == "absent" and self.attachment == 0 and flag(row, "actors_absent"),
                    "guest stopped before stage and peer teardown")
            self.no_visible_change(row)
            self.closed = True
        else:
            raise ContractError(f"unknown guest event {event}")


def check_provenance(trace, origin):
    if origin != "native_candidate":
        require("provenance" not in trace, "synthetic witness cannot attach native provenance")
        return
    provenance = object_value(trace.get("provenance"), "native candidate provenance")
    require(set(provenance) == {"host", "guest", "runner_receipt_sha256"},
            "native candidate lacks separate process and runner receipts")
    digest_field(provenance, "runner_receipt_sha256")
    host = object_value(provenance["host"], "host provenance")
    guest = object_value(provenance["guest"], "guest provenance")
    require(set(host) == set(guest) == PROVENANCE, "native candidate provenance is incomplete")
    for key in PROVENANCE:
        digest_field(host, key)
        digest_field(guest, key)
    require(all(host[key] == guest[key] for key in (
        "client_sha256", "source_tree_sha256", "public_arena_sha256", "public_scenario_sha256",
    )), "host and guest used different client or authored source identities")
    require(host["native_log_sha256"] != guest["native_log_sha256"],
            "native candidate repeats the same process log")


def verify_witness(trace, *, native_candidate=False):
    trace = object_value(trace, "online witness")
    require(number(trace, "schema", 1, 1) == 1, "invalid schema")
    origin = ident(trace, "origin")
    require(origin in {"synthetic", "native_candidate"}, "unknown witness origin")
    require(not native_candidate or origin == "native_candidate",
            "synthetic witness cannot enter the native-candidate evidence route")
    check_provenance(trace, origin)
    level, scenario = ident(trace, "level"), ident(trace, "scenario")
    authority, peer = ident(trace, "host_peer"), ident(trace, "guest_peer")
    require(authority != peer, "listen host and guest must be distinct")
    tunnels = {identifier(name, "tunnel ID") for name in array(trace.get("tunnels"), "tunnels", 3, 3)}
    require(len(tunnels) == 3, "three distinct authored tunnel IDs are required")
    host = Host(authority, peer, level, scenario, tunnels,
                number(trace, "max_commit_gap", 2, 64), number(trace, "initial_wave", 0))
    guest = Guest(host)
    for name, observer in (("host_events", host), ("guest_events", guest)):
        for ordinal, row in ordered_events(trace.get(name), name):
            try:
                observer.observe(ident(row, "event"), row)
            except ContractError as exc:
                raise ContractError(f"{name} event {ordinal}: {exc}") from exc

    require(host.closed and guest.closed and len(host.epochs) == 2 and len(host.attached) == 2,
            "ordinary closeout, rejoin or fresh same-map restart is missing")
    first_epoch, fresh_epoch = host.epochs
    first_attachment, rejoined = host.attached
    initial = first_epoch, first_attachment
    resumed = first_epoch, rejoined
    restarted = fresh_epoch, rejoined
    expected = {initial, resumed, restarted}
    require(host.ended == set(host.epochs) and host.ready == guest.ready == guest.ended == expected,
            "host and guest stage transaction evidence differs")
    require({key[:2] for key in guest.commits} == expected and guest.visible_raider == expected,
            "missing matching guest raider/pose commits in a required stage or attachment")
    require(guest.visible_giant and guest.victory, "guest did not observe the Giant and encounter victory")
    require(any(death[:2] == initial and death in guest.deaths for death in host.deaths),
            "initial authority death never committed on the guest")
    require({resumed, restarted} <= {key[:2] for key in guest.inputs},
            "no independently witnessed ordinary gameplay after rejoin and restart")
    require({"stale_epoch", "stale_attachment", "stale_sequence", "duplicate"} <= guest.rejected and guest.gaps > 0,
            "missing unchanged-state duplicate, stale-attachment, stale-epoch or reordered/gapped publication evidence")
    return {
        "verdict": "contract_pass",
        "origin": origin,
        "native_gameplay_verified": False,
        "native_qualification": "unverified",
        "summary": {
            "epochs": host.epochs, "attachments": host.attached,
            "actor_and_pose_commits": len(guest.commits),
            "observed_valid_publication_gaps": guest.gaps,
            "rejection_kinds": sorted(guest.rejected),
            "host_correlated_gameplay_inputs": len(guest.inputs),
        },
        "remaining_native_evidence": [
            "Independently bind both raw game logs, exact binaries, public sources, managed installs, and runner receipts.",
            "Verify the guest committed projection was read from real engine actor and pose state.",
            "Review ordinary gameplay, fixture delivery schedule and connection closeout.",
            "Host loss, physical-peer/WAN behavior and measured scale are separate native gates.",
        ],
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("witness", type=Path)
    parser.add_argument("--native-candidate", action="store_true",
                        help="demand native-candidate metadata without qualifying it as native")
    args = parser.parse_args(argv)
    try:
        report = verify_witness(load_json(args.witness), native_candidate=args.native_candidate)
    except (OSError, ValueError) as exc:
        print(json.dumps({"verdict": "invalid_input", "reason": str(exc), "native_gameplay_verified": False}))
        return 1
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
