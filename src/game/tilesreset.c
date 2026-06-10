#include <ultra64.h>
#include "constants.h"
#include "game/tiles.h"
#include "game/bg.h"
#include "game/file.h"
#include "bss.h"
#include "data.h"
#include "types.h"
#include "system.h"
#include "assetcatalog.h"
#include "assetload.h"
#include "asset_fallback_telemetry.h" /* c3849 Wave 1 */
#include "asset_source_debug.h"
#include "scenario_source_runtime.h"

void stageParseTiles(void);

void tilesReset(void)
{
	s32 index = bgGetStageIndex(g_Vars.stagenum);
	catalog_stage_result_t stage;

	if (index < 0) {
		index = 0;
	}

	catalogGetStageResultByIndex(index, &stage);

	g_LoadType = LOADTYPE_TILES;
	{
		s32 source_size = 0;
		s32 source_rooms = 0;
		s32 source_tiles = 0;
		u8 *source_tiles_data = scenarioSourceLoadTilesForStage(&stage,
			g_Vars.normmplayerisrunning, &source_size,
			&source_rooms, &source_tiles);
		if (source_tiles_data) {
			g_TileFileData.u8 = source_tiles_data;
			g_TileNumRooms = *g_TileFileData.u32;
			g_TileRooms = g_TileFileData.u32 + 1;
			sysLogPrintf(LOG_NOTE,
				"TILES: using scenario source scene-derived tile cache rooms=%d tiles=%d bytes=%d",
				source_rooms, source_tiles, source_size);
			stageParseTiles();
			return;
		}
	}

	assetSourceDebugFatalHandleFallback(ASSET_SCENARIO, "tiles",
		stage.entry && stage.entry->id[0] ? stage.entry->id : "?",
		stage.tile_handle);
	/* c3849 Wave 1: record only when a scenario source is registered. */
	if (scenarioSourceFindEntryForStage(&stage,
			g_Vars.normmplayerisrunning) != NULL) {
		sysLoudFailf("FALLBACK",
			"tiles fileid=%d scenario source failed -> ROM handle",
			(s32)stage.tilefileid);
		assetFallbackRecord(ASSET_SCENARIO, (s32)stage.tilefileid,
			"tiles source -> ROM handle");
	}
	g_TileFileData.u8 = assetLoadToNew(stage.tile_handle, FILELOADMETHOD_DEFAULT, LOADTYPE_TILES);
	if (!g_TileFileData.u8) {
		sysLogPrintf(LOG_ERROR, "TILES: failed to load tilefileid=%d for stage index=%d",
			stage.tilefileid, index);
		g_TileNumRooms = 0;
		g_TileRooms = NULL;
		return;
	}
	g_TileNumRooms = *g_TileFileData.u32;
	g_TileRooms = g_TileFileData.u32 + 1;

	stageParseTiles();
}

#define mult6(a) (((a) << 1) + ((a) << 2))

void stageParseTiles(void)
{
	struct geo *geo = (struct geo *)(g_TileFileData.u8 + g_TileRooms[0]);
	struct geo *end = (struct geo *)(g_TileFileData.u8 + g_TileRooms[g_TileNumRooms]);

	while (geo < end) {
		if (geo->type == GEOTYPE_TILE_I) {
			struct geotilei *tile = (struct geotilei *) geo;
			tile->xmin = mult6(tile->xmin) + 14;
			tile->xmax = mult6(tile->xmax) + 14;
			tile->ymin = mult6(tile->ymin) + 16;
			tile->ymax = mult6(tile->ymax) + 16;
			tile->zmin = mult6(tile->zmin) + 18;
			tile->zmax = mult6(tile->zmax) + 18;
			geo = (struct geo *)((u8 *)geo + (uintptr_t)(geo->numvertices - 0x40) * 6 + 0x18e);
		} else if (geo->type == GEOTYPE_TILE_F) {
			geo = (struct geo *)((u8 *)geo + (uintptr_t)(geo->numvertices - 0x40) * 12 + 0x310);
		} else if (geo->type == GEOTYPE_BLOCK) {
			geo = (struct geo *)((u8 *)geo + sizeof(struct geoblock));
		} else if (geo->type == GEOTYPE_CYL) {
			geo = (struct geo *)((u8 *)geo + sizeof(struct geocyl));
		}
	}
}
