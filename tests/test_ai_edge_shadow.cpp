/*
 * tests/test_ai_edge_shadow.cpp -- AI lists as graphs, slice 2
 * (context/designs/modding/ai-list-graphs.md): the runtime's edge tables
 * (port/src/ai_edge_shadow.c), the shadow walk's classification of what an
 * executed command did against its node's edges and flags, the lookup the
 * interpreter uses, the counters, and pins on the interpreter hook, the
 * tables' lifetime and the campaign runner's parity line.
 */

#include "catch.hpp"

extern "C" {
#include "ai_edge_shadow.h"
}

#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Cmd {
	uint16_t op;
	std::vector<uint8_t> operands;
};

// Opcodes the cases use (src/game/chrai.c g_CommandPointers).
const uint16_t GO_TO_NEXT = AI_LIST_OP_GO_TO_NEXT;
const uint16_t GO_TO_FIRST = AI_LIST_OP_GO_TO_FIRST;
const uint16_t LABEL = AI_LIST_OP_LABEL;
const uint16_t YIELD = AI_LIST_OP_YIELD;
const uint16_t END = AI_LIST_OP_END;
const uint16_t SET_LIST = AI_LIST_OP_SET_LIST;
const uint16_t RETURN = AI_LIST_OP_RETURN;
const uint16_t IF_IDLE = 0x000c; // cmd[2] = label
const uint16_t RESTART_TIMER = 0x00b6;
const uint16_t CAMERA = AI_EDGE_SHADOW_OP_SET_CAMERA_ANIMATION; // cmd[2..3] = animation

struct TableFree {
	void operator()(ai_edge_table_t *table) const { aiEdgeTableFree(table); }
};
using TablePtr = std::unique_ptr<ai_edge_table_t, TableFree>;

// The bytes the compilers write: each command's big-endian opcode, then its
// operand bytes.
std::vector<uint8_t> layOut(const std::vector<Cmd> &list)
{
	std::vector<uint8_t> bytes;
	for (const Cmd &cmd : list) {
		bytes.push_back((uint8_t)(cmd.op >> 8));
		bytes.push_back((uint8_t)(cmd.op & 0xff));
		bytes.insert(bytes.end(), cmd.operands.begin(), cmd.operands.end());
	}
	return bytes;
}

ai_edge_table_t *build(const std::vector<Cmd> &list, const uint8_t *bytes,
	int32_t list_id, std::string *error = nullptr)
{
	std::vector<ai_list_command_t> commands;
	for (const Cmd &cmd : list) {
		ai_list_command_t command;
		command.opcode = cmd.op;
		command.operands = cmd.operands.empty() ? nullptr : cmd.operands.data();
		command.operand_len = (uint32_t)cmd.operands.size();
		commands.push_back(command);
	}
	char reason[128] = "unset";
	ai_edge_table_t *table = aiEdgeTableBuild(list_id, bytes, commands.data(),
		(uint32_t)commands.size(), reason, sizeof(reason));
	if (error) {
		*error = reason;
	}
	return table;
}

// One list covering every kind of node (index: command, edges):
//  0 restart_timer          next 1
//  1 label 1                next 2
//  2 yield                  next 3            yields
//  3 if_idle 5              next 4, branch 7
//  4 set_camera_animation   next 5
//  5 if_idle 9              next 6, restart 0 (no label 9)
//  6 go_to_first 1          goto 1
//  7 label 5                next 8
//  8 set_list (chr 5)       next 9
//  9 return                 -                 returns
// 10 set_list (CHR_SELF)    -                 switches_list
// 11 go_to_next 7           restart 0 (no label 7 after it)
// 12 end                    -                 ends
const std::vector<Cmd> &sampleList()
{
	static const std::vector<Cmd> list = {
		{RESTART_TIMER, {}},
		{LABEL, {1}},
		{YIELD, {}},
		{IF_IDLE, {5}},
		{CAMERA, {0x01, 0x23}},
		{IF_IDLE, {9}},
		{GO_TO_FIRST, {1}},
		{LABEL, {5}},
		{SET_LIST, {0x05, 0x04, 0x01}},
		{RETURN, {}},
		{SET_LIST, {AI_LIST_CHR_SELF, 0x04, 0x02}},
		{GO_TO_NEXT, {7}},
		{END, {}},
	};
	return list;
}

// Byte offsets of sampleList()'s commands; [13] is its size.
const uint32_t kSampleOffsets[] = {0, 2, 5, 7, 10, 14, 17, 20, 23, 28, 30, 35, 38, 40};

ai_edge_shadow_verdict_t classifyAt(const ai_edge_table_t *table, uint32_t index,
	ai_edge_shadow_move_t move, int32_t to, int list_changed = 0,
	int64_t offset = -1)
{
	ai_edge_shadow_node_t node;
	REQUIRE(aiEdgeTableNode(table, index, &node) == 1);
	ai_edge_shadow_outcome_t outcome;
	outcome.move = move;
	outcome.list_changed = list_changed;
	outcome.to = to;
	outcome.offset = offset >= 0 ? (uint32_t)offset
		: to >= 0 ? aiEdgeTableOffsetOf(table, (uint32_t)to) : 0xffffu;
	return aiEdgeShadowClassify(&node, &outcome);
}

const ai_edge_shadow_move_t WENT_ON = AI_EDGE_SHADOW_MOVE_WENT_ON;
const ai_edge_shadow_move_t SKIPPED = AI_EDGE_SHADOW_MOVE_SKIPPED;
const ai_edge_shadow_move_t STOPPED = AI_EDGE_SHADOW_MOVE_STOPPED;
const ai_edge_shadow_verdict_t AGREE = AI_EDGE_SHADOW_AGREE;
const ai_edge_shadow_verdict_t DIFFER = AI_EDGE_SHADOW_DIFFER;
const ai_edge_shadow_verdict_t FALLTHROUGH = AI_EDGE_SHADOW_REFUSED_FALLTHROUGH;
const ai_edge_shadow_verdict_t UNTRACKED = AI_EDGE_SHADOW_UNTRACKED;

void clearRegistry()
{
	aiEdgeShadowClearOwner(AI_EDGE_SHADOW_OWNER_STAGE);
	aiEdgeShadowClearOwner(AI_EDGE_SHADOW_OWNER_BANK);
	aiEdgeShadowResetCounters();
}

ai_edge_shadow_counters_t counters()
{
	ai_edge_shadow_counters_t out;
	std::memset(&out, 0xff, sizeof(out));
	aiEdgeShadowGetCounters(&out);
	return out;
}

std::string readSource(const char *path)
{
	std::ifstream input(path, std::ios::binary);
	REQUIRE(input.good());
	std::ostringstream out;
	out << input.rdbuf();
	std::string text = out.str();
	std::string clean;
	clean.reserve(text.size());
	for (char c : text) {
		if (c != '\r') {
			clean += c;
		}
	}
	return clean;
}

// From the first mention of signature to the brace closing its body.
std::string functionBlock(const std::string &text, const std::string &signature)
{
	const size_t start = text.find(signature);
	if (start == std::string::npos) {
		return "";
	}
	const size_t brace = text.find('{', start);
	if (brace == std::string::npos) {
		return "";
	}
	int depth = 0;
	for (size_t i = brace; i < text.size(); i++) {
		if (text[i] == '{') {
			depth++;
		} else if (text[i] == '}' && --depth == 0) {
			return text.substr(start, i - start + 1);
		}
	}
	return "";
}

size_t countOf(const std::string &text, const std::string &needle)
{
	size_t n = 0;
	for (size_t at = text.find(needle); at != std::string::npos;
			at = text.find(needle, at + needle.size())) {
		n++;
	}
	return n;
}

void requireOrder(const std::string &text, const std::vector<std::string> &tokens)
{
	size_t previous = 0;
	for (size_t i = 0; i < tokens.size(); i++) {
		const size_t at = text.find(tokens[i], i == 0 ? 0 : previous + 1);
		INFO("token " << i << ": " << tokens[i]);
		REQUIRE(at != std::string::npos);
		previous = at;
	}
}

int g_ListIdCalls = 0;
const uint8_t *g_ListIdAsked = nullptr;

int32_t testListId(const uint8_t *list)
{
	g_ListIdCalls++;
	g_ListIdAsked = list;
	return 0x0bad;
}

} // namespace

TEST_CASE("AI edge shadow: a table holds the list's offsets, opcodes, flags and edges",
	"[ai][edges][shadow]")
{
	const std::vector<uint8_t> bytes = layOut(sampleList());
	REQUIRE(bytes.size() == 40);
	TablePtr table(build(sampleList(), bytes.data(), 0x0401));
	REQUIRE(table.get() != nullptr);
	CHECK(aiEdgeTableListId(table.get()) == 0x0401);
	REQUIRE(aiEdgeTableCommandCount(table.get()) == 13);
	CHECK(aiEdgeTableEdgeCount(table.get()) == 12);

	// Each command is found at its offset, nothing between or past the end.
	for (uint32_t i = 0; i < 13; i++) {
		INFO("command " << i);
		CHECK(aiEdgeTableOffsetOf(table.get(), i) == kSampleOffsets[i]);
		CHECK(aiEdgeTableCommandAt(table.get(), kSampleOffsets[i]) == (int32_t)i);
		for (uint32_t inside = kSampleOffsets[i] + 1; inside < kSampleOffsets[i + 1]; inside++) {
			CHECK(aiEdgeTableCommandAt(table.get(), inside) == -1);
		}
	}
	CHECK(aiEdgeTableOffsetOf(table.get(), 13) == 40);
	CHECK(aiEdgeTableCommandAt(table.get(), 40) == -1);
	CHECK(aiEdgeTableCommandAt(table.get(), 0xffffffffu) == -1);

	// The edges are aiListEdgesCompute's, by command, next first.
	std::vector<ai_list_command_t> commands;
	for (const Cmd &cmd : sampleList()) {
		commands.push_back({cmd.op, cmd.operands.empty() ? nullptr : cmd.operands.data(),
			(uint32_t)cmd.operands.size()});
	}
	std::vector<ai_list_edge_t> edges(commands.size() * AI_LIST_EDGES_PER_COMMAND);
	std::vector<uint8_t> flags(commands.size());
	uint32_t edge_count = 0;
	REQUIRE(aiListEdgesCompute(commands.data(), (uint32_t)commands.size(), edges.data(),
		(uint32_t)edges.size(), &edge_count, flags.data(), nullptr) == AI_LIST_EDGES_OK);
	REQUIRE(edge_count == 12);
	uint32_t seen = 0;
	for (uint32_t i = 0; i < 13; i++) {
		ai_edge_shadow_node_t node;
		REQUIRE(aiEdgeTableNode(table.get(), i, &node) == 1);
		INFO("command " << i);
		CHECK(node.index == i);
		CHECK(node.opcode == sampleList()[i].op);
		CHECK(node.flags == flags[i]);
		for (uint32_t e = 0; e < node.edge_count; e++, seen++) {
			REQUIRE(seen < edge_count);
			CHECK(edges[seen].from == i);
			CHECK(node.targets[e] == edges[seen].to);
			CHECK(node.kinds[e] == (uint8_t)edges[seen].kind);
		}
	}
	CHECK(seen == edge_count);
	ai_edge_shadow_node_t node;
	CHECK(aiEdgeTableNode(table.get(), 13, &node) == 0);
	CHECK(aiEdgeTableNode(nullptr, 0, &node) == 0);

	// A branching command without its label has no edges: no table, a reason.
	std::string error;
	const std::vector<Cmd> short_branch = {{YIELD, {}}, {IF_IDLE, {}}, {END, {}}};
	const std::vector<uint8_t> short_bytes = layOut(short_branch);
	CHECK(build(short_branch, short_bytes.data(), 0x0402, &error) == nullptr);
	CHECK(error.find("command 1 (opcode 0x000c)") != std::string::npos);
	CHECK(build({}, short_bytes.data(), 0x0403, &error) == nullptr);
	CHECK(build(short_branch, nullptr, 0x0404, &error) == nullptr);
}

TEST_CASE("AI edge shadow: yields, ends and the camera stop", "[ai][edges][shadow]")
{
	const std::vector<uint8_t> bytes = layOut(sampleList());
	TablePtr table(build(sampleList(), bytes.data(), 0x0401));
	REQUIRE(table.get() != nullptr);
	const ai_edge_table_t *t = table.get();

	// yield (2) stops with the next command saved as its resume point.
	CHECK(classifyAt(t, 2, STOPPED, 3) == AGREE);
	CHECK(classifyAt(t, 2, STOPPED, 2) == DIFFER);
	CHECK(classifyAt(t, 2, STOPPED, 4) == DIFFER);
	CHECK(classifyAt(t, 2, STOPPED, -1, 1) == DIFFER);
	CHECK(classifyAt(t, 2, WENT_ON, 3) == DIFFER); // a yield never goes on

	// end (12) stops where it stands.
	CHECK(classifyAt(t, 12, STOPPED, 12) == AGREE);
	CHECK(classifyAt(t, 12, STOPPED, 0) == DIFFER);
	CHECK(classifyAt(t, 12, SKIPPED, 0) == DIFFER);

	// set_camera_animation (4): stops without moving when its cutscene cannot
	// start, else goes on to the next command.
	CHECK(classifyAt(t, 4, STOPPED, 4) == AGREE);
	CHECK(classifyAt(t, 4, STOPPED, 5) == DIFFER);
	CHECK(classifyAt(t, 4, WENT_ON, 5) == AGREE);
	CHECK(classifyAt(t, 4, SKIPPED, 5) == AGREE);

	// Any other command that stops differs.
	CHECK(classifyAt(t, 0, STOPPED, 0) == DIFFER);
	CHECK(classifyAt(t, 3, STOPPED, 3) == DIFFER);
}

TEST_CASE("AI edge shadow: mp_init_simulants stays on its command while the wave is incomplete",
		  "[ai][edges][shadow][simulant]")
{
	// A multiplayer stage's background list 0x1000: mp_init_simulants,
	// rebuild_teams, rebuild_squadrons, set_list (CHR_SELF), end.
	const std::vector<Cmd> list = {
		{AI_EDGE_SHADOW_OP_MP_INIT_SIMULANTS, {}},
		{0x0145, {}},
		{0x0146, {}},
		{SET_LIST, {AI_LIST_CHR_SELF, 0x00, 0x00}},
		{END, {}},
	};
	const std::vector<uint8_t> bytes = layOut(list);
	TablePtr table(build(list, bytes.data(), 0x1000));
	REQUIRE(table.get() != nullptr);
	const ai_edge_table_t *t = table.get();

	// B-1255: an incomplete wave ends the tick where it stands; a
	// complete one goes on to rebuild_teams.
	CHECK(classifyAt(t, 0, STOPPED, 0) == AGREE);
	CHECK(classifyAt(t, 0, STOPPED, 1) == DIFFER);
	CHECK(classifyAt(t, 0, WENT_ON, 1) == AGREE);
	CHECK(classifyAt(t, 0, WENT_ON, 2) == DIFFER);
	// Other commands of the list still may not stop.
	CHECK(classifyAt(t, 1, STOPPED, 1) == DIFFER);
}

TEST_CASE("AI edge shadow: branches taken and not, gotos and restarts", "[ai][edges][shadow]")
{
	const std::vector<uint8_t> bytes = layOut(sampleList());
	TablePtr table(build(sampleList(), bytes.data(), 0x0401));
	REQUIRE(table.get() != nullptr);
	const ai_edge_table_t *t = table.get();

	// if_idle 5 (3): not taken (next) or taken (the label at 7); a refused
	// twin is skipped by the B-949 guard, which is not taken.
	CHECK(classifyAt(t, 3, WENT_ON, 4) == AGREE);
	CHECK(classifyAt(t, 3, WENT_ON, 7) == AGREE);
	CHECK(classifyAt(t, 3, SKIPPED, 4) == AGREE);
	CHECK(classifyAt(t, 3, WENT_ON, 5) == DIFFER);
	CHECK(classifyAt(t, 3, WENT_ON, 1) == DIFFER);
	CHECK(classifyAt(t, 3, WENT_ON, 0) == DIFFER);

	// go_to_first 1 (6) jumps back to the label at 1; it has no next edge.
	CHECK(classifyAt(t, 6, WENT_ON, 1) == AGREE);
	CHECK(classifyAt(t, 6, WENT_ON, 7) == DIFFER);

	// A label that is not found restarts the list at its first command:
	// if_idle 9 (5) and go_to_next 7 (11).
	CHECK(classifyAt(t, 5, WENT_ON, 0) == AGREE);
	CHECK(classifyAt(t, 5, WENT_ON, 6) == AGREE);
	CHECK(classifyAt(t, 11, WENT_ON, 0) == AGREE);
	CHECK(classifyAt(t, 11, WENT_ON, 12) == DIFFER);

	// Plain commands and labels go on to the next command only.
	CHECK(classifyAt(t, 0, WENT_ON, 1) == AGREE);
	CHECK(classifyAt(t, 1, WENT_ON, 2) == AGREE);
	CHECK(classifyAt(t, 7, WENT_ON, 8) == AGREE);
	CHECK(classifyAt(t, 0, SKIPPED, 1) == AGREE);
}

TEST_CASE("AI edge shadow: list switches, returns and refused fall-throughs",
	"[ai][edges][shadow]")
{
	const std::vector<uint8_t> bytes = layOut(sampleList());
	TablePtr table(build(sampleList(), bytes.data(), 0x0401));
	REQUIRE(table.get() != nullptr);
	const ai_edge_table_t *t = table.get();

	// set_list on CHR_SELF (10) and return (9) leave the list; set_list to
	// the running list starts it again at offset 0.
	CHECK(classifyAt(t, 10, WENT_ON, -1, 1) == AGREE);
	CHECK(classifyAt(t, 9, WENT_ON, -1, 1) == AGREE);
	CHECK(classifyAt(t, 10, WENT_ON, 0) == AGREE);
	CHECK(classifyAt(t, 9, WENT_ON, 0) == AGREE);
	CHECK(classifyAt(t, 10, STOPPED, -1, 1) == DIFFER);
	CHECK(classifyAt(t, 10, WENT_ON, 11) == DIFFER);

	// set_list on another chr (8) goes on with the next command and never
	// leaves the list; no other command does either.
	CHECK(classifyAt(t, 8, WENT_ON, 9) == AGREE);
	CHECK(classifyAt(t, 8, WENT_ON, -1, 1) == DIFFER);
	CHECK(classifyAt(t, 3, WENT_ON, -1, 1) == DIFFER);

	// B-949: a refused return or self set_list is skipped to the next
	// command, which neither node has an edge to: its own category.
	CHECK(classifyAt(t, 9, SKIPPED, 10) == FALLTHROUGH);
	CHECK(classifyAt(t, 10, SKIPPED, 11) == FALLTHROUGH);
	CHECK(classifyAt(t, 11, SKIPPED, 12) == FALLTHROUGH);
	CHECK(classifyAt(t, 9, SKIPPED, 11) == DIFFER);
	CHECK(classifyAt(t, 9, SKIPPED, -1, 0, 29) == DIFFER);

	// go_to_first at the first command whose label is not found restarts at
	// itself: the guard skips it, a fall-through as well.
	const std::vector<Cmd> loop = {{GO_TO_FIRST, {9}}, {YIELD, {}}, {END, {}}};
	const std::vector<uint8_t> loop_bytes = layOut(loop);
	TablePtr first(build(loop, loop_bytes.data(), 0x0402));
	REQUIRE(first.get() != nullptr);
	CHECK(classifyAt(first.get(), 0, WENT_ON, 0) == AGREE);
	CHECK(classifyAt(first.get(), 0, SKIPPED, 1) == FALLTHROUGH);
}

TEST_CASE("AI edge shadow: a move to no edge, or to no command, is a mismatch",
	"[ai][edges][shadow]")
{
	const std::vector<uint8_t> bytes = layOut(sampleList());
	TablePtr table(build(sampleList(), bytes.data(), 0x0401));
	REQUIRE(table.get() != nullptr);
	const ai_edge_table_t *t = table.get();

	// A twin that advances by the wrong length lands inside a command.
	CHECK(classifyAt(t, 3, WENT_ON, -1, 0, 9) == DIFFER);
	CHECK(classifyAt(t, 0, WENT_ON, -1, 0, 3) == DIFFER);
	// One that skips a command, or jumps to another label.
	CHECK(classifyAt(t, 0, WENT_ON, 2) == DIFFER);
	CHECK(classifyAt(t, 6, WENT_ON, 7) == DIFFER);
	// A skip past a command with a next edge must land on it.
	CHECK(classifyAt(t, 0, SKIPPED, 2) == DIFFER);
	CHECK(classifyAt(t, 0, SKIPPED, -1, 0, 3) == DIFFER);

	// No node, no outcome, or a node whose edges are missing.
	ai_edge_shadow_node_t node;
	REQUIRE(aiEdgeTableNode(t, 3, &node) == 1);
	ai_edge_shadow_outcome_t outcome = {WENT_ON, 0, 4, kSampleOffsets[4]};
	CHECK(aiEdgeShadowClassify(&node, &outcome) == AGREE);
	CHECK(aiEdgeShadowClassify(nullptr, &outcome) == DIFFER);
	CHECK(aiEdgeShadowClassify(&node, nullptr) == DIFFER);
	node.targets = nullptr;
	CHECK(aiEdgeShadowClassify(&node, &outcome) == DIFFER);

	CHECK(std::string(aiEdgeShadowVerdictName(AGREE)) == "agree");
	CHECK(std::string(aiEdgeShadowVerdictName(DIFFER)) == "differ");
	CHECK(std::string(aiEdgeShadowVerdictName(FALLTHROUGH)) == "refused_fallthrough");
	CHECK(std::string(aiEdgeShadowVerdictName(UNTRACKED)) == "untracked");
}

TEST_CASE("AI edge shadow: the lookup by list bytes, its owners and their lifetime",
	"[ai][edges][shadow]")
{
	clearRegistry();
	REQUIRE(aiEdgeShadowTableCount(AI_EDGE_SHADOW_OWNER_STAGE) == 0);
	REQUIRE(aiEdgeShadowTableCount(AI_EDGE_SHADOW_OWNER_BANK) == 0);

	// Lists one command long ("end"), keyed by neighbouring bytes, so the
	// probes collide and the removals must close their gaps.
	const std::vector<Cmd> end = {{END, {}}};
	std::vector<uint8_t> pool(4096, 0);
	const uint32_t n = 1200;
	for (uint32_t i = 0; i < n; i++) {
		ai_edge_table_t *table = build(end, &pool[i * 2], (int32_t)i);
		REQUIRE(table != nullptr);
		REQUIRE(aiEdgeShadowRegister(i % 2 ? AI_EDGE_SHADOW_OWNER_BANK
			: AI_EDGE_SHADOW_OWNER_STAGE, table) == 1);
	}
	CHECK(aiEdgeShadowTableCount(AI_EDGE_SHADOW_OWNER_STAGE) == n / 2);
	CHECK(aiEdgeShadowTableCount(AI_EDGE_SHADOW_OWNER_BANK) == n / 2);
	for (uint32_t i = 0; i < n; i++) {
		const ai_edge_table_t *found = aiEdgeShadowFind(&pool[i * 2]);
		REQUIRE(found);
		REQUIRE(aiEdgeTableListId(found) == (int32_t)i);
	}
	CHECK(aiEdgeShadowFind(&pool[1]) == nullptr);
	CHECK(aiEdgeShadowFind(nullptr) == nullptr);

	// The same bytes again: the new table replaces the old one.
	ai_edge_table_t *again = build(end, &pool[0], 0x7777);
	REQUIRE(again);
	REQUIRE(aiEdgeShadowRegister(AI_EDGE_SHADOW_OWNER_STAGE, again) == 1);
	CHECK(aiEdgeShadowFind(&pool[0]) == again);
	CHECK(aiEdgeShadowTableCount(AI_EDGE_SHADOW_OWNER_STAGE) == n / 2);

	// A list whose table could not be built is forgotten.
	aiEdgeShadowForget(&pool[4]);
	CHECK(aiEdgeShadowFind(&pool[4]) == nullptr);
	aiEdgeShadowForget(&pool[4]);
	CHECK(aiEdgeShadowTableCount(AI_EDGE_SHADOW_OWNER_STAGE) == n / 2 - 1);

	// The stage's tables go with the stage; the banks' stay, all reachable.
	aiEdgeShadowClearOwner(AI_EDGE_SHADOW_OWNER_STAGE);
	CHECK(aiEdgeShadowTableCount(AI_EDGE_SHADOW_OWNER_STAGE) == 0);
	CHECK(aiEdgeShadowTableCount(AI_EDGE_SHADOW_OWNER_BANK) == n / 2);
	for (uint32_t i = 0; i < n; i++) {
		const ai_edge_table_t *found = aiEdgeShadowFind(&pool[i * 2]);
		INFO("list " << i);
		if (i % 2) {
			REQUIRE(found);
			REQUIRE(aiEdgeTableListId(found) == (int32_t)i);
		} else {
			REQUIRE(found == nullptr);
		}
	}
	aiEdgeShadowClearOwner(AI_EDGE_SHADOW_OWNER_BANK);
	CHECK(aiEdgeShadowTableCount(AI_EDGE_SHADOW_OWNER_BANK) == 0);
	CHECK(aiEdgeShadowFind(&pool[2]) == nullptr);

	// A table without bytes, or an owner that does not exist, is refused.
	CHECK(aiEdgeShadowRegister(AI_EDGE_SHADOW_OWNER_STAGE, nullptr) == 0);
	CHECK(aiEdgeShadowRegister(AI_EDGE_SHADOW_OWNER_COUNT, build(end, &pool[0], 1)) == 0);
	CHECK(aiEdgeShadowFind(&pool[0]) == nullptr);
	clearRegistry();
}

TEST_CASE("AI edge shadow: the walk counts what it compares and what it cannot",
	"[ai][edges][shadow]")
{
	clearRegistry();
	const std::vector<uint8_t> bytes = layOut(sampleList());
	ai_edge_table_t *table = build(sampleList(), bytes.data(), 0x0401);
	REQUIRE(table != nullptr);
	REQUIRE(aiEdgeShadowRegister(AI_EDGE_SHADOW_OWNER_STAGE, table) == 1);
	const uint8_t *list = bytes.data();
	const uint8_t other[4] = {0x00, 0x04, 0x00, 0x00};

	auto step = [&](uint32_t offset, uint16_t opcode, ai_edge_shadow_move_t move,
			const uint8_t *after_list, uint32_t after_offset) {
		ai_edge_shadow_step_t s;
		s.list = list;
		s.offset = offset;
		s.opcode = opcode;
		s.move = move;
		s.after_list = after_list;
		s.after_offset = after_offset;
		s.list_id = testListId;
		return aiEdgeShadowObserve(&s);
	};

	// The branch at 3 taken; the yield at 2 stopping at its resume point.
	CHECK(step(7, IF_IDLE, WENT_ON, list, 20) == AGREE);
	CHECK(step(5, YIELD, STOPPED, list, 7) == AGREE);
	// set_list on CHR_SELF to another list.
	CHECK(step(30, SET_LIST, WENT_ON, other, 0) == AGREE);
	// A refused return falls through (B-949).
	CHECK(step(28, RETURN, SKIPPED, list, 30) == FALLTHROUGH);
	// A command that skipped the one after it, twice: counted twice.
	CHECK(step(0, RESTART_TIMER, WENT_ON, list, 5) == DIFFER);
	CHECK(step(0, RESTART_TIMER, WENT_ON, list, 5) == DIFFER);

	ai_edge_shadow_counters_t c = counters();
	CHECK(c.compared == 6);
	CHECK(c.differ == 2);
	CHECK(c.refused_fallthrough == 1);
	CHECK(c.untracked == 0);

	// Untracked: a list without a table (named through the caller), an
	// offset where no command starts, bytes that are not the table's command.
	g_ListIdCalls = 0;
	g_ListIdAsked = nullptr;
	ai_edge_shadow_step_t unknown;
	unknown.list = other;
	unknown.offset = 0;
	unknown.opcode = END;
	unknown.move = STOPPED;
	unknown.after_list = other;
	unknown.after_offset = 0;
	unknown.list_id = testListId;
	CHECK(aiEdgeShadowObserve(&unknown) == UNTRACKED);
	CHECK(g_ListIdCalls == 1);
	CHECK((const void *)g_ListIdAsked == (const void *)other);
	CHECK(step(1, LABEL, WENT_ON, list, 5) == UNTRACKED);
	CHECK(step(0, END, STOPPED, list, 0) == UNTRACKED);
	CHECK(aiEdgeShadowObserve(nullptr) == UNTRACKED);

	c = counters();
	CHECK(c.compared == 6);
	CHECK(c.differ == 2);
	CHECK(c.refused_fallthrough == 1);
	CHECK(c.untracked == 4);

	// A mission starts: the counters (and the once-per-command logs) start again.
	aiEdgeShadowResetCounters();
	c = counters();
	CHECK(c.compared == 0);
	CHECK(c.differ == 0);
	CHECK(c.refused_fallthrough == 0);
	CHECK(c.untracked == 0);
	CHECK(step(0, RESTART_TIMER, WENT_ON, list, 2) == AGREE);
	CHECK(counters().compared == 1);

	// Once the stage's tables are gone its lists are untracked.
	aiEdgeShadowClearOwner(AI_EDGE_SHADOW_OWNER_STAGE);
	CHECK(step(0, RESTART_TIMER, WENT_ON, list, 2) == UNTRACKED);
	CHECK(counters().untracked == 1);
	clearRegistry();
}

TEST_CASE("AI edge shadow: the interpreter reports each dispatched command after the B-949 guard",
	"[ai][edges][shadow][static]")
{
	const std::string chrai = readSource("src/game/chrai.c");
	const std::string signature = "void chraiExecute(void *entity, s32 proptype)";
	const std::string execute = functionBlock(chrai, signature);
	REQUIRE(!execute.empty());

	// The contract pins read the interpreter from its first mention.
	REQUIRE(chrai.find("chraiExecute") == chrai.find(signature) + std::strlen("void "));
	REQUIRE(chrai.find("#include \"ai_edge_shadow.h\"") != std::string::npos);

	// Decided once per entity, on a stage built from scenario source, before
	// the loop; a stop is reported before the loop ends, every other command
	// after the guard has moved the cursor, with the guard's skip marked.
	requireOrder(execute, {
		"const bool shadow = bgUsesScenarioSource() != 0;",
		"while (g_Vars.ailist) {",
		"u32 prevoffset = g_Vars.aioffset;",
		"ai_edge_shadow_move_t move = AI_EDGE_SHADOW_MOVE_WENT_ON;",
		"if (g_CommandPointers[type]()) {",
		"if (shadow) {",
		"chraiShadowCheck(prevailist, prevoffset, type,",
		"AI_EDGE_SHADOW_MOVE_STOPPED);",
		"break;",
		"if (g_Vars.ailist == prevailist && g_Vars.aioffset == prevoffset) {",
		"g_Vars.aioffset += chraiGetCommandLength(g_Vars.ailist, g_Vars.aioffset);",
		"move = AI_EDGE_SHADOW_MOVE_SKIPPED;",
		"}",
		"if (shadow) {",
		"chraiShadowCheck(prevailist, prevoffset, type, move);",
		"} else {",
	});
	CHECK(countOf(execute, "chraiShadowCheck(") == 2);
	CHECK(countOf(execute, "AI_EDGE_SHADOW_MOVE_SKIPPED") == 1);
	// The guard itself is unchanged.
	CHECK(execute.find("if (g_Vars.aioffset == prevoffset)") == std::string::npos);

	// The check reads the cursor after the handler, and names lists with the
	// interpreter's own lookup.
	const std::string check = functionBlock(chrai, "static void chraiShadowCheck(");
	REQUIRE(!check.empty());
	CHECK(check.find("step.after_list = g_Vars.ailist;") != std::string::npos);
	CHECK(check.find("step.after_offset = g_Vars.aioffset;") != std::string::npos);
	CHECK(check.find("step.list_id = chraiShadowListId;") != std::string::npos);
	CHECK(check.find("aiEdgeShadowObserve(&step);") != std::string::npos);
	CHECK(functionBlock(chrai, "static s32 chraiShadowListId(").find(
		"chraiGetListIdByList((u8 *)list, &is_global)") != std::string::npos);
}

TEST_CASE("AI edge shadow: tables are built where lists compile and go with their bytes",
	"[ai][edges][shadow][static]")
{
	const std::string runtime = readSource("port/src/scenario_source_runtime.c");
	const std::string header = readSource("port/include/scenario_source_runtime.h");
	const std::string bank = readSource("port/src/scenario_global_ailists.c");
	const std::string pdmain = readSource("port/src/pdmain.c");

	// A stage's lists: each list's table from its rows' commands, under the
	// bytes in the setup block that the interpreter runs.
	const std::string stage_block = functionBlock(runtime, "static u8 *s_setupBuildStageBlock(");
	REQUIRE(!stage_block.empty());
	requireOrder(stage_block, {
		"const u32 list_start = list_cursor;",
		"runtime_ailists[i].list = (u8 *)(uintptr_t)list_cursor;",
		"data[list_cursor++] = (u8)((cmd->opcode >> 8) & 0xff);",
		"edge_tables += s_aiRegisterStageEdgeTable(&ai_table->lists[i],",
		"data + list_start, scenario_id);",
		"\"AI.EDGES: %d of %d AI lists of '%s' have edge tables\"",
	});
	const std::string build_table = functionBlock(runtime, "static ai_edge_table_t *s_aiBuildEdgeTable(");
	CHECK(build_table.find("commands[j].operand_len = list->commands[j].operand_count;") !=
		std::string::npos);
	CHECK(build_table.find("aiEdgeTableBuild(list->id, bytes, commands,") != std::string::npos);
	const std::string register_stage = functionBlock(runtime, "static s32 s_aiRegisterStageEdgeTable(");
	requireOrder(register_stage, {
		"aiEdgeShadowRegister(AI_EDGE_SHADOW_OWNER_STAGE, edge_table)",
		"aiEdgeShadowForget(bytes);",
		"its commands run untracked",
	});
	// They live in the stage pool: the main loop drops them before resetting it.
	CHECK(countOf(pdmain, "mempResetPool(MEMPOOL_STAGE);") == 1);
	requireOrder(pdmain, {
		"meshDetachAllStageProps();",
		"aiEdgeShadowClearOwner(AI_EDGE_SHADOW_OWNER_STAGE);",
		"mempResetPool(MEMPOOL_STAGE);",
	});

	// The banks: compiled with their tables, which a whole bind hands to the
	// lookup; they go before the bank's bytes are freed. The round trip
	// compiles and frees without registering.
	const std::string compile = functionBlock(runtime, "s32 scenarioSourceAiListsCompile(const char *json_text,");
	CHECK(compile.find("lists[i].edges = s_aiBuildEdgeTable(list, lists[i].bytes,") !=
		std::string::npos);
	CHECK(functionBlock(runtime, "void scenarioSourceAiListsFree(").find(
		"aiEdgeTableFree(lists[i].edges);") != std::string::npos);
	CHECK(header.find("struct ai_edge_table *edges;") != std::string::npos);
	CHECK(countOf(runtime, "aiEdgeShadowRegister(") == 1);
	requireOrder(functionBlock(bank, "static void s_freeBanks(void)"), {
		"aiEdgeShadowClearOwner(AI_EDGE_SHADOW_OWNER_BANK);",
		"scenarioSourceAiListsFree(s_Bank.banks[b].lists, s_Bank.banks[b].count);",
	});
	const std::string handover = functionBlock(bank, "static s32 s_registerEdgeTables(");
	requireOrder(handover, {
		"bank->lists[l].edges = NULL;",
		"aiEdgeShadowRegister(AI_EDGE_SHADOW_OWNER_BANK, table)",
		"aiEdgeShadowForget(bank->lists[l].bytes);",
	});
	requireOrder(functionBlock(bank, "s32 scenarioGlobalAiListsBindBank(char *reason, u32 reason_size)"), {
		"snprintf(s_Bank.signature, sizeof(s_Bank.signature), \"%s\", signature);",
		"edge_tables = s_registerEdgeTables(&s_Bank.banks[b]);",
		"\"AI.EDGES: %d of %d AI lists of the bank '%s' have edge tables\"",
		"g_GlobalAilists[i].list = list;",
	});
	CHECK(countOf(bank, "aiEdgeShadowRegister(") == 1);
	CHECK(countOf(bank, "s_registerEdgeTables(&") == 1);
	// A stage's override points a global entry at a stage list, whose table
	// is the stage's.
	CHECK(functionBlock(bank, "s32 scenarioGlobalAiListsApplyStageOverrides(").find("aiEdge") ==
		std::string::npos);
	// scenario_source_runtime.c still reads no global AI list table.
	CHECK(runtime.find("g_GlobalAilists") == std::string::npos);
}

TEST_CASE("AI edge shadow: the campaign runner reports and enforces edge parity per mission",
	"[ai][edges][shadow][campaign][static]")
{
	const std::string runner = readSource("port/src/autocampaign.c");

	const std::string probe = functionBlock(runner, "static s32 probeAiEdgeParity(const char *stage_id)");
	REQUIRE(!probe.empty());
	requireOrder(probe, {
		"aiEdgeShadowGetCounters(&counters);",
		"ACLOG(\"ai edge parity stage_id='%s' compared=%d differ=%d refused_fallthrough=%d untracked=%d\",",
		"if (counters.differ > 0 || counters.untracked > 0) {",
		"failRun(CAMPAIGN_RUN_FAILURE_OBJECTIVES,",
		"\"ai edge parity: '%s' differ=%d untracked=%d of %d compared",
		"return -1;",
	});
	// Next to the objective and outcome parity probes, before forced
	// completion.
	CHECK(runner.find("ACLOG(\"outcome parity probe stage_id='%s' outcome=%s\",") <
		runner.find("static s32 probeAiEdgeParity(const char *stage_id)"));
	requireOrder(functionBlock(runner, "void autocampaignTick(void)"), {
		"probeObjectiveParity(mission->stage_id) < 0",
		"probeAiEdgeParity(mission->stage_id) < 0",
		"objectivesDebugCompleteCurrentMission()",
	});
	// The counters start at each mission's start, after the production
	// mission-start entry queued it.
	requireOrder(functionBlock(runner, "static s32 queuePlannedMission(s32 plan_index)"), {
		"menuhandlerAcceptMission(MENUOP_SET, NULL, NULL);",
		"if (g_MainChangeToStageNum != stage.stagenum) {",
		"aiEdgeShadowResetCounters();",
		"return 1;",
	});
	CHECK(countOf(runner, "aiEdgeShadowResetCounters();") == 1);
}
