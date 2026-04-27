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

/* Compile-time config. The cycle stops at 256 (top-of-cycle, per the
 * directive) and wraps back to 4. */
#define SWARM_TEST_CYCLE_STEPS    7
extern const s32 SWARM_TEST_CYCLE[SWARM_TEST_CYCLE_STEPS];  /* 4,8,16,32,64,128,256 */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PORT_SWARM_TEST_H */
