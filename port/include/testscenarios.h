/*
 * testscenarios.h -- Settings > Debug > Test Scenarios entry points (S483).
 *
 * Surface: a small dropdown in the Debug tab launches one of three
 * benchmarking sessions ("The Grid - Empty Map", "Swarm - CPU Bots",
 * "Swarm - GPU Boids"). Implementation routes through the existing Grid
 * session-start path (pdguiForgeStartSessionOn) so the catalog/manifest
 * pipeline behaves exactly as it does for a normal Grid launch.
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

typedef enum {
    SWARM_METHOD_CPU = 0,
    SWARM_METHOD_GPU = 1,
} swarm_method_t;

/* Maximum swarm count across the cycle. The cycler steps
 * 4 -> 8 -> 16 -> 32 -> 64 -> 128 -> 256 -> 4. Used by setup.c to size the
 * model/prop/chr pool budget when a swarm scenario is active. */
#define TESTSCEN_SWARM_MAX_COUNT 256

/* Initial bot count when a swarm scenario starts. */
#define TESTSCEN_SWARM_INITIAL_COUNT 4

/* Launch a scenario. Returns 1 on success, 0 on failure (already running,
 * netplay active, GPU scenario picked but compute unavailable, etc.). */
s32 testScenarioLaunch(test_scenario_t scen, const char *map_id);

/* Active scenario / method. Returns TESTSCEN_NONE if no scenario is
 * currently armed. */
test_scenario_t testScenarioActive(void);
swarm_method_t  testScenarioActiveMethod(void);

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
