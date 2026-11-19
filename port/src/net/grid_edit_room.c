#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asset_archive_policy.h"
#include "asset_path_contract.h"
#include "assetcatalog_scanner.h"
#include "mod_asset_validate.h"
#include "net/grid_edit_room.h"
#include "net/grid_edit_snapshot.h"
#include "scenario_stage_rules.h"

#define GRID_EDIT_BASELINE_REVISION 1u
/* Match the existing local pin store's archive limit. */
#define GRID_EDIT_ARENA_MAX_BYTES (128u * 1024u * 1024u)

struct grid_edit_room {
	grid_edit_session_t *session;
	grid_edit_room_state_t state;
	u64 peer_connect_generation;
	u64 latest_generation;
	u64 latest_peer_actor;
	grid_edit_wire_identity_t prior_identity;
	grid_edit_snapshot_offer_t resume_offer;
	forge_level_doc_t *resume_document;
	s32 resume_confirmed;
	grid_edit_command_t pending;
};

static s32 fail(char *err, size_t cap, const char *reason)
{
	if (err && cap) snprintf(err, cap, "%s", reason);
	return 0;
}

static s32 identityValid(const grid_edit_wire_identity_t *identity)
{
	return identity && identity->room_id != 0xff && identity->generation;
}

static s32 identityMatches(const grid_edit_wire_identity_t *a, const grid_edit_wire_identity_t *b)
{
	return b && a->room_id == b->room_id && a->generation == b->generation;
}

static s32 digestValid(const char *digest)
{
	if (!digest) return 0;
	for (size_t i = 0; i < SHA256_HEX_SIZE - 1; i++) {
		char c = digest[i];
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return 0;
	}
	return digest[SHA256_HEX_SIZE - 1] == '\0';
}

static s32 arenaIdValid(const char *id)
{
	if (!id) return 0;
	size_t separator = 0;
	for (size_t i = 0; i < CATALOG_ID_LEN; i++) {
		char c = id[i];
		if (!c) return separator && i > separator + 1;
		if (c == ':') {
			if (!i || separator) return 0;
			separator = i;
		} else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')
				|| c == '_' || c == '-' || c == '.')) {
			return 0;
		}
	}
	return 0;
}

static s32 inviteValid(const grid_edit_room_invite_t *invite)
{
	return invite && identityValid(&invite->identity) && invite->host_actor
		&& invite->baseline_revision == GRID_EDIT_BASELINE_REVISION
		&& arenaIdValid(invite->arena_id) && digestValid(invite->arena_sha256);
}

static s32 inviteMatches(const grid_edit_room_invite_t *expected, const grid_edit_room_invite_t *provided)
{
	return inviteValid(provided) && identityMatches(&expected->identity, &provided->identity)
		&& expected->host_actor == provided->host_actor
		&& expected->baseline_revision == provided->baseline_revision
		&& strcmp(expected->arena_id, provided->arena_id) == 0
		&& strcmp(expected->arena_sha256, provided->arena_sha256) == 0;
}

static u8 hexDigit(char c)
{
	return (u8)(c <= '9' ? c - '0' : c - 'a' + 10);
}

static grid_edit_snapshot_identity_t snapshotIdentity(const grid_edit_room_invite_t *invite, u64 revision)
{
	grid_edit_snapshot_identity_t identity = { .room = invite->identity, .revision = revision };
	memcpy(identity.arena_id, invite->arena_id, sizeof(identity.arena_id));
	/* The base digest characters were validated when the room opened. */
	for (size_t i = 0; i < SHA256_DIGEST_SIZE; i++) {
		identity.arena_sha256[i] = (u8)(hexDigit(invite->arena_sha256[2 * i]) << 4
			| hexDigit(invite->arena_sha256[2 * i + 1]));
	}
	return identity;
}

static void clearResumeState(grid_edit_room_t *room)
{
	forgeLevelDocFree(room->resume_document);
	room->resume_document = NULL;
	room->state.pending_resume = 0;
	room->state.snapshot_ready = 0;
	room->resume_confirmed = 0;
	room->prior_identity = (grid_edit_wire_identity_t){0};
	room->resume_offer = (grid_edit_snapshot_offer_t){0};
}

static s32 roomCanEdit(const grid_edit_room_t *room)
{
	return room && (room->state.ready
		|| (room->state.is_host && room->state.creator_detached && room->state.pending_invite
			&& room->state.pending_resume && room->state.snapshot_ready && room->resume_confirmed
			&& room->state.peer_actor && room->peer_connect_generation));
}

static void *readArena(const char *path, u32 *size)
{
	FILE *stream = fopen(path, "rb");
	if (!stream) return NULL;

	void *bytes = NULL;
	if (fseek(stream, 0, SEEK_END) == 0) {
		long length = ftell(stream);
		if (length > 0 && (unsigned long)length <= GRID_EDIT_ARENA_MAX_BYTES
				&& fseek(stream, 0, SEEK_SET) == 0) {
			bytes = malloc((size_t)length);
			if (bytes && fread(bytes, 1, (size_t)length, stream) == (size_t)length
					&& fgetc(stream) == EOF && !ferror(stream)) {
				*size = (u32)length;
			} else {
				free(bytes);
				bytes = NULL;
			}
		}
	}
	fclose(stream);
	return bytes;
}

static const char *uniqueIniValue(const ini_section_t *ini, const char *key)
{
	const char *value = NULL;
	for (s32 i = 0; i < ini->count; i++) {
		if (strcmp(ini->pairs[i].key, key) != 0) continue;
		if (value) return NULL;
		value = ini->pairs[i].value;
	}
	return value;
}

static s32 readDescriptor(const void *bytes, u32 size, const char *path,
	ini_section_t *ini, char *err, size_t cap)
{
	u32 length = 0;
	char *text = assetArchiveExtractDescriptorMemAlloc(bytes, size, path,
		ASSET_ARCHIVE_VALIDATE_RELEASE, &length, NULL);
	if (!text) return fail(err, cap, "the native arena or scenario descriptor is missing");
	s32 valid = iniParseText(text, length, ini, err, cap);
	free(text);
	return valid;
}

static forge_level_doc_t *readScenarioDocument(const void *scenario, u32 size, char *err, size_t cap)
{
	forge_level_texts_t texts = {0};
	void *members[FORGE_LEVEL_MEMBER_COUNT] = {0};
	forge_level_doc_t *doc = NULL;
	for (s32 i = 0; i < FORGE_LEVEL_MEMBER_COUNT; i++) {
		const char *name = forgeLevelDocMemberName(i);
		u32 length = 0;
		members[i] = modArchiveExtractMemAlloc(scenario, size, name, &length);
		if (!members[i] && (!forgeLevelDocMemberOptional(i) || modArchiveLastError() != MODARCHIVE_ERR_NOTFOUND)) {
			if (err && cap) snprintf(err, cap, "the pinned scenario could not read %s", name);
			goto done;
		}
		texts.text[i] = members[i];
		texts.size[i] = length;
	}
	doc = forgeLevelDocRead(&texts, err, cap);
done:
	for (s32 i = 0; i < FORGE_LEVEL_MEMBER_COUNT; i++) free(members[i]);
	return doc;
}

static forge_level_doc_t *decodeBaseline(const grid_edit_room_invite_t *invite, const void *bytes,
	u32 size, const char *path, char *err, size_t cap)
{
	if (!bytes || !size || size > GRID_EDIT_ARENA_MAX_BYTES) {
		fail(err, cap, "the exact Grid arena is missing or exceeds the edit limit");
		return NULL;
	}

	u8 hash[SHA256_DIGEST_SIZE];
	char hex[SHA256_HEX_SIZE];
	sha256Hash(bytes, size, hash);
	sha256ToHex(hash, hex);
	if (strcmp(hex, invite->arena_sha256) != 0) {
		fail(err, cap, "the available Grid arena has a different saved revision than the invitation");
		return NULL;
	}

	forge_level_doc_t *doc = NULL;
	ini_section_t *ini = NULL;
	void *scenario = NULL;
	if (modAssetValidateArchiveBytes(bytes, size, path, err, cap) != MOD_ASSET_VALIDATE_OK) goto done;

	ini = malloc(sizeof(*ini));
	if (!ini) {
		fail(err, cap, "could not allocate the Grid arena descriptor");
		goto done;
	}
	if (!readDescriptor(bytes, size, path, ini, err, cap)) goto done;
	const char *arena_id = uniqueIniValue(ini, "catalog_id");
	const char *scenario_path = uniqueIniValue(ini, "scenario_archive");
	const char *scenario_id = uniqueIniValue(ini, "scenario");
	if (!arena_id || strcmp(arena_id, invite->arena_id) != 0 || !arenaIdValid(scenario_id)
			|| !scenario_path || assetPathIsAbsolute(scenario_path) || assetPathHasParentTraversal(scenario_path)
			|| strchr(scenario_path, ':') || strchr(scenario_path, '\\')
			|| assetArchiveTypeForPath(scenario_path) != ASSET_SCENARIO) {
		fail(err, cap, "the Grid arena ID or embedded scenario does not match the invitation");
		goto done;
	}

	/* The scenario parse replaces the INI, so retain the referenced ID. */
	char expected_scenario[CATALOG_ID_LEN];
	strcpy(expected_scenario, scenario_id);
	u32 scenario_size = 0;
	scenario = modArchiveExtractMemAlloc(bytes, size, scenario_path, &scenario_size);
	if (!scenario || !scenario_size) {
		fail(err, cap, "the pinned Grid arena does not contain its native scenario");
		goto done;
	}
	if (!readDescriptor(scenario, scenario_size, ".pdscenario", ini, err, cap)) goto done;
	const char *embedded_id = uniqueIniValue(ini, "catalog_id");
	if (!embedded_id || strcmp(embedded_id, expected_scenario) != 0) {
		fail(err, cap, "the pinned Grid arena's embedded scenario ID does not match");
		goto done;
	}
	doc = readScenarioDocument(scenario, scenario_size, err, cap);
done:
	free(scenario);
	free(ini);
	return doc;
}

static forge_level_doc_t *openBaseline(const grid_edit_room_invite_t *invite, const char *path,
	char *err, size_t cap)
{
	if (!path || !path[0] || assetArchiveTypeForPath(path) != ASSET_ARENA) {
		fail(err, cap, "the exact saved Grid arena is unavailable locally");
		return NULL;
	}
	u32 size = 0;
	void *bytes = readArena(path, &size);
	forge_level_doc_t *doc = decodeBaseline(invite, bytes, size, path, err, cap);
	free(bytes);
	return doc;
}

static grid_edit_room_t *openRoom(const grid_edit_room_invite_t *invite, forge_level_doc_t *opened,
	s32 is_host, char *err, size_t cap)
{
	if (!opened) return NULL;
	grid_edit_room_t *room = calloc(1, sizeof(*room));
	if (!room) {
		forgeLevelDocFree(opened);
		fail(err, cap, "could not allocate the Grid room");
		return NULL;
	}

	room->session = gridEditSessionCreate(invite->host_actor, opened);
	scenario_stage_atmosphere_t atmosphere = {0};
	forgeLevelDocGetAtmosphere(opened, &atmosphere);
	forgeLevelDocFree(opened);
	if (!room->session) {
		free(room);
		fail(err, cap, "could not clone the pinned Grid draft");
		return NULL;
	}

	room->state.invite = *invite;
	room->latest_generation = invite->identity.generation;
	room->state.is_host = is_host;
	room->state.revision = invite->baseline_revision;
	room->state.next_sequence = 1;
	room->state.enabled = atmosphere.has_ambient;
	room->state.brightness = atmosphere.ambient_light;
	return room;
}

grid_edit_room_t *gridEditRoomHostOpen(const grid_edit_wire_identity_t *identity,
	u64 host_actor, const char *arena_id, const char *arena_path, const char *arena_sha256,
	char *err, size_t cap)
{
	if (err && cap) err[0] = '\0';
	if (!identityValid(identity) || !host_actor || !arenaIdValid(arena_id) || !digestValid(arena_sha256)) {
		fail(err, cap, "the Grid room or pinned arena identity is invalid");
		return NULL;
	}

	grid_edit_room_invite_t invite = {0};
	invite.identity = *identity;
	strncpy(invite.arena_id, arena_id, sizeof(invite.arena_id) - 1);
	memcpy(invite.arena_sha256, arena_sha256, sizeof(invite.arena_sha256));
	invite.baseline_revision = GRID_EDIT_BASELINE_REVISION;
	invite.host_actor = host_actor;
	grid_edit_room_t *room = openRoom(&invite, openBaseline(&invite, arena_path, err, cap), 1, err, cap);
	if (room) room->state.local_actor = host_actor;
	return room;
}

s32 gridEditRoomHostInvite(grid_edit_room_t *room, u64 peer_actor, u64 peer_connect_generation,
	grid_edit_room_invite_t *out)
{
	if (!room || !room->state.is_host || !out || room->state.ready || room->state.pending_invite
			|| room->state.creator_detached
			|| !peer_actor || peer_actor == room->state.local_actor || !peer_connect_generation
			|| room->state.revision != room->state.invite.baseline_revision) return 0;

	room->state.peer_actor = peer_actor;
	room->peer_connect_generation = peer_connect_generation;
	room->state.pending_invite = 1;
	*out = room->state.invite;
	return 1;
}

s32 gridEditRoomHostDetachPeer(grid_edit_room_t *room, u64 peer_actor, u64 peer_connect_generation)
{
	if (!room || !room->state.is_host || !room->state.ready
			|| room->state.peer_actor != peer_actor || room->peer_connect_generation != peer_connect_generation
			|| !gridEditSessionRemoveMember(room->session, room->state.local_actor, peer_actor)) return 0;
	gridEditSessionFocusResource(room->session, room->state.local_actor, NULL);
	room->state.peer_actor = 0;
	room->peer_connect_generation = 0;
	room->state.ready = 0;
	room->state.creator_detached = 1;
	return 1;
}

static s32 pendingInviteMatches(const grid_edit_room_t *room, u64 actor, u64 connect_generation)
{
	return room && room->state.is_host && room->state.pending_invite && !room->state.ready
		&& room->state.peer_actor == actor && room->peer_connect_generation == connect_generation;
}

s32 gridEditRoomHostCancelInvite(grid_edit_room_t *room, u64 peer_actor, u64 peer_connect_generation)
{
	if (!pendingInviteMatches(room, peer_actor, peer_connect_generation)) return 0;
	if (room->state.pending_resume) {
		gridEditSessionFocusResource(room->session, room->state.local_actor, NULL);
		room->state.invite.identity = room->prior_identity;
		clearResumeState(room);
	}
	room->state.pending_invite = 0;
	room->state.peer_actor = 0;
	room->peer_connect_generation = 0;
	return 1;
}

static s32 grantPendingCreator(grid_edit_room_t *room, u64 peer_actor)
{
	if (!gridEditSessionSetMember(room->session, room->state.local_actor, peer_actor, GRID_EDIT_ROLE_CREATOR)) {
		return 0;
	}
	if (room->resume_document) {
		gridEditSessionAdoptSnapshotDocument(room->session, room->resume_document);
		room->resume_document = NULL;
	}
	room->latest_peer_actor = peer_actor;
	room->state.pending_invite = 0;
	room->state.creator_detached = 0;
	room->state.ready = 1;
	clearResumeState(room);
	return 1;
}

s32 gridEditRoomHostGrant(grid_edit_room_t *room, u64 peer_actor, u64 peer_connect_generation,
	const grid_edit_room_invite_t *confirmation)
{
	if (!pendingInviteMatches(room, peer_actor, peer_connect_generation) || room->state.pending_resume
			|| !inviteMatches(&room->state.invite, confirmation)
			|| room->state.revision != confirmation->baseline_revision) {
		return 0;
	}
	return grantPendingCreator(room, peer_actor);
}

void *gridEditRoomHostPrepareResume(grid_edit_room_t *room, const grid_edit_wire_identity_t *fresh_identity,
	u64 peer_actor, u64 peer_connect_generation, grid_edit_room_invite_t *out_invite,
	grid_edit_snapshot_offer_t *out_offer, char *err, size_t cap)
{
	if (err && cap) err[0] = '\0';
	if (!room || !room->state.is_host || !room->state.creator_detached || room->state.ready
			|| room->state.pending_invite || !room->state.next_sequence || !out_invite || !out_offer
			|| !identityValid(fresh_identity) || fresh_identity->room_id != room->state.invite.identity.room_id
			|| fresh_identity->generation <= room->latest_generation
			|| peer_actor <= room->latest_peer_actor || peer_actor == room->state.local_actor
			|| !peer_connect_generation) {
		fail(err, cap, "the detached Grid draft needs a fresh room generation and creator identity");
		return NULL;
	}

	u64 revision = 0;
	forge_level_doc_t *document = gridEditRoomHostCloneDocument(room, &revision);
	if (!document || revision != room->state.revision) {
		forgeLevelDocFree(document);
		fail(err, cap, "could not read the exact retained Grid draft revision");
		return NULL;
	}
	grid_edit_room_invite_t invite = room->state.invite;
	invite.identity = *fresh_identity;
	const grid_edit_snapshot_identity_t identity = snapshotIdentity(&invite, revision);
	grid_edit_snapshot_offer_t offer = {
		.identity = invite.identity, .revision = revision, .host_sequence = room->state.next_sequence - 1,
	};
	void *bytes = gridEditSnapshotEncode(&identity, document, &offer.size, offer.sha256, err, cap);
	/* Rebuild the same native editor metadata as the guest from the shared
	 * snapshot and pinned members. A canceled invite keeps this draft intact. */
	forge_level_doc_t *canonical = bytes ? gridEditSnapshotDecodeWithBaseline(bytes, offer.size, &identity,
		offer.sha256, document, NULL, err, cap) : NULL;
	forgeLevelDocFree(document);
	if (!canonical) {
		free(bytes);
		return NULL;
	}

	room->prior_identity = room->state.invite.identity;
	room->latest_generation = invite.identity.generation;
	room->latest_peer_actor = peer_actor;
	room->state.invite = invite;
	room->state.pending_invite = 1;
	room->state.pending_resume = 1;
	room->state.snapshot_ready = 1;
	room->state.peer_actor = peer_actor;
	room->peer_connect_generation = peer_connect_generation;
	room->resume_offer = offer;
	room->resume_document = canonical;
	*out_invite = invite;
	*out_offer = offer;
	return bytes;
}

s32 gridEditRoomHostConfirmResume(grid_edit_room_t *room, u64 peer_actor, u64 peer_connect_generation,
	const grid_edit_room_invite_t *confirmation)
{
	if (!pendingInviteMatches(room, peer_actor, peer_connect_generation) || !room->state.pending_resume
			|| !inviteMatches(&room->state.invite, confirmation)) return 0;
	room->resume_confirmed = 1;
	return 1;
}

s32 gridEditRoomHostGrantResume(grid_edit_room_t *room, u64 peer_actor, u64 peer_connect_generation,
	const grid_edit_room_invite_t *confirmation, const grid_edit_snapshot_offer_t *ack,
	grid_edit_room_resume_tail_t *out_tail)
{
	if (!out_tail || !pendingInviteMatches(room, peer_actor, peer_connect_generation) || !room->state.pending_resume
			|| !room->resume_confirmed || !inviteMatches(&room->state.invite, confirmation)
			|| !gridEditSnapshotOfferMatches(&room->resume_offer, ack)) return 0;

	grid_edit_room_resume_tail_t tail = {0};
	u64 latest = 0;
	if (gridEditSessionReadDeltas(room->session, room->state.local_actor, ack->revision, tail.deltas,
			GRID_EDIT_DELTA_CAPACITY, &tail.count, &latest) != GRID_EDIT_TAIL_READY
			|| latest != room->state.revision || (tail.count == 0) != (room->resume_document != NULL)) return 0;

	u64 revision = ack->revision;
	u64 sequence = ack->host_sequence;
	for (size_t i = 0; i < tail.count; i++) {
		const grid_edit_delta_t *delta = &tail.deltas[i];
		if (delta->author_id != room->state.local_actor || delta->command.expected_revision != revision
				|| delta->revision != revision + 1 || delta->command.sequence != sequence + 1) return 0;
		revision = delta->revision;
		sequence = delta->command.sequence;
	}
	if (revision != latest || room->state.next_sequence != sequence + 1
			|| !grantPendingCreator(room, peer_actor)) return 0;
	*out_tail = tail;
	return 1;
}

static grid_edit_room_t *openGuestRoom(const grid_edit_room_invite_t *invite,
	u64 host_connect_generation, forge_level_doc_t *doc, char *err, size_t cap)
{
	grid_edit_room_t *room = openRoom(invite, doc, 0, err, cap);
	if (room) {
		room->state.peer_actor = invite->host_actor;
		room->peer_connect_generation = host_connect_generation;
		room->state.pending_invite = 1;
	}
	return room;
}

grid_edit_room_t *gridEditRoomGuestOpen(const grid_edit_room_invite_t *invite,
	u64 host_connect_generation, const char *local_arena_path, char *err, size_t cap)
{
	if (err && cap) err[0] = '\0';
	if (!inviteValid(invite) || !host_connect_generation) {
		fail(err, cap, "the Grid invitation or host connection identity is invalid");
		return NULL;
	}
	return openGuestRoom(invite, host_connect_generation,
		openBaseline(invite, local_arena_path, err, cap), err, cap);
}

grid_edit_room_t *gridEditRoomGuestOpenBytes(const grid_edit_room_invite_t *invite,
	u64 host_connect_generation, const void *arena_bytes, u32 arena_size, char *err, size_t cap)
{
	if (err && cap) err[0] = '\0';
	if (!inviteValid(invite) || !host_connect_generation) {
		fail(err, cap, "the Grid invitation or host connection identity is invalid");
		return NULL;
	}
	return openGuestRoom(invite, host_connect_generation,
		decodeBaseline(invite, arena_bytes, arena_size, ".pdarena", err, cap), err, cap);
}

static s32 pendingGuestMatches(const grid_edit_room_t *room, const grid_edit_wire_identity_t *identity,
	u64 host_connect_generation)
{
	return room && !room->state.is_host && room->state.pending_invite && !room->state.ready
		&& host_connect_generation == room->peer_connect_generation
		&& identityMatches(&room->state.invite.identity, identity);
}

s32 gridEditRoomGuestRequireResume(grid_edit_room_t *room, const grid_edit_wire_identity_t *identity,
	u64 host_connect_generation)
{
	if (!pendingGuestMatches(room, identity, host_connect_generation)) return 0;
	room->state.pending_resume = 1;
	return 1;
}

s32 gridEditRoomGuestInstallResume(grid_edit_room_t *room, u64 host_connect_generation,
	const grid_edit_snapshot_offer_t *offer, const void *bytes, u32 size, char *err, size_t cap)
{
	if (err && cap) err[0] = '\0';
	if (!offer || !pendingGuestMatches(room, &offer->identity, host_connect_generation)
			|| !room->state.pending_resume || room->state.snapshot_ready
			|| offer->revision < room->state.invite.baseline_revision || offer->host_sequence >= offer->revision
			|| !size || size != offer->size) {
		return fail(err, cap, "the Grid snapshot is outside the accepted resume invitation");
	}

	const grid_edit_snapshot_identity_t expected = snapshotIdentity(&room->state.invite, offer->revision);
	/* Until this snapshot is installed the pending guest session contains the
	 * exact authenticated pinned arena opened for the invitation. */
	forge_level_doc_t *baseline = gridEditSessionCloneDocument(room->session, room->state.peer_actor, NULL);
	if (!baseline) return fail(err, cap, "could not read the verified pinned Grid baseline");
	forge_level_doc_t *opened = gridEditSnapshotDecodeWithBaseline(bytes, size, &expected, offer->sha256,
		baseline, NULL, err, cap);
	forgeLevelDocFree(baseline);
	if (!opened) return 0;

	grid_edit_session_t *replica = gridEditSessionCreateAtRevision(room->state.peer_actor, opened,
		offer->revision, offer->host_sequence);
	scenario_stage_atmosphere_t atmosphere = {0};
	forgeLevelDocGetAtmosphere(opened, &atmosphere);
	forgeLevelDocFree(opened);
	if (!replica) return fail(err, cap, "could not install the verified Grid draft replica");

	gridEditSessionFree(room->session);
	room->session = replica;
	room->state.revision = offer->revision;
	room->state.next_sequence = 1;
	room->state.enabled = atmosphere.has_ambient;
	room->state.brightness = atmosphere.ambient_light;
	room->resume_offer = *offer;
	room->state.snapshot_ready = 1;
	return 1;
}

s32 gridEditRoomGuestGrant(grid_edit_room_t *room, const grid_edit_room_invite_t *invite,
	u64 host_connect_generation, u64 granted_actor_id)
{
	if (!invite || !pendingGuestMatches(room, &invite->identity, host_connect_generation)
			|| (room->state.pending_resume && !room->state.snapshot_ready)
			|| !granted_actor_id || granted_actor_id == room->state.peer_actor
			|| !inviteMatches(&room->state.invite, invite)
			|| !gridEditSessionSetMember(room->session, room->state.peer_actor, granted_actor_id, GRID_EDIT_ROLE_CREATOR)) {
		return 0;
	}

	room->state.local_actor = granted_actor_id;
	room->state.pending_invite = 0;
	room->state.ready = 1;
	clearResumeState(room);
	return 1;
}

static s32 ambientValid(s32 enabled, s32 brightness)
{
	return (enabled == 0 || enabled == 1) && brightness >= 0 && brightness <= UINT8_MAX;
}

static s32 commandValid(const grid_edit_command_t *command)
{
	if (!command) return 0;
	switch (command->operation) {
	case GRID_EDIT_MOVE_SPAWN:
		return gridEditMoveSpawnValid(&command->data.move_spawn);
	case GRID_EDIT_SET_AMBIENT_LIGHT:
		return ambientValid(command->data.ambient_light.enabled, command->data.ambient_light.brightness);
	case GRID_EDIT_MOVE_DECORATION:
		return gridEditMoveDecorationValid(&command->data.move_decoration);
	}
	return 0;
}

typedef union grid_edit_previous_position {
	grid_edit_move_spawn_t spawn;
	grid_edit_move_decoration_t decoration;
} grid_edit_previous_position_t;

static void readPreviousPosition(const grid_edit_room_t *room, u64 actor, const grid_edit_command_t *command,
	grid_edit_previous_position_t *previous)
{
	if (actor != room->state.local_actor) return;
	if (command->operation == GRID_EDIT_MOVE_SPAWN) {
		gridEditSessionFindSpawnPose(room->session, actor, command->data.move_spawn.spawn_id, &previous->spawn);
	} else if (command->operation == GRID_EDIT_MOVE_DECORATION) {
		gridEditSessionFindDecorationPose(room->session, actor, command->data.move_decoration.record_id,
			&previous->decoration);
	}
}

static void observeCommit(grid_edit_room_t *room, u64 actor, u64 revision, const grid_edit_command_t *command,
	const grid_edit_previous_position_t *previous)
{
	const s32 local = actor == room->state.local_actor;
	room->state.can_undo_ambient = 0;
	room->state.previous_ambient = (grid_edit_ambient_light_t){0};
	room->state.can_undo_spawn = 0;
	room->state.previous_spawn = (grid_edit_move_spawn_t){0};
	room->state.can_undo_decoration = 0;
	room->state.previous_decoration = (grid_edit_move_decoration_t){0};
	if (command->operation == GRID_EDIT_SET_AMBIENT_LIGHT) {
		const s32 enabled = command->data.ambient_light.enabled;
		const s32 brightness = enabled ? command->data.ambient_light.brightness : 0;
		room->state.can_undo_ambient = local
			&& (room->state.enabled != enabled || room->state.brightness != brightness);
		if (room->state.can_undo_ambient) {
			room->state.previous_ambient = (grid_edit_ambient_light_t){room->state.enabled, room->state.brightness};
		}
		room->state.enabled = enabled;
		room->state.brightness = brightness;
	} else if (command->operation == GRID_EDIT_MOVE_SPAWN && previous->spawn.spawn_id[0]) {
		const grid_edit_move_spawn_t *prior = &previous->spawn;
		grid_edit_move_spawn_t current;
		if (gridEditSessionFindSpawnPose(room->session, actor, prior->spawn_id, &current)
				&& (memcmp(current.position, prior->position, sizeof(current.position)) != 0
				|| memcmp(current.look, prior->look, sizeof(current.look)) != 0)) {
			room->state.can_undo_spawn = 1;
			room->state.previous_spawn = *prior;
		}
	} else if (command->operation == GRID_EDIT_MOVE_DECORATION && previous->decoration.record_id[0]) {
		const grid_edit_move_decoration_t *prior = &previous->decoration;
		grid_edit_move_decoration_t current;
		if (gridEditSessionFindDecorationPose(room->session, actor, prior->record_id, &current)
				&& (current.x != prior->x || current.z != prior->z)) {
			room->state.can_undo_decoration = 1;
			room->state.previous_decoration = *prior;
		}
	}
	room->state.revision = revision;
	if (local) {
		room->state.next_sequence = command->sequence == UINT64_MAX ? 0 : command->sequence + 1;
		room->state.pending_command = 0;
	}
}

static s32 hostActorMatches(const grid_edit_room_t *room, u64 actor, u64 connect_generation)
{
	return room && room->state.is_host && actor
		&& ((actor == room->state.local_actor && !connect_generation && roomCanEdit(room))
			|| (room->state.ready && actor == room->state.peer_actor && connect_generation
				&& connect_generation == room->peer_connect_generation));
}

s32 gridEditRoomHostAdvanceClock(grid_edit_room_t *room, u64 now_ms)
{
	return room && room->state.is_host && roomCanEdit(room)
		&& gridEditSessionAdvanceClock(room->session, room->state.local_actor, now_ms);
}

grid_edit_lease_result_t gridEditRoomHostFocusResource(grid_edit_room_t *room, u64 actor,
	u64 connect_generation, const grid_edit_resource_t *resource_or_null, u64 now_ms)
{
	if (!hostActorMatches(room, actor, connect_generation)) return GRID_EDIT_LEASE_DENIED;
	if (!gridEditRoomHostAdvanceClock(room, now_ms)) return GRID_EDIT_LEASE_CLOCK_UNAVAILABLE;
	return gridEditSessionFocusResource(room->session, actor, resource_or_null);
}

grid_edit_result_t gridEditRoomHostApply(grid_edit_room_t *room, const grid_edit_wire_identity_t *identity,
	u64 actor, u64 connect_generation, const grid_edit_command_t *command,
	grid_edit_receipt_t *receipt, grid_edit_delta_t *out_delta, char *err, size_t cap)
{
	if (err && cap) err[0] = '\0';
	if (receipt) receipt->revision = 0;

	if (!hostActorMatches(room, actor, connect_generation)
			|| !identityMatches(&room->state.invite.identity, identity)) {
		fail(err, cap, "the actor has no editing grant for this Grid room");
		return GRID_EDIT_DENIED;
	}
	if (!commandValid(command)) {
		fail(err, cap, "invalid Grid room edit");
		return GRID_EDIT_INVALID;
	}

	const s32 resuming = !room->state.ready;
	if (resuming && command->sequence == room->state.next_sequence
			&& command->expected_revision == room->state.revision
			&& (room->state.revision < room->resume_offer.revision
				|| room->state.revision - room->resume_offer.revision >= GRID_EDIT_DELTA_CAPACITY)) {
		fail(err, cap, "wait for the invited creator to catch up before making more edits");
		return GRID_EDIT_RESOURCE_EXHAUSTED;
	}

	/* Both editors must apply the tail to the same canonical document. Keep
	 * the original until the first successful commit, including on refusal. */
	forge_level_doc_t *previous_document = NULL;
	if (resuming && room->resume_document) {
		previous_document = gridEditSessionSwapDocument(room->session, room->resume_document);
		room->resume_document = NULL;
	}
	grid_edit_previous_position_t previous = {0};
	readPreviousPosition(room, actor, command, &previous);
	grid_edit_receipt_t committed = {0};
	grid_edit_result_t result = gridEditSessionApply(room->session, actor, command, &committed, err, cap);
	if (previous_document) {
		if (result == GRID_EDIT_APPLIED) {
			forgeLevelDocFree(previous_document);
		} else {
			room->resume_document = gridEditSessionSwapDocument(room->session, previous_document);
		}
	}
	if (receipt) *receipt = committed;
	if (result == GRID_EDIT_APPLIED) {
		observeCommit(room, actor, committed.revision, command, &previous);
		if (out_delta) *out_delta = (grid_edit_delta_t){committed.revision, actor, *command};
	}
	return result;
}

static s32 buildGuestCommand(grid_edit_room_t *room, grid_edit_command_t *command, grid_edit_command_t *out)
{
	if (!room || room->state.is_host || !room->state.ready || room->state.pending_command
			|| !room->state.next_sequence || room->state.revision == UINT64_MAX
			|| !out || !commandValid(command)) return 0;
	if (command->operation == GRID_EDIT_MOVE_SPAWN) {
		grid_edit_move_spawn_t current;
		if (!gridEditSessionFindSpawnPose(room->session, room->state.local_actor,
				command->data.move_spawn.spawn_id, &current)) return 0;
	} else if (command->operation == GRID_EDIT_MOVE_DECORATION) {
		grid_edit_move_decoration_t current;
		if (!gridEditSessionFindDecorationPose(room->session, room->state.local_actor,
				command->data.move_decoration.record_id, &current)) return 0;
	}
	command->sequence = room->state.next_sequence;
	command->expected_revision = room->state.revision;
	room->pending = *command;
	room->state.pending_command = 1;
	*out = *command;
	return 1;
}

s32 gridEditRoomGuestBuildAmbient(grid_edit_room_t *room, s32 enabled, s32 brightness,
	grid_edit_command_t *out)
{
	grid_edit_command_t command = {
		.operation = GRID_EDIT_SET_AMBIENT_LIGHT,
		.data.ambient_light = {enabled, brightness},
	};
	return buildGuestCommand(room, &command, out);
}

s32 gridEditRoomGuestBuildMoveSpawn(grid_edit_room_t *room, const grid_edit_move_spawn_t *move,
	grid_edit_command_t *out)
{
	if (!move) return 0;
	grid_edit_command_t command = {
		.operation = GRID_EDIT_MOVE_SPAWN,
		.data.move_spawn = *move,
	};
	return buildGuestCommand(room, &command, out);
}

s32 gridEditRoomGuestBuildMoveDecoration(grid_edit_room_t *room, const grid_edit_move_decoration_t *move,
	grid_edit_command_t *out)
{
	if (!move) return 0;
	grid_edit_command_t command = {
		.operation = GRID_EDIT_MOVE_DECORATION,
		.data.move_decoration = *move,
	};
	return buildGuestCommand(room, &command, out);
}

static s32 guestHostMatches(const grid_edit_room_t *room, const grid_edit_wire_identity_t *identity,
	u64 host_connect_generation)
{
	return room && !room->state.is_host && room->state.ready
		&& room->peer_connect_generation == host_connect_generation
		&& identityMatches(&room->state.invite.identity, identity);
}

s32 gridEditRoomGuestReceiveReceipt(grid_edit_room_t *room, const grid_edit_wire_identity_t *identity,
	u64 host_connect_generation, u64 sequence, u64 expected_revision, grid_edit_result_t result)
{
	if (!guestHostMatches(room, identity, host_connect_generation) || !room->state.pending_command
			|| room->pending.sequence != sequence || room->pending.expected_revision != expected_revision) return 0;

	switch (result) {
	case GRID_EDIT_APPLIED:
	case GRID_EDIT_DUPLICATE:
		return 1;
	case GRID_EDIT_DENIED:
	case GRID_EDIT_INVALID:
	case GRID_EDIT_STALE:
	case GRID_EDIT_OUT_OF_ORDER:
	case GRID_EDIT_SEQUENCE_REUSED:
	case GRID_EDIT_RESOURCE_EXHAUSTED:
	case GRID_EDIT_RESOURCE_LEASED:
		room->state.pending_command = 0;
		return 1;
	}
	return 0;
}

s32 gridEditRoomGuestApplyDelta(grid_edit_room_t *room, const grid_edit_wire_identity_t *identity,
	u64 host_connect_generation, const grid_edit_delta_t *delta, char *err, size_t cap)
{
	if (err && cap) err[0] = '\0';
	const s32 resuming = room && room->state.pending_resume && room->state.snapshot_ready
		&& pendingGuestMatches(room, identity, host_connect_generation);
	if ((!resuming && !guestHostMatches(room, identity, host_connect_generation)) || !delta
			|| (delta->author_id != room->state.peer_actor && (resuming || delta->author_id != room->state.local_actor))) {
		return fail(err, cap, "the delta is outside the authenticated Grid room");
	}
	if (resuming && (room->state.revision < room->resume_offer.revision
			|| room->state.revision - room->resume_offer.revision >= GRID_EDIT_DELTA_CAPACITY)) {
		return fail(err, cap, "the Grid resume tail exceeds the retained edit limit");
	}
	const grid_edit_command_t *command = &delta->command;
	if (!commandValid(command) || room->state.revision == UINT64_MAX
			|| command->expected_revision != room->state.revision || delta->revision != room->state.revision + 1) {
		return fail(err, cap, "the Grid delta does not follow the local draft revision");
	}
	if (delta->author_id == room->state.local_actor) {
		if (!room->state.pending_command || !gridEditCommandMatches(&room->pending, command)) {
			return fail(err, cap, "the Grid delta does not match the pending local edit");
		}
	}

	grid_edit_previous_position_t previous = {0};
	readPreviousPosition(room, delta->author_id, command, &previous);
	grid_edit_receipt_t committed = {0};
	if (gridEditSessionApply(room->session, delta->author_id, command, &committed, err, cap) != GRID_EDIT_APPLIED) {
		return 0;
	}
	observeCommit(room, delta->author_id, committed.revision, command, &previous);
	return 1;
}

forge_level_doc_t *gridEditRoomHostCloneDocument(const grid_edit_room_t *room, u64 *revision)
{
	if (revision) *revision = 0;
	if (!room || !room->state.is_host || (!room->state.ready && !room->state.creator_detached)) return NULL;
	return gridEditSessionCloneDocument(room->session, room->state.local_actor, revision);
}

s32 gridEditRoomSpawnCount(const grid_edit_room_t *room)
{
	return roomCanEdit(room) ? gridEditSessionSpawnCount(room->session, room->state.local_actor) : 0;
}

s32 gridEditRoomGetSpawnPose(const grid_edit_room_t *room, s32 index, grid_edit_move_spawn_t *out)
{
	return roomCanEdit(room) && gridEditSessionGetSpawnPose(room->session, room->state.local_actor, index, out);
}

s32 gridEditRoomDecorationCount(const grid_edit_room_t *room)
{
	return roomCanEdit(room) ? gridEditSessionDecorationCount(room->session, room->state.local_actor) : 0;
}

s32 gridEditRoomGetDecorationPose(const grid_edit_room_t *room, s32 index, grid_edit_move_decoration_t *out)
{
	return roomCanEdit(room)
		&& gridEditSessionGetDecorationPose(room->session, room->state.local_actor, index, out);
}

s32 gridEditRoomSetVisibility(grid_edit_room_t *room, s32 players_see_creators, s32 creators_see_match)
{
	return room && room->state.is_host && room->state.ready
		&& gridEditSessionSetVisibility(room->session, room->state.local_actor, players_see_creators, creators_see_match);
}

s32 gridEditRoomGetVisibility(const grid_edit_room_t *room, s32 *players_see_creators, s32 *creators_see_match)
{
	return room && room->state.is_host && room->state.ready
		&& gridEditSessionGetVisibility(room->session, room->state.local_actor, players_see_creators, creators_see_match);
}

s32 gridEditRoomGetState(const grid_edit_room_t *room, grid_edit_room_state_t *out)
{
	if (!room || !out) return 0;
	*out = room->state;
	out->can_edit = roomCanEdit(room);
	if (!out->is_host && !out->ready) {
		out->enabled = 0;
		out->brightness = 0;
	}
	return 1;
}

void gridEditRoomFree(grid_edit_room_t *room)
{
	if (!room) return;
	forgeLevelDocFree(room->resume_document);
	gridEditSessionFree(room->session);
	free(room);
}
