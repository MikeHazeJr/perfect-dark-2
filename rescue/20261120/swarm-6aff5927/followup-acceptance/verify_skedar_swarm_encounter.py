"""Replay the authored Skedar encounter trace for offline contract checks."""
import argparse
from collections import Counter
from dataclasses import dataclass
import json
import math
from pathlib import Path
import re

MAX_BYTES = 4 * 1024 * 1024
MAX_EVENTS = 20000
MAX_JSON_DEPTH = 32
MAX_TICK = 2**32 - 1
MAX_GENERATION = 2**64 - 1
MAX_WAVES = 16
MAX_RAIDERS = 4096
IDENTIFIER = re.compile(r"[A-Za-z0-9][A-Za-z0-9_./:-]{0,127}\Z")
SHA256 = re.compile(r"[a-f0-9]{64}\Z")
ATTACK_LIMIT_BY_KIND = {"melee": "max_melee", "jump": "max_jumps", "drop": "max_drops"}
EXTERNAL_CASES = {
    "paired_baseline", "gpu_snapshot", "authored_round_trip", "native_collision",
    "native_pose", "native_encounter", "stock", "listen_authority",
    "network_lifecycle", "repeat_scale",
}
TRACE_CASES = {"three_tunnels", "bounded_waves", "bounded_attacks", "giant_finale"}
LIMIT_FIELDS = {
    "max_alive_raiders", "spawn_interval_ticks", "max_active_attacks",
    "max_attack_starts_per_tick", "max_airborne_attacks", "max_melee",
    "max_jumps", "max_drops", "max_attack_ticks", "min_attack_cooldown_ticks",
    "min_telegraph_ticks",
}
PROVENANCE_FIELDS = {
    "source_tree_sha256", "client_sha256", "public_level_sha256",
    "public_scenario_sha256", "fixture_sha256", "native_log_sha256",
    "managed_recipe_sha256",
}


class ContractError(ValueError):
    pass


def require(condition, description):
    if not condition:
        raise ContractError(description)


def object_pairs(pairs):
    obj = {}
    for key, value in pairs:
        require(key not in obj, f"duplicate JSON field {key}")
        obj[key] = value
    return obj


def invalid_constant(value):
    raise ContractError(f"invalid JSON constant {value}")


def check_json_depth(text):
    depth = 0
    in_string = False
    escaped = False
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
            require(depth <= MAX_JSON_DEPTH, "JSON nesting is too deep")
        elif char in "]}":
            require(depth > 0, "unexpected JSON closing delimiter")
            depth -= 1


def load_json(path):
    with path.open("rb") as stream:
        data = stream.read(MAX_BYTES + 1)
    require(len(data) <= MAX_BYTES, f"JSON input exceeds {MAX_BYTES} bytes")
    text = data.decode("utf-8-sig")
    check_json_depth(text)
    return json.loads(text, object_pairs_hook=object_pairs, parse_constant=invalid_constant)


def mapping(value, name):
    require(isinstance(value, dict), f"{name} must be an object")
    return value


def sequence(value, name, low, high):
    require(isinstance(value, list) and low <= len(value) <= high,
            f"{name} must have {low} to {high} rows")
    return value


def field_id(obj, key):
    value = obj.get(key)
    require(isinstance(value, str) and IDENTIFIER.fullmatch(value), f"invalid {key}")
    return value


def uint(obj, key, low=0, high=MAX_TICK):
    value = obj.get(key)
    require(type(value) is int and low <= value <= high, f"invalid {key}")
    return value


def finite(obj, key):
    value = obj.get(key)
    require(type(value) in (int, float) and -1e8 <= value <= 1e8, f"invalid {key}")
    return value


def positive(obj, key):
    value = finite(obj, key)
    require(value > 0, f"{key} must be positive")
    return value


def unique_ids(rows, key):
    items = [field_id(mapping(row, key), key) for row in rows]
    require(len(items) == len(set(items)), f"duplicate {key}")
    return items


def authored_attack_reach(value, name):
    reaches = mapping(value, name)
    require(reaches and set(reaches) <= set(ATTACK_LIMIT_BY_KIND),
            f"{name} needs at least one known attack kind")
    return {kind: positive(reaches, kind) for kind in reaches}


def read_plan(plan):
    mapping(plan, "authored plan")
    tunnels = sequence(plan.get("tunnels"), "tunnels", 3, 3)
    tunnel_ids = unique_ids(tunnels, "id")
    unique_ids(tunnels, "portal")
    unique_ids(tunnels, "blocker")
    waves = sequence(plan.get("waves"), "waves", 1, MAX_WAVES)
    wave_ids = unique_ids(waves, "id")
    wave_specs = {}
    for wave_id, wave in zip(wave_ids, waves, strict=True):
        routes = sequence(wave.get("tunnels"), f"{wave_id} tunnels", 1, 3)
        require(
            all(isinstance(route, str) and route in tunnel_ids for route in routes)
            and len(routes) == len(set(routes)),
            f"invalid authored routes for wave {wave_id}",
        )
        wave_specs[wave_id] = (
            uint(wave, "max_spawns", 1, 65535), frozenset(routes), uint(wave, "delay_ticks"),
        )
    limits = mapping(plan.get("limits"), "limits")
    require(set(limits) == LIMIT_FIELDS, "authored limits are incomplete or unknown")
    for key in LIMIT_FIELDS:
        uint(limits, key, 1)
    active_limit = limits["max_active_attacks"]
    require(
        active_limit <= limits["max_alive_raiders"] <= MAX_RAIDERS
        and limits["max_airborne_attacks"] <= active_limit
        and limits["max_attack_starts_per_tick"] <= active_limit
        and all(limits[key] <= active_limit for key in ATTACK_LIMIT_BY_KIND.values()),
        "inconsistent attack or raider limits",
    )
    require(limits["min_telegraph_ticks"] < limits["max_attack_ticks"],
            "telegraph must end before attack expiry")
    giant = mapping(plan.get("giant"), "Giant specification")
    body = field_id(giant, "body_ref")
    require(":" in body, "Giant body must be a catalog ID")
    return (
        dict(zip(tunnel_ids, tunnels, strict=True)),
        wave_specs,
        limits,
        (body, field_id(giant, "spawn_pad"), field_id(giant, "room")),
        {
            "raider": authored_attack_reach(plan.get("raider_attack_reach"), "raider attack reach"),
            "giant": authored_attack_reach(giant.get("attack_reach"), "Giant attack reach"),
        },
    )


@dataclass
class Actor:
    generation: int
    role: str
    radius: float
    height: float
    scale: float
    health: float
    spawned_tick: int


@dataclass
class Attack:
    actor: str
    generation: int
    target: str
    target_generation: int
    kind: str
    start_tick: int
    launch_support: str | None
    hit: bool = False


class EncounterReplay:
    def __init__(self, trace):
        self.level = field_id(trace, "level_id")
        self.scenario = field_id(trace, "scenario_id")
        self.epoch = uint(trace, "stage_epoch", 1, MAX_GENERATION)
        self.tunnels, self.waves, self.limits, self.giant_source, self.attack_reach = read_plan(trace.get("plan"))
        self.wave_order = iter(self.waves)
        self.current_wave = None
        self.wave_spawns = 0
        self.waves_finished = 0
        self.wave_clock_start = 0
        self.last_spawn_tick = None
        self.closed = set()
        self.closed_tick = {}
        self.players = {}
        self.player_generations = {}
        self.open_crossings = Counter()
        self.blocked_crossings = Counter()
        self.actors = {}
        self.generations = {}
        self.attacks = {}
        self.attackers = set()
        self.active_kinds = Counter()
        self.attack_start_tick = -1
        self.starts_this_tick = 0
        self.attack_ids = set()
        self.last_attack = {}
        self.raider_sizes = (0, 0, 0)
        self.landings = set()
        self.ceiling_drop_landed = False
        self.hits = 0
        self.raider_player_kills = 0
        self.finale = False
        self.giant = None
        self.giant_attacked = False
        self.giant_defeated = False
        self.complete = False
        self.tick = 0

    def live_actor(self, row):
        key = field_id(row, "actor")
        generation = uint(row, "generation", 1, MAX_GENERATION)
        actor = self.actors.get(key)
        require(actor is not None and actor.generation == generation,
                f"stale or absent actor {key} generation {generation}")
        return key, actor

    def spawn(self, row, role, health):
        key = field_id(row, "actor")
        generation = uint(row, "generation", 1, MAX_GENERATION)
        require(key not in self.actors and generation > self.generations.get(key, 0),
                f"duplicate or stale spawn {key} generation {generation}")
        actor = Actor(generation, role, positive(row, "radius"), positive(row, "height"),
                      positive(row, "scale"), health, self.tick)
        require(actor.height >= 2 * actor.radius, "actor height cannot contain its capsule radius")
        self.actors[key] = actor
        self.generations[key] = generation
        return key, actor

    def encounter_start(self, row):
        require(row.get("authority") in ("local", "listen_host"),
                "trace must come from the local or listen-host authority")
        require(row.get("level_id") == self.level and row.get("scenario_id") == self.scenario,
                "runtime encounter identity differs from the trace envelope")
        self.wave_clock_start = self.tick

    def player_spawn(self, row):
        key = field_id(row, "player")
        generation = uint(row, "player_generation", 1, MAX_GENERATION)
        require(key not in self.players and generation > self.player_generations.get(key, 0),
                "duplicate or stale player incarnation")
        self.players[key] = generation
        self.player_generations[key] = generation

    def require_live_player(self, row, key_field, generation_field):
        key = field_id(row, key_field)
        generation = uint(row, generation_field, 1, MAX_GENERATION)
        require(self.players.get(key) == generation, "observed player generation is absent or stale")

    def player_death(self, row):
        self.require_live_player(row, "player", "player_generation")
        del self.players[row["player"]]

    def wave_start(self, row):
        require(not self.finale and len(self.closed) < 3 and self.current_wave is None and not self.actors,
                "wave started after closure or before the prior wave drained")
        wave = field_id(row, "wave")
        require(wave == next(self.wave_order, None), "wave started outside the authored order")
        _, _, delay_ticks = self.waves[wave]
        require(self.tick >= min(MAX_TICK, self.wave_clock_start + delay_ticks),
                "authored wave delay was skipped")
        self.current_wave = wave
        self.wave_spawns = 0

    def wave_end(self, row):
        require(field_id(row, "wave") == self.current_wave, "wave ended out of order")
        reason = row.get("reason")
        max_spawns, routes, _ = self.waves[self.current_wave]
        if reason == "all_tunnels_closed":
            require(len(self.closed) == 3, "wave cancelled before all tunnels closed")
        elif reason == "routes_closed":
            require(routes <= self.closed and not self.actors,
                    "a wave with no open routes must drain before advancing")
        else:
            require(reason == "finished", "unknown wave end reason")
            require(self.wave_spawns == max_spawns and not self.actors,
                    "wave finished before its spawn budget and raider drain")
        self.current_wave = None
        self.wave_clock_start = self.tick
        self.waves_finished += 1

    def raider_spawn(self, row):
        tunnel = field_id(row, "tunnel")
        require(tunnel in self.tunnels and tunnel not in self.closed, "raider spawned through a closed or unknown tunnel")
        require(not self.finale and field_id(row, "wave") == self.current_wave, "raider spawned outside its active wave")
        max_spawns, routes, _ = self.waves[self.current_wave]
        require(tunnel in routes, "spawn route is outside the authored wave")
        require(self.wave_spawns < max_spawns, "authored wave spawn budget exceeded")
        require(len(self.actors) < self.limits["max_alive_raiders"],
                "active raider limit exceeded")
        require(self.last_spawn_tick is None or (
            self.tick - self.last_spawn_tick >= self.limits["spawn_interval_ticks"]),
            "spawn interval was skipped")
        _, actor = self.spawn(row, "raider", 0)
        self.raider_sizes = tuple(max(a, b) for a, b in zip(
            self.raider_sizes, (actor.radius, actor.height, actor.scale), strict=True))
        self.last_spawn_tick = self.tick
        self.wave_spawns += 1

    def tunnel(self, row):
        tunnel = field_id(row, "tunnel")
        require(tunnel in self.tunnels, f"unknown tunnel {tunnel}")
        authored = self.tunnels[tunnel]
        require(field_id(row, "portal") == authored["portal"], f"portal differs for tunnel {tunnel}")
        return tunnel, authored

    def tunnel_close(self, row):
        tunnel, authored = self.tunnel(row)
        require(tunnel not in self.closed and not self.finale, f"duplicate or late closure for {tunnel}")
        require(row.get("source") == "player_interact", "closure must follow normal player input")
        self.require_live_player(row, "player", "player_generation")
        require(field_id(row, "blocker") == authored["blocker"], f"wrong closure blocker for {tunnel}")
        require(row.get("collision_enabled") is True, "closure lacks committed blocker collision")
        self.closed.add(tunnel)
        self.closed_tick[tunnel] = self.tick

    def tunnel_passage(self, row):
        tunnel, authored = self.tunnel(row)
        _, actor = self.live_actor(row)
        require(actor.role == "raider", "tunnel passage witness must be a live raider")
        start = finite(row, "start_side")
        desired = finite(row, "requested_side")
        resolved = finite(row, "resolved_side")
        # An episode may accumulate several logged native sweeps. Its capsule
        # extent along the portal normal is radius plus projected half-segment.
        episode_start = uint(row, "episode_start_tick", actor.spawned_tick, self.tick)
        uint(row, "sweep_count", 1, self.tick - episode_start + 1)
        axes = tuple(abs(finite(row, f"{endpoint}_axis_dot")) for endpoint in ("start", "requested", "resolved"))
        require(all(dot <= 1 for dot in axes), "capsule axis projection is outside [-1, 1]")
        half_segment = actor.height * 0.5 - actor.radius
        start_extent, desired_extent, resolved_extent = (
            actor.radius + half_segment * dot for dot in axes
        )
        require(start <= -start_extent and desired >= desired_extent,
                "passage episode must request an exterior-to-interior capsule crossing")
        if tunnel in self.closed:
            require(episode_start >= self.closed_tick[tunnel], "blocked episode started before closure")
            require(
                resolved <= -resolved_extent and row.get("hit_blocker") == authored["blocker"],
                f"closed tunnel {tunnel} admitted a body or lacks its blocker collision",
            )
            self.blocked_crossings[tunnel] += 1
        else:
            require(resolved >= resolved_extent and row.get("hit_blocker") is None,
                    f"open tunnel {tunnel} lacks a completed body crossing")
            self.open_crossings[tunnel] += 1

    def check_target(self, row, role, kind):
        require(uint(row, "target_check_tick") == self.tick and row.get("target_alive") is True
                and row.get("line_of_sight") is True and row.get("hostile") is True,
                "attack lacks a fresh live hostile position and line of sight")
        self.require_live_player(row, "target", "target_generation")
        reaches = self.attack_reach[role]
        reach = positive(row, "validated_reach")
        require(kind in reaches and reach <= reaches[kind], "reported reach exceeded the authored attack limit")
        require(0 <= finite(row, "distance") <= reach, "attack exceeded its validated reach")

    def attack_start(self, row):
        key, actor = self.live_actor(row)
        require(key not in self.attackers,
                "actor started a second active attack")
        previous = self.last_attack.get((key, actor.generation))
        require(previous is None or self.tick - previous >= self.limits["min_attack_cooldown_ticks"],
                "per-actor attack cooldown violated")
        attack_id = uint(row, "attack_id", 1)
        require(attack_id not in self.attack_ids, "duplicate attack identity")
        target = field_id(row, "target")
        target_generation = uint(row, "target_generation", 1, MAX_GENERATION)
        require(target != key, "actor selected itself as a target")
        kind = row.get("kind")
        require(isinstance(kind, str) and kind in ATTACK_LIMIT_BY_KIND, "unknown attack kind")
        self.check_target(row, actor.role, kind)
        require(len(self.attacks) < self.limits["max_active_attacks"], "active attack limit exceeded")
        if kind in ("jump", "drop"):
            require(self.active_kinds["jump"] + self.active_kinds["drop"] < self.limits["max_airborne_attacks"],
                    "airborne attack limit exceeded")
        require(self.active_kinds[kind] < self.limits[ATTACK_LIMIT_BY_KIND[kind]],
                f"concurrent {kind} limit exceeded")
        if self.tick != self.attack_start_tick:
            self.attack_start_tick = self.tick
            self.starts_this_tick = 0
        require(self.starts_this_tick < self.limits["max_attack_starts_per_tick"],
                "per-tick attack start limit exceeded")
        launch_support = row.get("launch_support")
        if kind == "drop":
            require(launch_support in ("wall", "ceiling"), "drop lacks a valid elevated support")
        self.attack_ids.add(attack_id)
        self.last_attack[(key, actor.generation)] = self.tick
        self.attacks[attack_id] = Attack(key, actor.generation, target, target_generation, kind, self.tick, launch_support)
        self.attackers.add(key)
        self.active_kinds[kind] += 1
        self.starts_this_tick += 1

    def active_attack(self, row):
        key, actor = self.live_actor(row)
        attack_id = uint(row, "attack_id", 1)
        attack = self.attacks.get(attack_id)
        require(attack is not None and (attack.actor, attack.generation) == (key, actor.generation),
                "attack result lacks a matching live actor and attack")
        return attack_id, attack

    def attack_hit(self, row):
        _, attack = self.active_attack(row)
        require(not attack.hit, "attack registered duplicate damage")
        age = self.tick - attack.start_tick
        require(self.limits["min_telegraph_ticks"] <= age, "attack impact arrived before its configured telegraph")
        require(age < self.limits["max_attack_ticks"], "attack impact exceeded its duration limit")
        require(field_id(row, "target") == attack.target
                and uint(row, "target_generation", 1, MAX_GENERATION) == attack.target_generation,
                "attack hit stale or different target identity")
        self.check_target(row, self.actors[attack.actor].role, attack.kind)
        positive(row, "amount")
        attack.hit = True
        if (attack.actor, attack.generation) == self.giant:
            self.giant_attacked = True
        self.hits += 1

    def attack_end(self, row):
        attack_id, attack = self.active_attack(row)
        result = row.get("result")
        if attack.kind in ("jump", "drop"):
            require(result in ("landed", "cancelled"), "airborne attack has no terminal transition")
            if result == "landed":
                require(self.tick > attack.start_tick, "airborne attack landed at its start tick")
                self.landings.add(attack.kind)
                if attack.kind == "drop" and attack.launch_support == "ceiling":
                    self.ceiling_drop_landed = True
        else:
            require(result in ("resolved", "cancelled"), "attack has no terminal transition")
        del self.attacks[attack_id]
        self.attackers.remove(attack.actor)
        self.active_kinds[attack.kind] -= 1

    def actor_death(self, row):
        key, actor = self.live_actor(row)
        cause = row.get("cause")
        require(cause in ("player_weapon", "native_environment"), "actor death bypassed combat")
        require(key not in self.attackers, "actor retired with a live attack")
        if actor.role == "giant":
            require(cause == "player_weapon" and actor.health == 0 and self.giant_attacked,
                    "Giant death lacks combat and zero health")
            self.giant_defeated = True
        elif cause == "player_weapon":
            self.raider_player_kills += 1
        del self.actors[key]

    def finale_start(self, row):
        require(not self.finale and len(self.closed) == 3 and self.current_wave is None
                and self.waves_finished > 0 and not self.actors and not self.attacks,
                "finale began before three closures and raider drain")
        self.finale = True

    def giant_spawn(self, row):
        require(self.finale and self.giant is None, "Giant spawned early or more than once")
        require(
            tuple(field_id(row, field) for field in ("body_ref", "spawn_pad", "room")) == self.giant_source,
            "Giant body or spawn differs from the authored catalog source",
        )
        health = positive(row, "health")
        key, actor = self.spawn(row, "giant", health)
        require(all(actual > normal for actual, normal in zip(
            (actor.radius, actor.height, actor.scale), self.raider_sizes, strict=True)),
            "Giant collider and model scale must exceed the raider envelope")
        require(positive(row, "clearance_radius") >= actor.radius
                and positive(row, "clearance_height") >= actor.height,
                "Giant spawn clearance is smaller than its scaled collider")
        self.giant = (key, actor.generation)

    def giant_damage(self, row):
        key, actor = self.live_actor(row)
        require(self.giant == (key, actor.generation) and actor.health > 0
                and row.get("source") == "player_weapon", "Giant damage bypassed live player combat")
        self.require_live_player(row, "player", "player_generation")
        remaining = max(0.0, actor.health - positive(row, "amount"))
        require(math.isclose(finite(row, "remaining_health"), remaining, abs_tol=1e-4),
                "Giant health does not match damage taken")
        actor.health = remaining

    def encounter_complete(self, row):
        require(row.get("result") == "victory" and self.giant_defeated
                and not self.actors and not self.attacks, "encounter completed before Giant defeat")
        self.complete = True

    def run(self, rows):
        handlers = {
            "encounter_start": self.encounter_start,
            "player_spawn": self.player_spawn,
            "player_death": self.player_death,
            "wave_start": self.wave_start,
            "wave_end": self.wave_end,
            "raider_spawn": self.raider_spawn,
            "tunnel_close": self.tunnel_close,
            "tunnel_passage": self.tunnel_passage,
            "attack_start": self.attack_start,
            "attack_hit": self.attack_hit,
            "attack_end": self.attack_end,
            "actor_death": self.actor_death,
            "finale_start": self.finale_start,
            "giant_spawn": self.giant_spawn,
            "giant_damage": self.giant_damage,
            "encounter_complete": self.encounter_complete,
        }
        for ordinal, row in enumerate(sequence(rows, "events", 1, MAX_EVENTS), 1):
            try:
                mapping(row, "event")
                require(uint(row, "seq", 1) == ordinal, "event sequence is missing or reordered")
                require(uint(row, "stage_epoch", 1, MAX_GENERATION) == self.epoch,
                        "event belongs to a stale stage epoch")
                tick = uint(row, "tick")
                require(tick >= self.tick and not self.complete, "tick regressed or arrived after completion")
                self.tick = tick
                oldest = next(iter(self.attacks.values()), None)
                require(oldest is None or tick - oldest.start_tick <= self.limits["max_attack_ticks"],
                        "active attack exceeded its duration limit")
                kind = field_id(row, "type")
                require((ordinal == 1) == (kind == "encounter_start"), "encounter start is missing or repeated")
                require(kind in handlers, f"unknown event {kind}")
                handlers[kind](row)
            except ContractError as exc:
                raise ContractError(f"event {ordinal}: {exc}") from exc

        require(self.complete, "natural victory and terminal encounter result missing")
        require(set(self.open_crossings) == set(self.tunnels), "missing open body crossing for a tunnel")
        require(set(self.blocked_crossings) == set(self.tunnels), "missing blocked swept-body witness for a tunnel")
        require(self.raider_player_kills > 0, "missing a raider defeated in player combat")
        require("jump" in self.landings and self.ceiling_drop_landed,
                "missing a completed jump or ceiling drop")
        require(self.hits > 0, "missing a validated attack impact")
        return {
            "stage_epoch": self.epoch,
            "tunnels_open_crossed": dict(self.open_crossings),
            "tunnels_closed_blocked": dict(self.blocked_crossings),
            "waves_finished": self.waves_finished,
            "raider_player_kills": self.raider_player_kills,
            "airborne_landings": sorted(self.landings),
            "validated_attack_hits": self.hits,
            "giant_actor": list(self.giant),
        }


def verify_matrix(matrix):
    mapping(matrix, "acceptance matrix")
    require(type(matrix.get("schema")) is int and matrix["schema"] == 1, "unsupported matrix schema")
    cases = sequence(matrix.get("cases"), "acceptance cases",
                     len(TRACE_CASES) + len(EXTERNAL_CASES), len(TRACE_CASES) + len(EXTERNAL_CASES))
    ids = unique_ids(cases, "id")
    require(set(ids) == TRACE_CASES | EXTERNAL_CASES, "acceptance matrix coverage differs")
    for case in cases:
        source = "trace" if case["id"] in TRACE_CASES else "native_evidence"
        require(case.get("source") == source, f"case {case['id']} has wrong evidence source")
        for key in ("run", "retain", "accept"):
            require(isinstance(case.get(key), str) and case[key].strip(),
                    f"case {case['id']} lacks {key}")
    return cases


def verify(trace, matrix, *, native_candidate=False):
    cases = verify_matrix(matrix)
    mapping(trace, "encounter trace")
    require(type(trace.get("schema")) is int and trace["schema"] == 1, "unsupported trace schema")
    origin = trace.get("origin")
    require(origin in ("synthetic", "native_candidate"), "trace origin must be explicit")
    if native_candidate:
        require(origin == "native_candidate", "synthetic trace cannot enter the native evidence route")
    if origin == "native_candidate":
        provenance = mapping(trace.get("provenance"), "native provenance record")
        for key in PROVENANCE_FIELDS:
            value = provenance.get(key)
            require(isinstance(value, str) and SHA256.fullmatch(value), f"invalid native provenance digest {key}")
        field_id(provenance, "run_id")
        field_id(provenance, "queue_id")
    summary = EncounterReplay(trace).run(trace.get("events"))
    return {
        "verdict": "contract_pass",
        "origin": origin,
        "native_qualification": "unverified",
        "native_gameplay_verified": False,
        "trace_summary": summary,
        "cases": [
            {
                "id": case["id"],
                "verdict": "contract_pass" if case["source"] == "trace" else "requires_native_evidence",
            }
            for case in cases
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--matrix", type=Path, default=Path(__file__).with_name("fixtures")
                        / "skedar_swarm_encounter_acceptance.json")
    parser.add_argument("--native-candidate", action="store_true",
                        help="Require native candidate provenance fields for later independent evidence review")
    options = parser.parse_args()
    try:
        result = verify(load_json(options.trace), load_json(options.matrix),
                        native_candidate=options.native_candidate)
    except (OSError, ValueError, OverflowError) as error:
        print(json.dumps({"verdict": "invalid_input", "reason": str(error),
                          "native_gameplay_verified": False}))
        return 1
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
