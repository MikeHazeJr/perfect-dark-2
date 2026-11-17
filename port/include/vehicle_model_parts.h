/**
 * vehicle_model_parts.h -- the toggle parts of a vehicle's typed mesh.
 *
 * og.vehicle.part_visible shows or hides a toggle part of the vehicle's model.
 * A typed mesh (.pdmesh) lists its parts in model.parts.json (pd2.mesh.parts.v1:
 * part number to node index) and its nodes in model.nodes.json
 * (pd2.mesh.nodes.v1: each node's type). Reading the two tables answers "is
 * this part a toggle?" from the archive alone, so a vehicle graph's parts are
 * checked when the catalog registers the vehicle, before any model is built
 * (CORE's rule: context/designs/modding/vehicle-graph-v1.md, answer 5).
 *
 * The tables are read the way the model compiler reads them
 * (port/src/modasset_compiler.c): a part's "node" is an index into the node
 * array, and a part marked node_unresolved with no node is not a part.
 */
#ifndef _IN_VEHICLE_MODEL_PARTS_H
#define _IN_VEHICLE_MODEL_PARTS_H

#include <PR/ultratypes.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vehicle_model_parts vehicle_model_parts_t;

/*
 * Reads a mesh's part table and node table. Returns NULL with the reason in
 * err when either is not valid JSON of its schema, a part names a node the
 * mesh does not have, or a part number is listed twice.
 */
vehicle_model_parts_t *vehicleModelPartsRead(const char *parts_json, size_t parts_size,
	const char *nodes_json, size_t nodes_size, char *err, size_t err_cap);

void vehicleModelPartsFree(vehicle_model_parts_t *parts);

/*
 * 1 when the mesh has that part and its node is a toggle node. context is the
 * vehicle_model_parts_t, so this is a vehicle_graph_has_part_fn
 * (vehicle_graph_runner.h). A NULL context has no parts.
 */
s32 vehicleModelPartsIsToggle(void *context, s32 part);

/* How many parts the mesh lists, and how many of them are toggles. */
s32 vehicleModelPartsCount(const vehicle_model_parts_t *parts);
s32 vehicleModelPartsToggleCount(const vehicle_model_parts_t *parts);

#ifdef __cplusplus
}
#endif

#endif
