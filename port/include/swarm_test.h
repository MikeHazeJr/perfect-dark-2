/*
 * swarm_test.h -- Swarm benchmark runtime (S483).
 *
 * Drives the per-frame behaviour of "Swarm - CPU Bots" and "Swarm - GPU
 * Boids" test scenarios:
 *   - Player setup (invincible, all guns, bottomless ammo).
 *   - Skedar swarm allocation in a separate test-mode chr table that
 *     does NOT go through botmgr / participant pool, so MAX_BOTS = 32
 *     is escaped (per design F.2.B).
 *   - Per-frame seek-player AI for CPU mode (mirrors the GPU shader's
 *     per-boid update for an apples-to-apples benchmark, F.5.B).
 *   - Per-frame GPU compute dispatch + readback for GPU mode.
 *   - D-pad-down / 0 cycler over 4 -> 8 -> 16 -> 32 -> 64 -> 128 -> 256.
 *   - HUD overlay + BENCHMARK.SWARM.{CPU,GPU} per-frame logging.
 *   - Local kill counter (existing damage path triggers chr->isdead;
 *     we poll once per tick and credit a kill).
 *
 * Hooks:
 *   - swarmTestTick is called from pdmain.c::mainTick once per frame.
 *     The first call after a stage load does session-start work
 *     (player setup, initial 4-bot spawn).
 *   - swarmTestOnSessionEnd is called from netDisconnect / stage exit.
 *
 * Design: context/designs/gpu-swarm-and-test-scenarios-2026-04-27.md
 */

#ifndef PORT_SWARM_TEST_H
#define PORT_SWARM_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <PR/ultratypes.h>

/* Per-frame entry. Idempotent + state-machine driven; safe to call from
 * mainTick unconditionally. Early-outs when no swarm scenario is
 * armed. */
void swarmTestTick(void);

/* Cleanup hook. Despawns all swarm chrs, resets state, calls into
 * testScenarioReset. Safe to call multiple times. */
void swarmTestOnSessionEnd(void);

/* HUD overlay entry. Called from the pdgui pre-frame stage in the
 * fast3d backend. Renders nothing when no swarm scenario is armed. */
void swarmTestRenderHud(void);

/* Read accessors for tests / external diagnostics. */
s32 swarmTestGetActiveCount(void);
s32 swarmTestGetKillCount(void);

/* Compile-time config. The ladder runs from 4 up to 4096. Two
 * regimes: small-step probe range (4..256) carried over from S593h,
 * then 256-bot increments up to 4096 per Mike's S594h-Unit-A
 * directive ("push the limits and see what kind of load we get").
 * Total: 8 small steps + 16 large steps = 24 entries. The cycler is
 * BIDIRECTIONAL; PgUp = next-higher count, PgDn = next-lower count. */
#define SWARM_TEST_CYCLE_STEPS    23  /* 4..256 (8) + 512..4096 in 256-step (15) */
extern const s32 SWARM_TEST_CYCLE[SWARM_TEST_CYCLE_STEPS];

/* Visibility mode for the player. Cycled by ACTION_TESTSCEN_VIS_TOGGLE.
 * NORMAL: standard bot target acquisition (LOS / range / hostility apply).
 * ALWAYS_SEE: swarm-mode override -- bots always know player's position.
 * INVISIBLE: player is undetectable; bots ignore the player entirely. */
typedef enum {
    SWARM_VIS_NORMAL = 0,
    SWARM_VIS_ALWAYS_SEE,
    SWARM_VIS_INVISIBLE,
    SWARM_VIS_COUNT
} swarm_vis_mode_t;

/* Team config. Set at session start by the Debug-menu picker.
 * SIMS_VS_PLAYERS: all bots on TEAM_ENEMY, player on TEAM_01, bots
 *   target the player. (Behavior carried over from S593h.)
 * TWO_TEAMS_PLUS_PLAYER: bots split 50/50 across team A (TEAM_ENEMY)
 *   and team B (a different combat team), all three teams hostile,
 *   three-way melee. Player on TEAM_01. */
typedef enum {
    SWARM_TEAMS_SIMS_VS_PLAYERS = 0,
    SWARM_TEAMS_TWO_TEAMS_PLUS_PLAYER,
    SWARM_TEAMS_COUNT
} swarm_team_mode_t;

/* Set the team mode for the next session start. Reads default
 * SIMS_VS_PLAYERS if never set. */
void swarmTestSetTeamMode(swarm_team_mode_t mode);
swarm_team_mode_t swarmTestGetTeamMode(void);

/* Read accessor for the current visibility mode (used by chr.c /
 * chraction.c overrides that gate on it). */
swarm_vis_mode_t swarmTestGetVisMode(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PORT_SWARM_TEST_H */
