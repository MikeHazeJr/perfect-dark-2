// The placements of a level, as Forge edits them: see port/include/forge_level_doc.h.
#include "forge_level_doc.h"

#include "modasset_gltf_document.h"
#include "../external/imgui-node-editor/crude_json.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
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
	"pads.json", "spawns.json", "objects.json", "volumes.json", "setup.fields.json"
};
constexpr const char *kSchemas[FORGE_LEVEL_MEMBER_COUNT] = {
	"pd2.scenario.pads.v1", "pd2.scenario.spawns.v1", "pd2.scenario.objects.v1",
	"pd2.scenario.volumes.v1", "pd2.scenario.setup.fields.v1"
};
/* A pad's bounds reach this far from it on every axis when it has no box of
 * its own; the extractor writes the same box for such a pad. */
constexpr f32 kPadReach = 100.0f;

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

/* The field of a record that says where it stands. */
bool isPlaceField(const std::string &field)
{
	return field == "base.pad" || field == "character.pad";
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
	bool wasEmpty = false;

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

	const std::string &text()
	{
		if (!changed) return original;
		if (stale) {
			built = head;
			for (size_t i = 0; i < rows.size(); i++) {
				if (i > 0) built += before[i];
				built += rows[i];
			}
			built += tail;
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
	};
	std::vector<FieldRow> fields;
	std::map<std::string, std::vector<size_t>> recordRows;
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
		doc.objects.push_back(object);
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

	for (forge_level_object_t &object : doc.objects) {
		const long number = trailingNumber(object.record_id, "setup_");
		object.order = -1;
		object.movable = 0;
		if (number >= doc.nextOrder) doc.nextOrder = number + 1;
		const auto found = doc.recordRows.find(object.record_id);
		if (found == doc.recordRows.end()) continue;
		for (const size_t index : found->second) {
			const forge_level_doc::FieldRow &field = doc.fields[index];
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
	copyOut(copy.record_id, id.c_str());
	copyOut(copy.pad_ref, ref.c_str());
	copy.order = static_cast<s32>(place);
	objects.append(objectRow);
	doc->objects.push_back(copy);
	for (size_t i = 0; i < texts.size(); i++) {
		fields.append(texts[i]);
		doc->fields.push_back(infos[i]);
		doc->recordRows[id].push_back(doc->fields.size() - 1);
	}
	doc->nextOrder = place + 1;
	if (out_record_id && out_cap) snprintf(out_record_id, out_cap, "%s", id.c_str());
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

s32 forgeLevelDocOverrides(forge_level_doc_t *doc, const char *scenario_member,
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
		if (!forgeLevelDocMemberChanged(doc, member)) continue;
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

} // extern "C"
