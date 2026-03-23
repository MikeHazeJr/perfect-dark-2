/**
 * assetcatalog_resolve.c -- Standalone catalog-driven file resolution
 *
 * D3R-5: Context-aware file resolution for mod stages.
 *
 * When a mod stage is loading, this module provides an alternate file
 * resolution path through the catalog's component directories. It runs
 * alongside the legacy mod system without modifying any legacy structures.
 *
 * Component directories contain the actual bgdata, setup, and texture
 * files for each mod map (either as direct copies or symlinks). When
 * the game requests a file during stage loading, this module checks
 * the active component's directory first.
 *
 * Auto-discovered by CMake glob (port src slash star dot c).
 * No build system changes needed.
 */

#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <PR/ultratypes.h>
#include "types.h"
#include "assetcatalog.h"
#include "assetcatalog_resolve.h"
#include "system.h"
#include "fs.h"

/* ========================================================================
 * State
 * ======================================================================== */

/* Active stage context -- set by assetCatalogActivateStage() */
static s32 s_ActiveStagenum = -1;
static const asset_entry_t *s_ActiveEntry = NULL;

/* Static buffer for resolved paths */
static char s_ResolveBuf[FS_MAXPATH + 1];

/* ========================================================================
 * Internal: file existence check
 * ======================================================================== */

static s32 fileExists(const char *path)
{
	struct stat st;
	return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

/* ========================================================================
 * Stagenum Lookup
 * ======================================================================== */

/* Callback context for iterating map entries */
typedef struct {
	s32 stagenum;
	const asset_entry_t *result;
} find_map_ctx_t;

static void findMapCb(const asset_entry_t *entry, void *userdata)
{
	find_map_ctx_t *ctx = (find_map_ctx_t *)userdata;

	/* Already found one -- skip */
	if (ctx->result) {
		return;
	}

	/* Skip base game entries -- they use the legacy path */
	if (entry->bundled) {
		return;
	}

	/* Skip disabled entries */
	if (!entry->enabled) {
		return;
	}

	/* Match stagenum */
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
	if (stagenum <= 0) {
		s_ActiveStagenum = -1;
		s_ActiveEntry = NULL;
		return;
	}

	const asset_entry_t *entry = assetCatalogFindModMapByStagenum(stagenum);

	if (entry && entry->dirpath[0]) {
		s_ActiveStagenum = stagenum;
		s_ActiveEntry = entry;
		sysLogPrintf(LOG_NOTE, "CATALOG RESOLVE: activated stage 0x%02x -> %s",
			stagenum, entry->dirpath);
	} else {
		/* Base game stage or no catalog component -- deactivate */
		s_ActiveStagenum = -1;
		s_ActiveEntry = NULL;
	}
}

void assetCatalogDeactivateStage(void)
{
	if (s_ActiveEntry) {
		sysLogPrintf(LOG_NOTE, "CATALOG RESOLVE: deactivated stage 0x%02x",
			s_ActiveStagenum);
	}
	s_ActiveStagenum = -1;
	s_ActiveEntry = NULL;
}

/* ========================================================================
 * File Resolution
 * ======================================================================== */

const char *assetCatalogResolvePath(const char *relPath)
{
	/* No active mod stage -- skip */
	if (!s_ActiveEntry || !relPath || !relPath[0]) {
		return NULL;
	}

	/*
	 * The component directory mirrors the file structure expected by the
	 * game's ROM data system. For a map component, the directory looks like:
	 *
	 *   {dirpath}/bgdata/bg_arec.seg
	 *   {dirpath}/bgdata/bg_arec_padsZ
	 *   {dirpath}/bgdata/bg_arec_tilesZ
	 *
	 * The relPath from romdata is typically:
	 *   "files/bgdata/bg_arec.seg"
	 *
	 * We strip the "files/" prefix if present and check the component dir.
	 */
	const char *stripped = relPath;
	if (strncmp(relPath, "files/", 6) == 0) {
		stripped = relPath + 6;
	}

	/* Try: {component_dirpath}/{stripped} */
	snprintf(s_ResolveBuf, sizeof(s_ResolveBuf), "%s/%s",
		s_ActiveEntry->dirpath, stripped);

	if (fileExists(s_ResolveBuf)) {
		return s_ResolveBuf;
	}

	/* Not found in component directory -- fall through to legacy */
	return NULL;
}
