/**
 * vehicle_debug.c -- `--debug-swap-vehicle`: see vehicle_debug.h.
 */
#include <stdio.h>
#include <string.h>

#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "assetcatalog_load.h"
#include "system.h"
#include "vehicle_debug.h"

static char s_SwapVehicleId[CATALOG_ID_LEN];

void vehicleDebugSetSwap(const char *catalog_id)
{
	s_SwapVehicleId[0] = '\0';
	if (catalog_id && catalog_id[0]) {
		snprintf(s_SwapVehicleId, sizeof(s_SwapVehicleId), "%s", catalog_id);
	}
}

const char *vehicleDebugSwapId(void)
{
	return s_SwapVehicleId;
}

s32 vehicleDebugSwapModelnum(s32 modelnum)
{
	const asset_entry_t *vehicle;
	s32 swapped;

	if (!s_SwapVehicleId[0]) {
		return modelnum;
	}

	vehicle = assetCatalogResolve(s_SwapVehicleId);
	if (!vehicle || vehicle->type != ASSET_VEHICLE || vehicle->runtime_index < 0) {
		sysLogPrintf(LOG_WARNING,
			"VEHICLE.SWAP: '%s' is not a placeable vehicle (runtime index %d); "
			"the hoverbike keeps modelnum %d",
			s_SwapVehicleId, vehicle ? vehicle->runtime_index : -1, modelnum);
		return modelnum;
	}

	/* Copy before loading: the row pointer is not kept across catalog calls. */
	swapped = vehicle->runtime_index;
	if (!catalogLoadStageAsset(ASSET_VEHICLE, s_SwapVehicleId)) {
		sysLogPrintf(LOG_WARNING,
			"VEHICLE.SWAP: '%s' did not load for this stage; the hoverbike keeps modelnum %d",
			s_SwapVehicleId, modelnum);
		return modelnum;
	}

	sysLogPrintf(LOG_NOTE, "VEHICLE.SWAP: hoverbike modelnum %d -> %d (%s)",
		modelnum, swapped, s_SwapVehicleId);
	return swapped;
}
