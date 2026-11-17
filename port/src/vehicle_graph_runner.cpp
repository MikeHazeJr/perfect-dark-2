// pd.vehicle_graph.v1 runner. The contract is port/include/vehicle_graph_runner.h;
// the schema is context/designs/modding/vehicle-graph-v1.md (accepted by CORE,
// 2026-11-16). The compile rules follow the mission graph runner's.
//
// A vehicle's behavior is a graph of events, gates and native modules. The game
// fires an event (a mount or dismount request, the per-update tick), the walk
// runs the nodes that event reaches, and the modules do the work: the original
// code for the seat, the drive and the bonus, and the boost and heat machine.
//
// Layout of this file:
//   1. Kinds     the events, gates and native modules a graph may name
//   2. Values    times, variables and node params
//   3. Compile   nodes, edges and the rules proved before a graph may run
//   4. Run       one activation: a breadth-first walk from an event's roots
//   5. C API     the functions vehicle_graph_runner.h declares
#include "vehicle_graph_runner.h"

#include "modasset_gltf_document.h"
#include "../external/imgui-node-editor/crude_json.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

using Json = crude_json::value;

/* ---------------------------------------------------------------------------
 * 1. Kinds
 * The events, gates and native modules a v1 graph may name. The conformance
 * tool (tools/asset_archive_conformance.py) holds the same lists, and a
 * pd-tests case compares them through vehicleGraphKnownKind.
 * ------------------------------------------------------------------------- */

/* What a node is. A data node is kept and counted but never walked. */
enum NodeType { NODE_DATA, NODE_EVENT, NODE_GATE, NODE_MODULE };
/* The output an execution edge leaves by: out for an event or a module, pass
 * or fail for a gate. */
enum Port { PORT_OUT, PORT_PASS, PORT_FAIL };

/* The executable kinds; kGates and kModules below give each its schema name. */
enum GateKind { GATE_NONE, GATE_SEAT, GATE_SEAT_FREE, GATE_HEAT_PHASE };
enum ModuleKind {
	MOD_NONE,
	MOD_MOUNT_SEAT,
	MOD_DISMOUNT_SEAT,
	MOD_HOVER_DRIVE,
	MOD_THROTTLE_BOOST,
	MOD_BOOST_HEAT,
	MOD_PART_VISIBLE,
	MOD_TURRET_LOCKOUT,
};

/* One bit per event kind, for the set of events a module may run on. */
constexpr u32 eventBit(int kind)
{
	return 1u << kind;
}

/* Every event: a module that may run wherever a graph puts it. */
constexpr u32 kAnyEvent = eventBit(VEHICLE_GRAPH_EVENT_MOUNT_REQUESTED)
	| eventBit(VEHICLE_GRAPH_EVENT_DISMOUNT_REQUESTED)
	| eventBit(VEHICLE_GRAPH_EVENT_TICK)
	| eventBit(VEHICLE_GRAPH_EVENT_HEAT_CHANGED);

/* The events a native seam may fire; heat_changed is raised only. */
constexpr int kFiredEvents[] = {
	VEHICLE_GRAPH_EVENT_MOUNT_REQUESTED,
	VEHICLE_GRAPH_EVENT_DISMOUNT_REQUESTED,
	VEHICLE_GRAPH_EVENT_TICK,
};

/* event.vehicle.*: where a walk starts. */
struct EventSpec {
	const char *kind;
	int event;
};

const EventSpec kEvents[] = {
	{ "event.vehicle.mount_requested", VEHICLE_GRAPH_EVENT_MOUNT_REQUESTED },
	{ "event.vehicle.dismount_requested", VEHICLE_GRAPH_EVENT_DISMOUNT_REQUESTED },
	{ "event.vehicle.tick", VEHICLE_GRAPH_EVENT_TICK },
	{ "event.vehicle.heat_changed", VEHICLE_GRAPH_EVENT_HEAT_CHANGED },
};

/* gate.vehicle.*: a gate sends the walk down its pass edges or its fail edges.
 * params lists the keys it takes, ending in nullptr. */
struct GateSpec {
	const char *kind;
	int gate;
	const char *params[2];
};

const GateSpec kGates[] = {
	{ "gate.vehicle.seat", GATE_SEAT, { "seat", nullptr } },
	{ "gate.vehicle.seat_free", GATE_SEAT_FREE, { "seat", nullptr } },
	{ "gate.vehicle.heat_phase", GATE_HEAT_PHASE, { "phases", nullptr } },
};

/* og.vehicle.*: the native modules. Each names the events it may run on (the
 * compiler refuses a module an event of another kind reaches), the event it
 * raises, and the keys it takes. */
struct ModuleSpec {
	const char *kind;
	int module;
	u32 events;      // the events it may run on
	int raises;      // the event it raises, 0 for none
	const char *params[9];
};

const ModuleSpec kModules[] = {
	{ "og.vehicle.mount_seat", MOD_MOUNT_SEAT,
		eventBit(VEHICLE_GRAPH_EVENT_MOUNT_REQUESTED), 0, { "seat", nullptr } },
	{ "og.vehicle.dismount_seat", MOD_DISMOUNT_SEAT,
		eventBit(VEHICLE_GRAPH_EVENT_DISMOUNT_REQUESTED), 0, { "seat", nullptr } },
	{ "og.vehicle.hover_drive", MOD_HOVER_DRIVE,
		eventBit(VEHICLE_GRAPH_EVENT_TICK), 0, { "drive", nullptr } },
	{ "og.vehicle.throttle_boost", MOD_THROTTLE_BOOST,
		eventBit(VEHICLE_GRAPH_EVENT_TICK), 0, { "boost_speed", "ramp_time", nullptr } },
	{ "og.vehicle.boost_heat", MOD_BOOST_HEAT,
		eventBit(VEHICLE_GRAPH_EVENT_TICK), VEHICLE_GRAPH_EVENT_HEAT_CHANGED,
		{ "boost_speed", "ramp_time", "overheat_after", "overheated_boost", "steer_limit",
			"steer_limit_time", "recover_time_stopped", "recover_speed_scale", nullptr } },
	{ "og.vehicle.part_visible", MOD_PART_VISIBLE, kAnyEvent, 0,
		{ "part", "visible", nullptr } },
	{ "og.vehicle.turret_lockout", MOD_TURRET_LOCKOUT, kAnyEvent, 0,
		{ "reason", "locked", nullptr } },
};

/* The largest boost speed a param may hold (world units per 60 Hz tick). */
constexpr double kMaxSpeed = 1000.0;
/* The longest time a param may hold, in 240 Hz ticks (one hour). */
constexpr double kMaxTicks240 = 240.0 * 3600.0;
/* The largest recovery multiplier og.vehicle.boost_heat takes. */
constexpr double kMaxRecoverScale = 100.0;

/* An execution edge: the node it reaches (an index into the program's nodes)
 * and the output it leaves by. */
struct Edge {
	int target;
	Port port;
};

/* One compiled node. Its params are already resolved: variables taken, times
 * in 240 Hz ticks, names turned into numbers. Only the fields of its own kind
 * are used. */
struct Node {
	std::string id;                  // the author's name for it, unique in the graph
	std::string kind;                // the schema kind, kept for messages
	NodeType type = NODE_DATA;
	int event_kind = 0;              // event: VEHICLE_GRAPH_EVENT_*
	int gate = GATE_NONE;            // gate: GateKind
	int module = MOD_NONE;           // module: ModuleKind
	u32 events = 0;                  // module: the events it may run on (eventBit)
	int raises = 0;                  // module: the event it raises, 0 for none
	int seat = VEHICLE_GRAPH_SEAT_NONE; // seat gates, mount_seat, dismount_seat
	u32 phase_mask = 0;              // gate.vehicle.heat_phase: one bit per phase
	int drive = 1;                   // hover_drive: takes the driver's inputs
	f32 boost_speed = 0.0f;          // throttle_boost: extra speed at full bonus
	f32 ramp_ticks240 = 0.0f;        // throttle_boost: full throttle time to full bonus
	vehicle_boost_heat_params_t heat{}; // boost_heat
	s32 part = -1;                   // part_visible: the model's part number
	int visible = 1;                 // part_visible
	std::string reason_text;         // turret_lockout: the reason's name
	int reason = -1;                 // turret_lockout: its bit in the lock mask
	int locked = 0;                  // turret_lockout
	int slot = -1;                   // throttle_boost: its timer in the instance
	std::vector<Edge> out;           // execution edges, in authored order
};

/* The schema name of an event kind, for messages. */
const char *eventName(int kind)
{
	for (const EventSpec &spec : kEvents) {
		if (spec.event == kind) return spec.kind;
	}
	return "?";
}

/* 1 to max_len of a-z, 0-9 and _: variable names and lockout reasons. */
bool isLowerName(const std::string &text, size_t max_len)
{
	if (text.empty() || text.size() > max_len) return false;
	for (char c : text) {
		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
	}
	return true;
}

/* A graph, a node, an edge and a variables file may each carry one "comment":
 * free text for people, checked to be a string and never read for behavior. */
bool commentOk(const Json &object, const std::string &where, std::string &error)
{
	if (object.contains("comment") && !object["comment"].is_string()) {
		error = where + " comment must be a string";
		return false;
	}
	return true;
}

/* A kind's prefix decides what a node is (event., gate., og.). */
bool startsWith(const std::string &text, const char *prefix)
{
	return text.rfind(prefix, 0) == 0;
}

/* ---------------------------------------------------------------------------
 * 2. Values
 * Times, variables and node params. A time is always {value, unit}, so no
 * number in a graph is a bare tick count; a "$name" param takes the
 * variable of that name from behavior/variables.json whole.
 * ------------------------------------------------------------------------- */

/* The one shape a time may have, quoted in refusals. */
const char *const kTimeShape =
	"must be {\"value\": N, \"unit\": \"ticks60\" | \"ticks240\" | \"centiseconds\"}";

/* A {value, unit} time with exactly those two keys and a known unit. */
bool readTimeObject(const Json &value, f32 &ticks240, std::string &why)
{
	if (!value.is_object() || value.get<crude_json::object>().size() != 2 ||
			!value.contains("value") || !value.contains("unit") ||
			!value["value"].is_number() || !value["unit"].is_string()) {
		why = kTimeShape;
		return false;
	}
	const double number = value["value"].get<crude_json::number>();
	const std::string &unit = value["unit"].get<crude_json::string>();
	f32 out = 0.0f;
	if (!std::isfinite(number) || number < 0.0 || number > kMaxTicks240 ||
			!vehicleBoostHeatTimeToTicks240(static_cast<f32>(number), unit.c_str(), &out)) {
		why = "has an unknown unit or a value out of range";
		return false;
	}
	if (out > kMaxTicks240) {
		why = "is longer than an hour";
		return false;
	}
	ticks240 = out;
	return true;
}

/* behavior/variables.json by name. */
struct Variables {
	std::map<std::string, Json> values;
};

/* Reads behavior/variables.json: its schema, an optional catalog_id that must be
 * this vehicle's, and the named values (a number, a bool, a string or a time). */
bool readVariables(const char *json, size_t size, const char *catalog_id,
	Variables &vars, std::string &error)
{
	Json root;
	if (!modAssetJsonReadValue(json, size, root) || !root.is_object()) {
		error = "behavior/variables.json is not one JSON object";
		return false;
	}
	if (!root.contains("schema") || !root["schema"].is_string() ||
			root["schema"].get<crude_json::string>() != VEHICLE_VARIABLES_SCHEMA) {
		error = "behavior/variables.json needs \"schema\": \"" VEHICLE_VARIABLES_SCHEMA "\"";
		return false;
	}
	for (const auto &entry : root.get<crude_json::object>()) {
		const std::string &name = entry.first;
		const Json &value = entry.second;
		if (name == "schema") continue;
		if (name == "comment") { // free text, not a variable
			if (!value.is_string()) {
				error = "behavior/variables.json comment must be a string";
				return false;
			}
			continue;
		}
		if (name == "catalog_id") {
			if (!value.is_string()) {
				error = "behavior/variables.json catalog_id must be a string";
				return false;
			}
			if (catalog_id && value.get<crude_json::string>() != catalog_id) {
				error = "behavior/variables.json is for '" + value.get<crude_json::string>() +
					"', not '" + catalog_id + "'";
				return false;
			}
			continue;
		}
		if (!isLowerName(name, 63)) {
			error = "variable name '" + name + "' must be 1 to 63 of a-z, 0-9 and _";
			return false;
		}
		if (value.is_number()) {
			if (!std::isfinite(value.get<crude_json::number>())) {
				error = "variable '" + name + "' is not a finite number";
				return false;
			}
		} else if (value.is_object()) {
			f32 ticks = 0.0f;
			std::string why;
			if (!readTimeObject(value, ticks, why)) {
				error = "variable '" + name + "' " + why;
				return false;
			}
		} else if (!value.is_boolean() && !value.is_string()) {
			error = "variable '" + name + "' must be a number, a bool, a string or {value, unit}";
			return false;
		}
		vars.values[name] = value;
	}
	return true;
}

/* Reads one node's params. "$name" takes the variable whole; unknown keys and
 * wrong types refuse the graph. */
class Params {
public:
	Params(const Node &node, const Json *params, const Variables &vars, std::string &error)
		: m_node(node), m_params(params), m_vars(vars), m_error(error)
	{
	}

	bool onlyKeys(const char *const *allowed)
	{
		if (!m_params) return true;
		if (!m_params->is_object()) {
			m_error = where() + " params must be an object";
			return false;
		}
		for (const auto &entry : m_params->get<crude_json::object>()) {
			bool known = false;
			for (const char *const *key = allowed; *key; key++) {
				if (entry.first == *key) known = true;
			}
			if (!known) {
				m_error = where() + " has no param '" + entry.first + "'";
				return false;
			}
		}
		return true;
	}

	bool number(const char *key, double lo, double hi, f32 &out)
	{
		const Json *value = nullptr;
		if (!get(key, true, value)) return false;
		if (!value->is_number() || !std::isfinite(value->get<crude_json::number>())) {
			return fail(key, "must be a number");
		}
		const double number = value->get<crude_json::number>();
		if (number < lo || number > hi) {
			return fail(key, "must be from " + text(lo) + " to " + text(hi));
		}
		out = static_cast<f32>(number);
		return true;
	}

	bool integer(const char *key, double lo, double hi, s32 &out)
	{
		f32 number = 0.0f;
		if (!this->number(key, lo, hi, number)) return false;
		if (std::floor(number) != number) return fail(key, "must be a whole number");
		out = static_cast<s32>(number);
		return true;
	}

	bool boolean(const char *key, bool required, int fallback, int &out)
	{
		const Json *value = nullptr;
		if (!get(key, required, value)) return false;
		if (!value) {
			out = fallback;
			return true;
		}
		if (!value->is_boolean()) return fail(key, "must be true or false");
		out = value->get<crude_json::boolean>() ? 1 : 0;
		return true;
	}

	bool time(const char *key, bool positive, f32 &ticks240)
	{
		const Json *value = nullptr;
		if (!get(key, true, value)) return false;
		std::string why;
		if (!readTimeObject(*value, ticks240, why)) return fail(key, why);
		if (positive && ticks240 <= 0.0f) return fail(key, "must be longer than 0");
		return true;
	}

	bool string(const char *key, std::string &out)
	{
		const Json *value = nullptr;
		if (!get(key, true, value)) return false;
		if (!value->is_string()) return fail(key, "must be a string");
		out = value->get<crude_json::string>();
		return true;
	}

	bool seat(const char *key, int &out)
	{
		std::string name;
		if (!string(key, name)) return false;
		if (name == "driver") out = VEHICLE_GRAPH_SEAT_DRIVER;
		else if (name == "gunner") out = VEHICLE_GRAPH_SEAT_GUNNER;
		else return fail(key, "must be \"driver\" or \"gunner\"");
		return true;
	}

	bool phases(const char *key, u32 &mask)
	{
		const Json *value = nullptr;
		if (!get(key, true, value)) return false;
		if (!value->is_array() || value->get<crude_json::array>().empty()) {
			return fail(key, "must be a list of heat phases");
		}
		mask = 0;
		for (const Json &item : value->get<crude_json::array>()) {
			int phase = -1;
			if (item.is_string()) {
				for (int i = 0; i < VEHICLE_BOOST_HEAT_PHASE_COUNT; i++) {
					if (item.get<crude_json::string>() == vehicleBoostHeatPhaseName(i)) phase = i;
				}
			}
			if (phase < 0) {
				return fail(key, "may name only cool, ramping, full, overheated and recovering");
			}
			if (mask & (1u << phase)) return fail(key, "names a phase twice");
			mask |= 1u << phase;
		}
		return true;
	}

	bool fail(const char *key, const std::string &why)
	{
		m_error = where() + " param '" + key + "' " + why;
		return false;
	}

private:
	bool get(const char *key, bool required, const Json *&out)
	{
		out = nullptr;
		if (m_params && m_params->is_object() && m_params->contains(key)) {
			out = &(*m_params)[key];
		}
		if (!out) {
			return required ? fail(key, "is missing") : true;
		}
		if (out->is_string()) {
			const std::string &name = out->get<crude_json::string>();
			if (!name.empty() && name[0] == '$') {
				auto found = m_vars.values.find(name.substr(1));
				if (found == m_vars.values.end()) {
					return fail(key, "uses unknown variable '" + name + "'");
				}
				out = &found->second;
			}
		}
		return true;
	}

	std::string where() const
	{
		return "node '" + m_node.id + "' (" + m_node.kind + ")";
	}

	static std::string text(double value)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%g", value);
		return buf;
	}

	const Node &m_node;
	const Json *m_params;
	const Variables &m_vars;
	std::string &m_error;
};

/* A native module's params into its node, each with its type and its range. */
bool readModuleParams(Node &node, Params &read, std::string &error)
{
	switch (node.module) {
	case MOD_MOUNT_SEAT:
	case MOD_DISMOUNT_SEAT:
		return read.seat("seat", node.seat);
	case MOD_HOVER_DRIVE:
		return read.boolean("drive", false, 1, node.drive);
	case MOD_THROTTLE_BOOST:
		return read.number("boost_speed", 0.0, kMaxSpeed, node.boost_speed) &&
			read.time("ramp_time", true, node.ramp_ticks240);
	case MOD_BOOST_HEAT: {
		vehicle_boost_heat_params_t &p = node.heat;
		if (!read.number("boost_speed", 0.0, kMaxSpeed, p.boost_speed) ||
				!read.time("ramp_time", true, p.ramp_ticks240) ||
				!read.time("overheat_after", true, p.overheat_ticks240) ||
				!read.number("overheated_boost", 0.0, 1.0, p.overheated_boost) ||
				!read.number("steer_limit", 0.0, 1.0, p.steer_limit) ||
				!read.time("steer_limit_time", false, p.steer_limit_ticks240) ||
				!read.time("recover_time_stopped", true, p.recover_ticks240) ||
				!read.number("recover_speed_scale", 0.0, kMaxRecoverScale, p.recover_speed_scale)) {
			return false;
		}
		const char *why = nullptr;
		if (!vehicleBoostHeatParamsValid(&p, &why)) {
			return read.fail(why ? why : "boost_speed", "is out of range");
		}
		return true;
	}
	case MOD_PART_VISIBLE:
		return read.integer("part", 0.0, 32767.0, node.part) &&
			read.boolean("visible", true, 1, node.visible);
	case MOD_TURRET_LOCKOUT:
		if (!read.string("reason", node.reason_text) ||
				!read.boolean("locked", true, 0, node.locked)) {
			return false;
		}
		if (!isLowerName(node.reason_text, 31)) {
			return read.fail("reason", "must be 1 to 31 of a-z, 0-9 and _");
		}
		return true;
	default:
		error = "node '" + node.id + "' has an unknown module";
		return false;
	}
}

/* ---------------------------------------------------------------------------
 * 3. Compile
 * Nodes, edges and the rules proved before a graph may run. A graph that
 * breaks one is refused with the reason; nothing is repaired or skipped.
 * ------------------------------------------------------------------------- */

/* One node from JSON. Its kind's prefix says what it is: an event, a gate or a
 * native module; any other kind is a data node, kept but never walked. Only
 * id, kind and params are read, so a node may carry a "comment" (and an
 * editor its layout) without changing what the graph does. */
bool parseNode(const Json &source, size_t index, const Variables &vars, Node &node,
	std::string &error)
{
	if (!source.is_object() || !source.contains("id") || !source["id"].is_string() ||
			!source.contains("kind") || !source["kind"].is_string()) {
		error = "node " + std::to_string(index) + " needs a string id and kind";
		return false;
	}
	node.id = source["id"].get<crude_json::string>();
	node.kind = source["kind"].get<crude_json::string>();
	if (node.id.empty() || node.kind.empty()) {
		error = "node " + std::to_string(index) + " needs a non-empty id and kind";
		return false;
	}
	if (!commentOk(source, "node '" + node.id + "'", error)) return false;
	const Json *params = source.contains("params") ? &source["params"] : nullptr;
	Params read(node, params, vars, error);

	if (startsWith(node.kind, "event.")) {
		for (const EventSpec &spec : kEvents) {
			if (node.kind == spec.kind) {
				node.type = NODE_EVENT;
				node.event_kind = spec.event;
			}
		}
		if (node.type != NODE_EVENT) {
			error = "event kind '" + node.kind + "' is not a vehicle event";
			return false;
		}
		const char *const none[] = { nullptr };
		return read.onlyKeys(none);
	}
	if (startsWith(node.kind, "gate.")) {
		const GateSpec *gate = nullptr;
		for (const GateSpec &spec : kGates) {
			if (node.kind == spec.kind) gate = &spec;
		}
		if (!gate) {
			error = "gate kind '" + node.kind + "' is not executable";
			return false;
		}
		node.type = NODE_GATE;
		node.gate = gate->gate;
		if (!read.onlyKeys(gate->params)) return false;
		if (node.gate == GATE_HEAT_PHASE) return read.phases("phases", node.phase_mask);
		return read.seat("seat", node.seat);
	}
	if (startsWith(node.kind, "og.")) {
		const ModuleSpec *module = nullptr;
		for (const ModuleSpec &spec : kModules) {
			if (node.kind == spec.kind) module = &spec;
		}
		if (!module) {
			error = "native module '" + node.kind + "' does not exist";
			return false;
		}
		node.type = NODE_MODULE;
		node.module = module->module;
		node.events = module->events;
		node.raises = module->raises;
		return read.onlyKeys(module->params) && readModuleParams(node, read, error);
	}
	node.type = NODE_DATA; // checked here, never walked
	return true;
}

/* The edges, by the mission graph runner's rules. An execution edge carries
 * "input": "exec", starts at an event, a gate or a module and ends at a gate
 * or a module; a gate's output is "pass" or "fail", any other "out". An
 * edge with no input is structural: checked for its ends, never walked. */
bool readEdges(const Json &root, std::map<std::string, int> &index, std::vector<Node> &nodes,
	int &exec_edges, std::string &error)
{
	if (!root.contains("edges")) return true;
	if (!root["edges"].is_array()) {
		error = "edges must be an array";
		return false;
	}
	const crude_json::array &edges = root["edges"].get<crude_json::array>();
	if (edges.size() > VEHICLE_GRAPH_MAX_EDGES) {
		error = "more than " + std::to_string(VEHICLE_GRAPH_MAX_EDGES) + " edges";
		return false;
	}
	for (size_t i = 0; i < edges.size(); i++) {
		const Json &edge = edges[i];
		const std::string where = "edge " + std::to_string(i);
		if (!edge.is_object() || !edge.contains("from") || !edge["from"].is_string() ||
				!edge.contains("to") || !edge["to"].is_string()) {
			error = where + " needs from and to";
			return false;
		}
		if (!commentOk(edge, where, error)) return false;
		const std::string &from = edge["from"].get<crude_json::string>();
		const std::string &to = edge["to"].get<crude_json::string>();
		auto source = index.find(from);
		auto target = index.find(to);
		if (source == index.end() || target == index.end()) {
			error = where + " names a missing node ('" + (source == index.end() ? from : to) + "')";
			return false;
		}
		const bool has_input = edge.contains("input");
		if (has_input && (!edge["input"].is_string() ||
				edge["input"].get<crude_json::string>() != "exec")) {
			error = where + " input must be \"exec\"";
			return false;
		}
		const bool has_output = edge.contains("output");
		std::string output;
		if (has_output) {
			if (!edge["output"].is_string()) {
				error = where + " output must be a string";
				return false;
			}
			output = edge["output"].get<crude_json::string>();
		}
		if (!has_input) {
			if (has_output) {
				error = where + " has an output but is not an execution edge";
				return false;
			}
			continue; // structural edge: data, never walked
		}
		Node &from_node = nodes[source->second];
		const Node &to_node = nodes[target->second];
		if (from_node.type == NODE_DATA) {
			error = where + " starts at '" + from + "' (" + from_node.kind + "), which cannot execute";
			return false;
		}
		if (to_node.type != NODE_GATE && to_node.type != NODE_MODULE) {
			error = where + " ends at '" + to + "' (" + to_node.kind +
				"), which is not a gate or a native module";
			return false;
		}
		Port port = PORT_OUT;
		if (from_node.type == NODE_GATE) {
			if (output == "pass") port = PORT_PASS;
			else if (output == "fail") port = PORT_FAIL;
			else {
				error = where + " leaves gate '" + from + "' and needs output \"pass\" or \"fail\"";
				return false;
			}
		} else if (has_output && output != "out") {
			error = where + " output must be \"out\"";
			return false;
		}
		from_node.out.push_back(Edge{ target->second, port });
		exec_edges++;
	}
	return true;
}

/* A tick's drive before any module runs: no hover, no drive, no extra speed,
 * full steering. The tick's modules then record what they ask for. */
void resetDrive(vehicle_graph_drive_t &drive)
{
	drive.hover = 0;
	drive.drive = 0;
	drive.extra_speed = 0.0f;
	drive.steer_scale = 1.0f;
}

/* Copies a refusal into the caller's buffer. */
void setError(char *err, size_t err_cap, const std::string &text)
{
	if (err && err_cap) {
		std::snprintf(err, err_cap, "%s", text.c_str());
	}
}

} // namespace

/* A compiled graph. Every vehicle of its catalog entry shares it, and nothing
 * changes it after it compiles. */
struct vehicle_graph_program {
	std::vector<Node> nodes;
	/* Per event kind: the nodes its event nodes lead to first, and every node
	 * its walk can reach (both branches of every gate). */
	std::vector<int> roots[VEHICLE_GRAPH_EVENT_KIND_END];
	std::vector<char> reach[VEHICLE_GRAPH_EVENT_KIND_END];
	int exec_edges = 0;
	int throttle_slots = 0;          // one timer per throttle_boost node
	int boost_heat_node = -1;        // the one boost_heat node, -1 without one
	vehicle_boost_heat_params_t boost_heat{};
	/* The hover_drive's drive param (0 without one): when 0 the game zeroes
	 * the driver's inputs, so the throttle bonus sees no throttle either. */
	int drive_takes_input = 0;
	std::vector<std::string> reasons; // turret lockout reasons, by bit
	std::vector<s32> parts;          // the model parts its part_visible nodes name
	int pending_bound = 0;           // the most raised events one fired event leads to
};

/* One vehicle's state for its program. Everything is sized when the instance
 * is made, from what the compiler proved, so a walk allocates nothing. */
struct vehicle_graph_instance {
	const vehicle_graph_program *program = nullptr;
	std::vector<f32> throttle;       // throttle_boost timers, 240 Hz ticks
	vehicle_boost_heat_state_t heat{};
	std::vector<unsigned char> reached; // per node: ran in this activation
	std::vector<int> queue;          // the walk's queue, one slot per execution edge
	std::vector<vehicle_graph_event_t> pending; // raised events, sized by pending_bound
	size_t pending_head = 0;
	size_t pending_tail = 0;
	u32 lock_mask = 0;               // turret lockout reasons now set
	vehicle_graph_drive_t drive{};   // what the last tick asked of the movement
};

namespace {

/* The nodes an event's walk can reach (both branches of every gate). */
std::vector<char> reachFrom(const vehicle_graph_program &program, int event_kind)
{
	std::vector<char> reached(program.nodes.size(), 0);
	std::vector<int> queue(program.roots[event_kind]);
	for (size_t head = 0; head < queue.size(); head++) {
		const int at = queue[head];
		if (reached[at]) continue;
		reached[at] = 1;
		for (const Edge &edge : program.nodes[at].out) queue.push_back(edge.target);
	}
	return reached;
}

/* How many follow-up events one activation of event_kind can lead to: each
 * raising node it can reach raises once, and each raised event leads to its
 * own. A raised event already being delivered refuses the graph. */
bool pendingBound(const vehicle_graph_program &program, int event_kind, int *state,
	long long *memo, long long &out, std::string &error)
{
	if (state[event_kind] == 2) {
		out = memo[event_kind];
		return true;
	}
	if (state[event_kind] == 1) {
		error = std::string(eventName(event_kind)) +
			" can raise itself again while it is being delivered";
		return false;
	}
	state[event_kind] = 1;
	long long total = 0;
	for (size_t i = 0; i < program.nodes.size(); i++) {
		const Node &node = program.nodes[i];
		if (!program.reach[event_kind][i] || node.type != NODE_MODULE || !node.raises) continue;
		long long nested = 0;
		if (!pendingBound(program, node.raises, state, memo, nested, error)) {
			error += " (" + node.kind + " '" + node.id + "' raises " + eventName(node.raises) + ")";
			return false;
		}
		total += 1 + nested;
		if (total > VEHICLE_GRAPH_MAX_PENDING) {
			total = VEHICLE_GRAPH_MAX_PENDING + 1;
			break;
		}
	}
	state[event_kind] = 2;
	memo[event_kind] = total;
	out = total;
	return true;
}

/* Compiles a graph with its variables, or returns null with the reason. In
 * order: the variables, the graph's schema and catalog_id, every node (one
 * hover_drive and one boost_heat at most, the model's parts when a part check
 * is given, the lockout reasons), the edges, then for each event its reach,
 * the modules that may not run on it, one mount and one dismount per seat, and
 * last the bound on raised events. */
std::unique_ptr<vehicle_graph_program> compile(const char *graph_json, size_t graph_size,
	const char *variables_json, size_t variables_size, const char *catalog_id,
	vehicle_graph_has_part_fn has_part, void *has_part_context, std::string &error)
{
	if (!graph_json || !graph_size) {
		error = "empty vehicle graph";
		return nullptr;
	}
	Variables vars;
	if (variables_json && variables_size &&
			!readVariables(variables_json, variables_size, catalog_id, vars, error)) {
		return nullptr;
	}
	Json root;
	if (!modAssetJsonReadValue(graph_json, graph_size, root) || !root.is_object()) {
		error = "behavior.graph.json is not one JSON object";
		return nullptr;
	}
	if (!root.contains("schema") || !root["schema"].is_string() ||
			root["schema"].get<crude_json::string>() != VEHICLE_GRAPH_SCHEMA) {
		error = "behavior.graph.json needs \"schema\": \"" VEHICLE_GRAPH_SCHEMA "\"";
		return nullptr;
	}
	if (!root.contains("catalog_id") || !root["catalog_id"].is_string() ||
			root["catalog_id"].get<crude_json::string>().empty()) {
		error = "behavior.graph.json needs its vehicle's catalog_id";
		return nullptr;
	}
	if (catalog_id && root["catalog_id"].get<crude_json::string>() != catalog_id) {
		error = "behavior.graph.json is for '" + root["catalog_id"].get<crude_json::string>() +
			"', not '" + catalog_id + "'";
		return nullptr;
	}
	if (!commentOk(root, "behavior.graph.json", error)) return nullptr;
	if (!root.contains("nodes") || !root["nodes"].is_array()) {
		error = "behavior.graph.json has no nodes array";
		return nullptr;
	}
	const crude_json::array &nodes = root["nodes"].get<crude_json::array>();
	if (nodes.size() > VEHICLE_GRAPH_MAX_NODES) {
		error = "more than " + std::to_string(VEHICLE_GRAPH_MAX_NODES) + " nodes";
		return nullptr;
	}

	auto program = std::make_unique<vehicle_graph_program>();
	std::map<std::string, int> index;
	int drives = 0;
	for (size_t i = 0; i < nodes.size(); i++) {
		Node node;
		if (!parseNode(nodes[i], i, vars, node, error)) return nullptr;
		if (index.count(node.id)) {
			error = "node id '" + node.id + "' is used twice";
			return nullptr;
		}
		const int at = static_cast<int>(program->nodes.size());
		if (node.module == MOD_HOVER_DRIVE && ++drives > 1) {
			error = "a vehicle graph has one og.vehicle.hover_drive ('" + node.id + "' is a second)";
			return nullptr;
		}
		if (node.module == MOD_HOVER_DRIVE) program->drive_takes_input = node.drive ? 1 : 0;
		if (node.module == MOD_BOOST_HEAT) {
			if (program->boost_heat_node >= 0) {
				error = "a vehicle graph has one og.vehicle.boost_heat ('" + node.id + "' is a second)";
				return nullptr;
			}
			program->boost_heat_node = at;
			program->boost_heat = node.heat;
		}
		if (node.module == MOD_THROTTLE_BOOST) node.slot = program->throttle_slots++;
		if (node.module == MOD_PART_VISIBLE) {
			if (has_part && !has_part(has_part_context, node.part)) {
				error = "node '" + node.id + "' (og.vehicle.part_visible) names part " +
					std::to_string(node.part) + ", which the vehicle's model does not have";
				return nullptr;
			}
			if (std::find(program->parts.begin(), program->parts.end(), node.part) ==
					program->parts.end()) {
				program->parts.push_back(node.part);
			}
		}
		if (node.module == MOD_TURRET_LOCKOUT) {
			auto found = std::find(program->reasons.begin(), program->reasons.end(), node.reason_text);
			if (found == program->reasons.end()) {
				if (program->reasons.size() >= VEHICLE_GRAPH_MAX_LOCKOUT_REASONS) {
					error = "more than " + std::to_string(VEHICLE_GRAPH_MAX_LOCKOUT_REASONS) +
						" turret lockout reasons";
					return nullptr;
				}
				program->reasons.push_back(node.reason_text);
				found = program->reasons.end() - 1;
			}
			node.reason = static_cast<int>(found - program->reasons.begin());
		}
		index[node.id] = at;
		program->nodes.push_back(std::move(node));
	}

	if (!readEdges(root, index, program->nodes, program->exec_edges, error)) return nullptr;

	for (size_t i = 0; i < program->nodes.size(); i++) {
		const Node &node = program->nodes[i];
		if (node.type != NODE_EVENT) continue;
		for (const Edge &edge : node.out) program->roots[node.event_kind].push_back(edge.target);
	}
	for (int kind = 1; kind < VEHICLE_GRAPH_EVENT_KIND_END; kind++) {
		/* One event mounts or dismounts a seat at most once (CORE's review,
		 * 2026-11-16): the host's mount and dismount change the rider, so a
		 * second one for the same seat would act on a rider who has gone. */
		int seat_modules[2][2] = { { 0, 0 }, { 0, 0 } };
		program->reach[kind] = reachFrom(*program, kind);
		for (size_t i = 0; i < program->nodes.size(); i++) {
			const Node &node = program->nodes[i];
			if (!program->reach[kind][i] || node.type != NODE_MODULE) continue;
			if (!(node.events & eventBit(kind))) {
				error = node.kind + " '" + node.id + "' cannot run on " + eventName(kind) +
					", which reaches it";
				return nullptr;
			}
			if ((node.module == MOD_MOUNT_SEAT || node.module == MOD_DISMOUNT_SEAT) &&
					node.seat >= 0 && node.seat < 2 &&
					++seat_modules[node.module == MOD_DISMOUNT_SEAT][node.seat] > 1) {
				error = std::string(eventName(kind)) + " reaches a second " + node.kind +
					" for the " + (node.seat == VEHICLE_GRAPH_SEAT_DRIVER ? "driver" : "gunner") +
					" seat ('" + node.id + "'); one event mounts or dismounts a seat once";
				return nullptr;
			}
		}
	}

	int state[VEHICLE_GRAPH_EVENT_KIND_END] = { 0 };
	long long memo[VEHICLE_GRAPH_EVENT_KIND_END] = { 0 };
	long long bound = 0;
	for (int kind : kFiredEvents) {
		long long count = 0;
		if (!pendingBound(*program, kind, state, memo, count, error)) return nullptr;
		bound = std::max(bound, count);
	}
	if (bound > VEHICLE_GRAPH_MAX_PENDING) {
		error = "one event can lead to more than " + std::to_string(VEHICLE_GRAPH_MAX_PENDING) +
			" raised events";
		return nullptr;
	}
	program->pending_bound = static_cast<int>(bound);
	return program;
}

/* ---------------------------------------------------------------------------
 * 4. Run
 * One activation: the walk starts at the fired event's roots and runs each
 * node it reaches once, breadth first in authored order. A gate picks its
 * pass or fail edges; a module does its work and the walk goes on from it.
 * ------------------------------------------------------------------------- */

/* An event a native seam may fire, with fields a module can trust. */
bool eventValid(const vehicle_graph_event_t &event)
{
	switch (event.kind) {
	case VEHICLE_GRAPH_EVENT_MOUNT_REQUESTED:
	case VEHICLE_GRAPH_EVENT_DISMOUNT_REQUESTED:
		return event.seat == VEHICLE_GRAPH_SEAT_DRIVER || event.seat == VEHICLE_GRAPH_SEAT_GUNNER;
	case VEHICLE_GRAPH_EVENT_TICK:
		return event.ticks240 >= 0 && std::isfinite(event.ticks60) && event.ticks60 >= 0.0f &&
			std::isfinite(event.throttle) && std::isfinite(event.speed_frac);
	default:
		return false; // heat_changed is raised by the graph, never fired
	}
}

/* A gate's answer for this event: the event's seat, a free seat (the host
 * says), or the heat phase now (cool for a graph with no boost_heat). */
bool gatePasses(const vehicle_graph_instance &instance, const Node &node,
	const vehicle_graph_event_t &event, const vehicle_graph_host_t *host)
{
	switch (node.gate) {
	case GATE_SEAT:
		return (event.kind == VEHICLE_GRAPH_EVENT_MOUNT_REQUESTED ||
				event.kind == VEHICLE_GRAPH_EVENT_DISMOUNT_REQUESTED) && event.seat == node.seat;
	case GATE_SEAT_FREE:
		return host && host->seat_free && host->seat_free(host->context, node.seat);
	case GATE_HEAT_PHASE: {
		const int phase = instance.program->boost_heat_node >= 0
			? instance.heat.phase : VEHICLE_BOOST_HEAT_COOL;
		return (node.phase_mask & (1u << phase)) != 0;
	}
	default:
		return false;
	}
}

/* Runs one native module. The seat and part modules ask the host (the game);
 * the tick's modules record the drive they ask for, which the movement reads
 * after the walk, so the physics runs in the original's order.
 * 1 ran, 0 refused, -1 the instance broke its proved bound (never expected). */
int runModule(vehicle_graph_instance &instance, const Node &node,
	const vehicle_graph_event_t &event, const vehicle_graph_host_t *host)
{
	const vehicle_graph_program &program = *instance.program;
	switch (node.module) {
	case MOD_MOUNT_SEAT:
		return host && host->mount_seat && host->mount_seat(host->context, node.seat, &event) ? 1 : 0;
	case MOD_DISMOUNT_SEAT:
		return host && host->dismount_seat &&
			host->dismount_seat(host->context, node.seat, &event) ? 1 : 0;
	case MOD_HOVER_DRIVE:
		instance.drive.hover = 1;
		if (node.drive) instance.drive.drive = 1;
		return 1;
	case MOD_THROTTLE_BOOST: {
		// The original full-throttle bonus (hoverbikeUpdateMovement), with its
		// timer (maxspeedtime240) in this node's state.
		// With hover_drive {drive: false} the game zeroes the driver's inputs,
		// so the bonus reads no throttle either (CORE's review, 2026-11-16).
		const f32 throttle = program.drive_takes_input ? event.throttle : 0.0f;
		f32 &time = instance.throttle[node.slot];
		if (throttle >= 0.99f) {
			time += static_cast<f32>(event.ticks240);
			if (time > node.ramp_ticks240) time = node.ramp_ticks240;
		} else if (time > 0.0f) {
			if (throttle >= 0.8f) {
				// held
			} else if (throttle >= -0.1f) {
				time -= static_cast<f32>(event.ticks240);
				if (time < 0.0f) time = 0.0f;
			} else {
				time = 0.0f;
			}
		}
		instance.drive.extra_speed += time * node.boost_speed / node.ramp_ticks240;
		return 1;
	}
	case MOD_BOOST_HEAT: {
		s32 previous = instance.heat.phase;
		const s32 changed = vehicleBoostHeatStep(&instance.heat, &program.boost_heat,
			event.boost_held != 0, event.speed_frac, static_cast<f32>(event.ticks240), &previous);
		instance.drive.extra_speed += vehicleBoostHeatExtraSpeed(&instance.heat, &program.boost_heat);
		instance.drive.steer_scale *= vehicleBoostHeatSteerScale(&instance.heat, &program.boost_heat);
		if (changed) {
			if (instance.pending_tail >= instance.pending.size()) return -1;
			vehicle_graph_event_t &raised = instance.pending[instance.pending_tail++];
			std::memset(&raised, 0, sizeof(raised));
			raised.kind = VEHICLE_GRAPH_EVENT_HEAT_CHANGED;
			raised.seat = VEHICLE_GRAPH_SEAT_NONE;
			raised.player = -1;
			raised.phase = instance.heat.phase;
			raised.previous_phase = previous;
		}
		return 1;
	}
	case MOD_PART_VISIBLE:
		return host && host->set_part_visible &&
			host->set_part_visible(host->context, node.part, node.visible) ? 1 : 0;
	case MOD_TURRET_LOCKOUT:
		if (node.locked) instance.lock_mask |= 1u << node.reason;
		else instance.lock_mask &= ~(1u << node.reason);
		return 1;
	default:
		return 0;
	}
}

/* One activation. Returns the modules run, or -1 when the instance broke a
 * proved bound (never expected); *refused is set when a module refused. */
s32 walk(vehicle_graph_instance &instance, const vehicle_graph_event_t &event,
	const vehicle_graph_host_t *host, bool &refused)
{
	const vehicle_graph_program &program = *instance.program;
	if (event.kind == VEHICLE_GRAPH_EVENT_TICK) resetDrive(instance.drive);
	std::fill(instance.reached.begin(), instance.reached.end(), 0);
	size_t count = 0;
	auto push = [&](int target) {
		if (count >= instance.queue.size()) return false;
		instance.queue[count++] = target;
		return true;
	};
	for (int target : program.roots[event.kind]) {
		if (!push(target)) return -1;
	}
	s32 ran = 0;
	// Breadth first in authored order; each node runs at most once.
	for (size_t head = 0; head < count; head++) {
		const int at = instance.queue[head];
		if (instance.reached[at]) continue;
		instance.reached[at] = 1;
		const Node &node = program.nodes[at];
		if (node.type == NODE_GATE) {
			const bool pass = gatePasses(instance, node, event, host);
			for (const Edge &edge : node.out) {
				if ((pass && edge.port == PORT_PASS) || (!pass && edge.port == PORT_FAIL)) {
					if (!push(edge.target)) return -1;
				}
			}
		} else if (node.type == NODE_MODULE) {
			const int result = runModule(instance, node, event, host);
			if (result < 0) return -1;
			if (result == 0) {
				refused = true;
				continue;
			}
			ran++;
			for (const Edge &edge : node.out) {
				if (!push(edge.target)) return -1;
			}
		}
	}
	return ran;
}

} // namespace

/* ---------------------------------------------------------------------------
 * 5. C API
 * Declared and documented in port/include/vehicle_graph_runner.h. No
 * exception leaves these functions: compiling, reading a schema and making
 * an instance catch them, and the rest allocate nothing.
 * ------------------------------------------------------------------------- */

extern "C" s32 vehicleGraphIsV1(const char *json, size_t json_size)
{
	if (!json || !json_size) return 0;
	try {
		Json root;
		return modAssetJsonReadValue(json, json_size, root) && root.is_object() &&
			root.contains("schema") && root["schema"].is_string() &&
			root["schema"].get<crude_json::string>() == VEHICLE_GRAPH_SCHEMA;
	} catch (...) {
		return 0;
	}
}

extern "C" const char *vehicleGraphKnownKind(s32 index)
{
	const s32 events = static_cast<s32>(sizeof(kEvents) / sizeof(kEvents[0]));
	const s32 gates = static_cast<s32>(sizeof(kGates) / sizeof(kGates[0]));
	const s32 modules = static_cast<s32>(sizeof(kModules) / sizeof(kModules[0]));
	if (index < 0) return nullptr;
	if (index < events) return kEvents[index].kind;
	index -= events;
	if (index < gates) return kGates[index].kind;
	index -= gates;
	if (index < modules) return kModules[index].kind;
	return nullptr;
}

extern "C" vehicle_graph_program_t *vehicleGraphProgramCompile(const char *graph_json,
	size_t graph_size, const char *variables_json, size_t variables_size,
	const char *catalog_id, vehicle_graph_has_part_fn has_part, void *has_part_context,
	char *err, size_t err_cap)
{
	setError(err, err_cap, "");
	std::string error;
	try {
		std::unique_ptr<vehicle_graph_program> program = compile(graph_json, graph_size,
			variables_json, variables_size, catalog_id, has_part, has_part_context, error);
		if (program) return program.release();
	} catch (...) {
		error = "vehicle graph could not be read";
	}
	setError(err, err_cap, error.empty() ? "vehicle graph refused" : error);
	return nullptr;
}

extern "C" void vehicleGraphProgramFree(vehicle_graph_program_t *program)
{
	delete program;
}

extern "C" s32 vehicleGraphProgramNodeCount(const vehicle_graph_program_t *program)
{
	return program ? static_cast<s32>(program->nodes.size()) : 0;
}

extern "C" s32 vehicleGraphProgramExecEdgeCount(const vehicle_graph_program_t *program)
{
	return program ? program->exec_edges : 0;
}

extern "C" s32 vehicleGraphProgramEventReachesModule(const vehicle_graph_program_t *program,
	s32 event_kind, const char *module_kind)
{
	if (!program || !module_kind || event_kind <= 0 || event_kind >= VEHICLE_GRAPH_EVENT_KIND_END) {
		return 0;
	}
	// The event's own walk, then every event its reachable modules raise.
	bool seen[VEHICLE_GRAPH_EVENT_KIND_END] = { false };
	int work[VEHICLE_GRAPH_EVENT_KIND_END];
	int count = 0;
	work[count++] = event_kind;
	seen[event_kind] = true;
	for (int head = 0; head < count; head++) {
		const std::vector<char> &reach = program->reach[work[head]];
		for (size_t i = 0; i < program->nodes.size(); i++) {
			const Node &node = program->nodes[i];
			if (!reach[i] || node.type != NODE_MODULE) continue;
			if (node.kind == module_kind) return 1;
			if (node.raises && !seen[node.raises]) {
				seen[node.raises] = true;
				work[count++] = node.raises;
			}
		}
	}
	return 0;
}

extern "C" s32 vehicleGraphProgramPendingBound(const vehicle_graph_program_t *program)
{
	return program ? program->pending_bound : 0;
}

extern "C" const vehicle_boost_heat_params_t *vehicleGraphProgramBoostHeatParams(
	const vehicle_graph_program_t *program)
{
	return program && program->boost_heat_node >= 0 ? &program->boost_heat : nullptr;
}

extern "C" s32 vehicleGraphProgramLockoutReason(const vehicle_graph_program_t *program,
	const char *reason)
{
	if (!program || !reason) return -1;
	for (size_t i = 0; i < program->reasons.size(); i++) {
		if (program->reasons[i] == reason) return static_cast<s32>(i);
	}
	return -1;
}

extern "C" s32 vehicleGraphProgramPartCount(const vehicle_graph_program_t *program)
{
	return program ? static_cast<s32>(program->parts.size()) : 0;
}

extern "C" s32 vehicleGraphProgramPart(const vehicle_graph_program_t *program, s32 index)
{
	if (!program || index < 0 || index >= static_cast<s32>(program->parts.size())) return -1;
	return program->parts[static_cast<size_t>(index)];
}

extern "C" vehicle_graph_instance_t *vehicleGraphInstanceCreate(
	const vehicle_graph_program_t *program)
{
	if (!program) return nullptr;
	try {
		auto instance = std::make_unique<vehicle_graph_instance>();
		instance->program = program;
		instance->throttle.assign(static_cast<size_t>(program->throttle_slots), 0.0f);
		vehicleBoostHeatReset(&instance->heat);
		instance->reached.assign(program->nodes.size(), 0);
		instance->queue.assign(static_cast<size_t>(std::max(program->exec_edges, 1)), 0);
		instance->pending.resize(static_cast<size_t>(std::max(program->pending_bound, 1)));
		resetDrive(instance->drive);
		return instance.release();
	} catch (...) {
		return nullptr;
	}
}

extern "C" void vehicleGraphInstanceFree(vehicle_graph_instance_t *instance)
{
	delete instance;
}

extern "C" const vehicle_graph_program_t *vehicleGraphInstanceProgram(
	const vehicle_graph_instance_t *instance)
{
	return instance ? instance->program : nullptr;
}

extern "C" s32 vehicleGraphInstanceFire(vehicle_graph_instance_t *instance,
	const vehicle_graph_event_t *event, const vehicle_graph_host_t *host)
{
	if (!instance || !event || !eventValid(*event)) return -1;
	instance->pending_head = 0;
	instance->pending_tail = 0;
	bool refused = false;
	s32 ran = 0;
	vehicle_graph_event_t current = *event;
	for (;;) {
		const s32 result = walk(*instance, current, host, refused);
		if (result < 0) return -1;
		ran += result;
		if (instance->pending_head >= instance->pending_tail) break;
		current = instance->pending[instance->pending_head++];
	}
	return refused ? -1 : ran;
}

extern "C" void vehicleGraphInstanceDrive(const vehicle_graph_instance_t *instance,
	vehicle_graph_drive_t *out)
{
	if (!out) return;
	if (!instance) {
		resetDrive(*out);
		return;
	}
	*out = instance->drive;
}

extern "C" void vehicleGraphInstanceRest(vehicle_graph_instance_t *instance)
{
	if (instance) std::fill(instance->throttle.begin(), instance->throttle.end(), 0.0f);
}

extern "C" s32 vehicleGraphInstanceHeatPhase(const vehicle_graph_instance_t *instance)
{
	if (!instance || instance->program->boost_heat_node < 0) return VEHICLE_BOOST_HEAT_COOL;
	return instance->heat.phase;
}

extern "C" const vehicle_boost_heat_state_t *vehicleGraphInstanceBoostHeat(
	const vehicle_graph_instance_t *instance)
{
	return instance && instance->program->boost_heat_node >= 0 ? &instance->heat : nullptr;
}

extern "C" u32 vehicleGraphInstanceTurretLockMask(const vehicle_graph_instance_t *instance)
{
	return instance ? instance->lock_mask : 0;
}
