#ifndef PD_LOADER_POOL_H
#define PD_LOADER_POOL_H

#include <PR/ultratypes.h>
#include <stddef.h>

/*
 * port/include/loader_pool.h -- Catalog universality pivot Step 5
 * (2026-05-03). Heavyweight typed pool for weapon / head / body / arena
 * runtime payloads, populated from per-asset .pdweapon / .pdhead / .pdbody /
 * .pdarena content via the universal directory walker.
 *
 * Scope split from the catalog row layer (assetcatalog) -- the catalog
 * stores per-row identity + unlock metadata; the pool stores the typed
 * runtime payload (struct weapon, struct weaponfunc_*, struct
 * inventory_ammo, head_data_t, body_data_t, arena_data_t). One pool per
 * record type; arena-style allocation; populated once at startup.
 *
 * Boot wiring: loader_walker_{weapon,head,body,arena}.c each call the
 * matching loaderPoolParse*Json helper while iterating their per-asset
 * subdirectory; loaderPoolFinalize() flips s_LoaderActive at the end of
 * the walker pass so the catalog managers can route through the pool.
 *
 * Single-thread invariant: parses run on the main thread at startup,
 * before any other system can read pool data. No locking.
 *
 * Logging channels:
 *   LOADER.POOL.WEAPON.OK / SCAN_FAIL / RESOLVE_FAIL / FIELD_UNKNOWN /
 *     POOL_FULL / STORED
 *   LOADER.POOL.HEAD.OK / RESOLVE_FAIL / FIELD_UNKNOWN
 *   LOADER.POOL.BODY.OK / RESOLVE_FAIL / FIELD_UNKNOWN
 *   LOADER.POOL.ARENA.OK / RESOLVE_FAIL / FIELD_UNKNOWN
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Reset the pools and clear the active flag. Called once before the
 * walker scan starts so a re-extraction (force_rewrite) sees an empty
 * pool. */
void loaderPoolReset(void);

/* Per-asset JSON parsers. `json` points at NUL-terminated JSON loaded
 * by the walker scaffold (plain or extracted from a ZIP manifest). The
 * envelope's index field (`weapon_id` / `headnum` / `bodynum` /
 * `arena_index`) selects the pool slot; subsequent fields populate the
 * typed payload. Returns 1 on success, 0 if the envelope index is out
 * of range (one log line at WARNING). */
s32 loaderPoolParseWeaponJson(const char *json, size_t json_len);
s32 loaderPoolParseWeaponJsonWithRuntimeSlot(const char *json, size_t json_len,
                s32 runtime_weapon_id);
s32 loaderPoolParseHeadJson(const char *json, size_t json_len);
s32 loaderPoolParseBodyJson(const char *json, size_t json_len);
/* c3844 Gate 2: parse a custom body/head manifest into a catalog-owned
 * private slot (forced_slot in the custom range), overriding the manifest's
 * absent bodynum/headnum. Parallels loaderPoolParseWeaponJsonWithRuntimeSlot. */
s32 loaderPoolParseBodyJsonForSlot(const char *json, size_t json_len, s32 forced_slot);
s32 loaderPoolParseHeadJsonForSlot(const char *json, size_t json_len, s32 forced_slot);
s32 loaderPoolParseArenaJson(const char *json, size_t json_len);

/* Per-asset .pdanim parser. Used by loader_walker_anim.c for public
 * weapon-animation commands.json sources; the parser extracts the command
 * array into the shared guncmd pool so subsequent parseWeapon
 * resolveAnimByName lookups find the right entry. */
s32 loaderPoolParseAnimationJson(const char *json, size_t json_len);
s32 loaderPoolParseAnimationSourceJson(const char *json, size_t json_len,
                const char *source_path);

/* Finalize: seed default aim / noise sentinels, flip s_LoaderActive,
 * emit the LOADER.POOL.*.OK summary line. Called once at the end of
 * the walker pass. */
void loaderPoolFinalize(void);

/* Active flag: 1 once loaderPoolFinalize has run AND at least one
 * weapon was parsed. Catalog managers gate their pool reads on this. */
s32 loaderPoolIsActive(void);

/* ---- Manager-side accessors (consumed by catalog_mgr_*.c) ---- */

struct weapon;
struct invaimsettings;
struct noisesettings;
struct guncmd;
struct aibotweaponpreference;
struct weapon_graph_archive_descriptor;
struct weapon_graph_held_function;

const struct weapon                *loaderPoolGetWeapon(s32 idx);
const struct invaimsettings        *loaderPoolGetDefaultAim(void);
const struct noisesettings         *loaderPoolGetDefaultNoise(void);
const struct aibotweaponpreference *loaderPoolGetBotPref(s32 idx);
s32         loaderPoolGetWeaponsRegistered(void);
const char *loaderPoolGetWeaponCatalogId(s32 idx);

/* B-1021: custom .pdweapon activation owns a loader-facing legacy adapter in
 * the same runtime slot as its compiled public graphs. The descriptor and
 * held functions are both parsed from public editable source; private
 * _meta/manifest.json is identity/provenance only and is never accepted as a
 * second behavior source. Install is transactional and Clear is paired with
 * the final catalog owner release. */
s32 loaderPoolInstallPublicWeaponAdapter(s32 runtime_weapon_id,
                const char *catalog_id, s32 model_filenum,
                s32 dual_wieldable,
                const struct weapon_graph_archive_descriptor *descriptor,
                const struct weapon_graph_held_function *primary,
                const struct weapon_graph_held_function *secondary,
                char *err, size_t err_cap);
void loaderPoolClearPublicWeaponAdapter(s32 runtime_weapon_id);

/* Animation-pool walking accessors used by the .pdanim / .pdweapon
 * emitters at extraction time. */
s32         loaderPoolGetAnimationCount(void);
const char *loaderPoolGetAnimationName(s32 idx);
s32         loaderPoolGetAnimationOpcodes(s32 idx,
                const struct guncmd **out_cmds, s32 *out_count);
const char *loaderPoolAnimationNameForCmds(const struct guncmd *cmds);
const char *loaderPoolAnimationSourceForCmds(const struct guncmd *cmds);

/* ---- Heads / bodies / arenas pool accessors ---- */

typedef struct head_data head_data_t;
typedef struct body_data body_data_t;
typedef struct arena_data arena_data_t;

s32                 loaderPoolHeadsActive(void);
const head_data_t  *loaderPoolGetHead(s32 idx);
s32                 loaderPoolGetHeadsRegistered(void);

s32                 loaderPoolBodiesActive(void);
const body_data_t  *loaderPoolGetBody(s32 idx);
s32                 loaderPoolGetBodiesRegistered(void);

s32                 loaderPoolArenasActive(void);
const arena_data_t *loaderPoolGetArena(s32 idx);
s32                 loaderPoolGetArenasRegistered(void);

/* Round-trip helper: encode a single struct guncmd back to JSON-ish
 * text. Used by tests that pin opcode codec round-trip behaviour. */
s32 loaderPoolEncodeOpcode(const struct guncmd *cmd, char *out_buf,
                            size_t out_n);

#ifdef __cplusplus
}
#endif

#endif /* PD_LOADER_POOL_H */
