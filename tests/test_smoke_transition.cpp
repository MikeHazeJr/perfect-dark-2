#include "catch.hpp"

extern "C" {
#include "smoke_transition.h"
}

namespace {
smoke_transition_config_t plainConfig()
{
	smoke_transition_config_t config{};
	config.timeout_ms = 10000;
	config.stable_ms = 1000;
	return config;
}

smoke_transition_config_t assistedConfig()
{
	auto config = plainConfig();
	config.assist_enabled = 1;
	config.assist_hold_ms = 900;
	return config;
}

smoke_transition_input_t input(bool target, bool assist)
{
	smoke_transition_input_t value{};
	value.target_met = target;
	value.assist_condition_met = assist;
	return value;
}
}

TEST_CASE("smoke transition configuration is bounded and fail closed",
	"[smoke][transition][b1088]")
{
	auto plain = plainConfig();
	auto assisted = assistedConfig();
	REQUIRE(smokeTransitionConfigValid(&plain));
	REQUIRE(smokeTransitionConfigValid(&assisted));
	REQUIRE_FALSE(smokeTransitionConfigValid(nullptr));

	plain.timeout_ms = 0;
	REQUIRE_FALSE(smokeTransitionConfigValid(&plain));
	plain = plainConfig();
	plain.stable_ms = plain.timeout_ms;
	REQUIRE_FALSE(smokeTransitionConfigValid(&plain));
	plain = plainConfig();
	plain.assist_hold_ms = 1;
	REQUIRE_FALSE(smokeTransitionConfigValid(&plain));

	assisted.assist_hold_ms = 0;
	REQUIRE_FALSE(smokeTransitionConfigValid(&assisted));
	assisted = assistedConfig();
	assisted.stable_ms = 0;
	REQUIRE_FALSE(smokeTransitionConfigValid(&assisted));
	assisted = assistedConfig();
	assisted.assist_hold_ms = assisted.timeout_ms;
	REQUIRE_FALSE(smokeTransitionConfigValid(&assisted));
}

TEST_CASE("smoke transition requires one continuous stable target window",
	"[smoke][transition][b1088]")
{
	auto config = plainConfig();
	smoke_transition_state_t state{};
	smokeTransitionInit(&state, &config, 100);

	auto target = input(true, false);
	auto plan = smokeTransitionTick(&state, &config, &target, 100);
	REQUIRE(plan.stability_started);
	REQUIRE_FALSE(plan.satisfied);

	auto absent = input(false, false);
	plan = smokeTransitionTick(&state, &config, &absent, 999);
	REQUIRE(plan.stability_reset);
	REQUIRE(plan.stable_elapsed_ms == 0);
	REQUIRE_FALSE(plan.satisfied);

	plan = smokeTransitionTick(&state, &config, &target, 1000);
	REQUIRE(plan.stability_started);
	REQUIRE_FALSE(plan.satisfied);
	plan = smokeTransitionTick(&state, &config, &target, 1999);
	REQUIRE_FALSE(plan.satisfied);
	REQUIRE(plan.stable_elapsed_ms == 999);
	plan = smokeTransitionTick(&state, &config, &target, 2000);
	REQUIRE(plan.satisfied);
	REQUIRE(plan.stable_elapsed_ms == 1000);
	REQUIRE_FALSE(smokeTransitionAssistWasUsed(&state));
}

TEST_CASE("smoke transition uses at most one bounded assist before stable success",
	"[smoke][transition][b1088]")
{
	auto config = assistedConfig();
	smoke_transition_state_t state{};
	smokeTransitionInit(&state, &config, 0);

	auto aperture = input(false, true);
	auto plan = smokeTransitionTick(&state, &config, &aperture, 100);
	REQUIRE(plan.press_assist);
	REQUIRE(smokeTransitionAssistIsHeld(&state));

	plan = smokeTransitionTick(&state, &config, &aperture, 500);
	REQUIRE_FALSE(plan.press_assist);
	REQUIRE_FALSE(plan.release_assist);

	auto target = input(true, false);
	plan = smokeTransitionTick(&state, &config, &target, 600);
	REQUIRE(plan.stability_started);
	REQUIRE_FALSE(plan.satisfied);
	plan = smokeTransitionTick(&state, &config, &target, 1000);
	REQUIRE(plan.release_assist);
	REQUIRE_FALSE(plan.satisfied);
	REQUIRE_FALSE(smokeTransitionAssistIsHeld(&state));
	REQUIRE(smokeTransitionAssistWasUsed(&state));
	plan = smokeTransitionTick(&state, &config, &target, 1600);
	REQUIRE(plan.satisfied);
	REQUIRE(plan.stable_elapsed_ms == 1000);

	plan = smokeTransitionTick(&state, &config, &aperture, 1700);
	REQUIRE(plan.satisfied);
	REQUIRE_FALSE(plan.press_assist);
}

TEST_CASE("smoke transition completes stable gameplay without synthetic input",
	"[smoke][transition][b1088]")
{
	auto config = assistedConfig();
	smoke_transition_state_t state{};
	smokeTransitionInit(&state, &config, 500);
	auto target = input(true, false);

	auto plan = smokeTransitionTick(&state, &config, &target, 500);
	REQUIRE(plan.stability_started);
	REQUIRE_FALSE(plan.press_assist);
	plan = smokeTransitionTick(&state, &config, &target, 1500);
	REQUIRE(plan.satisfied);
	REQUIRE_FALSE(plan.press_assist);
	REQUIRE_FALSE(smokeTransitionAssistWasUsed(&state));
}

TEST_CASE("smoke transition deadline wins and releases a held assist",
	"[smoke][transition][b1088]")
{
	auto config = assistedConfig();
	/* Keep the assist held through the wait deadline for this policy edge. */
	config.assist_hold_ms = 9999;
	smoke_transition_state_t state{};
	smokeTransitionInit(&state, &config, 1000);
	auto aperture = input(false, true);
	auto plan = smokeTransitionTick(&state, &config, &aperture, 1500);
	REQUIRE(plan.press_assist);

	auto target = input(true, false);
	plan = smokeTransitionTick(&state, &config, &target, 11000);
	REQUIRE(plan.timed_out);
	REQUIRE(plan.release_assist);
	REQUIRE_FALSE(plan.satisfied);
	REQUIRE_FALSE(smokeTransitionAssistIsHeld(&state));
	REQUIRE(smokeTransitionAssistWasUsed(&state));
}

TEST_CASE("smoke transition forced cleanup is idempotent",
	"[smoke][transition][b1088]")
{
	auto config = assistedConfig();
	smoke_transition_state_t state{};
	smokeTransitionInit(&state, &config, 0);
	auto aperture = input(false, true);
	REQUIRE(smokeTransitionTick(&state, &config, &aperture, 1).press_assist);
	REQUIRE(smokeTransitionForceRelease(&state));
	REQUIRE_FALSE(smokeTransitionForceRelease(&state));
	REQUIRE_FALSE(smokeTransitionAssistIsHeld(&state));
}
