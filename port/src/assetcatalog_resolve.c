/**
 * assetcatalog_resolve.c -- Standalone catalog-driven file resolution
 *
 * D3R-5: Context-aware file resolution for mod stages.
 *
 * The catalog is the single source of truth for mod stage file resolution.
 * When a mod stage is loading, this module redirects file requests to the
 * catalog component's actual files -- regardless of what the legacy system
 * asked for. This fixes B-17 (wrong maps loading) because we bypass the
 * broken g_Stages[] file ID patching entirely.
 *
 * Architecture:
 *   - Completely standalone: does not modify g_Stages[], modmgr, or any
 *     legacy data structures.
 *   - Stage context: lvReset() calls assetCatalogActivateStage() to set
 *     which catalog component (if any) should resolve files.
 *   - Smart bgdata redirect: when the game requests any bgdata file during
 *     stage loading, the component's actual file for that role wins --
 *     matching by suffix (.seg, _padsZ, _tilesZ) not by filename.
 *   - Exact match fallback for non-bgdata files (textures, props, etc.).
 *
 * Auto-discovered by CMake glob (port src slash star dot c).
 */

#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <PR/ultratypes.h>
#include "types.h"
#include "assetcatalog.h"
#include "assetcatalog_resolve.h"
#include "system.h"
#include "fs.h"

/* ========================================================================
 * Bgdata role classification
 *
 * Each map component has up to 5 bgdata files, identified by suffix:
 *   .seg       = main background geometry
 *   _padsZ     = pad (spawn/waypoint) data
 *   _tilesZ    = tile (texture) data
 *   _setupZ    = stage setup (objects, scripts)
 *   _mpsetupZ  = multiplayer setup
 * ======================================================================== */

#define BGF_NUM_ROLES 5

enum {
	BGF_SEG = 0,
	BGF_PADS,
	BGF_TILES,
	BGF_SETUP,
	BGF_MPSETUP,
};

/* Suffix table for role matching: tested in order, longest first */
static const struct {
	const char *suffix;
	s32 len;
	s32 role;
} s_BgSuffixes[] = {
	{ "_mpsetupZ", 9, BGF_MPSETUP },
	{ "_setupZ",   7, BGF_SETUP },
	{ "_tilesZ",   7, BGF_TILES },
	{ "_padsZ",    6, BGF_PADS },
	{ ".seg",      4, BGF_SEG },
};

#define NUM_SUFFIXES (s32)(sizeof(s_BgSuffixes) / sizeof(s_BgSuffixes[0]))

/* ========================================================================
 * State
 * ======================================================================== */

/* Active stage context -- set by assetCatalogActivateStage() */
static s32 s_ActiveStagenum = -1;
static const asset_entry_t *s_ActiveEntry = NULL;

/* Discovered bgdata files for the active component, indexed by role */
static char s_BgFilePaths[BGF_NUM_ROLES][FS_MAXPATH + 1];
static s32  s_BgFileValid[BGF_NUM_ROLES]; /* bool: path is populated */

/* Static buffer for resolved paths */
static char s_ResolveBuf[FS_MAXPATH + 1];

/* ========================================================================
 * Internal helpers
 * ======================================================================== */

static s32 fileExists(const char *path)
{
	struct stat st;
	return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

/**
 * Classify a bgdata filename by its suffix.
 * Returns the role index (BGF_SEG, etc.) or -1 if unrecognized.
 */
static s32 classifyBgFile(const char *filename)
{
	s32 len = (s32)strlen(filename);
	s32 i;

	for (i = 0; i < NUM_SUFFIXES; i++) {
		if (len >= s_BgSuffixes[i].len &&
			strcmp(filename + len - s_BgSuffixes[i].len,
				s_BgSuffixes[i].suffix) == 0) {
			return s_BgSuffixes[i].role;
		}
	}

	return -1;
}

/**
 * Scan a component's bgdata/ directory and populate s_BgFilePaths[].
 * Called once when a stage component is activated.
 */
static void scanBgdataDir(const char *dirpath)
{
	char bgdir[FS_MAXPATH + 1];
	DIR *dp;
	struct dirent *de;
	s32 i;

	/* Clear previous state */
	for (i = 0; i < BGF_NUM_ROLES; i++) {
		s_BgFilePaths[i][0] = '\0';
		s_BgFileValid[i] = 0;
	}

	snprintf(bgdir, sizeof(bgdir), "%s/bgdata", dirpath);
	dp = opendir(bgdir);
	if (!dp) {
		return; /* No bgdata dir -- not unusual for non-map components */
	}

	while ((de = readdir(dp)) != NULL) {
		if (de->d_name[0] == '.') {
			continue;
		}

		s32 role = classifyBgFile(de->d_name);
		if (role >= 0 && role < BGF_NUM_ROLES && !s_BgFileValid[role]) {
			snprintf(s_BgFilePaths[role], sizeof(s_BgFilePaths[role]),
				"%s/%s", bgdir, de->d_name);

			/* Verify the file actually exists (symlinks might be broken) */
			if (fileExists(s_BgFilePaths[role])) {
				s_BgFileValid[role] = 1;
				sysLogPrintf(LOG_NOTE, "CATALOG RESOLVE: bgdata[%d] = %s",
					role, de->d_name);
			} else {
				s_BgFilePaths[role][0] = '\0';
			}
		}
	}

	closedir(dp);
}

/* ========================================================================
 * Stagenum Lookup
 * ======================================================================== */

typedef struct {
	s32 stagenum;
	const asset_entry_t *result;
} find_map_ctx_t;

static void findMapCb(const asset_entry_t *entry, void *userdata)
{
	find_map_ctx_t *ctx = (find_map_ctx_t *)userdata;

	if (ctx->result) {
		return;
	}

	if (entry->bundled) {
		return;
	}

	if (!entry->enabled) {
		return;
	}

	if (entry->ext.map.stagenum == ctx->stagenum) {
		ctx->result = entry;
	}
}

const asset_entry_t *assetCatalogFindModMapByStagenum(s32 stagenum)
{
	find_map_ctx_t ctx;
	ctx.stagenum = stagenum;
	ctx.result = NULL;

	assetCatalogIterateByType(ASSET_MAP, findMapCb, &ctx);

	return ctx.result;
}

/* ========================================================================
 * Stage Activation
 * ======================================================================== */

void assetCatalogActivateStage(s32 stagenum)
{
	s32 i;

	if (stagenum <= 0) {
		s_ActiveStagenum = -1;
		s_ActiveEntry = NULL;
		for (i = 0; i < BGF_NUM_ROLES; i++) {
			s_BgFileValid[i] = 0;
		}
		return;
	}

	const asset_entry_t *entry = assetCatalogFindModMapByStagenum(stagenum);

	if (entry && entry->dirpath[0]) {
		s_ActiveStagenum = stagenum;
		s_ActiveEntry = entry;

		/* Scan the component's bgdata directory to discover actual files.
		 * This is the core of "catalog as source of truth" -- we know
		 * exactly which files this component has, and we serve THOSE
		 * regardless of what the legacy system asks for. */
		scanBgdataDir(entry->dirpath);

		sysLogPrintf(LOG_NOTE,
			"CATALOG RESOLVE: activated stage 0x%02x -> %s",
			stagenum, entry->dirpath);
	} else {
		s_ActiveStagenum = -1;
		s_ActiveEntry = NULL;
		for (i = 0; i < BGF_NUM_ROLES; i++) {
			s_BgFileValid[i] = 0;
		}
	}
}

void assetCatalogDeactivateStage(void)
{
	s32 i;

	if (s_ActiveEntry) {
		sysLogPrintf(LOG_NOTE, "CATALOG RESOLVE: deactivated stage 0x%02x",
			s_ActiveStagenum);
	}
	s_ActiveStagenum = -1;
	s_ActiveEntry = NULL;
	for (i = 0; i < BGF_NUM_ROLES; i++) {
		s_BgFileValid[i] = 0;
	}
}

/* ========================================================================
 * File Resolution -- catalog as single source of truth
 * ======================================================================== */

const char *assetCatalogResolvePath(const char *relPath)
{
	if (!s_ActiveEntry || !relPath || !relPath[0]) {
		return NULL;
	}

	/*
	 * Strip the "files/" prefix that romdata prepends.
	 * After stripping, a typical path looks like: "bgdata/bg_arec.seg"
	 */
	const char *stripped = relPath;
	if (strncmp(relPath, "files/", 6) == 0) {
		stripped = relPath + 6;
	}

	/*
	 * SMART BGDATA REDIRECT (catalog as source of truth)
	 *
	 * If the request is for a bgdata file, the catalog's discovered file
	 * for that role wins -- regardless of the requested filename. This
	 * fixes B-17: even if g_Stages[] was patched with wrong file IDs,
	 * the catalog knows which bgdata files this component actually has.
	 *
	 * Example: game requests "bgdata/bg_WRONG.seg" because g_Stages[]
	 * was corrupted. We detect the .seg suffix, look up our discovered
	 * .seg file for this component, and return that instead.
	 */
	if (strncmp(stripped, "bgdata/", 7) == 0) {
		const char *filename = stripped + 7;  /* after "bgdata/" */
		s32 role = classifyBgFile(filename);

		if (role >= 0 && role < BGF_NUM_ROLES && s_BgFileValid[role]) {
			/* Return the component's actual file for this role */
			strncpy(s_ResolveBuf, s_BgFilePaths[role], FS_MAXPATH);
			s_ResolveBuf[FS_MAXPATH] = '\0';
			return s_ResolveBuf;
		}

		/* Unrecognized bgdata suffix -- fall through to exact match */
	}

	/*
	 * EXACT MATCH (non-bgdata files: textures, props, etc.)
	 *
	 * For non-bgdata requests, check if the exact file exists in the
	 * component directory. This handles supplementary files that mods
	 * might include (shared textures, prop models, etc.).
	 */
	snprintf(s_ResolveBuf, sizeof(s_ResolveBuf), "%s/%s",
		s_ActiveEntry->dirpath, stripped);

	if (fileExists(s_ResolveBuf)) {
		return s_ResolveBuf;
	}

	/* Not found in component directory -- fall through to legacy */
	return NULL;
}
