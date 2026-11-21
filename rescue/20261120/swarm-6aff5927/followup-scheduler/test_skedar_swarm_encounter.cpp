#include "catch.hpp"

#include <initializer_list>
#include <memory>

#include "skedar_swarm_encounter.h"

namespace {

constexpr uint64_t epoch = 11;
constexpr skedar_swarm_actor first = {70, 100};
constexpr skedar_swarm_actor second = {71, 101};
constexpr skedar_swarm_actor giant = {72, 102};
constexpr skedar_swarm_actor player = {2, 200};

skedar_swarm_config configuration()
{
	skedar_swarm_config config{};
	config.max_live_raiders = 2;
	config.max_placement_failures = 3;
	config.spawn_interval_ticks = 2;
	config.wave_count = 2;
	config.waves[0] = {2, SKEDAR_SWARM_ALL_TUNNELS, 0};
	config.waves[1] = {2, 1u << 2, 5};
	config.attacks.active = 2;
	config.attacks.airborne = 2;
	config.attacks.active_by_kind[SKEDAR_SWARM_MELEE] = 2;
	config.attacks.active_by_kind[SKEDAR_SWARM_JUMP] = 1;
	config.attacks.active_by_kind[SKEDAR_SWARM_DROP] = 1;
	config.attacks.starts_per_tick = 1;
	config.attacks.telegraph_ticks = 2;
	config.attacks.duration_ticks = 6;
	config.attacks.cooldown_ticks = 8;
	return config;
}

void admit(skedar_swarm_encounter &state, uint32_t tick, skedar_swarm_actor actor)
{
	skedar_swarm_ticket ticket{};
	REQUIRE(skedarSwarmRequestSpawn(&state, epoch, tick, &ticket));
	REQUIRE(ticket.kind == SKEDAR_SWARM_RAIDER);
	REQUIRE(skedarSwarmResolveSpawn(&state, &ticket, ticket.issued_tick, actor) == SKEDAR_SWARM_SPAWN_COMMITTED);
}

skedar_swarm_target target_at(uint32_t tick, skedar_swarm_actor actor = player)
{
	return {actor, tick, SKEDAR_SWARM_TARGET_QUALIFIED};
}

} // namespace

TEST_CASE("Swarm commits physical closures once and drains raiders before one Giant", "[swarm][encounter]")
{
	auto state = std::make_unique<skedar_swarm_encounter>();
	const auto config = configuration();
	REQUIRE(skedarSwarmInit(state.get(), &config, epoch, 0));
	REQUIRE_FALSE(skedarSwarmCloseTunnel(state.get(), epoch, 0, 0));
	admit(*state, 0, first);
	skedar_swarm_ticket pending{};
	REQUIRE(skedarSwarmRequestSpawn(state.get(), epoch, 2, &pending));
	REQUIRE(pending.tunnel_index == 1);

	REQUIRE(skedarSwarmCloseTunnel(state.get(), epoch, 1, 1));
	REQUIRE(skedarSwarmResolveSpawn(state.get(), &pending, pending.issued_tick, second) == SKEDAR_SWARM_SPAWN_STALE);
	REQUIRE_FALSE(skedarSwarmCloseTunnel(state.get(), epoch, 1, 1));
	REQUIRE(skedarSwarmRequestSpawn(state.get(), epoch, 4, &pending));
	REQUIRE(pending.tunnel_index == 2);
	REQUIRE(skedarSwarmResolveSpawn(state.get(), &pending, pending.issued_tick, second) == SKEDAR_SWARM_SPAWN_COMMITTED);

	REQUIRE(skedarSwarmCloseTunnel(state.get(), epoch, 0, 1));
	REQUIRE(skedarSwarmCloseTunnel(state.get(), epoch, 2, 1));
	REQUIRE(state->phase == SKEDAR_SWARM_DRAINING);
	REQUIRE_FALSE(skedarSwarmRequestSpawn(state.get(), epoch, 6, &pending));
	REQUIRE(skedarSwarmRetireActor(state.get(), epoch, first, SKEDAR_SWARM_DEFEATED));
	REQUIRE_FALSE(skedarSwarmRequestSpawn(state.get(), epoch, 7, &pending));
	REQUIRE(skedarSwarmRetireActor(state.get(), epoch, second, SKEDAR_SWARM_DEFEATED));
	REQUIRE(state->phase == SKEDAR_SWARM_FINALE);
	REQUIRE(skedarSwarmRequestSpawn(state.get(), epoch, 8, &pending));
	REQUIRE(pending.kind == SKEDAR_SWARM_GIANT);
	REQUIRE(pending.tunnel_index == -1);
	REQUIRE(skedarSwarmResolveSpawn(state.get(), &pending, pending.issued_tick, giant) == SKEDAR_SWARM_SPAWN_COMMITTED);
	REQUIRE(skedarSwarmResolveSpawn(state.get(), &pending, pending.issued_tick, giant) == SKEDAR_SWARM_SPAWN_STALE);
	REQUIRE_FALSE(skedarSwarmRequestSpawn(state.get(), epoch, 20, &pending));
	REQUIRE(skedarSwarmRetireActor(state.get(), epoch, giant, SKEDAR_SWARM_DEFEATED));
	REQUIRE(state->phase == SKEDAR_SWARM_COMPLETE);
	REQUIRE_FALSE(skedarSwarmRetireActor(state.get(), epoch, giant, SKEDAR_SWARM_DEFEATED));
}

TEST_CASE("Swarm respects active, wave, spacing, and replacement budgets", "[swarm][encounter]")
{
	auto state = std::make_unique<skedar_swarm_encounter>();
	auto config = configuration();
	config.max_live_raiders = 1;
	config.attacks.active = 1;
	config.attacks.airborne = 1;
	config.attacks.active_by_kind[SKEDAR_SWARM_MELEE] = 1;
	REQUIRE(skedarSwarmInit(state.get(), &config, epoch, 40));

	admit(*state, 40, first);
	skedar_swarm_ticket ticket{};
	REQUIRE_FALSE(skedarSwarmRequestSpawn(state.get(), epoch, 41, &ticket));
	REQUIRE_FALSE(skedarSwarmRequestSpawn(state.get(), epoch, 42, &ticket));
	REQUIRE(skedarSwarmRetireActor(state.get(), epoch, first, SKEDAR_SWARM_DEFEATED));
	const skedar_swarm_actor replacement = {first.prop_index, first.prop_generation + 1};
	admit(*state, 42, replacement);
	REQUIRE_FALSE(skedarSwarmRetireActor(state.get(), epoch, first, SKEDAR_SWARM_DEFEATED));
	REQUIRE(skedarSwarmRetireActor(state.get(), epoch, replacement, SKEDAR_SWARM_DEFEATED));
	REQUIRE_FALSE(skedarSwarmRequestSpawn(state.get(), epoch, 44, &ticket));
	REQUIRE_FALSE(skedarSwarmRequestSpawn(state.get(), epoch, 48, &ticket));
	REQUIRE(skedarSwarmRequestSpawn(state.get(), epoch, 49, &ticket));
	REQUIRE(ticket.tunnel_index == 2);
	REQUIRE(ticket.wave_index == 1);
	REQUIRE_FALSE(skedarSwarmRequestSpawn(state.get(), epoch, 49, &ticket));
}

TEST_CASE("Swarm rejects old epoch work and fails repeated placement without a false clear", "[swarm][encounter]")
{
	auto state = std::make_unique<skedar_swarm_encounter>();
	const auto config = configuration();
	REQUIRE(skedarSwarmInit(state.get(), &config, epoch, 0));
	skedar_swarm_ticket stale{};
	REQUIRE(skedarSwarmRequestSpawn(state.get(), epoch, 0, &stale));
	skedarSwarmStop(state.get());
	REQUIRE(skedarSwarmInit(state.get(), &config, epoch + 1, 0));
	skedar_swarm_ticket ticket{};
	REQUIRE(skedarSwarmRequestSpawn(state.get(), epoch + 1, 0, &ticket));
	REQUIRE(ticket.sequence == stale.sequence);
	REQUIRE(skedarSwarmResolveSpawn(state.get(), &stale, stale.issued_tick, first) == SKEDAR_SWARM_SPAWN_STALE);
	REQUIRE_FALSE(skedarSwarmCloseTunnel(state.get(), epoch, 2, 1));

	for (uint32_t tick: {0u, 2u, 4u}) {
		if (tick) REQUIRE(skedarSwarmRequestSpawn(state.get(), epoch + 1, tick, &ticket));
		const auto result = tick == 4 ? SKEDAR_SWARM_SPAWN_FAILED : SKEDAR_SWARM_SPAWN_RETRY;
		REQUIRE(skedarSwarmResolveSpawn(state.get(), &ticket, ticket.issued_tick, {}) == result);
	}
	REQUIRE(state->phase == SKEDAR_SWARM_FAILED);
	REQUIRE_FALSE(skedarSwarmRequestSpawn(state.get(), epoch + 1, 10, &ticket));
}

TEST_CASE("Giant route ignores an abandoned wave delay and cannot clear by removal", "[swarm][encounter]")
{
	auto state = std::make_unique<skedar_swarm_encounter>();
	auto config = configuration();
	config.waves[0].delay_ticks = 600;
	REQUIRE(skedarSwarmInit(state.get(), &config, epoch, 0));
	for (int route = 0; route < SKEDAR_SWARM_TUNNEL_COUNT; route++) {
		REQUIRE(skedarSwarmCloseTunnel(state.get(), epoch, route, 1));
	}
	skedar_swarm_ticket ticket{};
	REQUIRE(skedarSwarmRequestSpawn(state.get(), epoch, 1, &ticket));
	REQUIRE(ticket.kind == SKEDAR_SWARM_GIANT);
	REQUIRE(skedarSwarmResolveSpawn(state.get(), &ticket, ticket.issued_tick, giant) == SKEDAR_SWARM_SPAWN_COMMITTED);
	REQUIRE(skedarSwarmRetireActor(state.get(), epoch, giant, SKEDAR_SWARM_REMOVED));
	REQUIRE(state->phase == SKEDAR_SWARM_FAILED);
}

TEST_CASE("Swarm attacks consume bounded tokens and recheck target at contact", "[swarm][encounter]")
{
	auto state = std::make_unique<skedar_swarm_encounter>();
	const auto config = configuration();
	REQUIRE(skedarSwarmInit(state.get(), &config, epoch, 0));
	admit(*state, 0, first);
	admit(*state, 2, second);

	skedar_swarm_attack jump{}, drop{}, ignored{};
	auto observed = target_at(2);
	REQUIRE(skedarSwarmBeginAttack(state.get(), epoch, 2, first, SKEDAR_SWARM_JUMP, &observed, &jump));
	REQUIRE_FALSE(skedarSwarmBeginAttack(state.get(), epoch, 2, second, SKEDAR_SWARM_DROP, &observed, &ignored));

	observed = target_at(3);
	REQUIRE_FALSE(skedarSwarmAcceptHit(state.get(), 3, &jump, &observed));
	REQUIRE_FALSE(skedarSwarmBeginAttack(state.get(), epoch, 3, second, SKEDAR_SWARM_JUMP, &observed, &ignored));
	REQUIRE(skedarSwarmBeginAttack(state.get(), epoch, 3, second, SKEDAR_SWARM_DROP, &observed, &drop));
	REQUIRE(state->active_attacks == 2);

	REQUIRE_FALSE(skedarSwarmAcceptHit(state.get(), 4, &jump, &observed));
	observed = target_at(4, {player.prop_index, player.prop_generation + 1});
	REQUIRE_FALSE(skedarSwarmAcceptHit(state.get(), 4, &jump, &observed));
	for (const unsigned invalid: {SKEDAR_SWARM_TARGET_LIVE, SKEDAR_SWARM_TARGET_HOSTILE,
			SKEDAR_SWARM_TARGET_VISIBLE, SKEDAR_SWARM_TARGET_IN_RANGE}) {
		observed = target_at(4);
		observed.flags &= ~invalid;
		REQUIRE_FALSE(skedarSwarmAcceptHit(state.get(), 4, &jump, &observed));
	}
	observed = target_at(4);
	REQUIRE(skedarSwarmAcceptHit(state.get(), 4, &jump, &observed));
	REQUIRE_FALSE(skedarSwarmAcceptHit(state.get(), 4, &jump, &observed));
	REQUIRE(skedarSwarmEndAttack(state.get(), 4, &jump));
	REQUIRE_FALSE(skedarSwarmBeginAttack(state.get(), epoch, 4, first, SKEDAR_SWARM_JUMP, &observed, &ignored));

	REQUIRE(skedarSwarmRetireActor(state.get(), epoch, second, SKEDAR_SWARM_DEFEATED));
	REQUIRE(state->active_attacks == 0);
	observed = target_at(5);
	REQUIRE_FALSE(skedarSwarmAcceptHit(state.get(), 5, &drop, &observed));
	observed = target_at(10);
	REQUIRE(skedarSwarmBeginAttack(state.get(), epoch, 10, first, SKEDAR_SWARM_JUMP, &observed, &jump));
	REQUIRE(skedarSwarmAdvanceAttacks(state.get(), epoch, 16));
	REQUIRE(state->active_attacks == 0);
	observed = target_at(16);
	REQUIRE_FALSE(skedarSwarmAcceptHit(state.get(), 16, &jump, &observed));
}

TEST_CASE("Giant starts a fresh placement budget after tunnel shutdown", "[swarm][encounter]")
{
	auto state = std::make_unique<skedar_swarm_encounter>();
	const auto config = configuration();
	REQUIRE(skedarSwarmInit(state.get(), &config, epoch, 0));
	skedar_swarm_ticket ticket{};
	for (uint32_t tick: {0u, 2u}) {
		REQUIRE(skedarSwarmRequestSpawn(state.get(), epoch, tick, &ticket));
		REQUIRE(skedarSwarmResolveSpawn(state.get(), &ticket, ticket.issued_tick, {}) == SKEDAR_SWARM_SPAWN_RETRY);
	}
	for (int tunnel = 0; tunnel < SKEDAR_SWARM_TUNNEL_COUNT; tunnel++) {
		REQUIRE(skedarSwarmCloseTunnel(state.get(), epoch, tunnel, 1));
	}

	REQUIRE(skedarSwarmRequestSpawn(state.get(), epoch, 4, &ticket));
	REQUIRE(ticket.kind == SKEDAR_SWARM_GIANT);
	REQUIRE(skedarSwarmResolveSpawn(state.get(), &ticket, ticket.issued_tick, {}) == SKEDAR_SWARM_SPAWN_RETRY);
	REQUIRE(skedarSwarmRequestSpawn(state.get(), epoch, 6, &ticket));
	REQUIRE(skedarSwarmResolveSpawn(state.get(), &ticket, ticket.issued_tick, giant) == SKEDAR_SWARM_SPAWN_COMMITTED);
}

TEST_CASE("Swarm advances after an expired placement without publishing the old prop", "[swarm][encounter]")
{
	auto state = std::make_unique<skedar_swarm_encounter>();
	const auto config = configuration();
	REQUIRE(skedarSwarmInit(state.get(), &config, epoch, 0));
	skedar_swarm_ticket expired{}, replacement{};
	REQUIRE(skedarSwarmRequestSpawn(state.get(), epoch, 0, &expired));
	REQUIRE(expired.tunnel_index == 0);
	REQUIRE_FALSE(skedarSwarmRequestSpawn(state.get(), epoch, 1, &replacement));
	REQUIRE(skedarSwarmRequestSpawn(state.get(), epoch, 2, &replacement));
	REQUIRE(replacement.tunnel_index == 1);
	REQUIRE(skedarSwarmResolveSpawn(state.get(), &expired, 2, first) == SKEDAR_SWARM_SPAWN_STALE);
	REQUIRE(skedarSwarmResolveSpawn(state.get(), &replacement, 2, second) == SKEDAR_SWARM_SPAWN_COMMITTED);
	REQUIRE(state->live_raiders == 1);
}

TEST_CASE("Swarm can retire actors and attack budgets after placement exhausts", "[swarm][encounter]")
{
	auto state = std::make_unique<skedar_swarm_encounter>();
	auto config = configuration();
	config.max_placement_failures = 1;
	REQUIRE(skedarSwarmInit(state.get(), &config, epoch, 0));
	admit(*state, 0, first);
	skedar_swarm_attack jump{};
	const auto observed = target_at(1);
	REQUIRE(skedarSwarmBeginAttack(state.get(), epoch, 1, first, SKEDAR_SWARM_JUMP, &observed, &jump));
	skedar_swarm_ticket ticket{};
	REQUIRE(skedarSwarmRequestSpawn(state.get(), epoch, 2, &ticket));
	REQUIRE(skedarSwarmResolveSpawn(state.get(), &ticket, 2, {}) == SKEDAR_SWARM_SPAWN_FAILED);
	REQUIRE(state->phase == SKEDAR_SWARM_FAILED);
	REQUIRE(skedarSwarmRetireActor(state.get(), epoch, first, SKEDAR_SWARM_REMOVED));
	REQUIRE(state->active_attacks == 0);
	REQUIRE(state->live_raiders == 0);
	REQUIRE(state->phase == SKEDAR_SWARM_FAILED);
}

TEST_CASE("Swarm shares the authored airborne budget across jump and drop", "[swarm][encounter]")
{
	auto state = std::make_unique<skedar_swarm_encounter>();
	auto config = configuration();
	config.attacks.airborne = 1;
	REQUIRE(skedarSwarmInit(state.get(), &config, epoch, 0));
	admit(*state, 0, first);
	admit(*state, 2, second);
	skedar_swarm_attack jump{}, drop{};
	auto seen = target_at(2);
	REQUIRE(skedarSwarmBeginAttack(state.get(), epoch, 2, first, SKEDAR_SWARM_JUMP, &seen, &jump));
	seen = target_at(3);
	REQUIRE_FALSE(skedarSwarmBeginAttack(state.get(), epoch, 3, second, SKEDAR_SWARM_DROP, &seen, &drop));
	REQUIRE(skedarSwarmEndAttack(state.get(), 3, &jump));
	REQUIRE(skedarSwarmBeginAttack(state.get(), epoch, 3, second, SKEDAR_SWARM_DROP, &seen, &drop));
}

TEST_CASE("A matching reply after its issuing tick charges only one failure", "[swarm][encounter]")
{
	auto state = std::make_unique<skedar_swarm_encounter>();
	const auto config = configuration();
	REQUIRE(skedarSwarmInit(state.get(), &config, epoch, 0));
	skedar_swarm_ticket expired{}, replacement{};
	REQUIRE(skedarSwarmRequestSpawn(state.get(), epoch, 0, &expired));
	REQUIRE(skedarSwarmResolveSpawn(state.get(), &expired, 1, first) == SKEDAR_SWARM_SPAWN_STALE);
	REQUIRE(skedarSwarmResolveSpawn(state.get(), &expired, 1, first) == SKEDAR_SWARM_SPAWN_STALE);
	REQUIRE(state->placement_failures == 1);
	REQUIRE(state->live_raiders == 0);
	REQUIRE(skedarSwarmRequestSpawn(state.get(), epoch, 2, &replacement));
	REQUIRE(replacement.tunnel_index == 1);
}
