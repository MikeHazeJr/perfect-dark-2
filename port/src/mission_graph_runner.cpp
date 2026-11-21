// Mission graph runner (decision 2, CORE, 2026-11-15). See
// port/include/mission_graph_runner.h for the contract.
#include <algorithm>
#include "mission_graph_runner.h"

#include "modasset_gltf_document.h"
#include "../external/imgui-node-editor/crude_json.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

enum NodeType { NODE_DATA, NODE_EVENT, NODE_EVENT_INERT, NODE_GATE, NODE_MODULE };

// Which graph a program comes from: a mission graph keeps its data nodes as
// structure; a level graph, a projectile or entity graph and a held weapon
// function graph skip them; a projectile program has the projectile events
// only (projectile-events.md slice 2) and a weapon program the weapon events
// only (held-weapon-events.md slice 1).
enum Domain { DOMAIN_MISSION, DOMAIN_LEVEL, DOMAIN_PROJECTILE, DOMAIN_WEAPON };

} // namespace

// object-events.md slice 3: a node's scalar fields as text.
struct mission_graph_params {
	std::vector<std::pair<std::string, std::string>> values;
};

namespace {
// A node leaves by one of these. PORT_GATE_FAILED and the four outcomes are a
// weapon program's (held-weapon-events.md slice 1): a gate that cannot be
// answered, and how an asynchronous module's work ended.
enum Port {
	PORT_OUT, PORT_PASS, PORT_FAIL, PORT_GATE_FAILED,
	PORT_COMPLETED, PORT_BLOCKED, PORT_OUTCOME_FAILED, PORT_CANCELLED,
};

const char *portName(Port port)
{
	switch (port) {
	case PORT_OUT: return "out";
	case PORT_PASS: return "pass";
	case PORT_FAIL: return "fail";
	case PORT_GATE_FAILED: return "failed";
	case PORT_COMPLETED: return "completed";
	case PORT_BLOCKED: return "blocked";
	case PORT_OUTCOME_FAILED: return "failed";
	case PORT_CANCELLED: return "cancelled";
	}
	return "out";
}

struct Edge {
	int target;
	Port port;
};

struct Node {
	std::string id;
	std::string kind;
	NodeType type = NODE_DATA;
	int event_kind = 0;
	std::string objective; // "" means any objective
	int gate_status = -1;
	int gate_phase = -1; // gate.mission.phase; -1 for an objective gate
	std::string volume; // event.volume.*: the volumes.json volume_id
	std::string object; // object events: the record_id filter; "" for any
	bool native_gate = false; // a gate.* the module table evaluates
	bool async = false; // a weapon program's module that finishes later
	mission_graph_params params;
	std::vector<Edge> out;
};

const char *const kObjectiveStatusEvent = "event.objective.status_changed";
const char *const kMissionCompletedEvent = "event.mission.completed";
const char *const kMissionContinueEvent = "event.mission.continue";
const char *const kObjectiveStatusGate = "gate.objective.status";
const char *const kMissionPhaseEvent = "event.mission.phase_changed";
const char *const kMissionPhaseGate = "gate.mission.phase";
const char *const kVolumeEnteredEvent = "event.volume.entered";
const char *const kVolumeLeftEvent = "event.volume.left";
const char *const kPickupCollectedEvent = "event.pickup.collected";
const char *const kDoorOpenedEvent = "event.door.opened";
const char *const kDoorClosedEvent = "event.door.closed";
const char *const kObjectUsedEvent = "event.object.used";
const char *const kPickupTouchedEvent = "event.pickup.touched";
const char *const kDoorUseEvent = "event.door.use";
const char *const kDoorApproachEvent = "event.door.approach";
const char *const kLiftCalledEvent = "event.lift.called";
const char *const kLiftIdleEvent = "event.lift.idle";
const char *const kLiftArrivedEvent = "event.lift.arrived";
const char *const kGlassDamagedEvent = "event.glass.damaged";
const char *const kGlassBrokenEvent = "event.glass.broken";

// Object events and the rule events carry an object and may filter one.
bool isObjectEventKind(s32 kind)
{
	return kind >= MISSION_GRAPH_EVENT_PICKUP_COLLECTED && kind <= MISSION_GRAPH_EVENT_GLASS_BROKEN;
}

// The object event kinds by name, in kind order (slices 2, 4 and 5).
const struct {
	const char *name;
	s32 kind;
} kObjectEventKinds[] = {
	{ kPickupCollectedEvent, MISSION_GRAPH_EVENT_PICKUP_COLLECTED },
	{ kDoorOpenedEvent, MISSION_GRAPH_EVENT_DOOR_OPENED },
	{ kDoorClosedEvent, MISSION_GRAPH_EVENT_DOOR_CLOSED },
	{ kObjectUsedEvent, MISSION_GRAPH_EVENT_OBJECT_USED },
	{ kPickupTouchedEvent, MISSION_GRAPH_EVENT_PICKUP_TOUCHED },
	{ kDoorUseEvent, MISSION_GRAPH_EVENT_DOOR_USE },
	{ kDoorApproachEvent, MISSION_GRAPH_EVENT_DOOR_APPROACH },
	{ kLiftCalledEvent, MISSION_GRAPH_EVENT_LIFT_CALLED },
	{ kLiftIdleEvent, MISSION_GRAPH_EVENT_LIFT_IDLE },
	{ kLiftArrivedEvent, MISSION_GRAPH_EVENT_LIFT_ARRIVED },
	{ kGlassDamagedEvent, MISSION_GRAPH_EVENT_GLASS_DAMAGED },
	{ kGlassBrokenEvent, MISSION_GRAPH_EVENT_GLASS_BROKEN },
};

// The object event kind of a node kind, or 0 for none.
s32 objectEventKindOf(const std::string &kind)
{
	for (const auto &entry : kObjectEventKinds) {
		if (kind == entry.name) return entry.kind;
	}
	return 0;
}

// The projectile events by name (projectile-events.md slice 2).
const struct {
	const char *name;
	s32 kind;
} kProjectileEventKinds[] = {
	{ "event.projectile.spawned", MISSION_GRAPH_EVENT_PROJECTILE_SPAWNED },
	{ "event.projectile.bounced", MISSION_GRAPH_EVENT_PROJECTILE_BOUNCED },
	{ "event.projectile.hit_target", MISSION_GRAPH_EVENT_PROJECTILE_HIT_TARGET },
	{ "event.projectile.landed", MISSION_GRAPH_EVENT_PROJECTILE_LANDED },
	{ "event.projectile.ended", MISSION_GRAPH_EVENT_PROJECTILE_ENDED },
	{ "event.projectile.detonated", MISSION_GRAPH_EVENT_PROJECTILE_DETONATED },
	{ "event.projectile.timer_expired", MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED },
	// Slice 3: the contact rule events.
	{ "event.projectile.contact", MISSION_GRAPH_EVENT_PROJECTILE_CONTACT },
	{ "event.projectile.launch_blocked", MISSION_GRAPH_EVENT_PROJECTILE_LAUNCH_BLOCKED },
	{ "event.projectile.came_to_rest", MISSION_GRAPH_EVENT_PROJECTILE_CAME_TO_REST },
	// Slice 4: the device rule events.
	{ "event.projectile.proximity", MISSION_GRAPH_EVENT_PROJECTILE_PROXIMITY },
	{ "event.projectile.remote_signal", MISSION_GRAPH_EVENT_PROJECTILE_REMOTE_SIGNAL },
	{ "event.projectile.damaged", MISSION_GRAPH_EVENT_PROJECTILE_DAMAGED },
	// Slice 5: the sentry rule events.
	{ "event.projectile.thrown", MISSION_GRAPH_EVENT_PROJECTILE_THROWN },
	{ "event.projectile.sentry_aimed", MISSION_GRAPH_EVENT_PROJECTILE_SENTRY_AIMED },
	// Slice 6: the arm rule event.
	{ "event.projectile.armed", MISSION_GRAPH_EVENT_PROJECTILE_ARMED },
};

// The projectile event kind of a node kind, or 0 for none.
s32 projectileEventKindOf(const std::string &kind)
{
	for (const auto &entry : kProjectileEventKinds) {
		if (kind == entry.name) return entry.kind;
	}
	return 0;
}

bool isProjectileEventKind(s32 kind)
{
	return kind >= MISSION_GRAPH_EVENT_PROJECTILE_SPAWNED &&
		kind <= MISSION_GRAPH_EVENT_PROJECTILE_ARMED;
}

// The held weapon events by name (held-weapon-events.md slice 1).
const struct {
	const char *name;
	s32 kind;
} kWeaponEventKinds[] = {
	{ "event.weapon.trigger_pressed", MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED },
	{ "event.weapon.ready_while_held", MISSION_GRAPH_EVENT_WEAPON_READY_WHILE_HELD },
	// Slice 2: the idle seam's third rule event and two records.
	{ "event.weapon.clip_empty", MISSION_GRAPH_EVENT_WEAPON_CLIP_EMPTY },
	{ "event.weapon.trigger_released", MISSION_GRAPH_EVENT_WEAPON_TRIGGER_RELEASED },
	{ "event.weapon.reload_done", MISSION_GRAPH_EVENT_WEAPON_RELOAD_DONE },
};

// The weapon event kind of a node kind, or 0 for none.
s32 weaponEventKindOf(const std::string &kind)
{
	for (const auto &entry : kWeaponEventKinds) {
		if (kind == entry.name) return entry.kind;
	}
	return 0;
}

bool isWeaponEventKind(s32 kind)
{
	return kind >= MISSION_GRAPH_EVENT_WEAPON_TRIGGER_PRESSED &&
		kind <= MISSION_GRAPH_EVENT_WEAPON_RELOAD_DONE;
}

// An event kind another kind of program owns (a weapon program refuses it):
// the mission, volume, object and projectile events.
bool isOtherProgramEvent(const std::string &kind)
{
	return kind == "event.objective.status_changed" || kind == "event.mission.completed" ||
		kind == "event.mission.continue" || kind == "event.mission.phase_changed" ||
		kind == "event.volume.entered" || kind == "event.volume.left" ||
		objectEventKindOf(kind) != 0 || projectileEventKindOf(kind) != 0;
}

// A node's scalar fields other than id and kind, as text.
void readParams(const crude_json::value &node, mission_graph_params &out)
{
	if (!node.is_object()) return;
	for (const auto &field : node.get<crude_json::object>()) {
		const std::string &key = field.first;
		const crude_json::value &value = field.second;
		if (key == "id" || key == "kind") continue;
		if (value.is_string()) {
			out.values.emplace_back(key, value.get<crude_json::string>());
		} else if (value.is_number()) {
			const double number = value.get<crude_json::number>();
			char text[48];
			if (number == static_cast<double>(static_cast<long long>(number))) {
				std::snprintf(text, sizeof(text), "%lld", static_cast<long long>(number));
			} else {
				std::snprintf(text, sizeof(text), "%.17g", number);
			}
			out.values.emplace_back(key, text);
		} else if (value.is_boolean()) {
			out.values.emplace_back(key, value.get<crude_json::boolean>() ? "true" : "false");
		}
	}
}

void setError(char *err, size_t err_cap, const std::string &text)
{
	if (err && err_cap) {
		std::snprintf(err, err_cap, "%s", text.c_str());
	}
}

bool stringField(const crude_json::value &object, const char *key, std::string &out)
{
	if (!object.contains(key)) return false;
	const crude_json::value &value = object[key];
	if (!value.is_string()) return false;
	out = value.get<crude_json::string>();
	return true;
}

int statusFromText(const std::string &text)
{
	if (text == "complete") return MISSION_GRAPH_STATUS_COMPLETE;
	if (text == "incomplete") return MISSION_GRAPH_STATUS_INCOMPLETE;
	if (text == "failed") return MISSION_GRAPH_STATUS_FAILED;
	return -1;
}

} // namespace

struct mission_graph_program {
	std::vector<Node> nodes;
	std::vector<std::string> volumes; // distinct event.volume.* volumes
	int exec_edges = 0;
	int modules = 0;
	bool weapon = false; // a weapon program (held-weapon-events.md slice 1)
};

// held-weapon-events.md slice 1: what one event reached, kept so that a walk
// that stopped at a pending module can go on from it.
struct mission_graph_activation {
	const mission_graph_program *program = nullptr;
	std::vector<char> reached;
	std::vector<int> queue;
	size_t head = 0;
	mission_graph_trace_fn trace = nullptr;
	void *trace_user = nullptr;
};

namespace {

bool isProgramKind(const std::string &kind)
{
	return kind.rfind("event.", 0) == 0 || kind.rfind("gate.", 0) == 0 || kind.rfind("og.", 0) == 0;
}

// held-weapon-events.md slice 2: a held function's record has event.* and
// gate.* nodes of its own (event.trigger_pressed, gate.cooldown_ready); its
// program is the weapon events, the weapon gates and the native modules. A
// known event of another program is kept so that it is refused by name.
bool isOtherProgramEvent(const std::string &kind);
bool isWeaponProgramKind(const std::string &kind)
{
	return kind.rfind("event.weapon.", 0) == 0 || kind.rfind("gate.weapon.", 0) == 0 ||
		kind.rfind("og.", 0) == 0 || isOtherProgramEvent(kind);
}

// level (object-events.md slice 1): the graph is a level graph, whose
// program is only its event.*, gate.* and og.* nodes; every other node is
// level data, skipped and not counted, and an edge that is not an
// execution edge may name it.
mission_graph_program_t *compileProgram(const char *json, size_t json_size,
	mission_graph_module_known_fn module_known, char *err, size_t err_cap, Domain domain)
{
	// A level graph, a projectile or entity graph and a weapon function graph
	// carry data nodes that are not the program (object-events.md slice 1,
	// projectile-events.md slice 2, held-weapon-events.md slice 1).
	const bool data_nodes = domain != DOMAIN_MISSION;
	const std::string what = domain == DOMAIN_LEVEL ? "level graph"
		: domain == DOMAIN_PROJECTILE ? "projectile graph"
		: domain == DOMAIN_WEAPON ? "weapon graph" : "mission graph";
	const std::string file = domain == DOMAIN_LEVEL ? "level.graph.json"
		: domain == DOMAIN_PROJECTILE ? "behavior.graph.json"
		: domain == DOMAIN_WEAPON ? "the weapon function graph" : "mission.graph.json";

	setError(err, err_cap, "");
	if (!json || !json_size) {
		setError(err, err_cap, "empty " + what);
		return nullptr;
	}
	try {
		crude_json::value root;
		if (!modAssetJsonReadValue(json, json_size, root) || !root.is_object()) {
			setError(err, err_cap, file + " is not one JSON object");
			return nullptr;
		}
		if (!root.contains("nodes") || !root["nodes"].is_array()) {
			setError(err, err_cap, file + " has no nodes array");
			return nullptr;
		}
		const crude_json::array &nodes = root["nodes"].get<crude_json::array>();
		if (!data_nodes && nodes.size() > MISSION_GRAPH_MAX_NODES) {
			setError(err, err_cap, "more than " + std::to_string(MISSION_GRAPH_MAX_NODES) + " nodes");
			return nullptr;
		}

		auto program = std::make_unique<mission_graph_program>();
		program->weapon = domain == DOMAIN_WEAPON;
		std::map<std::string, int> index;
		for (size_t i = 0; i < nodes.size(); i++) {
			const crude_json::value &node = nodes[i];
			Node compiled;
			if (data_nodes) {
				std::string kind;
				if (!node.is_object() || !stringField(node, "kind", kind) ||
						!(domain == DOMAIN_WEAPON ? isWeaponProgramKind(kind) : isProgramKind(kind))) {
					continue; // level, projectile or weapon record data, not the program
				}
				if (program->nodes.size() >= MISSION_GRAPH_MAX_NODES) {
					setError(err, err_cap, "more than " + std::to_string(MISSION_GRAPH_MAX_NODES) +
						" program nodes");
					return nullptr;
				}
			}
			if (!node.is_object() || !stringField(node, "id", compiled.id) || compiled.id.empty() ||
					!stringField(node, "kind", compiled.kind) || compiled.kind.empty()) {
				setError(err, err_cap, "node " + std::to_string(i) + " needs a non-empty id and kind");
				return nullptr;
			}
			if (index.count(compiled.id)) {
				setError(err, err_cap, "node id '" + compiled.id + "' is used twice");
				return nullptr;
			}
			std::string objective;
			const bool has_objective = node.contains("objective");
			if (has_objective && !stringField(node, "objective", objective)) {
				setError(err, err_cap, "node '" + compiled.id + "' objective must be a string");
				return nullptr;
			}
			if (objective == "any") objective.clear();

			// held-weapon-events.md slice 1: a weapon program has the weapon
			// events and no other program's; no other graph has them. The
			// function record's own event.* nodes (event.trigger_pressed) are
			// inert, as any unknown event kind is.
			const s32 weapon_kind = weaponEventKindOf(compiled.kind);
			if (domain == DOMAIN_WEAPON && weapon_kind == 0 &&
					(compiled.kind.rfind("event.weapon.", 0) == 0 || isOtherProgramEvent(compiled.kind))) {
				setError(err, err_cap, "event '" + compiled.id + "' (" + compiled.kind +
					") is not a weapon event");
				return nullptr;
			}
			if (weapon_kind != 0 && domain != DOMAIN_WEAPON) {
				setError(err, err_cap, "weapon event '" + compiled.id + "' (" + compiled.kind +
					") belongs to a weapon function graph");
				return nullptr;
			}

			// projectile-events.md slice 2: a projectile program has the
			// projectile events and no other; no other graph has them.
			const s32 projectile_kind = projectileEventKindOf(compiled.kind);
			if (domain == DOMAIN_PROJECTILE && compiled.kind.rfind("event.", 0) == 0 &&
					projectile_kind == 0) {
				setError(err, err_cap, "event '" + compiled.id + "' (" + compiled.kind +
					") is not a projectile event");
				return nullptr;
			}
			if (weapon_kind != 0) {
				if (has_objective) {
					setError(err, err_cap, "weapon event '" + compiled.id + "' has no objective to filter");
					return nullptr;
				}
				compiled.type = NODE_EVENT;
				compiled.event_kind = weapon_kind;
			} else if (projectile_kind != 0) {
				if (domain != DOMAIN_PROJECTILE) {
					setError(err, err_cap, "projectile event '" + compiled.id + "' (" + compiled.kind +
						") belongs to a projectile or entity graph");
					return nullptr;
				}
				if (has_objective) {
					setError(err, err_cap, "projectile event '" + compiled.id + "' has no objective to filter");
					return nullptr;
				}
				compiled.type = NODE_EVENT;
				compiled.event_kind = projectile_kind;
			} else if (compiled.kind == kObjectiveStatusEvent) {
				compiled.type = NODE_EVENT;
				compiled.event_kind = MISSION_GRAPH_EVENT_OBJECTIVE_STATUS_CHANGED;
				compiled.objective = objective;
			} else if (compiled.kind == kMissionCompletedEvent || compiled.kind == kMissionContinueEvent ||
					compiled.kind == kMissionPhaseEvent) {
				if (has_objective) {
					setError(err, err_cap, "mission event '" + compiled.id + "' has no objective to filter");
					return nullptr;
				}
				compiled.type = NODE_EVENT;
				compiled.event_kind = compiled.kind == kMissionCompletedEvent
					? MISSION_GRAPH_EVENT_MISSION_COMPLETED
					: compiled.kind == kMissionContinueEvent
						? MISSION_GRAPH_EVENT_MISSION_CONTINUE : MISSION_GRAPH_EVENT_MISSION_PHASE_CHANGED;
			} else if (compiled.kind == kVolumeEnteredEvent || compiled.kind == kVolumeLeftEvent) {
				// trigger-volume-events.md: one named volume per node.
				if (has_objective) {
					setError(err, err_cap, "volume event '" + compiled.id + "' has no objective to filter");
					return nullptr;
				}
				if (!stringField(node, "volume", compiled.volume) || compiled.volume.empty() ||
						compiled.volume == "any") {
					setError(err, err_cap, "volume event '" + compiled.id +
						"' needs \"volume\": the volume_id of a volumes.json row");
					return nullptr;
				}
				compiled.type = NODE_EVENT;
				compiled.event_kind = compiled.kind == kVolumeEnteredEvent
					? MISSION_GRAPH_EVENT_VOLUME_ENTERED : MISSION_GRAPH_EVENT_VOLUME_LEFT;
				if (std::find(program->volumes.begin(), program->volumes.end(), compiled.volume) ==
						program->volumes.end()) {
					if (program->volumes.size() >= MISSION_GRAPH_MAX_VOLUMES) {
						setError(err, err_cap, "more than " + std::to_string(MISSION_GRAPH_MAX_VOLUMES) +
							" volumes in volume events");
						return nullptr;
					}
					program->volumes.push_back(compiled.volume);
				}
			} else if (objectEventKindOf(compiled.kind) != 0) {
				// object-events.md slices 2, 4 and 5: one object (its record_id) or any.
				if (has_objective) {
					setError(err, err_cap, "object event '" + compiled.id + "' has no objective to filter");
					return nullptr;
				}
				if (node.contains("object") && !stringField(node, "object", compiled.object)) {
					setError(err, err_cap, "object event '" + compiled.id +
						"' object must be a string: a record_id of objects.json, or \"any\"");
					return nullptr;
				}
				if (compiled.object == "any") compiled.object.clear();
				compiled.type = NODE_EVENT;
				compiled.event_kind = objectEventKindOf(compiled.kind);
				// slice 4: the level's own rules; a mission graph does not take them.
				if (domain == DOMAIN_MISSION && missionGraphEventIsObjectRule(compiled.event_kind)) {
					setError(err, err_cap, "rule event '" + compiled.id + "' (" + compiled.kind +
						") belongs to the level graph, not the mission graph");
					return nullptr;
				}
			} else if (compiled.kind.rfind("event.", 0) == 0) {
				compiled.type = NODE_EVENT_INERT;
			} else if (compiled.kind == kObjectiveStatusGate) {
				std::string status;
				if (!stringField(node, "status", status) || statusFromText(status) < 0) {
					setError(err, err_cap, "gate '" + compiled.id +
						"' needs status \"complete\", \"incomplete\" or \"failed\"");
					return nullptr;
				}
				compiled.type = NODE_GATE;
				compiled.gate_status = statusFromText(status);
				compiled.objective = objective;
			} else if (compiled.kind == kMissionPhaseGate) {
				std::string phase;
				if (has_objective) {
					setError(err, err_cap, "gate '" + compiled.id + "' is a phase gate; it has no objective to filter");
					return nullptr;
				}
				if (!stringField(node, "phase", phase) || missionGraphPhaseFromText(phase.c_str()) < 0) {
					setError(err, err_cap, "gate '" + compiled.id +
						"' needs phase \"active\", \"complete\" or \"failed\"");
					return nullptr;
				}
				compiled.type = NODE_GATE;
				compiled.gate_phase = missionGraphPhaseFromText(phase.c_str());
			} else if (compiled.kind.rfind("gate.", 0) == 0) {
				// object-events.md slice 3: a gate the module table evaluates.
				if (!module_known || !module_known(compiled.kind.c_str())) {
					setError(err, err_cap, "gate kind '" + compiled.kind + "' is not executable");
					return nullptr;
				}
				compiled.type = NODE_GATE;
				compiled.native_gate = true;
			} else if (compiled.kind.rfind("og.", 0) == 0) {
				const s32 known = module_known ? module_known(compiled.kind.c_str()) : 0;
				if (!known) {
					setError(err, err_cap, "native module '" + compiled.kind + "' does not exist");
					return nullptr;
				}
				compiled.type = NODE_MODULE;
				// held-weapon-events.md slice 1: a weapon program's module may
				// finish later than the walk that reached it.
				compiled.async = domain == DOMAIN_WEAPON && known == MISSION_GRAPH_MODULE_ASYNC;
				program->modules++;
			}
			if (compiled.type != NODE_DATA) {
				readParams(node, compiled.params);
			}
			index[compiled.id] = static_cast<int>(program->nodes.size());
			program->nodes.push_back(std::move(compiled));
		}

		if (root.contains("edges")) {
			if (!root["edges"].is_array()) {
				setError(err, err_cap, "edges must be an array");
				return nullptr;
			}
			const crude_json::array &edges = root["edges"].get<crude_json::array>();
			if (edges.size() > MISSION_GRAPH_MAX_EDGES) {
				setError(err, err_cap, "more than " + std::to_string(MISSION_GRAPH_MAX_EDGES) + " edges");
				return nullptr;
			}
			for (size_t i = 0; i < edges.size(); i++) {
				const crude_json::value &edge = edges[i];
				std::string from, to, input, output;
				const std::string where = "edge " + std::to_string(i);
				if (!edge.is_object() || !stringField(edge, "from", from) || !stringField(edge, "to", to)) {
					setError(err, err_cap, where + " needs from and to");
					return nullptr;
				}
				auto source = index.find(from);
				auto target = index.find(to);
				if (data_nodes && !edge.contains("input") &&
						(source == index.end() || target == index.end())) {
					continue; // structural edge of the level or projectile data
				}
				if (source == index.end() || target == index.end()) {
					setError(err, err_cap, where + " names a missing node ('" +
						(source == index.end() ? from : to) + "')");
					return nullptr;
				}
				const bool has_input = edge.contains("input");
				if (has_input && (!stringField(edge, "input", input) || input != "exec")) {
					setError(err, err_cap, where + " input must be \"exec\"");
					return nullptr;
				}
				const bool has_output = edge.contains("output");
				if (has_output && !stringField(edge, "output", output)) {
					setError(err, err_cap, where + " output must be a string");
					return nullptr;
				}
				if (!has_input) {
					if (has_output) {
						setError(err, err_cap, where + " has an output but is not an execution edge");
						return nullptr;
					}
					continue; // structural edge: data, never walked
				}
				Node &from_node = program->nodes[source->second];
				const Node &to_node = program->nodes[target->second];
				if (from_node.type == NODE_DATA || from_node.type == NODE_EVENT_INERT) {
					setError(err, err_cap, where + " starts at '" + from + "' (" + from_node.kind +
						"), which cannot execute");
					return nullptr;
				}
				if (to_node.type != NODE_GATE && to_node.type != NODE_MODULE) {
					setError(err, err_cap, where + " ends at '" + to + "' (" + to_node.kind +
						"), which is not a gate or a native module");
					return nullptr;
				}
				Port port = PORT_OUT;
				if (from_node.type == NODE_GATE) {
					if (output == "pass") port = PORT_PASS;
					else if (output == "fail") port = PORT_FAIL;
					else if (domain == DOMAIN_WEAPON && output == "failed") port = PORT_GATE_FAILED;
					else if (domain == DOMAIN_WEAPON) {
						setError(err, err_cap, where + " leaves gate '" + from +
							"' and needs output \"pass\", \"fail\" or \"failed\"");
						return nullptr;
					} else {
						setError(err, err_cap, where + " leaves gate '" + from +
							"' and needs output \"pass\" or \"fail\"");
						return nullptr;
					}
				} else if (from_node.async) {
					// held-weapon-events.md slice 1: a module that finishes
					// later leaves by how its work ended.
					if (output == "completed") port = PORT_COMPLETED;
					else if (output == "blocked") port = PORT_BLOCKED;
					else if (output == "failed") port = PORT_OUTCOME_FAILED;
					else if (output == "cancelled") port = PORT_CANCELLED;
					else {
						setError(err, err_cap, where + " leaves '" + from + "' (" + from_node.kind +
							"), which finishes later, and needs output \"completed\", \"blocked\", "
							"\"failed\" or \"cancelled\"");
						return nullptr;
					}
				} else if (has_output && output != "out") {
					setError(err, err_cap, where + " output must be \"out\"");
					return nullptr;
				}
				from_node.out.push_back(Edge{ target->second, port });
				program->exec_edges++;
			}
		}
		return program.release();
	} catch (...) {
		setError(err, err_cap, what + " could not be read");
		return nullptr;
	}
}

} // namespace

extern "C" mission_graph_program_t *missionGraphProgramCompile(const char *json, size_t json_size,
	mission_graph_module_known_fn module_known, char *err, size_t err_cap)
{
	return compileProgram(json, json_size, module_known, err, err_cap, DOMAIN_MISSION);
}

extern "C" mission_graph_program_t *missionGraphProgramCompileLevel(const char *json, size_t json_size,
	mission_graph_module_known_fn module_known, char *err, size_t err_cap)
{
	return compileProgram(json, json_size, module_known, err, err_cap, DOMAIN_LEVEL);
}

extern "C" mission_graph_program_t *missionGraphProgramCompileProjectile(const char *json, size_t json_size,
	mission_graph_module_known_fn module_known, char *err, size_t err_cap)
{
	return compileProgram(json, json_size, module_known, err, err_cap, DOMAIN_PROJECTILE);
}

extern "C" mission_graph_program_t *missionGraphProgramCompileWeapon(const char *json, size_t json_size,
	mission_graph_module_known_fn module_known, char *err, size_t err_cap)
{
	return compileProgram(json, json_size, module_known, err, err_cap, DOMAIN_WEAPON);
}

extern "C" s32 missionGraphProgramNodeIndex(const mission_graph_program_t *program, const char *node_id)
{
	if (!program || !node_id) return -1;
	for (size_t i = 0; i < program->nodes.size(); i++) {
		if (program->nodes[i].id == node_id) return static_cast<s32>(i);
	}
	return -1;
}

extern "C" const char *missionGraphProgramNodeId(const mission_graph_program_t *program, s32 index)
{
	if (!program || index < 0 || static_cast<size_t>(index) >= program->nodes.size()) return nullptr;
	return program->nodes[static_cast<size_t>(index)].id.c_str();
}

extern "C" const char *missionGraphProgramNodeKind(const mission_graph_program_t *program, s32 index)
{
	if (!program || index < 0 || static_cast<size_t>(index) >= program->nodes.size()) return nullptr;
	return program->nodes[static_cast<size_t>(index)].kind.c_str();
}

extern "C" const char *missionGraphParamText(const mission_graph_params_t *params, const char *key)
{
	if (!params || !key) return nullptr;
	for (const auto &value : params->values) {
		if (value.first == key) return value.second.c_str();
	}
	return nullptr;
}

extern "C" s32 missionGraphPhaseFromText(const char *text)
{
	if (!text) return -1;
	if (std::strcmp(text, "active") == 0) return MISSION_GRAPH_PHASE_ACTIVE;
	if (std::strcmp(text, "complete") == 0) return MISSION_GRAPH_PHASE_COMPLETE;
	if (std::strcmp(text, "failed") == 0) return MISSION_GRAPH_PHASE_FAILED;
	return -1;
}

extern "C" void missionGraphProgramFree(mission_graph_program_t *program)
{
	delete program;
}

extern "C" s32 missionGraphProgramVisitModuleParams(const mission_graph_program_t *program,
	mission_graph_module_param_visitor_t visit, void *userdata)
{
	if (!program || !visit) return 0;
	for (const Node &node : program->nodes) {
		if ((node.type == NODE_MODULE || node.native_gate)
				&& !visit(node.kind.c_str(), &node.params, userdata)) return 0;
	}
	return 1;
}

extern "C" s32 missionGraphProgramNodeCount(const mission_graph_program_t *program)
{
	return program ? static_cast<s32>(program->nodes.size()) : 0;
}

extern "C" s32 missionGraphProgramExecEdgeCount(const mission_graph_program_t *program)
{
	return program ? program->exec_edges : 0;
}

extern "C" s32 missionGraphProgramEventNodeCount(const mission_graph_program_t *program, s32 event_kind)
{
	s32 count = 0;
	if (!program) return 0;
	for (const Node &node : program->nodes) {
		if (node.type == NODE_EVENT && node.event_kind == event_kind) count++;
	}
	return count;
}

extern "C" s32 missionGraphProgramModuleNodeCount(const mission_graph_program_t *program)
{
	return program ? program->modules : 0;
}

extern "C" s32 missionGraphProgramVolumeCount(const mission_graph_program_t *program)
{
	return program ? static_cast<s32>(program->volumes.size()) : 0;
}

extern "C" const char *missionGraphProgramVolumeAt(const mission_graph_program_t *program, s32 index)
{
	if (!program || index < 0 || static_cast<size_t>(index) >= program->volumes.size()) {
		return nullptr;
	}
	return program->volumes[static_cast<size_t>(index)].c_str();
}

extern "C" s32 missionGraphProgramHandlesEvent(const mission_graph_program_t *program, s32 event_kind)
{
	if (!program) return 0;
	for (const Node &node : program->nodes) {
		if (node.type == NODE_EVENT && node.event_kind == event_kind && !node.out.empty()) return 1;
	}
	return 0;
}

extern "C" s32 missionGraphEventIsProjectile(s32 event_kind)
{
	return isProjectileEventKind(event_kind) ? 1 : 0;
}

extern "C" s32 missionGraphEventIsWeapon(s32 event_kind)
{
	return isWeaponEventKind(event_kind) ? 1 : 0;
}

extern "C" s32 missionGraphEventIsObjectRule(s32 event_kind)
{
	return event_kind == MISSION_GRAPH_EVENT_PICKUP_TOUCHED || event_kind == MISSION_GRAPH_EVENT_DOOR_USE ||
		event_kind == MISSION_GRAPH_EVENT_DOOR_APPROACH || event_kind == MISSION_GRAPH_EVENT_LIFT_CALLED ||
		event_kind == MISSION_GRAPH_EVENT_LIFT_IDLE || event_kind == MISSION_GRAPH_EVENT_LIFT_ARRIVED ||
		event_kind == MISSION_GRAPH_EVENT_GLASS_DAMAGED;
}

extern "C" s32 missionGraphProgramEventReachesModule(const mission_graph_program_t *program,
	s32 event_kind, const char *module_kind)
{
	if (!program || !module_kind) return 0;
	try {
		std::vector<char> reached(program->nodes.size(), 0);
		std::vector<int> queue;
		for (const Node &root : program->nodes) {
			if (root.type != NODE_EVENT || root.event_kind != event_kind) continue;
			for (const Edge &edge : root.out) queue.push_back(edge.target);
		}
		for (size_t head = 0; head < queue.size(); head++) {
			const int at = queue[head];
			if (reached[at]) continue;
			reached[at] = 1;
			const Node &node = program->nodes[at];
			if ((node.type == NODE_MODULE || node.native_gate) && node.kind == module_kind) return 1;
			for (const Edge &edge : node.out) queue.push_back(edge.target);
		}
	} catch (...) {
	}
	return 0;
}

namespace {

bool objectiveMatches(const std::string &filter, const mission_graph_event_t *event)
{
	return filter.empty() || (event->objective_node && filter == event->objective_node);
}

// Whether the event is well formed for its kind (the kind is known and it
// carries what its kind needs).
bool eventValid(const mission_graph_event_t *event)
{
	if (event->kind == MISSION_GRAPH_EVENT_OBJECTIVE_STATUS_CHANGED) {
		return event->status >= MISSION_GRAPH_STATUS_INCOMPLETE && event->status <= MISSION_GRAPH_STATUS_FAILED;
	}
	if (event->kind == MISSION_GRAPH_EVENT_MISSION_COMPLETED) {
		return event->seam == MISSION_GRAPH_SEAM_NONE;
	}
	if (event->kind == MISSION_GRAPH_EVENT_MISSION_CONTINUE) {
		return event->seam == MISSION_GRAPH_SEAM_ENDSCREEN || event->seam == MISSION_GRAPH_SEAM_COOP_STAGE_END;
	}
	if (event->kind == MISSION_GRAPH_EVENT_MISSION_PHASE_CHANGED) {
		return event->seam == MISSION_GRAPH_SEAM_NONE && event->phase >= MISSION_GRAPH_PHASE_ACTIVE &&
			event->phase <= MISSION_GRAPH_PHASE_FAILED;
	}
	if (event->kind == MISSION_GRAPH_EVENT_VOLUME_ENTERED || event->kind == MISSION_GRAPH_EVENT_VOLUME_LEFT) {
		return event->seam == MISSION_GRAPH_SEAM_NONE && event->volume && event->volume[0] && event->player >= 0;
	}
	if (isObjectEventKind(event->kind)) {
		return event->seam == MISSION_GRAPH_SEAM_NONE && event->object;
	}
	if (isProjectileEventKind(event->kind) || isWeaponEventKind(event->kind)) {
		return event->seam == MISSION_GRAPH_SEAM_NONE && event->instance;
	}
	return false;
}

// A node leaves by a port: the trace sees it, and the port's edges join the
// queue.
void routePort(mission_graph_activation &activation, int at, Port port)
{
	const Node &node = activation.program->nodes[static_cast<size_t>(at)];
	if (activation.trace) activation.trace(node.id.c_str(), portName(port), activation.trace_user);
	for (const Edge &edge : node.out) {
		if (edge.port == port) activation.queue.push_back(edge.target);
	}
}

// Every node may run again, and the event's roots are queued.
bool beginActivation(mission_graph_activation &activation, const mission_graph_event_t *event)
{
	const mission_graph_program &program = *activation.program;
	std::fill(activation.reached.begin(), activation.reached.end(), 0);
	activation.queue.clear();
	activation.head = 0;
	if (!event || !eventValid(event)) return false;
	const bool objective_event = event->kind == MISSION_GRAPH_EVENT_OBJECTIVE_STATUS_CHANGED;
	const bool volume_event = event->kind == MISSION_GRAPH_EVENT_VOLUME_ENTERED ||
		event->kind == MISSION_GRAPH_EVENT_VOLUME_LEFT;
	const bool object_event = isObjectEventKind(event->kind);
	for (size_t i = 0; i < program.nodes.size(); i++) {
		const Node &root = program.nodes[i];
		if (root.type != NODE_EVENT || root.event_kind != event->kind ||
				(objective_event && !objectiveMatches(root.objective, event)) ||
				(volume_event && root.volume != event->volume) ||
				(object_event && !root.object.empty() && root.object != event->object)) {
			continue;
		}
		routePort(activation, static_cast<int>(i), PORT_OUT);
	}
	return true;
}

// The queue runs to empty, breadth first in authored order; each node runs at
// most once per activation. Returns the modules run, or -1 when a module
// refused the event (the walk still runs to empty).
s32 walkActivation(mission_graph_activation &activation, const mission_graph_event_t *event,
	mission_graph_module_run_fn run, void *user)
{
	const mission_graph_program &program = *activation.program;
	const bool objective_event = event->kind == MISSION_GRAPH_EVENT_OBJECTIVE_STATUS_CHANGED;
	s32 ran = 0;
	bool refused = false;
	while (activation.head < activation.queue.size()) {
		const int at = activation.queue[activation.head++];
		if (activation.reached[static_cast<size_t>(at)]) continue;
		activation.reached[static_cast<size_t>(at)] = 1;
		const Node &node = program.nodes[static_cast<size_t>(at)];
		if (node.type == NODE_GATE) {
			Port port = PORT_FAIL;
			if (node.native_gate) {
				const s32 answer = run(node.kind.c_str(), node.id.c_str(), &node.params, event, user);
				port = answer > 0 ? PORT_PASS
					: program.weapon && answer == MISSION_GRAPH_RUN_GATE_FAILED ? PORT_GATE_FAILED : PORT_FAIL;
			} else {
				const bool pass = node.gate_phase >= 0
					? event->kind == MISSION_GRAPH_EVENT_MISSION_PHASE_CHANGED && node.gate_phase == event->phase
					: objective_event && node.gate_status == event->status &&
						objectiveMatches(node.objective, event);
				port = pass ? PORT_PASS : PORT_FAIL;
			}
			routePort(activation, at, port);
		} else if (node.type == NODE_MODULE) {
			const s32 result = run(node.kind.c_str(), node.id.c_str(), &node.params, event, user);
			if (!node.async) {
				if (!result) {
					refused = true;
					continue;
				}
				ran++;
				routePort(activation, at, PORT_OUT);
				continue;
			}
			// held-weapon-events.md slice 1: how its work ended, or nothing
			// yet (the caller routes the outcome when it ends).
			switch (result) {
			case MISSION_GRAPH_RUN_PENDING: ran++; break;
			case MISSION_GRAPH_RUN_COMPLETED: ran++; routePort(activation, at, PORT_COMPLETED); break;
			case MISSION_GRAPH_RUN_BLOCKED: ran++; routePort(activation, at, PORT_BLOCKED); break;
			case MISSION_GRAPH_RUN_FAILED: ran++; routePort(activation, at, PORT_OUTCOME_FAILED); break;
			case MISSION_GRAPH_RUN_CANCELLED: ran++; routePort(activation, at, PORT_CANCELLED); break;
			default: refused = true; break;
			}
		}
	}
	return refused ? -1 : ran;
}

} // namespace

extern "C" mission_graph_activation_t *missionGraphActivationCreate(const mission_graph_program_t *program)
{
	if (!program) return nullptr;
	try {
		auto activation = std::make_unique<mission_graph_activation>();
		activation->program = program;
		activation->reached.assign(program->nodes.size(), 0);
		activation->queue.reserve(program->nodes.size());
		return activation.release();
	} catch (...) {
		return nullptr;
	}
}

extern "C" void missionGraphActivationFree(mission_graph_activation_t *activation)
{
	delete activation;
}

extern "C" void missionGraphActivationSetTrace(mission_graph_activation_t *activation,
	mission_graph_trace_fn trace, void *user)
{
	if (!activation) return;
	activation->trace = trace;
	activation->trace_user = user;
}

extern "C" s32 missionGraphActivationBegin(mission_graph_activation_t *activation,
	const mission_graph_event_t *event)
{
	if (!activation) return 0;
	try {
		return beginActivation(*activation, event) ? 1 : 0;
	} catch (...) {
		return 0;
	}
}

extern "C" s32 missionGraphActivationRoute(mission_graph_activation_t *activation, s32 node_index, s32 outcome)
{
	if (!activation || node_index < 0 || static_cast<size_t>(node_index) >= activation->program->nodes.size()) {
		return 0;
	}
	const Node &node = activation->program->nodes[static_cast<size_t>(node_index)];
	if (node.type != NODE_MODULE || !node.async) return 0;
	const Port port = outcome == MISSION_GRAPH_RUN_COMPLETED ? PORT_COMPLETED
		: outcome == MISSION_GRAPH_RUN_BLOCKED ? PORT_BLOCKED
		: outcome == MISSION_GRAPH_RUN_FAILED ? PORT_OUTCOME_FAILED
		: outcome == MISSION_GRAPH_RUN_CANCELLED ? PORT_CANCELLED : PORT_OUT;
	if (port == PORT_OUT) return 0;
	try {
		routePort(*activation, node_index, port);
		return 1;
	} catch (...) {
		return 0;
	}
}

extern "C" s32 missionGraphActivationWalk(mission_graph_activation_t *activation,
	const mission_graph_event_t *event, mission_graph_module_run_fn run, void *user)
{
	if (!activation || !event || !run || !eventValid(event)) return -1;
	try {
		return walkActivation(*activation, event, run, user);
	} catch (...) {
		return -1;
	}
}

extern "C" s32 missionGraphProgramFire(const mission_graph_program_t *program,
	const mission_graph_event_t *event, mission_graph_module_run_fn run, void *user)
{
	if (!program || !event || !run) {
		return -1;
	}
	try {
		mission_graph_activation activation;
		activation.program = program;
		activation.reached.assign(program->nodes.size(), 0);
		if (!beginActivation(activation, event)) return -1;
		return walkActivation(activation, event, run, user);
	} catch (...) {
		return -1;
	}
}

namespace {

bool bindLevelLayoutIdentity(crude_json::value &graph, const char *identity)
{
	if (!graph.is_object() || !graph.contains("scenario") || !graph["scenario"].is_string()
			|| graph["scenario"].get<crude_json::string>() != identity
			|| !graph.contains("nodes") || !graph["nodes"].is_array()) return false;
	graph["scenario"] = "";
	auto *nodes = graph["nodes"].get_ptr<crude_json::array>();
	bool found = false;
	for (auto &node : *nodes) {
		if (!node.is_object() || !node.contains("id") || !node["id"].is_string()
				|| node["id"].get<crude_json::string>() != "scenario.global.settings") continue;
		if (found || !node.contains("kind") || !node["kind"].is_string()
				|| node["kind"].get<crude_json::string>() != "scenario.global.settings.source"
				|| !node.contains("scenario") || !node["scenario"].is_string()
				|| node["scenario"].get<crude_json::string>() != identity) return false;
		node["scenario"] = "";
		found = true;
	}
	return found;
}

} // namespace

extern "C" s32 missionGraphLevelLayoutCompatible(const char *outgoing, size_t outgoing_size, const char *outgoing_id,
	const char *incoming, size_t incoming_size, const char *incoming_id)
{
	if (!outgoing_id || !outgoing_id[0] || !incoming_id || !incoming_id[0]) return 0;
	try {
		crude_json::value before, after;
		return modAssetJsonReadValue(outgoing, outgoing_size, before)
			&& modAssetJsonReadValue(incoming, incoming_size, after)
			&& bindLevelLayoutIdentity(before, outgoing_id)
			&& bindLevelLayoutIdentity(after, incoming_id)
			&& before.dump() == after.dump();
	} catch (...) {
		return 0;
	}
}
