// The placements of a level, as Forge edits them: see port/include/forge_level_doc.h.
#include "forge_level_doc.h"

#include "modasset_gltf_document.h"
#include "../external/imgui-node-editor/crude_json.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <set>
#include <string>
#include <vector>

namespace {

using Json = crude_json::value;

constexpr const char *kMemberNames[FORGE_LEVEL_MEMBER_COUNT] = {
	"pads.json", "spawns.json", "objects.json", "volumes.json", "setup.fields.json", "level.graph.json"
};
constexpr const char *kSchemas[FORGE_LEVEL_MEMBER_COUNT] = {
	"pd2.scenario.pads.v1", "pd2.scenario.spawns.v1", "pd2.scenario.objects.v1",
	"pd2.scenario.volumes.v1", "pd2.scenario.setup.fields.v1", "pd2.level.graph.v1"
};
/* What the game counts in a level's graph when it loads the level, as text
 * (scenario_source_runtime.c, s_countTextOccurrences): one of these for each
 * row of volumes.json, or the level is refused. */
constexpr const char *kVolumeNodeKind = "\"kind\": \"scenario.trigger.volume.source\"";
/* A link to such a node, as the extractor writes one. */
constexpr const char *kVolumeLinkTo = "\"to\": \"trigger.volume.";
/* The graph's `counts` this module keeps true, and the table each one counts. */
constexpr const char *kGraphCounts[3] = { "pads", "volumes", "objects" };
/* A pad's bounds reach this far from it on every axis when it has no box of
 * its own; the extractor writes the same box for such a pad. */
constexpr f32 kPadReach = 100.0f;

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
			const size_t keyStart = pos;
			if (!skipString(text, pos)) return false;
			member.key = text.substr(keyStart + 1, pos - keyStart - 2);
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

/* "[1.000000, 2.000000, 3.000000]", the way the extractor writes a vector. */
std::string vectorText(const f32 v[3])
{
	char text[96];
	double c[3];
	for (int i = 0; i < 3; i++) c[i] = v[i] == 0.0f ? 0.0 : static_cast<double>(v[i]);
	snprintf(text, sizeof(text), "[%.6f, %.6f, %.6f]", c[0], c[1], c[2]);
	return text;
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
	std::vector<forge_level_pad_t> pads;
	std::vector<forge_level_spawn_t> spawns;
	std::vector<forge_level_object_t> objects;
	std::vector<int> padVolume;         /* each pad's row in volumes.json, or -1 */
	std::set<std::string> volumeIds;    /* every volume_id the file has held */
	/* setup.fields.json: what each row says, and each record's rows in file order. */
	struct FieldRow {
		std::string record;
		std::string field;
		std::string value;
		std::string type;
		std::string catalog;   /* its catalog_id */
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
	const std::string from = graphVolumeLinkFrom(doc, text);
	const std::string node = std::string("{ \"id\": \"") + nodeId + "\", " + kVolumeNodeKind +
		", \"table\": \"volumes.json\", \"volume\": \"" + volumeId + "\", \"pad\": \"" + padRef + "\" }";
	const std::string link = "{ \"from\": " + from + ", \"to\": \"" + nodeId + "\" }";
	if (!graphInsert(text, "nodes", kVolumeNodeKind, node) ||
			(!from.empty() && !graphInsert(text, "links", kVolumeLinkTo, link))) {
		error = "level.graph.json could not take the new pad's trigger volume node";
		return false;
	}
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
	/* The game takes a graph with `links`, with `edges`, or with both. Only
	 * `links` is written to here, and only when the graph has it. */
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
	const std::string volumeId = numbered("volume_pad_", number);
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

/*
 * Puts whatever names `padIndex` at a new position and facing. A pad this
 * document added is moved where it stands. Any other pad may be shared, so
 * the mover gets a new pad and the old one stays. Returns the pad to name,
 * or -1.
 */
int movePad(forge_level_doc &doc, int padIndex, const f32 position[3], const f32 look[3],
	std::string &error)
{
	if (padIndex < 0 || !doc.pads[static_cast<size_t>(padIndex)].added) {
		return addPad(doc, position, look, error);
	}
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
	std::string error;
	const int pad = movePad(*doc, findPad(*doc, spawn.pad_ref), position, look, error);
	if (pad < 0) {
		setError(err, err_cap, "%s", error.c_str());
		return 0;
	}
	const char *ref = doc->pads[static_cast<size_t>(pad)].pad_ref;
	if (strcmp(ref, spawn.pad_ref) != 0) {
		Table &table = doc->tables[FORGE_LEVEL_MEMBER_SPAWNS];
		std::string row = table.rows[static_cast<size_t>(index)];
		if (!replaceValue(row, "pad_ref", std::string("\"") + ref + "\"")) {
			setError(err, err_cap, "%s could not be rewritten", spawn.spawn_id);
			return 0;
		}
		/* A row that repeats its pad's place would now be wrong: clear it. */
		replaceValue(row, "position", "null");
		replaceValue(row, "look", "null");
		table.rows[static_cast<size_t>(index)] = row;
		table.touch();
		copyOut(spawn.pad_ref, ref);
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

	/* A pad this document added moves where it stands and no row that names
	 * it changes. Any other pad stays, and the object names a new one: work
	 * out every row that will say so before anything is changed. */
	const int padIndex = findPad(*doc, object->pad_ref);
	const bool newPad = !doc->pads[static_cast<size_t>(padIndex)].added;
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
	if (movePad(*doc, padIndex, position, look, error) < 0) {
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

/* forgeLevelDocAddObject and forgeLevelDocAddProp: `kind` is one of the
 * public kinds or kNewProp, `prop_model` the prop's model (kNewProp only). */
static s32 addFromNothing(forge_level_doc_t *doc, s32 kind, s32 weapon_slot, const char *prop_model,
	const f32 position[3], const f32 look[3], char *out_record_id, size_t out_cap,
	char *err, size_t err_cap)
{
	const bool isPlace = kind == FORGE_LEVEL_NEW_WEAPON_PLACE;
	const bool isBike = kind == FORGE_LEVEL_NEW_HOVERBIKE;
	const bool isProp = kind == kNewProp;
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
	const char *const model = isPlace ? kPlaceModel : isBike ? kBikeModel : isProp ? prop_model : kCrateModel;
	/* A weapon place, a crate and a prop fall to the floor under them and
	 * have no other flag; a hoverbike has its own three words. */
	const char *const flagsWord = isBike ? kBikeFlags : "0x00000001";
	const char *const flags2Word = isBike ? kBikeFlags2 : "0x00000000";
	const char *const flags3Word = isBike ? kBikeFlags3 : "0x00000000";

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
	flags.value = strtoul(flags2Word, nullptr, 16);
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

s32 forgeLevelDocAddProp(forge_level_doc_t *doc, const char *model_catalog_id,
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
	return addFromNothing(doc, kNewProp, -1, model_catalog_id, position, look, out_record_id, out_cap, err,
		err_cap);
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

s32 forgeLevelDocSetObjectModel(forge_level_doc_t *doc, const char *record_id,
	const char *model_catalog_id, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	const int index = doc ? findObject(*doc, record_id) : -1;
	if (index < 0) {
		setError(err, err_cap, "the level has no object \"%s\"", record_id ? record_id : "");
		return 0;
	}
	forge_level_object_t &object = doc->objects[static_cast<size_t>(index)];
	if (strcmp(object.kind, kPropKind) != 0) {
		setError(err, err_cap, "%s is a %s; only a plain prop can be given another model here",
			object.record_id, object.kind);
		return 0;
	}
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
