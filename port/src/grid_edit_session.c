#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "grid_edit_session.h"
#include "scenario_stage_rules.h"

typedef struct grid_edit_member {
	u64 id;
	grid_edit_role_t role;
	u64 last_sequence;
	grid_edit_command_t last_command;
	u64 last_revision;
} grid_edit_member_t;

typedef struct grid_edit_lease {
	u64 owner_id;
	u64 expires_at_ms;
	grid_edit_resource_t resource;
} grid_edit_lease_t;

struct grid_edit_session {
	forge_level_doc_t *document;
	u64 host_id;
	u64 revision;
	u64 snapshot_revision;
	grid_edit_member_t members[GRID_EDIT_MAX_MEMBERS];
	grid_edit_delta_t deltas[GRID_EDIT_DELTA_CAPACITY];
	grid_edit_lease_t leases[GRID_EDIT_MAX_LEASES];
	u64 now_ms;
	s32 clock_ready;
	s32 players_see_creators;
	s32 creators_see_match;
};

static s32 findMember(const grid_edit_session_t *session, u64 member_id)
{
	for (s32 i = 0; i < GRID_EDIT_MAX_MEMBERS; i++) {
		if (session->members[i].id == member_id) return i;
	}
	return -1;
}

static void releaseMemberLeases(grid_edit_session_t *session, u64 member_id)
{
	for (size_t i = 0; i < GRID_EDIT_MAX_LEASES; i++) {
		if (session->leases[i].owner_id == member_id) session->leases[i].owner_id = 0;
	}
}

grid_edit_session_t *gridEditSessionCreate(u64 host_id, const forge_level_doc_t *opened)
{
	return gridEditSessionCreateAtRevision(host_id, opened, 1, 0);
}

grid_edit_session_t *gridEditSessionCreateAtRevision(u64 host_id, const forge_level_doc_t *opened,
	u64 revision, u64 host_sequence)
{
	if (!host_id || !opened || !revision || host_sequence >= revision) return NULL;

	grid_edit_session_t *session = calloc(1, sizeof(*session));
	if (!session) return NULL;

	session->document = forgeLevelDocClone(opened);
	if (!session->document) {
		free(session);
		return NULL;
	}
	session->host_id = host_id;
	session->revision = revision;
	session->snapshot_revision = revision;
	session->members[0].id = host_id;
	session->members[0].role = GRID_EDIT_ROLE_CREATOR;
	session->members[0].last_sequence = host_sequence;
	return session;
}

void gridEditSessionFree(grid_edit_session_t *session)
{
	if (!session) return;
	forgeLevelDocFree(session->document);
	free(session);
}

s32 gridEditSessionSetMember(grid_edit_session_t *session, u64 actor_id, u64 member_id,
	grid_edit_role_t role)
{
	if (!session || actor_id != session->host_id || !member_id
			|| (role != GRID_EDIT_ROLE_CREATOR && role != GRID_EDIT_ROLE_PLAYER)) {
		return 0;
	}
	s32 index = findMember(session, member_id);
	if (index < 0) index = findMember(session, 0);
	if (index < 0) return 0;

	session->members[index].id = member_id;
	session->members[index].role = role;
	if (role == GRID_EDIT_ROLE_PLAYER) releaseMemberLeases(session, member_id);
	return 1;
}

s32 gridEditSessionRemoveMember(grid_edit_session_t *session, u64 actor_id, u64 member_id)
{
	if (!session || actor_id != session->host_id || !member_id || member_id == session->host_id) {
		return 0;
	}
	s32 index = findMember(session, member_id);
	if (index < 0) return 0;
	session->members[index] = (grid_edit_member_t){0};
	releaseMemberLeases(session, member_id);
	return 1;
}

s32 gridEditSessionSetVisibility(grid_edit_session_t *session, u64 actor_id,
	s32 players_see_creators, s32 creators_see_match)
{
	if (!session || actor_id != session->host_id) return 0;
	session->players_see_creators = players_see_creators != 0;
	session->creators_see_match = creators_see_match != 0;
	return 1;
}

s32 gridEditSessionGetVisibility(const grid_edit_session_t *session, u64 actor_id,
	s32 *players_see_creators, s32 *creators_see_match)
{
	if (!session || actor_id != session->host_id || !players_see_creators || !creators_see_match) return 0;
	*players_see_creators = session->players_see_creators;
	*creators_see_match = session->creators_see_match;
	return 1;
}

grid_edit_capabilities_t gridEditSessionCapabilities(const grid_edit_session_t *session,
	u64 viewer_id)
{
	grid_edit_capabilities_t caps = {0};
	if (!session || !viewer_id) return caps;

	s32 index = findMember(session, viewer_id);
	if (index < 0) return caps;

	caps.role = session->members[index].role;
	caps.can_edit = caps.role == GRID_EDIT_ROLE_CREATOR;
	caps.see_creators = caps.can_edit || session->players_see_creators;
	caps.see_match = caps.role == GRID_EDIT_ROLE_PLAYER || session->creators_see_match;
	return caps;
}

s32 gridEditSessionAdvanceClock(grid_edit_session_t *session, u64 actor_id, u64 now_ms)
{
	if (!session || actor_id != session->host_id || now_ms < session->now_ms) return 0;

	session->now_ms = now_ms;
	session->clock_ready = 1;
	for (size_t i = 0; i < GRID_EDIT_MAX_LEASES; i++) {
		grid_edit_lease_t *lease = &session->leases[i];
		if (lease->owner_id && now_ms >= lease->expires_at_ms) lease->owner_id = 0;
	}
	return 1;
}

static s32 validSpawnId(const char spawn_id[GRID_EDIT_SPAWN_ID_CAP])
{
	return spawn_id[0] && memchr(spawn_id, '\0', GRID_EDIT_SPAWN_ID_CAP);
}

static s32 validIndexedId(const char *id, const char *prefix)
{
	if (memcmp(id, prefix, 6) != 0 || id[10] != '\0') return 0;
	for (size_t i = 6; i < 10; i++) {
		if (id[i] < '0' || id[i] > '9') return 0;
	}
	return 1;
}

s32 gridEditMoveSpawnValid(const grid_edit_move_spawn_t *move)
{
	if (!move || !validIndexedId(move->spawn_id, "spawn_")) return 0;

	double length_squared = 0;
	for (size_t i = 0; i < 3; i++) {
		if (!__builtin_isfinite(move->position[i]) || !__builtin_isfinite(move->look[i])) return 0;
		length_squared += (double)move->look[i] * move->look[i];
	}
	return __builtin_sqrt(length_squared) >= 1e-6;
}

s32 gridEditMoveDecorationValid(const grid_edit_move_decoration_t *move)
{
	return move && validIndexedId(move->record_id, "setup_")
		&& __builtin_isfinite(move->x) && __builtin_isfinite(move->z);
}

s32 gridEditResourceValid(const grid_edit_resource_t *resource)
{
	if (!resource) return 0;
	switch (resource->kind) {
	case GRID_EDIT_RESOURCE_SPAWN: return validIndexedId(resource->spawn_id, "spawn_");
	case GRID_EDIT_RESOURCE_AMBIENT_LIGHT: return 1;
	case GRID_EDIT_RESOURCE_DECORATION: return validIndexedId(resource->record_id, "setup_");
	}
	return 0;
}

static s32 findSpawn(const forge_level_doc_t *document, const char *spawn_id, forge_level_spawn_t *out)
{
	s32 count = forgeLevelDocSpawnCount(document);
	for (s32 i = 0; i < count; i++) {
		if (forgeLevelDocGetSpawn(document, i, out) && strcmp(out->spawn_id, spawn_id) == 0) return 1;
	}
	return 0;
}

static s32 readSpawnPose(const forge_level_doc_t *document, const forge_level_spawn_t *spawn,
	grid_edit_move_spawn_t *out)
{
	forge_level_pad_t pad;
	if (!forgeLevelDocFindPad(document, spawn->pad_ref, &pad)) return 0;
	grid_edit_move_spawn_t pose = {0};
	strcpy(pose.spawn_id, spawn->spawn_id);
	memcpy(pose.position, pad.position, sizeof(pose.position));
	memcpy(pose.look, pad.look, sizeof(pose.look));
	*out = pose;
	return 1;
}

s32 gridEditSessionSpawnCount(const grid_edit_session_t *session, u64 viewer_id)
{
	return gridEditSessionCapabilities(session, viewer_id).can_edit ? forgeLevelDocSpawnCount(session->document) : 0;
}

s32 gridEditSessionGetSpawnPose(const grid_edit_session_t *session, u64 viewer_id,
	s32 index, grid_edit_move_spawn_t *out)
{
	forge_level_spawn_t spawn;
	return out && gridEditSessionCapabilities(session, viewer_id).can_edit
		&& forgeLevelDocGetSpawn(session->document, index, &spawn)
		&& readSpawnPose(session->document, &spawn, out);
}

s32 gridEditSessionFindSpawnPose(const grid_edit_session_t *session, u64 viewer_id,
	const char *spawn_id, grid_edit_move_spawn_t *out)
{
	forge_level_spawn_t spawn;
	return spawn_id && out && gridEditSessionCapabilities(session, viewer_id).can_edit
		&& findSpawn(session->document, spawn_id, &spawn)
		&& readSpawnPose(session->document, &spawn, out);
}

static s32 readDecorationPose(const forge_level_doc_t *doc, const char *record_id,
	grid_edit_move_decoration_t *out)
{
	forge_level_pad_t pad;
	if (!forgeLevelDocGetDecorationPose(doc, record_id, &pad)) return 0;
	grid_edit_move_decoration_t pose = { .x = pad.position[0], .z = pad.position[2] };
	snprintf(pose.record_id, sizeof(pose.record_id), "%s", record_id);
	*out = pose;
	return 1;
}

s32 gridEditSessionDecorationCount(const grid_edit_session_t *session, u64 viewer_id)
{
	if (!gridEditSessionCapabilities(session, viewer_id).can_edit) return 0;
	const s32 count = forgeLevelDocObjectCount(session->document);
	s32 decorations = 0;
	for (s32 i = 0; i < count; i++) {
		forge_level_object_t object;
		if (forgeLevelDocGetObject(session->document, i, &object)
				&& forgeLevelDocIsDecoration(session->document, object.record_id)) decorations++;
	}
	return decorations;
}

s32 gridEditSessionGetDecorationPose(const grid_edit_session_t *session, u64 viewer_id,
	s32 index, grid_edit_move_decoration_t *out)
{
	if (index < 0 || !out || !gridEditSessionCapabilities(session, viewer_id).can_edit) return 0;
	const s32 count = forgeLevelDocObjectCount(session->document);
	for (s32 i = 0; i < count; i++) {
		forge_level_object_t object;
		if (forgeLevelDocGetObject(session->document, i, &object)
				&& forgeLevelDocIsDecoration(session->document, object.record_id) && index-- == 0) {
			return readDecorationPose(session->document, object.record_id, out);
		}
	}
	return 0;
}

s32 gridEditSessionFindDecorationPose(const grid_edit_session_t *session, u64 viewer_id,
	const char *record_id, grid_edit_move_decoration_t *out)
{
	return record_id && out && gridEditSessionCapabilities(session, viewer_id).can_edit
		&& readDecorationPose(session->document, record_id, out);
}

static const char *resourceId(const grid_edit_resource_t *resource)
{
	switch (resource->kind) {
	case GRID_EDIT_RESOURCE_SPAWN: return resource->spawn_id;
	case GRID_EDIT_RESOURCE_DECORATION: return resource->record_id;
	case GRID_EDIT_RESOURCE_AMBIENT_LIGHT: return NULL;
	}
	return NULL;
}

static s32 validResourceKey(const grid_edit_resource_t *resource)
{
	if (resource && resource->kind == GRID_EDIT_RESOURCE_SPAWN) return validSpawnId(resource->spawn_id);
	return gridEditResourceValid(resource);
}

static grid_edit_lease_t *findLease(grid_edit_session_t *session, grid_edit_resource_kind_t kind,
	const char *id)
{
	for (size_t i = 0; i < GRID_EDIT_MAX_LEASES; i++) {
		grid_edit_lease_t *lease = &session->leases[i];
		if (lease->owner_id && lease->resource.kind == kind
				&& (!id || strcmp(resourceId(&lease->resource), id) == 0)) {
			return lease;
		}
	}
	return NULL;
}

grid_edit_lease_result_t gridEditSessionAcquireLease(grid_edit_session_t *session, u64 author_id,
	const grid_edit_resource_t *resource)
{
	if (!gridEditSessionCapabilities(session, author_id).can_edit) return GRID_EDIT_LEASE_DENIED;
	forge_level_spawn_t spawn;
	if (!validResourceKey(resource)
			|| (resource->kind == GRID_EDIT_RESOURCE_SPAWN && !findSpawn(session->document, resource->spawn_id, &spawn))
			|| (resource->kind == GRID_EDIT_RESOURCE_DECORATION
				&& !forgeLevelDocIsDecoration(session->document, resource->record_id))) {
		return GRID_EDIT_LEASE_INVALID_RESOURCE;
	}
	if (!session->clock_ready || session->now_ms > UINT64_MAX - GRID_EDIT_LEASE_MS) {
		return GRID_EDIT_LEASE_CLOCK_UNAVAILABLE;
	}

	grid_edit_lease_t *lease = findLease(session, resource->kind, resourceId(resource));
	if (lease && lease->owner_id != author_id) return GRID_EDIT_LEASE_HELD;

	if (!lease) {
		for (size_t i = 0; i < GRID_EDIT_MAX_LEASES; i++) {
			if (!session->leases[i].owner_id) {
				lease = &session->leases[i];
				break;
			}
		}
		if (!lease) return GRID_EDIT_LEASE_FULL;

		lease->resource = (grid_edit_resource_t){ .kind = resource->kind };
		if (resource->kind == GRID_EDIT_RESOURCE_SPAWN) {
			strcpy(lease->resource.spawn_id, resource->spawn_id);
		} else if (resource->kind == GRID_EDIT_RESOURCE_DECORATION) {
			strcpy(lease->resource.record_id, resource->record_id);
		}
		lease->owner_id = author_id;
	}

	lease->expires_at_ms = session->now_ms + GRID_EDIT_LEASE_MS;
	return GRID_EDIT_LEASE_GRANTED;
}

s32 gridEditSessionReleaseLease(grid_edit_session_t *session, u64 author_id,
	const grid_edit_resource_t *resource)
{
	if (!gridEditSessionCapabilities(session, author_id).can_edit || !validResourceKey(resource)) return 0;

	grid_edit_lease_t *lease = findLease(session, resource->kind, resourceId(resource));
	if (!lease || lease->owner_id != author_id) return 0;

	lease->owner_id = 0;
	return 1;
}

grid_edit_lease_result_t gridEditSessionFocusResource(grid_edit_session_t *session, u64 author_id,
	const grid_edit_resource_t *resource_or_null)
{
	if (!gridEditSessionCapabilities(session, author_id).can_edit) return GRID_EDIT_LEASE_DENIED;
	if (!resource_or_null) {
		releaseMemberLeases(session, author_id);
		return GRID_EDIT_LEASE_GRANTED;
	}
	if (!gridEditResourceValid(resource_or_null)) return GRID_EDIT_LEASE_INVALID_RESOURCE;
	const grid_edit_lease_t *keep = findLease(session, resource_or_null->kind, resourceId(resource_or_null));
	for (size_t i = 0; i < GRID_EDIT_MAX_LEASES; i++) {
		grid_edit_lease_t *lease = &session->leases[i];
		if (lease->owner_id == author_id && lease != keep) lease->owner_id = 0;
	}
	return gridEditSessionAcquireLease(session, author_id, resource_or_null);
}

static grid_edit_result_t editError(grid_edit_result_t result, char *err, size_t cap,
	const char *reason)
{
	if (err && cap) snprintf(err, cap, "%s", reason);
	return result;
}

static s32 hasValidPayload(const grid_edit_command_t *command)
{
	switch (command->operation) {
	case GRID_EDIT_MOVE_SPAWN:
		return validSpawnId(command->data.move_spawn.spawn_id);
	case GRID_EDIT_SET_AMBIENT_LIGHT:
		return command->data.ambient_light.enabled == 0 || command->data.ambient_light.enabled == 1;
	case GRID_EDIT_MOVE_DECORATION:
		return gridEditMoveDecorationValid(&command->data.move_decoration);
	default:
		return 0;
	}
}

s32 gridEditCommandMatches(const grid_edit_command_t *a, const grid_edit_command_t *b)
{
	if (!a || !b || a->operation != b->operation || a->sequence != b->sequence
			|| a->expected_revision != b->expected_revision) return 0;
	switch (a->operation) {
	case GRID_EDIT_MOVE_SPAWN:
		return strncmp(a->data.move_spawn.spawn_id, b->data.move_spawn.spawn_id, GRID_EDIT_SPAWN_ID_CAP) == 0
			&& memcmp(a->data.move_spawn.position, b->data.move_spawn.position,
				sizeof(a->data.move_spawn.position)) == 0
			&& memcmp(a->data.move_spawn.look, b->data.move_spawn.look,
				sizeof(a->data.move_spawn.look)) == 0;
	case GRID_EDIT_SET_AMBIENT_LIGHT:
		return a->data.ambient_light.enabled == b->data.ambient_light.enabled
			&& a->data.ambient_light.brightness == b->data.ambient_light.brightness;
	case GRID_EDIT_MOVE_DECORATION:
		return strncmp(a->data.move_decoration.record_id, b->data.move_decoration.record_id, GRID_EDIT_SPAWN_ID_CAP) == 0
			&& memcmp(&a->data.move_decoration.x, &b->data.move_decoration.x, sizeof(f32)) == 0
			&& memcmp(&a->data.move_decoration.z, &b->data.move_decoration.z, sizeof(f32)) == 0;
	default:
		return 0;
	}
}

static const grid_edit_lease_t *leaseForCommand(grid_edit_session_t *session, const grid_edit_command_t *command)
{
	switch (command->operation) {
	case GRID_EDIT_MOVE_SPAWN:
		return findLease(session, GRID_EDIT_RESOURCE_SPAWN, command->data.move_spawn.spawn_id);
	case GRID_EDIT_SET_AMBIENT_LIGHT:
		return findLease(session, GRID_EDIT_RESOURCE_AMBIENT_LIGHT, NULL);
	case GRID_EDIT_MOVE_DECORATION:
		return findLease(session, GRID_EDIT_RESOURCE_DECORATION, command->data.move_decoration.record_id);
	default:
		return NULL;
	}
}

static size_t deltaIndex(u64 revision)
{
	/* Revision 1 is the opened document. Its first delta commits revision 2. */
	return (size_t)((revision - 2) % GRID_EDIT_DELTA_CAPACITY);
}

static void recordDelta(grid_edit_session_t *session, u64 author_id, const grid_edit_command_t *command)
{
	grid_edit_delta_t *delta = &session->deltas[deltaIndex(session->revision)];
	delta->revision = session->revision;
	delta->author_id = author_id;

	/* Preserve active typed fields and the exact ID through its terminator. */
	grid_edit_command_t *stored = &delta->command;
	memset(stored, 0, sizeof(*stored));
	stored->sequence = command->sequence;
	stored->expected_revision = command->expected_revision;
	stored->operation = command->operation;
	switch (command->operation) {
	case GRID_EDIT_MOVE_SPAWN:
		strcpy(stored->data.move_spawn.spawn_id, command->data.move_spawn.spawn_id);
		memcpy(stored->data.move_spawn.position, command->data.move_spawn.position,
			sizeof(stored->data.move_spawn.position));
		memcpy(stored->data.move_spawn.look, command->data.move_spawn.look,
			sizeof(stored->data.move_spawn.look));
		break;
	case GRID_EDIT_SET_AMBIENT_LIGHT:
		stored->data.ambient_light = command->data.ambient_light;
		break;
	case GRID_EDIT_MOVE_DECORATION:
		strcpy(stored->data.move_decoration.record_id, command->data.move_decoration.record_id);
		stored->data.move_decoration.x = command->data.move_decoration.x;
		stored->data.move_decoration.z = command->data.move_decoration.z;
		break;
	}
}

static s32 applyOperation(forge_level_doc_t *candidate, const grid_edit_command_t *command,
	char *err, size_t err_cap)
{
	switch (command->operation) {
	case GRID_EDIT_MOVE_SPAWN: {
		const grid_edit_move_spawn_t *move = &command->data.move_spawn;
		return forgeLevelDocMoveSpawn(candidate, move->spawn_id, move->position, move->look, err, err_cap);
	}
	case GRID_EDIT_SET_AMBIENT_LIGHT: {
		scenario_stage_atmosphere_t atmosphere;
		if (!forgeLevelDocGetAtmosphere(candidate, &atmosphere)) return 0;
		atmosphere.has_ambient = command->data.ambient_light.enabled;
		atmosphere.ambient_light = command->data.ambient_light.brightness;
		return forgeLevelDocSetAtmosphere(candidate, &atmosphere, err, err_cap);
	}
	case GRID_EDIT_MOVE_DECORATION: {
		const grid_edit_move_decoration_t *move = &command->data.move_decoration;
		forge_level_pad_t pad;
		if (!forgeLevelDocGetDecorationPose(candidate, move->record_id, &pad)) {
			if (err && err_cap) snprintf(err, err_cap, "select an independent decoration to move");
			return 0;
		}
		const f32 position[3] = {move->x, pad.position[1], move->z};
		return forgeLevelDocMoveDecoration(candidate, move->record_id, position, pad.look, err, err_cap);
	}
	default:
		return 0;
	}
}

grid_edit_result_t gridEditSessionApply(grid_edit_session_t *session, u64 author_id,
	const grid_edit_command_t *command, grid_edit_receipt_t *receipt, char *err, size_t err_cap)
{
	if (receipt) receipt->revision = 0;
	if (err && err_cap) err[0] = '\0';

	s32 index = session && author_id ? findMember(session, author_id) : -1;
	if (index < 0 || session->members[index].role != GRID_EDIT_ROLE_CREATOR) {
		return editError(GRID_EDIT_DENIED, err, err_cap, "only creators can edit this draft");
	}
	if (!command || !command->sequence || !command->expected_revision || !hasValidPayload(command)) {
		return editError(GRID_EDIT_INVALID, err, err_cap, "invalid Grid edit command");
	}

	grid_edit_member_t *member = &session->members[index];
	u64 last_sequence = member->last_sequence;
	if (command->sequence == last_sequence) {
		if (!member->last_revision) {
			return editError(GRID_EDIT_OUT_OF_ORDER, err, err_cap, "edit is already included in the Grid snapshot");
		}
		if (!gridEditCommandMatches(command, &member->last_command)) {
			return editError(GRID_EDIT_SEQUENCE_REUSED, err, err_cap,
				"command sequence already used by a different edit");
		}
		if (receipt) receipt->revision = member->last_revision;
		return GRID_EDIT_DUPLICATE;
	}
	if (command->sequence < last_sequence || command->sequence - last_sequence != 1) {
		return editError(GRID_EDIT_OUT_OF_ORDER, err, err_cap, "Grid edit command is out of order");
	}
	if (command->expected_revision != session->revision) {
		if (receipt) receipt->revision = session->revision;
		return editError(GRID_EDIT_STALE, err, err_cap, "Grid draft revision has changed");
	}
	const grid_edit_lease_t *lease = leaseForCommand(session, command);
	if (lease && lease->owner_id != author_id) {
		return editError(GRID_EDIT_RESOURCE_LEASED, err, err_cap, "another creator holds the resource lease");
	}
	if (session->revision == UINT64_MAX) {
		return editError(GRID_EDIT_RESOURCE_EXHAUSTED, err, err_cap, "Grid draft revision limit reached");
	}

	forge_level_doc_t *candidate = forgeLevelDocClone(session->document);
	if (!candidate) {
		return editError(GRID_EDIT_RESOURCE_EXHAUSTED, err, err_cap, "could not copy the Grid draft");
	}
	if (!applyOperation(candidate, command, err, err_cap)) {
		forgeLevelDocFree(candidate);
		return GRID_EDIT_INVALID;
	}

	forgeLevelDocFree(session->document);
	session->document = candidate;
	member->last_sequence = command->sequence;
	member->last_command = *command;
	member->last_revision = ++session->revision;
	recordDelta(session, author_id, command);
	if (receipt) receipt->revision = session->revision;
	return GRID_EDIT_APPLIED;
}

grid_edit_tail_result_t gridEditSessionReadDeltas(const grid_edit_session_t *session, u64 viewer_id,
	u64 after_revision, grid_edit_delta_t *out, size_t capacity, size_t *count, u64 *latest_revision)
{
	if (count) *count = 0;
	if (latest_revision) *latest_revision = 0;
	if (!gridEditSessionCapabilities(session, viewer_id).can_edit) return GRID_EDIT_TAIL_DENIED;

	u64 revision = session->revision;
	if (latest_revision) *latest_revision = revision;
	if (after_revision > revision) return GRID_EDIT_TAIL_INVALID_REVISION;
	if (after_revision < session->snapshot_revision || revision - after_revision > GRID_EDIT_DELTA_CAPACITY) {
		return GRID_EDIT_TAIL_SNAPSHOT_REQUIRED;
	}

	size_t available = (size_t)(revision - after_revision);
	if (count) *count = available;
	if (available > capacity || (available && !out)) return GRID_EDIT_TAIL_BUFFER_TOO_SMALL;

	for (size_t i = 0; i < available; i++) {
		out[i] = session->deltas[deltaIndex(after_revision + 1 + i)];
	}
	return GRID_EDIT_TAIL_READY;
}

forge_level_doc_t *gridEditSessionCloneDocument(const grid_edit_session_t *session,
	u64 viewer_id, u64 *revision)
{
	if (revision) *revision = 0;
	if (!gridEditSessionCapabilities(session, viewer_id).can_edit) return NULL;

	forge_level_doc_t *copy = forgeLevelDocClone(session->document);
	if (copy && revision) *revision = session->revision;
	return copy;
}

forge_level_doc_t *gridEditSessionSwapDocument(grid_edit_session_t *session, forge_level_doc_t *document)
{
	forge_level_doc_t *previous = session->document;
	session->document = document;
	return previous;
}

void gridEditSessionAdoptSnapshotDocument(grid_edit_session_t *session, forge_level_doc_t *document)
{
	forgeLevelDocFree(gridEditSessionSwapDocument(session, document));
}
