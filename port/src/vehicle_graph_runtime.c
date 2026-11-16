/**
 * vehicle_graph_runtime.c -- one vehicle graph instance per vehicle: see
 * vehicle_graph_runtime.h.
 */
#include <math.h>
#include <string.h>

#include <PR/ultratypes.h>

#include "vehicle_graph_runtime.h"

typedef struct vehicle_graph_record {
	const void *prop;
	u64 generation;
	const vehicle_graph_program_t *program;
	vehicle_graph_instance_t *instance;
	s32 held;
	s32 refused;
	/* Speed over the ground (vehicleGraphRuntimeGroundSpeed). */
	s32 has_pos;
	f32 last_x;
	f32 last_z;
	f32 last_ticks60;
	f32 peak_ground;
} vehicle_graph_record_t;

static vehicle_graph_record_t s_Records[VEHICLE_GRAPH_RUNTIME_MAX];

static void vgrClear(vehicle_graph_record_t *record)
{
	vehicleGraphInstanceFree(record->instance);
	memset(record, 0, sizeof(*record));
}

void vehicleGraphRuntimeReset(void)
{
	for (s32 i = 0; i < VEHICLE_GRAPH_RUNTIME_MAX; i++) {
		vgrClear(&s_Records[i]);
	}
}

/* The record for this prop, made when make is set. A record left by an
 * earlier vehicle in the same prop slot (another generation) is cleared. */
static vehicle_graph_record_t *vgrRecord(const void *prop, u64 generation, s32 make)
{
	vehicle_graph_record_t *free_record = NULL;

	if (!prop) {
		return NULL;
	}
	for (s32 i = 0; i < VEHICLE_GRAPH_RUNTIME_MAX; i++) {
		vehicle_graph_record_t *record = &s_Records[i];
		if (record->prop == prop) {
			if (record->generation != generation) {
				vgrClear(record);
				if (!make) {
					return NULL;
				}
				record->prop = prop;
				record->generation = generation;
			}
			return record;
		}
		if (!record->prop && !free_record) {
			free_record = record;
		}
	}
	if (!make || !free_record) {
		return NULL;
	}
	free_record->prop = prop;
	free_record->generation = generation;
	return free_record;
}

vehicle_graph_instance_t *vehicleGraphRuntimeInstance(const void *prop, u64 generation,
	const vehicle_graph_program_t *program, s32 *created)
{
	vehicle_graph_record_t *record;

	if (created) {
		*created = 0;
	}
	if (!program) {
		return NULL;
	}
	record = vgrRecord(prop, generation, 1);
	if (!record || record->refused) {
		return NULL;
	}
	if (record->instance && record->program != program) {
		vehicleGraphInstanceFree(record->instance);
		record->instance = NULL;
	}
	if (!record->instance) {
		record->instance = vehicleGraphInstanceCreate(program);
		record->program = record->instance ? program : NULL;
		if (record->instance && created) {
			*created = 1;
		}
	}
	return record->instance;
}

void vehicleGraphRuntimeRefuse(const void *prop, u64 generation)
{
	vehicle_graph_record_t *record = vgrRecord(prop, generation, 1);

	if (record) {
		vehicleGraphInstanceFree(record->instance);
		record->instance = NULL;
		record->program = NULL;
		record->refused = 1;
	}
}

void vehicleGraphRuntimeSetBoostHeld(const void *prop, u64 generation, s32 held)
{
	/* A record is made only for a held boost, so idle vehicles take none. */
	vehicle_graph_record_t *record = vgrRecord(prop, generation, held ? 1 : 0);

	if (record) {
		record->held = held ? 1 : 0;
	}
}

s32 vehicleGraphRuntimeTakeBoostHeld(const void *prop, u64 generation)
{
	vehicle_graph_record_t *record = vgrRecord(prop, generation, 0);
	s32 held;

	if (!record) {
		return 0;
	}
	held = record->held;
	record->held = 0;
	return held;
}

f32 vehicleGraphRuntimeGroundSpeed(const void *prop, u64 generation, f32 x, f32 z, f32 ticks60)
{
	vehicle_graph_record_t *record = vgrRecord(prop, generation, 1);
	f32 ground = 0.0f;

	if (!record) {
		return 0.0f;
	}
	if (record->has_pos && record->last_ticks60 > 0.0f) {
		const f32 dx = x - record->last_x;
		const f32 dz = z - record->last_z;
		ground = sqrtf(dx * dx + dz * dz) / record->last_ticks60;
		if (ground > record->peak_ground) {
			record->peak_ground = ground;
		}
	}
	record->has_pos = 1;
	record->last_x = x;
	record->last_z = z;
	record->last_ticks60 = ticks60;
	return ground;
}

f32 vehicleGraphRuntimeTakePeakGround(const void *prop, u64 generation)
{
	vehicle_graph_record_t *record = vgrRecord(prop, generation, 0);
	f32 peak;

	if (!record) {
		return 0.0f;
	}
	peak = record->peak_ground;
	record->peak_ground = 0.0f;
	return peak;
}

s32 vehicleGraphRuntimeCount(void)
{
	s32 count = 0;

	for (s32 i = 0; i < VEHICLE_GRAPH_RUNTIME_MAX; i++) {
		if (s_Records[i].prop) {
			count++;
		}
	}
	return count;
}
