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
#define MODMGR_MAX_TAGS          8    // S196: mod tag pool (free-form UI grouping labels)
#define MODMGR_TAG_LEN          32    // S196: tag string length

// Priority M / B-238: canonical archive extension. Plain ".zip" is also
// accepted as a fallback (manifest-probed). See port/src/modarchive.c
// and context/designs/pdmod-unified-mod-format.md.
#define MODMGR_PDMOD_EXT        ".pdmod"
#define MODMGR_ZIP_EXT          ".zip"

// Top-level subdirectory names under the mods/ root that are NOT installed
// mods. Trust invariant per design Section 8: only mods that are present
// directly under mods/ (or under category folders) are loader-trusted. The
// "shared" inbox is read-only-for-browsing; it must never auto-mount.
// Anything appearing in this list is skipped during enumeration.
#define MODMGR_RESERVED_NAMES_COUNT 3
#define MODMGR_RESERVED_NAMES_LIST   { "shared", "inbox", "untrusted" }

// Forward declaration -- archive handle is opaque to UI code that does not
// link modarchive.h.
struct mod_archive;
typedef struct mod_archive mod_archive_t;

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
	s32  has_audioini;                   // has audio.ini (ini-based audio mod)
	s32  valid;                          // true if manifest passed validation
	char validation_error[MODMGR_ERROR_LEN]; // if !valid, describes what's wrong
	s32  num_bodies;                     // bodies declared in mod.json
	s32  num_heads;                      // heads declared in mod.json
	s32  num_arenas;                     // arenas declared in mod.json

	// S196: Base-Game Template Mod extensions
	// ----------------------------------------
	// is_template : mod.json "template": true → refuse save-over, require
	//               Save As to create a user-editable clone (see
	//               modmgrSaveAs).  The Modding Hub also shows a lock
	//               icon and a "Base Game Templates" group for these.
	// tags        : free-form string labels parsed from mod.json "tags": [...]
	//               Used purely for UI grouping and display; the enforcement
	//               of template behaviour comes from is_template, not from
	//               any particular tag value.  Common labels are "base-game",
	//               "template", "chrome", but authors can invent their own.
	s32  is_template;                    // non-zero if mod.json had "template": true
	s32  num_tags;                       // number of entries in tags[]
	char tags[MODMGR_MAX_TAGS][MODMGR_TAG_LEN]; // parsed from mod.json "tags" array

	// Priority M / B-238: archive-backed mods (.pdmod / .zip)
	// ----------------------------------------------------------------
	// is_archive       : 1 if this mod was discovered as a .pdmod or .zip
	//                    archive (single-file mod). 0 = legacy folder mod.
	// archive_path     : full path to the archive file (when is_archive).
	// archive_handle   : opaque mod_archive handle if currently mounted.
	//                    NULL when the archive is registered but not loaded.
	// requires_restart : mod.json "requires_restart": true -- mod opts out
	//                    of hot-reload. The loader surfaces "Restart
	//                    required for [name] to take effect." when the
	//                    user toggles such a mod live.
	s32             is_archive;
	char            archive_path[FS_MAXPATH + 1];
	mod_archive_t  *archive_handle;
	s32             requires_restart;

	// pending_restart : runtime-only. Set by modmgrApplyChanges when this
	// mod has requires_restart=true and the user toggled `enabled` to a
	// value that differs from the current `loaded` state. The persistence
	// pass writes the user's INTENT to disk; the runtime keeps the
	// pre-apply loaded state until the next launch. UI surfaces this flag
	// as "Restart required for [name] to take effect."
	s32             pending_restart;
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

// Re-scan mods/ on disk and merge into the registry, preserving the
// enabled/loaded state of mods already known. New mods discovered on disk
// appear in the list with enabled=0; disappeared mods are dropped. Does
// NOT rebuild the catalog or change the active stage — intended for in-game
// content creation flows (e.g. Nine-Slice Chrome save, Map Import) that
// drop a new mod dir to disk and need the Mods list to reflect it without
// a title restart. Pair with modmgrSaveConfig() if the caller also wants
// a newly-discovered mod to persist as enabled.
void modmgrRescanDirectory(void);

// Rebuild asset catalog + load gates from the current registry (after rescan).
void modmgrSyncCatalogToRegistry(void);

// Validate a .pdmod archive before it is installed or mounted. This checks
// root mod.json plus the external-format public payload contract.
s32 modmgrValidateArchiveFile(const char *archive_path,
                              char *out_error,
                              s32 error_len);

// Install a validated .pdmod archive into mods/installed, rescan the registry,
// and optionally enable/apply it immediately. Returns the registry index on
// success, or -1 with out_error populated on failure.
s32 modmgrInstallArchiveFile(const char *archive_path,
                             s32 enable_now,
                             char *out_error,
                             s32 error_len);

// ---- Registry queries ----

s32         modmgrGetCount(void);
modinfo_t  *modmgrGetMod(s32 index);
modinfo_t  *modmgrFindMod(const char *id);

// ---- Enable/Disable ----

void modmgrSetEnabled(s32 index, s32 enabled);
s32  modmgrIsDirty(void);              // true if enable state changed since last reload
void modmgrApplyChanges(void);         // save + rebuild catalog in-place (no stage restart)

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
// The legacy binary manifest API (modmgrGetManifestHash / modmgrWriteManifest /
// modmgrReadManifest) was removed 2026-07-04 (c064) -- no live caller, superseded
// by the match_manifest_t / manifestBuildForHost path. See net/netmanifest.c.

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

// --- S196: Base-Game Template Mod accessors (for UI) ---

// Return 1 if this mod.json declared "template": true, 0 otherwise.
// Template mods are refused by the save-over path; callers must use
// modmgrSaveAs (future work) to clone to an editable user mod.
s32  modmgrGetModIsTemplate(s32 index);

// Number of tag strings in this mod's tags[] array.
s32  modmgrGetModNumTags(s32 index);

// Get tag string at tagIndex.  Returns NULL if out of range.
const char *modmgrGetModTag(s32 index, s32 tagIndex);

// Return 1 if this mod has the given tag (case-sensitive), 0 otherwise.
// Convenience helper for Modding Hub grouping (e.g. modmgrModHasTag(i, "chrome")).
s32  modmgrModHasTag(s32 index, const char *tag);

// --- Priority M / B-238: archive-backed mod accessors (for UI) ---

// Returns 1 if the mod was discovered as a .pdmod or .zip archive.
s32  modmgrGetModIsArchive(s32 index);

// Returns the on-disk archive path for archive-backed mods, or "" otherwise.
const char *modmgrGetModArchivePath(s32 index);

// Returns 1 if the mod's manifest opts out of hot-reload via
// "requires_restart": true. UI surfaces this when the user toggles the
// mod mid-session.
s32  modmgrGetModRequiresRestart(s32 index);

// Returns 1 if the user toggled this mod's enabled state but the change
// could not be applied live (because the mod has requires_restart=true).
// The new state is persisted to config and will take effect on next launch.
s32  modmgrGetModPendingRestart(s32 index);

// Returns the total number of mods currently flagged pending_restart.
// UI uses this to render a "Restart required" banner when > 0.
s32  modmgrGetPendingRestartCount(void);

// Signal that the Asset Catalog contents have changed.
// Causes all catalog-backed caches (arenas, future: bodies, heads)
// to lazily rebuild on next accessor call.
// Call after assetCatalogRegisterBaseGame(), assetCatalogScanComponents(),
// assetCatalogClearMods(), or any catalog mutation.
void modmgrCatalogChanged(void);

#endif // _IN_MODMGR_H
