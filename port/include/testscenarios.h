/*
 * testscenarios.h -- Settings > Debug > Test Scenarios entry points (S483).
 *
 * Surface: a small dropdown in the Debug tab launches one of three
 * benchmarking sessions ("The Grid - Empty Map", "Swarm - CPU Bots",
 * "Swarm - GPU Boids"). Empty Map routes through the existing Grid
 * session-start path. Swarm scenarios enter through matchStart() so MP
 * arenas use the MP setup and manifest path.
 *
 * Per the design doc context/designs/gpu-swarm-and-test-scenarios-2026-04-27.md
 * (sections A.3, F.4, G.1).
 *
 * Test scenarios are local-only; the dropdown greys out in netplay. State
 * is volatile and never persisted to disk. Gated by PD_DEV_BUILD at the UI
 * level; the runtime is unconditional so calls from anywhere fail-soft.
 */

#ifndef PORT_TESTSCENARIOS_H
#define PORT_TESTSCENARIOS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <PR/ultratypes.h>

typedef enum {
    TESTSCEN_NONE        = 0,
    TESTSCEN_EMPTY_MAP   = 1,  /* "The Grid - Empty Map"  */
    TESTSCEN_SWARM_CPU   = 2,  /* "Swarm - CPU Bots"      */
    TESTSCEN_SWARM_GPU   = 3,  /* "Swarm - GPU Boids"     */
    TESTSCEN_COUNT
} test_scenario_t;

/* Swarm execution method.
 *
 * B-308 first slice (c3807, 2026-05-15): expanded from the original 2-value
 * CPU/GPU enum to a 3-value CPU/GPU_POS_ONLY/GPU_FULL enum so the GPU bot AI
 * pipeline (see context/designs/in-flight/gpu-swarm-bot-pipeline.md) can be
 * staged incrementally:
 *
 *   - SWARM_METHOD_CPU         -- real bot AI on CPU via GAILIST_AIBOT_INIT.
 *   - SWARM_METHOD_GPU_POS_ONLY -- GPU compute drives positions only; chrs
 *     are passive props with no aibot. This is the historical "Swarm-GPU"
 *     behaviour (S593d), now kept as an explicit diagnostic submode.
 *   - SWARM_METHOD_GPU_FULL    -- GPU compute also makes AI decisions
 *     (per-bot action class, anim key, fire request, target index, and
 *     benchmark jump/surface requests). CPU reads the decisions back and
 *     applies benchmark-local side effects so this path matches the CPU
 *     swarm behavior contract.
 *
 * SWARM_METHOD_GPU is kept as a backward-compat alias for GPU_POS_ONLY so
 * existing call sites (HUD label, log lines) compile unchanged. The launch
 * dispatch in testscenarios.c maps TESTSCEN_SWARM_GPU to GPU_FULL by
 * default; ACTION_TESTSCEN_GPU_FULL_TOGGLE flips to GPU_POS_ONLY when an
 * explicit position-only diagnostic run is needed. */
typedef enum {
    SWARM_METHOD_CPU          = 0,
    SWARM_METHOD_GPU_POS_ONLY = 1,
    SWARM_METHOD_GPU_FULL     = 2,
    SWARM_METHOD_COUNT,
} swarm_method_t;

/* Backward-compat alias: the original code referred to "GPU mode" without
 * distinguishing the AI subtype. New code should use GPU_POS_ONLY or
 * GPU_FULL explicitly. */
#define SWARM_METHOD_GPU SWARM_METHOD_GPU_POS_ONLY

/* Maximum swarm count across the cycle. S594h-Unit-A bumped this
 * from 256 to 4096 (16x) so the benchmark can probe heavy-load
 * regimes; the chr pool, model pool, and rwdata bindings size to
 * this cap at level load. The cycler ladder is in
 * port/include/swarm_test.h::SWARM_TEST_CYCLE. */
#define TESTSCEN_SWARM_MAX_COUNT 4096

/* Initial bot count when a swarm scenario starts. */
#define TESTSCEN_SWARM_INITIAL_COUNT 4

/* Launch a scenario. Returns 1 on success, 0 on failure (already running,
 * netplay active, GPU scenario picked but compute unavailable, etc.). */
s32 testScenarioLaunch(test_scenario_t scen, const char *map_id);

/* Active scenario / method. Returns TESTSCEN_NONE if no scenario is
 * currently armed. */
test_scenario_t testScenarioActive(void);
swarm_method_t  testScenarioActiveMethod(void);

/* B-308 first slice (c3807, 2026-05-15): cycle the GPU sub-method between
 * GPU_POS_ONLY and GPU_FULL while a GPU swarm scenario is armed. No-op
 * when the active scenario is not TESTSCEN_SWARM_GPU. Does NOT respawn
 * chrs; both GPU sub-methods share the same passive-prop chr layout, so
 * the only thing that changes is which compute kernel path runs (and
 * which fields the readback consumer applies). */
void testScenarioCycleGpuSubmode(void);

/* True iff the active scenario is one of the swarm variants (CPU or GPU). */
s32 testScenarioIsSwarmActive(void);

/* The cap to add into setup.c's chr/prop budget when a swarm scenario is
 * active. Returns TESTSCEN_SWARM_MAX_COUNT when armed, 0 otherwise. */
s32 testScenarioGetSwarmMaxCount(void);

/* The current armed bot count (initial value or cycler position).
 * Used by swarm_test.c when respawning the swarm. */
s32 testScenarioGetCurrentSwarmCount(void);
void testScenarioSetCurrentSwarmCount(s32 count);

/* Reset all test-scenario state. Called on stage exit, disconnect, or
 * scenario change. Idempotent. */
void testScenarioReset(void);

/* True iff the current network state allows launching a test scenario.
 * (Single-player local only; greys out in any net mode != NONE.) */
s32 testScenarioCanLaunch(void);

/* Reason text for why launch is unavailable. Returns NULL when launch is
 * permitted. Used by the dropdown tooltip. */
const char *testScenarioWhyDisabled(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PORT_TESTSCENARIOS_H */
