// Mission graph runner (decision 2, CORE, 2026-11-15). See
// port/include/mission_graph_runner.h for the contract.
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
	std::vector<Edge> out;
};

const char *const kObjectiveStatusEvent = "event.objective.status_changed";
const char *const kMissionCompletedEvent = "event.mission.completed";
const char *const kMissionContinueEvent = "event.mission.continue";
const char *const kObjectiveStatusGate = "gate.objective.status";

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
	int exec_edges = 0;
	int modules = 0;
};

extern "C" mission_graph_program_t *missionGraphProgramCompile(const char *json, size_t json_size,
	mission_graph_module_known_fn module_known, char *err, size_t err_cap)
{
	setError(err, err_cap, "");
	if (!json || !json_size) {
		setError(err, err_cap, "empty mission graph");
		return nullptr;
	}
	try {
		crude_json::value root;
		if (!modAssetJsonReadValue(json, json_size, root) || !root.is_object()) {
			setError(err, err_cap, "mission.graph.json is not one JSON object");
			return nullptr;
		}
		if (!root.contains("nodes") || !root["nodes"].is_array()) {
			setError(err, err_cap, "mission.graph.json has no nodes array");
			return nullptr;
		}
		const crude_json::array &nodes = root["nodes"].get<crude_json::array>();
		if (nodes.size() > MISSION_GRAPH_MAX_NODES) {
			setError(err, err_cap, "more than " + std::to_string(MISSION_GRAPH_MAX_NODES) + " nodes");
			return nullptr;
		}

		auto program = std::make_unique<mission_graph_program>();
		std::map<std::string, int> index;
		for (size_t i = 0; i < nodes.size(); i++) {
			const crude_json::value &node = nodes[i];
			Node compiled;
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

			if (compiled.kind == kObjectiveStatusEvent) {
				compiled.type = NODE_EVENT;
				compiled.event_kind = MISSION_GRAPH_EVENT_OBJECTIVE_STATUS_CHANGED;
				compiled.objective = objective;
			} else if (compiled.kind == kMissionCompletedEvent || compiled.kind == kMissionContinueEvent) {
				if (has_objective) {
					setError(err, err_cap, "mission event '" + compiled.id + "' has no objective to filter");
					return nullptr;
				}
				compiled.type = NODE_EVENT;
				compiled.event_kind = compiled.kind == kMissionCompletedEvent
					? MISSION_GRAPH_EVENT_MISSION_COMPLETED : MISSION_GRAPH_EVENT_MISSION_CONTINUE;
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
			} else if (compiled.kind.rfind("gate.", 0) == 0) {
				setError(err, err_cap, "gate kind '" + compiled.kind + "' is not executable");
				return nullptr;
			} else if (compiled.kind.rfind("og.", 0) == 0) {
				if (!module_known || !module_known(compiled.kind.c_str())) {
					setError(err, err_cap, "native module '" + compiled.kind + "' does not exist");
					return nullptr;
				}
				compiled.type = NODE_MODULE;
				program->modules++;
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
		setError(err, err_cap, "mission graph could not be read");
		return nullptr;
	}
}

extern "C" void missionGraphProgramFree(mission_graph_program_t *program)
{
	delete program;
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

extern "C" s32 missionGraphProgramHandlesEvent(const mission_graph_program_t *program, s32 event_kind)
{
	if (!program) return 0;
	for (const Node &node : program->nodes) {
		if (node.type == NODE_EVENT && node.event_kind == event_kind && !node.out.empty()) return 1;
	}
	return 0;
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
			if (node.type == NODE_MODULE && node.kind == module_kind) return 1;
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
	} else {
		return -1;
	}
	const bool objective_event = event->kind == MISSION_GRAPH_EVENT_OBJECTIVE_STATUS_CHANGED;
	try {
		std::vector<char> reached(program->nodes.size(), 0);
		std::vector<int> queue;
		s32 ran = 0;
		bool refused = false;
		for (const Node &root : program->nodes) {
			if (root.type != NODE_EVENT || root.event_kind != event->kind ||
					(objective_event && !objectiveMatches(root.objective, event))) {
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
				const bool pass = objective_event && node.gate_status == event->status &&
					objectiveMatches(node.objective, event);
				for (const Edge &edge : node.out) {
					if ((pass && edge.port == PORT_PASS) || (!pass && edge.port == PORT_FAIL)) {
						queue.push_back(edge.target);
					}
				}
			} else if (node.type == NODE_MODULE) {
				if (!run(node.kind.c_str(), node.id.c_str(), event, user)) {
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
