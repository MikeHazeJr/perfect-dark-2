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

TEST_CASE("AI command schema: arguments round-trip through JSON for every command",
	"[ai][schema][args]")
{
	char json[512];
	uint8_t bytes[64];
	uint8_t back[64];
	char error[160];

	for (uint32_t i = 0; i < aiCommandSchemaCount(); i++) {
		const ai_command_schema_t *schema = aiCommandSchemaAt(i);
		INFO("opcode 0x" << std::hex << schema->opcode << " " << schema->name);
		if (schema->opcode == AI_COMMAND_OP_PRINT) {
			continue;
		}
		const uint32_t count = (uint32_t)schema->length - 2u;
		REQUIRE(count <= sizeof(bytes));
		// Every field at a value inside its range: the top value, then its
		// smallest; an unused field zero, then not.
		for (int pass = 0; pass < 2; pass++) {
			std::memset(bytes, 0, sizeof(bytes));
			for (uint32_t f = 0; f < schema->field_count; f++) {
				const ai_command_field_t *field = &schema->fields[f];
				int64_t min = 0;
				int64_t max = 0;
				aiCommandFieldRange(field, &min, &max);
				int64_t value = pass == 0 ? max : min;
				if (field->kind == AI_OPERAND_UNUSED) {
					value = pass;
				}
				REQUIRE(aiCommandFieldWrite(field, bytes, count, value) == 1);
			}
			const int len = aiCommandArgsToJson(schema, bytes, count, json, sizeof(json));
			REQUIRE(len > 0);
			INFO(json);
			std::memset(back, 0xaa, sizeof(back));
			REQUIRE(aiCommandArgsEncode(schema, json, (uint32_t)len, back, sizeof(back),
				error, sizeof(error)) == (int)count);
			REQUIRE(std::memcmp(bytes, back, count) == 0);
		}
	}
}

TEST_CASE("AI command schema: arguments as authors write them", "[ai][schema][args]")
{
	char json[256];
	uint8_t bytes[64];
	char error[160];
	const ai_command_schema_t *dead = aiCommandSchemaFind(0x0033);
	REQUIRE(dead != nullptr);

	// if_chr_dead: chr 0x24, label 6, as the extractor writes it.
	const uint8_t operands[2] = {0x24, 0x06};
	REQUIRE(aiCommandArgsToJson(dead, operands, 2, json, sizeof(json)) > 0);
	REQUIRE(std::string(json) == "{\"chr\": 36, \"label\": 6}");
	// Any order and spacing.
	const std::string reordered = " { \"label\" : 6 ,\n \"chr\": 36 } ";
	REQUIRE(aiCommandArgsEncode(dead, reordered.c_str(), (uint32_t)reordered.size(), bytes,
		sizeof(bytes), error, sizeof(error)) == 2);
	REQUIRE(bytes[0] == 0x24);
	REQUIRE(bytes[1] == 0x06);

	// Refusals, each with its reason.
	const std::vector<std::pair<std::string, std::string>> refused = {
		{"{\"chr\": 36}", "missing argument 'label'"},
		{"{\"chr\": 36, \"label\": 6, \"pad\": 1}", "no argument 'pad'"},
		{"{\"chr\": 36, \"label\": 256}", "outside its 1-byte unsigned range"},
		{"{\"chr\": -1, \"label\": 6}", "outside its 1-byte unsigned range"},
		{"{\"chr\": 36, \"chr\": 36, \"label\": 6}", "appears twice"},
		{"{\"chr\": \"bond\", \"label\": 6}", "must be a whole number"},
		{"{\"chr\": 36, \"label\": 6", "not a closed object"},
		{"[36, 6]", "not an object"},
		{"{\"chr\": 36, \"label\": 6} x", "text after the object"},
	};
	for (const auto &[text, reason] : refused) {
		INFO(text);
		REQUIRE(aiCommandArgsEncode(dead, text.c_str(), (uint32_t)text.size(), bytes,
			sizeof(bytes), error, sizeof(error)) == -1);
		REQUIRE(std::string(error).find(reason) != std::string::npos);
	}

	// print: its text with escapes, NUL-terminated in the bytes.
	const ai_command_schema_t *print = aiCommandSchemaFind(AI_COMMAND_OP_PRINT);
	REQUIRE(print != nullptr);
	const uint8_t text[] = {'s', 'a', 'y', ' ', '"', 'h', 'i', '"', 0x5c, 0x01, 0};
	const int len = aiCommandArgsToJson(print, text, sizeof(text), json, sizeof(json));
	REQUIRE(len > 0);
	// The quotes and the backslash escaped, byte 0x01 as a u-escape.
	const std::string expected = std::string("{\"text\": \"say ") + '\\' + "\"hi" + '\\' + "\""
		+ '\\' + '\\' + '\\' + "u0001\"}";
	REQUIRE(std::string(json) == expected);
	REQUIRE(aiCommandArgsEncode(print, json, (uint32_t)len, bytes, sizeof(bytes), error,
		sizeof(error)) == (int)sizeof(text));
	REQUIRE(std::memcmp(bytes, text, sizeof(text)) == 0);
	// A text without its NUL, or with one inside, does not fit.
	REQUIRE(aiCommandArgsToJson(print, text, sizeof(text) - 1, json, sizeof(json)) == -1);
	const uint8_t inner[] = {'a', 0, 'b', 0};
	REQUIRE(aiCommandArgsToJson(print, inner, sizeof(inner), json, sizeof(json)) == -1);

	// A bool field is written true or false; an unused field only when set.
	const ai_command_schema_t *warp = aiCommandSchemaFind(0x00df);
	REQUIRE(warp != nullptr);
	const uint8_t warp_bytes[5] = {0x07, 0x00, 0x01, 0x00, 0x00};
	REQUIRE(aiCommandArgsToJson(warp, warp_bytes, 5, json, sizeof(json)) > 0);
	REQUIRE(std::string(json) == "{\"object\": 7, \"has_direction\": true}");
	const uint8_t warp_set[5] = {0x07, 0x00, 0x00, 0x12, 0x34};
	REQUIRE(aiCommandArgsToJson(warp, warp_set, 5, json, sizeof(json)) > 0);
	REQUIRE(std::string(json) == "{\"object\": 7, \"has_direction\": false, \"unused\": 4660}");
}

TEST_CASE("AI command schema: rows and nodes carry args, the compiler checks them",
	"[ai][schema][args][static]")
{
	const std::string extractor = readSource("port/src/romextract_pdarena.c");
	const std::string bank = readSource("port/src/romextract_pdai.c");
	const std::string runtime = readSource("port/src/scenario_source_runtime.c");
	const std::string conformance = readSource("tools/asset_archive_conformance.py");

	// The extractor writes args after the operands of every stage and bank
	// row, and on every stage and bank command node, from the schema.
	REQUIRE(extractor.find("#include \"ai_command_schema.h\"") != std::string::npos);
	REQUIRE(extractor.find("static s32 s_appendAiArgsJson(pdscenario_textbuf_t *out, const u8 *cmd, u32 len)")
		!= std::string::npos);
	REQUIRE(extractor.find("aiCommandArgsToJson(schema, cmd + 2, len - 2, args,") != std::string::npos);
	REQUIRE(extractor.find("s_appendAiOperandsJsonArray(ai_lists_json, cmd, len) != 0 ||\n"
		"\t\t\t\t\ts_appendAiArgsJson(ai_lists_json, cmd, len) != 0 ||") != std::string::npos);
	REQUIRE(extractor.find("s_appendAiArgsJson(ai_command_nodes_json, cmd, len) != 0 ||") != std::string::npos);
	REQUIRE(extractor.find("s_appendAiOperandsJsonArray(&json, cmd, len) != 0 ||\n"
		"\t\t\t\t\ts_appendAiArgsJson(&json, cmd, len) != 0 ||") != std::string::npos);
	REQUIRE(extractor.find("s_appendAiArgsJson(&nodes, g_GlobalAilists[i].list + scan.offsets[c],")
		!= std::string::npos);
	// SP-86: the scenario kind and the bank's version moved with the output.
	REQUIRE(extractor.find("_aiedges_simtwins_typedargs\"") != std::string::npos);
	REQUIRE(bank.find("#define PDAI_EXTRACT_VERSION \"global_ailists_rows_v3_typedargs\"") != std::string::npos);

	// The compiler encodes a row's args and compares them with its operands
	// before the catalog ids patch the bytes.
	// The row's bytes come from its operands, its args or both (which must
	// agree), and only then do the catalog ids patch them.
	const size_t parse = runtime.find("static s32 s_aiRowOperands(scenario_source_ai_table_t *table,");
	const size_t check = runtime.find("\t\tif (!s_aiRowOperands(table, ailist_ref, command_index,");
	const size_t patch = runtime.find("!s_aiApplyCatalogOperands(table, &cmd, model_id,");
	REQUIRE(parse != std::string::npos);
	REQUIRE(check != std::string::npos);
	REQUIRE(patch != std::string::npos);
	REQUIRE(parse < check);
	REQUIRE(check < patch);
	REQUIRE(runtime.find("count = aiCommandArgsEncode(schema, args, (u32)(args_end - args + 1), bytes,")
		!= std::string::npos);
	REQUIRE(runtime.find("\"AI row '%s' args do not give its operands%s\"") != std::string::npos);
	REQUIRE(runtime.find("\"AI row '%s' has neither operands nor args%s\"") != std::string::npos);
	// Args alone are the row's bytes.
	REQUIRE(runtime.find("\t/* Args alone: they are the row's bytes. */\n"
		"\tif (count > 0) {\n"
		"\t\tcmd->operands = (u8 *)malloc((size_t)count);") != std::string::npos);
	REQUIRE(runtime.find("s_aiRowArgsMatchOperands") == std::string::npos);
	REQUIRE(conformance.find("def ai_command_encode(opcode: int, args: object) -> tuple[list[int] | None, str | None]:")
		!= std::string::npos);
	REQUIRE(conformance.find("def ai_row_operand_bytes(row: dict) -> list[int] | None:") != std::string::npos);

	// Conformance recomputes the args from the operands, compares nodes with
	// rows, and requires args with the new kind and the v3 bank.
	REQUIRE(conformance.find("def ai_command_args(opcode: int, operands: list[int]) -> dict | None:")
		!= std::string::npos);
	REQUIRE(conformance.find("def ai_args_node_errors(rows: object, nodes: object, require: bool) -> list[str]:")
		!= std::string::npos);
	REQUIRE(conformance.find("PDAI_TYPED_ARGS_EXTRACT_VERSION = \"global_ailists_rows_v3_typedargs\"")
		!= std::string::npos);
	REQUIRE(conformance.find("if extract_version == PDAI_TYPED_ARGS_EXTRACT_VERSION:") != std::string::npos);
}
