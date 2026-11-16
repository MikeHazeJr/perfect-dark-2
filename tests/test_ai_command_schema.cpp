/*
 * tests/test_ai_command_schema.cpp -- typed AI operands, slice 1
 * (context/designs/modding/ai-typed-operands.md): the command schema
 * (port/src/ai_command_schema.c) against the native handlers and their
 * length table, its field helpers, and the conformance tool's copy.
 */

#include "catch.hpp"

extern "C" {
#include "ai_command_schema.h"
}

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace {

std::string readSource(const char *path)
{
	std::ifstream input(path, std::ios::binary);
	REQUIRE(input.good());
	std::ostringstream out;
	out << input.rdbuf();
	std::string text = out.str();
	text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
	return text;
}

// g_CommandPointers or g_CommandLengths as opcode -> entry text, keeping the
// first entry of an opcode (the NTSC 1.0+ side, which the port builds).
std::map<uint16_t, std::string> commandTable(const std::string &chrai,
	const char *opening)
{
	const size_t start = chrai.find(opening);
	REQUIRE(start != std::string::npos);
	const size_t end = chrai.find("\n};", start);
	REQUIRE(end != std::string::npos);
	const std::string table = chrai.substr(start, end - start);
	static const std::regex entry(R"(/\*0x([0-9a-fA-F]{4})\*/\s*([^,\n]+),)");
	std::map<uint16_t, std::string> out;
	for (std::sregex_iterator it(table.begin(), table.end(), entry), last;
			it != last; ++it) {
		out.emplace((uint16_t)std::stoul((*it)[1].str(), nullptr, 16),
			(*it)[2].str());
	}
	return out;
}

int lengthOf(const std::string &text)
{
	// VERSION >= VERSION_NTSC_1_0 ? a : b -> a, the port's side.
	if (text.find('?') != std::string::npos) {
		const size_t q = text.find('?');
		return std::stoi(text.substr(q + 1, text.find(':', q) - q - 1));
	}
	return std::stoi(text);
}

// Every handler: "bool <name>(...)" at column 0, then a line "{"; the body
// runs to the first line that is exactly "}".
void collectHandlerBodies(const std::string &src,
	std::map<std::string, std::string> &out)
{
	size_t pos = 0;
	while ((pos = src.find("\nbool ", pos)) != std::string::npos) {
		const size_t name_start = pos + 6;
		const size_t paren = src.find('(', name_start);
		const size_t line_end = src.find('\n', name_start);
		pos = name_start;
		if (paren == std::string::npos || line_end == std::string::npos ||
				paren > line_end || src.compare(line_end + 1, 1, "{") != 0) {
			continue;
		}
		const std::string name = src.substr(name_start, paren - name_start);
		const size_t body_end = src.find("\n}\n", line_end);
		REQUIRE(body_end != std::string::npos);
		out.emplace(name, src.substr(line_end + 1, body_end - line_end));
		pos = body_end;
	}
}

const ai_command_field_t *fieldAt(const ai_command_schema_t *schema, int offset)
{
	for (uint32_t i = 0; i < schema->field_count; i++) {
		const ai_command_field_t &f = schema->fields[i];
		if (offset >= f.offset && offset < f.offset + f.width) {
			return &f;
		}
	}
	return nullptr;
}

std::string kindName(uint8_t kind)
{
	const char *name = aiOperandKindName((ai_operand_kind_t)kind);
	return name ? name : "?";
}

// The byte offsets a handler body reads through `cmd`: cmd[n] and cmd->bn.
std::set<int> bytesRead(const std::string &body)
{
	static const std::regex read(R"(cmd(?:\[(\d+)\]|->b(\d+)))");
	std::set<int> out;
	for (std::sregex_iterator it(body.begin(), body.end(), read), last; it != last; ++it) {
		out.insert(std::stoi((*it)[1].matched ? (*it)[1].str() : (*it)[2].str()));
	}
	return out;
}

// The operand byte n of each call "<call>(... cmd[n])" or "<call>(cmd[n])".
std::vector<int> bytesPassedTo(const std::string &body, const std::string &call)
{
	const std::regex pattern(call + R"(\((?:[^()]*,\s*)?cmd(?:\[(\d+)\]|->b(\d+))\s*\))");
	std::vector<int> out;
	for (std::sregex_iterator it(body.begin(), body.end(), pattern), last; it != last; ++it) {
		out.push_back(std::stoi((*it)[1].matched ? (*it)[1].str() : (*it)[2].str()));
	}
	return out;
}

} // namespace

TEST_CASE("AI command schema: every handler has a command with its length",
	"[ai][schema][static]")
{
	const std::string chrai = readSource("src/game/chrai.c");
	const std::map<uint16_t, std::string> pointers =
		commandTable(chrai, "bool (*g_CommandPointers[])(void) = {");
	const std::map<uint16_t, std::string> lengths =
		commandTable(chrai, "u16 g_CommandLengths[] = {");
	REQUIRE(pointers.size() > 0x01d0);

	uint32_t handlers = 0;
	for (const auto &[opcode, handler] : pointers) {
		INFO("opcode 0x" << std::hex << opcode << " " << handler);
		const ai_command_schema_t *schema = aiCommandSchemaFind(opcode);
		if (handler == "NULL") {
			REQUIRE(schema == nullptr);
			continue;
		}
		handlers++;
		REQUIRE(schema != nullptr);
		REQUIRE(schema->opcode == opcode);
		if (opcode == AI_COMMAND_OP_PRINT) {
			REQUIRE(schema->length == 0);
		} else {
			REQUIRE(lengths.count(opcode) == 1);
			REQUIRE(schema->length == lengthOf(lengths.at(opcode)));
		}
	}
	REQUIRE(aiCommandSchemaCount() == handlers);
}

TEST_CASE("AI command schema: the fields cover every operand byte once",
	"[ai][schema]")
{
	REQUIRE(aiCommandSchemaCount() > 400);
	int previous = -1;
	for (uint32_t i = 0; i < aiCommandSchemaCount(); i++) {
		const ai_command_schema_t *schema = aiCommandSchemaAt(i);
		REQUIRE(schema != nullptr);
		INFO("opcode 0x" << std::hex << schema->opcode << " " << schema->name);
		REQUIRE((int)schema->opcode > previous);
		previous = schema->opcode;
		REQUIRE(schema->name != nullptr);
		REQUIRE(std::strlen(schema->name) > 0);
		REQUIRE(aiCommandSchemaFind(schema->opcode) == schema);

		if (schema->opcode == AI_COMMAND_OP_PRINT) {
			REQUIRE(schema->field_count == 1);
			REQUIRE(schema->fields[0].kind == AI_OPERAND_STRING);
			REQUIRE(schema->fields[0].offset == 2);
			REQUIRE(schema->fields[0].width == 0);
			continue;
		}
		REQUIRE(schema->length >= 2);
		int next = 2;
		std::set<std::string> names;
		for (uint32_t f = 0; f < schema->field_count; f++) {
			const ai_command_field_t &field = schema->fields[f];
			INFO("field " << field.name);
			REQUIRE(field.offset == next);
			REQUIRE(field.width >= 1);
			REQUIRE(field.width <= 4);
			REQUIRE(field.kind < AI_OPERAND_KIND_COUNT);
			REQUIRE(field.kind != AI_OPERAND_STRING);
			REQUIRE(names.insert(field.name).second);
			REQUIRE(aiCommandSchemaField(schema, field.name) == &field);
			next += field.width;
		}
		REQUIRE(next == schema->length);
	}
}

TEST_CASE("AI command schema: the handlers read the bytes as the fields say",
	"[ai][schema][static]")
{
	const std::string chrai = readSource("src/game/chrai.c");
	std::map<std::string, std::string> bodies;
	collectHandlerBodies(readSource("src/game/chraicommands.c"), bodies);
	collectHandlerBodies(readSource("src/game/mplayer/mpaicommands.c"), bodies);
	collectHandlerBodies(chrai, bodies);
	const std::map<uint16_t, std::string> pointers =
		commandTable(chrai, "bool (*g_CommandPointers[])(void) = {");

	// A byte read as another kind than its field's on purpose: chr_move_to_pad
	// reads its pad's low byte as a chr when its mode byte is 88, and
	// play_sound_from_entity's entity is a chr or an object tag by its
	// entity_is_chr byte (so its kind is number).
	const std::set<std::tuple<uint16_t, int, std::string>> other_use = {
		{0x00e2, 4, "chr"},
		{0x0179, 3, "chr"},
		{0x0179, 3, "object"},
	};

	uint32_t checked = 0;
	for (const auto &[opcode, handler] : pointers) {
		if (handler == "NULL" || opcode == AI_COMMAND_OP_PRINT) {
			continue;
		}
		const ai_command_schema_t *schema = aiCommandSchemaFind(opcode);
		REQUIRE(schema != nullptr);
		const auto body = bodies.find(handler);
		if (body == bodies.end()) {
			continue;
		}
		INFO("opcode 0x" << std::hex << opcode << " " << handler);
		// Every byte the handler reads is a field it uses, not an unused one,
		// except a value read and then dropped: warp_jo_to_tag passes bytes
		// 5-6 to playerPrepareWarpType2, which stores them in
		// g_WarpType2Arg2, and nothing reads that.
		const std::set<std::pair<uint16_t, int>> read_but_dropped = {
			{0x00df, 5}, {0x00df, 6},
		};
		for (int n : bytesRead(body->second)) {
			if (n < 2 || read_but_dropped.count({opcode, n})) {
				continue;
			}
			INFO("cmd[" << std::dec << n << "]");
			REQUIRE(n < schema->length);
			const ai_command_field_t *field = fieldAt(schema, n);
			REQUIRE(field != nullptr);
			REQUIRE(field->kind != AI_OPERAND_UNUSED);
		}
		// Bytes passed to the lookups are fields of the lookup's kind.
		const std::vector<std::pair<std::string, ai_operand_kind_t>> lookups = {
			{"chraiGoToLabel", AI_OPERAND_LABEL},
			{"chrFindById", AI_OPERAND_CHR},
			{"objFindByTagId", AI_OPERAND_OBJECT},
		};
		for (const auto &[call, kind] : lookups) {
			for (int n : bytesPassedTo(body->second, call)) {
				INFO(call << "(cmd[" << std::dec << n << "])");
				const ai_command_field_t *field = fieldAt(schema, n);
				REQUIRE(field != nullptr);
				if (other_use.count({opcode, n, kindName(kind)})) {
					continue;
				}
				REQUIRE(kindName(field->kind) == kindName(kind));
				REQUIRE(field->width == 1);
			}
		}
		checked++;
	}
	REQUIRE(checked > 400);
}

TEST_CASE("AI command schema: reading and writing a field", "[ai][schema]")
{
	uint8_t bytes[6] = {0};
	const ai_command_field_t u16 = {"value", 3, 2, 0, AI_OPERAND_NUMBER};
	const ai_command_field_t s8 = {"delta", 2, 1, 1, AI_OPERAND_NUMBER};
	const ai_command_field_t u24 = {"ticks", 5, 3, 0, AI_OPERAND_NUMBER};
	int64_t value = 0;

	// Big-endian, at the field's offset from the opcode.
	REQUIRE(aiCommandFieldWrite(&u16, bytes, sizeof(bytes), 0x1234) == 1);
	REQUIRE(bytes[1] == 0x12);
	REQUIRE(bytes[2] == 0x34);
	REQUIRE(aiCommandFieldRead(&u16, bytes, sizeof(bytes), &value) == 1);
	REQUIRE(value == 0x1234);

	// Two's complement; out of range is refused and changes nothing.
	REQUIRE(aiCommandFieldWrite(&s8, bytes, sizeof(bytes), -1) == 1);
	REQUIRE(bytes[0] == 0xff);
	REQUIRE(aiCommandFieldRead(&s8, bytes, sizeof(bytes), &value) == 1);
	REQUIRE(value == -1);
	REQUIRE(aiCommandFieldWrite(&s8, bytes, sizeof(bytes), 128) == 0);
	REQUIRE(aiCommandFieldWrite(&s8, bytes, sizeof(bytes), -129) == 0);
	REQUIRE(bytes[0] == 0xff);

	int64_t min = 0;
	int64_t max = 0;
	aiCommandFieldRange(&u24, &min, &max);
	REQUIRE(min == 0);
	REQUIRE(max == 0xffffff);
	REQUIRE(aiCommandFieldWrite(&u24, bytes, sizeof(bytes), 0x1000000) == 0);
	REQUIRE(aiCommandFieldWrite(&u24, bytes, sizeof(bytes), 0xabcdef) == 1);
	REQUIRE(aiCommandFieldRead(&u24, bytes, sizeof(bytes), &value) == 1);
	REQUIRE(value == 0xabcdef);

	// A field past the operand bytes does not fit.
	REQUIRE(aiCommandFieldRead(&u24, bytes, 5, &value) == 0);
	REQUIRE(aiCommandFieldWrite(&u24, bytes, 5, 1) == 0);

	// A real command: if_chr_dead 0x24, label 6.
	const ai_command_schema_t *dead = aiCommandSchemaFind(0x0033);
	REQUIRE(dead != nullptr);
	REQUIRE(std::string(dead->name) == "if_chr_dead");
	const uint8_t operands[2] = {0x24, 0x06};
	REQUIRE(aiCommandFieldRead(aiCommandSchemaField(dead, "chr"), operands, 2, &value) == 1);
	REQUIRE(value == 0x24);
	REQUIRE(aiCommandFieldRead(aiCommandSchemaField(dead, "label"), operands, 2, &value) == 1);
	REQUIRE(value == 6);
	REQUIRE(aiCommandSchemaField(dead, "chr")->kind == AI_OPERAND_CHR);
	REQUIRE(aiCommandSchemaField(dead, "label")->kind == AI_OPERAND_LABEL);

	// Kind names round-trip.
	for (int kind = 0; kind < AI_OPERAND_KIND_COUNT; kind++) {
		REQUIRE(aiOperandKindFromName(aiOperandKindName((ai_operand_kind_t)kind)) == kind);
	}
	REQUIRE(aiOperandKindFromName("nope") == -1);
}

TEST_CASE("AI command schema: the conformance tool carries the same table",
	"[ai][schema][static]")
{
	const std::string conformance = readSource("tools/asset_archive_conformance.py");
	const size_t start = conformance.find("AI_COMMAND_SCHEMA = {");
	REQUIRE(start != std::string::npos);
	const size_t end = conformance.find("\n}", start);
	REQUIRE(end != std::string::npos);
	const std::string block = conformance.substr(start, end - start);
	static const std::regex line(R"re(0x([0-9a-f]{4}): \("(\w+)", (\d+), \((.*)\)\),)re");
	static const std::regex field(R"re(\("(\w+)", (\d+), (\d+), (True|False), "(\w+)"\))re");

	uint32_t count = 0;
	for (std::sregex_iterator it(block.begin(), block.end(), line), last; it != last; ++it) {
		const uint16_t opcode = (uint16_t)std::stoul((*it)[1].str(), nullptr, 16);
		INFO("opcode 0x" << std::hex << opcode);
		const ai_command_schema_t *schema = aiCommandSchemaFind(opcode);
		REQUIRE(schema != nullptr);
		REQUIRE((*it)[2].str() == schema->name);
		REQUIRE(std::stoi((*it)[3].str()) == schema->length);
		const std::string fields = (*it)[4].str();
		uint32_t f = 0;
		for (std::sregex_iterator ft(fields.begin(), fields.end(), field), flast; ft != flast; ++ft) {
			REQUIRE(f < schema->field_count);
			const ai_command_field_t &c = schema->fields[f];
			INFO("field " << c.name);
			REQUIRE((*ft)[1].str() == c.name);
			REQUIRE(std::stoi((*ft)[2].str()) == c.offset);
			REQUIRE(std::stoi((*ft)[3].str()) == c.width);
			REQUIRE(((*ft)[4].str() == "True") == (c.is_signed != 0));
			REQUIRE((*ft)[5].str() == kindName(c.kind));
			f++;
		}
		REQUIRE(f == schema->field_count);
		count++;
	}
	REQUIRE(count == aiCommandSchemaCount());
}
