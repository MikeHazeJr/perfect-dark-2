#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net/grid_edit_snapshot.h"
#include "net/netbuf.h"
#include "platform.h"

enum {
	/* V1 describes only the members that belong to the shared draft. */
	SNAPSHOT_MEMBER_COUNT = 8,
	SNAPSHOT_IDENTITY_PREFIX = 1 + 1 + 8 + 8 + 1,
	SNAPSHOT_IDENTITY_SUFFIX = SHA256_DIGEST_SIZE + 1,
	SNAPSHOT_MEMBER_HEADER = 1 + 4,
	SNAPSHOT_MIN_BYTES = SNAPSHOT_IDENTITY_PREFIX + 3 + SNAPSHOT_IDENTITY_SUFFIX
		+ SNAPSHOT_MEMBER_HEADER * SNAPSHOT_MEMBER_COUNT + SHA256_DIGEST_SIZE
};

_Static_assert(CATALOG_ID_LEN - 1 <= UINT8_MAX, "Grid snapshot arena ID must fit its length field");
_Static_assert(FORGE_LEVEL_MEMBER_COVERS == SNAPSHOT_MEMBER_COUNT, "Grid v1 member ordinals must stay stable");
_Static_assert(FORGE_LEVEL_CHECKPOINT_MAX_BYTES <= UINT32_MAX, "Grid snapshot size must fit a u32");

/* These native members stay in the authenticated arena, in document order. */
static const struct {
	s32 member;
	const char *edit_name;
} snapshot_inherited[] = {
	{ FORGE_LEVEL_MEMBER_COVERS, "cover" },
	{ FORGE_LEVEL_MEMBER_PATHS, "path" },
	{ FORGE_LEVEL_MEMBER_ROOMS, "room lighting" },
};

static void *snapshotError(char *err, size_t cap, const char *message)
{
	if (err && cap) snprintf(err, cap, "%s", message);
	return NULL;
}

static s32 checkInheritedMembers(const forge_level_doc_t *doc, char *err, size_t cap)
{
	for (size_t i = 0; i < sizeof(snapshot_inherited) / sizeof(snapshot_inherited[0]); i++) {
		if (!forgeLevelDocMemberChanged(doc, snapshot_inherited[i].member)) continue;
		if (err && cap) snprintf(err, cap, "save offline %s edits into the pinned arena before sharing its draft",
			snapshot_inherited[i].edit_name);
		return 0;
	}
	return 1;
}

static s32 digestPresent(const u8 digest[SHA256_DIGEST_SIZE])
{
	static const u8 zero[SHA256_DIGEST_SIZE] = {0};
	return digest && memcmp(digest, zero, sizeof(zero)) != 0;
}

static s32 identityValid(const grid_edit_snapshot_identity_t *identity)
{
	if (!identity || identity->room.room_id == 0xff || !identity->room.generation
			|| !digestPresent(identity->arena_sha256)) return 0;

	const char *id = identity->arena_id;
	const char *end = memchr(id, '\0', sizeof(identity->arena_id));
	if (!end || strspn(id, "abcdefghijklmnopqrstuvwxyz0123456789:_.-") != (size_t)(end - id)) return 0;
	const char *separator = strchr(id, ':');
	return separator && separator != id && separator[1] && !strchr(separator + 1, ':');
}

static s32 identityMatches(const grid_edit_snapshot_identity_t *got,
	const grid_edit_snapshot_identity_t *expected)
{
	return identityValid(got) && got->revision
		&& got->room.room_id == expected->room.room_id
		&& got->room.generation == expected->room.generation
		&& (!expected->revision || got->revision == expected->revision)
		&& strcmp(got->arena_id, expected->arena_id) == 0
		&& memcmp(got->arena_sha256, expected->arena_sha256, sizeof(got->arena_sha256)) == 0;
}

/* Use the same member-presence rules as the native archive writer. */
static s32 snapshotTexts(forge_level_doc_t *doc, forge_level_texts_t *out, char *err, size_t cap)
{
	forge_level_overrides_t members;
	if (!forgeLevelDocOverridesAll(doc, NULL, &members, err, cap)) return 0;

	u32 next = 0;
	for (s32 i = 0; i < SNAPSHOT_MEMBER_COUNT; i++) {
		if (next < members.count && strcmp(members.entries[next].entry, forgeLevelDocMemberName(i)) == 0) {
			const asset_archive_clone_override_t *member = &members.entries[next++];
			if (!member->data || !member->len || member->remove) return 0;
			out->text[i] = member->data;
			out->size[i] = member->len;
		} else if (!forgeLevelDocMemberOptional(i)) {
			return 0;
		}
	}
	for (size_t i = 0; i < sizeof(snapshot_inherited) / sizeof(snapshot_inherited[0]) && next < members.count; ++i) {
		if (strcmp(members.entries[next].entry, forgeLevelDocMemberName(snapshot_inherited[i].member)) == 0) ++next;
	}
	return next == members.count;
}

static void writeU32(struct netbuf *dst, u32 value)
{
	const u32 encoded = PD_LE32(value);
	netbufWriteData(dst, &encoded, sizeof(encoded));
}

static void writeU64(struct netbuf *dst, u64 value)
{
	const u64 encoded = PD_LE64(value);
	netbufWriteData(dst, &encoded, sizeof(encoded));
}

void *gridEditSnapshotEncode(const grid_edit_snapshot_identity_t *identity,
	forge_level_doc_t *doc, u32 *out_size, u8 out_sha256[SHA256_DIGEST_SIZE], char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!doc || !out_size || !out_sha256 || !identityValid(identity) || !identity->revision) {
		return snapshotError(err, err_cap, "invalid Grid snapshot identity or output");
	}
	if (!checkInheritedMembers(doc, err, err_cap)) return NULL;
	forge_level_texts_t texts = {0};
	if (!snapshotTexts(doc, &texts, err, err_cap)) {
		if (err && err_cap && err[0]) return NULL;
		return snapshotError(err, err_cap, "the Grid draft is missing a native snapshot member");
	}

	const u8 id_length = (u8)strlen(identity->arena_id);
	size_t size = SNAPSHOT_IDENTITY_PREFIX + id_length + SNAPSHOT_IDENTITY_SUFFIX
		+ SNAPSHOT_MEMBER_HEADER * SNAPSHOT_MEMBER_COUNT + SHA256_DIGEST_SIZE;
	for (s32 i = 0; i < SNAPSHOT_MEMBER_COUNT; i++) {
		if (texts.size[i] > FORGE_LEVEL_CHECKPOINT_MAX_BYTES - size) {
			return snapshotError(err, err_cap, "the Grid snapshot exceeds the native document size limit");
		}
		size += texts.size[i];
	}

	u8 *bytes = malloc(size);
	if (!bytes) return snapshotError(err, err_cap, "could not allocate the Grid snapshot");

	const u32 prefix_size = (u32)(size - SHA256_DIGEST_SIZE);
	struct netbuf wire = { .data = bytes, .size = prefix_size };
	netbufWriteU8(&wire, GRID_EDIT_SNAPSHOT_WIRE_VERSION);
	netbufWriteU8(&wire, identity->room.room_id);
	writeU64(&wire, identity->room.generation);
	writeU64(&wire, identity->revision);
	netbufWriteU8(&wire, id_length);
	netbufWriteData(&wire, identity->arena_id, id_length);
	netbufWriteData(&wire, identity->arena_sha256, SHA256_DIGEST_SIZE);
	netbufWriteU8(&wire, SNAPSHOT_MEMBER_COUNT);

	for (s32 i = 0; i < SNAPSHOT_MEMBER_COUNT; i++) {
		const u32 length = (u32)texts.size[i];
		netbufWriteU8(&wire, (u8)i);
		writeU32(&wire, length);
		if (length) netbufWriteData(&wire, texts.text[i], length);
	}
	if (wire.error || wire.wp != prefix_size) {
		free(bytes);
		return snapshotError(err, err_cap, "could not encode the Grid snapshot");
	}

	u8 *digest = bytes + prefix_size;
	sha256Hash(bytes, prefix_size, digest);
	*out_size = (u32)size;
	memcpy(out_sha256, digest, SHA256_DIGEST_SIZE);
	return bytes;
}

static s32 readIdentity(struct netbuf *wire, grid_edit_snapshot_identity_t *out)
{
	if (netbufReadU8(wire) != GRID_EDIT_SNAPSHOT_WIRE_VERSION) return 0;
	out->room.room_id = netbufReadU8(wire);
	out->room.generation = netbufReadU64(wire);
	out->revision = netbufReadU64(wire);
	u8 length = netbufReadU8(wire);
	if (!length || length >= sizeof(out->arena_id) || wire->wp - wire->rp < length + SNAPSHOT_IDENTITY_SUFFIX) {
		return 0;
	}

	netbufReadData(wire, out->arena_id, length);
	if (memchr(out->arena_id, '\0', length)) return 0;
	netbufReadData(wire, out->arena_sha256, SHA256_DIGEST_SIZE);
	return netbufReadU8(wire) == SNAPSHOT_MEMBER_COUNT && !wire->error;
}

static s32 readTexts(struct netbuf *wire, forge_level_texts_t *out)
{
	for (s32 i = 0; i < SNAPSHOT_MEMBER_COUNT; i++) {
		if (wire->wp - wire->rp < SNAPSHOT_MEMBER_HEADER || netbufReadU8(wire) != (u8)i) return 0;
		const u32 length = netbufReadU32(wire);
		if (length > wire->wp - wire->rp || (!length && !forgeLevelDocMemberOptional(i))) return 0;
		if (length) {
			out->text[i] = (const char *)wire->data + wire->rp;
			out->size[i] = length;
			wire->rp += length;
		}
	}
	return !wire->error && wire->rp == wire->wp;
}

static forge_level_doc_t *snapshotDecode(const void *bytes, u32 size,
	const grid_edit_snapshot_identity_t *expected,
	const u8 expected_snapshot_sha256[SHA256_DIGEST_SIZE],
	forge_level_doc_t *baseline, grid_edit_snapshot_identity_t *received, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!bytes || size < SNAPSHOT_MIN_BYTES || size > FORGE_LEVEL_CHECKPOINT_MAX_BYTES
			|| !identityValid(expected) || !digestPresent(expected_snapshot_sha256)) {
		return snapshotError(err, err_cap, "invalid Grid snapshot size or authenticated identity");
	}
	const u32 prefix_size = size - SHA256_DIGEST_SIZE;
	const u8 *trailer = (const u8 *)bytes + prefix_size;
	if (memcmp(trailer, expected_snapshot_sha256, SHA256_DIGEST_SIZE) != 0) {
		return snapshotError(err, err_cap, "the Grid snapshot digest differs from the authenticated offer");
	}
	u8 digest[SHA256_DIGEST_SIZE];
	sha256Hash(bytes, prefix_size, digest);
	if (memcmp(digest, trailer, sizeof(digest)) != 0) {
		return snapshotError(err, err_cap, "the Grid snapshot payload failed its digest check");
	}

	struct netbuf wire;
	netbufStartReadData(&wire, bytes, prefix_size);
	grid_edit_snapshot_identity_t identity = {0};
	if (!readIdentity(&wire, &identity) || !identityMatches(&identity, expected)) {
		return snapshotError(err, err_cap, "the Grid snapshot does not match the authenticated arena and room");
	}
	forge_level_texts_t texts = {0};
	if (!readTexts(&wire, &texts)) {
		return snapshotError(err, err_cap, "the Grid snapshot has missing, malformed or trailing member data");
	}
	if (baseline) {
		if (!checkInheritedMembers(baseline, err, err_cap)) return NULL;
		for (size_t i = 0; i < sizeof(snapshot_inherited) / sizeof(snapshot_inherited[0]); i++) {
			const s32 member = snapshot_inherited[i].member;
			size_t length = 0;
			const char *text = forgeLevelDocMemberText(baseline, member, &length);
			if (!length) continue;
			texts.text[member] = text;
			texts.size[member] = length;
		}
	}

	forge_level_doc_t *doc = forgeLevelDocRead(&texts, err, err_cap);
	if (doc && received) *received = identity;
	return doc;
}

forge_level_doc_t *gridEditSnapshotDecode(const void *bytes, u32 size,
	const grid_edit_snapshot_identity_t *expected,
	const u8 expected_snapshot_sha256[SHA256_DIGEST_SIZE],
	grid_edit_snapshot_identity_t *received, char *err, size_t err_cap)
{
	return snapshotDecode(bytes, size, expected, expected_snapshot_sha256, NULL, received, err, err_cap);
}

forge_level_doc_t *gridEditSnapshotDecodeWithBaseline(const void *bytes, u32 size,
	const grid_edit_snapshot_identity_t *expected,
	const u8 expected_snapshot_sha256[SHA256_DIGEST_SIZE],
	forge_level_doc_t *baseline, grid_edit_snapshot_identity_t *received, char *err, size_t err_cap)
{
	if (!baseline) return snapshotError(err, err_cap, "the authenticated pinned Grid baseline is missing");
	return snapshotDecode(bytes, size, expected, expected_snapshot_sha256, baseline, received, err, err_cap);
}
