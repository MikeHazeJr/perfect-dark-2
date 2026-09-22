#include "catch.hpp"

#include <string>
#include <cmath>
#include <cstdio>
#include <limits>

extern "C" {
#include "prop_graph_runtime.h"
}

namespace {

struct TargetState {
	s32 enabled = 1;
	f32 health = 0.0f;
	s16 maxdamage = 0;
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
	auto *state = static_cast<TargetState *>(context);
	state->health = value;
	state->maxdamage = propGraphHealthToMaxDamage(value);
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

TEST_CASE("pdprop health validates the numeric value passed to production callbacks",
		"[modding][pdxxx][prop_graph][c3842][source_authority]") {
	for (const char *value : { "0", "125", "125.5", "3276", "3276.7", "-1", "-1.5",
			"3277", "3276.7002", "1e20", "-4294967296", "1e1000", "1e100", "125oops" }) {
		CAPTURE(value);
		propGraphRuntimeReset();
		std::string graph = productionGraph("mod:health_contract");
		graph.replace(graph.find("125.0"), 5, value);
		char err[256] = {};
		const bool valid = std::string(value) == "0" || std::string(value) == "125" ||
			std::string(value) == "125.5" || std::string(value) == "3276" ||
			std::string(value) == "3276.7";
		const s32 result = propGraphRuntimeRegisterJson("mod:health_contract",
			graph.data(), static_cast<u32>(graph.size()), err, sizeof(err));
		if (valid) {
			REQUIRE(result == 0);
			TargetState state;
			auto target = targetFor(state);
			REQUIRE(propGraphRuntimeBind("mod:health_contract", 42, &target) == 1);
			REQUIRE(state.health == Approx(std::stof(value)));
			REQUIRE(state.maxdamage == static_cast<s16>(std::stof(value) * 10.0f));
		} else {
			REQUIRE(result != 0);
			REQUIRE(propGraphRuntimeHasAsset("mod:health_contract") == 0);
		}
		propGraphRuntimeReset();
	}
}

TEST_CASE("pdprop native health handles adjacent float boundaries and dynamic invalid values",
		"[modding][pdxxx][prop_graph][source_authority][T-ASSETS-041]") {
	const float maximum = 32767.0f / 10.0f;
	const float below = std::nextafter(maximum, 0.0f);
	const float above = std::nextafter(maximum, std::numeric_limits<float>::infinity());
	for (float health : { below, maximum, above }) {
		CAPTURE(health);
		propGraphRuntimeReset();
		char value[64];
		std::snprintf(value, sizeof(value), "%.9g", static_cast<double>(health));
		std::string graph = productionGraph("mod:health_boundary");
		graph.replace(graph.find("125.0"), 5, value);
		char err[256] = {};
		const s32 result = propGraphRuntimeRegisterJson("mod:health_boundary", graph.data(),
			static_cast<u32>(graph.size()), err, sizeof(err));
		if (health <= maximum) {
			REQUIRE(result == 0);
			TargetState state;
			auto target = targetFor(state);
			REQUIRE(propGraphRuntimeBind("mod:health_boundary", 42, &target) == 1);
			REQUIRE(state.maxdamage == (health == maximum ? 32767 : 32766));
		} else {
			REQUIRE(result != 0);
			REQUIRE(std::string(err).find("native health range") != std::string::npos);
			REQUIRE(propGraphRuntimeHasAsset("mod:health_boundary") == 0);
		}
		propGraphRuntimeReset();
	}
	REQUIRE(propGraphHealthToMaxDamage(125.55f) == 1255);
	REQUIRE(propGraphHealthToMaxDamage(above) == 32767);
	REQUIRE(propGraphHealthToMaxDamage(std::numeric_limits<float>::max()) == 32767);
	REQUIRE(propGraphHealthToMaxDamage(std::numeric_limits<float>::infinity()) == 32767);
	REQUIRE(propGraphHealthToMaxDamage(-std::numeric_limits<float>::infinity()) == 0);
	REQUIRE(propGraphHealthToMaxDamage(std::numeric_limits<float>::quiet_NaN()) == 0);
	REQUIRE(propGraphHealthToMaxDamage(-1.0f) == 0);
	REQUIRE(propGraphHealthIsRepresentable(std::numeric_limits<float>::infinity()) == 0);
	REQUIRE(propGraphHealthIsRepresentable(std::numeric_limits<float>::quiet_NaN()) == 0);
}
