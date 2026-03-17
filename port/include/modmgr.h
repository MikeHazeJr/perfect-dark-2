#ifndef _IN_MODMGR_H
#define _IN_MODMGR_H

#include <PR/ultratypes.h>
#include "fs.h"

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
#define MODMGR_MODS_DIR         "mods"

// Mod info structure — one per discovered mod
typedef struct modinfo {
	char id[MODMGR_ID_LEN];
	char name[MODMGR_NAME_LEN];
	char version[MODMGR_VERSION_LEN];
	char author[MODMGR_AUTHOR_LEN];
	char description[MODMGR_DESC_LEN];
	char dirpath[FS_MAXPATH + 1];       // absolute path to mod directory
	u32  contenthash;                    // hash of mod directory for network compare
	s32  enabled;                        // user preference (persisted)
	s32  loaded;                         // assets currently registered in tables
	s32  bundled;                        // shipped with the game
	s32  has_modconfig;                  // has legacy modconfig.txt
	s32  has_modjson;                    // has mod.json manifest
	s32  num_stages;                     // stages configured in modconfig
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
// Clears mod shadow arrays, restores base stage table, re-registers enabled mods,
// flushes texture cache. Caller should return to title screen after this.
void modmgrReload(void);

// ---- Registry queries ----

s32         modmgrGetCount(void);
modinfo_t  *modmgrGetMod(s32 index);
modinfo_t  *modmgrFindMod(const char *id);

// ---- Enable/Disable ----

void modmgrSetEnabled(s32 index, s32 enabled);
s32  modmgrIsDirty(void);              // true if enable state changed since last reload
void modmgrApplyChanges(void);         // save + reload + return to title

// ---- Config persistence ----

void modmgrSaveConfig(void);
void modmgrLoadConfig(void);

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

// Get the directory path for a specific mod (for modConfigLoad).
const char *modmgrGetModDir(s32 index);

// ---- Dynamic asset table accessors ----
// These transparently handle base game arrays + mod-added shadow arrays.
// Index 0..base_count-1 returns from base array, base_count..total-1 from mod array.

struct mpbody;
struct mphead;
struct mparena;

// Bodies: base array is g_MpBodies[63], mod additions in shadow array
s32             modmgrGetTotalBodies(void);
struct mpbody  *modmgrGetBody(s32 index);

// Heads: base array is g_MpHeads[76], mod additions in shadow array
s32             modmgrGetTotalHeads(void);
struct mphead  *modmgrGetHead(s32 index);

// Arenas: base array is g_MpArenas[75], mod additions in shadow array
s32             modmgrGetTotalArenas(void);
struct mparena *modmgrGetArena(s32 index);

// Get counts of mod-added entries only
s32 modmgrGetModBodyCount(void);
s32 modmgrGetModHeadCount(void);
s32 modmgrGetModArenaCount(void);

#endif // _IN_MODMGR_H
