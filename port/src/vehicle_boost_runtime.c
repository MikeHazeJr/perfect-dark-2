/**
 * vehicle_boost_runtime.c -- per-vehicle boost and heat state: see
 * vehicle_boost_runtime.h.
 */
#include <string.h>

#include <PR/ultratypes.h>

#include "vehicle_boost_runtime.h"

typedef struct vehicle_boost_slot {
	const void *vehicle;
	vehicle_boost_heat_state_t state;
	s32 held;
} vehicle_boost_slot_t;

static vehicle_boost_slot_t s_Slots[VEHICLE_BOOST_RUNTIME_SLOTS];

void vehicleBoostRuntimeReset(void)
{
	memset(s_Slots, 0, sizeof(s_Slots));
}

static vehicle_boost_slot_t *vehicleBoostSlot(const void *vehicle, s32 make, s32 *created)
{
	vehicle_boost_slot_t *free_slot = NULL;

	if (created) {
		*created = 0;
	}
	if (!vehicle) {
		return NULL;
	}
	for (s32 i = 0; i < VEHICLE_BOOST_RUNTIME_SLOTS; i++) {
		if (s_Slots[i].vehicle == vehicle) {
			return &s_Slots[i];
		}
		if (!s_Slots[i].vehicle && !free_slot) {
			free_slot = &s_Slots[i];
		}
	}
	if (!make || !free_slot) {
		return NULL;
	}
	memset(free_slot, 0, sizeof(*free_slot));
	free_slot->vehicle = vehicle;
	vehicleBoostHeatReset(&free_slot->state);
	if (created) {
		*created = 1;
	}
	return free_slot;
}

vehicle_boost_heat_state_t *vehicleBoostRuntimeState(const void *vehicle, s32 *created)
{
	vehicle_boost_slot_t *slot = vehicleBoostSlot(vehicle, 1, created);
	return slot ? &slot->state : NULL;
}

void vehicleBoostRuntimeSetHeld(const void *vehicle, s32 held)
{
	vehicle_boost_slot_t *slot = vehicleBoostSlot(vehicle, held != 0, NULL);

	if (slot) {
		slot->held = held != 0;
	}
}

s32 vehicleBoostRuntimeTakeHeld(const void *vehicle)
{
	vehicle_boost_slot_t *slot = vehicleBoostSlot(vehicle, 0, NULL);
	s32 held;

	if (!slot) {
		return 0;
	}
	held = slot->held;
	slot->held = 0;
	return held;
}
