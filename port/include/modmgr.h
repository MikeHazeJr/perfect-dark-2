#ifndef _IN_MODMGR_H
#define _IN_MODMGR_H

#include <PR/ultratypes.h>
#include "fs.h"
#include "sha256.h"

// Note: Do NOT include <stdbool.h> here — the codebase defines bool as s32
// in types.h (#define bool s32), and stdbool.h would conflict with that.

// Base game asset counts (must match array definitions in mplayer.c / setup.c)
#define MODMGR_BASE_BODIES      63
#define MODMGR_BASE_HEADS       76
#define MODMGR_BASE_ARENAS      75

// Limits
#define MODMGR_MAX_MODS         32
#define MODMGR_MAX_MOD_BODIES   64
#define MODMGR_MAX_MOD_HEADS    64
#define MODMGR_MAX_MOD_ARENAS   64
#define MODMGR_ID_LEN           64
#define MODMGR_NAME_LEN         128
#define MODMGR_VERSION_LEN      32
#define MODMGR_AUTHOR_LEN       64
#define MODMGR_DESC_LEN         256
#define MODMGR_FALLBACK_LEN     64
#define MODMGR_MAX_DEPS         16
#define MODMGR_DEP_ID_LEN       64
#define MODMGR_ERROR_LEN        256
#define MODMGR_MODS_DIR         "mods"

// Mod info structure — one per discovered mod
typedef struct modinfo {
	char id[MODMGR_ID_LEN];
	char name[MODMGR_NAME_LEN];
	char version[MODMGR_VERSION_LEN];
	char author[MODMGR_AUTHOR_LEN];
	char description[MODMGR_DESC_LEN];
	char base_fallback[MODMGR_FALLBACK_LEN]; // required: catalog ID to fall back to if mod fails
	char dependencies[MODMGR_MAX_DEPS][MODMGR_DEP_ID_LEN]; // optional: mod IDs this mod depends on
	s32  num_dependencies;               // number of entries in dependencies[]
	char dirpath[FS_MAXPATH + 1];       // absolute path to mod directory
	u32  contenthash;                    // CRC32 of mod id:version for quick compare
	u8   sha256[SHA256_DIGEST_SIZE];     // SHA-256 of mod.json for authoritative verification
	u32  size_bytes;                     // total size of mod directory (bytes), for download estimation
	s32  enabled;                        // user preference (persisted)
	s32  loaded;                         // assets currently registered in tables
	s32  bundled;                        // reserved; always 0 (no hardcoded bundled mods)
	s32  has_modjson;                    // has mod.json manifest
	s32  valid;                          // true if manifest passed validation
	char validation_error[MODMGR_ERROR_LEN]; // if !valid, describes what's wrong
	s32  num_bodies;                     // bodies declared in mod.json
	s32  num_heads;                      // heads declared in mod.json
	s32  num_arenas;                     // arenas declared in mod.json
} modinfo_t;

// ---- Lifecycle ----

// Scan mods/ directory, parse manifests, load config, register enabled mods.
// Call once during startup, after fsInit() but before game asset init.
void modmgrInit(void);

// Free any dynamic resources. Call on shutdown.
void modmgrShutdown(void);

// Rebuild asset tables from currently enabled mods (hot-toggle).
// Re-registers enabled mods, invalidates caches, flushes texture cache.
// Caller should return to title screen after this.
void modmgrReload(void);

// ---- Registry queries ----

s32         modmgrGetCount(void);
modinfo_t  *modmgrGetMod(s32 index);
modinfo_t  *modmgrFindMod(const char *id);

// ---- Enable/Disable ----

void modmgrSetEnabled(s32 index, s32 enabled);
s32  modmgrIsDirty(void);              // true if enable state changed since last reload
void modmgrApplyChanges(void);         // save + reload + return to title

// Check if all dependencies of a mod are enabled.
// Returns 0 if all satisfied, >0 = number of missing deps.
// Writes comma-separated list of missing dep IDs into `missing` (may be NULL).
s32  modmgrCheckDependencies(s32 index, char *missing, s32 misslen);

// Swap load order of two mods in the registry (for reordering).
void modmgrSwapOrder(s32 indexA, s32 indexB);

// ---- Config persistence ----

void modmgrSaveConfig(void);
void modmgrLoadConfig(void);

// ---- Component-level enable state (D3R-6) ----
//
// These persist per-component enabled/disabled state for the Asset Catalog.
// State file: mods/.modstate  (one disabled component ID per line; # = comment)
// Only non-bundled entries are ever written (bundled = base game, always on).

// Write current catalog disabled-component list to mods/.modstate.
// Call before modmgrApplyChanges() so user choices survive across sessions.
void modmgrSaveComponentState(void);

// Read mods/.modstate and mark matching catalog entries disabled.
// Silently ignores unknown IDs (component may have been removed).
// Call after assetCatalogScanComponents() in main.c startup.
void modmgrLoadComponentState(void);

// ---- Network ----

// Combined CRC32 of all enabled mod IDs+versions, for quick manifest compare
u32  modmgrGetManifestHash(void);

// Serialize enabled mod list into buffer. Returns bytes written.
s32  modmgrWriteManifest(u8 *buf, s32 maxlen);

// Compare received manifest against local mods.
// Returns 0 if compatible, >0 = number of missing mods.
// Writes human-readable missing mod list into `missing` buffer.
s32  modmgrReadManifest(const u8 *buf, s32 len, char *missing, s32 misslen);

// ---- Filesystem integration ----

// Resolve a relative file path through enabled mods (in load order).
// Returns full path if found in any enabled mod, or NULL if not found.
// This is called by the new fsFullPath() mod resolution path.
const char *modmgrResolvePath(const char *relPath);

// Get the directory path for a specific mod.
const char *modmgrGetModDir(s32 index);

// ---- Bot name mod override (P2) ----

#define MODMGR_MAX_BOT_PROFILES  18
#define MODMGR_BOT_NAME_LEN      16

// Get mod override name for a bot profile index (0-17).
// Returns the override string, or NULL if no override is active.
const char *modmgrGetBotProfileName(s32 profileIndex);

// Returns true if any bot-names mod override is currently active.
s32 modmgrHasBotNameOverride(void);

// ---- Dynamic asset table accessors ----
// All entries come from the Asset Catalog. Index 0..total-1 is valid.

struct mpbody;
struct mphead;
struct mparena;

s32             modmgrGetTotalBodies(void);
struct mpbody  *modmgrGetBody(s32 index);

s32             modmgrGetTotalHeads(void);
struct mphead  *modmgrGetHead(s32 index);

s32             modmgrGetTotalArenas(void);
struct mparena *modmgrGetArena(s32 index);

// Size threshold: mods exceeding this (in MB) trigger a confirmation prompt.
s32  modmgrGetSizeThresholdMB(void);
void modmgrSetSizeThresholdMB(s32 mb);

// Check if a mod exceeds the size threshold.
s32  modmgrExceedsThreshold(s32 index);

// ---- UI accessor helpers (safe from C++ without including types.h) ----

const char *modmgrGetModId(s32 index);
const char *modmgrGetModName(s32 index);
const char *modmgrGetModVersion(s32 index);
const char *modmgrGetModAuthor(s32 index);
const char *modmgrGetModDescription(s32 index);
const char *modmgrGetModBaseFallback(s32 index);
const char *modmgrGetModValidationError(s32 index);
s32         modmgrGetModEnabled(s32 index);
s32         modmgrGetModValid(s32 index);
u32         modmgrGetModSizeBytes(s32 index);
s32         modmgrGetModNumDeps(s32 index);
const char *modmgrGetModDep(s32 index, s32 depIndex);

// Get resolved mods directory path (set by modmgrInit).
// Returns NULL if no mods directory was found.
const char *modmgrGetModsDir(void);

// Signal that the Asset Catalog contents have changed.
// Causes all catalog-backed caches (arenas, future: bodies, heads)
// to lazily rebuild on next accessor call.
// Call after assetCatalogRegisterBaseGame(), assetCatalogScanComponents(),
// assetCatalogClearMods(), or any catalog mutation.
void modmgrCatalogChanged(void);

#endif // _IN_MODMGR_H
