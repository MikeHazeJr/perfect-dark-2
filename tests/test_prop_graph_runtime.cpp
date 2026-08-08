#include "catch.hpp"

#include <string>

extern "C" {
#include "prop_graph_runtime.h"
}

namespace {

struct TargetState {
	s32 enabled = 1;
	f32 health = 0.0f;
	s32 collision = 1;
	s32 channel = 0;
	std::string channel_name;
};

s32 isEnabled(void *context) {
	return static_cast<TargetState *>(context)->enabled;
}

void setEnabled(void *context, s32 value) {
	static_cast<TargetState *>(context)->enabled = value;
}

void setHealth(void *context, f32 value) {
	static_cast<TargetState *>(context)->health = value;
}

void setCollision(void *context, s32 value) {
	static_cast<TargetState *>(context)->collision = value;
}

void setChannel(void *context, const char *name, s32 value) {
	auto *state = static_cast<TargetState *>(context);
	state->channel_name = name ? name : "";
	state->channel = value;
}

prop_graph_runtime_target_t targetFor(TargetState &state) {
	return { &state, isEnabled, setEnabled, setHealth, setCollision, setChannel };
}

std::string productionGraph(const char *id) {
	return std::string(
		"{\"schema\":\"pd.prop_behavior.v1\",\"asset_id\":\"") + id +
		"\",\"graph_id\":\"runtime\",\"nodes\":["
		"{\"id\":\"spawn\",\"kind\":\"event.spawn\",\"params\":{}},"
		"{\"id\":\"health\",\"kind\":\"action.set_health\",\"params\":{\"value\":125.0}},"
		"{\"id\":\"channel\",\"kind\":\"action.set_channel\",\"params\":{\"channel\":\"ready\",\"value\":true}},"
		"{\"id\":\"tick\",\"kind\":\"event.tick\",\"params\":{}},"
		"{\"id\":\"enabled\",\"kind\":\"condition.enabled\",\"params\":{}},"
		"{\"id\":\"collision\",\"kind\":\"action.set_collision\",\"params\":{\"value\":false}}],"
		"\"edges\":[{\"from\":\"spawn\",\"to\":\"health\"},"
		"{\"from\":\"health\",\"to\":\"channel\"},"
		"{\"from\":\"tick\",\"to\":\"enabled\"},"
		"{\"from\":\"enabled\",\"to\":\"collision\"}]}";
}

}  // namespace

TEST_CASE("pdprop graph compiles, binds, and executes production callbacks",
		"[modding][pdxxx][prop_graph]") {
	propGraphRuntimeReset();
	const std::string graph = productionGraph("mod:crate");
	char err[256] = {};
	REQUIRE(propGraphRuntimeRegisterJson("mod:crate", graph.data(),
		static_cast<u32>(graph.size()), err, sizeof(err)) == 0);
	REQUIRE(propGraphRuntimeHasAsset("mod:crate") == 1);

	TargetState state;
	auto target = targetFor(state);
	REQUIRE(propGraphRuntimeBind("mod:crate", 42, &target) == 1);
	REQUIRE(state.health == Approx(125.0f));
	REQUIRE(state.channel_name == "ready");
	REQUIRE(state.channel == 1);
	REQUIRE(state.collision == 1);

	propGraphRuntimeTick();
	REQUIRE(state.collision == 0);
	state.enabled = 0;
	state.collision = 1;
	propGraphRuntimeTick();
	REQUIRE(state.collision == 1);
	REQUIRE(propGraphRuntimeInstanceCount() == 1);
	propGraphRuntimeUnbindAll();
	REQUIRE(propGraphRuntimeInstanceCount() == 0);
	propGraphRuntimeReset();
}

TEST_CASE("pdprop graph fails closed on omitted, mismatched, and unsupported source",
		"[modding][pdxxx][prop_graph]") {
	char err[256] = {};
	propGraphRuntimeReset();

	const std::string empty =
		"{\"schema\":\"pd.prop_behavior.v1\",\"asset_id\":\"mod:empty\","
		"\"graph_id\":\"runtime\",\"nodes\":[],\"edges\":[]}";
	REQUIRE(propGraphRuntimeRegisterJson("mod:empty", empty.data(),
		static_cast<u32>(empty.size()), err, sizeof(err)) != 0);

	const std::string mismatch = productionGraph("mod:other");
	REQUIRE(propGraphRuntimeRegisterJson("mod:crate", mismatch.data(),
		static_cast<u32>(mismatch.size()), err, sizeof(err)) != 0);

	const std::string unsupported =
		"{\"schema\":\"pd.prop_behavior.v1\",\"asset_id\":\"mod:bad\","
		"\"graph_id\":\"runtime\",\"nodes\":[{\"id\":\"spawn\","
		"\"kind\":\"event.spawn\",\"params\":{}},{\"id\":\"fake\","
		"\"kind\":\"action.native_fallback\",\"params\":{}}],"
		"\"edges\":[{\"from\":\"spawn\",\"to\":\"fake\"}]}";
	REQUIRE(propGraphRuntimeRegisterJson("mod:bad", unsupported.data(),
		static_cast<u32>(unsupported.size()), err, sizeof(err)) != 0);
	REQUIRE(propGraphRuntimeHasAsset("mod:bad") == 0);

	const std::string unreachable =
		"{\"schema\":\"pd.prop_behavior.v1\",\"asset_id\":\"mod:orphan\","
		"\"graph_id\":\"runtime\",\"nodes\":[{\"id\":\"spawn\","
		"\"kind\":\"event.spawn\",\"params\":{}},{\"id\":\"orphan\","
		"\"kind\":\"action.set_enabled\",\"params\":{\"value\":true}}],"
		"\"edges\":[]}";
	REQUIRE(propGraphRuntimeRegisterJson("mod:orphan", unreachable.data(),
		static_cast<u32>(unreachable.size()), err, sizeof(err)) != 0);
	propGraphRuntimeReset();
}
