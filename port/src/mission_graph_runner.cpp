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
// structure; a level graph and a projectile or entity graph skip them, and a
// projectile program has the projectile events only (projectile-events.md
// slice 2).
enum Domain { DOMAIN_MISSION, DOMAIN_LEVEL, DOMAIN_PROJECTILE };

} // namespace

// object-events.md slice 3: a node's scalar fields as text.
struct mission_graph_params {
	std::vector<std::pair<std::string, std::string>> values;
};

namespace {
enum Port { PORT_OUT, PORT_PASS, PORT_FAIL };

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
		kind <= MISSION_GRAPH_EVENT_PROJECTILE_SENTRY_AIMED;
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
};

namespace {

bool isProgramKind(const std::string &kind)
{
	return kind.rfind("event.", 0) == 0 || kind.rfind("gate.", 0) == 0 || kind.rfind("og.", 0) == 0;
}

// level (object-events.md slice 1): the graph is a level graph, whose
// program is only its event.*, gate.* and og.* nodes; every other node is
// level data, skipped and not counted, and an edge that is not an
// execution edge may name it.
mission_graph_program_t *compileProgram(const char *json, size_t json_size,
	mission_graph_module_known_fn module_known, char *err, size_t err_cap, Domain domain)
{
	// A level graph and a projectile or entity graph carry data nodes that
	// are not the program (object-events.md slice 1, projectile-events.md
	// slice 2).
	const bool data_nodes = domain != DOMAIN_MISSION;
	const std::string what = domain == DOMAIN_LEVEL ? "level graph"
		: domain == DOMAIN_PROJECTILE ? "projectile graph" : "mission graph";
	const std::string file = domain == DOMAIN_LEVEL ? "level.graph.json"
		: domain == DOMAIN_PROJECTILE ? "behavior.graph.json" : "mission.graph.json";

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
		std::map<std::string, int> index;
		for (size_t i = 0; i < nodes.size(); i++) {
			const crude_json::value &node = nodes[i];
			Node compiled;
			if (data_nodes) {
				std::string kind;
				if (!node.is_object() || !stringField(node, "kind", kind) || !isProgramKind(kind)) {
					continue; // level or projectile data, not the program
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

			// projectile-events.md slice 2: a projectile program has the
			// projectile events and no other; no other graph has them.
			const s32 projectile_kind = projectileEventKindOf(compiled.kind);
			if (domain == DOMAIN_PROJECTILE && compiled.kind.rfind("event.", 0) == 0 &&
					projectile_kind == 0) {
				setError(err, err_cap, "event '" + compiled.id + "' (" + compiled.kind +
					") is not a projectile event");
				return nullptr;
			}
			if (projectile_kind != 0) {
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
				if (!module_known || !module_known(compiled.kind.c_str())) {
					setError(err, err_cap, "native module '" + compiled.kind + "' does not exist");
					return nullptr;
				}
				compiled.type = NODE_MODULE;
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
					else {
						setError(err, err_cap, where + " leaves gate '" + from +
							"' and needs output \"pass\" or \"fail\"");
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

} // namespace

extern "C" s32 missionGraphProgramFire(const mission_graph_program_t *program,
	const mission_graph_event_t *event, mission_graph_module_run_fn run, void *user)
{
	if (!program || !event || !run) {
		return -1;
	}
	if (event->kind == MISSION_GRAPH_EVENT_OBJECTIVE_STATUS_CHANGED) {
		if (event->status < MISSION_GRAPH_STATUS_INCOMPLETE || event->status > MISSION_GRAPH_STATUS_FAILED) {
			return -1;
		}
	} else if (event->kind == MISSION_GRAPH_EVENT_MISSION_COMPLETED) {
		if (event->seam != MISSION_GRAPH_SEAM_NONE) return -1;
	} else if (event->kind == MISSION_GRAPH_EVENT_MISSION_CONTINUE) {
		if (event->seam != MISSION_GRAPH_SEAM_ENDSCREEN && event->seam != MISSION_GRAPH_SEAM_COOP_STAGE_END) {
			return -1;
		}
	} else if (event->kind == MISSION_GRAPH_EVENT_MISSION_PHASE_CHANGED) {
		if (event->seam != MISSION_GRAPH_SEAM_NONE || event->phase < MISSION_GRAPH_PHASE_ACTIVE ||
				event->phase > MISSION_GRAPH_PHASE_FAILED) {
			return -1;
		}
	} else if (event->kind == MISSION_GRAPH_EVENT_VOLUME_ENTERED ||
			event->kind == MISSION_GRAPH_EVENT_VOLUME_LEFT) {
		if (event->seam != MISSION_GRAPH_SEAM_NONE || !event->volume || !event->volume[0] ||
				event->player < 0) {
			return -1;
		}
	} else if (isObjectEventKind(event->kind)) {
		if (event->seam != MISSION_GRAPH_SEAM_NONE || !event->object) {
			return -1;
		}
	} else if (isProjectileEventKind(event->kind)) {
		if (event->seam != MISSION_GRAPH_SEAM_NONE || !event->instance) {
			return -1;
		}
	} else {
		return -1;
	}
	const bool objective_event = event->kind == MISSION_GRAPH_EVENT_OBJECTIVE_STATUS_CHANGED;
	const bool volume_event = event->kind == MISSION_GRAPH_EVENT_VOLUME_ENTERED ||
		event->kind == MISSION_GRAPH_EVENT_VOLUME_LEFT;
	const bool object_event = isObjectEventKind(event->kind);
	try {
		std::vector<char> reached(program->nodes.size(), 0);
		std::vector<int> queue;
		s32 ran = 0;
		bool refused = false;
		for (const Node &root : program->nodes) {
			if (root.type != NODE_EVENT || root.event_kind != event->kind ||
					(objective_event && !objectiveMatches(root.objective, event)) ||
					(volume_event && root.volume != event->volume) ||
					(object_event && !root.object.empty() && root.object != event->object)) {
				continue;
			}
			for (const Edge &edge : root.out) queue.push_back(edge.target);
		}
		// Breadth first in authored order; each node runs at most once per event.
		for (size_t head = 0; head < queue.size(); head++) {
			const int at = queue[head];
			if (reached[at]) continue;
			reached[at] = 1;
			const Node &node = program->nodes[at];
			if (node.type == NODE_GATE) {
				const bool pass = node.native_gate
					? run(node.kind.c_str(), node.id.c_str(), &node.params, event, user) > 0
					: node.gate_phase >= 0
					? event->kind == MISSION_GRAPH_EVENT_MISSION_PHASE_CHANGED && node.gate_phase == event->phase
					: objective_event && node.gate_status == event->status &&
						objectiveMatches(node.objective, event);
				for (const Edge &edge : node.out) {
					if ((pass && edge.port == PORT_PASS) || (!pass && edge.port == PORT_FAIL)) {
						queue.push_back(edge.target);
					}
				}
			} else if (node.type == NODE_MODULE) {
				if (!run(node.kind.c_str(), node.id.c_str(), &node.params, event, user)) {
					refused = true;
					continue;
				}
				ran++;
				for (const Edge &edge : node.out) queue.push_back(edge.target);
			}
		}
		return refused ? -1 : ran;
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
