// The toggle parts of a vehicle's typed mesh: see port/include/vehicle_model_parts.h.
#include "vehicle_model_parts.h"

#include "modasset_gltf_document.h"
#include "../external/imgui-node-editor/crude_json.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>

/* Part number to "its node is a toggle". */
struct vehicle_model_parts {
	std::map<s32, bool> parts;
	s32 toggles = 0;
};

namespace {

using Json = crude_json::value;

constexpr const char *kPartsSchema = "pd2.mesh.parts.v1";
constexpr const char *kNodesSchema = "pd2.mesh.nodes.v1";
/* MODELNODETYPE_TOGGLE (src/include/constants.h). */
constexpr int kToggleNodeType = 0x12;

/* A whole number in a JSON value, or false. */
bool readInt(const Json &object, const char *key, long long &out)
{
	if (!object.is_object() || !object.contains(key) || !object[key].is_number()) return false;
	const double number = object[key].get<crude_json::number>();
	if (!std::isfinite(number) || std::floor(number) != number) return false;
	out = static_cast<long long>(number);
	return true;
}

/* The table's root object, with its schema and its array checked. */
bool readTable(const char *json, size_t size, const char *file, const char *schema,
	const char *array, Json &root, std::string &error)
{
	if (!json || !size || !modAssetJsonReadValue(json, size, root) || !root.is_object()) {
		error = std::string(file) + " is not one JSON object";
		return false;
	}
	if (!root.contains("schema") || !root["schema"].is_string() ||
			root["schema"].get<crude_json::string>() != schema) {
		error = std::string(file) + " needs \"schema\": \"" + schema + "\"";
		return false;
	}
	if (!root.contains(array) || !root[array].is_array()) {
		error = std::string(file) + " has no " + array + " array";
		return false;
	}
	return true;
}

/* Both tables into the part map, or null with the reason. */
std::unique_ptr<vehicle_model_parts> read(const char *parts_json, size_t parts_size,
	const char *nodes_json, size_t nodes_size, std::string &error)
{
	Json nodes_root;
	Json parts_root;
	if (!readTable(nodes_json, nodes_size, "model.nodes.json", kNodesSchema, "nodes", nodes_root, error) ||
			!readTable(parts_json, parts_size, "model.parts.json", kPartsSchema, "parts", parts_root, error)) {
		return nullptr;
	}
	const crude_json::array &nodes = nodes_root["nodes"].get<crude_json::array>();
	const crude_json::array &parts = parts_root["parts"].get<crude_json::array>();

	auto out = std::make_unique<vehicle_model_parts>();
	for (size_t i = 0; i < parts.size(); i++) {
		const Json &part = parts[i];
		long long partnum = 0;
		long long node = 0;
		if (!readInt(part, "partnum", partnum) || !readInt(part, "node", node) ||
				partnum < 0 || partnum > 32767) {
			error = "model.parts.json part " + std::to_string(i) + " needs a partnum and a node";
			return nullptr;
		}
		/* The model compiler skips a part whose node could not be resolved. */
		if (node < 0 && part.contains("node_unresolved") && part["node_unresolved"].is_boolean() &&
				part["node_unresolved"].get<crude_json::boolean>()) {
			continue;
		}
		if (node < 0 || static_cast<size_t>(node) >= nodes.size()) {
			error = "model.parts.json part " + std::to_string(partnum) + " names node " +
				std::to_string(node) + ", which the mesh does not have";
			return nullptr;
		}
		long long type = 0;
		if (!readInt(nodes[static_cast<size_t>(node)], "type", type)) {
			error = "model.nodes.json node " + std::to_string(node) + " has no type";
			return nullptr;
		}
		const s32 key = static_cast<s32>(partnum);
		if (out->parts.count(key)) {
			error = "model.parts.json lists part " + std::to_string(partnum) + " twice";
			return nullptr;
		}
		const bool toggle = type == kToggleNodeType;
		out->parts[key] = toggle;
		if (toggle) out->toggles++;
	}
	return out;
}

} // namespace

extern "C" vehicle_model_parts_t *vehicleModelPartsRead(const char *parts_json, size_t parts_size,
	const char *nodes_json, size_t nodes_size, char *err, size_t err_cap)
{
	std::string error;
	if (err && err_cap) err[0] = '\0';
	try {
		std::unique_ptr<vehicle_model_parts> parts =
			read(parts_json, parts_size, nodes_json, nodes_size, error);
		if (parts) return parts.release();
	} catch (...) {
		error = "the mesh's part tables could not be read";
	}
	if (err && err_cap) {
		std::snprintf(err, err_cap, "%s", error.empty() ? "the mesh's part tables were refused"
			: error.c_str());
	}
	return nullptr;
}

extern "C" void vehicleModelPartsFree(vehicle_model_parts_t *parts)
{
	delete parts;
}

extern "C" s32 vehicleModelPartsIsToggle(void *context, s32 part)
{
	const vehicle_model_parts *parts = static_cast<const vehicle_model_parts *>(context);
	if (!parts) return 0;
	const auto found = parts->parts.find(part);
	return found != parts->parts.end() && found->second ? 1 : 0;
}

extern "C" s32 vehicleModelPartsCount(const vehicle_model_parts_t *parts)
{
	return parts ? static_cast<s32>(parts->parts.size()) : 0;
}

extern "C" s32 vehicleModelPartsToggleCount(const vehicle_model_parts_t *parts)
{
	return parts ? parts->toggles : 0;
}
