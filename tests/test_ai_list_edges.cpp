/*
 * tests/test_ai_list_edges.cpp -- AI lists as graphs, slice 1
 * (context/designs/modding/ai-list-graphs.md): the control flow of a list as
 * edges between its commands (port/src/ai_list_edges.c), the branching table
 * re-derived from the native handlers, the conformance tool's copy of it,
 * and pins on the extractor and the conformance check that use them.
 */

#include "catch.hpp"

extern "C" {
#include "ai_list_edges.h"
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

struct Cmd {
	uint16_t op;
	std::vector<uint8_t> operands;
};

using Edge = std::tuple<uint32_t, uint32_t, std::string>;

struct Flow {
	ai_list_edges_result_t result = AI_LIST_EDGES_OK;
	std::vector<Edge> edges;
	std::vector<uint8_t> flags;
	uint32_t bad_command = 0;
};

Flow computeFlow(const std::vector<Cmd> &list)
{
	std::vector<ai_list_command_t> commands;
	for (const Cmd &cmd : list) {
		ai_list_command_t command;
		command.opcode = cmd.op;
		command.operands = cmd.operands.empty() ? nullptr : cmd.operands.data();
		command.operand_len = (uint32_t)cmd.operands.size();
		commands.push_back(command);
	}
	std::vector<ai_list_edge_t> edges(list.size() * AI_LIST_EDGES_PER_COMMAND + 1);
	Flow flow;
	uint32_t edge_count = 0;
	flow.flags.assign(list.size(), 0xff);
	flow.result = aiListEdgesCompute(commands.data(), (uint32_t)commands.size(),
		edges.data(), (uint32_t)edges.size(), &edge_count, flow.flags.data(),
		&flow.bad_command);
	for (uint32_t i = 0; i < edge_count; i++) {
		const char *kind = aiListEdgeKindName(edges[i].kind);
		flow.edges.emplace_back(edges[i].from, edges[i].to, kind ? kind : "?");
	}
	return flow;
}

bool has(const Flow &flow, uint32_t node, uint8_t flag)
{
	return node < flow.flags.size() && (flow.flags[node] & flag) != 0;
}

// Opcodes the cases use (src/game/chrai.c g_CommandPointers).
const uint16_t GO_TO_NEXT = AI_LIST_OP_GO_TO_NEXT;
const uint16_t GO_TO_FIRST = AI_LIST_OP_GO_TO_FIRST;
const uint16_t LABEL = AI_LIST_OP_LABEL;
const uint16_t YIELD = AI_LIST_OP_YIELD;
const uint16_t END = AI_LIST_OP_END;
const uint16_t SET_LIST = AI_LIST_OP_SET_LIST;
const uint16_t RETURN = AI_LIST_OP_RETURN;
const uint16_t IF_IDLE = 0x000c;             // cmd[2] = label
const uint16_t IF_RANDOM_LESS_THAN = 0x0037; // cmd[2] = value, cmd[3] = label
const uint16_t IF_TELEPORT_WHITE = 0x01c0;   // cmd[2] = label, cmd[3] = chr
const uint16_t RESTART_TIMER = 0x00b6;
const uint16_t IF_TIMER_GREATER_THAN = 0x00bd; // cmd[5] = label

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

std::map<uint16_t, uint8_t> branchTable()
{
	std::map<uint16_t, uint8_t> table;
	for (uint32_t i = 0; i < aiListEdgesBranchOpcodeCount(); i++) {
		uint16_t opcode = 0;
		uint8_t label_byte = 0;
		REQUIRE(aiListEdgesBranchOpcodeAt(i, &opcode, &label_byte) == 1);
		table[opcode] = label_byte;
	}
	return table;
}

// g_CommandPointers or g_CommandLengths as opcode -> entry text, keeping the
// first entry of an opcode (the NTSC 1.0+ side of an #if/#else, which the
// port builds).
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

// Every handler: a line "bool <name>(...)" at column 0 followed by a line
// opening with "{"; the body runs to the first line that is exactly "}".
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
		INFO("handler " << name);
		REQUIRE(out.count(name) == 0);
		out[name] = src.substr(line_end + 1, body_end - line_end);
		pos = body_end;
	}
}

struct LabelCall {
	std::string offset;
	int label_byte;
};

// The chraiGoToLabel calls of a handler body, each as (offset argument,
// label byte n of cmd[n] or cmd->bn). Any other shape fails the case, so a
// new form of call is looked at rather than skipped.
std::vector<LabelCall> labelCalls(const std::string &body)
{
	static const std::regex call(
		R"(^g_Vars\.ailist,(g_Vars\.aioffset|0),cmd(?:\[(\d+)\]|->b(\d+))$)");
	std::vector<LabelCall> calls;
	size_t pos = 0;
	while ((pos = body.find("chraiGoToLabel(", pos)) != std::string::npos) {
		const size_t open = pos + std::strlen("chraiGoToLabel(");
		const size_t close = body.find(')', open);
		REQUIRE(close != std::string::npos);
		std::string args;
		for (char c : body.substr(open, close - open)) {
			if (!std::isspace((unsigned char)c)) {
				args += c;
			}
		}
		std::smatch match;
		INFO("chraiGoToLabel(" << args << ")");
		REQUIRE(std::regex_match(args, match, call));
		calls.push_back({match[1].str(),
			std::stoi(match[2].matched ? match[2].str() : match[3].str())});
		pos = close;
	}
	return calls;
}

} // namespace

TEST_CASE("AI list edges: next edges, a forward branch, gotos and yields",
	"[ai][edges]")
{
	// Defection's list 0x0401, as its rows hold it.
	const std::vector<Cmd> list = {
		{RESTART_TIMER, {}},
		{0x000b, {0x00, 0x66, 0x00, 0x00, 0x00, 0x01, 0x04, 0x10, 0xfd, 0x02}},
		{LABEL, {0x08}},
		{YIELD, {}},
		{IF_TIMER_GREATER_THAN, {0x00, 0x00, 0x00, 0x2c}},
		{GO_TO_FIRST, {0x08}},
		{LABEL, {0x2c}},
		{0x000b, {0x00, 0x66, 0x00, 0x01, 0xff, 0xff, 0x04, 0x10, 0xfd, 0x02}},
		{LABEL, {0x85}},
		{YIELD, {}},
		{GO_TO_FIRST, {0x85}},
		{SET_LIST, {0xfd, 0x00, 0x00}},
		{END, {}},
	};
	const Flow flow = computeFlow(list);
	REQUIRE(flow.result == AI_LIST_EDGES_OK);
	const std::vector<Edge> expected = {
		{0, 1, "next"}, {1, 2, "next"}, {2, 3, "next"}, {3, 4, "next"},
		{4, 5, "next"}, {4, 6, "branch"}, {5, 2, "goto"}, {6, 7, "next"},
		{7, 8, "next"}, {8, 9, "next"}, {9, 10, "next"}, {10, 8, "goto"},
	};
	REQUIRE(flow.edges == expected);
	REQUIRE(has(flow, 3, AI_LIST_NODE_YIELDS));
	REQUIRE(has(flow, 9, AI_LIST_NODE_YIELDS));
	REQUIRE(has(flow, 11, AI_LIST_NODE_SWITCHES_LIST));
	REQUIRE(has(flow, 12, AI_LIST_NODE_ENDS));
	for (uint32_t i = 0; i <= 10; i++) {
		INFO("command " << i);
		REQUIRE(has(flow, i, AI_LIST_NODE_REACHABLE));
	}
	// After the final go_to_first nothing reaches the set_list and the end.
	REQUIRE_FALSE(has(flow, 11, AI_LIST_NODE_REACHABLE));
	REQUIRE_FALSE(has(flow, 12, AI_LIST_NODE_REACHABLE));
	REQUIRE(flow.flags[4] == AI_LIST_NODE_REACHABLE);
}

TEST_CASE("AI list edges: repeated label numbers resolve by position",
	"[ai][edges]")
{
	// Label 5 at 0 and 3: a branch or go_to_next at 1 or 2 finds the one at
	// 3 (at or after itself); go_to_first finds the one at 0.
	const Flow flow = computeFlow({
		{LABEL, {5}},
		{IF_IDLE, {5}},
		{GO_TO_NEXT, {5}},
		{LABEL, {5}},
		{GO_TO_FIRST, {5}},
		{END, {}},
	});
	REQUIRE(flow.result == AI_LIST_EDGES_OK);
	const std::vector<Edge> expected = {
		{0, 1, "next"}, {1, 2, "next"}, {1, 3, "branch"}, {2, 3, "goto"},
		{3, 4, "next"}, {4, 0, "goto"},
	};
	REQUIRE(flow.edges == expected);
	REQUIRE(has(flow, 5, AI_LIST_NODE_ENDS));
	REQUIRE_FALSE(has(flow, 5, AI_LIST_NODE_REACHABLE));
}

TEST_CASE("AI list edges: a label that is not found restarts the list",
	"[ai][edges]")
{
	const Flow flow = computeFlow({
		{YIELD, {}},
		{IF_IDLE, {7}},     // no label 7: restart
		{GO_TO_NEXT, {9}},  // no label 9: restart
		{LABEL, {3}},
		{GO_TO_FIRST, {4}}, // no label 4 from the top either
		{END, {}},
	});
	REQUIRE(flow.result == AI_LIST_EDGES_OK);
	const std::vector<Edge> expected = {
		{0, 1, "next"}, {1, 2, "next"}, {1, 0, "restart"}, {2, 0, "restart"},
		{3, 4, "next"}, {4, 0, "restart"},
	};
	REQUIRE(flow.edges == expected);
	REQUIRE(has(flow, 0, AI_LIST_NODE_YIELDS));
	REQUIRE_FALSE(has(flow, 3, AI_LIST_NODE_REACHABLE));
	REQUIRE_FALSE(has(flow, 5, AI_LIST_NODE_REACHABLE));
}

TEST_CASE("AI list edges: a branch scans forward only, go_to_first from the top",
	"[ai][edges]")
{
	// Label 3 stands before the branch and the go_to_next: chraiGoToLabel
	// scans from their own offset and reaches the end first. go_to_first
	// jumps back to it.
	const Flow flow = computeFlow({
		{LABEL, {3}},
		{IF_IDLE, {3}},
		{GO_TO_NEXT, {3}},
		{LABEL, {6}},
		{GO_TO_FIRST, {3}},
		{END, {}},
	});
	REQUIRE(flow.result == AI_LIST_EDGES_OK);
	const std::vector<Edge> expected = {
		{0, 1, "next"}, {1, 2, "next"}, {1, 0, "restart"}, {2, 0, "restart"},
		{3, 4, "next"}, {4, 0, "goto"},
	};
	REQUIRE(flow.edges == expected);

	// go_to_next jumps forward past commands it skips.
	const Flow forward = computeFlow({
		{GO_TO_NEXT, {2}},
		{YIELD, {}},
		{LABEL, {2}},
		{END, {}},
	});
	REQUIRE(forward.result == AI_LIST_EDGES_OK);
	const std::vector<Edge> forward_expected = {
		{0, 2, "goto"}, {1, 2, "next"}, {2, 3, "next"},
	};
	REQUIRE(forward.edges == forward_expected);
	REQUIRE_FALSE(has(forward, 1, AI_LIST_NODE_REACHABLE));
	REQUIRE(has(forward, 3, AI_LIST_NODE_REACHABLE));
}

TEST_CASE("AI list edges: end, return and set_list leave the walk",
	"[ai][edges]")
{
	// set_list on another chr continues; on CHR_SELF the list switches.
	const Flow flow = computeFlow({
		{SET_LIST, {0x05, 0x04, 0x01}},
		{YIELD, {}},
		{IF_IDLE, {1}},
		{RETURN, {}},
		{LABEL, {1}},
		{SET_LIST, {AI_LIST_CHR_SELF, 0x04, 0x02}},
		{END, {}},
	});
	REQUIRE(flow.result == AI_LIST_EDGES_OK);
	const std::vector<Edge> expected = {
		{0, 1, "next"}, {1, 2, "next"}, {2, 3, "next"}, {2, 4, "branch"},
		{4, 5, "next"},
	};
	REQUIRE(flow.edges == expected);
	REQUIRE(flow.flags[0] == AI_LIST_NODE_REACHABLE);
	REQUIRE(has(flow, 1, AI_LIST_NODE_YIELDS));
	REQUIRE(has(flow, 3, AI_LIST_NODE_RETURNS));
	REQUIRE(has(flow, 3, AI_LIST_NODE_REACHABLE));
	REQUIRE(has(flow, 5, AI_LIST_NODE_SWITCHES_LIST));
	REQUIRE(has(flow, 6, AI_LIST_NODE_ENDS));
	REQUIRE_FALSE(has(flow, 6, AI_LIST_NODE_REACHABLE));
}

TEST_CASE("AI list edges: the label is the last operand byte, cmd[2] for 0x01c0",
	"[ai][edges]")
{
	// if_random_less_than reads its label from its last byte (cmd[3]), not
	// from the value before it.
	const Flow random = computeFlow({
		{IF_RANDOM_LESS_THAN, {9, 2}},
		{LABEL, {9}},
		{LABEL, {2}},
		{END, {}},
	});
	REQUIRE(random.result == AI_LIST_EDGES_OK);
	REQUIRE(std::find(random.edges.begin(), random.edges.end(),
		Edge{0, 2, "branch"}) != random.edges.end());

	// if_chr_teleport_full_white reads it from cmd[2]; cmd[3] is the chr.
	const Flow white = computeFlow({
		{IF_TELEPORT_WHITE, {4, 7}},
		{YIELD, {}},
		{LABEL, {7}},
		{LABEL, {4}},
		{END, {}},
	});
	REQUIRE(white.result == AI_LIST_EDGES_OK);
	const std::vector<Edge> expected = {
		{0, 1, "next"}, {0, 3, "branch"}, {1, 2, "next"}, {2, 3, "next"},
		{3, 4, "next"},
	};
	REQUIRE(white.edges == expected);
	REQUIRE(aiListEdgesBranchLabelByte(IF_TELEPORT_WHITE) == 2);
	REQUIRE(aiListEdgesBranchLabelByte(IF_RANDOM_LESS_THAN) == 3);
}

TEST_CASE("AI list edges: refusals and the table's shape", "[ai][edges]")
{
	// A branching command without its label byte is refused, by index.
	const Flow short_branch = computeFlow({{YIELD, {}}, {IF_RANDOM_LESS_THAN, {9}}, {END, {}}});
	REQUIRE(short_branch.result == AI_LIST_EDGES_MISSING_OPERAND);
	REQUIRE(short_branch.bad_command == 1);
	REQUIRE(computeFlow({{GO_TO_FIRST, {}}}).result == AI_LIST_EDGES_MISSING_OPERAND);
	REQUIRE(computeFlow({{SET_LIST, {}}}).result == AI_LIST_EDGES_MISSING_OPERAND);

	// Too small an edge buffer is refused; an empty list has no edges.
	const Cmd yield = {YIELD, {}};
	ai_list_command_t commands[2] = {
		{yield.op, nullptr, 0}, {END, nullptr, 0},
	};
	ai_list_edge_t one_edge[1];
	uint8_t flags[2];
	uint32_t edge_count = 99;
	REQUIRE(aiListEdgesCompute(commands, 2, one_edge, 1, &edge_count, flags,
		nullptr) == AI_LIST_EDGES_OK);
	REQUIRE(edge_count == 1);
	REQUIRE(aiListEdgesCompute(commands, 2, one_edge, 0, &edge_count, flags,
		nullptr) == AI_LIST_EDGES_NO_ROOM);
	REQUIRE(aiListEdgesCompute(nullptr, 0, nullptr, 0, &edge_count, nullptr,
		nullptr) == AI_LIST_EDGES_OK);
	REQUIRE(edge_count == 0);
	REQUIRE(aiListEdgesCompute(nullptr, 2, one_edge, 1, &edge_count, flags,
		nullptr) == AI_LIST_EDGES_INVALID_ARGUMENT);

	REQUIRE(std::string(aiListEdgeKindName(AI_LIST_EDGE_NEXT)) == "next");
	REQUIRE(std::string(aiListEdgeKindName(AI_LIST_EDGE_BRANCH)) == "branch");
	REQUIRE(std::string(aiListEdgeKindName(AI_LIST_EDGE_GOTO)) == "goto");
	REQUIRE(std::string(aiListEdgeKindName(AI_LIST_EDGE_RESTART)) == "restart");

	// The table is in strictly ascending opcode order (binary search), holds
	// no interpreter opcode and every label byte is an operand byte.
	uint16_t previous = 0;
	for (uint32_t i = 0; i < aiListEdgesBranchOpcodeCount(); i++) {
		uint16_t opcode = 0;
		uint8_t label_byte = 0;
		REQUIRE(aiListEdgesBranchOpcodeAt(i, &opcode, &label_byte) == 1);
		INFO("opcode 0x" << std::hex << opcode);
		REQUIRE((i == 0 || opcode > previous));
		REQUIRE(opcode > AI_LIST_OP_RETURN);
		REQUIRE(label_byte >= 2);
		REQUIRE(aiListEdgesBranchLabelByte(opcode) == label_byte);
		previous = opcode;
	}
	REQUIRE(aiListEdgesBranchOpcodeAt(aiListEdgesBranchOpcodeCount(), nullptr,
		nullptr) == 0);
	REQUIRE(aiListEdgesBranchLabelByte(GO_TO_NEXT) == -1);
	REQUIRE(aiListEdgesBranchLabelByte(LABEL) == -1);
	REQUIRE(aiListEdgesBranchLabelByte(RESTART_TIMER) == -1);
	REQUIRE(aiListEdgesBranchLabelByte(0x0176) == 2); // aiIfBotRespawning
}

TEST_CASE("AI list edges: the branching table is the handlers that jump to a label",
	"[ai][edges][static]")
{
	const std::string chrai = readSource("src/game/chrai.c");
	std::map<std::string, std::string> bodies;
	collectHandlerBodies(readSource("src/game/chraicommands.c"), bodies);
	collectHandlerBodies(readSource("src/game/mplayer/mpaicommands.c"), bodies);
	const std::map<uint16_t, std::string> pointers =
		commandTable(chrai, "bool (*g_CommandPointers[])(void) = {");
	const std::map<uint16_t, std::string> lengths =
		commandTable(chrai, "u16 g_CommandLengths[] = {");
	REQUIRE(pointers.size() > 0x01d0);

	std::map<uint16_t, uint8_t> derived;
	for (const auto &[opcode, handler] : pointers) {
		if (handler == "NULL") {
			continue;
		}
		INFO("opcode 0x" << std::hex << opcode << " " << handler);
		const auto body = bodies.find(handler);
		REQUIRE(body != bodies.end());
		const std::vector<LabelCall> calls = labelCalls(body->second);
		if (opcode == GO_TO_NEXT || opcode == GO_TO_FIRST) {
			// The interpreter's own gotos: from the command, or from the top.
			REQUIRE(calls.size() == 1);
			REQUIRE(calls[0].offset == (opcode == GO_TO_NEXT ? "g_Vars.aioffset" : "0"));
			REQUIRE(calls[0].label_byte == 2);
			continue;
		}
		if (calls.empty()) {
			continue;
		}
		for (const LabelCall &call : calls) {
			REQUIRE(call.offset == "g_Vars.aioffset");
			REQUIRE(call.label_byte == calls[0].label_byte);
		}
		// The label is a byte of the command (g_CommandLengths).
		REQUIRE(lengths.count(opcode) == 1);
		const std::string length_text = lengths.at(opcode);
		const int length = length_text.find("VERSION") != std::string::npos
			? 3 : std::stoi(length_text);
		REQUIRE(calls[0].label_byte < length);
		derived[(uint16_t)opcode] = (uint8_t)calls[0].label_byte;
	}

	const std::map<uint16_t, uint8_t> table = branchTable();
	for (const auto &[opcode, label_byte] : derived) {
		INFO("handler opcode 0x" << std::hex << opcode << " label cmd[" << std::dec
			<< (int)label_byte << "]");
		REQUIRE(table.count(opcode) == 1);
		REQUIRE(table.at(opcode) == label_byte);
	}
	for (const auto &[opcode, label_byte] : table) {
		INFO("table opcode 0x" << std::hex << opcode);
		REQUIRE(derived.count(opcode) == 1);
	}
	// 158 if_* conditions, if_bot_respawning and 56 that act and then branch.
	REQUIRE(table.size() == 215);
}

TEST_CASE("AI list edges: the conformance tool's table equals the C table",
	"[ai][edges][static]")
{
	const std::string conformance = readSource("tools/asset_archive_conformance.py");
	const size_t start = conformance.find("AI_LIST_BRANCH_LABEL_BYTE = {");
	REQUIRE(start != std::string::npos);
	const size_t end = conformance.find("\n}", start);
	REQUIRE(end != std::string::npos);
	const std::string block = conformance.substr(start, end - start);
	static const std::regex entry(R"(0x([0-9a-fA-F]{4}):\s*(\d+))");
	std::map<uint16_t, uint8_t> python;
	for (std::sregex_iterator it(block.begin(), block.end(), entry), last;
			it != last; ++it) {
		const uint16_t opcode = (uint16_t)std::stoul((*it)[1].str(), nullptr, 16);
		REQUIRE(python.count(opcode) == 0);
		python[opcode] = (uint8_t)std::stoi((*it)[2].str());
	}
	REQUIRE(python == branchTable());

	// The rules around the table name the same interpreter opcodes.
	REQUIRE(conformance.find("AI_OP_GO_TO_NEXT = 0x0000") != std::string::npos);
	REQUIRE(conformance.find("AI_OP_GO_TO_FIRST = 0x0001") != std::string::npos);
	REQUIRE(conformance.find("AI_OP_LABEL = 0x0002") != std::string::npos);
	REQUIRE(conformance.find("AI_OP_YIELD = 0x0003") != std::string::npos);
	REQUIRE(conformance.find("AI_OP_END = 0x0004") != std::string::npos);
	REQUIRE(conformance.find("AI_OP_SET_LIST = 0x0005") != std::string::npos);
	REQUIRE(conformance.find("AI_OP_RETURN = 0x0008") != std::string::npos);
	REQUIRE(conformance.find("AI_CHR_SELF = 0xFD") != std::string::npos);
}

TEST_CASE("AI list edges: the extractor writes them and conformance recomputes them",
	"[ai][edges][static]")
{
	const std::string arena = readSource("port/src/romextract_pdarena.c");
	const std::string conformance = readSource("tools/asset_archive_conformance.py");

	// Stage lists: the scan computes the edges before the rows are written;
	// nodes carry the flags, links the edges, after the list's own links.
	REQUIRE(arena.find("#include \"ai_list_edges.h\"") != std::string::npos);
	REQUIRE(arena.find("result = aiListEdgesCompute(scan->commands, scan->count, scan->edges,") !=
		std::string::npos);
	REQUIRE(arena.find("if (s_aiListScan(list, size - (u32)list_ofs, 0, &scan) != 0) {") !=
		std::string::npos);
	REQUIRE(arena.find("s_appendAiCommandFlagsJson(ai_command_nodes_json,\n\t\t\t\t\t\tscan.flags[command_index]) != 0 ||") !=
		std::string::npos);
	REQUIRE(arena.find("if (s_appendAiEdgeLinksJson(ai_command_links_json, list_node_prefix,") !=
		std::string::npos);
	REQUIRE(arena.find("\"%s    { \\\"from\\\": \\\"%s.command.%04u\\\", \\\"to\\\": \\\"%s.command.%04u\\\", \\\"kind\\\": \\\"%s\\\" }\"") !=
		std::string::npos);
	REQUIRE(arena.find("\", \\\"reachable\\\": false\"") != std::string::npos);
	REQUIRE(arena.find("\", \\\"switches_list\\\": true\"") != std::string::npos);
	// The global bank: the same scan, ending each list at its end command.
	REQUIRE(arena.find("char *romExtractGlobalAiListsGraphJson(u32 *out_nodes, u32 *out_edges)") !=
		std::string::npos);
	REQUIRE(arena.find("if (s_aiListScan(g_GlobalAilists[i].list, 0x10000u, 1, &scan) != 0) {") !=
		std::string::npos);
	REQUIRE(arena.find("\"  \\\"schema\\\": \\\"pd2.scenario.ai.graph.v1\\\",\\n\"") !=
		std::string::npos);
	// SP-86: the scenario kind moved with the edges.
	REQUIRE(arena.find("_objrules_sparks_gas_mplifts_aiedges_simtwins_typedargs_catalogargs\"") != std::string::npos);

	// Conformance: the scenario's graph against its rows, required when the
	// embedding arena's kind is the new one; the bank's graph member.
	REQUIRE(conformance.find("def ai_list_control_flow(") != std::string::npos);
	REQUIRE(conformance.find("def ai_list_edge_errors(") != std::string::npos);
	REQUIRE(conformance.find("require=parent_scenario_graph_cache\n") != std::string::npos);
	REQUIRE(conformance.find("== ARENA_SCENARIO_GRAPH_CACHE_KIND,") != std::string::npos);
	REQUIRE(conformance.find("parent_scenario_graph_cache=(") != std::string::npos);
	REQUIRE(conformance.find("PDAI_AI_EDGES_EXTRACT_VERSION = \"global_ailists_rows_v2_aiedges\"") !=
		std::string::npos);
	REQUIRE(conformance.find("PDAI_GRAPH_MEMBER = \"ai/global_ailists.graph.json\"") !=
		std::string::npos);
	REQUIRE(conformance.find("errors.extend(validate_ai_lists_graph_member(label, zf, rows))") !=
		std::string::npos);
	REQUIRE(conformance.find("missing {kind} edge to {describe(target)}") != std::string::npos);
	REQUIRE(conformance.find("_objrules_sparks_gas_mplifts_aiedges_simtwins_typedargs_catalogargs\"") != std::string::npos);
}
