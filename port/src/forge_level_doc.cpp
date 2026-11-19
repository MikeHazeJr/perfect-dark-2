// The placements of a level, as Forge edits them: see port/include/forge_level_doc.h.
#include "forge_level_doc.h"

#include "mission_graph_runner.h"
#include "mission_graph_modules.h"
#include "modasset_gltf_document.h"
#include "modasset_json.h"
#include "net/net_scenario_state_wire.h"
#include "scenario_intro.h"
#include "scenario_room_domain.h"
#include "scenario_stage_rules.h"
#include "../external/imgui-node-editor/crude_json.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <locale>
#include <map>
#include <memory>
#include <new>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "constants.h"
#undef false
#undef true

namespace {

using Json = crude_json::value;

constexpr const char *kMemberNames[FORGE_LEVEL_MEMBER_COUNT] = {
	"pads.json", "spawns.json", "objects.json", "volumes.json", "setup.fields.json", "level.graph.json",
	"intro.json", "stage.json", "navigation/covers.json", "navigation/paths.json", "rooms.json"
};
constexpr const char *kSchemas[FORGE_LEVEL_MEMBER_COUNT] = {
	"pd2.scenario.pads.v1", "pd2.scenario.spawns.v1", "pd2.scenario.objects.v1",
	"pd2.scenario.volumes.v1", "pd2.scenario.setup.fields.v1", "pd2.level.graph.v1", SCENARIO_INTRO_SCHEMA,
	SCENARIO_STAGE_RULES_SCHEMA, "pd2.scenario.covers.v1", "pd2.scenario.paths.v1", "pd2.scenario.rooms.v1"
};
constexpr char kEmptyIntro[] = "{\"schema\":\"" SCENARIO_INTRO_SCHEMA "\",\"rows\":[]}";
constexpr char kEmptyStage[] = "{\"schema\":\"" SCENARIO_STAGE_RULES_SCHEMA "\"}";
/* Both alternate scenario banks have this many padnums (scenariodata_htm/htb). */
constexpr size_t kCaseBankPadLimit = 60;
static_assert(SCENARIO_INTRO_MAX_CASE_TEAMS == NET_SCENARIO_CTC_BASES);
/* What the game counts in a level's graph when it loads the level, as text
 * (scenario_source_runtime.c, s_countTextOccurrences): one of these for each
 * row of volumes.json, or the level is refused. */
constexpr const char *kVolumeNodeKind = "\"kind\": \"scenario.trigger.volume.source\"";
/* A link to such a node, as the extractor writes one. */
constexpr const char *kVolumeLinkTo = "\"to\": \"trigger.volume.";
constexpr char kTriggerEvent[] = "event.volume.entered";
constexpr char kDoorTriggerAction[] = "og.door.open";
constexpr char kPickupTriggerAction[] = "og.pickup.give";
/* The graph's `counts` this module keeps true, and the table each one counts. */
constexpr const char *kGraphCounts[3] = { "pads", "volumes", "objects" };
/* A pad's bounds reach this far from it on every axis when it has no box of
 * its own; the extractor writes the same box for such a pad. */
constexpr f32 kPadReach = 100.0f;
constexpr unsigned long kPadWalkDirect = 0x1000ul; /* PADFLAG_AIWALKDIRECT in constants.h */

/* The four bits of an object's second flags word that leave it out of the
 * level on one difficulty each (OBJFLAG2_EXCLUDE_A, _SA, _PA and _PD in
 * constants.h). The setup loop tests the word against the bit of the
 * difficulty being played, so an object with all four is never created. */
constexpr unsigned long kExcludeEverywhere = 0x000000f0ul;
/* The field of setup.fields.json that repeats objects.json's `flags2`. */
constexpr const char *kFlagsField = "base.flags2";
/* The fields that say what an object is drawn with (repeated by objects.json's
 * `model_catalog_id`), at what size (256 is life size), and what a weapon
 * pickup gives (a catalog weapon repeated by objects.json's
 * `weapon_catalog_id`, or one of the match's weapon places). */
constexpr const char *kModelField = "base.model";
constexpr const char *kScaleField = "base.extra_scale";
constexpr const char *kWeaponField = "pickup.weapon";
constexpr const char *kPickupKind = "weapon_pickup";
constexpr const char *kPropKind = "prop";
constexpr unsigned long kDecorationFlags = OBJFLAG_INVINCIBLE | OBJFLAG_UNCOLLECTABLE
	| OBJFLAG_CANNOT_ACTIVATE | OBJFLAG_AISEETHROUGH;
constexpr unsigned long kDecorationFlags2 = OBJFLAG2_NOFALL | OBJFLAG2_IMMUNETOGUNFIRE
	| OBJFLAG2_SHOOTTHROUGH | OBJFLAG2_IMMUNETOEXPLOSIONS | OBJFLAG2_THROWTHROUGH;
constexpr unsigned long kDecorationFlags3 = OBJFLAG3_WALKTHROUGH;
constexpr const char *kCrateKind = "multi_ammo_crate";
/* What a new weapon place and a new ammo crate are drawn with: the models
 * every weapon place (260 of 260) and every such crate (524 of 524) of the
 * base levels names. A place shows its stand-in only until a match's weapon
 * set gives it a weapon. */
constexpr const char *kPlaceModel = "base:model_roofgun";
constexpr const char *kCrateModel = "base:model_multi_ammo_crate";
/* A new hoverbike: the kind, the model and the three flag words that all five
 * hoverbikes of the base levels have (it falls to rest without turning over
 * and takes no damage from gunfire or explosions). */
constexpr const char *kBikeKind = "hoverbike";
constexpr const char *kBikeModel = "base:model_hovbike";
constexpr const char *kBikeFlags = "0x05120101";
constexpr const char *kBikeFlags2 = "0x00304300";
constexpr const char *kBikeFlags3 = "0x02000003";
constexpr int kCrateSlots = 19;          /* multi_ammo_crate.slots[0] to [18] */
constexpr long kWeaponSlots = 16;        /* mp_location_00 to mp_location_15 */
/* An object's size is an unsigned 16-bit number in the game and a weapon
 * set's a signed one (types.h: defaultobj and mpweapon); this fits both. */
constexpr long kMaxExtraScale = 32767;

/* ------------------------------------------------------------------------ */
/* Where values start and end                                               */
/* ------------------------------------------------------------------------ */
/*
 * Every text is first read whole by the game's strict JSON reader, which is
 * what decides whether it is valid. The functions here only find where the
 * values of an already valid text start and end, so that a row can be carried
 * from input to output as the bytes it was, and one value inside a row can be
 * replaced without touching the others.
 */

struct Span {
	size_t begin = 0;
	size_t end = 0;       /* one past the last character */
};

struct Member {
	std::string key;      /* the key's text between its quotes, as written */
	size_t begin = 0;     /* the opening quote of the key */
	Span value;
};

bool isSpace(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

void skipSpace(const std::string &text, size_t &pos)
{
	while (pos < text.size() && isSpace(text[pos])) pos++;
}

bool skipString(const std::string &text, size_t &pos)
{
	if (pos >= text.size() || text[pos] != '"') return false;
	pos++;
	while (pos < text.size()) {
		const char c = text[pos];
		if (c == '\\') {
			if (pos + 1 >= text.size()) return false;
			pos += 2;
			continue;
		}
		pos++;
		if (c == '"') return true;
	}
	return false;
}

bool skipValue(const std::string &text, size_t &pos, int depth);

/* The members of the object or the elements of the array that starts at pos. */
bool skipContainer(const std::string &text, size_t &pos, int depth,
	std::vector<Member> *members, std::vector<Span> *elements)
{
	if (depth > 64 || pos >= text.size()) return false;
	const bool object = text[pos] == '{';
	const char close = object ? '}' : ']';
	pos++;
	skipSpace(text, pos);
	if (pos < text.size() && text[pos] == close) {
		pos++;
		return true;
	}
	for (;;) {
		Member member;
		skipSpace(text, pos);
		if (object) {
			member.begin = pos;
			if (!skipString(text, pos)) return false;
			member.key = text.substr(member.begin + 1, pos - member.begin - 2);
			skipSpace(text, pos);
			if (pos >= text.size() || text[pos] != ':') return false;
			pos++;
			skipSpace(text, pos);
		}
		member.value.begin = pos;
		if (!skipValue(text, pos, depth + 1)) return false;
		member.value.end = pos;
		if (object && members) members->push_back(member);
		if (!object && elements) elements->push_back(member.value);
		skipSpace(text, pos);
		if (pos >= text.size()) return false;
		if (text[pos] == ',') {
			pos++;
			continue;
		}
		if (text[pos] == close) {
			pos++;
			return true;
		}
		return false;
	}
}

bool skipValue(const std::string &text, size_t &pos, int depth)
{
	skipSpace(text, pos);
	if (pos >= text.size()) return false;
	const char c = text[pos];
	if (c == '"') return skipString(text, pos);
	if (c == '{' || c == '[') return skipContainer(text, pos, depth, nullptr, nullptr);
	const size_t start = pos;
	while (pos < text.size() && !isSpace(text[pos]) && text[pos] != ',' && text[pos] != '}' &&
			text[pos] != ']' && text[pos] != ':') {
		pos++;
	}
	return pos > start;
}

bool objectMembers(const std::string &text, size_t objectBegin, std::vector<Member> &out)
{
	size_t pos = objectBegin;
	out.clear();
	if (pos >= text.size() || text[pos] != '{') return false;
	return skipContainer(text, pos, 0, &out, nullptr);
}

/* ------------------------------------------------------------------------ */
/* Small helpers                                                            */
/* ------------------------------------------------------------------------ */

void setError(char *err, size_t err_cap, const char *format, ...)
{
	if (!err || !err_cap) return;
	va_list args;
	va_start(args, format);
	vsnprintf(err, err_cap, format, args);
	va_end(args);
	err[err_cap - 1] = '\0';
}

template <size_t N> void copyText(char (&out)[N], const std::string &text)
{
	snprintf(out, N, "%s", text.c_str());
}

std::string stringOf(const Json &row, const char *key)
{
	if (!row.is_object() || !row.contains(key) || !row[key].is_string()) return std::string();
	return row[key].get<crude_json::string>();
}

bool hasString(const Json &row, const char *key)
{
	return row.is_object() && row.contains(key) && row[key].is_string();
}

bool vectorOf(const Json &value, f32 out[3])
{
	if (!value.is_array()) return false;
	const crude_json::array &items = value.get<crude_json::array>();
	if (items.size() != 3) return false;
	for (size_t i = 0; i < 3; i++) {
		if (!items[i].is_number()) return false;
		const double number = items[i].get<crude_json::number>();
		if (!std::isfinite(number)) return false;
		out[i] = static_cast<f32>(number);
	}
	return true;
}

/* The number at the end of "pad_0012", or -1 when the name is not that form. */
long trailingNumber(const std::string &name, const char *prefix)
{
	const size_t plen = strlen(prefix);
	if (name.size() <= plen || name.compare(0, plen, prefix) != 0) return -1;
	if (name.size() - plen > 9) return -1;
	long value = 0;
	for (size_t i = plen; i < name.size(); i++) {
		if (name[i] < '0' || name[i] > '9') return -1;
		value = value * 10 + (name[i] - '0');
	}
	return value;
}

/* "12" as 12; -1 for anything that is not a plain whole number. */
long wholeNumber(const std::string &text)
{
	if (text.empty() || text.size() > 9) return -1;
	long value = 0;
	for (const char c : text) {
		if (c < '0' || c > '9') return -1;
		value = value * 10 + (c - '0');
	}
	return value;
}

std::string quoted(const std::string &text)
{
	return "\"" + text + "\"";
}

/* "0x000000f0" as 0xf0. False for anything that is not "0x" and one to eight
 * hex digits, which is how the extractor writes a flags word. */
bool hexWord(const std::string &text, unsigned long &out)
{
	if (text.size() < 3 || text.size() > 10 || text[0] != '0' || (text[1] != 'x' && text[1] != 'X')) return false;
	unsigned long value = 0;
	for (size_t i = 2; i < text.size(); i++) {
		const char c = text[i];
		unsigned long digit;
		if (c >= '0' && c <= '9') digit = static_cast<unsigned long>(c - '0');
		else if (c >= 'a' && c <= 'f') digit = static_cast<unsigned long>(c - 'a') + 10;
		else if (c >= 'A' && c <= 'F') digit = static_cast<unsigned long>(c - 'A') + 10;
		else return false;
		value = (value << 4) | digit;
	}
	out = value;
	return true;
}

/* A flags word the way the extractor writes one. */
std::string hexWordText(unsigned long value)
{
	char text[16];
	snprintf(text, sizeof(text), "0x%08lx", value & 0xfffffffful);
	return text;
}

bool readPadFlags(const Json &row, unsigned long &flags)
{
	flags = 0;
	if (!row.is_object()) return false;
	if (!row.contains("flags")) return true;
	const Json &value = row["flags"];
	if (value.is_string()) return hexWord(value.get<crude_json::string>(), flags);
	if (!value.is_number()) return false;
	const double number = value.get<crude_json::number>();
	if (!(number >= 0.0 && number <= 0xfffffffful) || number != std::floor(number)) return false;
	flags = static_cast<unsigned long>(number);
	return true;
}

/* A kind the game's setup loop always filters by the flags word. A lift and
 * an escalator step are kept on some levels in multiplayer whatever theirs
 * says (setup.c, "relaxing LIFT/ESCASTEP exclude filter"). */
bool kindCanBeLeftOut(const char *kind)
{
	return strcmp(kind, "lift") != 0 && strcmp(kind, "escalator_step") != 0;
}

/* The field of a record that says where it stands. */
bool isPlaceField(const std::string &field)
{
	return field == "base.pad" || field == "character.pad";
}

/* "mp_location_03" as 3: one of the match's weapon places, the way the
 * extractor writes it (two digits). -1 for anything else. */
long weaponSlot(const std::string &value)
{
	static const char prefix[] = "mp_location_";
	if (value.size() != sizeof(prefix) - 1 + 2) return -1;
	const long slot = trailingNumber(value, prefix);
	return slot >= 0 && slot < kWeaponSlots ? slot : -1;
}

std::string weaponSlotText(long slot)
{
	char text[32];
	snprintf(text, sizeof(text), "mp_location_%02ld", slot);
	return text;
}

/* A catalog id, safe to write between quotes: "base:dragon",
 * "mod:weapon_crystal_pistol". Lower-case letters, digits, '_', '-', '.' and
 * one ':' with something on both sides; short enough for the 64 bytes an
 * object's ids are kept in. */
bool catalogIdOk(const char *id)
{
	if (!id) return false;
	const size_t length = strlen(id);
	if (length < 3 || length > 63) return false;
	size_t colons = 0;
	for (size_t i = 0; i < length; i++) {
		const char c = id[i];
		if (c == ':') {
			colons++;
			if (i == 0 || i + 1 == length) return false;
			continue;
		}
		const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
		if (!ok) return false;
	}
	return colons == 1;
}

std::string numbered(const char *prefix, long number)
{
	char text[48];
	snprintf(text, sizeof(text), "%s%04ld", prefix, number);
	return text;
}

/* Keep the extractor's six-decimal format across the finite float range. */
std::string vectorText(const f32 v[3])
{
	std::ostringstream text;
	text.imbue(std::locale::classic());
	text.setf(std::ios::fixed, std::ios::floatfield);
	text.precision(6);
	text << '[';
	for (int i = 0; i < 3; ++i) {
		if (i) text << ", ";
		text << (v[i] == 0.0f ? 0.0 : static_cast<double>(v[i]));
	}
	text << ']';
	return text.str();
}

bool finite3(const f32 v[3])
{
	return v && std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

/* A team name the game's spawn reader takes: any, default, or team_N. */
bool teamOk(const std::string &team)
{
	if (team == "any" || team == "default") return true;
	const long number = trailingNumber(team, "team_");
	return number >= 0 && number <= 99;
}

/* A plain word, safe to write between quotes without escaping. */
bool plainWord(const std::string &text)
{
	if (text.empty() || text.size() > 31) return false;
	for (const char c : text) {
		const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
			c == '_' || c == '-';
		if (!ok) return false;
	}
	return true;
}

/* Replaces the value of a top-level key of a row. False when the row has no such key. */
bool replaceValue(std::string &row, const char *key, const std::string &value)
{
	std::vector<Member> members;
	if (!objectMembers(row, 0, members)) return false;
	for (const Member &member : members) {
		if (member.key == key) {
			row.replace(member.value.begin, member.value.end - member.value.begin, value);
			return true;
		}
	}
	return false;
}

/* ------------------------------------------------------------------------ */
/* One member: its rows as text, and what sits around them                  */
/* ------------------------------------------------------------------------ */

struct Table {
	std::string original;            /* the text as read */
	std::string head;                /* everything before the first row */
	std::string tail;                /* everything after the last row */
	std::string separator;           /* between two rows this document puts side by side */
	std::vector<std::string> rows;   /* each row's text */
	std::vector<std::string> before; /* what stood before each row (unused for the first) */
	std::string built;               /* the text to save, when changed */
	bool changed = false;            /* a row was added, removed or rewritten */
	bool stale = false;              /* `built` is older than the rows */
	bool wasEmpty = false;           /* no row yet, and the list is still laid out as it was read */
	bool startedEmpty = false;       /* the text that was read had no rows */

	/* Every edit goes through here, so `built` is rebuilt once per edit and
	 * the text a caller holds stays valid until the next one. */
	void touch()
	{
		changed = true;
		stale = true;
	}

	void append(const std::string &row)
	{
		if (rows.empty() && wasEmpty) {
			/* "rows": [] becomes a list laid out like the extractor's. */
			head += "\n    ";
			tail = "\n  " + tail;
			wasEmpty = false;
		}
		before.push_back(separator);
		rows.push_back(row);
		touch();
	}

	void remove(size_t index)
	{
		rows.erase(rows.begin() + static_cast<long>(index));
		before.erase(before.begin() + static_cast<long>(index));
		touch();
	}

	/* A member that is not a list of rows (the level's graph) is kept and
	 * replaced whole. */
	void setWhole(const std::string &text)
	{
		built = text;
		changed = true;
		stale = false;
	}

	const std::string &text()
	{
		if (!changed) return original;
		if (stale) {
			if (rows.empty() && startedEmpty) {
				/* Back to no rows in a list that was read with none (a start
				 * added and taken out again): the bytes that were read. The
				 * first row re-laid the empty list out, so head and tail no
				 * longer add up to them. */
				built = original;
			} else {
				built = head;
				for (size_t i = 0; i < rows.size(); i++) {
					if (i > 0) built += before[i];
					built += rows[i];
				}
				built += tail;
			}
			stale = false;
		}
		return built;
	}
};

/* Reads one member: checks it whole, then finds its rows. */
bool readTable(int member, const char *json, size_t size, Table &table, Json &root, std::string &error)
{
	const char *file = kMemberNames[member];
	if (!json || !size || !modAssetJsonReadValue(json, size, root) || !root.is_object()) {
		error = std::string(file) + " is not one JSON object";
		return false;
	}
	if (!hasString(root, "schema") || stringOf(root, "schema") != kSchemas[member]) {
		error = std::string(file) + " needs \"schema\": \"" + kSchemas[member] + "\"";
		return false;
	}
	if (!root.contains("rows") || !root["rows"].is_array()) {
		error = std::string(file) + " has no rows array";
		return false;
	}

	table.original.assign(json, size);
	const std::string &text = table.original;
	size_t start = 0;
	skipSpace(text, start);
	std::vector<Member> members;
	if (!objectMembers(text, start, members)) {
		error = std::string(file) + " could not be laid out";
		return false;
	}
	const Member *rowsMember = nullptr;
	for (const Member &m : members) {
		if (m.key == "rows") {
			if (rowsMember) {
				error = std::string(file) + " has two rows arrays";
				return false;
			}
			rowsMember = &m;
		}
	}
	std::vector<Span> spans;
	size_t pos = rowsMember ? rowsMember->value.begin : 0;
	if (!rowsMember || text[pos] != '[' || !skipContainer(text, pos, 0, nullptr, &spans) ||
			spans.size() != root["rows"].get<crude_json::array>().size()) {
		error = std::string(file) + " could not be laid out";
		return false;
	}

	if (spans.empty()) {
		table.wasEmpty = true;
		table.startedEmpty = true;
		table.head = text.substr(0, rowsMember->value.begin + 1);
		table.tail = text.substr(rowsMember->value.end - 1);
		table.separator = ",\n    ";
		return true;
	}
	table.head = text.substr(0, spans.front().begin);
	table.tail = text.substr(spans.back().end);
	for (size_t i = 0; i < spans.size(); i++) {
		if (text[spans[i].begin] != '{') {
			error = std::string(file) + " has a row that is not an object";
			return false;
		}
		table.rows.push_back(text.substr(spans[i].begin, spans[i].end - spans[i].begin));
		table.before.push_back(i == 0 ? std::string()
			: text.substr(spans[i - 1].end, spans[i].begin - spans[i - 1].end));
	}
	if (spans.size() > 1) {
		table.separator = table.before[1];
	} else {
		/* One row: a second one goes on its own line at the first one's indent. */
		const size_t line = table.head.find_last_of('\n');
		table.separator = line == std::string::npos ? std::string(", ")
			: ",\n" + table.head.substr(line + 1);
	}
	return true;
}

} // namespace

/* ------------------------------------------------------------------------ */
/* The document                                                             */
/* ------------------------------------------------------------------------ */

struct forge_level_doc {
	Table tables[FORGE_LEVEL_MEMBER_COUNT];
	bool recoveredMembers[FORGE_LEVEL_MEMBER_COUNT] = {};
	bool sourcePresent[FORGE_LEVEL_MEMBER_COUNT] = {};
	scenario_stage_atmosphere_t atmosphere = {};
	struct RoomBrightnessRow {
		forge_level_room_brightness_t value;
		size_t roomRow;
		bool hasRange;
	};
	std::vector<RoomBrightnessRow> roomBrightness;
	s32 weatherStartKind = SCENARIO_STAGE_WEATHER_START_NONE;
	s32 weatherStartIntensity = 0;
	std::multiset<std::string> hills;
	struct CaseMarkerRow {
		forge_level_case_marker_t marker;
		size_t introRow;
	};
	std::vector<CaseMarkerRow> caseMarkers;
	struct TriggerRow {
		forge_level_trigger_t trigger;
		size_t volumeRow;
	};
	std::vector<TriggerRow> triggers;
	std::vector<forge_level_logic_zone_t> logicZones;
	std::set<std::string> triggerVolumeIds;
	std::set<std::string> logicVolumePads;
	struct CoverRow {
		forge_level_cover_t cover;
		size_t sourceRow;
	};
	std::vector<CoverRow> covers;
	std::set<std::string> routePads;
	bool routePadsUnknown = false;
	std::vector<forge_level_pad_t> pads;
	std::vector<forge_level_spawn_t> spawns;
	std::vector<forge_level_object_t> objects;
	std::vector<int> padVolume;         /* each pad's row in volumes.json, or -1 */
	std::set<std::string> volumeIds;    /* every volume_id the file has held */
	long nextTriggerVolume = FORGE_LEVEL_MAX_PADS - 1;
	/* setup.fields.json: what each row says, and each record's rows in file order. */
	struct FieldRow {
		std::string record;
		std::string field;
		std::string value;
		std::string type;
		std::string catalog;   /* its catalog_id */
		std::string reference; /* its ref_record_id */
	};
	std::vector<FieldRow> fields;
	std::map<std::string, std::vector<size_t>> recordRows;
	/* Each object's rows that say what it is drawn with and, for a weapon
	 * pickup, what it gives: -1 unless setup.fields.json has the row and it
	 * agrees with objects.json. One entry per object. */
	struct ObjectCatalog {
		long modelRow = -1;    /* base.model */
		long scaleRow = -1;    /* base.extra_scale */
		long weaponRow = -1;   /* pickup.weapon */
	};
	std::vector<ObjectCatalog> objectCatalog;
	/* Each object's second flags word, one entry per object. */
	struct ObjectFlags {
		bool inObjects = false;     /* objects.json gives the word */
		bool known = false;         /* setup.fields.json gives it too, and the same */
		unsigned long value = 0;
		unsigned long restore = 0;  /* the exclusion bits to put back when it is no longer left out */
		size_t fieldRow = 0;        /* its row in setup.fields.json, when known */
		/* The word as each file wrote it when read, so that going back to that
		 * word gives back those exact characters. */
		unsigned long original = 0;
		std::string objectsText;
		std::string fieldText;
	};
	std::vector<ObjectFlags> objectFlags;
	/* level.graph.json: every node id it holds, and which of its counts of
	 * pads, volumes and objects were true when it was read. */
	std::set<std::string> graphNodeIds;
	bool graphCountTrue[3] = { false, false, false };
	long nextOrder = 0;      /* above every place in the command list, and every setup number */
	long nextSpawn = 0;      /* above every spawn number this document has held */
	std::string spawnProfile = "intro";
};

namespace {

/* The native room loader uses the first literal key in a source row. An
 * escaped or shadowed key cannot safely identify a field to edit. */
bool roomField(const std::string &raw, const Json &parsed, const std::vector<Member> &fields,
	const char *key, const Member *&out)
{
	const auto field = std::find_if(fields.begin(), fields.end(), [key](const Member &item) {
		return item.key == key;
	});
	out = field == fields.end() ? nullptr : &*field;
	return raw.find(quoted(key)) == (out ? out->begin : std::string::npos)
		&& (out || !parsed.contains(key));
}

bool roomInteger(const std::string &raw, const Span &token, s32 &out)
{
	const char *end = raw.data() + token.end;
	const char *parsed = nullptr;
	return modAssetJsonParseS32Token(raw.data() + token.begin, end, &out, &parsed) && parsed == end;
}

bool validRoomBrightness(s32 minimum, s32 maximum)
{
	return minimum >= 0 && minimum <= maximum && maximum <= 255;
}

bool readRoomBrightnessRange(const std::string &raw, const Span &range, forge_level_room_brightness_t &out)
{
	size_t position = range.begin;
	std::vector<Span> values;
	return raw[position] == '[' && skipContainer(raw, position, 0, nullptr, &values)
		&& values.size() == 2 && roomInteger(raw, values[0], out.minimum)
		&& roomInteger(raw, values[1], out.maximum) && validRoomBrightness(out.minimum, out.maximum);
}

bool collectRoomBrightness(const char *text, size_t size, Table &table,
	std::vector<forge_level_doc::RoomBrightnessRow> &editable)
{
	constexpr int member = FORGE_LEVEL_MEMBER_ROOMS;
	Json root;
	std::string error;
	if (!readTable(member, text, size, table, root, error)
			|| table.original.find(kSchemas[member]) == std::string::npos
			|| table.rows.size() >= SCENARIO_ROOM_DOMAIN_MAX_COUNT) return false;
	std::vector<Member> fields;
	size_t start = 0;
	skipSpace(table.original, start);
	const Member *rowsField = nullptr;
	if (!objectMembers(table.original, start, fields)
			|| !roomField(table.original, root, fields, "rows", rowsField) || !rowsField) return false;

	std::set<s32> roomNumbers;
	const auto &rows = root["rows"].get<crude_json::array>();
	for (size_t i = 0; i < rows.size(); ++i) {
		const Json &row = rows[i];
		const std::string &raw = table.rows[i];
		forge_level_room_brightness_t value = {0, 255, 255, 0};
		const Member *id = nullptr;
		if (!objectMembers(raw, 0, fields) || !roomField(raw, row, fields, "roomnum", id) || !id
				|| !roomInteger(raw, id->value, value.roomnum) || value.roomnum == 0
				|| !scenarioRoomDomainContains(SCENARIO_ROOM_DOMAIN_MAX_COUNT, value.roomnum)
				|| !roomNumbers.insert(value.roomnum).second) return false;

		const Member *low = nullptr, *high = nullptr, *origin = nullptr, *range = nullptr, *base = nullptr;
		scenario_room_bounds_t bounds;
		f32 roomOrigin[3];
		if (!roomField(raw, row, fields, "min", low) || !low
				|| !roomField(raw, row, fields, "max", high) || !high
				|| !vectorOf(row["min"], bounds.minimum) || !vectorOf(row["max"], bounds.maximum)
				|| !scenarioRoomBoundsValid(&bounds, 1)
				|| !roomField(raw, row, fields, "origin", origin)
				|| (origin && (!vectorOf(row["origin"], roomOrigin) || !scenarioRoomOriginValid(roomOrigin)))
				|| !roomField(raw, row, fields, "brightness", range)
				|| (range && !readRoomBrightnessRange(raw, range->value, value))
				|| !roomField(raw, row, fields, "base_brightness", base)) continue;
		s32 baseBrightness = 0;
		value.has_base_override = base && roomInteger(raw, base->value, baseBrightness)
			&& validRoomBrightness(baseBrightness, baseBrightness);
		editable.push_back({value, i, range != nullptr});
	}
	return true;
}

int findPad(const forge_level_doc &doc, const std::string &ref)
{
	const long number = trailingNumber(ref, "pad_");
	if (number < 0 || static_cast<size_t>(number) >= doc.pads.size()) return -1;
	return ref == doc.pads[static_cast<size_t>(number)].pad_ref ? static_cast<int>(number) : -1;
}

int findSpawn(const forge_level_doc &doc, const char *id)
{
	for (size_t i = 0; id && i < doc.spawns.size(); i++) {
		if (strcmp(doc.spawns[i].spawn_id, id) == 0) return static_cast<int>(i);
	}
	return -1;
}

struct RoutePath {
	long id;
	unsigned long flags;
	std::vector<std::string> pads;
	bool gridLink;
};

constexpr char kGridRouteLinkField[] = "grid_link";
constexpr char kGridRouteLinkKind[] = "connect_pads";

bool readRoutePath(const forge_level_doc &doc, const Json &row, RoutePath &path)
{
	const std::string ref = stringOf(row, "path_ref");
	path.id = trailingNumber(ref, "path_");
	if (path.id < 0 || path.id > UINT8_MAX || ref != numbered("path_", path.id)
			|| !row.contains("flags") || !row.contains("pads") || !row["pads"].is_array()) return false;
	const auto &flags = row["flags"];
	if (flags.is_number()) {
		const double value = flags.get<crude_json::number>();
		if (value < 0 || value > UINT8_MAX || std::floor(value) != value) return false;
		path.flags = static_cast<unsigned long>(value);
	} else if (flags.is_string()) {
		const auto &word = flags.get<crude_json::string>();
		if (!hexWord(word, path.flags)) {
			const long value = wholeNumber(word);
			if (value < 0) return false;
			path.flags = static_cast<unsigned long>(value);
		}
		if (path.flags > UINT8_MAX) return false;
	} else {
		return false;
	}
	const auto &pads = row["pads"].get<crude_json::array>();
	if (pads.empty() || pads.size() > UINT16_MAX) return false;
	for (const auto &value : pads) {
		if (!value.is_string()) return false;
		const auto &segment = value.get<crude_json::string>();
		const size_t delimiter = segment.find('|');
		if (delimiter != std::string::npos) {
			const auto direction = segment.substr(delimiter + 1);
			if (direction != "inward" && direction != "outward") return false;
		}
		const auto pad = segment.substr(0, delimiter);
		if (findPad(doc, pad.c_str()) < 0) return false;
		path.pads.push_back(pad);
	}
	path.gridLink = path.flags == 0 && pads.size() == 2 && path.pads[0] != path.pads[1]
		&& row.get<crude_json::object>().size() == 4
		&& stringOf(row, kGridRouteLinkField) == kGridRouteLinkKind
		&& pads[0].get<crude_json::string>() == path.pads[0]
		&& pads[1].get<crude_json::string>() == path.pads[1];
	return true;
}

bool routeJoinsPads(const RoutePath &path, const char *first, const char *second)
{
	const auto joins = [first, second](const std::string &a, const std::string &b) {
		return (a == first && b == second) || (a == second && b == first);
	};
	for (size_t i = 1; i < path.pads.size(); ++i) {
		if (joins(path.pads[i - 1], path.pads[i])) return true;
	}
	return (path.flags & PATHFLAG_CIRCULAR) && path.pads.size() > 2
		&& joins(path.pads.back(), path.pads.front());
}

bool readEditableRouteTable(const forge_level_doc *doc, const char *first, const char *second,
		Table &table, Json &root, char *err, size_t err_cap)
{
	constexpr int member = FORGE_LEVEL_MEMBER_PATHS;
	if (!doc || !doc->sourcePresent[member]) {
		setError(err, err_cap, "open a level with its native path table");
		return false;
	}
	if (!first || !second || strcmp(first, second) == 0 || findPad(*doc, first) < 0 || findPad(*doc, second) < 0) {
		setError(err, err_cap, "select two different pads");
		return false;
	}
	const auto &source = doc->tables[member];
	/* Paths are replaced as a whole so the current text is already materialized. */
	const std::string &text = source.changed ? source.built : source.original;
	std::string error;
	if (!readTable(member, text.data(), text.size(), table, root, error)) {
		setError(err, err_cap, "%s", error.c_str());
		return false;
	}
	return true;
}

struct GridRouteLinkLocation {
	size_t row = 0;
	long id = -1;
};

bool findGridRouteLink(const forge_level_doc &doc, const Json &root, const char *first, const char *second,
		GridRouteLinkLocation &link, std::set<std::string> *survivingPads, char *err, size_t err_cap)
{
	const auto &rows = root["rows"].get<crude_json::array>();
	bool used[UINT8_MAX + 1] = {};
	bool otherLink = false;
	for (size_t i = 0; i < rows.size(); ++i) {
		RoutePath path = {};
		if (!readRoutePath(doc, rows[i], path) || used[path.id]) {
			setError(err, err_cap, "the existing native path table has an invalid row or repeated id");
			return false;
		}
		used[path.id] = true;
		if (routeJoinsPads(path, first, second)) {
			if (path.gridLink && link.id < 0) {
				link = { i, path.id };
				continue;
			}
			otherLink = true;
		}
		if (survivingPads) survivingPads->insert(path.pads.begin(), path.pads.end());
	}
	if (link.id < 0) {
		setError(err, err_cap, "%s and %s have no editable Grid link", first, second);
		return false;
	}
	if (otherLink) {
		setError(err, err_cap, "another native path also joins these pads");
		return false;
	}
	if (rows.size() == 1) {
		setError(err, err_cap, "at least one native path must remain");
		return false;
	}
	return true;
}

/* Preserve unusual inherited sources. If they cannot be interpreted here,
 * treat their pad references as shared when deciding whether a pad can move. */
void readRoutePadRefs(forge_level_doc &doc, const char *text, size_t size)
{
	Json root;
	if (!modAssetJsonReadValue(text, size, root) || !root.is_object()
			|| !root.contains("rows") || !root["rows"].is_array()) {
		doc.routePadsUnknown = true;
		return;
	}
	for (const auto &row : root["rows"].get<crude_json::array>()) {
		RoutePath path;
		if (!readRoutePath(doc, row, path)) {
			doc.routePadsUnknown = true;
			return;
		}
		doc.routePads.insert(path.pads.begin(), path.pads.end());
	}
}

bool readPads(forge_level_doc &doc, const Json &root, std::string &error)
{
	const crude_json::array &rows = root["rows"].get<crude_json::array>();
	if (rows.size() > FORGE_LEVEL_MAX_PADS) {
		error = "pads.json has more pads than a level made here can hold";
		return false;
	}
	for (size_t i = 0; i < rows.size(); i++) {
		const Json &row = rows[i];
		forge_level_pad_t pad = {};
		const std::string ref = stringOf(row, "pad_ref");
		/* The game takes a pad's number from its place in the file. */
		if (trailingNumber(ref, "pad_") != static_cast<long>(i) || ref.size() > 31) {
			error = "pads.json row " + std::to_string(i) + " is named \"" + ref +
				"\"; a pad's name is its place in the file (" + numbered("pad_", static_cast<long>(i)) + ")";
			return false;
		}
		if (!hasString(row, "room_ref") || stringOf(row, "room_ref").size() > 31) {
			error = "pads.json " + ref + " has no room_ref";
			return false;
		}
		if (!row.contains("position") || !vectorOf(row["position"], pad.position) ||
				!row.contains("up") || !vectorOf(row["up"], pad.up) ||
				!row.contains("look") || !vectorOf(row["look"], pad.look)) {
			error = "pads.json " + ref + " needs position, up and look, three numbers each";
			return false;
		}
		copyText(pad.pad_ref, ref);
		copyText(pad.room_ref, stringOf(row, "room_ref"));
		unsigned long flags;
		if (readPadFlags(row, flags)) pad.walk_direct = (flags & kPadWalkDirect) != 0;
		doc.pads.push_back(pad);
		doc.padVolume.push_back(-1);
	}
	return true;
}

/* A spawn's or object's pad: one the level has, or (objects only) none. */
bool padRefOk(const forge_level_doc &doc, const char *file, const std::string &id,
	const std::string &ref, bool mayBeEmpty, std::string &error)
{
	if (ref.empty() && mayBeEmpty) return true;
	if (findPad(doc, ref) < 0) {
		error = std::string(file) + " " + id + " names pad \"" + ref + "\", which the level does not have";
		return false;
	}
	return true;
}

/* coverUnpack addresses the native cover definitions with an eight-bit index. */
constexpr size_t kMaxForgeCoverRows = 256;

bool validateCoverRows(const Json &root, std::string &error)
{
	if (root.contains("generate_routes") && !root["generate_routes"].is_boolean()) {
		error = "navigation/covers.json generate_routes must be true or false";
		return false;
	}
	const auto &rows = root["rows"].get<crude_json::array>();
	for (size_t i = 0; i < rows.size(); ++i) {
		const auto &row = rows[i];
		const std::string ref = stringOf(row, "cover_ref");
		const size_t first = ref.find_first_of("0123456789+-");
		const char *number = first == std::string::npos ? "" : ref.c_str() + first;
		char *end;
		const long ordinal = std::strtol(number, &end, 10);
		f32 position[3], look[3];
		bool flagsValid = false;
		if (row.contains("flags")) {
			const auto &flags = row["flags"];
			if (flags.is_number()) {
				const double value = flags.get<crude_json::number>();
				flagsValid = value >= 0.0 && value <= 0xfffffffful && std::floor(value) == value;
			} else if (flags.is_string()) {
				const std::string value = flags.get<crude_json::string>();
				unsigned long parsed;
				flagsValid = hexWord(value, parsed) || wholeNumber(value) >= 0;
			}
		}
		if (ref.size() >= 64 || number == end || ordinal < 0 || static_cast<size_t>(ordinal) != i
				|| !flagsValid || !row.contains("position") || !row.contains("look")
				|| !vectorOf(row["position"], position) || !finite3(position)
				|| !vectorOf(row["look"], look) || !finite3(look)) {
			error = "navigation/covers.json has an invalid cover row " + std::to_string(i);
			return false;
		}
	}
	return true;
}

void collectEditableCovers(forge_level_doc &doc, const Table &table, const Json &root)
{
	const auto &rows = root["rows"].get<crude_json::array>();
	std::vector<Member> fields;
	for (size_t i = 0; i < rows.size(); ++i) {
		const auto &row = rows[i];
		const auto &raw = table.rows[i];
		const std::string ref = stringOf(row, "cover_ref");
		forge_level_cover_t cover = {};
		const Member *id = nullptr;
		const Member *position = nullptr;
		if (ref.size() >= sizeof(cover.cover_ref) || ref.find('\0') != std::string::npos
				|| !objectMembers(raw, 0, fields)
				|| !roomField(raw, row, fields, "cover_ref", id) || !id
				|| raw.compare(id->value.begin, id->value.end - id->value.begin, "\"" + ref + "\"") != 0
				|| !roomField(raw, row, fields, "position", position) || !position
				|| !vectorOf(row["position"], cover.position)) continue;
		copyText(cover.cover_ref, ref);
		doc.covers.push_back({cover, i});
	}
}

bool readSpawns(forge_level_doc &doc, const Json &root, std::string &error)
{
	const crude_json::array &rows = root["rows"].get<crude_json::array>();
	std::set<std::string> seen;
	for (size_t i = 0; i < rows.size(); i++) {
		const Json &row = rows[i];
		forge_level_spawn_t spawn = {};
		const std::string id = stringOf(row, "spawn_id");
		if (id.empty() || id.size() > 31) {
			error = "spawns.json row " + std::to_string(i) + " has no spawn_id";
			return false;
		}
		if (!seen.insert(id).second) {
			error = "spawns.json lists " + id + " twice";
			return false;
		}
		const std::string team = hasString(row, "team") ? stringOf(row, "team") : std::string("any");
		if (!hasString(row, "pad_ref") ||
				!padRefOk(doc, "spawns.json", id, stringOf(row, "pad_ref"), false, error)) {
			if (error.empty()) error = "spawns.json " + id + " has no pad_ref";
			return false;
		}
		if (team.size() > 31) {
			error = "spawns.json " + id + " has a team name that is too long";
			return false;
		}
		copyText(spawn.spawn_id, id);
		copyText(spawn.pad_ref, stringOf(row, "pad_ref"));
		copyText(spawn.team, team.empty() ? std::string("any") : team);
		doc.spawns.push_back(spawn);
		const long number = trailingNumber(id, "spawn_");
		if (number >= doc.nextSpawn) doc.nextSpawn = number + 1;
		if (i == 0 && plainWord(stringOf(row, "profile"))) doc.spawnProfile = stringOf(row, "profile");
	}
	return true;
}

bool readObjects(forge_level_doc &doc, const Json &root, std::string &error)
{
	const crude_json::array &rows = root["rows"].get<crude_json::array>();
	std::set<std::string> seen;
	for (size_t i = 0; i < rows.size(); i++) {
		const Json &row = rows[i];
		forge_level_object_t object = {};
		const std::string id = stringOf(row, "record_id");
		if (id.empty() || id.size() > 31) {
			error = "objects.json row " + std::to_string(i) + " has no record_id";
			return false;
		}
		if (!seen.insert(id).second) {
			error = "objects.json lists " + id + " twice";
			return false;
		}
		/* An object may stand on no pad (4,470 of the base levels' 15,940 do). */
		const std::string ref = stringOf(row, "pad_ref");
		if (!padRefOk(doc, "objects.json", id, ref, true, error)) return false;
		copyText(object.record_id, id);
		copyText(object.kind, stringOf(row, "kind"));
		copyText(object.pad_ref, ref);
		copyText(object.model_catalog_id, stringOf(row, "model_catalog_id"));
		copyText(object.weapon_catalog_id, stringOf(row, "weapon_catalog_id"));
		object.weapon_slot = -1;
		forge_level_doc::ObjectFlags flags;
		flags.objectsText = stringOf(row, "flags2");
		flags.inObjects = hexWord(flags.objectsText, flags.value);
		flags.original = flags.value;
		doc.objects.push_back(object);
		doc.objectFlags.push_back(flags);
		doc.objectCatalog.push_back(forge_level_doc::ObjectCatalog());
	}
	return true;
}

/* volumes.json: each pad's bounds. A row names its pad; the first row that
 * names a pad is the one that moves with it. */
bool readVolumes(forge_level_doc &doc, const Json &root, std::string &error)
{
	const crude_json::array &rows = root["rows"].get<crude_json::array>();
	for (size_t i = 0; i < rows.size(); i++) {
		const Json &row = rows[i];
		const std::string id = stringOf(row, "volume_id");
		if (id.empty()) {
			error = "volumes.json row " + std::to_string(i) + " has no volume_id";
			return false;
		}
		if (!doc.volumeIds.insert(id).second) {
			error = "volumes.json lists " + id + " twice";
			return false;
		}
		if (!hasString(row, "pad_ref") ||
				!padRefOk(doc, "volumes.json", id, stringOf(row, "pad_ref"), false, error)) {
			if (error.empty()) error = "volumes.json " + id + " has no pad_ref";
			return false;
		}
		const size_t pad = static_cast<size_t>(findPad(doc, stringOf(row, "pad_ref")));
		if (doc.padVolume[pad] < 0) {
			doc.padVolume[pad] = static_cast<int>(i);
			doc.pads[pad].has_volume = 1;
		}
	}
	return true;
}

int findObject(const forge_level_doc &doc, const char *id)
{
	for (size_t i = 0; id && i < doc.objects.size(); i++) {
		if (strcmp(doc.objects[i].record_id, id) == 0) return static_cast<int>(i);
	}
	return -1;
}

bool containsReference(const Json &value, const std::string &id)
{
	if (value.is_string()) return value.get<crude_json::string>() == id;
	if (value.is_array()) {
		for (const Json &item : value.get<crude_json::array>()) {
			if (containsReference(item, id)) return true;
		}
	} else if (value.is_object()) {
		for (const auto &[key, member] : value.get<crude_json::object>()) {
			if (key == id || containsReference(member, id)) return true;
		}
	}
	return false;
}

bool decorationGraphIndependent(const forge_level_doc &doc, const forge_level_object_t &object,
	const std::string &volumeId)
{
	const Table &table = doc.tables[FORGE_LEVEL_MEMBER_LEVEL_GRAPH];
	const std::string &text = table.changed ? table.built : table.original;
	Json graph;
	if (!modAssetJsonReadValue(text.data(), text.size(), graph) || !graph.is_object()
			|| !graph.contains("nodes") || !graph["nodes"].is_array()) return false;
	std::string boundsNode;
	for (const Json &node : graph["nodes"].get<crude_json::array>()) {
		if (stringOf(node, "kind") == "scenario.trigger.volume.source"
				&& stringOf(node, "table") == "volumes.json"
				&& stringOf(node, "volume") == volumeId && stringOf(node, "pad") == object.pad_ref) {
			if (!boundsNode.empty()) return false;
			boundsNode = stringOf(node, "id");
		}
	}
	if (boundsNode.empty()) return false;
	/* Its inert bounds source and inbound load link are part of a normal pad.
	 * No executable node or other graph relationship may use the decoration. */
	const auto referencesDecoration = [&](const Json &value) {
		return containsReference(value, object.record_id) || containsReference(value, object.pad_ref)
			|| containsReference(value, volumeId) || containsReference(value, boundsNode);
	};
	for (const auto &[key, value] : graph.get<crude_json::object>()) {
		if (key == "nodes" || key == "links") {
			if (!value.is_array()) return false;
			for (const Json &item : value.get<crude_json::array>()) {
				if (key == "nodes" && stringOf(item, "id") == boundsNode
						&& stringOf(item, "kind") == "scenario.trigger.volume.source"
						&& stringOf(item, "table") == "volumes.json" && stringOf(item, "volume") == volumeId
						&& stringOf(item, "pad") == object.pad_ref
						&& item.get<crude_json::object>().size() == 5) continue;
				if (key == "links" && stringOf(item, "from") == "scenario.load"
						&& stringOf(item, "to") == boundsNode && item.get<crude_json::object>().size() == 2) continue;
				if (referencesDecoration(item)) return false;
			}
		} else if (referencesDecoration(value)) {
			return false;
		}
	}
	return true;
}

bool placementPadIsIndependent(const forge_level_doc &doc, int padIndex,
	const forge_level_spawn_t *spawn, const forge_level_object_t *object);

int decorationPadIndex(const forge_level_doc &doc, const char *id)
{
	const int index = findObject(doc, id);
	if (index < 0) return -1;
	const auto &object = doc.objects[static_cast<size_t>(index)];
	const auto &flags = doc.objectFlags[static_cast<size_t>(index)];
	if (strcmp(object.kind, kPropKind) != 0 || !object.movable || object.removed || !object.model_settable
			|| object.order < 0 || !flags.known || flags.value != kDecorationFlags2
			|| numbered("setup_", object.order) != id) return -1;
	const int padIndex = findPad(doc, object.pad_ref);
	if (!placementPadIsIndependent(doc, padIndex, nullptr, &object)) return -1;

	const std::string &row = doc.tables[FORGE_LEVEL_MEMBER_OBJECTS].rows[static_cast<size_t>(index)];
	Json source;
	unsigned long first = 0, third = 0;
	if (!modAssetJsonReadValue(row.data(), row.size(), source) || !source.is_object()
			|| source.get<crude_json::object>().size() != 12
			|| !hexWord(stringOf(source, "flags"), first) || !hexWord(stringOf(source, "flags3"), third)
			|| !forgeLevelDecorationFlagsMatch(first, flags.value, third)) return -1;
	for (const char *key : {"weapon_catalog_id", "secondary_weapon_catalog_id", "body_catalog_id", "head_catalog_id", "ailist_ref"}) {
		if (!hasString(source, key) || !stringOf(source, key).empty()) return -1;
	}

	u8 nativeFlags = 0;
	for (const auto &field : doc.fields) {
		if (field.reference == id || (field.record != id && field.value == id)) return -1;
		if (field.record != id) continue;
		if (!field.reference.empty() || (field.field != "command.order" && field.field.compare(0, 5, "base.") != 0)) return -1;
		if (field.field == "base.hidden" && field.value != "0x00000000") return -1;
		if ((field.field == "base.hidden2" || field.field == "base.damage" || field.field == "base.geo_count")
				&& field.value != "0") return -1;
		const u8 flag = field.field == "base.flags" ? 1 : field.field == "base.flags2" ? 2
			: field.field == "base.flags3" ? 4 : 0;
		if (flag) {
			const unsigned long expected = flag == 1 ? first : flag == 2 ? flags.value : third;
			unsigned long actual;
			if ((nativeFlags & flag) || field.type != "u32_hex" || !hexWord(field.value, actual) || actual != expected) return -1;
			nativeFlags |= flag;
		}
	}
	if (nativeFlags != 7) return -1;

	const int bounds = doc.padVolume[static_cast<size_t>(padIndex)];
	if (bounds < 0) return -1;
	const auto &volumeRow = doc.tables[FORGE_LEVEL_MEMBER_VOLUMES].rows[static_cast<size_t>(bounds)];
	Json volume;
	if (!modAssetJsonReadValue(volumeRow.data(), volumeRow.size(), volume)
			|| stringOf(volume, "kind") != "pad_bounds") return -1;
	const std::string volumeId = stringOf(volume, "volume_id");
	return !volumeId.empty() && decorationGraphIndependent(doc, object, volumeId) ? padIndex : -1;
}

/*
 * setup.fields.json: every field of every record, a row each. The document
 * needs three things from it: each object's place in the command list, the
 * field that repeats its pad, and the next free place.
 */
bool readFields(forge_level_doc &doc, const Json &root, std::string &error)
{
	const crude_json::array &rows = root["rows"].get<crude_json::array>();
	for (size_t i = 0; i < rows.size(); i++) {
		const Json &row = rows[i];
		forge_level_doc::FieldRow info;
		info.record = stringOf(row, "record_id");
		info.field = stringOf(row, "field");
		info.value = stringOf(row, "value");
		info.type = stringOf(row, "type");
		info.catalog = stringOf(row, "catalog_id");
		info.reference = stringOf(row, "ref_record_id");
		if (info.record.empty() || info.field.empty() || !hasString(row, "kind") ||
				!hasString(row, "type")) {
			error = "setup.fields.json row " + std::to_string(i) + " needs record_id, kind, field and type";
			return false;
		}
		if (info.field == "command.order") {
			const long order = wholeNumber(info.value);
			if (order < 0) {
				error = "setup.fields.json " + info.record + " has a command.order that is not a number";
				return false;
			}
			if (order >= doc.nextOrder) doc.nextOrder = order + 1;
		}
		doc.recordRows[info.record].push_back(i);
		doc.fields.push_back(info);
	}

	for (size_t objectIndex = 0; objectIndex < doc.objects.size(); objectIndex++) {
		forge_level_object_t &object = doc.objects[objectIndex];
		forge_level_doc::ObjectFlags &flags = doc.objectFlags[objectIndex];
		forge_level_doc::ObjectCatalog &what = doc.objectCatalog[objectIndex];
		const long number = trailingNumber(object.record_id, "setup_");
		object.order = -1;
		object.movable = 0;
		object.removable = 0;
		object.removed = 0;
		object.weapon_slot = -1;
		object.model_settable = 0;
		object.weapon_settable = 0;
		if (number >= doc.nextOrder) doc.nextOrder = number + 1;
		const auto found = doc.recordRows.find(object.record_id);
		if (found == doc.recordRows.end()) continue;
		for (const size_t index : found->second) {
			const forge_level_doc::FieldRow &field = doc.fields[index];
			/* What it is drawn with and what it gives. As with the flags word,
			 * a row that does not agree with objects.json is still read; the
			 * object just cannot be changed here. The first row of each field
			 * is the one that counts. */
			if (field.field == kModelField && what.modelRow < 0 && field.type == "model_catalog_id" &&
					field.catalog == object.model_catalog_id) {
				what.modelRow = static_cast<long>(index);
			}
			if (field.field == kScaleField && what.scaleRow < 0 && wholeNumber(field.value) > 0) {
				what.scaleRow = static_cast<long>(index);
			}
			if (field.field == kWeaponField && what.weaponRow < 0 && strcmp(object.kind, kPickupKind) == 0) {
				const long slot = weaponSlot(field.value);
				if (field.type == "weapon_catalog_id" && !field.catalog.empty() &&
						field.catalog == object.weapon_catalog_id) {
					what.weaponRow = static_cast<long>(index);
				} else if (field.type == "weapon_selector" && slot >= 0 && field.catalog.empty() &&
						!object.weapon_catalog_id[0]) {
					what.weaponRow = static_cast<long>(index);
					object.weapon_slot = static_cast<s32>(slot);
				}
			}
			if (field.field == kFlagsField) {
				/* The two files agree about this word in every base level. One
				 * that does not is still read; its object just cannot be left
				 * out here, because there would be no telling which to change. */
				unsigned long value = 0;
				if (flags.inObjects && !flags.known && hexWord(field.value, value) && value == flags.value) {
					flags.known = true;
					flags.fieldRow = index;
					flags.fieldText = field.value;
				}
			}
			if (field.field == "command.order") {
				object.order = static_cast<s32>(wholeNumber(field.value));
				/* References between records count on this (see the header). */
				if (number >= 0 && number != object.order) {
					error = std::string("setup.fields.json puts ") + object.record_id + " at place " +
						field.value + " of the command list; its id says " + std::to_string(number);
					return false;
				}
			}
			if (isPlaceField(field.field) && object.pad_ref[0]) {
				if (field.value != object.pad_ref) {
					error = std::string("objects.json says ") + object.record_id + " stands on " +
						object.pad_ref + " and setup.fields.json says " + field.value;
					return false;
				}
				object.movable = 1;
			}
		}
		if (flags.known) {
			object.removed = (flags.value & kExcludeEverywhere) == kExcludeEverywhere ? 1 : 0;
			object.removable = kindCanBeLeftOut(object.kind) ? 1 : 0;
			/* What to put back if it stops being left out: the bits it has now,
			 * or none when the level it came from already leaves it out. */
			flags.restore = object.removed ? 0 : (flags.value & kExcludeEverywhere);
		}
		object.model_settable = what.modelRow >= 0 ? 1 : 0;
		/* A pickup of a catalog weapon is drawn with its own record's model at
		 * its own record's size, so all three rows are needed to change it. */
		object.weapon_settable = what.weaponRow >= 0 && what.modelRow >= 0 && what.scaleRow >= 0 ? 1 : 0;
	}
	return true;
}

/* The bounds of a pad at `position` that has no box of its own. */
void padReach(const f32 position[3], f32 lo[3], f32 hi[3])
{
	for (int i = 0; i < 3; i++) {
		lo[i] = position[i] - kPadReach;
		hi[i] = position[i] + kPadReach;
	}
}

/* ------------------------------------------------------------------------ */
/* The level's graph                                                        */
/* ------------------------------------------------------------------------ */
/*
 * level.graph.json is not a list of rows, so it is kept whole and edited as
 * text in three places only: one more element in its `nodes`, one more in
 * its `links`, and numbers in its `counts`. Everything else keeps its bytes.
 */

/* How many times `needle` stands in `text`. */
long countText(const std::string &text, const char *needle)
{
	const size_t length = strlen(needle);
	long count = 0;
	for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + length)) {
		count++;
	}
	return count;
}

/* The value of a top-level key of the graph; for an array, its elements too. */
bool graphValue(const std::string &text, const char *key, Span &value, std::vector<Span> *elements)
{
	size_t start = 0;
	skipSpace(text, start);
	std::vector<Member> members;
	if (!objectMembers(text, start, members)) return false;
	const Member *found = nullptr;
	for (const Member &member : members) {
		if (member.key != key) continue;
		if (found) return false;
		found = &member;
	}
	if (!found) return false;
	value = found->value;
	if (elements) {
		size_t pos = value.begin;
		elements->clear();
		if (text[pos] != '[' || !skipContainer(text, pos, 0, nullptr, elements)) return false;
	}
	return true;
}

/* True when the text between a span's ends holds `marker`. */
bool spanHolds(const std::string &text, const Span &span, const char *marker)
{
	const auto first = text.begin() + static_cast<std::ptrdiff_t>(span.begin);
	const auto last = text.begin() + static_cast<std::ptrdiff_t>(span.end);
	return std::search(first, last, marker, marker + strlen(marker)) != last;
}

/*
 * Puts `element` into the top-level array `key`: after the last element
 * whose text holds `marker`, after the last element when none does, or as
 * the only element of an empty array.
 */
bool graphInsert(std::string &text, const char *key, const char *marker, const std::string &element)
{
	Span array;
	std::vector<Span> elements;
	if (!graphValue(text, key, array, &elements)) return false;
	if (elements.empty()) {
		text.replace(array.begin, array.end - array.begin, "[\n    " + element + "\n  ]");
		return true;
	}
	size_t at = elements.back().end;
	for (size_t i = elements.size(); i-- > 0;) {
		if (spanHolds(text, elements[i], marker)) {
			at = elements[i].end;
			break;
		}
	}
	text.insert(at, ",\n    " + element);
	return true;
}

/* The members of the graph's `counts` object. */
bool graphCounts(const std::string &text, std::vector<Member> &members)
{
	Span counts;
	return graphValue(text, "counts", counts, nullptr) && text[counts.begin] == '{' &&
		objectMembers(text, counts.begin, members);
}

/* The number at counts.<key>, or -1 when the graph has no such whole number. */
long graphCount(const std::string &text, const char *key)
{
	std::vector<Member> members;
	if (!graphCounts(text, members)) return -1;
	for (const Member &member : members) {
		if (member.key == key) {
			return wholeNumber(text.substr(member.value.begin, member.value.end - member.value.begin));
		}
	}
	return -1;
}

/* Writes counts.<key>. False when the graph has no such number to write over. */
bool graphSetCount(std::string &text, const char *key, long value)
{
	std::vector<Member> members;
	if (!graphCounts(text, members)) return false;
	for (const Member &member : members) {
		if (member.key != key) continue;
		const size_t length = member.value.end - member.value.begin;
		if (wholeNumber(text.substr(member.value.begin, length)) < 0) return false;
		text.replace(member.value.begin, length, std::to_string(value));
		return true;
	}
	return false;
}

/* The rows of the table that counts.<kGraphCounts[which]> counts. */
long graphTableRows(const forge_level_doc &doc, int which)
{
	if (which == 0) return static_cast<long>(doc.pads.size());
	if (which == 1) return static_cast<long>(doc.tables[FORGE_LEVEL_MEMBER_VOLUMES].rows.size());
	return static_cast<long>(doc.objects.size());
}

/*
 * Brings the graph's counts of pads, volumes and objects up to the tables.
 * Only a count that was true when the level was read: one that was not is
 * left as it is, because nothing here repairs a level.
 */
void graphSyncCounts(forge_level_doc &doc)
{
	Table &table = doc.tables[FORGE_LEVEL_MEMBER_LEVEL_GRAPH];
	std::string text = table.text();
	bool changed = false;
	for (int i = 0; i < 3; i++) {
		if (!doc.graphCountTrue[i]) continue;
		const long rows = graphTableRows(doc, i);
		if (graphCount(text, kGraphCounts[i]) == rows) continue;
		if (graphSetCount(text, kGraphCounts[i], rows)) changed = true;
	}
	if (changed) table.setWhole(text);
}

/*
 * Where a new trigger volume node hangs from: the `from` of the graph's last
 * link to such a node, as written; the load event when the graph has that
 * node and no such link yet; or "" when the graph gives nothing to copy, and
 * then the new node gets no link.
 */
std::string graphVolumeLinkFrom(const forge_level_doc &doc, const std::string &text)
{
	Span links;
	std::vector<Span> elements;
	if (!graphValue(text, "links", links, &elements)) return std::string();
	for (size_t i = elements.size(); i-- > 0;) {
		if (!spanHolds(text, elements[i], kVolumeLinkTo)) continue;
		std::vector<Member> members;
		if (text[elements[i].begin] != '{' || !objectMembers(text, elements[i].begin, members)) break;
		for (const Member &member : members) {
			if (member.key == "from") {
				return text.substr(member.value.begin, member.value.end - member.value.begin);
			}
		}
		break;
	}
	return doc.graphNodeIds.count("scenario.load") ? std::string("\"scenario.load\"") : std::string();
}

bool graphAddVolumeNode(const forge_level_doc &doc, const std::string &nodeId,
	const std::string &volumeId, const char *padRef, std::string &text)
{
	const std::string from = graphVolumeLinkFrom(doc, text);
	const std::string node = std::string("{ \"id\": \"") + nodeId + "\", " + kVolumeNodeKind +
		", \"table\": \"volumes.json\", \"volume\": \"" + volumeId + "\", \"pad\": \"" + padRef + "\" }";
	const std::string link = "{ \"from\": " + from + ", \"to\": \"" + nodeId + "\" }";
	return graphInsert(text, "nodes", kVolumeNodeKind, node) &&
		(from.empty() || graphInsert(text, "links", kVolumeLinkTo, link));
}

/*
 * `text`, the graph, with the node of a new bounds row and the link the
 * extractor gives every such node. The node's id is the first free
 * "trigger.volume.NNNN" from `number` up, and goes to `nodeId`.
 */
bool graphWithVolume(const forge_level_doc &doc, long number, const std::string &volumeId,
	const char *padRef, std::string &text, std::string &nodeId, std::string &error)
{
	nodeId.clear();
	for (long n = number; n <= 9999; n++) {
		const std::string candidate = numbered("trigger.volume.", n);
		if (!doc.graphNodeIds.count(candidate)) {
			nodeId = candidate;
			break;
		}
	}
	if (nodeId.empty()) {
		error = "level.graph.json has no trigger volume node id left for another pad";
		return false;
	}
	if (!graphAddVolumeNode(doc, nodeId, volumeId, padRef, text)) {
		error = "level.graph.json could not take the new pad's trigger volume node";
		return false;
	}
	return true;
}

struct TriggerIds {
	std::string source;
	std::string enter;
	std::string open;
	std::string give;
};

TriggerIds triggerIds(const std::string &volume)
{
	const std::string prefix = "grid.trigger." + volume;
	return { "trigger.volume.grid." + volume, prefix + ".enter", prefix + ".open", prefix + ".give" };
}

struct TriggerGraphRows {
	std::vector<size_t> nodes;
	std::vector<size_t> links;
	std::vector<size_t> edges;
	std::string target;
	size_t sourceNode = 0;
	size_t targetNode = 0;
	s32 action = FORGE_LEVEL_TRIGGER_OPEN_DOOR;
};

/* Only the native three-node, one-edge pattern owned by Level File. If
 * another authored node or edge uses it, leave the entire binding alone. */
bool triggerGraphRows(const Json &root, const std::string &volume,
	const std::string &pad, TriggerGraphRows &out)
{
	if (!root.is_object() || !root.contains("nodes") || !root["nodes"].is_array()) return false;
	const TriggerIds ids = triggerIds(volume);
	const auto owns = [&](const std::string &id) {
		return id == ids.source || id == ids.enter || id == ids.open || id == ids.give;
	};
	bool source = false;
	bool enter = false;
	const auto &nodes = root["nodes"].get<crude_json::array>();
	for (size_t i = 0; i < nodes.size(); ++i) {
		const auto &node = nodes[i];
		const std::string id = stringOf(node, "id");
		const std::string kind = stringOf(node, "kind");
		const std::string ref = stringOf(node, "volume");
		if (id == ids.source && !source && kind == "scenario.trigger.volume.source" &&
				ref == volume && stringOf(node, "pad") == pad && stringOf(node, "table") == "volumes.json") {
			source = true;
			out.sourceNode = i;
		} else if (id == ids.enter && !enter && kind == kTriggerEvent && ref == volume) {
			enter = true;
		} else if (out.target.empty() && ((id == ids.open && kind == kDoorTriggerAction)
				|| (id == ids.give && kind == kPickupTriggerAction))) {
			out.target = stringOf(node, "object");
			out.targetNode = i;
			out.action = id == ids.give ? FORGE_LEVEL_TRIGGER_GIVE_PICKUP : FORGE_LEVEL_TRIGGER_OPEN_DOOR;
		} else {
			if (owns(id) || ref == volume) return false;
			continue;
		}
		out.nodes.push_back(i);
	}
	if (!source || !enter || out.nodes.size() != 3 || trailingNumber(out.target, "setup_") < 0) return false;
	const auto &actionId = out.action == FORGE_LEVEL_TRIGGER_GIVE_PICKUP ? ids.give : ids.open;
	for (const char *key : {"links", "edges"}) {
		if (!root.contains(key)) continue;
		if (!root[key].is_array()) return false;
		const bool isEdge = strcmp(key, "edges") == 0;
		const auto &rows = root[key].get<crude_json::array>();
		for (size_t i = 0; i < rows.size(); ++i) {
			const std::string from = stringOf(rows[i], "from");
			const std::string to = stringOf(rows[i], "to");
			if (!owns(from) && !owns(to)) continue;
			if (isEdge && out.edges.empty() && from == ids.enter && to == actionId &&
					stringOf(rows[i], "input") == "exec") {
				out.edges.push_back(i);
			} else if (!isEdge && out.links.empty() && !owns(from) && to == ids.source) {
				out.links.push_back(i);
			} else {
				return false;
			}
		}
	}
	return out.edges.size() == 1;
}

bool triggerHalfSize(const forge_level_pad_t &pad, const f32 lo[3], const f32 hi[3], f32 out[3])
{
	f32 halfSize[3];
	for (size_t axis = 0; axis < 3; ++axis) {
		if (!std::isfinite(pad.position[axis]) || !std::isfinite(lo[axis]) || !std::isfinite(hi[axis])
				|| lo[axis] >= hi[axis]) return false;
		const double below = static_cast<double>(pad.position[axis]) - lo[axis];
		const double above = static_cast<double>(hi[axis]) - pad.position[axis];
		const double half = (below + above) * 0.5;
		// Bounds are written to six decimal places after rounding to f32.
		const double roundoff = 0.000001 + std::max(std::abs(lo[axis]), std::abs(hi[axis]))
			* static_cast<double>(std::numeric_limits<f32>::epsilon());
		if (below < 0.0 || above < 0.0 || std::abs(below - above) > roundoff
				|| half < FORGE_LEVEL_TRIGGER_MIN_HALF_SIZE - roundoff * 0.5
				|| half > FORGE_LEVEL_TRIGGER_MAX_HALF_SIZE + roundoff * 0.5) return false;
		halfSize[axis] = static_cast<f32>(std::clamp(half,
			static_cast<double>(FORGE_LEVEL_TRIGGER_MIN_HALF_SIZE),
			static_cast<double>(FORGE_LEVEL_TRIGGER_MAX_HALF_SIZE)));
	}
	std::copy_n(halfSize, 3, out);
	return true;
}

bool readTriggerHalfSize(const Json &row, const forge_level_pad_t &pad, f32 out[3])
{
	f32 lo[3], hi[3];
	return hasString(row, "room_ref") && stringOf(row, "room_ref") == pad.room_ref
		&& row.contains("min") && row.contains("max") && vectorOf(row["min"], lo) && vectorOf(row["max"], hi)
		&& triggerHalfSize(pad, lo, hi, out);
}

struct TriggerTracking {
	std::vector<forge_level_doc::TriggerRow> rows;
	std::vector<forge_level_logic_zone_t> zones;
	std::set<std::string> ownedVolumes;
	std::set<std::string> otherVolumePads;
};

TriggerTracking readTriggers(const forge_level_doc &doc, const Json &root,
	const std::vector<std::string> &rows, const std::vector<int> &padVolumes)
{
	TriggerTracking tracking;
	std::set<std::string> named, used;
	std::map<std::string, std::string> anchors;
	for (const Json &node : root["nodes"].get<crude_json::array>()) {
		const std::string kind = stringOf(node, "kind");
		if (kind != kTriggerEvent && kind != "event.volume.left" && kind != "scenario.trigger.volume.source") continue;
		const std::string volume = stringOf(node, "volume");
		if (volume.empty()) continue;
		const std::string id = stringOf(node, "id");
		const TriggerIds ids = triggerIds(volume);
		if (kind == "scenario.trigger.volume.source") {
			if (id == ids.source) {
				tracking.ownedVolumes.insert(volume);
				if (stringOf(node, "table") == "volumes.json") anchors.emplace(volume, stringOf(node, "pad"));
			}
			continue;
		}
		used.insert(volume);
		if (kind == kTriggerEvent && id == ids.enter) named.insert(volume);
	}
	for (size_t i = 0; (!used.empty() || !anchors.empty()) && i < rows.size(); ++i) {
		Json row;
		if (!modAssetJsonReadValue(rows[i].data(), rows[i].size(), row)) continue;
		const std::string volume = stringOf(row, "volume_id");
		const bool referenced = used.erase(volume) != 0;
		const auto bound = anchors.find(volume);
		if (!referenced && bound == anchors.end()) continue;
		const long number = trailingNumber(volume, "volume_pad_");
		const std::string pad = stringOf(row, "pad_ref");
		const int padIndex = findPad(doc, pad);
		const bool anchored = bound != anchors.end() && bound->second == pad;
		if (bound != anchors.end()) anchors.erase(bound);
		if (padIndex < 0) continue;
		const bool secondary = number >= 0 && number < FORGE_LEVEL_MAX_PADS && volume == numbered("volume_pad_", number)
			&& padVolumes[padIndex] >= 0 && padVolumes[padIndex] != static_cast<int>(i)
			&& stringOf(row, "kind") == "pad_bounds" && stringOf(row, "shape") == "aabb";
		TriggerGraphRows owned;
		if (!secondary || !named.count(volume) || !triggerGraphRows(root, volume, pad, owned)) {
			if (referenced || (secondary && anchored)) tracking.otherVolumePads.insert(pad);
			forge_level_logic_zone_t zone = {};
			if (secondary && anchored && readTriggerHalfSize(row, doc.pads[padIndex], zone.half_size)) {
				copyText(zone.volume_id, volume);
				copyText(zone.pad_ref, pad);
				zone.referenced = referenced;
				tracking.zones.push_back(zone);
			}
			continue;
		}
		forge_level_trigger_t trigger = {};
		copyText(trigger.volume_id, volume);
		copyText(trigger.pad_ref, pad);
		copyText(trigger.target_id, owned.target);
		trigger.action = owned.action;
		trigger.editable = readTriggerHalfSize(row, doc.pads[padIndex], trigger.half_size);
		tracking.rows.push_back({trigger, i});
	}
	return tracking;
}

/* Copy untouched array items and their separators as written. */
bool graphRemoveRows(std::string &text, const char *key, const std::vector<size_t> &removed)
{
	if (removed.empty()) return true;
	Span array;
	std::vector<Span> rows;
	if (!graphValue(text, key, array, &rows) || removed.back() >= rows.size()) return false;
	std::string kept;
	for (size_t i = 0; i < rows.size(); ++i) {
		if (std::binary_search(removed.begin(), removed.end(), i)) continue;
		if (!kept.empty()) kept.append(text, rows[i - 1].end, rows[i].begin - rows[i - 1].end);
		kept.append(text, rows[i].begin, rows[i].end - rows[i].begin);
	}
	text.replace(rows.front().begin, rows.back().end - rows.front().begin, kept);
	return true;
}

/*
 * level.graph.json: read whole by the strict reader like every member, then
 * held as text. What the document needs from it: every node id (so a new
 * node's id is new), the count the game checks, and which of its own counts
 * are true.
 */
bool readGraph(forge_level_doc &doc, const char *json, size_t size, std::string &error)
{
	const char *file = kMemberNames[FORGE_LEVEL_MEMBER_LEVEL_GRAPH];
	Table &table = doc.tables[FORGE_LEVEL_MEMBER_LEVEL_GRAPH];
	Json root;
	if (!json || !size || !modAssetJsonReadValue(json, size, root) || !root.is_object()) {
		error = std::string(file) + " is not one JSON object";
		return false;
	}
	if (!hasString(root, "schema") ||
			stringOf(root, "schema") != kSchemas[FORGE_LEVEL_MEMBER_LEVEL_GRAPH]) {
		error = std::string(file) + " needs \"schema\": \"" + kSchemas[FORGE_LEVEL_MEMBER_LEVEL_GRAPH] + "\"";
		return false;
	}
	if (!root.contains("nodes") || !root["nodes"].is_array()) {
		error = std::string(file) + " has no nodes array";
		return false;
	}
	/* The game takes a graph with `links`, with `edges`, or with both. */
	const bool hasLinks = root.contains("links");
	if (hasLinks && !root["links"].is_array()) {
		error = std::string(file) + " has links that are not an array";
		return false;
	}
	table.original.assign(json, size);

	const crude_json::array &nodes = root["nodes"].get<crude_json::array>();
	for (const Json &node : nodes) {
		const std::string id = stringOf(node, "id");
		if (!id.empty()) doc.graphNodeIds.insert(id);
	}
	Span value;
	std::vector<Span> elements;
	if (!graphValue(table.original, "nodes", value, &elements) || elements.size() != nodes.size() ||
			(hasLinks && (!graphValue(table.original, "links", value, &elements) ||
				elements.size() != root["links"].get<crude_json::array>().size()))) {
		error = std::string(file) + " could not be laid out";
		return false;
	}

	/* The game's own test, on the same text. */
	const long volumeNodes = countText(table.original, kVolumeNodeKind);
	const long volumeRows = graphTableRows(doc, 1);
	if (volumeNodes != volumeRows) {
		error = std::string(file) + " has " + std::to_string(volumeNodes) +
			" trigger volume nodes and volumes.json has " + std::to_string(volumeRows) +
			" rows; the game refuses a level where the two differ";
		return false;
	}
	for (int i = 0; i < 3; i++) {
		doc.graphCountTrue[i] = graphCount(table.original, kGraphCounts[i]) == graphTableRows(doc, i);
	}
	TriggerTracking tracking = readTriggers(doc, root, doc.tables[FORGE_LEVEL_MEMBER_VOLUMES].rows, doc.padVolume);
	doc.triggers = std::move(tracking.rows);
	doc.logicZones = std::move(tracking.zones);
	doc.triggerVolumeIds = std::move(tracking.ownedVolumes);
	doc.logicVolumePads = std::move(tracking.otherVolumePads);
	return true;
}

bool levelLogicEvent(const std::string &kind)
{
	return kind == kTriggerEvent || kind == "event.volume.left" || kind == "event.pickup.collected"
		|| kind == "event.door.opened" || kind == "event.door.closed" || kind == "event.object.used";
}

bool nativeObjectRuleNode(const std::string &id, const std::string &kind)
{
	return id.rfind("object.rules.", 0) == 0 || kind == "event.pickup.touched"
		|| kind == "event.door.use" || kind == "event.door.approach"
		|| kind == "gate.pickup.native_rules" || kind == "og.pickup.collect"
		|| kind == "gate.door.native_unlocked" || kind == "og.door.activate"
		|| kind == "og.door.locked_message" || kind == "og.door.activate_automatic";
}

struct LogicGraph {
	Json root;
	Span nodes, edges;
	bool hasEdges = false;
	std::set<std::string> ids;
	std::map<std::string, const Json *> editable;
	std::map<std::string_view, const Json *> editableEdges;
	std::vector<std::string_view> protectedNodes, protectedEdges;
};

bool graphLogicRead(const std::string &text, LogicGraph &graph)
{
	std::vector<Span> nodeRows, edgeRows;
	if (!modAssetJsonReadValue(text.data(), text.size(), graph.root) || !graph.root.is_object()
			|| !graphValue(text, "nodes", graph.nodes, &nodeRows) || !graph.root["nodes"].is_array()
			|| (graph.root.contains("links") && !graph.root["links"].is_array())) return false;
	graph.hasEdges = graph.root.contains("edges");
	if (graph.hasEdges && (!graphValue(text, "edges", graph.edges, &edgeRows) || !graph.root["edges"].is_array())) {
		return false;
	}
	const auto &nodes = graph.root["nodes"].get<crude_json::array>();
	const crude_json::array empty;
	const auto &edges = graph.hasEdges ? graph.root["edges"].get<crude_json::array>() : empty;
	if (nodes.size() != nodeRows.size() || edges.size() != edgeRows.size() || edges.size() > MISSION_GRAPH_MAX_EDGES) {
		return false;
	}
	std::set<std::string> protectedIds;
	std::map<std::string, std::vector<std::string>> adjacent;
	for (const auto &node : nodes) {
		const std::string id = stringOf(node, "id");
		const std::string kind = stringOf(node, "kind");
		if (!id.empty() && !graph.ids.insert(id).second) return false;
		if (!levelLogicEvent(kind) && kind.rfind("gate.", 0) != 0 && kind.rfind("og.", 0) != 0) {
			protectedIds.insert(id);
		} else if (nativeObjectRuleNode(id, kind)) {
			protectedIds.insert(id);
		}
	}
	if (graph.root.contains("links")) {
		for (const auto &row : graph.root["links"].get<crude_json::array>()) {
			protectedIds.insert(stringOf(row, "from"));
			protectedIds.insert(stringOf(row, "to"));
		}
	}
	for (const auto &edge : edges) {
		const std::string from = stringOf(edge, "from");
		const std::string to = stringOf(edge, "to");
		if (stringOf(edge, "input") == "exec") {
			adjacent[from].push_back(to);
			adjacent[to].push_back(from);
		} else {
			protectedIds.insert(from);
			protectedIds.insert(to);
		}
	}
	/* An executable chain already connected to a structural binding or a
	 * native object-rule hook is part of that binding, including custom gates. */
	std::vector<std::string> queue(protectedIds.begin(), protectedIds.end());
	for (size_t i = 0; i < queue.size(); ++i) {
		const auto neighbors = adjacent.find(queue[i]);
		if (neighbors == adjacent.end()) continue;
		for (const auto &neighbor : neighbors->second) {
			if (protectedIds.insert(neighbor).second) queue.push_back(neighbor);
		}
	}
	const std::string_view bytes(text);
	for (size_t i = 0; i < nodes.size(); ++i) {
		const std::string id = stringOf(nodes[i], "id");
		const auto row = nodeRows[i];
		if (id.empty() || protectedIds.count(id)) graph.protectedNodes.push_back(bytes.substr(row.begin, row.end - row.begin));
		else graph.editable.emplace(id, &nodes[i]);
	}
	for (size_t i = 0; i < edges.size(); ++i) {
		const auto row = edgeRows[i];
		const auto entry = bytes.substr(row.begin, row.end - row.begin);
		if (stringOf(edges[i], "input") == "exec" && !protectedIds.count(stringOf(edges[i], "from"))
				&& !protectedIds.count(stringOf(edges[i], "to"))) {
			graph.editableEdges.emplace(entry, &edges[i]);
		} else {
			graph.protectedEdges.push_back(entry);
		}
	}
	return true;
}

bool graphLogicMetadataMatches(const LogicGraph &first, const LogicGraph &second)
{
	const auto &before = first.root.get<crude_json::object>();
	const auto &after = second.root.get<crude_json::object>();
	if (before.size() - first.hasEdges != after.size() - second.hasEdges) return false;
	for (const auto &[key, val] : before) {
		if (key == "nodes" || key == "edges") continue;
		const auto found = after.find(key);
		if (found == after.end() || val.dump() != found->second.dump()) return false;
	}
	return first.protectedNodes == second.protectedNodes && first.protectedEdges == second.protectedEdges;
}

bool graphLogicScalarFields(const Json &row)
{
	const auto &fields = row.get<crude_json::object>();
	return std::all_of(fields.begin(), fields.end(), [](const auto &field) { return field.second.is_primitive(); });
}

bool graphLogicNodeValid(const forge_level_doc &doc, const Json &node)
{
	if (!graphLogicScalarFields(node)) return false;
	if (stringOf(node, "id").find('\0') != std::string::npos) return false;
	const std::string kind = stringOf(node, "kind");
	const std::string ref = stringOf(node, "object");
	const int index = findObject(doc, ref.c_str());
	const bool live = index >= 0 && !doc.objects[index].removed && doc.objects[index].order >= 0
		&& doc.objects[index].order == trailingNumber(ref, "setup_");
	if (kind == kTriggerEvent || kind == "event.volume.left") return true; // verified against native volumes below
	if (levelLogicEvent(kind)) {
		return !hasString(node, "object") || ref.empty() || ref == "any" || live;
	}
	if (kind == "gate.player.has_item") return catalogIdOk(stringOf(node, "item").c_str());
	if (kind == "gate.object.kind") return !stringOf(node, "object_kind").empty();
	if (kind == kPickupTriggerAction) return forgeLevelDocCanTriggerObject(&doc, FORGE_LEVEL_TRIGGER_GIVE_PICKUP, ref.c_str());
	const bool door = live && strcmp(doc.objects[index].kind, "door") == 0;
	if (kind == kDoorTriggerAction || kind == "og.door.close") return door;
	if (kind == "og.door.lock" || kind == "og.door.unlock") {
		if (!door || !node.contains("bits") || !node["bits"].is_number()) return false;
		const double bits = node["bits"].get<crude_json::number>();
		return bits >= 1 && bits <= 255 && bits == std::floor(bits);
	}
	return false;
}

bool graphLogicVolumesValid(const forge_level_doc &doc, const mission_graph_program_t *program, char *err, size_t err_cap)
{
	std::set<std::string> wanted;
	for (s32 i = 0; i < missionGraphProgramVolumeCount(program); ++i) wanted.insert(missionGraphProgramVolumeAt(program, i));
	for (const auto &text : doc.tables[FORGE_LEVEL_MEMBER_VOLUMES].rows) {
		if (wanted.empty()) return true;
		Json row;
		if (!modAssetJsonReadValue(text.data(), text.size(), row)) continue;
		const std::string id = stringOf(row, "volume_id");
		if (!wanted.count(id) || stringOf(row, "shape") != "aabb" || !row.contains("min") || !row.contains("max")) continue;
		f32 lo[3], hi[3];
		if (vectorOf(row["min"], lo) && vectorOf(row["max"], hi) && finite3(lo) && finite3(hi)
				&& lo[0] <= hi[0] && lo[1] <= hi[1] && lo[2] <= hi[2]) wanted.erase(id);
	}
	if (wanted.empty()) return true;
	setError(err, err_cap, "logic names \"%s\", which is not a native volume box", wanted.begin()->c_str());
	return false;
}

bool graphLogicSplice(std::string &graph, const LogicGraph &before, const std::string &candidate, const LogicGraph &after)
{
	const auto nodes = candidate.substr(after.nodes.begin, after.nodes.end - after.nodes.begin);
	const auto edges = after.hasEdges ? candidate.substr(after.edges.begin, after.edges.end - after.edges.begin) : "[]";
	const auto replace = [&](const Span &span, const std::string &value) {
		graph.replace(span.begin, span.end - span.begin, value);
	};
	if (before.hasEdges && before.edges.begin > before.nodes.begin) replace(before.edges, edges);
	replace(before.nodes, nodes);
	if (before.hasEdges && before.edges.begin < before.nodes.begin) replace(before.edges, edges);
	if (!before.hasEdges && after.hasEdges) {
		std::vector<Member> fields;
		size_t pos = 0;
		skipSpace(graph, pos);
		if (!objectMembers(graph, pos, fields) || fields.empty()) return false;
		graph.insert(fields.back().value.end, ",\n  \"edges\": " + edges);
	}
	return true;
}

/* A new pad at the end of the list, facing along look. Its number, or -1. */
int addPad(forge_level_doc &doc, const f32 position[3], const f32 look[3], std::string &error)
{
	if (!finite3(position) || !finite3(look)) {
		error = "a position and a facing need three finite numbers each";
		return -1;
	}
	const double length = std::sqrt(static_cast<double>(look[0]) * look[0] +
		static_cast<double>(look[1]) * look[1] + static_cast<double>(look[2]) * look[2]);
	if (length < 1e-6) {
		error = "a facing needs a direction; (0, 0, 0) is none";
		return -1;
	}
	if (doc.pads.size() >= FORGE_LEVEL_MAX_PADS) {
		error = "the level already has as many pads as a level made here can hold";
		return -1;
	}

	forge_level_pad_t pad = {};
	const long number = static_cast<long>(doc.pads.size());
	std::string volumeId = numbered("volume_pad_", number);
	if (doc.triggerVolumeIds.count(volumeId)) {
		/* A Grid trigger may already use the new pad's usual volume id.
		 * The runtime looks up the bounds by pad_ref, independently of the id. */
		for (long candidate = FORGE_LEVEL_MAX_PADS - 1; candidate >= 0; --candidate) {
			const std::string available = numbered("volume_pad_", candidate);
			if (doc.volumeIds.count(available)) continue;
			volumeId = available;
			break;
		}
	}
	if (doc.volumeIds.count(volumeId)) {
		error = "volumes.json already has a " + volumeId + ", the name the new pad's bounds would take";
		return -1;
	}
	copyText(pad.pad_ref, numbered("pad_", number));
	for (int i = 0; i < 3; i++) {
		pad.position[i] = position[i];
		pad.look[i] = static_cast<f32>(look[i] / length);
	}
	pad.up[1] = 1.0f;
	pad.added = 1;
	pad.has_volume = 1;

	/* The graph's node for the bounds row, worked out before anything is
	 * changed: the game refuses a level with a bounds row and no node. */
	Table &graphTable = doc.tables[FORGE_LEVEL_MEMBER_LEVEL_GRAPH];
	std::string graph = graphTable.text();
	std::string nodeId;
	if (!graphWithVolume(doc, static_cast<long>(doc.tables[FORGE_LEVEL_MEMBER_VOLUMES].rows.size()),
			volumeId, pad.pad_ref, graph, nodeId, error)) {
		return -1;
	}

	/* The row the extractor would write for a pad with a free facing: no
	 * axis-alignment flags, and the box it gives a pad that has none of its
	 * own (the game's reader wants the keys either way). */
	const std::string row = std::string("{ \"pad_ref\": \"") + pad.pad_ref +
		"\", \"room_ref\": \"\", \"liftnum\": 0, \"flags\": \"0x00000\", \"position\": " +
		vectorText(pad.position) + ", \"up\": " + vectorText(pad.up) + ", \"look\": " +
		vectorText(pad.look) + ", \"bbox\": { \"min\": [-100.000000, -100.000000, -100.000000], "
		"\"max\": [100.000000, 100.000000, 100.000000] } }";
	doc.tables[FORGE_LEVEL_MEMBER_PADS].append(row);
	doc.pads.push_back(pad);

	/* Its bounds row, as every pad of a base level has one. */
	f32 lo[3];
	f32 hi[3];
	padReach(pad.position, lo, hi);
	const std::string volume = std::string("{ \"volume_id\": \"") + volumeId +
		"\", \"pad_ref\": \"" + pad.pad_ref + "\", \"kind\": \"pad_bounds\", \"room_ref\": \"\", "
		"\"shape\": \"aabb\", \"min\": " + vectorText(lo) + ", \"max\": " + vectorText(hi) + " }";
	Table &volumes = doc.tables[FORGE_LEVEL_MEMBER_VOLUMES];
	volumes.append(volume);
	doc.padVolume.push_back(static_cast<int>(volumes.rows.size()) - 1);
	doc.volumeIds.insert(volumeId);

	graphTable.setWhole(graph);
	doc.graphNodeIds.insert(nodeId);
	graphSyncCounts(doc);
	return static_cast<int>(number);
}

bool placementPadIsIndependent(const forge_level_doc &doc, int padIndex,
	const forge_level_spawn_t *spawn, const forge_level_object_t *object)
{
	if (padIndex < 0) return false;
	const auto &pad = doc.pads[static_cast<size_t>(padIndex)];
	if (pad.walk_direct) return false;
	const std::string ref = pad.pad_ref;
	if (doc.hills.count(ref) || doc.routePadsUnknown || doc.routePads.count(ref) || doc.logicVolumePads.count(ref)) return false;
	for (const auto &row : doc.caseMarkers) {
		if (ref == row.marker.pad_ref) return false;
	}
	for (const auto &row : doc.triggers) {
		if (ref == row.trigger.pad_ref) return false;
	}
	for (const auto &row : doc.spawns) {
		if (&row != spawn && ref == row.pad_ref) return false;
	}
	for (const auto &row : doc.objects) {
		if (&row != object && ref == row.pad_ref) return false;
	}
	for (const auto &field : doc.fields) {
		if (field.type == "pad_ref" && field.value == ref
				&& (!object || field.record != object->record_id || !isPlaceField(field.field))) return false;
	}
	return true;
}

/* Generic placement moves only update pads added in this document. */
bool placementNeedsNewPad(const forge_level_doc &doc, int padIndex,
	const forge_level_spawn_t *spawn, const forge_level_object_t *object)
{
	return padIndex < 0 || !doc.pads[static_cast<size_t>(padIndex)].added
		|| !placementPadIsIndependent(doc, padIndex, spawn, object);
}

/* Change a pad and its bounds after checking the placement owns it. */
int movePad(forge_level_doc &doc, int padIndex, const f32 position[3], const f32 look[3],
	std::string &error)
{
	if (!finite3(position) || !finite3(look)) {
		error = "a position and a facing need three finite numbers each";
		return -1;
	}
	const double length = std::sqrt(static_cast<double>(look[0]) * look[0] +
		static_cast<double>(look[1]) * look[1] + static_cast<double>(look[2]) * look[2]);
	if (length < 1e-6) {
		error = "a facing needs a direction; (0, 0, 0) is none";
		return -1;
	}
	forge_level_pad_t &pad = doc.pads[static_cast<size_t>(padIndex)];
	Table &table = doc.tables[FORGE_LEVEL_MEMBER_PADS];
	f32 unit[3];
	for (int i = 0; i < 3; i++) unit[i] = static_cast<f32>(look[i] / length);
	std::string row = table.rows[static_cast<size_t>(padIndex)];
	if (!replaceValue(row, "position", vectorText(position)) ||
			!replaceValue(row, "look", vectorText(unit))) {
		error = std::string(pad.pad_ref) + " could not be rewritten";
		return -1;
	}
	const int volumeIndex = doc.padVolume[static_cast<size_t>(padIndex)];
	std::string volume;
	if (volumeIndex >= 0) {
		f32 lo[3];
		f32 hi[3];
		padReach(position, lo, hi);
		volume = doc.tables[FORGE_LEVEL_MEMBER_VOLUMES].rows[static_cast<size_t>(volumeIndex)];
		if (!replaceValue(volume, "min", vectorText(lo)) || !replaceValue(volume, "max", vectorText(hi))) {
			error = std::string(pad.pad_ref) + "'s bounds could not be rewritten";
			return -1;
		}
	}
	table.rows[static_cast<size_t>(padIndex)] = row;
	table.touch();
	if (volumeIndex >= 0) {
		Table &volumes = doc.tables[FORGE_LEVEL_MEMBER_VOLUMES];
		volumes.rows[static_cast<size_t>(volumeIndex)] = volume;
		volumes.touch();
	}
	for (int i = 0; i < 3; i++) {
		pad.position[i] = position[i];
		pad.look[i] = unit[i];
	}
	return padIndex;
}

template <size_t N> bool copyOut(char (&out)[N], const char *value)
{
	snprintf(out, N, "%s", value ? value : "");
	return true;
}

void removeIntroRow(forge_level_doc &doc, size_t index)
{
	doc.tables[FORGE_LEVEL_MEMBER_INTRO].remove(index);
	auto marker = std::lower_bound(doc.caseMarkers.begin(), doc.caseMarkers.end(), index,
		[](const forge_level_doc::CaseMarkerRow &row, size_t introRow) { return row.introRow < introRow; });
	if (marker != doc.caseMarkers.end() && marker->introRow == index) {
		marker = doc.caseMarkers.erase(marker);
	}
	for (; marker != doc.caseMarkers.end(); ++marker) {
		--marker->introRow;
	}
}

bool validCasePlacement(const forge_level_doc *doc, s32 team, const char *pad_ref, char *err, size_t err_cap)
{
	if (team < 0 || team >= SCENARIO_INTRO_MAX_CASE_TEAMS) {
		setError(err, err_cap, "select a valid base slot");
		return false;
	}
	if (!doc || !pad_ref || findPad(*doc, pad_ref) < 0) {
		setError(err, err_cap, "select a valid pad");
		return false;
	}
	return true;
}

s32 appendCaseMarker(forge_level_doc &doc, s32 team, const char *pad_ref, bool respawn,
		char *err, size_t err_cap)
{
	Table &table = doc.tables[FORGE_LEVEL_MEMBER_INTRO];
	if (doc.caseMarkers.size() >= kCaseBankPadLimit || table.rows.size() >= SCENARIO_INTRO_MAX_ROWS) {
		setError(err, err_cap, "the level has reached its case bank or intro row limit");
		return 0;
	}
	forge_level_case_marker_t marker = {};
	marker.team = team;
	marker.respawn = respawn;
	copyOut(marker.pad_ref, pad_ref);
	const size_t index = table.rows.size();
	table.append(std::string("{\"cmd\":\"") + (respawn ? "case_respawn" : "case")
		+ "\",\"team\":" + std::to_string(team) + ",\"pad_ref\":" + quoted(pad_ref) + "}");
	doc.caseMarkers.push_back({marker, index});
	return 1;
}

} // namespace

extern "C" {

forge_level_doc_t *forgeLevelDocRead(const forge_level_texts_t *texts, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!texts) {
		setError(err, err_cap, "no level was given");
		return nullptr;
	}
	std::unique_ptr<forge_level_doc> doc(new (std::nothrow) forge_level_doc);
	if (!doc) {
		setError(err, err_cap, "out of memory");
		return nullptr;
	}
	std::string error;
	for (int member = 0; member < FORGE_LEVEL_MEMBER_COUNT; member++) {
		if (member == FORGE_LEVEL_MEMBER_LEVEL_GRAPH) {
			/* Not a list of rows, and read last: its rule is about the bounds rows. */
			if (!readGraph(*doc, texts->text[member], texts->size[member], error)) {
				setError(err, err_cap, "%s", error.c_str());
				return nullptr;
			}
			continue;
		}
		if (member == FORGE_LEVEL_MEMBER_INTRO) {
			const bool hasIntro = texts->text[member] != nullptr;
			doc->sourcePresent[member] = hasIntro;
			const char *introText = hasIntro ? texts->text[member] : kEmptyIntro;
			const size_t introSize = hasIntro ? texts->size[member] : sizeof(kEmptyIntro) - 1;
			Json intro;
			if (!readTable(member, introText, introSize, doc->tables[member], intro, error)) {
				setError(err, err_cap, "%s", error.c_str());
				return nullptr;
			}
			/* The setup reader resolves weapon numbers only when loading a stage.
			 * Use its validation rules here while preserving the catalog IDs. */
			scenario_intro_t parsed = {};
			if (!scenarioIntroParse(introText, introSize, [](const char *id, s32 *weapon) -> s32 {
				*weapon = 0;
				return catalogIdOk(id) ? 1 : 0;
			}, &parsed, err, err_cap)) return nullptr;
			scenarioIntroFree(&parsed);
			const auto &rows = intro["rows"].get<crude_json::array>();
			for (size_t i = 0; i < rows.size(); i++) {
				const Json &row = rows[i];
				const std::string cmd = stringOf(row, "cmd");
				if (cmd == "hill") {
					doc->hills.insert(stringOf(row, "pad_ref"));
				} else if (cmd == "case" || cmd == "case_respawn") {
					forge_level_case_marker_t marker = {};
					marker.team = static_cast<s32>(row["team"].get<crude_json::number>());
					marker.respawn = cmd == "case_respawn";
					copyOut(marker.pad_ref, stringOf(row, "pad_ref").c_str());
					doc->caseMarkers.push_back({marker, i});
				}
			}
			continue;
		}
		if (member == FORGE_LEVEL_MEMBER_STAGE) {
			const bool hasStage = texts->text[member] != nullptr;
			doc->sourcePresent[member] = hasStage;
			const char *stage = hasStage ? texts->text[member] : kEmptyStage;
			const size_t size = hasStage ? texts->size[member] : sizeof(kEmptyStage) - 1;
			scenario_stage_rules_t parsed = {};
			if (!scenarioStageRulesParse(stage, size, &parsed, err, err_cap)) return nullptr;
			doc->tables[member].original.assign(stage, size);
			doc->atmosphere = parsed.atmosphere;
			doc->weatherStartKind = parsed.weather.startup_kind;
			doc->weatherStartIntensity = parsed.weather.startup_intensity;
			continue;
		}
		if (member == FORGE_LEVEL_MEMBER_ROOMS) {
			const char *text = texts->text[member];
			const size_t size = texts->size[member];
			if (text && size) {
				doc->sourcePresent[member] = true;
				Table table;
				std::vector<forge_level_doc::RoomBrightnessRow> editable;
				if (collectRoomBrightness(text, size, table, editable)) {
					doc->tables[member] = std::move(table);
					doc->roomBrightness = std::move(editable);
				} else {
					doc->tables[member].original.assign(text, size);
				}
			}
			continue;
		}
		if (member == FORGE_LEVEL_MEMBER_COVERS) {
			const char *text = texts->text[member];
			const size_t size = texts->size[member];
			if (text && size) {
				doc->sourcePresent[member] = true;
				Table table;
				Json root;
				std::string coverError;
				if (readTable(member, text, size, table, root, coverError)
						&& table.rows.size() <= kMaxForgeCoverRows && validateCoverRows(root, coverError)) {
					collectEditableCovers(*doc, table, root);
					doc->tables[member] = std::move(table);
				} else {
					/* Preserve unsupported native sources for other edits. */
					doc->tables[member].original.assign(text, size);
				}
			}
			continue;
		}
		if (member == FORGE_LEVEL_MEMBER_PATHS) {
			/* Older snapshots omit this member. Carry its source through other edits. */
			if (texts->text[member] && texts->size[member]) {
				doc->sourcePresent[member] = true;
				doc->tables[member].original.assign(texts->text[member], texts->size[member]);
				readRoutePadRefs(*doc, texts->text[member], texts->size[member]);
			}
			continue;
		}
		Json root;
		bool ok = readTable(member, texts->text[member], texts->size[member], doc->tables[member], root, error);
		if (ok && member == FORGE_LEVEL_MEMBER_PADS) ok = readPads(*doc, root, error);
		if (ok && member == FORGE_LEVEL_MEMBER_SPAWNS) ok = readSpawns(*doc, root, error);
		if (ok && member == FORGE_LEVEL_MEMBER_OBJECTS) ok = readObjects(*doc, root, error);
		if (ok && member == FORGE_LEVEL_MEMBER_VOLUMES) ok = readVolumes(*doc, root, error);
		if (ok && member == FORGE_LEVEL_MEMBER_SETUP_FIELDS) ok = readFields(*doc, root, error);
		if (!ok) {
			setError(err, err_cap, "%s", error.c_str());
			return nullptr;
		}
	}
	return doc.release();
}

void forgeLevelDocFree(forge_level_doc_t *doc)
{
	delete doc;
}

s32 forgeLevelDocMemberOptional(s32 member)
{
	return member == FORGE_LEVEL_MEMBER_INTRO || member == FORGE_LEVEL_MEMBER_STAGE
		|| member == FORGE_LEVEL_MEMBER_COVERS || member == FORGE_LEVEL_MEMBER_PATHS || member == FORGE_LEVEL_MEMBER_ROOMS;
}

s32 forgeLevelDocRoomBrightnessCount(const forge_level_doc_t *doc)
{
	return doc ? static_cast<s32>(doc->roomBrightness.size()) : 0;
}

s32 forgeLevelDocGetRoomBrightness(const forge_level_doc_t *doc, s32 index, forge_level_room_brightness_t *out)
{
	if (!out || index < 0 || index >= forgeLevelDocRoomBrightnessCount(doc)) return 0;
	*out = doc->roomBrightness[index].value;
	return 1;
}

s32 forgeLevelDocSetRoomBrightness(forge_level_doc_t *doc, s32 roomnum, s32 minimum, s32 maximum,
	char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!doc || !validRoomBrightness(minimum, maximum)) {
		setError(err, err_cap, "choose a room brightness range with minimum and maximum between 0 and 255");
		return 0;
	}
	const auto room = std::find_if(doc->roomBrightness.begin(), doc->roomBrightness.end(), [roomnum](const auto &entry) {
		return entry.value.roomnum == roomnum;
	});
	if (room == doc->roomBrightness.end()) {
		setError(err, err_cap, "select an editable native room");
		return 0;
	}
	if (room->value.minimum == minimum && room->value.maximum == maximum) {
		setError(err, err_cap, "the room brightness is unchanged");
		return 0;
	}

	auto &table = doc->tables[FORGE_LEVEL_MEMBER_ROOMS];
	std::string row = table.rows[room->roomRow];
	const std::string brightness = "[" + std::to_string(minimum) + ", " + std::to_string(maximum) + "]";
	if (room->hasRange) {
		if (!replaceValue(row, "brightness", brightness)) {
			setError(err, err_cap, "the native brightness field could not be rewritten");
			return 0;
		}
	} else {
		row.insert(1, " \"brightness\": " + brightness + ",");
	}
	table.rows[room->roomRow] = std::move(row);
	table.touch();
	room->value.minimum = minimum;
	room->value.maximum = maximum;
	room->hasRange = true;
	return 1;
}

s32 forgeLevelDocGetAtmosphere(const forge_level_doc_t *doc, scenario_stage_atmosphere_t *out)
{
	if (!doc || !out) return 0;
	*out = doc->atmosphere;
	return 1;
}

/* Edit one stage family without rewriting the source of any other family. */
static s32 setStageRule(forge_level_doc_t &doc, const char *key, const std::string &value,
		char *err, size_t err_cap)
{
	auto &table = doc.tables[FORGE_LEVEL_MEMBER_STAGE];
	std::string updated = table.text();
	std::vector<Member> members;
	size_t begin = 0;
	skipSpace(updated, begin);
	if (!objectMembers(updated, begin, members) || members.empty()) {
		setError(err, err_cap, "the current stage settings could not be read");
		return 0;
	}

	size_t index = 0;
	for (; index < members.size(); ++index) {
		const auto &member = members[index];
		if (member.key == key) break;
		if (member.key.find('\\') != std::string::npos) {
			const std::string escaped = quoted(member.key);
			Json decoded;
			if (modAssetJsonReadValue(escaped.data(), escaped.size(), decoded)
					&& decoded.is_string() && decoded.get<crude_json::string>() == key) break;
		}
	}

	if (index == members.size()) {
		if (!value.empty()) updated.insert(members.back().value.end, ", " + quoted(key) + ": " + value);
	} else if (!value.empty()) {
		const Span &span = members[index].value;
		updated.replace(span.begin, span.end - span.begin, value);
	} else if (index + 1 < members.size()) {
		updated.erase(members[index].begin, members[index + 1].begin - members[index].begin);
	} else {
		// The mandatory schema member precedes an optional field in this case.
		const size_t previousEnd = members[index - 1].value.end;
		updated.erase(previousEnd, members[index].value.end - previousEnd);
	}

	scenario_stage_rules_t parsed = {};
	if (!scenarioStageRulesParse(updated.data(), updated.size(), &parsed, err, err_cap)) return 0;
	if (updated != table.text()) table.setWhole(updated);
	doc.atmosphere = parsed.atmosphere;
	doc.weatherStartKind = parsed.weather.startup_kind;
	doc.weatherStartIntensity = parsed.weather.startup_intensity;
	return 1;
}

s32 forgeLevelDocSetAtmosphere(forge_level_doc_t *doc, const scenario_stage_atmosphere_t *look,
		char *err, size_t err_cap)
{
	if (!doc || !look || !std::memchr(look->primary_music, '\0', sizeof(look->primary_music))) {
		setError(err, err_cap, "invalid atmosphere document");
		return 0;
	}
	crude_json::object atmosphere;
	if (look->has_sky) {
		crude_json::array rgb;
		for (u8 channel : look->sky_rgb) rgb.emplace_back(static_cast<double>(channel));
		atmosphere.emplace("sky_rgb", std::move(rgb));
	}
	if (look->has_fog) {
		atmosphere.emplace("fog", crude_json::object{
			{"start", static_cast<double>(look->fog_start)}, {"end", static_cast<double>(look->fog_end)}
		});
	}
	if (look->has_ambient) atmosphere.emplace("ambient_light", static_cast<double>(look->ambient_light));
	if (look->primary_music[0]) atmosphere.emplace("primary_music", look->primary_music);
	const std::string source = atmosphere.empty() ? std::string() : Json(std::move(atmosphere)).dump();
	return setStageRule(*doc, "atmosphere", source, err, err_cap);
}

s32 forgeLevelDocGetWeatherStart(const forge_level_doc_t *doc, s32 *kind, s32 *intensity)
{
	if (!doc || !kind || !intensity) return 0;
	*kind = doc->weatherStartKind;
	*intensity = doc->weatherStartIntensity;
	return 1;
}

s32 forgeLevelDocSetWeatherStart(forge_level_doc_t *doc, s32 kind, s32 intensity,
		char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!doc || kind < SCENARIO_STAGE_WEATHER_START_NONE || kind > SCENARIO_STAGE_WEATHER_START_SNOW
			|| (kind == SCENARIO_STAGE_WEATHER_START_NONE && intensity != 0)) {
		setError(err, err_cap, "invalid stage weather startup");
		return 0;
	}
	if (kind == doc->weatherStartKind && intensity == doc->weatherStartIntensity) return 0;
	std::string source;
	if (kind != SCENARIO_STAGE_WEATHER_START_NONE) {
		const char *name = kind == SCENARIO_STAGE_WEATHER_START_RAIN ? "rain" : "snow";
		source = Json(crude_json::object{
			{ "kind", name },
			{ "intensity", static_cast<double>(intensity) },
		}).dump();
	}
	return setStageRule(*doc, "weather_start", source, err, err_cap);
}

s32 forgeLevelDocHasHill(const forge_level_doc_t *doc, const char *pad_ref)
{
	return doc && pad_ref && doc->hills.count(pad_ref) != 0;
}

s32 forgeLevelDocHillCount(const forge_level_doc_t *doc)
{
	return doc ? static_cast<s32>(doc->hills.size()) : 0;
}

s32 forgeLevelDocGetHillPad(const forge_level_doc_t *doc, s32 index, char *out, size_t cap)
{
	if (!out || !cap || index < 0 || index >= forgeLevelDocHillCount(doc)) return 0;
	const std::string &id = *std::next(doc->hills.begin(), index);
	if (id.size() >= cap) return 0;
	memcpy(out, id.c_str(), id.size() + 1);
	return 1;
}

s32 forgeLevelDocSetHill(forge_level_doc_t *doc, const char *pad_ref, s32 present,
		char *err, size_t err_cap)
{
	if (!doc || !pad_ref || (present && findPad(*doc, pad_ref) < 0)) {
		setError(err, err_cap, "select a valid pad");
		return 0;
	}
	const bool exists = forgeLevelDocHasHill(doc, pad_ref);
	if ((present != 0) == exists) {
		setError(err, err_cap, exists ? "this pad already has a hill" : "this pad has no hill");
		return 0;
	}
	Table &table = doc->tables[FORGE_LEVEL_MEMBER_INTRO];
	if (present) {
		if (doc->hills.size() >= NET_SCENARIO_KOH_MAX_HILLS || table.rows.size() >= SCENARIO_INTRO_MAX_ROWS) {
			setError(err, err_cap, "the level has reached its hill or intro row limit");
			return 0;
		}
		table.append("{\"cmd\":\"hill\",\"pad_ref\":" + quoted(pad_ref) + "}");
		doc->hills.insert(pad_ref);
	} else {
		for (size_t i = table.rows.size(); i > 0; i--) {
			const auto &row = table.rows[i - 1];
			Json entry;
			if (modAssetJsonReadValue(row.data(), row.size(), entry)
					&& stringOf(entry, "cmd") == "hill" && stringOf(entry, "pad_ref") == pad_ref) {
				removeIntroRow(*doc, i - 1);
			}
		}
		doc->hills.erase(pad_ref);
	}
	return 1;
}

s32 forgeLevelDocCaseMarkerCount(const forge_level_doc_t *doc)
{
	return doc ? static_cast<s32>(doc->caseMarkers.size()) : 0;
}

s32 forgeLevelDocGetCaseMarker(const forge_level_doc_t *doc, s32 index, forge_level_case_marker_t *out)
{
	if (!out || index < 0 || index >= forgeLevelDocCaseMarkerCount(doc)) return 0;
	*out = doc->caseMarkers[index].marker;
	return 1;
}

s32 forgeLevelDocSetCaseHome(forge_level_doc_t *doc, s32 team, const char *pad_ref,
		char *err, size_t err_cap)
{
	if (!validCasePlacement(doc, team, pad_ref, err, err_cap)) return 0;
	/* The last home row in a slot wins in Capture the Case. Keep legacy
	 * duplicate rows in place because the other scenarios use them too. */
	for (auto row = doc->caseMarkers.rbegin(); row != doc->caseMarkers.rend(); ++row) {
		if (row->marker.respawn || row->marker.team != team) continue;
		if (std::strcmp(row->marker.pad_ref, pad_ref) == 0) {
			setError(err, err_cap, "this pad is already the case home for this base slot");
			return 0;
		}
		Table &table = doc->tables[FORGE_LEVEL_MEMBER_INTRO];
		if (!replaceValue(table.rows[row->introRow], "pad_ref", quoted(pad_ref))) {
			setError(err, err_cap, "could not replace the case home pad");
			return 0;
		}
		copyOut(row->marker.pad_ref, pad_ref);
		table.touch();
		return 1;
	}
	return appendCaseMarker(*doc, team, pad_ref, false, err, err_cap);
}

s32 forgeLevelDocAddCaseRespawn(forge_level_doc_t *doc, s32 team, const char *pad_ref,
		char *err, size_t err_cap)
{
	if (!validCasePlacement(doc, team, pad_ref, err, err_cap)) return 0;
	s32 respawns = 0;
	for (const auto &row : doc->caseMarkers) {
		if (!row.marker.respawn || row.marker.team != team) continue;
		if (std::strcmp(row.marker.pad_ref, pad_ref) == 0) {
			setError(err, err_cap, "this base slot already uses this respawn pad");
			return 0;
		}
		++respawns;
	}
	if (respawns >= NET_SCENARIO_CTC_SPAWNPADS) {
		setError(err, err_cap, "this base slot has reached its respawn limit");
		return 0;
	}
	return appendCaseMarker(*doc, team, pad_ref, true, err, err_cap);
}

s32 forgeLevelDocRemoveCaseMarker(forge_level_doc_t *doc, s32 index, char *err, size_t err_cap)
{
	if (index < 0 || index >= forgeLevelDocCaseMarkerCount(doc)) {
		setError(err, err_cap, "select a case marker to remove");
		return 0;
	}
	removeIntroRow(*doc, doc->caseMarkers[index].introRow);
	return 1;
}

s32 forgeLevelDocTriggerCount(const forge_level_doc_t *doc)
{
	return doc ? static_cast<s32>(doc->triggers.size()) : 0;
}

s32 forgeLevelDocGetTrigger(const forge_level_doc_t *doc, s32 index, forge_level_trigger_t *out)
{
	if (!out || index < 0 || index >= forgeLevelDocTriggerCount(doc)) return 0;
	*out = doc->triggers[index].trigger;
	return 1;
}

static bool ordinaryTriggerPickup(const char *id)
{
	return catalogIdOk(id) && strcmp(id, "base:briefcase2") != 0
		&& strcmp(id, "base:datauplink") != 0 && strcmp(id, "base:bolt") != 0;
}

s32 forgeLevelDocCanTriggerObject(const forge_level_doc_t *doc, s32 action, const char *record_id)
{
	const int index = doc ? findObject(*doc, record_id) : -1;
	if (index < 0) return 0;
	const auto &object = doc->objects[index];
	if (object.order < 0 || object.order != trailingNumber(record_id, "setup_")
			|| object.removed || !object.movable) return 0;
	if (action == FORGE_LEVEL_TRIGGER_OPEN_DOOR) return strcmp(object.kind, "door") == 0;
	if (action != FORGE_LEVEL_TRIGGER_GIVE_PICKUP || strcmp(object.kind, kPickupKind) != 0
			|| !object.weapon_settable || object.weapon_slot >= 0 || !doc->objectFlags[index].known
			|| !ordinaryTriggerPickup(object.weapon_catalog_id)) return 0;
	const auto &fields = doc->recordRows.at(object.record_id);
	return std::count_if(fields.begin(), fields.end(), [&](size_t row) {
		return doc->fields[row].field == kWeaponField;
	}) == 1;
}

static bool triggerTargetAvailable(const forge_level_doc_t *doc, s32 action, const char *pad_ref,
	const char *target_id, const forge_level_trigger_t *editing, char *err, size_t err_cap)
{
	const bool pickup = action == FORGE_LEVEL_TRIGGER_GIVE_PICKUP;
	if (!forgeLevelDocCanTriggerObject(doc, action, target_id)) {
		setError(err, err_cap, pickup ? "select a placed fixed weapon pickup" : "select a placed door");
		return false;
	}
	for (const auto &existing : doc->triggers) {
		if (&existing.trigger != editing && existing.trigger.action == action && strcmp(existing.trigger.pad_ref, pad_ref) == 0
				&& strcmp(existing.trigger.target_id, target_id) == 0) {
			setError(err, err_cap, pickup ? "this pad already has a trigger for this pickup"
				: "this pad already has a trigger for this door");
			return false;
		}
	}
	return true;
}

static bool triggerBounds(const forge_level_pad_t &pad, const f32 half_size[3],
	f32 lo[3], f32 hi[3], char *err, size_t err_cap)
{
	for (int axis = 0; axis < 3; ++axis) {
		if (!half_size || !std::isfinite(half_size[axis]) ||
				half_size[axis] < FORGE_LEVEL_TRIGGER_MIN_HALF_SIZE ||
				half_size[axis] > FORGE_LEVEL_TRIGGER_MAX_HALF_SIZE) {
			setError(err, err_cap, "set each trigger half-size between %d and %d",
				FORGE_LEVEL_TRIGGER_MIN_HALF_SIZE, FORGE_LEVEL_TRIGGER_MAX_HALF_SIZE);
			return false;
		}
		lo[axis] = pad.position[axis] - half_size[axis];
		hi[axis] = pad.position[axis] + half_size[axis];
		if (!std::isfinite(lo[axis]) || !std::isfinite(hi[axis]) || lo[axis] >= hi[axis]) {
			setError(err, err_cap, "the trigger bounds are outside the level coordinate range");
			return false;
		}
	}
	return true;
}

static long nextTriggerVolume(const forge_level_doc &doc, std::string &volume, TriggerIds &ids)
{
	for (long number = doc.nextTriggerVolume; number >= 0; --number) {
		volume = numbered("volume_pad_", number);
		ids = triggerIds(volume);
		if (!doc.volumeIds.count(volume) && !doc.graphNodeIds.count(ids.source) && !doc.graphNodeIds.count(ids.enter)
				&& !doc.graphNodeIds.count(ids.open) && !doc.graphNodeIds.count(ids.give)) return number;
	}
	return -1;
}

static std::string triggerVolumeRow(const std::string &volume, const forge_level_pad_t &pad,
	const f32 lo[3], const f32 hi[3])
{
	return "{ \"volume_id\": " + quoted(volume) + ", \"pad_ref\": " + quoted(pad.pad_ref) +
		", \"kind\": \"pad_bounds\", \"room_ref\": " + quoted(pad.room_ref) +
		", \"shape\": \"aabb\", \"min\": " + vectorText(lo) + ", \"max\": " + vectorText(hi) + " }";
}

s32 forgeLevelDocAddTrigger(forge_level_doc_t *doc, s32 action, const char *pad_ref, const char *target_id,
	const f32 half_size[3], char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (action != FORGE_LEVEL_TRIGGER_OPEN_DOOR && action != FORGE_LEVEL_TRIGGER_GIVE_PICKUP) {
		setError(err, err_cap, "select a trigger action");
		return 0;
	}
	const bool pickup = action == FORGE_LEVEL_TRIGGER_GIVE_PICKUP;
	const int padIndex = doc && pad_ref ? findPad(*doc, pad_ref) : -1;
	if (padIndex < 0 || doc->padVolume[padIndex] < 0) {
		setError(err, err_cap, "select an existing pad with bounds");
		return 0;
	}
	if (!triggerTargetAvailable(doc, action, pad_ref, target_id, nullptr, err, err_cap)) return 0;
	const forge_level_pad_t &pad = doc->pads[padIndex];
	const std::string room = pad.room_ref;
	if (!room.empty() && trailingNumber(room, "room_") < 0 && trailingNumber(room, "room_-") < 0) {
		setError(err, err_cap, "the pad has no valid room reference");
		return 0;
	}
	f32 lo[3], hi[3];
	if (!triggerBounds(pad, half_size, lo, hi, err, err_cap)) return 0;
	std::string volume;
	TriggerIds ids;
	const long number = nextTriggerVolume(*doc, volume, ids);
	if (number < 0) {
		setError(err, err_cap, "the level has no free trigger volume id");
		return 0;
	}
	Table &graphTable = doc->tables[FORGE_LEVEL_MEMBER_LEVEL_GRAPH];
	std::string graph = graphTable.text();
	const auto &actionId = pickup ? ids.give : ids.open;
	const std::string enter = "{ \"id\": " + quoted(ids.enter) + ", \"kind\": " +
		quoted(kTriggerEvent) + ", \"volume\": " + quoted(volume) + " }";
	const std::string target = "{ \"id\": " + quoted(actionId) + ", \"kind\": " +
		quoted(pickup ? kPickupTriggerAction : kDoorTriggerAction) + ", \"object\": " + quoted(target_id) + " }";
	const std::string edge = "{ \"from\": " + quoted(ids.enter) + ", \"to\": " +
		quoted(actionId) + ", \"input\": \"exec\" }";
	if (!graphAddVolumeNode(*doc, ids.source, volume, pad_ref, graph) ||
			!graphInsert(graph, "nodes", "", enter) || !graphInsert(graph, "nodes", "", target) ||
			!graphInsert(graph, "edges", "", edge)) {
		setError(err, err_cap, "level.graph.json needs nodes and execution edges");
		return 0;
	}
	/* The runner validates topology and runtime budgets. The editor preserves
	 * the graph's pre-existing modules while adding an existing native action. */
	mission_graph_program_t *program = missionGraphProgramCompileLevel(graph.data(), graph.size(),
		[](const char *) -> s32 { return 1; }, err, err_cap);
	if (!program) return 0;
	missionGraphProgramFree(program);

	forge_level_trigger_t trigger = {};
	copyText(trigger.volume_id, volume);
	copyText(trigger.pad_ref, pad_ref);
	copyText(trigger.target_id, target_id);
	trigger.action = action;
	trigger.editable = triggerHalfSize(pad, lo, hi, trigger.half_size);
	Table &volumes = doc->tables[FORGE_LEVEL_MEMBER_VOLUMES];
	doc->triggers.push_back({trigger, volumes.rows.size()});
	doc->nextTriggerVolume = number - 1;
	volumes.append(triggerVolumeRow(volume, pad, lo, hi));
	doc->volumeIds.insert(volume);
	doc->triggerVolumeIds.insert(volume);
	doc->graphNodeIds.insert(ids.source);
	doc->graphNodeIds.insert(ids.enter);
	doc->graphNodeIds.insert(actionId);
	graphTable.setWhole(graph);
	graphSyncCounts(*doc);
	return 1;
}

static bool replaceTriggerGraphField(std::string &graph, size_t index, const char *field, const char *value)
{
	Span array;
	std::vector<Span> nodes;
	if (!graphValue(graph, "nodes", array, &nodes) || index >= nodes.size()) return false;
	const Span node = nodes[index];
	std::string text = graph.substr(node.begin, node.end - node.begin);
	if (!replaceValue(text, field, quoted(value))) return false;
	graph.replace(node.begin, node.end - node.begin, text);
	return true;
}

static bool triggerAnchorRoomValid(const std::string &room)
{
	if (room.empty()) return true;
	const long assigned = trailingNumber(room, "room_");
	const long unassigned = trailingNumber(room, "room_-");
	/* The native pad header carries a signed 10-bit room; -1 is inferred. */
	return (assigned >= 0 && assigned <= 511) || unassigned == 1;
}

s32 forgeLevelDocUpdateTrigger(forge_level_doc_t *doc, const char *volume_id, const char *pad_ref, const char *target_id,
	const f32 half_size[3], char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!doc || !volume_id) {
		setError(err, err_cap, "select an editable trigger");
		return 0;
	}
	auto found = std::find_if(doc->triggers.begin(), doc->triggers.end(), [&](const auto &row) {
		return strcmp(row.trigger.volume_id, volume_id) == 0;
	});
	if (found == doc->triggers.end() || !found->trigger.editable) {
		setError(err, err_cap, "select an editable pad-centered trigger");
		return 0;
	}
	const auto &current = found->trigger;
	const int padIndex = pad_ref ? findPad(*doc, pad_ref) : -1;
	if (padIndex < 0 || doc->padVolume[padIndex] < 0) {
		setError(err, err_cap, "select an existing trigger pad with bounds");
		return 0;
	}
	const auto &pad = doc->pads[padIndex];
	const bool reanchor = strcmp(current.pad_ref, pad.pad_ref) != 0;
	if (reanchor && !triggerAnchorRoomValid(pad.room_ref)) {
		setError(err, err_cap, "the destination pad has no valid room reference");
		return 0;
	}
	if (!triggerTargetAvailable(doc, current.action, pad.pad_ref, target_id, &current, err, err_cap)) return 0;
	f32 lo[3], hi[3];
	if (!triggerBounds(pad, half_size, lo, hi, err, err_cap)) return 0;
	const bool resize = !std::equal(half_size, half_size + 3, current.half_size);
	const bool retarget = strcmp(current.target_id, target_id) != 0;
	if (!reanchor && !resize && !retarget) {
		setError(err, err_cap, "the trigger is unchanged");
		return 0;
	}

	Table &volumes = doc->tables[FORGE_LEVEL_MEMBER_VOLUMES];
	const size_t volumeRow = found->volumeRow;
	const size_t primaryRow = static_cast<size_t>(doc->padVolume[padIndex]);
	if (volumeRow >= volumes.rows.size() || primaryRow >= volumes.rows.size() || volumeRow == primaryRow) {
		setError(err, err_cap, "the trigger needs a separate native volume row and pad bounds");
		return 0;
	}
	const bool rewriteBounds = reanchor || resize;
	std::string volume;
	forge_level_trigger_t edited = current;
	if (rewriteBounds) {
		volume = volumes.rows[volumeRow];
		Json checked;
		if ((reanchor && (!replaceValue(volume, "pad_ref", quoted(pad.pad_ref))
					|| !replaceValue(volume, "room_ref", quoted(pad.room_ref))))
				|| !replaceValue(volume, "min", vectorText(lo)) || !replaceValue(volume, "max", vectorText(hi))
				|| !modAssetJsonReadValue(volume.data(), volume.size(), checked)
				|| stringOf(checked, "volume_id") != current.volume_id || stringOf(checked, "pad_ref") != pad.pad_ref
				|| stringOf(checked, "kind") != "pad_bounds" || stringOf(checked, "shape") != "aabb"
				|| !readTriggerHalfSize(checked, pad, edited.half_size)) {
			setError(err, err_cap, "the trigger bounds could not be rewritten");
			return 0;
		}
		copyOut(edited.pad_ref, pad.pad_ref);
	}

	Table &graphTable = doc->tables[FORGE_LEVEL_MEMBER_LEVEL_GRAPH];
	std::string graph = graphTable.text();
	Json root;
	TriggerGraphRows owned;
	if (!modAssetJsonReadValue(graph.data(), graph.size(), root) ||
			!triggerGraphRows(root, current.volume_id, current.pad_ref, owned) ||
			owned.action != current.action || owned.target != current.target_id) {
		setError(err, err_cap, "the trigger shares graph references with other rules");
		return 0;
	}
	if (reanchor && !replaceTriggerGraphField(graph, owned.sourceNode, "pad", pad.pad_ref)) {
		setError(err, err_cap, "the trigger graph anchor could not be rewritten");
		return 0;
	}
	if (retarget && !replaceTriggerGraphField(graph, owned.targetNode, "object", target_id)) {
		setError(err, err_cap, "the trigger action could not be rewritten");
		return 0;
	}
	if (reanchor || retarget) {
		mission_graph_program_t *program = missionGraphProgramCompileLevel(graph.data(), graph.size(),
			[](const char *) -> s32 { return 1; }, err, err_cap);
		if (!program) return 0;
		missionGraphProgramFree(program);
		if (retarget) copyOut(edited.target_id, target_id);
		graphTable.setWhole(graph);
	}
	found->trigger = edited;
	if (rewriteBounds) {
		volumes.rows[volumeRow] = std::move(volume);
		volumes.touch();
	}
	if (reanchor && volumeRow < primaryRow) {
		/* Both the document and native room queries use the first volume for a pad. */
		std::rotate(volumes.rows.begin() + volumeRow, volumes.rows.begin() + volumeRow + 1,
			volumes.rows.begin() + primaryRow + 1);
		for (auto &padRow : doc->padVolume) {
			if (padRow > static_cast<int>(volumeRow) && padRow <= static_cast<int>(primaryRow)) --padRow;
		}
		for (auto &trigger : doc->triggers) {
			if (&trigger != &*found && trigger.volumeRow > volumeRow && trigger.volumeRow <= primaryRow) {
				--trigger.volumeRow;
			}
		}
		found->volumeRow = primaryRow;
		std::sort(doc->triggers.begin(), doc->triggers.end(), [](const auto &a, const auto &b) {
			return a.volumeRow < b.volumeRow;
		});
	}
	return 1;
}

s32 forgeLevelDocRemoveTrigger(forge_level_doc_t *doc, s32 index, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (index < 0 || index >= forgeLevelDocTriggerCount(doc)) {
		setError(err, err_cap, "select a trigger to remove");
		return 0;
	}
	const auto row = doc->triggers[index];
	Table &graphTable = doc->tables[FORGE_LEVEL_MEMBER_LEVEL_GRAPH];
	std::string graph = graphTable.text();
	Json root;
	TriggerGraphRows owned;
	if (!modAssetJsonReadValue(graph.data(), graph.size(), root) ||
			!triggerGraphRows(root, row.trigger.volume_id, row.trigger.pad_ref, owned) ||
			owned.action != row.trigger.action || owned.target != row.trigger.target_id ||
			!graphRemoveRows(graph, "nodes", owned.nodes) || !graphRemoveRows(graph, "links", owned.links) ||
			!graphRemoveRows(graph, "edges", owned.edges)) {
		setError(err, err_cap, "the trigger shares graph references with other rules");
		return 0;
	}
	const TriggerIds ids = triggerIds(row.trigger.volume_id);
	doc->graphNodeIds.erase(ids.source);
	doc->graphNodeIds.erase(ids.enter);
	doc->graphNodeIds.erase(owned.action == FORGE_LEVEL_TRIGGER_GIVE_PICKUP ? ids.give : ids.open);
	doc->tables[FORGE_LEVEL_MEMBER_VOLUMES].remove(row.volumeRow);
	doc->triggers.erase(doc->triggers.begin() + index);
	for (auto &trigger : doc->triggers) {
		if (trigger.volumeRow > row.volumeRow) --trigger.volumeRow;
	}
	for (auto &padRow : doc->padVolume) {
		if (padRow > static_cast<int>(row.volumeRow)) --padRow;
	}
	graphTable.setWhole(graph);
	graphSyncCounts(*doc);
	return 1;
}

s32 forgeLevelDocReplaceLogic(forge_level_doc_t *doc, const char *json, size_t size, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!doc || !json || !size || size > FORGE_LEVEL_CHECKPOINT_MAX_BYTES) {
		setError(err, err_cap, "give a complete level graph within the draft size limit");
		return 0;
	}
	try {
		Table &table = doc->tables[FORGE_LEVEL_MEMBER_LEVEL_GRAPH];
		const std::string &current = table.text();
		std::string candidate(json, size);
		if (candidate == current) return 0;
		LogicGraph before, after;
		if (!graphLogicRead(current, before) || !graphLogicRead(candidate, after)) {
			setError(err, err_cap, "level logic needs one native graph with unique IDs and valid node and edge arrays");
			return 0;
		}
		if (!graphLogicMetadataMatches(before, after)) {
			setError(err, err_cap, "keep level data, structural links and native pickup and door rules as opened");
			return 0;
		}
		if (before.root["nodes"].dump() == after.root["nodes"].dump()
				&& (before.hasEdges ? before.root["edges"].dump() : "[]")
				== (after.hasEdges ? after.root["edges"].dump() : "[]")) return 0;
		std::string graph = current;
		if (!graphLogicSplice(graph, before, candidate, after) || graph.size() > FORGE_LEVEL_CHECKPOINT_MAX_BYTES) {
			setError(err, err_cap, "level logic could not fit the existing graph");
			return 0;
		}
		if (countText(graph, kVolumeNodeKind) != graphTableRows(*doc, 1)) {
			setError(err, err_cap, "logic must retain one source node per native volume row");
			return 0;
		}
		std::unique_ptr<mission_graph_program_t, decltype(&missionGraphProgramFree)> program(
			missionGraphProgramCompileLevel(graph.data(), graph.size(), missionGraphModuleKnown, err, err_cap),
			missionGraphProgramFree);
		if (!program || !graphLogicVolumesValid(*doc, program.get(), err, err_cap)) return 0;
		for (const auto &[id, node] : after.editable) {
			const auto old = before.editable.find(id);
			if (old != before.editable.end() && node->dump() == old->second->dump()) continue;
			if (!graphLogicNodeValid(*doc, *node)) {
				setError(err, err_cap, "logic node \"%s\" needs a supported arena operation and valid typed operands", id.c_str());
				return 0;
			}
		}
		for (const auto &[text, edge] : after.editableEdges) {
			if (!before.editableEdges.count(text) && !graphLogicScalarFields(*edge)) {
				setError(err, err_cap, "authored execution edges use scalar fields");
				return 0;
			}
		}
		TriggerTracking tracking = readTriggers(*doc, after.root, doc->tables[FORGE_LEVEL_MEMBER_VOLUMES].rows, doc->padVolume);
		table.setWhole(graph);
		doc->graphNodeIds = std::move(after.ids);
		doc->triggers = std::move(tracking.rows);
		doc->logicZones = std::move(tracking.zones);
		doc->triggerVolumeIds.merge(tracking.ownedVolumes);
		doc->logicVolumePads = std::move(tracking.otherVolumePads);
		return 1;
	} catch (const std::bad_alloc &) {
		setError(err, err_cap, "out of memory preparing level logic");
		return 0;
	}
}

s32 forgeLevelDocLogicZoneCount(const forge_level_doc_t *doc)
{
	return doc ? static_cast<s32>(doc->logicZones.size()) : 0;
}

s32 forgeLevelDocGetLogicZone(const forge_level_doc_t *doc, s32 index, forge_level_logic_zone_t *out)
{
	if (!out || index < 0 || index >= forgeLevelDocLogicZoneCount(doc)) return 0;
	*out = doc->logicZones[index];
	return 1;
}

static const forge_level_logic_zone_t *findLogicZone(const forge_level_doc_t *doc, const char *volume_id)
{
	if (!doc || !volume_id) return nullptr;
	const auto found = std::find_if(doc->logicZones.begin(), doc->logicZones.end(), [&](const auto &zone) {
		return strcmp(zone.volume_id, volume_id) == 0;
	});
	return found == doc->logicZones.end() ? nullptr : &*found;
}

struct LogicZoneBinding {
	size_t volumeRow = 0;
	size_t sourceNode = 0;
	std::vector<size_t> links;
	bool referenced = false;
};

static bool logicZoneBinding(const forge_level_doc &doc, const forge_level_logic_zone_t &zone,
	const std::string &graph, LogicZoneBinding &binding)
{
	const std::string volume = zone.volume_id;
	const std::string source = triggerIds(volume).source;
	const auto &volumes = doc.tables[FORGE_LEVEL_MEMBER_VOLUMES].rows;
	bool bound = false;
	for (size_t i = 0; i < volumes.size(); ++i) {
		Json row;
		if (!modAssetJsonReadValue(volumes[i].data(), volumes[i].size(), row)
				|| stringOf(row, "volume_id") != volume) continue;
		if (bound || stringOf(row, "pad_ref") != zone.pad_ref) return false;
		binding.volumeRow = i;
		bound = true;
	}
	if (!bound) return false;
	Json root;
	if (!modAssetJsonReadValue(graph.data(), graph.size(), root) || !root.is_object()
			|| !root.contains("nodes") || !root["nodes"].is_array()) return false;
	const auto &nodes = root["nodes"].get<crude_json::array>();
	bound = false;
	for (size_t i = 0; i < nodes.size(); ++i) {
		const auto &node = nodes[i];
		if (stringOf(node, "id") == source) {
			if (bound || stringOf(node, "kind") != "scenario.trigger.volume.source"
					|| stringOf(node, "volume") != volume || stringOf(node, "pad") != zone.pad_ref
					|| stringOf(node, "table") != "volumes.json") return false;
			binding.sourceNode = i;
			bound = true;
		} else if (containsReference(node, volume) || containsReference(node, source)) {
			binding.referenced = true;
		}
	}
	for (const char *key : {"links", "edges"}) {
		if (!root.contains(key)) continue;
		if (!root[key].is_array()) return false;
		const auto &rows = root[key].get<crude_json::array>();
		for (size_t i = 0; i < rows.size(); ++i) {
			if (containsReference(rows[i], volume)) binding.referenced = true;
			if (!containsReference(rows[i], source)) continue;
			const std::string from = stringOf(rows[i], "from");
			if (strcmp(key, "links") != 0 || !binding.links.empty() || from.empty() || from == source
					|| stringOf(rows[i], "to") != source || rows[i].contains("input")) return false;
			binding.links.push_back(i);
		}
	}
	return bound;
}

static bool commitLogicZone(forge_level_doc &doc, std::string &graph, Table &&volumes, std::vector<int> &&padVolumes,
	const char *zone_id, bool present, char *err, size_t err_cap)
{
	const long count = static_cast<long>(volumes.rows.size());
	Json root;
	if ((doc.graphCountTrue[1] && graphCount(graph, "volumes") != count && !graphSetCount(graph, "volumes", count))
			|| countText(graph, kVolumeNodeKind) != count || graph.size() > FORGE_LEVEL_CHECKPOINT_MAX_BYTES
			|| !modAssetJsonReadValue(graph.data(), graph.size(), root) || !root.is_object()
			|| !root.contains("nodes") || !root["nodes"].is_array()) {
		setError(err, err_cap, "the zone and its native graph binding could not be written together");
		return false;
	}
	TriggerTracking tracking = readTriggers(doc, root, volumes.rows, padVolumes);
	const bool found = std::any_of(tracking.zones.begin(), tracking.zones.end(), [&](const auto &zone) {
		return strcmp(zone.volume_id, zone_id) == 0;
	});
	if (found != present) {
		setError(err, err_cap, "the native zone could not be reopened with its anchor and bounds");
		return false;
	}
	std::set<std::string> ids;
	for (const auto &node : root["nodes"].get<crude_json::array>()) {
		std::string id = stringOf(node, "id");
		if (!id.empty()) ids.insert(std::move(id));
	}
	doc.tables[FORGE_LEVEL_MEMBER_LEVEL_GRAPH].setWhole(graph);
	doc.tables[FORGE_LEVEL_MEMBER_VOLUMES] = std::move(volumes);
	doc.padVolume = std::move(padVolumes);
	doc.graphNodeIds = std::move(ids);
	doc.triggers = std::move(tracking.rows);
	doc.logicZones = std::move(tracking.zones);
	doc.triggerVolumeIds.merge(tracking.ownedVolumes);
	doc.logicVolumePads = std::move(tracking.otherVolumePads);
	return true;
}

s32 forgeLevelDocAddLogicZone(forge_level_doc_t *doc, const char *pad_ref, const f32 half_size[3],
	char *volume_id, size_t volume_cap, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	try {
		const int index = doc && pad_ref ? findPad(*doc, pad_ref) : -1;
		if (index < 0 || doc->padVolume[index] < 0 || !triggerAnchorRoomValid(doc->pads[index].room_ref)) {
			setError(err, err_cap, "choose an existing pad with native bounds and a valid room reference");
			return 0;
		}
		const auto &pad = doc->pads[index];
		f32 lo[3], hi[3];
		if (!triggerBounds(pad, half_size, lo, hi, err, err_cap)) return 0;
		std::string id;
		TriggerIds ids;
		const long number = nextTriggerVolume(*doc, id, ids);
		if (number < 0 || (volume_id && id.size() >= volume_cap) || (!volume_id && volume_cap)) {
			setError(err, err_cap, "no available zone ID fits this request");
			return 0;
		}
		std::string graph = doc->tables[FORGE_LEVEL_MEMBER_LEVEL_GRAPH].text();
		if (!graphAddVolumeNode(*doc, ids.source, id, pad.pad_ref, graph)) {
			setError(err, err_cap, "the level graph has no usable volume binding");
			return 0;
		}
		Table volumes = doc->tables[FORGE_LEVEL_MEMBER_VOLUMES];
		auto padVolumes = doc->padVolume;
		std::set<std::string> admitted{id};
		volumes.append(triggerVolumeRow(id, pad, lo, hi));
		if (!commitLogicZone(*doc, graph, std::move(volumes), std::move(padVolumes), id.c_str(), true, err, err_cap)) return 0;
		doc->volumeIds.merge(admitted);
		doc->nextTriggerVolume = number - 1;
		if (volume_id) memcpy(volume_id, id.c_str(), id.size() + 1);
		return 1;
	} catch (const std::bad_alloc &) {
		setError(err, err_cap, "out of memory preparing the zone");
		return 0;
	}
}

s32 forgeLevelDocUpdateLogicZone(forge_level_doc_t *doc, const char *volume_id, const char *pad_ref,
	const f32 half_size[3], char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	try {
		const auto *zone = findLogicZone(doc, volume_id);
		const int index = zone && pad_ref ? findPad(*doc, pad_ref) : -1;
		if (index < 0 || doc->padVolume[index] < 0) {
			setError(err, err_cap, "choose an editable zone and an existing pad with native bounds");
			return 0;
		}
		const auto &pad = doc->pads[index];
		const bool reanchor = strcmp(pad.pad_ref, zone->pad_ref) != 0;
		f32 lo[3], hi[3];
		if (reanchor && !triggerAnchorRoomValid(pad.room_ref)) {
			setError(err, err_cap, "the destination pad has no valid room reference");
			return 0;
		}
		if (!triggerBounds(pad, half_size, lo, hi, err, err_cap)) return 0;
		if (!reanchor && std::equal(half_size, half_size + 3, zone->half_size)) return 0;
		std::string graph = doc->tables[FORGE_LEVEL_MEMBER_LEVEL_GRAPH].text();
		LogicZoneBinding binding;
		if (!logicZoneBinding(*doc, *zone, graph, binding)) {
			setError(err, err_cap, "the zone's source node is shared or has an unsupported binding");
			return 0;
		}
		Table volumes = doc->tables[FORGE_LEVEL_MEMBER_VOLUMES];
		auto padVolumes = doc->padVolume;
		std::string &row = volumes.rows[binding.volumeRow];
		if ((reanchor && (!replaceValue(row, "pad_ref", quoted(pad.pad_ref)) || !replaceValue(row, "room_ref", quoted(pad.room_ref))
				|| !replaceTriggerGraphField(graph, binding.sourceNode, "pad", pad.pad_ref)))
				|| !replaceValue(row, "min", vectorText(lo)) || !replaceValue(row, "max", vectorText(hi))) {
			setError(err, err_cap, "the zone bounds or anchor could not be rewritten");
			return 0;
		}
		volumes.touch();
		const size_t primary = static_cast<size_t>(padVolumes[index]);
		if (reanchor && binding.volumeRow < primary) {
			/* Native pad lookups use the first volume of each pad. */
			std::rotate(volumes.rows.begin() + binding.volumeRow, volumes.rows.begin() + binding.volumeRow + 1,
				volumes.rows.begin() + primary + 1);
			for (auto &padRow : padVolumes) {
				if (padRow > static_cast<int>(binding.volumeRow) && padRow <= static_cast<int>(primary)) --padRow;
			}
		}
		return commitLogicZone(*doc, graph, std::move(volumes), std::move(padVolumes), zone->volume_id, true, err, err_cap);
	} catch (const std::bad_alloc &) {
		setError(err, err_cap, "out of memory preparing the zone");
		return 0;
	}
}

s32 forgeLevelDocRemoveLogicZone(forge_level_doc_t *doc, const char *volume_id, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	try {
		const auto *zone = findLogicZone(doc, volume_id);
		if (!zone) {
			setError(err, err_cap, "select an editable zone to remove");
			return 0;
		}
		std::string graph = doc->tables[FORGE_LEVEL_MEMBER_LEVEL_GRAPH].text();
		LogicZoneBinding binding;
		if (!logicZoneBinding(*doc, *zone, graph, binding) || binding.referenced
				|| !graphRemoveRows(graph, "nodes", {binding.sourceNode}) || !graphRemoveRows(graph, "links", binding.links)) {
			setError(err, err_cap, "remove the zone's Level Logic references before removing the zone");
			return 0;
		}
		Table volumes = doc->tables[FORGE_LEVEL_MEMBER_VOLUMES];
		auto padVolumes = doc->padVolume;
		volumes.remove(binding.volumeRow);
		for (auto &padRow : padVolumes) {
			if (padRow > static_cast<int>(binding.volumeRow)) --padRow;
		}
		return commitLogicZone(*doc, graph, std::move(volumes), std::move(padVolumes), zone->volume_id, false, err, err_cap);
	} catch (const std::bad_alloc &) {
		setError(err, err_cap, "out of memory preparing the zone");
		return 0;
	}
}

s32 forgeLevelDocAddLogicZoneEvent(forge_level_doc_t *doc, const char *volume_id, s32 when,
	char *event_id, size_t event_cap, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	try {
		if (!findLogicZone(doc, volume_id) || (when != FORGE_LEVEL_LOGIC_ZONE_ENTER && when != FORGE_LEVEL_LOGIC_ZONE_LEAVE)) {
			setError(err, err_cap, "select a zone and an enter or leave event");
			return 0;
		}
		const bool enter = when == FORGE_LEVEL_LOGIC_ZONE_ENTER;
		const std::string id = std::string("grid.zone.") + volume_id + (enter ? ".enter" : ".leave");
		if (doc->graphNodeIds.count(id) || (event_id && id.size() >= event_cap) || (!event_id && event_cap)) {
			setError(err, err_cap, "this zone event already exists or the event ID does not fit");
			return 0;
		}
		std::string graph = doc->tables[FORGE_LEVEL_MEMBER_LEVEL_GRAPH].text();
		const std::string node = "{ \"id\": " + quoted(id) + ", \"kind\": " + quoted(enter ? kTriggerEvent : "event.volume.left")
			+ ", \"volume\": " + quoted(volume_id) + " }";
		if (!graphInsert(graph, "nodes", "", node)) {
			setError(err, err_cap, "the native graph has no node array for the event");
			return 0;
		}
		if (!forgeLevelDocReplaceLogic(doc, graph.data(), graph.size(), err, err_cap)) return 0;
		if (event_id) memcpy(event_id, id.c_str(), id.size() + 1);
		return 1;
	} catch (const std::bad_alloc &) {
		setError(err, err_cap, "out of memory preparing the zone event");
		return 0;
	}
}

s32 forgeLevelDocDoorTriggerCount(const forge_level_doc_t *doc)
{
	if (!doc) return 0;
	return static_cast<s32>(std::count_if(doc->triggers.begin(), doc->triggers.end(), [](const auto &row) {
		return row.trigger.action == FORGE_LEVEL_TRIGGER_OPEN_DOOR;
	}));
}

static s32 doorTriggerIndex(const forge_level_doc_t *doc, s32 index)
{
	if (index < 0) return -1;
	for (s32 i = 0; i < forgeLevelDocTriggerCount(doc); ++i) {
		if (doc->triggers[i].trigger.action == FORGE_LEVEL_TRIGGER_OPEN_DOOR && index-- == 0) return i;
	}
	return -1;
}

s32 forgeLevelDocGetDoorTrigger(const forge_level_doc_t *doc, s32 index, forge_level_door_trigger_t *out)
{
	const s32 found = doorTriggerIndex(doc, index);
	if (!out || found < 0) return 0;
	const auto &trigger = doc->triggers[found].trigger;
	copyOut(out->volume_id, trigger.volume_id);
	copyOut(out->pad_ref, trigger.pad_ref);
	copyOut(out->door_id, trigger.target_id);
	return 1;
}

s32 forgeLevelDocAddDoorTrigger(forge_level_doc_t *doc, const char *pad_ref, const char *door_id,
	const f32 half_size[3], char *err, size_t err_cap)
{
	return forgeLevelDocAddTrigger(doc, FORGE_LEVEL_TRIGGER_OPEN_DOOR, pad_ref, door_id, half_size, err, err_cap);
}

s32 forgeLevelDocRemoveDoorTrigger(forge_level_doc_t *doc, s32 index, char *err, size_t err_cap)
{
	return forgeLevelDocRemoveTrigger(doc, doorTriggerIndex(doc, index), err, err_cap);
}

static bool levelMemberPresent(forge_level_doc_t *doc, s32 member)
{
	if (!forgeLevelDocMemberOptional(member) || doc->sourcePresent[member]) return true;
	auto &table = doc->tables[member];
	return table.changed && table.text() != table.original;
}

s32 forgeLevelDocWriteCheckpoint(forge_level_doc_t *doc, const char *source_sha256,
		FILE *stream, char *err, size_t err_cap)
{
	if (!doc || !stream || !source_sha256 || !source_sha256[0]) {
		setError(err, err_cap, "an open level and its source digest are required");
		return 0;
	}
	try {
		crude_json::object members;
		size_t bytes = 0;
		for (s32 i = 0; i < FORGE_LEVEL_MEMBER_COUNT; i++) {
			if (!levelMemberPresent(doc, i)) continue;
			const std::string &text = doc->tables[i].text();
			if (text.size() > FORGE_LEVEL_CHECKPOINT_MAX_BYTES - bytes) {
				setError(err, err_cap, "the level exceeds the checkpoint size limit");
				return 0;
			}
			bytes += text.size();
			members.emplace(kMemberNames[i], text);
		}
		const Json root(crude_json::object{
			{ "schema", "pd2.forge.recovery.v1" },
			{ "source_sha256", source_sha256 },
			{ "members", std::move(members) }
		});
		const std::string json = root.dump();
		if (json.size() > FORGE_LEVEL_CHECKPOINT_MAX_BYTES) {
			setError(err, err_cap, "the level exceeds the checkpoint size limit");
			return 0;
		}
		if (fwrite(json.data(), 1, json.size(), stream) != json.size()) {
			setError(err, err_cap, "the checkpoint could not be written");
			return 0;
		}
	} catch (const std::bad_alloc &) {
		setError(err, err_cap, "out of memory while writing the checkpoint");
		return 0;
	}
	return 1;
}

forge_level_doc_t *forgeLevelDocReadCheckpoint(const char *json, size_t size, const char *source_sha256,
		const forge_level_doc_t *opened, char *err, size_t err_cap)
{
	if (!opened || !source_sha256 || !source_sha256[0] || !json || !size
			|| size > FORGE_LEVEL_CHECKPOINT_MAX_BYTES) {
		setError(err, err_cap, "the checkpoint is missing, too large, or has no open level");
		return nullptr;
	}
	try {
		Json root;
		if (!modAssetJsonReadValue(json, size, root) || !root.is_object()
				|| stringOf(root, "schema") != "pd2.forge.recovery.v1"
				|| !root.contains("members") || !root["members"].is_object()) {
			setError(err, err_cap, "invalid level checkpoint");
			return nullptr;
		}
		if (stringOf(root, "source_sha256") != source_sha256) {
			setError(err, err_cap, "the checkpoint was made from a different level revision");
			return nullptr;
		}
		const Json &members = root["members"];
		forge_level_texts_t texts = {};
		for (s32 i = 0; i < FORGE_LEVEL_MEMBER_COUNT; i++) {
			if (forgeLevelDocMemberOptional(i) && !members.contains(kMemberNames[i])) {
				if ((i == FORGE_LEVEL_MEMBER_COVERS || i == FORGE_LEVEL_MEMBER_PATHS || i == FORGE_LEVEL_MEMBER_ROOMS)
						&& opened->sourcePresent[i]) {
					const std::string &source = opened->tables[i].original;
					texts.text[i] = source.data();
					texts.size[i] = source.size();
				}
				continue;
			}
			if (!hasString(members, kMemberNames[i])) {
				setError(err, err_cap, "the checkpoint has no %s", kMemberNames[i]);
				return nullptr;
			}
			const std::string &text = members[kMemberNames[i]].get<std::string>();
			texts.text[i] = text.data();
			texts.size[i] = text.size();
		}
		forge_level_doc_t *doc = forgeLevelDocRead(&texts, err, err_cap);
		if (!doc) return nullptr;
		for (s32 i = 0; i < FORGE_LEVEL_MEMBER_COUNT; i++) {
			doc->recoveredMembers[i] = doc->sourcePresent[i] != opened->sourcePresent[i]
				|| doc->tables[i].original != opened->tables[i].original;
		}
		return doc;
	} catch (const std::bad_alloc &) {
		setError(err, err_cap, "out of memory while reading the checkpoint");
		return nullptr;
	}
}

s32 forgeLevelTurnLook(const f32 look[3], f32 degrees, f32 out[3])
{
	if (!look || !out || !std::isfinite(look[0]) || !std::isfinite(look[2]) || !std::isfinite(degrees)) {
		return 0;
	}
	const double length = std::sqrt(static_cast<double>(look[0]) * look[0] + static_cast<double>(look[2]) * look[2]);
	if (length < 1e-6) {
		return 0;
	}
	const double x = look[0] / length;
	const double z = look[2] / length;
	const double radians = degrees * 3.14159265358979323846 / 180.0;
	const double c = std::cos(radians);
	const double s = std::sin(radians);
	/* The right of (x, 0, z) is (-z, 0, x): the turned look is
	 * cos * look + sin * right. */
	out[0] = static_cast<f32>(x * c - z * s);
	out[1] = 0.0f;
	out[2] = static_cast<f32>(z * c + x * s);
	return 1;
}

forge_level_doc_t *forgeLevelDocClone(const forge_level_doc_t *doc)
{
	/* Every part of a document is a value (texts, lists, maps), so a copy
	 * shares nothing with the document it was made from. */
	return doc ? new (std::nothrow) forge_level_doc(*doc) : nullptr;
}

s32 forgeLevelDocPadCount(const forge_level_doc_t *doc)
{
	return doc ? static_cast<s32>(doc->pads.size()) : 0;
}

s32 forgeLevelDocSpawnCount(const forge_level_doc_t *doc)
{
	return doc ? static_cast<s32>(doc->spawns.size()) : 0;
}

s32 forgeLevelDocObjectCount(const forge_level_doc_t *doc)
{
	return doc ? static_cast<s32>(doc->objects.size()) : 0;
}

s32 forgeLevelDocVolumeCount(const forge_level_doc_t *doc)
{
	return doc ? static_cast<s32>(doc->tables[FORGE_LEVEL_MEMBER_VOLUMES].rows.size()) : 0;
}

s32 forgeLevelDocFieldRowCount(const forge_level_doc_t *doc)
{
	return doc ? static_cast<s32>(doc->fields.size()) : 0;
}

s32 forgeLevelDocGetPad(const forge_level_doc_t *doc, s32 index, forge_level_pad_t *out)
{
	if (!doc || !out || index < 0 || static_cast<size_t>(index) >= doc->pads.size()) return 0;
	*out = doc->pads[static_cast<size_t>(index)];
	return 1;
}

s32 forgeLevelDocGetSpawn(const forge_level_doc_t *doc, s32 index, forge_level_spawn_t *out)
{
	if (!doc || !out || index < 0 || static_cast<size_t>(index) >= doc->spawns.size()) return 0;
	*out = doc->spawns[static_cast<size_t>(index)];
	return 1;
}

s32 forgeLevelDocGetObject(const forge_level_doc_t *doc, s32 index, forge_level_object_t *out)
{
	if (!doc || !out || index < 0 || static_cast<size_t>(index) >= doc->objects.size()) return 0;
	*out = doc->objects[static_cast<size_t>(index)];
	return 1;
}

s32 forgeLevelDocObjectExtraScale(const forge_level_doc_t *doc, s32 index)
{
	if (!doc || index < 0 || static_cast<size_t>(index) >= doc->objects.size()) return 256;
	const long row = doc->objectCatalog[static_cast<size_t>(index)].scaleRow;
	if (row < 0 || static_cast<size_t>(row) >= doc->fields.size()) return 256;
	const long scale = wholeNumber(doc->fields[static_cast<size_t>(row)].value);
	return scale > 0 && scale <= 0xffff ? static_cast<s32>(scale) : 256;
}

s32 forgeLevelDocFindPad(const forge_level_doc_t *doc, const char *pad_ref, forge_level_pad_t *out)
{
	const int index = doc && pad_ref ? findPad(*doc, pad_ref) : -1;
	if (index < 0) return 0;
	if (out) *out = doc->pads[static_cast<size_t>(index)];
	return 1;
}

s32 forgeLevelDocSetPadWalkDirect(forge_level_doc_t *doc, const char *pad_ref, s32 enabled,
	char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	const int index = doc && pad_ref ? findPad(*doc, pad_ref) : -1;
	if (index < 0) {
		setError(err, err_cap, "select a valid waypoint pad");
		return 0;
	}
	if (enabled != 0 && enabled != 1) {
		setError(err, err_cap, "the waypoint setting must be on or off");
		return 0;
	}
	const size_t padIndex = static_cast<size_t>(index);
	Table &table = doc->tables[FORGE_LEVEL_MEMBER_PADS];
	std::string row = table.rows[padIndex];
	Json parsed;
	unsigned long flags;
	if (!modAssetJsonReadValue(row.data(), row.size(), parsed) || !readPadFlags(parsed, flags)) {
		setError(err, err_cap, "%s has invalid pad flags", pad_ref);
		return 0;
	}
	const unsigned long updated = enabled ? flags | kPadWalkDirect : flags & ~kPadWalkDirect;
	if (updated == flags) return 0;

	if (parsed.contains("flags")) {
		const std::string value = parsed["flags"].is_number() ? std::to_string(updated) : quoted(hexWordText(updated));
		if (!replaceValue(row, "flags", value)) {
			setError(err, err_cap, "%s could not be rewritten", pad_ref);
			return 0;
		}
	} else {
		row.insert(1, " \"flags\": " + quoted(hexWordText(updated)) + ",");
	}
	table.rows[padIndex] = std::move(row);
	table.touch();
	doc->pads[padIndex].walk_direct = enabled;
	return 1;
}

s32 forgeLevelDocConnectPads(forge_level_doc_t *doc, const char *first, const char *second,
	char *out_path_id, size_t out_cap, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (out_path_id && out_cap) out_path_id[0] = '\0';
	constexpr int member = FORGE_LEVEL_MEMBER_PATHS;
	Table edited;
	Json root;
	if (!readEditableRouteTable(doc, first, second, edited, root, err, err_cap)) return 0;
	bool used[UINT8_MAX + 1] = {};
	bool hasRouteEndpoint = false;
	const auto &rows = root["rows"].get<crude_json::array>();
	for (const auto &row : rows) {
		RoutePath path = {};
		if (!readRoutePath(*doc, row, path) || used[path.id]) {
			setError(err, err_cap, "the existing native path table has an invalid row or repeated id");
			return 0;
		}
		used[path.id] = true;
		if (routeJoinsPads(path, first, second)) {
			setError(err, err_cap, "%s and %s are already connected", first, second);
			return 0;
		}
		for (size_t i = 1; i < path.pads.size(); ++i) {
			const auto &from = path.pads[i - 1];
			const auto &to = path.pads[i];
			if (from == to) continue;
			hasRouteEndpoint = hasRouteEndpoint || from == first || from == second || to == first || to == second;
		}
	}
	if (!hasRouteEndpoint) {
		setError(err, err_cap, "one of the pads needs a connection to an existing route");
		return 0;
	}
	const bool *available = std::find(std::begin(used), std::end(used), false);
	if (available == std::end(used)) {
		setError(err, err_cap, "the level has reached its native path limit");
		return 0;
	}
	const std::string id = numbered("path_", available - used);
	if (out_path_id && id.size() >= out_cap) {
		setError(err, err_cap, "the path id does not fit");
		return 0;
	}
	auto &graphTable = doc->tables[FORGE_LEVEL_MEMBER_LEVEL_GRAPH];
	std::string graph = graphTable.text();
	const long before = static_cast<long>(rows.size());
	if (graphCount(graph, "paths") == before) {
		if (!graphSetCount(graph, "paths", before + 1)) {
			setError(err, err_cap, "the level graph's path count could not be updated");
			return 0;
		}
		graphTable.setWhole(graph);
	}
	edited.append("{ \"path_ref\": " + quoted(id) + ", \"flags\": 0, \"pads\": ["
		+ quoted(first) + ", " + quoted(second) + "], " + quoted(kGridRouteLinkField) + ": "
		+ quoted(kGridRouteLinkKind) + " }");
	doc->tables[member].setWhole(edited.text());
	doc->routePads.insert(first);
	doc->routePads.insert(second);
	if (out_path_id) snprintf(out_path_id, out_cap, "%s", id.c_str());
	return 1;
}

s32 forgeLevelDocCanDisconnectPads(const forge_level_doc_t *doc, const char *first, const char *second)
{
	try {
		Table edited;
		Json root;
		GridRouteLinkLocation link;
		return readEditableRouteTable(doc, first, second, edited, root, nullptr, 0)
			&& findGridRouteLink(*doc, root, first, second, link, nullptr, nullptr, 0);
	} catch (const std::bad_alloc &) {
		return 0;
	}
}

s32 forgeLevelDocDisconnectPads(forge_level_doc_t *doc, const char *first, const char *second,
	char *out_path_id, size_t out_cap, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (out_path_id && out_cap) out_path_id[0] = '\0';
	try {
		Table edited;
		Json root;
		GridRouteLinkLocation link;
		std::set<std::string> survivingPads;
		if (!readEditableRouteTable(doc, first, second, edited, root, err, err_cap)
				|| !findGridRouteLink(*doc, root, first, second, link, &survivingPads, err, err_cap)) return 0;
		const std::string id = numbered("path_", link.id);
		if (out_path_id && id.size() >= out_cap) {
			setError(err, err_cap, "the path id does not fit");
			return 0;
		}

		/* Stage both member edits before publishing a new document state. */
		const long before = static_cast<long>(edited.rows.size());
		edited.remove(link.row);
		auto &pathsTable = doc->tables[FORGE_LEVEL_MEMBER_PATHS];
		Table nextPaths = pathsTable;
		nextPaths.setWhole(edited.text());
		auto &graphTable = doc->tables[FORGE_LEVEL_MEMBER_LEVEL_GRAPH];
		std::string graph = graphTable.text();
		const bool updateGraph = graphCount(graph, "paths") == before;
		Table nextGraph;
		if (updateGraph) {
			if (!graphSetCount(graph, "paths", before - 1)) {
				setError(err, err_cap, "the level graph's path count could not be updated");
				return 0;
			}
			nextGraph = graphTable;
			nextGraph.setWhole(graph);
		}

		pathsTable = std::move(nextPaths);
		if (updateGraph) graphTable = std::move(nextGraph);
		doc->routePads = std::move(survivingPads);
		doc->routePadsUnknown = false;
		if (out_path_id) snprintf(out_path_id, out_cap, "%s", id.c_str());
		return 1;
	} catch (const std::bad_alloc &) {
		setError(err, err_cap, "out of memory editing the native route");
		return 0;
	}
}

s32 forgeLevelDocCoverCount(const forge_level_doc_t *doc)
{
	return doc ? static_cast<s32>(doc->covers.size()) : 0;
}

s32 forgeLevelDocGetCover(const forge_level_doc_t *doc, s32 index, forge_level_cover_t *out)
{
	if (!out || index < 0 || index >= forgeLevelDocCoverCount(doc)) return 0;
	*out = doc->covers[index].cover;
	return 1;
}

s32 forgeLevelDocMoveCoverToPad(forge_level_doc_t *doc, const char *cover_ref, const char *pad_ref,
	char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!doc || !cover_ref || doc->covers.empty()) {
		setError(err, err_cap, "select an editable authored cover");
		return 0;
	}
	const auto cover = std::find_if(doc->covers.begin(), doc->covers.end(), [cover_ref](const auto &entry) {
		return strcmp(entry.cover.cover_ref, cover_ref) == 0;
	});
	if (cover == doc->covers.end()) {
		setError(err, err_cap, "select an editable authored cover");
		return 0;
	}
	const int padIndex = pad_ref ? findPad(*doc, pad_ref) : -1;
	if (padIndex < 0 || !finite3(doc->pads[padIndex].position)) {
		setError(err, err_cap, "select a valid cover pad");
		return 0;
	}
	const auto &pad = doc->pads[padIndex];
	if (std::equal(pad.position, pad.position + 3, cover->cover.position)) {
		setError(err, err_cap, "the cover is already at that position");
		return 0;
	}

	/* Copy the source value so the cover retains the destination pad's precision. */
	const auto &padRow = doc->tables[FORGE_LEVEL_MEMBER_PADS].rows[padIndex];
	std::vector<Member> fields;
	if (!objectMembers(padRow, 0, fields)) {
		setError(err, err_cap, "the pad position could not be read");
		return 0;
	}
	const auto position = std::find_if(fields.begin(), fields.end(), [](const auto &field) {
		return field.key == "position";
	});
	if (position == fields.end()) {
		setError(err, err_cap, "the pad position could not be read");
		return 0;
	}
	const auto &value = position->value;
	auto &table = doc->tables[FORGE_LEVEL_MEMBER_COVERS];
	std::string row = table.rows[cover->sourceRow];
	if (!replaceValue(row, "position", padRow.substr(value.begin, value.end - value.begin))) {
		setError(err, err_cap, "the native cover position could not be rewritten");
		return 0;
	}
	table.rows[cover->sourceRow] = std::move(row);
	table.touch();
	std::copy(pad.position, pad.position + 3, cover->cover.position);
	return 1;
}

s32 forgeLevelDocAddCoverAtPad(forge_level_doc_t *doc, const char *pad_ref,
	char *out_cover_id, size_t out_cap, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (out_cover_id && out_cap) out_cover_id[0] = '\0';
	constexpr int member = FORGE_LEVEL_MEMBER_COVERS;
	if (!doc || !doc->sourcePresent[member]) {
		setError(err, err_cap, "open a level with its native cover table");
		return 0;
	}
	forge_level_pad_t pad;
	if (!forgeLevelDocFindPad(doc, pad_ref, &pad)) {
		setError(err, err_cap, "select a valid cover pad");
		return 0;
	}
	f32 look[3];
	if (!finite3(pad.position) || !forgeLevelTurnLook(pad.look, 0.0f, look)) {
		setError(err, err_cap, "%s needs a finite position and a horizontal facing", pad.pad_ref);
		return 0;
	}
	auto &table = doc->tables[member];
	std::string source = table.text();
	Table edited;
	Json parsed;
	std::string error;
	if (!readTable(member, source.data(), source.size(), edited, parsed, error)
			|| !validateCoverRows(parsed, error)) {
		setError(err, err_cap, "%s", error.c_str());
		return 0;
	}
	if (edited.rows.size() >= kMaxForgeCoverRows) {
		setError(err, err_cap, "the level has reached its 256-cover limit");
		return 0;
	}
	const std::string id = numbered("cover_", static_cast<long>(edited.rows.size()));
	if (out_cover_id && id.size() >= out_cap) {
		setError(err, err_cap, "the cover id does not fit");
		return 0;
	}
	const std::string row = "{ \"cover_ref\": " + quoted(id) + ", \"flags\": 0, \"position\": "
		+ vectorText(pad.position) + ", \"look\": " + vectorText(look) + " }";
	Json authored;
	forge_level_cover_t cover = {};
	if (!modAssetJsonReadValue(row.data(), row.size(), authored)
			|| !vectorOf(authored["position"], cover.position)) {
		setError(err, err_cap, "%s is outside the cover coordinate range", pad.pad_ref);
		return 0;
	}
	copyText(cover.cover_ref, id);
	if (edited.rows.empty() && (!parsed.contains("generate_routes")
			|| !parsed["generate_routes"].get<crude_json::boolean>())) {
		/* Preserve the routes previously derived from pads and paths. */
		size_t begin = 0;
		skipSpace(source, begin);
		if (!parsed.contains("generate_routes")) {
			source.insert(begin + 1, "\"generate_routes\": true, ");
		} else {
			std::vector<Member> members;
			if (!objectMembers(source, begin, members)) {
				setError(err, err_cap, "the cover settings could not be read");
				return 0;
			}
			const auto mode = std::find_if(members.begin(), members.end(), [](const Member &field) {
				if (field.key == "generate_routes") return true;
				if (field.key.find('\\') == std::string::npos) return false;
				const std::string escaped = quoted(field.key);
				Json decoded;
				return modAssetJsonReadValue(escaped.data(), escaped.size(), decoded)
					&& decoded.is_string() && decoded.get<crude_json::string>() == "generate_routes";
			});
			if (mode == members.end()) {
				setError(err, err_cap, "the cover routing rule could not be rewritten");
				return 0;
			}
			source.replace(mode->value.begin, mode->value.end - mode->value.begin, "true");
		}
		if (!readTable(member, source.data(), source.size(), edited, parsed, error)) {
			setError(err, err_cap, "%s", error.c_str());
			return 0;
		}
	}
	const size_t sourceRow = edited.rows.size();
	edited.append(row);
	doc->covers.push_back({cover, sourceRow});
	edited.original = std::move(table.original);
	table = std::move(edited);
	if (out_cover_id) snprintf(out_cover_id, out_cap, "%s", id.c_str());
	return 1;
}

s32 forgeLevelDocAddSpawn(forge_level_doc_t *doc, const f32 position[3], const f32 look[3],
	const char *team, char *out_spawn_id, size_t out_cap, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (out_spawn_id && out_cap) out_spawn_id[0] = '\0';
	if (!doc) {
		setError(err, err_cap, "no level is open");
		return 0;
	}
	const std::string teamName = team && team[0] ? team : "any";
	if (!teamOk(teamName)) {
		setError(err, err_cap, "a spawn's team is any, default or team_N; \"%s\" is none of them",
			teamName.c_str());
		return 0;
	}
	if (doc->nextSpawn > FORGE_LEVEL_MAX_SPAWN_NUMBER) {
		setError(err, err_cap, "the level has used every spawn number up to spawn_%04d",
			FORGE_LEVEL_MAX_SPAWN_NUMBER);
		return 0;
	}
	std::string error;
	const int pad = addPad(*doc, position, look, error);
	if (pad < 0) {
		setError(err, err_cap, "%s", error.c_str());
		return 0;
	}

	forge_level_spawn_t spawn = {};
	copyText(spawn.spawn_id, numbered("spawn_", doc->nextSpawn));
	copyText(spawn.pad_ref, std::string(doc->pads[static_cast<size_t>(pad)].pad_ref));
	copyText(spawn.team, teamName);
	doc->nextSpawn++;

	/* The extractor's spawn row: the pad carries the place, so position and
	 * look are null here, as they are in every base level. */
	const std::string row = std::string("{ \"spawn_id\": \"") + spawn.spawn_id + "\", \"pad_ref\": \"" +
		spawn.pad_ref + "\", \"room_ref\": \"\", \"team\": \"" + teamName + "\", \"profile\": \"" +
		doc->spawnProfile + "\", \"position\": null, \"look\": null }";
	doc->tables[FORGE_LEVEL_MEMBER_SPAWNS].append(row);
	doc->spawns.push_back(spawn);
	if (out_spawn_id && out_cap) snprintf(out_spawn_id, out_cap, "%s", spawn.spawn_id);
	return 1;
}

s32 forgeLevelDocMoveSpawn(forge_level_doc_t *doc, const char *spawn_id,
	const f32 position[3], const f32 look[3], char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	const int index = doc ? findSpawn(*doc, spawn_id) : -1;
	if (index < 0) {
		setError(err, err_cap, "the level has no spawn \"%s\"", spawn_id ? spawn_id : "");
		return 0;
	}
	forge_level_spawn_t &spawn = doc->spawns[static_cast<size_t>(index)];
	const int padIndex = findPad(*doc, spawn.pad_ref);
	const bool newPad = placementNeedsNewPad(*doc, padIndex, &spawn, nullptr);
	Table &table = doc->tables[FORGE_LEVEL_MEMBER_SPAWNS];
	std::string row, ref;
	if (newPad) {
		ref = numbered("pad_", static_cast<long>(doc->pads.size()));
		row = table.rows[static_cast<size_t>(index)];
		if (!replaceValue(row, "pad_ref", quoted(ref))) {
			setError(err, err_cap, "%s could not be rewritten", spawn.spawn_id);
			return 0;
		}
		/* A row that repeats its pad's place would now be wrong: clear it. */
		replaceValue(row, "position", "null");
		replaceValue(row, "look", "null");
	}

	std::string error;
	const int movedPad = newPad ? addPad(*doc, position, look, error)
		: movePad(*doc, padIndex, position, look, error);
	if (movedPad < 0) {
		setError(err, err_cap, "%s", error.c_str());
		return 0;
	}
	if (newPad) {
		table.rows[static_cast<size_t>(index)] = std::move(row);
		table.touch();
		copyOut(spawn.pad_ref, ref.c_str());
	}
	return 1;
}

/* The object `record_id` when it can be given a place, or NULL with the reason. */
static forge_level_object_t *movableObject(forge_level_doc_t *doc, const char *record_id,
	int *out_index, char *err, size_t err_cap)
{
	const int index = doc ? findObject(*doc, record_id) : -1;
	if (index < 0) {
		setError(err, err_cap, "the level has no object \"%s\"", record_id ? record_id : "");
		return nullptr;
	}
	forge_level_object_t &object = doc->objects[static_cast<size_t>(index)];
	if (!object.pad_ref[0]) {
		setError(err, err_cap, "%s stands on no pad, so it has no place to move", object.record_id);
		return nullptr;
	}
	if (!object.movable) {
		setError(err, err_cap, "%s is a %s with no field that says where it stands", object.record_id,
			object.kind);
		return nullptr;
	}
	*out_index = index;
	return &object;
}

s32 forgeLevelDocMoveObject(forge_level_doc_t *doc, const char *record_id,
	const f32 position[3], const f32 look[3], char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	int index = -1;
	forge_level_object_t *object = movableObject(doc, record_id, &index, err, err_cap);
	if (!object) return 0;

	/* Prepare both references to a new pad before editing the document. */
	const int padIndex = findPad(*doc, object->pad_ref);
	const bool newPad = placementNeedsNewPad(*doc, padIndex, nullptr, object);
	const std::string ref = newPad ? numbered("pad_", static_cast<long>(doc->pads.size()))
		: std::string(object->pad_ref);
	Table &objects = doc->tables[FORGE_LEVEL_MEMBER_OBJECTS];
	Table &fields = doc->tables[FORGE_LEVEL_MEMBER_SETUP_FIELDS];
	std::string objectRow = objects.rows[static_cast<size_t>(index)];
	std::vector<std::pair<size_t, std::string>> fieldRows;
	if (newPad) {
		if (!replaceValue(objectRow, "pad_ref", quoted(ref))) {
			setError(err, err_cap, "%s could not be rewritten", object->record_id);
			return 0;
		}
		for (const size_t row : doc->recordRows[object->record_id]) {
			if (!isPlaceField(doc->fields[row].field)) continue;
			std::string text = fields.rows[row];
			if (!replaceValue(text, "value", quoted(ref))) {
				setError(err, err_cap, "%s's place could not be rewritten", object->record_id);
				return 0;
			}
			fieldRows.emplace_back(row, text);
		}
	}

	std::string error;
	const int movedPad = newPad ? addPad(*doc, position, look, error)
		: movePad(*doc, padIndex, position, look, error);
	if (movedPad < 0) {
		setError(err, err_cap, "%s", error.c_str());
		return 0;
	}
	if (newPad) {
		objects.rows[static_cast<size_t>(index)] = objectRow;
		objects.touch();
		for (const auto &changed : fieldRows) {
			fields.rows[changed.first] = changed.second;
			doc->fields[changed.first].value = ref;
		}
		if (!fieldRows.empty()) fields.touch();
		copyOut(object->pad_ref, ref.c_str());
	}
	return 1;
}

s32 forgeLevelDocDuplicateObject(forge_level_doc_t *doc, const char *record_id,
	const f32 position[3], const f32 look[3], char *out_record_id, size_t out_cap,
	char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (out_record_id && out_cap) out_record_id[0] = '\0';
	int index = -1;
	forge_level_object_t *source = movableObject(doc, record_id, &index, err, err_cap);
	if (!source) return 0;
	if (source->removed) {
		setError(err, err_cap, "%s is left out of the level; put it back before copying it", source->record_id);
		return 0;
	}
	if (doc->nextOrder > FORGE_LEVEL_MAX_OBJECT_NUMBER) {
		setError(err, err_cap, "the level's command list is full (setup_%04d is its last place)",
			FORGE_LEVEL_MAX_OBJECT_NUMBER);
		return 0;
	}

	/* The copy is the next command: its id number and its place are the
	 * same number, which is what references between records count on. */
	const long place = doc->nextOrder;
	const std::string id = numbered("setup_", place);
	const std::string ref = numbered("pad_", static_cast<long>(doc->pads.size()));
	if (doc->recordRows.count(id) || findObject(*doc, id.c_str()) >= 0) {
		setError(err, err_cap, "the level already has a %s", id.c_str());
		return 0;
	}

	/* Every row of the copy, before anything is changed. */
	Table &objects = doc->tables[FORGE_LEVEL_MEMBER_OBJECTS];
	Table &fields = doc->tables[FORGE_LEVEL_MEMBER_SETUP_FIELDS];
	std::string objectRow = objects.rows[static_cast<size_t>(index)];
	if (!replaceValue(objectRow, "record_id", quoted(id)) || !replaceValue(objectRow, "pad_ref", quoted(ref))) {
		setError(err, err_cap, "%s could not be copied", source->record_id);
		return 0;
	}
	const std::vector<size_t> sourceRows = doc->recordRows[source->record_id];
	std::vector<std::string> texts;
	std::vector<forge_level_doc::FieldRow> infos;
	for (const size_t row : sourceRows) {
		std::string text = fields.rows[row];
		forge_level_doc::FieldRow info = doc->fields[row];
		bool ok = replaceValue(text, "record_id", quoted(id));
		info.record = id;
		if (ok && info.field == "command.order") {
			info.value = std::to_string(place);
			ok = replaceValue(text, "value", quoted(info.value));
		}
		if (ok && isPlaceField(info.field)) {
			info.value = ref;
			ok = replaceValue(text, "value", quoted(ref));
		}
		if (!ok) {
			setError(err, err_cap, "%s's %s could not be copied", source->record_id, info.field.c_str());
			return 0;
		}
		texts.push_back(text);
		infos.push_back(info);
	}

	std::string error;
	const forge_level_object_t original = *source;   /* addPad may move the objects */
	if (addPad(*doc, position, look, error) < 0) {
		setError(err, err_cap, "%s", error.c_str());
		return 0;
	}
	forge_level_object_t copy = original;
	forge_level_doc::ObjectFlags copyFlags = doc->objectFlags[static_cast<size_t>(index)];
	const size_t sourceFlagsRow = copyFlags.fieldRow;
	const forge_level_doc::ObjectCatalog sourceWhat = doc->objectCatalog[static_cast<size_t>(index)];
	forge_level_doc::ObjectCatalog copyWhat = sourceWhat;
	copyOut(copy.record_id, id.c_str());
	copyOut(copy.pad_ref, ref.c_str());
	copy.order = static_cast<s32>(place);
	objects.append(objectRow);
	for (size_t i = 0; i < texts.size(); i++) {
		fields.append(texts[i]);
		doc->fields.push_back(infos[i]);
		doc->recordRows[id].push_back(doc->fields.size() - 1);
		/* The copy's flags word is the row copied from the original's, and so
		 * are the rows that say what it is drawn with and what it gives. */
		const long sourceRow = static_cast<long>(sourceRows[i]);
		const long copyRow = static_cast<long>(doc->fields.size() - 1);
		if (copyFlags.known && sourceRows[i] == sourceFlagsRow) {
			copyFlags.fieldRow = doc->fields.size() - 1;
		}
		if (sourceRow == sourceWhat.modelRow) copyWhat.modelRow = copyRow;
		if (sourceRow == sourceWhat.scaleRow) copyWhat.scaleRow = copyRow;
		if (sourceRow == sourceWhat.weaponRow) copyWhat.weaponRow = copyRow;
	}
	doc->objects.push_back(copy);
	doc->objectFlags.push_back(copyFlags);
	doc->objectCatalog.push_back(copyWhat);
	doc->nextOrder = place + 1;
	graphSyncCounts(*doc);
	if (out_record_id && out_cap) snprintf(out_record_id, out_cap, "%s", id.c_str());
	return 1;
}

/* A plain prop, made by forgeLevelDocAddProp: not in the public list of
 * kinds, because a prop needs a model to be made. */
constexpr s32 kNewProp = FORGE_LEVEL_NEW_KIND_COUNT + 1;
constexpr s32 kNewDecoration = kNewProp + 1;

/* `prop_model` is supplied for the two kinds of basic prop. */
static s32 addFromNothing(forge_level_doc_t *doc, s32 kind, s32 weapon_slot, const char *selected_model,
	const f32 position[3], const f32 look[3], char *out_record_id, size_t out_cap,
	char *err, size_t err_cap)
{
	const bool isPlace = kind == FORGE_LEVEL_NEW_WEAPON_PLACE;
	const bool isBike = kind == FORGE_LEVEL_NEW_HOVERBIKE;
	const bool isDecoration = kind == kNewDecoration;
	const bool isProp = kind == kNewProp || isDecoration;
	if (isPlace && (weapon_slot < 0 || weapon_slot >= kWeaponSlots)) {
		setError(err, err_cap, "a match has weapon places 0 to %ld, not %d", kWeaponSlots - 1,
			static_cast<int>(weapon_slot));
		return 0;
	}
	if (doc->nextOrder > FORGE_LEVEL_MAX_OBJECT_NUMBER) {
		setError(err, err_cap, "the level's command list is full (setup_%04d is its last place)",
			FORGE_LEVEL_MAX_OBJECT_NUMBER);
		return 0;
	}

	/* The next command, on the next pad: the same rule a copy follows. */
	const long place = doc->nextOrder;
	const std::string id = numbered("setup_", place);
	const std::string ref = numbered("pad_", static_cast<long>(doc->pads.size()));
	if (doc->recordRows.count(id) || findObject(*doc, id.c_str()) >= 0) {
		setError(err, err_cap, "the level already has a %s", id.c_str());
		return 0;
	}
	const char *const kindName = isPlace ? kPickupKind : isBike ? kBikeKind : isProp ? kPropKind : kCrateKind;
	const char *const model = selected_model ? selected_model
		: isPlace ? kPlaceModel : isBike ? kBikeModel : kCrateModel;
	const std::string flagsWord = isDecoration ? hexWordText(kDecorationFlags)
		: isBike ? kBikeFlags : "0x00000001";
	const std::string flags2Word = isDecoration ? hexWordText(kDecorationFlags2)
		: isBike ? kBikeFlags2 : "0x00000000";
	const std::string flags3Word = isDecoration ? hexWordText(kDecorationFlags3)
		: isBike ? kBikeFlags3 : "0x00000000";

	/* Every row of the record, in the extractor's order and layout, before
	 * anything is changed. The rows that the document keeps track of are
	 * remembered by their place in this list. */
	std::vector<std::string> texts;
	std::vector<forge_level_doc::FieldRow> infos;
	long modelAt = -1;
	long scaleAt = -1;
	long weaponAt = -1;
	long flagsAt = -1;
	const auto add = [&](const std::string &field, const char *type, const std::string &value,
			const std::string &catalog = std::string()) {
		forge_level_doc::FieldRow info;
		info.record = id;
		info.field = field;
		info.value = value;
		info.type = type;
		info.catalog = catalog;
		infos.push_back(info);
		texts.push_back("{\n      \"record_id\": \"" + id + "\",\n      \"kind\": \"" + kindName +
			"\",\n      \"field\": \"" + field + "\",\n      \"type\": \"" + type + "\",\n      \"value\": \"" +
			value + "\",\n      \"catalog_id\": \"" + catalog + "\",\n      \"ref_record_id\": \"\"\n    }");
		return static_cast<long>(texts.size() - 1);
	};
	add("command.order", "s32", std::to_string(place));
	scaleAt = add(kScaleField, "s32", "256");
	add("base.hidden2", "s32", "0");
	modelAt = add(kModelField, "model_catalog_id", "", model);
	add("base.pad", "pad_ref", ref);
	add("base.flags", "u32_hex", flagsWord);
	flagsAt = add(kFlagsField, "u32_hex", flags2Word);
	add("base.flags3", "u32_hex", flags3Word);
	for (int row = 0; row < 3; row++) {
		for (int column = 0; column < 3; column++) {
			add("base.rotation[" + std::to_string(row) + "][" + std::to_string(column) + "]", "f32", "0");
		}
	}
	add("base.hidden", "u32_hex", "0x00000000");
	add("base.damage", "s32", "0");
	add("base.max_damage", "s32", "1000");
	for (int i = 0; i < 4; i++) add("base.shade_color[" + std::to_string(i) + "]", "s32", "0");
	for (int i = 0; i < 4; i++) add("base.next_color[" + std::to_string(i) + "]", "s32", "0");
	add("base.floor_color", "s32", "4095");
	add("base.geo_count", "s32", "0");
	if (isPlace) {
		weaponAt = add(kWeaponField, "weapon_selector", weaponSlotText(weapon_slot));
		add("pickup.unknown_5d", "s32", "0");
		add("pickup.unknown_5e", "s32", "0");
		add("pickup.fire_mode", "s32", "0");
		add("pickup.fadeout_timer60", "s32", "0");
		add("pickup.dual_weapon", "weapon_selector", "none");
		add("pickup.team_or_timer240", "s32", "-1");
	} else if (isBike) {
		/* The hover state and the bike's own motion, all at rest: the game
		 * fills them in once the bike is placed (the five base hoverbikes
		 * write the same values). */
		add("hover.type", "s32", "1");
		add("hover.flags", "s32", "1");
		for (const char *name : { "bobycur", "bobytarget", "bobyspeed", "yrot", "bobpitchcur", "bobpitchtarget",
				"bobpitchspeed", "bobrollcur", "bobrolltarget", "bobrollspeed", "groundpitch", "y", "ground" }) {
			add(std::string("hover.") + name, "f32", "0");
		}
		add("hover.prevframe60", "s32", "0");
		add("hover.prevgroundframe60", "s32", "0");
		for (const char *name : { "speed[0]", "speed[1]", "previous_position[0]", "previous_position[1]", "ex_real",
				"ez_real", "ez_real2", "lean_speed", "lean_diff" }) {
			add(std::string("hoverbike.") + name, "f32", "0");
		}
		add("hoverbike.max_speed_time240", "s32", "0");
		for (const char *name : { "relative[0]", "relative[1]", "absolute_speed[0]", "absolute_speed[1]",
				"relative_speed[0]", "relative_speed[1]" }) {
			add(std::string("hoverbike.") + name, "f32", "0");
		}
	} else if (isProp) {
		/* A plain prop's record is the common rows only (2,226 of 2,226). */
	} else {
		for (int i = 0; i < kCrateSlots; i++) {
			const std::string slot = "multi_ammo_crate.slots[" + std::to_string(i) + "]";
			add(slot + ".model", "model_catalog_id", "");
			add(slot + ".quantity", "s32", "0");
		}
	}
	const std::string objectRow = "{\n      \"record_id\": \"" + id + "\",\n      \"kind\": \"" + kindName +
		"\",\n      \"pad_ref\": \"" + ref + "\",\n      \"model_catalog_id\": \"" + model +
		"\",\n      \"weapon_catalog_id\": \"\",\n      \"secondary_weapon_catalog_id\": \"\",\n"
		"      \"body_catalog_id\": \"\",\n      \"head_catalog_id\": \"\",\n      \"ailist_ref\": \"\",\n"
		"      \"flags\": \"" + flagsWord + "\",\n      \"flags2\": \"" + flags2Word + "\",\n      \"flags3\": \"" +
		flags3Word + "\"\n    }";

	std::string error;
	if (addPad(*doc, position, look, error) < 0) {
		setError(err, err_cap, "%s", error.c_str());
		return 0;
	}

	Table &objects = doc->tables[FORGE_LEVEL_MEMBER_OBJECTS];
	Table &fields = doc->tables[FORGE_LEVEL_MEMBER_SETUP_FIELDS];
	const long firstRow = static_cast<long>(doc->fields.size());
	objects.append(objectRow);
	for (size_t i = 0; i < texts.size(); i++) {
		fields.append(texts[i]);
		doc->fields.push_back(infos[i]);
		doc->recordRows[id].push_back(doc->fields.size() - 1);
	}

	forge_level_object_t object = {};
	copyOut(object.record_id, id.c_str());
	copyOut(object.kind, kindName);
	copyOut(object.pad_ref, ref.c_str());
	copyOut(object.model_catalog_id, model);
	object.order = static_cast<s32>(place);
	object.movable = 1;
	object.removable = 1;
	object.removed = 0;
	object.weapon_slot = isPlace ? weapon_slot : -1;
	object.model_settable = 1;
	object.weapon_settable = isPlace ? 1 : 0;
	forge_level_doc::ObjectFlags flags;
	flags.inObjects = true;
	flags.known = true;
	flags.fieldRow = static_cast<size_t>(firstRow + flagsAt);
	flags.objectsText = flags2Word;
	flags.fieldText = flags2Word;
	flags.value = strtoul(flags2Word.c_str(), nullptr, 16);
	flags.original = flags.value;
	forge_level_doc::ObjectCatalog what;
	what.modelRow = firstRow + modelAt;
	what.scaleRow = firstRow + scaleAt;
	what.weaponRow = isPlace ? firstRow + weaponAt : -1;
	doc->objects.push_back(object);
	doc->objectFlags.push_back(flags);
	doc->objectCatalog.push_back(what);
	doc->nextOrder = place + 1;
	graphSyncCounts(*doc);
	if (out_record_id && out_cap) snprintf(out_record_id, out_cap, "%s", id.c_str());
	return 1;
}

s32 forgeLevelDocAddObject(forge_level_doc_t *doc, s32 kind, s32 weapon_slot,
	const f32 position[3], const f32 look[3], char *out_record_id, size_t out_cap,
	char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (out_record_id && out_cap) out_record_id[0] = '\0';
	if (!doc) {
		setError(err, err_cap, "no level is open");
		return 0;
	}
	if (kind < 0 || kind >= FORGE_LEVEL_NEW_KIND_COUNT) {
		setError(err, err_cap, "%d is not a kind of object that can be made here", static_cast<int>(kind));
		return 0;
	}
	return addFromNothing(doc, kind, weapon_slot, nullptr, position, look, out_record_id, out_cap, err, err_cap);
}

static s32 addObjectWithModel(forge_level_doc_t *doc, s32 kind, const char *model_catalog_id,
	const f32 position[3], const f32 look[3], char *out_record_id, size_t out_cap,
	char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (out_record_id && out_cap) out_record_id[0] = '\0';
	if (!doc) {
		setError(err, err_cap, "no level is open");
		return 0;
	}
	if (!model_catalog_id || !catalogIdOk(model_catalog_id) ||
			strlen(model_catalog_id) >= sizeof(forge_level_object_t::model_catalog_id)) {
		setError(err, err_cap, "\"%s\" is not a model's catalog id", model_catalog_id ? model_catalog_id : "");
		return 0;
	}
	return addFromNothing(doc, kind, -1, model_catalog_id, position, look, out_record_id, out_cap, err, err_cap);
}

s32 forgeLevelDocAddHoverbike(forge_level_doc_t *doc, const char *model_catalog_id,
	const f32 position[3], const f32 look[3], char *out_record_id, size_t out_cap,
	char *err, size_t err_cap)
{
	return addObjectWithModel(doc, FORGE_LEVEL_NEW_HOVERBIKE, model_catalog_id, position, look,
		out_record_id, out_cap, err, err_cap);
}

s32 forgeLevelDocAddProp(forge_level_doc_t *doc, const char *model_catalog_id,
	const f32 position[3], const f32 look[3], char *out_record_id, size_t out_cap,
	char *err, size_t err_cap)
{
	return addObjectWithModel(doc, kNewProp, model_catalog_id, position, look, out_record_id,
		out_cap, err, err_cap);
}

s32 forgeLevelDocAddDecoration(forge_level_doc_t *doc, const char *model_catalog_id,
	const f32 position[3], const f32 look[3], char *out_record_id, size_t out_cap,
	char *err, size_t err_cap)
{
	return addObjectWithModel(doc, kNewDecoration, model_catalog_id, position, look, out_record_id,
		out_cap, err, err_cap);
}

s32 forgeLevelDecorationFlagsMatch(u32 flags, u32 flags2, u32 flags3)
{
	return flags == kDecorationFlags && flags2 == kDecorationFlags2 && flags3 == kDecorationFlags3;
}

s32 forgeLevelDocIsDecoration(const forge_level_doc_t *doc, const char *record_id)
{
	return doc && decorationPadIndex(*doc, record_id) >= 0;
}

s32 forgeLevelDocGetDecorationPose(const forge_level_doc_t *doc, const char *record_id, forge_level_pad_t *out)
{
	const int index = doc && out ? decorationPadIndex(*doc, record_id) : -1;
	if (index < 0) return 0;
	*out = doc->pads[static_cast<size_t>(index)];
	return 1;
}

s32 forgeLevelDocMoveDecoration(forge_level_doc_t *doc, const char *record_id,
	const f32 position[3], const f32 look[3], char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	const int index = doc ? decorationPadIndex(*doc, record_id) : -1;
	if (index < 0) {
		setError(err, err_cap, "select an independent decoration to move");
		return 0;
	}
	/* The native record and graph prove ownership even after reopening. */
	std::string error;
	if (movePad(*doc, index, position, look, error) < 0) {
		setError(err, err_cap, "%s", error.c_str());
		return 0;
	}
	return 1;
}

s32 forgeLevelDocSetObjectRemoved(forge_level_doc_t *doc, const char *record_id, s32 removed,
	char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	const int index = doc ? findObject(*doc, record_id) : -1;
	if (index < 0) {
		setError(err, err_cap, "the level has no object \"%s\"", record_id ? record_id : "");
		return 0;
	}
	forge_level_object_t &object = doc->objects[static_cast<size_t>(index)];
	forge_level_doc::ObjectFlags &flags = doc->objectFlags[static_cast<size_t>(index)];
	if (!flags.known) {
		setError(err, err_cap, "%s is a %s with no flags word that both of the level's object files give, "
			"so it cannot be left out", object.record_id, object.kind);
		return 0;
	}
	if (!object.removable) {
		setError(err, err_cap, "%s is a %s, which the game keeps on some levels whatever its flags say",
			object.record_id, object.kind);
		return 0;
	}
	const bool want = removed != 0;
	if (want == (object.removed != 0)) return 1;

	const unsigned long before = flags.value;
	const unsigned long after = want ? (before | kExcludeEverywhere)
		: ((before & ~kExcludeEverywhere) | flags.restore);
	/* Back at the word that was read, each file gets the characters it had. */
	const std::string objectsText = after == flags.original ? flags.objectsText : hexWordText(after);
	const std::string fieldText = after == flags.original ? flags.fieldText : hexWordText(after);
	Table &objects = doc->tables[FORGE_LEVEL_MEMBER_OBJECTS];
	Table &fields = doc->tables[FORGE_LEVEL_MEMBER_SETUP_FIELDS];
	std::string objectRow = objects.rows[static_cast<size_t>(index)];
	std::string fieldRow = fields.rows[flags.fieldRow];
	if (!replaceValue(objectRow, "flags2", quoted(objectsText)) ||
			!replaceValue(fieldRow, "value", quoted(fieldText))) {
		setError(err, err_cap, "%s's flags could not be rewritten", object.record_id);
		return 0;
	}
	objects.rows[static_cast<size_t>(index)] = objectRow;
	objects.touch();
	fields.rows[flags.fieldRow] = fieldRow;
	fields.touch();
	doc->fields[flags.fieldRow].value = fieldText;
	if (want) flags.restore = before & kExcludeEverywhere;
	flags.value = after;
	object.removed = want ? 1 : 0;
	return 1;
}

/* The weapon pickup `record_id` with every row a change of weapon needs, or
 * NULL with the reason. */
static forge_level_object_t *settablePickup(forge_level_doc_t *doc, const char *record_id,
	int *out_index, char *err, size_t err_cap)
{
	const int index = doc ? findObject(*doc, record_id) : -1;
	if (index < 0) {
		setError(err, err_cap, "the level has no object \"%s\"", record_id ? record_id : "");
		return nullptr;
	}
	forge_level_object_t &object = doc->objects[static_cast<size_t>(index)];
	if (strcmp(object.kind, kPickupKind) != 0) {
		setError(err, err_cap, "%s is a %s, not a weapon pickup", object.record_id, object.kind);
		return nullptr;
	}
	if (!object.weapon_settable) {
		setError(err, err_cap, "%s has no weapon, model and size that both of the level's object files give, "
			"so what it gives cannot be changed", object.record_id);
		return nullptr;
	}
	*out_index = index;
	return &object;
}

/* Writes changed rows back: the object's row in objects.json and any of its
 * rows in setup.fields.json, with what the document remembers of each. */
static void commitObjectRows(forge_level_doc_t *doc, int index, const std::string &objectRow,
	const std::vector<std::pair<long, std::string>> &fieldRows)
{
	Table &objects = doc->tables[FORGE_LEVEL_MEMBER_OBJECTS];
	Table &fields = doc->tables[FORGE_LEVEL_MEMBER_SETUP_FIELDS];
	objects.rows[static_cast<size_t>(index)] = objectRow;
	objects.touch();
	for (const auto &changed : fieldRows) {
		fields.rows[static_cast<size_t>(changed.first)] = changed.second;
	}
	if (!fieldRows.empty()) fields.touch();
}

s32 forgeLevelDocSetObjectScale(forge_level_doc_t *doc, const char *record_id, s32 extra_scale,
	char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	const int index = doc ? findObject(*doc, record_id) : -1;
	if (index < 0) {
		setError(err, err_cap, "the level has no object \"%s\"", record_id ? record_id : "");
		return 0;
	}
	const forge_level_object_t &object = doc->objects[static_cast<size_t>(index)];
	if (object.weapon_slot >= 0) {
		setError(err, err_cap, "%s is one of the match's weapon places, which the match's weapon set sizes",
			object.record_id);
		return 0;
	}
	const forge_level_doc::ObjectCatalog what = doc->objectCatalog[static_cast<size_t>(index)];
	if (what.scaleRow < 0) {
		setError(err, err_cap, "%s has no field that says its size, so it cannot be changed here",
			object.record_id);
		return 0;
	}
	if (extra_scale < 1 || extra_scale > kMaxExtraScale) {
		setError(err, err_cap, "a size is 1 to %ld (256 is life size), not %d", kMaxExtraScale,
			static_cast<int>(extra_scale));
		return 0;
	}
	Table &fields = doc->tables[FORGE_LEVEL_MEMBER_SETUP_FIELDS];
	std::string scaleRow = fields.rows[static_cast<size_t>(what.scaleRow)];
	const std::string scale = std::to_string(static_cast<long>(extra_scale));
	if (!replaceValue(scaleRow, "value", quoted(scale))) {
		setError(err, err_cap, "%s's size row could not be rewritten", object.record_id);
		return 0;
	}
	fields.rows[static_cast<size_t>(what.scaleRow)] = scaleRow;
	fields.touch();
	doc->fields[static_cast<size_t>(what.scaleRow)].value = scale;
	return 1;
}

static bool pickupHasTrigger(const forge_level_doc &doc, const char *record_id)
{
	return std::any_of(doc.triggers.begin(), doc.triggers.end(), [&](const auto &row) {
		return row.trigger.action == FORGE_LEVEL_TRIGGER_GIVE_PICKUP && strcmp(row.trigger.target_id, record_id) == 0;
	});
}

s32 forgeLevelDocSetPickupWeapon(forge_level_doc_t *doc, const char *record_id,
	const char *weapon_catalog_id, const char *model_catalog_id, s32 extra_scale,
	char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	int index = -1;
	forge_level_object_t *object = settablePickup(doc, record_id, &index, err, err_cap);
	if (!object) return 0;
	if (!catalogIdOk(weapon_catalog_id)) {
		setError(err, err_cap, "\"%s\" is not a weapon's catalog id", weapon_catalog_id ? weapon_catalog_id : "");
		return 0;
	}
	if (!ordinaryTriggerPickup(weapon_catalog_id) && pickupHasTrigger(*doc, record_id)) {
		setError(err, err_cap, "remove the pickup's trigger before assigning this item");
		return 0;
	}
	if (!catalogIdOk(model_catalog_id)) {
		setError(err, err_cap, "\"%s\" is not a model's catalog id", model_catalog_id ? model_catalog_id : "");
		return 0;
	}
	if (extra_scale < 1 || extra_scale > kMaxExtraScale) {
		setError(err, err_cap, "a pickup's size is 1 to %ld (256 is life size), not %d", kMaxExtraScale,
			static_cast<int>(extra_scale));
		return 0;
	}

	/* Every row, before anything is changed. */
	const forge_level_doc::ObjectCatalog what = doc->objectCatalog[static_cast<size_t>(index)];
	Table &fields = doc->tables[FORGE_LEVEL_MEMBER_SETUP_FIELDS];
	std::string objectRow = doc->tables[FORGE_LEVEL_MEMBER_OBJECTS].rows[static_cast<size_t>(index)];
	std::string weaponRow = fields.rows[static_cast<size_t>(what.weaponRow)];
	std::string modelRow = fields.rows[static_cast<size_t>(what.modelRow)];
	std::string scaleRow = fields.rows[static_cast<size_t>(what.scaleRow)];
	const std::string scale = std::to_string(static_cast<long>(extra_scale));
	if (!replaceValue(objectRow, "weapon_catalog_id", quoted(weapon_catalog_id)) ||
			!replaceValue(objectRow, "model_catalog_id", quoted(model_catalog_id)) ||
			!replaceValue(weaponRow, "type", quoted("weapon_catalog_id")) ||
			!replaceValue(weaponRow, "value", quoted("")) ||
			!replaceValue(weaponRow, "catalog_id", quoted(weapon_catalog_id)) ||
			!replaceValue(modelRow, "catalog_id", quoted(model_catalog_id)) ||
			!replaceValue(scaleRow, "value", quoted(scale))) {
		setError(err, err_cap, "%s's rows could not be rewritten", object->record_id);
		return 0;
	}

	commitObjectRows(doc, index, objectRow,
		{ { what.weaponRow, weaponRow }, { what.modelRow, modelRow }, { what.scaleRow, scaleRow } });
	forge_level_doc::FieldRow &weaponInfo = doc->fields[static_cast<size_t>(what.weaponRow)];
	weaponInfo.type = "weapon_catalog_id";
	weaponInfo.value.clear();
	weaponInfo.catalog = weapon_catalog_id;
	doc->fields[static_cast<size_t>(what.modelRow)].catalog = model_catalog_id;
	doc->fields[static_cast<size_t>(what.scaleRow)].value = scale;
	copyOut(object->weapon_catalog_id, weapon_catalog_id);
	copyOut(object->model_catalog_id, model_catalog_id);
	object->weapon_slot = -1;
	return 1;
}

s32 forgeLevelDocSetPickupSlot(forge_level_doc_t *doc, const char *record_id, s32 slot,
	char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	int index = -1;
	forge_level_object_t *object = settablePickup(doc, record_id, &index, err, err_cap);
	if (!object) return 0;
	if (pickupHasTrigger(*doc, record_id)) {
		setError(err, err_cap, "remove the pickup's trigger before making it a match weapon place");
		return 0;
	}
	if (slot < 0 || slot >= kWeaponSlots) {
		setError(err, err_cap, "a match has weapon places 0 to %ld, not %d", kWeaponSlots - 1,
			static_cast<int>(slot));
		return 0;
	}

	const forge_level_doc::ObjectCatalog what = doc->objectCatalog[static_cast<size_t>(index)];
	std::string objectRow = doc->tables[FORGE_LEVEL_MEMBER_OBJECTS].rows[static_cast<size_t>(index)];
	std::string weaponRow = doc->tables[FORGE_LEVEL_MEMBER_SETUP_FIELDS].rows[static_cast<size_t>(what.weaponRow)];
	const std::string place = weaponSlotText(slot);
	if (!replaceValue(objectRow, "weapon_catalog_id", quoted("")) ||
			!replaceValue(weaponRow, "type", quoted("weapon_selector")) ||
			!replaceValue(weaponRow, "value", quoted(place)) ||
			!replaceValue(weaponRow, "catalog_id", quoted(""))) {
		setError(err, err_cap, "%s's rows could not be rewritten", object->record_id);
		return 0;
	}

	commitObjectRows(doc, index, objectRow, { { what.weaponRow, weaponRow } });
	forge_level_doc::FieldRow &weaponInfo = doc->fields[static_cast<size_t>(what.weaponRow)];
	weaponInfo.type = "weapon_selector";
	weaponInfo.value = place;
	weaponInfo.catalog.clear();
	object->weapon_catalog_id[0] = '\0';
	object->weapon_slot = slot;
	return 1;
}

static s32 setObjectModelAt(forge_level_doc_t *doc, int index,
	const char *model_catalog_id, char *err, size_t err_cap)
{
	forge_level_object_t &object = doc->objects[static_cast<size_t>(index)];
	if (!object.model_settable) {
		setError(err, err_cap, "%s has no model that both of the level's object files give, "
			"so it cannot be changed", object.record_id);
		return 0;
	}
	if (!catalogIdOk(model_catalog_id)) {
		setError(err, err_cap, "\"%s\" is not a model's catalog id", model_catalog_id ? model_catalog_id : "");
		return 0;
	}

	const forge_level_doc::ObjectCatalog what = doc->objectCatalog[static_cast<size_t>(index)];
	std::string objectRow = doc->tables[FORGE_LEVEL_MEMBER_OBJECTS].rows[static_cast<size_t>(index)];
	std::string modelRow = doc->tables[FORGE_LEVEL_MEMBER_SETUP_FIELDS].rows[static_cast<size_t>(what.modelRow)];
	if (!replaceValue(objectRow, "model_catalog_id", quoted(model_catalog_id)) ||
			!replaceValue(modelRow, "catalog_id", quoted(model_catalog_id))) {
		setError(err, err_cap, "%s's rows could not be rewritten", object.record_id);
		return 0;
	}

	commitObjectRows(doc, index, objectRow, { { what.modelRow, modelRow } });
	doc->fields[static_cast<size_t>(what.modelRow)].catalog = model_catalog_id;
	copyOut(object.model_catalog_id, model_catalog_id);
	return 1;
}

s32 forgeLevelDocSetObjectModel(forge_level_doc_t *doc, const char *record_id,
	const char *model_catalog_id, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	const int index = doc ? findObject(*doc, record_id) : -1;
	if (index < 0) {
		setError(err, err_cap, "the level has no object \"%s\"", record_id ? record_id : "");
		return 0;
	}
	const forge_level_object_t &object = doc->objects[static_cast<size_t>(index)];
	if (strcmp(object.kind, kPropKind) != 0) {
		setError(err, err_cap, "%s is a %s; only a plain prop can be given another model here",
			object.record_id, object.kind);
		return 0;
	}
	return setObjectModelAt(doc, index, model_catalog_id, err, err_cap);
}

static bool placementFallbackFieldsMatch(const forge_level_doc &doc, const forge_level_object_t &object, bool pickup)
{
	const auto record = doc.recordRows.find(object.record_id);
	if (record == doc.recordRows.end() || record->second.empty()) return false;
	/* Native setup creates the command from its first field row. */
	const auto &first = doc.tables[FORGE_LEVEL_MEMBER_SETUP_FIELDS].rows[record->second.front()];
	Json row;
	if (!modAssetJsonReadValue(first.data(), first.size(), row) || stringOf(row, "kind") != object.kind) return false;

	size_t models = 0, weapons = 0, scales = 0;
	for (const size_t index : record->second) {
		const auto &field = doc.fields[index].field;
		models += field == kModelField;
		weapons += field == kWeaponField;
		scales += field == kScaleField;
	}
	return models == 1 && (!pickup || (weapons == 1 && scales == 1));
}

forge_level_doc_t *forgeLevelDocCloneWithPlacementFallbacks(const forge_level_doc_t *doc,
	const forge_level_placement_fallback_t *edits, size_t count, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!doc || (count && !edits) || count > doc->objects.size()) {
		setError(err, err_cap, "the level or placement fallback list is invalid");
		return nullptr;
	}

	try {
		std::unique_ptr<forge_level_doc> copy(forgeLevelDocClone(doc));
		if (!copy) {
			setError(err, err_cap, "could not copy the level for placement fallbacks");
			return nullptr;
		}
		std::set<int> edited;
		for (size_t i = 0; i < count; ++i) {
			const auto &edit = edits[i];
			const int index = findObject(*doc, edit.record_id);
			if (index < 0 || !edited.insert(index).second) {
				setError(err, err_cap, "placement \"%s\" is missing or repeated", edit.record_id ? edit.record_id : "");
				return nullptr;
			}
			const auto &object = doc->objects[static_cast<size_t>(index)];
			const bool pickup = edit.kind == FORGE_LEVEL_FALLBACK_PICKUP;
			const char *required_kind = nullptr;
			const char *base_model = nullptr;
			switch (edit.kind) {
			case FORGE_LEVEL_FALLBACK_PICKUP:
				required_kind = kPickupKind;
				break;
			case FORGE_LEVEL_FALLBACK_PROP:
				required_kind = kPropKind;
				base_model = "base:model_crate2_cq";
				break;
			case FORGE_LEVEL_FALLBACK_HOVERBIKE:
				required_kind = kBikeKind;
				base_model = kBikeModel;
				break;
			}
			if (!required_kind || strcmp(object.kind, required_kind) != 0
					|| !placementFallbackFieldsMatch(*doc, object, pickup)) {
				setError(err, err_cap, "%s has no supported placement fallback", object.record_id);
				return nullptr;
			}
			const char *current = pickup ? object.weapon_catalog_id : object.model_catalog_id;
			if (!catalogIdOk(edit.expected_catalog_id) || strncmp(edit.expected_catalog_id, "base:", 5) == 0
					|| strcmp(current, edit.expected_catalog_id) != 0) {
				setError(err, err_cap, "%s no longer has the selected mod asset", object.record_id);
				return nullptr;
			}
			if (pickup) {
				if (!edit.weapon_catalog_id || (strcmp(edit.weapon_catalog_id, "base:falcon2") != 0
						&& strcmp(edit.weapon_catalog_id, "base:slayer") != 0) || !edit.model_catalog_id
						|| strncmp(edit.model_catalog_id, "base:", 5) != 0) {
					setError(err, err_cap, "%s needs a Falcon 2 or Slayer pickup fallback and its base model", object.record_id);
					return nullptr;
				}
				if (!forgeLevelDocSetPickupWeapon(copy.get(), object.record_id,
						edit.weapon_catalog_id, edit.model_catalog_id, edit.extra_scale, err, err_cap)) return nullptr;
			} else {
				if (edit.weapon_catalog_id || edit.model_catalog_id || edit.extra_scale) {
					setError(err, err_cap, "%s has a fixed base model fallback", object.record_id);
					return nullptr;
				}
				if (!setObjectModelAt(copy.get(), index, base_model, err, err_cap)) return nullptr;
			}
		}
		return copy.release();
	} catch (const std::bad_alloc &) {
		setError(err, err_cap, "could not allocate placement fallbacks");
		return nullptr;
	}
}

s32 forgeLevelDocRemoveSpawn(forge_level_doc_t *doc, const char *spawn_id,
	char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	const int index = doc ? findSpawn(*doc, spawn_id) : -1;
	if (index < 0) {
		setError(err, err_cap, "the level has no spawn \"%s\"", spawn_id ? spawn_id : "");
		return 0;
	}
	doc->tables[FORGE_LEVEL_MEMBER_SPAWNS].remove(static_cast<size_t>(index));
	doc->spawns.erase(doc->spawns.begin() + index);
	return 1;
}

s32 forgeLevelDocMemberChanged(const forge_level_doc_t *doc, s32 member)
{
	if (!doc || member < 0 || member >= FORGE_LEVEL_MEMBER_COUNT) return 0;
	if (doc->recoveredMembers[member]) return 1;
	if (!doc->tables[member].changed) return 0;
	forge_level_doc_t *mutableDoc = const_cast<forge_level_doc_t *>(doc);
	return mutableDoc->tables[member].text() != doc->tables[member].original ? 1 : 0;
}

const char *forgeLevelDocMemberText(forge_level_doc_t *doc, s32 member, size_t *out_size)
{
	if (out_size) *out_size = 0;
	if (!doc || member < 0 || member >= FORGE_LEVEL_MEMBER_COUNT) return nullptr;
	const std::string &text = doc->tables[member].text();
	if (out_size) *out_size = text.size();
	return text.c_str();
}

const char *forgeLevelDocMemberName(s32 member)
{
	return member >= 0 && member < FORGE_LEVEL_MEMBER_COUNT ? kMemberNames[member] : nullptr;
}

static s32 levelOverrides(forge_level_doc_t *doc, const char *scenario_member, bool every,
	forge_level_overrides_t *out, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (out) memset(out, 0, sizeof(*out));
	if (!doc || !out) {
		setError(err, err_cap, "no level is open");
		return 0;
	}
	const std::string prefix = scenario_member && scenario_member[0]
		? std::string(scenario_member) + "::" : std::string();
	for (s32 member = 0; member < FORGE_LEVEL_MEMBER_COUNT; member++) {
		if (!levelMemberPresent(doc, member)) continue;
		if (!every && !forgeLevelDocMemberChanged(doc, member)) continue;
		const std::string name = prefix + kMemberNames[member];
		const std::string &text = doc->tables[member].text();
		if (name.size() >= FORGE_LEVEL_OVERRIDE_NAME_MAX || text.size() > 0xffffffffu) {
			memset(out, 0, sizeof(*out));
			setError(err, err_cap, "%s is too long a name or too large a file to save",
				kMemberNames[member]);
			return 0;
		}
		char *slot = out->names[out->count];
		snprintf(slot, FORGE_LEVEL_OVERRIDE_NAME_MAX, "%s", name.c_str());
		out->entries[out->count].entry = slot;
		out->entries[out->count].data = text.data();
		out->entries[out->count].len = static_cast<u32>(text.size());
		out->entries[out->count].remove = 0;
		out->count++;
	}
	return 1;
}

s32 forgeLevelDocOverrides(forge_level_doc_t *doc, const char *scenario_member,
	forge_level_overrides_t *out, char *err, size_t err_cap)
{
	return levelOverrides(doc, scenario_member, false, out, err, err_cap);
}

s32 forgeLevelDocOverridesAll(forge_level_doc_t *doc, const char *scenario_member,
	forge_level_overrides_t *out, char *err, size_t err_cap)
{
	return levelOverrides(doc, scenario_member, true, out, err, err_cap);
}

} // extern "C"
