#include <ultra64.h>
#include "constants.h"
#include "memsizes.h"
#include "game/prop.h"
#include "bss.h"
#include "lib/memp.h"
#include "lib/vars.h"
#include "data.h"
#include "types.h"

#include <string.h>

void varsResetRoomProps(void);

void varsReset(void)
{
	s32 i;

	g_Vars.props = mempAlloc(ALIGN64(g_Vars.maxprops * sizeof(struct prop)), MEMPOOL_STAGE);
	g_Vars.onscreenprops = mempAlloc(ALIGN64(MAX_ONSCREEN_PROPS * sizeof(void *)), MEMPOOL_STAGE);

	/* Free slots must have a deterministic zero identity and inert payload.
	 * propAllocate stamps the exact process-lifetime generation before any slot
	 * can be published. This also prevents scanners from interpreting stale
	 * stage-pool bytes as live props. */
	if (g_Vars.props && g_Vars.maxprops > 0) {
		memset(g_Vars.props, 0,
			(size_t)g_Vars.maxprops * sizeof(struct prop));
	}

	g_AutoAimScale = 1;

	g_Vars.activeprops = g_Vars.activepropstail = NULL;
	g_Vars.pausedprops = NULL;

	g_Vars.numonscreenprops = 0;
	g_Vars.onscreenprops[0] = NULL;
	g_Vars.endonscreenprops = g_Vars.onscreenprops;

	g_Vars.freeprops = g_Vars.props;

	// @bug: The tail of the freeprops list will have an uninitialised next pointer.
	// This will likely crash the game if too many props get allocated,
	// but there is no known way to exhaust the free props list.
	for (i = 0; i < g_Vars.maxprops - 1; i++) {
		g_Vars.props[i].next = &g_Vars.props[i + 1];
		g_Vars.props[i].colmesh = NULL;
	}
	if (g_Vars.maxprops > 0) {
		g_Vars.props[g_Vars.maxprops - 1].next = NULL;
		g_Vars.props[g_Vars.maxprops - 1].colmesh = NULL;
	}

	varsResetRoomProps();

	if (g_Vars.normmplayerisrunning) {
		g_Vars.numpropstates = 4;
	} else {
		g_Vars.numpropstates = 7;
	}

	g_Vars.allocstateindex = 0;
	g_Vars.runstateindex = 0;
	g_Vars.alwaystick = 0;
	g_Vars.updateframe = 0xfffe;
	g_Vars.prevupdateframe = 0xffff;

	for (i = 0; i < ARRAYCOUNT(g_Vars.propstates); i++) {
		g_Vars.propstates[i].propcount = 0;
		g_Vars.propstates[i].chrpropcount = 0;
		g_Vars.propstates[i].updatetime = 0;
		g_Vars.propstates[i].chrupdatetime = 0;
		g_Vars.propstates[i].slotupdate240 = 0;
		g_Vars.propstates[i].slotupdate60error = 2;
	}
}

void varsResetRoomProps(void)
{
	s32 i;
	s32 j;

	g_RoomPropListChunkIndexes = mempAlloc(ALIGN16(g_Vars.roomcount * sizeof(s16)), MEMPOOL_STAGE);
	g_RoomPropListChunks = mempAlloc(MAX_ROOMPROPLISTCHUNKS * sizeof(struct roomproplistchunk), MEMPOOL_STAGE);

	for (i = 0; i < g_Vars.roomcount; i++) {
		g_RoomPropListChunkIndexes[i] = -1;
	}

	for (i = 0; i < MAX_ROOMPROPLISTCHUNKS; i++) {
		g_RoomPropListChunks[i].propnums[0] = -2;

		for (j = 1; j < ARRAYCOUNT(g_RoomPropListChunks[i].propnums); j++) {
			g_RoomPropListChunks[i].propnums[j] = -1;
		}
	}
}
