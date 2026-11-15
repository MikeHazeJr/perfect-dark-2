// The stage intro from intro.json (CORE, 2026-11-15). See
// port/include/scenario_intro.h for the format.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "scenario_intro.h"
#include "scenario_stage_rules.h"
#include "modasset_gltf_document.h"
#include "../external/imgui-node-editor/crude_json.h"

extern "C" {
#include "constants.h"
}

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

s32 integer(const Json &object, const char *key, double lo, double hi, const std::string &where)
{
	if (!object.contains(key) || !object[key].is_number()) refuse(where + "." + key + " must be a whole number");
	const double value = object[key].get<crude_json::number>();
	if (!std::isfinite(value) || value != std::floor(value) || value < lo || value > hi) {
		char range[64];
		std::snprintf(range, sizeof(range), "%.0f..%.0f", lo, hi);
		refuse(where + "." + key + " must be a whole number " + range);
	}
	return static_cast<s32>(value);
}

const std::string &text(const Json &object, const char *key, const std::string &where)
{
	if (!object.contains(key) || !object[key].is_string()) refuse(where + "." + key + " must be text");
	return object[key].get<crude_json::string>();
}

s32 padRef(const Json &object, const std::string &where)
{
	const std::string &ref = text(object, "pad_ref", where);
	if (ref.size() < 5 || ref.compare(0, 4, "pad_") != 0) refuse(where + ".pad_ref must be pad_NNNN");
	for (size_t i = 4; i < ref.size(); i++) {
		if (ref[i] < '0' || ref[i] > '9') refuse(where + ".pad_ref must be pad_NNNN");
	}
	const long pad = std::strtol(ref.c_str() + 4, nullptr, 10);
	if (pad < 0 || pad > 0x7fff || ref.size() > 9) refuse(where + ".pad_ref names a pad outside 0..32767");
	return static_cast<s32>(pad);
}

s32 weapon(const Json &row, const char *key, scenario_intro_weapon_resolver_t resolve,
	const std::string &where)
{
	const std::string &id = text(row, key, where);
	s32 weaponnum = -1;
	if (id.empty() || !resolve || !resolve(id.c_str(), &weaponnum) || weaponnum < 0) {
		refuse(where + "." + key + " is not a known weapon ('" + id + "')");
	}
	return weaponnum;
}

void legacy(const Json &row, s32 cmd, size_t params, std::vector<s32> &words, const std::string &where)
{
	onlyKeys(row, { "cmd", "words" }, where);
	if (!row.contains("words") || !row["words"].is_array()) refuse(where + ".words must be a list");
	const crude_json::array &list = row["words"].get<crude_json::array>();
	if (list.size() != params) {
		refuse(where + ".words must hold exactly " + std::to_string(params) + " numbers");
	}
	words.push_back(cmd);
	for (const Json &item : list) {
		const double value = item.is_number() ? item.get<crude_json::number>() : 0.5;
		if (!std::isfinite(value) || value != std::floor(value) || value < -2147483648.0 ||
				value > 2147483647.0) {
			refuse(where + ".words holds something that is not a whole 32-bit number");
		}
		words.push_back(static_cast<s32>(value));
	}
}

} // namespace

extern "C" s32 scenarioIntroCommandWords(s32 cmd)
{
	switch (cmd) {
	case INTROCMD_SPAWN: return 3;
	case INTROCMD_WEAPON: return 4;
	case INTROCMD_AMMO: return 4;
	case INTROCMD_3: return 8;
	case INTROCMD_4: return 2;
	case INTROCMD_OUTFIT: return 2;
	case INTROCMD_6: return 10;
	case INTROCMD_WATCHTIME: return 3;
	case INTROCMD_CREDITOFFSET: return 2;
	case INTROCMD_CASE: return 3;
	case INTROCMD_CASERESPAWN: return 3;
	case INTROCMD_HILL: return 2;
	case INTROCMD_END: return 1;
	default: return 0;
	}
}

extern "C" void scenarioIntroFree(scenario_intro_t *intro)
{
	if (!intro) return;
	std::free(intro->words);
	std::memset(intro, 0, sizeof(*intro));
}

extern "C" s32 scenarioIntroParse(const char *json, size_t json_size,
	scenario_intro_weapon_resolver_t resolve_weapon,
	scenario_intro_t *out, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!out) return 0;
	std::memset(out, 0, sizeof(*out));
	try {
		Json root;
		if (!json || !json_size || !modAssetJsonReadValue(json, json_size, root) || !root.is_object()) {
			refuse("intro.json is not one JSON object");
		}
		if (!root.contains("schema") || !root["schema"].is_string() ||
				root["schema"].get<crude_json::string>() != SCENARIO_INTRO_SCHEMA) {
			refuse(std::string("intro.json must declare schema ") + SCENARIO_INTRO_SCHEMA);
		}
		onlyKeys(root, { "schema", "note", "rows" }, "intro.json");
		if (root.contains("note") && !root["note"].is_string()) refuse("intro.json note must be text");
		if (!root.contains("rows") || !root["rows"].is_array()) refuse("intro.json rows must be a list");
		const crude_json::array &rows = root["rows"].get<crude_json::array>();
		if (rows.size() > SCENARIO_INTRO_MAX_ROWS) {
			refuse("intro.json has more than " + std::to_string(SCENARIO_INTRO_MAX_ROWS) + " rows");
		}

		std::vector<s32> words;
		scenario_intro_counts_t counts;
		std::memset(&counts, 0, sizeof(counts));
		for (size_t i = 0; i < rows.size(); i++) {
			const Json &row = rows[i];
			const std::string where = "intro.json rows[" + std::to_string(i) + "]";
			if (!row.is_object()) refuse(where + " must be an object");
			const std::string &cmd = text(row, "cmd", where);
			if (cmd == "weapon") {
				onlyKeys(row, { "cmd", "weapon_catalog_id", "weapon_selector", "left_weapon_catalog_id",
					"player" }, where);
				const bool has_id = row.contains("weapon_catalog_id");
				const bool has_selector = row.contains("weapon_selector");
				if (has_id == has_selector) refuse(where + " needs exactly one of weapon_catalog_id and weapon_selector");
				s32 right = WEAPON_NONE;
				if (has_id) {
					right = weapon(row, "weapon_catalog_id", resolve_weapon, where);
				} else if (text(row, "weapon_selector", where) != "none") {
					refuse(where + ".weapon_selector must be \"none\"");
				}
				const s32 left = row.contains("left_weapon_catalog_id")
					? weapon(row, "left_weapon_catalog_id", resolve_weapon, where) : -1;
				words.insert(words.end(), { INTROCMD_WEAPON, right, left,
					integer(row, "player", 0, 255, where) });
				counts.weapons++;
			} else if (cmd == "ammo") {
				onlyKeys(row, { "cmd", "ammo_type", "quantity", "player" }, where);
				words.insert(words.end(), { INTROCMD_AMMO, integer(row, "ammo_type", 0, 32, where),
					integer(row, "quantity", 0, 1000000, where), integer(row, "player", 0, 255, where) });
				counts.ammo++;
			} else if (cmd == "outfit") {
				onlyKeys(row, { "cmd", "outfit" }, where);
				const std::string &name = text(row, "outfit", where);
				const s32 value = scenarioStageOutfitFromName(name.c_str());
				if (value < 0) refuse(where + ".outfit is not an outfit ('" + name + "')");
				words.insert(words.end(), { INTROCMD_OUTFIT, value });
				counts.outfits++;
			} else if (cmd == "watch_time") {
				onlyKeys(row, { "cmd", "hours", "minutes" }, where);
				words.insert(words.end(), { INTROCMD_WATCHTIME, integer(row, "hours", 0, 23, where),
					integer(row, "minutes", 0, 59, where) });
				counts.watch_times++;
			} else if (cmd == "case" || cmd == "case_respawn") {
				onlyKeys(row, { "cmd", "team", "pad_ref" }, where);
				words.insert(words.end(), { cmd == "case" ? INTROCMD_CASE : INTROCMD_CASERESPAWN,
					integer(row, "team", 0, SCENARIO_INTRO_MAX_CASE_TEAMS - 1, where), padRef(row, where) });
				if (cmd == "case") counts.cases++; else counts.case_respawns++;
			} else if (cmd == "hill") {
				onlyKeys(row, { "cmd", "pad_ref" }, where);
				words.insert(words.end(), { INTROCMD_HILL, padRef(row, where) });
				counts.hills++;
			} else if (cmd == "legacy_3") {
				legacy(row, INTROCMD_3, 7, words, where);
				counts.legacy++;
			} else if (cmd == "legacy_4") {
				legacy(row, INTROCMD_4, 1, words, where);
				counts.legacy++;
			} else if (cmd == "legacy_6") {
				legacy(row, INTROCMD_6, 9, words, where);
				counts.legacy++;
			} else {
				refuse(where + ".cmd '" + cmd + "' is not an intro command");
			}
		}

		if (!words.empty()) {
			out->words = static_cast<s32 *>(std::malloc(words.size() * sizeof(s32)));
			if (!out->words) refuse("intro.json: out of memory");
			std::memcpy(out->words, words.data(), words.size() * sizeof(s32));
		}
		out->word_count = static_cast<s32>(words.size());
		out->row_count = static_cast<s32>(rows.size());
		out->counts = counts;
		return 1;
	} catch (const Refusal &refusal) {
		scenarioIntroFree(out);
		if (err && err_cap) std::snprintf(err, err_cap, "%s", refusal.reason.c_str());
		return 0;
	} catch (...) {
		scenarioIntroFree(out);
		if (err && err_cap) std::snprintf(err, err_cap, "intro.json could not be read");
		return 0;
	}
}
