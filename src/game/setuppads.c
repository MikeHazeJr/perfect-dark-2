#include <ultra64.h>
#include "constants.h"
#include "game/bondhead.h"
#include "game/bg.h"
#include "game/pad.h"
#include "game/setup.h"
#include "bss.h"
#include "lib/collision.h"
#include "lib/mtx.h"
#include "lib/anim.h"
#include "lib/model.h"
#include "data.h"
#include "types.h"
#include "platform.h"
#include "scenario_source_runtime.h"
#include "system.h"

static s32 s_SetupPadFileDataSize;

void setupSetPadFileDataSize(s32 size)
{
	s_SetupPadFileDataSize = size > 0 ? size : 0;
}

s32 setupGetPadFileDataSize(void)
{
	return s_SetupPadFileDataSize;
}

static bool setupPadFileContains(uintptr_t offset, s32 size)
{
	uintptr_t file_size = (uintptr_t)s_SetupPadFileDataSize;

	return s_SetupPadFileDataSize > 0
		&& size >= 0
		&& offset <= file_size
		&& (uintptr_t)size <= file_size - offset;
}

static bool setupPadFileListHasTerminator(uintptr_t offset)
{
	uintptr_t file_size = (uintptr_t)s_SetupPadFileDataSize;
	uintptr_t pos;

	if ((offset & (sizeof(s32) - 1)) != 0 || !setupPadFileContains(offset, sizeof(s32))) {
		return false;
	}

	for (pos = offset; pos + sizeof(s32) <= file_size; pos += sizeof(s32)) {
		if (*(s32 *)(g_StageSetup.padfiledata + pos) < 0) {
			return true;
		}
	}

	return false;
}

static bool setupPromoteWaypointOffsets(void)
{
	struct waypoint *waypoints;
	uintptr_t offset = g_PadsFile->waypointsoffset;
	s32 maxwaypoints;
	s32 numwaypoints = -1;
	s32 i;

	if ((offset & (sizeof(s32) - 1)) != 0 || !setupPadFileContains(offset, sizeof(struct waypoint))) {
		sysLogPrintf(LOG_WARNING,
			"SETUP.PADS: invalid waypoint table offset=%llu size=%d -- disabling waypoints",
			(unsigned long long)offset, s_SetupPadFileDataSize);
		g_StageSetup.waypoints = NULL;
		return false;
	}

	waypoints = (struct waypoint *)((uintptr_t)g_StageSetup.padfiledata + offset);
	maxwaypoints = (s32)(((uintptr_t)s_SetupPadFileDataSize - offset) / sizeof(struct waypoint));

	for (i = 0; i < maxwaypoints; i++) {
		uintptr_t neighbours;

		if (waypoints[i].padnum < 0) {
			numwaypoints = i;
			break;
		}

		if (waypoints[i].padnum >= g_PadsFile->numpads) {
			sysLogPrintf(LOG_WARNING,
				"SETUP.PADS: invalid waypoint padnum index=%d padnum=%d numpads=%d -- disabling waypoints",
				i, waypoints[i].padnum, g_PadsFile->numpads);
			g_StageSetup.waypoints = NULL;
			return false;
		}

		neighbours = (uintptr_t)waypoints[i].neighbours;

		if (!setupPadFileListHasTerminator(neighbours)) {
			sysLogPrintf(LOG_WARNING,
				"SETUP.PADS: invalid waypoint neighbours index=%d offset=%llu size=%d -- disabling waypoints",
				i, (unsigned long long)neighbours, s_SetupPadFileDataSize);
			g_StageSetup.waypoints = NULL;
			return false;
		}
	}

	if (numwaypoints < 0) {
		sysLogPrintf(LOG_WARNING,
			"SETUP.PADS: waypoint table reached file bounds offset=%llu size=%d -- disabling waypoints",
			(unsigned long long)offset, s_SetupPadFileDataSize);
		g_StageSetup.waypoints = NULL;
		return false;
	}

	for (i = 0; i < numwaypoints; i++) {
		waypoints[i].neighbours = (s32 *)((uintptr_t)g_StageSetup.padfiledata + (uintptr_t)waypoints[i].neighbours);
	}

	g_StageSetup.waypoints = waypoints;
	return true;
}

static bool setupPromoteWaygroupOffsets(void)
{
	struct waygroup *waygroups;
	uintptr_t offset = g_PadsFile->waygroupsoffset;
	s32 maxwaygroups;
	s32 numwaygroups = -1;
	s32 i;

	if ((offset & (sizeof(s32) - 1)) != 0 || !setupPadFileContains(offset, sizeof(struct waygroup))) {
		sysLogPrintf(LOG_WARNING,
			"SETUP.PADS: invalid waygroup table offset=%llu size=%d -- disabling waygroups",
			(unsigned long long)offset, s_SetupPadFileDataSize);
		g_StageSetup.waygroups = NULL;
		return false;
	}

	waygroups = (struct waygroup *)((uintptr_t)g_StageSetup.padfiledata + offset);
	maxwaygroups = (s32)(((uintptr_t)s_SetupPadFileDataSize - offset) / sizeof(struct waygroup));

	for (i = 0; i < maxwaygroups; i++) {
		uintptr_t neighbours;
		uintptr_t waypoints;

		if (waygroups[i].neighbours == NULL) {
			numwaygroups = i;
			break;
		}

		neighbours = (uintptr_t)waygroups[i].neighbours;
		waypoints = (uintptr_t)waygroups[i].waypoints;

		if (!setupPadFileListHasTerminator(neighbours) || !setupPadFileListHasTerminator(waypoints)) {
			sysLogPrintf(LOG_WARNING,
				"SETUP.PADS: invalid waygroup lists index=%d neighbours=%llu waypoints=%llu size=%d -- disabling waygroups",
				i, (unsigned long long)neighbours, (unsigned long long)waypoints,
				s_SetupPadFileDataSize);
			g_StageSetup.waygroups = NULL;
			return false;
		}
	}

	if (numwaygroups < 0) {
		sysLogPrintf(LOG_WARNING,
			"SETUP.PADS: waygroup table reached file bounds offset=%llu size=%d -- disabling waygroups",
			(unsigned long long)offset, s_SetupPadFileDataSize);
		g_StageSetup.waygroups = NULL;
		return false;
	}

	for (i = 0; i < numwaygroups; i++) {
		waygroups[i].neighbours = (s32 *)((uintptr_t)g_StageSetup.padfiledata + (uintptr_t)waygroups[i].neighbours);
		waygroups[i].waypoints = (s32 *)((uintptr_t)g_StageSetup.padfiledata + (uintptr_t)waygroups[i].waypoints);
	}

	g_StageSetup.waygroups = waygroups;
	return true;
}

/**
 * The function assumes that a pad file's data has been loaded from the ROM
 * and is pointed to by g_StageSetup.padfiledata. These pads are in a packed
 * format. During gameplay, the game uses padUnpack as needed to temporarily
 * populate pad structs from this data.
 *
 * setupPreparePads prepares the packed data by doing the following:
 * - populates the room field (if -1)
 * - multiplies each pad's bounding box by 1 (this is effectively a no op)
 * - sets the g_StageSetup pad/waygroup/waypoint/cover pointers
 * - promotes file offsets to RAM pointers
 * - does similar things for cover by calling setupPrepareCover()
 */
void setupPreparePads(void)
{
	struct packedpad *packedpad;
	RoomNum *roomsptr;
	s32 padnum;
	s32 numpads;
	s32 roomnum;
	struct pad pad;
	RoomNum inrooms[24];
	RoomNum aboverooms[22];
	u32 offset;
	u32 *wide_offsets;
	s32 wide_count;

	g_PadsFile = (struct padsfileheader *)g_StageSetup.padfiledata;
#ifdef PLATFORM_64BIT
	g_PadOffsets = (u16 *)(g_StageSetup.padfiledata + 0x20);
#else
	g_PadOffsets = (u16 *)(g_StageSetup.padfiledata + 0x14);
#endif
	wide_count = 0;
	wide_offsets = scenarioSourcePadsGetWideOffsets(
		(const u8 *)g_StageSetup.padfiledata, &wide_count);
	if (wide_offsets && wide_count == g_PadsFile->numpads) {
		padSetWideOffsets(wide_offsets);
	} else {
		if (wide_offsets) {
			sysLogPrintf(LOG_WARNING,
				"SETUP.PADS: source wide pad offset count mismatch count=%d numpads=%d -- using legacy offsets",
				wide_count, g_PadsFile->numpads);
		}
		padSetWideOffsets(NULL);
	}

	padnum = 0;
	numpads = g_PadsFile->numpads;

	for (; padnum < numpads; padnum++) {
		offset = padGetPackedOffset(padnum);
		packedpad = (struct packedpad *) &g_StageSetup.padfiledata[offset];
		padUnpack(padnum, PADFIELD_POS | PADFIELD_BBOX, &pad);

		// If room is negative (ie. not specified)
		if (packedpad->room < 0) {
			roomsptr = NULL;
			bgFindRoomsByPos(&pad.pos, inrooms, aboverooms, 20, NULL);

			if (inrooms[0] != -1) {
				roomsptr = inrooms;
			} else if (aboverooms[0] != -1) {
				roomsptr = aboverooms;
			}

			if (roomsptr != NULL) {
				roomnum = cdFindFloorRoomAtPos(&pad.pos, roomsptr);

				if (roomnum > 0) {
					packedpad->room = roomnum;
				} else {
					packedpad->room = roomsptr[0];
				}
			}
		}

		// Scale the bbox by 1 and save it back into the packed pad data.
		// Yeah, this is effectively doing nothing.
		if ((*(u32 *) packedpad >> 14) & PADFLAG_HASBBOXDATA) {
			f32 scale = 1;

			pad.bbox.xmin *= scale;
			pad.bbox.xmax *= scale;
			pad.bbox.ymin *= scale;
			pad.bbox.ymax *= scale;
			pad.bbox.zmin *= scale;
			pad.bbox.zmax *= scale;

			padCopyBboxFromPad(padnum, &pad);
		}
	}

	if (!setupPromoteWaypointOffsets()) {
		g_StageSetup.waygroups = NULL;
	} else {
		setupPromoteWaygroupOffsets();
	}

	if (g_PadsFile->numcovers > 0 && g_PadsFile->coversoffset != 0
			&& g_PadsFile->numcovers <= 8192
			&& setupPadFileContains(g_PadsFile->coversoffset,
				(s32)(g_PadsFile->numcovers * sizeof(struct coverdefinition)))) {
		g_StageSetup.cover = (void *) ((intptr_t)g_StageSetup.padfiledata + g_PadsFile->coversoffset);
	} else {
		if (g_PadsFile->numcovers > 0 || g_PadsFile->coversoffset != 0) {
			sysLogPrintf(LOG_WARNING,
				"SETUP.PADS: invalid cover table numcovers=%d offset=%llu size=%d -- disabling cover",
				g_PadsFile->numcovers,
				(unsigned long long)g_PadsFile->coversoffset,
				s_SetupPadFileDataSize);
		}
		g_StageSetup.cover = NULL;
	}

	if (g_StageSetup.cover != NULL) {
		setupPrepareCover();
	}
}
