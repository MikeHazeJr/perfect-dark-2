#ifndef PD_WEAPON_GRAPH_V2_H
#define PD_WEAPON_GRAPH_V2_H
/* D-006A executable kernel. Production adapters own gameplay admission. */
#include <stddef.h>
#include <stdint.h>
#include "weapon_graph_runtime.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct wg_v2_program wg_v2_program;
typedef struct wg_v2_instance wg_v2_instance;
typedef enum wg_v2_result {
    WG_V2_COMPLETED, WG_V2_RUNNING, WG_V2_WAITING, WG_V2_BLOCKED,
    WG_V2_FAILED, WG_V2_CANCELLED, WG_V2_QUEUED, WG_V2_IGNORED
} wg_v2_result;
typedef enum wg_v2_gate { WG_V2_AMMO_AVAILABLE, WG_V2_COOLDOWN_READY } wg_v2_gate;
typedef struct wg_v2_action {
    const char *source_node_id;
    const char *asset_id;
    const char *mode;
    const char *module;                 /* og.fire.hitscan */
    uint32_t module_version;            /* 1 */
    const weapon_graph_ir_t *typed;     /* one synthetic node; owned by program */
    const char *params_json;            /* original validated bytes; owned */
    size_t params_size;
} wg_v2_action;

/* Host callbacks are synchronous/non-reentrant, must not throw, and must not
 * free the instance. start accepts the native attack; it does NOT fire a shot.
 * Only tick/wake may advance that accepted native lifecycle. cleanup is called
 * exactly once after every start, even when start blocks/fails with NULL state.
 * cancel is called before cleanup only for outstanding running/waiting work.
 * Neither this executor nor either gate owns an ammunition debit. prepare is
 * validation-only: it must not retain allocations or alter gameplay state. */
typedef struct wg_v2_host {
    int (*alive)(void *host, uint64_t owner_generation);
    int (*prepare)(void *host, const wg_v2_action *, char *error, size_t cap);
    int (*gate)(void *host, wg_v2_gate, int ammo_slot, int required,
                char *error, size_t cap); /* 1 pass, 0 blocked, -1 failed */
    wg_v2_result (*start)(void *host, const wg_v2_action *, uint64_t ticket,
                         void **state, char *error, size_t cap);
    wg_v2_result (*tick)(void *host, const wg_v2_action *, uint64_t ticket,
                        void *state, double simulation_delta,
                        char *error, size_t cap);
    wg_v2_result (*wake)(void *host, const wg_v2_action *, uint64_t ticket,
                        void *state, uint32_t event, char *error, size_t cap);
    void (*cancel)(void *host, const wg_v2_action *, uint64_t ticket,
                   void *state, const char *reason);
    void (*cleanup)(void *host, const wg_v2_action *, uint64_t ticket, void *state);
    void (*trace)(void *host, const char *node, const char *port, uint64_t activation);
} wg_v2_host;

wg_v2_program *wgV2Compile(const char *json, size_t size, uint64_t source_generation,
                           const char *dependency_sha256, char *error, size_t cap);
void wgV2ProgramRetain(wg_v2_program *);
void wgV2ProgramRelease(wg_v2_program *);
const char *wgV2ProgramSourceHash(const wg_v2_program *);
const char *wgV2ProgramDependencyHash(const wg_v2_program *);
const char *wgV2CompilerVersion(void);
size_t wgV2ProgramNodeCount(const wg_v2_program *);
const char *wgV2ProgramAssetId(const wg_v2_program *);
const char *wgV2ProgramMode(const wg_v2_program *);
uint64_t wgV2ProgramGeneration(const wg_v2_program *);
/* Borrowed action view for source preparation. Returns 0 for a non-action node
 * or invalid index; its pointers live as long as the retained program. */
int wgV2ProgramAction(const wg_v2_program *, size_t node_index, wg_v2_action *out);

wg_v2_instance *wgV2Bind(wg_v2_program *, const wg_v2_host *, void *host,
                        uint64_t owner_generation, char *error, size_t cap);
/* Input only queues a press; it never starts/ticks an action. Sequence must
 * strictly increase. Coalesce one pending press, retaining the latest sequence.
 * Release drops pending input, not an already accepted native attack. */
wg_v2_result wgV2Press(wg_v2_instance *, uint64_t press_sequence,
                      uint64_t owner_generation, uint64_t source_generation);
void wgV2ReleasePendingPress(wg_v2_instance *);
/* Call at the actual native idle admission seam AFTER switch handling. One
 * unique visit chooses pending physical press OR native-ready-while-held, never
 * both. A blocked/empty physical route does not fall through to a held event.
 * Visit identity is per native admission, not per render/simulation frame: the
 * native hand loop may legitimately return to idle again within one update.
 * Optional held-ready export absent means physical-press-only authoring. */
wg_v2_result wgV2NativeIdle(wg_v2_instance *, uint64_t idle_visit, int trigger_held,
                           uint64_t owner_generation, uint64_t source_generation);
wg_v2_result wgV2Advance(wg_v2_instance *, uint64_t simulation_step, double delta,
                        uint64_t owner_generation, uint64_t source_generation);
wg_v2_result wgV2Wake(wg_v2_instance *, uint64_t ticket, uint32_t native_event,
                     uint64_t owner_generation, uint64_t source_generation);
void wgV2Cancel(wg_v2_instance *, const char *reason);
void wgV2Destroy(wg_v2_instance *);
const char *wgV2Error(const wg_v2_instance *);
uint64_t wgV2ActiveTicket(const wg_v2_instance *, const char *node_id);
int wgV2Busy(const wg_v2_instance *);

#ifdef __cplusplus
}
#endif
#endif
