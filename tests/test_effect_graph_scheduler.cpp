#include "catch.hpp"

#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "effect_graph_runtime.h"
#include "weapon_graph_runtime.h"
}

namespace {

struct DispatchState {
	std::vector<std::string> kinds;
	s32 begin_count = 0;
	s32 commit_count = 0;
	s32 rollback_count = 0;
	s32 rollback_attempted = -1;
	std::string fail_kind;
	s32 fail_begin = 0;
	s32 fail_commit = 0;
};

s32 beginDispatch(const effect_graph_dispatch_context_t *context) {
	auto *state = static_cast<DispatchState *>(context->user);
	state->begin_count++;
	return state->fail_begin;
}

s32 commitDispatch(const effect_graph_dispatch_context_t *context) {
	auto *state = static_cast<DispatchState *>(context->user);
	state->commit_count++;
	return state->fail_commit;
}

void rollbackDispatch(const effect_graph_dispatch_context_t *context,
	s32 attempted_count) {
	auto *state = static_cast<DispatchState *>(context->user);
	state->rollback_count++;
	state->rollback_attempted = attempted_count;
}

s32 dispatchNode(const effect_graph_dispatch_context_t *context,
	const weapon_graph_ir_node_t *node, s32 execution_index) {
	auto *state = static_cast<DispatchState *>(context->user);
	REQUIRE(execution_index == static_cast<s32>(state->kinds.size()));
	state->kinds.emplace_back(node->kind);
	return state->fail_kind == node->kind ? -1 : 0;
}

effect_graph_dispatch_table_t completeDispatchTable() {
	effect_graph_dispatch_table_t dispatch = {};
	dispatch.begin = beginDispatch;
	dispatch.commit = commitDispatch;
	dispatch.rollback = rollbackDispatch;
	for (s32 i = 0; i < EFFECT_GRAPH_DISPATCH_SLOT_COUNT; i++) {
		dispatch.nodes[i] = dispatchNode;
	}
	return dispatch;
}

const std::string kAllKindsGraph =
	"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:scheduled_fx\","
	"\"nodes\":["
	"{\"id\":\"smoke\",\"kind\":\"effect.smoke\",\"params\":{\"smoke_class\":\"small\"}},"
	"{\"id\":\"spark\",\"kind\":\"effect.spark\",\"params\":{}},"
	"{\"id\":\"explosion\",\"kind\":\"effect.explosion\",\"params\":{\"explosion_class\":\"small\"}},"
	"{\"id\":\"tint\",\"kind\":\"effect.tint\",\"params\":{}},"
	"{\"id\":\"glow\",\"kind\":\"effect.glow\",\"params\":{}},"
	"{\"id\":\"shimmer\",\"kind\":\"effect.shimmer\",\"params\":{}},"
	"{\"id\":\"darken\",\"kind\":\"effect.darken\",\"params\":{}},"
	"{\"id\":\"screen\",\"kind\":\"effect.screen\",\"params\":{}},"
	"{\"id\":\"particle\",\"kind\":\"effect.particle\",\"params\":{}}],"
	"\"edges\":[{\"from\":\"explosion\",\"to\":\"spark\"},"
	"{\"from\":\"spark\",\"to\":\"smoke\"}]}";

void resetRuntime() {
	effectGraphRuntimeClearAll();
	weaponGraphRuntimeSetEnabled(1);
}

void registerAllKinds() {
	char err[256] = {};
	INFO(err);
	REQUIRE(effectGraphRuntimeRegisterGraphJson("modx:scheduled_fx",
		kAllKindsGraph.data(), static_cast<u32>(kAllKindsGraph.size()),
		err, sizeof(err)) == 0);
}

} // namespace

TEST_CASE("pdeffect scheduler has an exact dispatch slot for every accepted v1 kind",
	"[modding][pdxxx][effect_graph][t-assets-032]") {
	const weapon_graph_opcode_e opcodes[] = {
		WEAPON_GRAPH_OP_EFFECT_TINT,
		WEAPON_GRAPH_OP_EFFECT_GLOW,
		WEAPON_GRAPH_OP_EFFECT_SHIMMER,
		WEAPON_GRAPH_OP_EFFECT_DARKEN,
		WEAPON_GRAPH_OP_EFFECT_SCREEN,
		WEAPON_GRAPH_OP_EFFECT_PARTICLE,
		WEAPON_GRAPH_OP_EFFECT_EXPLOSION,
		WEAPON_GRAPH_OP_EFFECT_SPARK,
		WEAPON_GRAPH_OP_EFFECT_SMOKE,
	};
	for (s32 i = 0; i < EFFECT_GRAPH_DISPATCH_SLOT_COUNT; i++) {
		REQUIRE(effectGraphDispatchSlotForOpcode(opcodes[i]) == i);
	}
	REQUIRE(effectGraphDispatchSlotForOpcode(WEAPON_GRAPH_OP_INVALID) == -1);
	REQUIRE(effectGraphDispatchSlotForOpcode(WEAPON_GRAPH_OP_FIRE_HITSCAN) == -1);
}

TEST_CASE("pdeffect scheduler dispatches every accepted kind in stable dependency order",
	"[modding][pdxxx][effect_graph][t-assets-032]") {
	resetRuntime();
	registerAllKinds();
	DispatchState state;
	auto dispatch = completeDispatchTable();
	char err[256] = {};
	REQUIRE(effectGraphRuntimeDispatch("modx:scheduled_fx", 0.25f, &dispatch,
		&state, err, sizeof(err)) == EFFECT_GRAPH_DISPATCH_SLOT_COUNT);
	REQUIRE(state.kinds == std::vector<std::string>{
		"effect.explosion", "effect.spark", "effect.smoke", "effect.tint",
		"effect.glow", "effect.shimmer", "effect.darken", "effect.screen",
		"effect.particle" });
	REQUIRE(state.begin_count == 1);
	REQUIRE(state.commit_count == 1);
	REQUIRE(state.rollback_count == 0);
	resetRuntime();
}

TEST_CASE("pdeffect scheduler preflights complete handler coverage before side effects",
	"[modding][pdxxx][effect_graph][t-assets-032]") {
	resetRuntime();
	registerAllKinds();
	DispatchState state;
	auto dispatch = completeDispatchTable();
	dispatch.nodes[EFFECT_GRAPH_DISPATCH_SCREEN] = nullptr;
	char err[256] = {};
	REQUIRE(effectGraphRuntimeDispatch("modx:scheduled_fx", 0, &dispatch,
		&state, err, sizeof(err)) == -1);
	REQUIRE(std::string(err).find("effect.screen has no installed handler") !=
		std::string::npos);
	REQUIRE(state.kinds.empty());
	REQUIRE(state.begin_count == 0);
	REQUIRE(state.commit_count == 0);
	REQUIRE(state.rollback_count == 0);
	resetRuntime();
}

TEST_CASE("pdeffect scheduler rolls back failed nodes and failed transaction phases",
	"[modding][pdxxx][effect_graph][t-assets-032]") {
	resetRuntime();
	registerAllKinds();
	auto dispatch = completeDispatchTable();
	char err[256] = {};

	SECTION("node failure includes the attempted node in rollback") {
		DispatchState state;
		state.fail_kind = "effect.smoke";
		REQUIRE(effectGraphRuntimeDispatch("modx:scheduled_fx", 0, &dispatch,
			&state, err, sizeof(err)) == -1);
		REQUIRE(state.kinds.size() == 3);
		REQUIRE(state.commit_count == 0);
		REQUIRE(state.rollback_count == 1);
		REQUIRE(state.rollback_attempted == 3);
	}

	SECTION("begin failure rolls back zero attempted nodes") {
		DispatchState state;
		state.fail_begin = 1;
		REQUIRE(effectGraphRuntimeDispatch("modx:scheduled_fx", 0, &dispatch,
			&state, err, sizeof(err)) == -1);
		REQUIRE(state.kinds.empty());
		REQUIRE(state.rollback_attempted == 0);
	}

	SECTION("commit failure rolls back the complete graph") {
		DispatchState state;
		state.fail_commit = 1;
		REQUIRE(effectGraphRuntimeDispatch("modx:scheduled_fx", 0, &dispatch,
			&state, err, sizeof(err)) == -1);
		REQUIRE(state.kinds.size() == EFFECT_GRAPH_DISPATCH_SLOT_COUNT);
		REQUIRE(state.rollback_attempted == EFFECT_GRAPH_DISPATCH_SLOT_COUNT);
	}
	resetRuntime();
}

TEST_CASE("pdeffect activation rejects public node kinds outside the dispatch contract",
	"[modding][pdxxx][effect_graph][t-assets-032]") {
	resetRuntime();
	const std::string graph =
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:future_fx\","
		"\"nodes\":[{\"id\":\"future\",\"kind\":\"effect.future\",\"params\":{}}]}";
	char err[256] = {};
	REQUIRE(effectGraphRuntimeRegisterGraphJson("modx:future_fx", graph.data(),
		static_cast<u32>(graph.size()), err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("unsupported effect graph module effect.future") !=
		std::string::npos);
	REQUIRE(effectGraphRuntimeGet("modx:future_fx") == nullptr);
	resetRuntime();
}

TEST_CASE("pdeffect scheduler rejects corrupted retained order before begin",
	"[modding][pdxxx][effect_graph][t-assets-032]") {
	resetRuntime();
	registerAllKinds();
	auto *runtime = const_cast<effect_graph_runtime_t *>(
		effectGraphRuntimeGet("modx:scheduled_fx"));
	REQUIRE(runtime != nullptr);
	runtime->program.execution_order[1] = runtime->program.execution_order[0];
	DispatchState state;
	auto dispatch = completeDispatchTable();
	char err[256] = {};
	REQUIRE(effectGraphRuntimeDispatch("modx:scheduled_fx", 0, &dispatch,
		&state, err, sizeof(err)) == -1);
	REQUIRE(std::string(err).find("invalid or duplicate node") != std::string::npos);
	REQUIRE(state.begin_count == 0);
	resetRuntime();
}
