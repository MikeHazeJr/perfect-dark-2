// Stage-wide rules from stage.json (decision 3, CORE, 2026-11-15). See
// port/include/scenario_stage_rules.h for the format.
#include "scenario_stage_rules.h"

#include "modasset_gltf_document.h"
#include "../external/imgui-node-editor/crude_json.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

namespace {

using Json = crude_json::value;

struct Refusal {
	std::string reason;
};

void refuse(const std::string &reason)
{
	throw Refusal{ reason };
}

void onlyKeys(const Json &object, const std::set<std::string> &known, const std::string &where)
{
	for (const auto &item : object.get<crude_json::object>()) {
		if (!known.count(item.first)) refuse(where + " has an unknown key '" + item.first + "'");
	}
}

f32 number(const Json &object, const char *key, const std::string &where)
{
	if (!object.contains(key) || !object[key].is_number()) refuse(where + "." + key + " must be a number");
	const double value = object[key].get<crude_json::number>();
	if (!std::isfinite(value) || std::fabs(value) > 1.0e7) refuse(where + "." + key + " is out of range");
	return static_cast<f32>(value);
}

s32 wholeNumber(const Json &object, const char *key, s32 lo, s32 hi, const std::string &where)
{
	if (!object.contains(key) || !object[key].is_number()) refuse(where + "." + key + " must be a whole number");
	const double value = object[key].get<crude_json::number>();
	if (!std::isfinite(value) || value != std::floor(value) || value < lo || value > hi) {
		refuse(where + "." + key + " must be a whole number from " + std::to_string(lo) + " to " + std::to_string(hi));
	}
	return static_cast<s32>(value);
}

bool boolean(const Json &object, const char *key, const std::string &where)
{
	if (!object.contains(key)) return false;
	if (!object[key].is_boolean()) refuse(where + "." + key + " must be true or false");
	return object[key].get<crude_json::boolean>();
}

void readWeather(const Json &weather, scenario_stage_weather_t &out)
{
	const std::string where = "weather";
	if (!weather.is_object()) refuse("weather must be an object");
	onlyKeys(weather, { "wind_speed", "y_min", "y_max", "z_max", "cutscene_only", "weather_rooms",
		"dry_rooms", "wind_direction" }, where);
	out.present = 1;
	out.wind_speed = number(weather, "wind_speed", where);
	out.y_min = number(weather, "y_min", where);
	out.y_max = number(weather, "y_max", where);
	out.z_max = weather.contains("z_max") ? number(weather, "z_max", where) : 0.0f;
	if (!(out.y_min < out.y_max)) refuse("weather.y_min must be below weather.y_max");
	out.cutscene_only = boolean(weather, "cutscene_only", where) ? 1 : 0;

	const bool has_only = weather.contains("weather_rooms");
	const bool has_dry = weather.contains("dry_rooms");
	if (has_only == has_dry) refuse("weather needs exactly one of weather_rooms and dry_rooms");
	const char *rooms_key = has_only ? "weather_rooms" : "dry_rooms";
	out.rooms_have_weather = has_only ? 1 : 0;
	const Json &rooms = weather[rooms_key];
	if (!rooms.is_array()) refuse(where + "." + rooms_key + " must be a list of room numbers");
	const crude_json::array &list = rooms.get<crude_json::array>();
	if (list.size() > SCENARIO_STAGE_MAX_WEATHER_ROOMS) {
		refuse(where + "." + rooms_key + " names more than " +
			std::to_string(SCENARIO_STAGE_MAX_WEATHER_ROOMS) + " rooms");
	}
	for (const Json &room : list) {
		const double value = room.is_number() ? room.get<crude_json::number>() : -1.0;
		if (value < 1.0 || value > 32767.0 || value != std::floor(value)) {
			refuse(where + "." + rooms_key + " holds something that is not a room number 1..32767");
		}
		out.rooms[out.room_count++] = static_cast<s16>(value);
	}

	if (weather.contains("wind_direction")) {
		const Json &wind = weather["wind_direction"];
		if (!wind.is_object()) refuse("weather.wind_direction must be an object");
		onlyKeys(wind, { "angle", "speed_x", "speed_z" }, "weather.wind_direction");
		out.has_wind_direction = 1;
		out.wind_angle = number(wind, "angle", "weather.wind_direction");
		out.wind_speed_x = number(wind, "speed_x", "weather.wind_direction");
		out.wind_speed_z = number(wind, "speed_z", "weather.wind_direction");
	}
}

const char *const kOutfitNames[] = {
	"default", "elvis", "trent", "trench", "frock_ripped", "frock", "leather", "deepsea",
	"wetsuit", "snow", "lab", "stewardess", "mrblonde", "maian", "negotiator",
};
const int kOutfitCount = static_cast<int>(sizeof(kOutfitNames) / sizeof(kOutfitNames[0]));

s32 outfit(const Json &object, const char *key, const std::string &where)
{
	if (!object.contains(key) || !object[key].is_string()) refuse(where + "." + key + " must be an outfit name");
	const s32 value = scenarioStageOutfitFromName(object[key].get<crude_json::string>().c_str());
	if (value < 0) refuse(where + "." + key + " is not an outfit ('" + object[key].get<crude_json::string>() + "')");
	return value;
}

void readPlayer(const Json &player, scenario_stage_player_t &out)
{
	if (!player.is_object()) refuse("player must be an object");
	onlyKeys(player, { "perfect_agent_outfit", "disguise", "coop_spawn_distance",
		"unlimited_ammo_excludes" }, "player");
	out.present = 1;
	if (player.contains("coop_spawn_distance")) {
		out.coop_spawn_distance = number(player, "coop_spawn_distance", "player");
		if (!(out.coop_spawn_distance >= 1.0f && out.coop_spawn_distance <= 1000.0f)) {
			refuse("player.coop_spawn_distance must be 1..1000");
		}
	}
	out.perfect_agent_outfit = player.contains("perfect_agent_outfit")
		? outfit(player, "perfect_agent_outfit", "player") : -1;
	out.disguise_outfit = -1;
	if (player.contains("disguise")) {
		const Json &disguise = player["disguise"];
		if (!disguise.is_object()) refuse("player.disguise must be an object");
		onlyKeys(disguise, { "outfit", "untargetable" }, "player.disguise");
		if (disguise.contains("outfit")) out.disguise_outfit = outfit(disguise, "outfit", "player.disguise");
		out.disguise_untargetable = boolean(disguise, "untargetable", "player.disguise") ? 1 : 0;
	}
	if (player.contains("unlimited_ammo_excludes")) {
		const Json &list = player["unlimited_ammo_excludes"];
		if (!list.is_array()) refuse("player.unlimited_ammo_excludes must be a list of weapon catalog ids");
		const crude_json::array &ids = list.get<crude_json::array>();
		if (ids.empty() || ids.size() > SCENARIO_STAGE_MAX_AMMO_EXCLUDES) {
			refuse("player.unlimited_ammo_excludes names 1 to " +
				std::to_string(SCENARIO_STAGE_MAX_AMMO_EXCLUDES) + " weapons");
		}
		for (const Json &id : ids) {
			const std::string value = id.is_string() ? id.get<crude_json::string>() : std::string();
			const size_t colon = value.find(':');
			if (value.empty() || value.size() >= SCENARIO_STAGE_ID_LEN || colon == std::string::npos ||
					colon == 0 || colon + 1 == value.size()) {
				refuse("player.unlimited_ammo_excludes holds something that is not a weapon catalog id");
			}
			for (s32 i = 0; i < out.unlimited_ammo_exclude_count; i++) {
				if (value == out.unlimited_ammo_excludes[i]) {
					refuse("player.unlimited_ammo_excludes names " + value + " twice");
				}
			}
			std::memcpy(out.unlimited_ammo_excludes[out.unlimited_ammo_exclude_count], value.c_str(),
				value.size() + 1);
			out.unlimited_ammo_exclude_count++;
		}
	}
}

void readAi(const Json &ai, scenario_stage_ai_t &out)
{
	if (!ai.is_object()) refuse("ai must be an object");
	onlyKeys(ai, { "dark_room_lists" }, "ai");
	out.present = 1;
	if (ai.contains("dark_room_lists")) {
		out.no_dark_room_lists = boolean(ai, "dark_room_lists", "ai") ? 0 : 1;
	}
}

void readEnding(const Json &ending, scenario_stage_ending_t &out)
{
	if (!ending.is_object()) refuse("ending must be an object");
	onlyKeys(ending, { "outcome" }, "ending");
	out.present = 1;
	if (ending.contains("outcome")) {
		if (!ending["outcome"].is_string()) refuse("ending.outcome must be \"known\" or \"unknown\"");
		const std::string &outcome = ending["outcome"].get<crude_json::string>();
		if (outcome == "unknown") out.outcome_unknown = 1;
		else if (outcome != "known") refuse("ending.outcome must be \"known\" or \"unknown\"");
	}
}

void readStars(const Json &stars, scenario_stage_stars_t &out)
{
	if (!stars.is_object()) refuse("stars must be an object");
	onlyKeys(stars, { "count", "grid", "below_horizon" }, "stars");
	out.present = 1;
	out.count = wholeNumber(stars, "count", 1, 8192, "stars");
	out.grid = wholeNumber(stars, "grid", 1, 8, "stars");
	out.below_horizon = boolean(stars, "below_horizon", "stars") ? 1 : 0;
}

s32 roomNumber(const Json &value, const std::string &where)
{
	const double room = value.is_number() ? value.get<crude_json::number>() : -1.0;
	if (room < 1.0 || room > 32767.0 || room != std::floor(room)) {
		refuse(where + " holds something that is not a room number 1..32767");
	}
	return static_cast<s32>(room);
}

s32 doorTag(const Json &value, const std::string &where)
{
	const double tag = value.is_number() ? value.get<crude_json::number>() : -1.0;
	if (tag < 0.0 || tag > 65535.0 || tag != std::floor(tag)) {
		refuse(where + " holds something that is not a tag number 0..65535");
	}
	return static_cast<s32>(tag);
}

void readGas(const Json &gas, scenario_stage_gas_t &out)
{
	if (!gas.is_object()) refuse("gas must be an object");
	onlyKeys(gas, { "rooms", "sphere", "door_rooms", "cutscene_fade" }, "gas");
	out.present = 1;
	out.sphere_door_tag = -1;
	if (gas.contains("rooms")) {
		const Json &rooms = gas["rooms"];
		if (!rooms.is_array()) refuse("gas.rooms must be a list of room numbers");
		const crude_json::array &list = rooms.get<crude_json::array>();
		if (list.size() > SCENARIO_STAGE_MAX_GAS_ROOMS) {
			refuse("gas.rooms names more than " + std::to_string(SCENARIO_STAGE_MAX_GAS_ROOMS) + " rooms");
		}
		for (const Json &room : list) {
			out.rooms[out.room_count++] = static_cast<s16>(roomNumber(room, "gas.rooms"));
		}
	}
	if (gas.contains("sphere")) {
		const Json &sphere = gas["sphere"];
		if (!sphere.is_object()) refuse("gas.sphere must be an object");
		onlyKeys(sphere, { "center", "radius", "door_tag" }, "gas.sphere");
		if (!sphere.contains("center") || !sphere["center"].is_array() ||
				sphere["center"].get<crude_json::array>().size() != 3) {
			refuse("gas.sphere.center must be [x, y, z]");
		}
		const crude_json::array &center = sphere["center"].get<crude_json::array>();
		for (int i = 0; i < 3; i++) {
			if (!center[i].is_number()) refuse("gas.sphere.center must be [x, y, z]");
			out.sphere_center[i] = static_cast<f32>(center[i].get<crude_json::number>());
		}
		out.sphere_radius = number(sphere, "radius", "gas.sphere");
		if (!(out.sphere_radius > 0.0f)) refuse("gas.sphere.radius must be more than 0");
		if (sphere.contains("door_tag")) out.sphere_door_tag = doorTag(sphere["door_tag"], "gas.sphere.door_tag");
		out.has_sphere = 1;
	}
	if (out.room_count == 0 && !out.has_sphere) refuse("gas needs rooms or a sphere");
	if (gas.contains("door_rooms")) {
		const Json &rooms = gas["door_rooms"];
		if (!rooms.is_array()) refuse("gas.door_rooms must be a list");
		const crude_json::array &list = rooms.get<crude_json::array>();
		if (list.size() > SCENARIO_STAGE_MAX_GAS_DOOR_ROOMS) {
			refuse("gas.door_rooms has more than " + std::to_string(SCENARIO_STAGE_MAX_GAS_DOOR_ROOMS) + " rooms");
		}
		for (const Json &entry : list) {
			if (!entry.is_object()) refuse("gas.door_rooms holds something that is not an object");
			onlyKeys(entry, { "room", "door_tags", "add" }, "gas.door_rooms");
			scenario_stage_gas_door_room_t &door = out.door_rooms[out.door_room_count++];
			if (!entry.contains("room")) refuse("gas.door_rooms needs a room");
			door.room = static_cast<s16>(roomNumber(entry["room"], "gas.door_rooms.room"));
			if (!entry.contains("door_tags") || !entry["door_tags"].is_array()) {
				refuse("gas.door_rooms.door_tags must be a list of tag numbers");
			}
			const crude_json::array &tags = entry["door_tags"].get<crude_json::array>();
			if (tags.empty() || tags.size() > SCENARIO_STAGE_MAX_GAS_DOOR_TAGS) {
				refuse("gas.door_rooms.door_tags names 1 to " + std::to_string(SCENARIO_STAGE_MAX_GAS_DOOR_TAGS) + " doors");
			}
			for (const Json &tag : tags) door.tags[door.tag_count++] = doorTag(tag, "gas.door_rooms.door_tags");
			door.add = entry.contains("add") ? number(entry, "add", "gas.door_rooms") : 0.0f;
			if (!(door.add >= -1.0f && door.add <= 1.0f)) refuse("gas.door_rooms.add must be -1..1");
		}
	}
	if (gas.contains("cutscene_fade")) {
		const Json &fade = gas["cutscene_fade"];
		if (!fade.is_object()) refuse("gas.cutscene_fade must be an object");
		onlyKeys(fade, { "start_frame", "full_frame" }, "gas.cutscene_fade");
		out.fade_start = wholeNumber(fade, "start_frame", 0, 1000000, "gas.cutscene_fade");
		out.fade_full = wholeNumber(fade, "full_frame", 1, 1000000, "gas.cutscene_fade");
		if (out.fade_full <= out.fade_start) refuse("gas.cutscene_fade.full_frame must come after start_frame");
		out.has_cutscene_fade = 1;
	}
}

void readSparks(const Json &sparks, scenario_stage_sparks_t &out)
{
	if (!sparks.is_object()) refuse("sparks must be an object");
	onlyKeys(sparks, { "flash_lights" }, "sparks");
	if (!sparks.contains("flash_lights")) refuse("sparks.flash_lights must be true or false");
	out.present = 1;
	out.no_light_flash = boolean(sparks, "flash_lights", "sparks") ? 0 : 1;
}

} // namespace

extern "C" const char *scenarioStageOutfitName(s32 value)
{
	return value >= 0 && value < kOutfitCount ? kOutfitNames[value] : nullptr;
}

extern "C" s32 scenarioStageOutfitFromName(const char *name)
{
	if (!name) return -1;
	for (int i = 0; i < kOutfitCount; i++) {
		if (std::strcmp(name, kOutfitNames[i]) == 0) return i;
	}
	return -1;
}

extern "C" s32 scenarioStageRulesParse(const char *json, size_t json_size,
	scenario_stage_rules_t *out, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!out) return 0;
	std::memset(out, 0, sizeof(*out));
	try {
		Json root;
		if (!json || !json_size || !modAssetJsonReadValue(json, json_size, root) || !root.is_object()) {
			refuse("stage.json is not one JSON object");
		}
		if (!root.contains("schema") || !root["schema"].is_string() ||
				root["schema"].get<crude_json::string>() != SCENARIO_STAGE_RULES_SCHEMA) {
			refuse(std::string("stage.json must declare schema ") + SCENARIO_STAGE_RULES_SCHEMA);
		}
		onlyKeys(root, { "schema", "note", "weather", "player", "ai", "ending", "hub", "stars", "sparks",
			"gas" }, "stage.json");
		if (root.contains("note") && !root["note"].is_string()) refuse("stage.json note must be text");
		out->player.perfect_agent_outfit = -1;
		out->player.disguise_outfit = -1;
		if (root.contains("weather")) readWeather(root["weather"], out->weather);
		if (root.contains("player")) readPlayer(root["player"], out->player);
		if (root.contains("ai")) readAi(root["ai"], out->ai);
		if (root.contains("ending")) readEnding(root["ending"], out->ending);
		if (root.contains("hub")) {
			if (!root["hub"].is_boolean()) refuse("hub must be true or false");
			out->hub.present = 1;
			out->hub.hub = root["hub"].get<crude_json::boolean>() ? 1 : 0;
		}
		if (root.contains("stars")) readStars(root["stars"], out->stars);
		if (root.contains("sparks")) readSparks(root["sparks"], out->sparks);
		if (root.contains("gas")) readGas(root["gas"], out->gas);
		return 1;
	} catch (const Refusal &refusal) {
		std::memset(out, 0, sizeof(*out));
		if (err && err_cap) std::snprintf(err, err_cap, "%s", refusal.reason.c_str());
		return 0;
	} catch (...) {
		std::memset(out, 0, sizeof(*out));
		if (err && err_cap) std::snprintf(err, err_cap, "stage.json could not be read");
		return 0;
	}
}
