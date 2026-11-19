#include "scenario_portal_source.h"

#include "modasset_gltf_document.h"
#include "scenario_room_domain.h"
#include "../external/imgui-node-editor/crude_json.h"

#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>

namespace {

using Json = crude_json::value;

s32 reject(char *err, size_t cap, const char *reason, size_t row = 0)
{
	if (err && cap) {
		if (row) std::snprintf(err, cap, "portal row %zu: %s", row, reason);
		else std::snprintf(err, cap, "%s", reason);
	}
	return 0;
}

s32 roomNumber(const Json &value)
{
	if (!value.is_string()) return -1;
	const auto &ref = value.get<std::string>();
	const char *number = ref.c_str();
	while (*number && !std::isdigit(static_cast<unsigned char>(*number)) && *number != '-' && *number != '+') ++number;
	char *end = nullptr;
	const long parsed = std::strtol(number, &end, 10);
	return end != number && parsed > 0 && parsed < SCENARIO_ROOM_DOMAIN_MAX_COUNT ? static_cast<s32>(parsed) : -1;
}

bool readFlags(const Json &value, u32 &out)
{
	if (value.is_string()) {
		const auto &text = value.get<std::string>();
		char *end = nullptr;
		errno = 0;
		const auto flags = std::strtoll(text.c_str(), &end, 0);
		if (end == text.c_str() || end != text.c_str() + text.size() || errno == ERANGE
				|| flags < std::numeric_limits<s32>::min() || flags > std::numeric_limits<u32>::max()) return false;
		out = static_cast<u32>(flags);
		return true;
	}
	if (!value.is_number()) return false;
	const double flags = value.get<double>();
	if (!std::isfinite(flags) || flags < std::numeric_limits<s32>::min() || flags > std::numeric_limits<u32>::max()
			|| std::trunc(flags) != flags) return false;
	out = static_cast<u32>(static_cast<s64>(flags));
	return true;
}

bool readRow(const Json &source, scenario_source_portal_row_t &row)
{
	if (!source.is_object() || !source.contains("portal_ref") || !source["portal_ref"].is_string()
			|| !source.contains("room_a") || !source.contains("room_b")
			|| !source.contains("flags") || !readFlags(source["flags"], row.flags)
			|| !source.contains("vertices") || !source["vertices"].is_array()) return false;
	row.room1 = roomNumber(source["room_a"]);
	row.room2 = roomNumber(source["room_b"]);
	if (row.room1 <= 0 || row.room2 <= 0 || row.room1 == row.room2) return false;

	const auto &vertices = source["vertices"].get<crude_json::array>();
	if (vertices.size() < 3 || vertices.size() > SCENARIO_SOURCE_PORTAL_MAX_VERTICES) return false;
	row.vertex_count = static_cast<s32>(vertices.size());
	for (size_t i = 0; i < vertices.size(); ++i) {
		if (!vertices[i].is_array()) return false;
		const auto &point = vertices[i].get<crude_json::array>();
		if (point.size() != 3) return false;
		for (size_t axis = 0; axis < point.size(); ++axis) {
			if (!point[axis].is_number()) return false;
			const double coordinate = point[axis].get<double>();
			if (!std::isfinite(coordinate) || std::abs(coordinate) > std::numeric_limits<f32>::max()) return false;
			row.vertices[i][axis] = static_cast<f32>(coordinate);
		}
	}
	if (source.contains("light_flood_blocked") && source["light_flood_blocked"].is_boolean()
			&& source["light_flood_blocked"].get<bool>()) row.light_rules |= SCENARIO_PORTAL_RULE_FLOOD_BLOCKED;
	if (source.contains("open_to_light") && source["open_to_light"].is_boolean()
			&& source["open_to_light"].get<bool>()) row.light_rules |= SCENARIO_PORTAL_RULE_OPEN_TO_LIGHT;
	return true;
}

} // namespace

extern "C" s32 scenarioPortalSourceRead(const char *text, size_t size,
	scenario_source_portal_row_t **rows, s32 *row_count, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	Json source;
	if (!rows || !row_count || !modAssetJsonReadValue(text, size, source) || !source.is_object()
			|| !source.contains("schema") || !source["schema"].is_string()
			|| source["schema"].get<std::string>() != "pd2.scenario.portals.v1"
			|| !source.contains("rows") || !source["rows"].is_array()) {
		return reject(err, err_cap, "invalid native portal table");
	}
	const auto &input = source["rows"].get<crude_json::array>();
	if (input.size() > static_cast<size_t>(std::numeric_limits<s32>::max())) {
		return reject(err, err_cap, "too many native portal rows");
	}
	std::unique_ptr<scenario_source_portal_row_t, decltype(&std::free)> parsed(
		static_cast<scenario_source_portal_row_t *>(std::calloc(input.empty() ? 1 : input.size(),
			sizeof(scenario_source_portal_row_t))), std::free);
	if (!parsed) return reject(err, err_cap, "could not allocate native portal rows");
	for (size_t i = 0; i < input.size(); ++i) {
		if (!readRow(input[i], parsed.get()[i])) return reject(err, err_cap, "invalid native portal", i + 1);
	}
	*rows = parsed.release();
	*row_count = static_cast<s32>(input.size());
	return 1;
}

extern "C" s32 scenarioPortalSourceCheckCandidate(const scenario_source_portal_row_t *rows, s32 row_count,
	s32 declared_rooms, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!rows || row_count < 0) return reject(err, err_cap, "missing native portal table");
	s32 room_count = 0;
	if (scenarioRoomDomainResolve(declared_rooms, -1, -1, &room_count) != SCENARIO_ROOM_DOMAIN_OK) {
		return reject(err, err_cap, "invalid declared room count");
	}
	u8 links[SCENARIO_ROOM_DOMAIN_MAX_COUNT] = {};
	for (s32 i = 0; i < row_count; ++i) {
		const auto &row = rows[i];
		if (row.room1 <= 0 || row.room2 <= 0 || row.room1 == row.room2
				|| row.room1 >= SCENARIO_ROOM_DOMAIN_MAX_COUNT || row.room2 >= SCENARIO_ROOM_DOMAIN_MAX_COUNT
				|| (declared_rooms && (row.room1 >= room_count || row.room2 >= room_count))) {
			return reject(err, err_cap, "endpoint outside the candidate room domain", i + 1);
		}
		if (++links[row.room1] > 127 || ++links[row.room2] > 127) {
			return reject(err, err_cap, "room cannot represent more than 127 portal links", i + 1);
		}
		if (row.vertex_count < 3 || row.vertex_count > SCENARIO_SOURCE_PORTAL_MAX_VERTICES) {
			return reject(err, err_cap, "invalid vertex count", i + 1);
		}
		/* Use the native BG plane calculation and precision. */
		f32 normal[3] = {};
		for (s32 j = 0; j < row.vertex_count; ++j) {
			const auto &a = row.vertices[j];
			const auto &b = row.vertices[(j + 1) % row.vertex_count];
			normal[0] += (a[1] - b[1]) * (a[2] + b[2]);
			normal[1] += (a[2] - b[2]) * (a[0] + b[0]);
			normal[2] += (a[0] - b[0]) * (a[1] + b[1]);
		}
		const f32 squared = normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2];
		if (!std::isfinite(squared) || squared <= 0.0f) {
			return reject(err, err_cap, "no finite nonzero portal plane", i + 1);
		}
		const f32 divisor = -std::sqrt(squared);
		for (s32 axis = 0; axis < 3; ++axis) normal[axis] /= divisor;
		for (s32 j = 0; j < row.vertex_count; ++j) {
			const auto &point = row.vertices[j];
			const f32 distance = point[0] * normal[0] + point[1] * normal[1] + point[2] * normal[2];
			if (!std::isfinite(distance)) return reject(err, err_cap, "portal plane distance exceeds native range", i + 1);
		}
	}
	return 1;
}
