/**
 * theater_format.c -- explicit-field v2 .pdth codec and atomic lifecycle.
 */

#include "theater_format.h"
#include "sha256.h"

#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

static s32 theaterFormatFloatIsFinite(f32 value)
{
	return value == value && value >= -FLT_MAX && value <= FLT_MAX;
}

typedef struct theater_blob_s {
	u8 *data;
	u32 size;
	u32 capacity;
	s32 failed;
} theater_blob_t;

typedef struct theater_reader_s {
	const u8 *data;
	u32 size;
	u32 offset;
	s32 failed;
} theater_reader_t;

typedef struct theater_record_header_s {
	u16 kind;
	u16 flags;
	u32 payload_length;
	u32 sequence;
	u64 tick;
	u32 payload_crc;
	u32 header_crc;
} theater_record_header_t;

struct theater_format_writer_s {
	FILE *stream;
	char final_path[512];
	char part_path[520];
	theater_format_match_t match;
	theater_format_manifest_entry_t *manifest;
	theater_format_roster_entry_t roster[THEATER_FORMAT_MAX_ROSTER];
	u32 manifest_count;
	u32 roster_count;
	u32 record_count;
	u32 checkpoint_count;
	u32 next_sequence;
	u64 bytes_written;
	u64 last_tick;
	s32 has_tick;
	theater_format_index_entry_t *index;
	u32 index_capacity;
};

static void putU16(u8 *dst, u16 value)
{
	dst[0] = (u8)value;
	dst[1] = (u8)(value >> 8);
}

static void putU32(u8 *dst, u32 value)
{
	dst[0] = (u8)value;
	dst[1] = (u8)(value >> 8);
	dst[2] = (u8)(value >> 16);
	dst[3] = (u8)(value >> 24);
}

static void putU64(u8 *dst, u64 value)
{
	for (u32 i = 0; i < 8; i++) dst[i] = (u8)(value >> (i * 8));
}

static u16 getU16(const u8 *src)
{
	return (u16)((u16)src[0] | ((u16)src[1] << 8));
}

static u32 getU32(const u8 *src)
{
	return (u32)src[0] | ((u32)src[1] << 8) | ((u32)src[2] << 16)
		| ((u32)src[3] << 24);
}

static u64 getU64(const u8 *src)
{
	u64 value = 0;
	for (u32 i = 0; i < 8; i++) value |= (u64)src[i] << (i * 8);
	return value;
}

static u32 theaterCrc32(const void *data, u32 size)
{
	const u8 *bytes = (const u8 *)data;
	u32 crc = 0xffffffffu;
	for (u32 i = 0; i < size; i++) {
		crc ^= bytes[i];
		for (u32 bit = 0; bit < 8; bit++) {
			const u32 mask = 0u - (crc & 1u);
			crc = (crc >> 1) ^ (0xedb88320u & mask);
		}
	}
	return ~crc;
}

static u32 boundedStringLength(const char *text, u32 capacity)
{
	if (!text || capacity == 0) return capacity;
	for (u32 i = 0; i < capacity; i++) {
		if (text[i] == '\0') return i;
	}
	return capacity;
}

static s32 catalogIdValid(const char *id, s32 required)
{
	const u32 length = boundedStringLength(id, THEATER_FORMAT_ID_MAX);
	if (length >= THEATER_FORMAT_ID_MAX) return 0;
	if (required && length == 0) return 0;
	for (u32 i = 0; i < length; i++) {
		const unsigned char c = (unsigned char)id[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
				|| (c >= '0' && c <= '9') || c == ':' || c == '_'
				|| c == '-' || c == '.')) return 0;
	}
	return 1;
}

static s32 displayNameValid(const char *name)
{
	return boundedStringLength(name, THEATER_FORMAT_NAME_MAX)
		< THEATER_FORMAT_NAME_MAX;
}

static s32 printableIdentityValid(const char *value, u32 capacity,
	s32 required)
{
	const u32 length = boundedStringLength(value, capacity);
	if (length >= capacity || (required && length == 0)) return 0;
	for (u32 i = 0; i < length; i++) {
		const unsigned char c = (unsigned char)value[i];
		if (c < 0x20 || c > 0x7e) return 0;
	}
	return 1;
}

static s32 digestPresent(const u8 digest[32])
{
	for (u32 i = 0; i < 32; i++) {
		if (digest[i] != 0) return 1;
	}
	return 0;
}

static s32 matchValid(const theater_format_match_t *match, s32 require_digest)
{
	if (!match) return 0;
	if (match->mode != THEATER_FORMAT_MODE_CAMPAIGN
			&& match->mode != THEATER_FORMAT_MODE_COMBAT_SIMULATOR) return 0;
	if (match->authority != THEATER_FORMAT_AUTHORITY_OFFLINE
			&& match->authority != THEATER_FORMAT_AUTHORITY_LISTEN) return 0;
	if (match->mode == THEATER_FORMAT_MODE_CAMPAIGN) {
		if (match->campaign_variant < THEATER_FORMAT_CAMPAIGN_SOLO
				|| match->campaign_variant
					> THEATER_FORMAT_CAMPAIGN_COUNTER_OPERATIVE) return 0;
	} else if (match->campaign_variant != THEATER_FORMAT_CAMPAIGN_NONE) {
		return 0;
	}
	if (match->tick_rate == 0 || match->tick_rate > 1000
			|| match->checkpoint_interval_ticks == 0) return 0;
	if (!catalogIdValid(match->stage_id, 1)) return 0;
	if (match->mode == THEATER_FORMAT_MODE_COMBAT_SIMULATOR
			&& !catalogIdValid(match->mode_id, 1)) return 0;
	if (match->mode == THEATER_FORMAT_MODE_CAMPAIGN
			&& !catalogIdValid(match->mission_id, 1)) return 0;
	if (!catalogIdValid(match->mode_id, 0)
			|| !catalogIdValid(match->mission_id, 0)) return 0;
	for (u32 i = 0; i < THEATER_FORMAT_WEAPON_SLOTS; i++) {
		if (!catalogIdValid(match->weapon_ids[i], 0)) return 0;
	}
	if (!catalogIdValid(match->spawn_weapon_id, 0)
			|| match->reserved[0] != 0 || match->reserved[1] != 0
			|| match->reserved[2] != 0) return 0;
	if (match->mode == THEATER_FORMAT_MODE_CAMPAIGN) {
		if (match->spawn_weapon_mode != THEATER_FORMAT_SPAWN_WEAPON_NONE
				|| match->spawn_weapon_id[0]) return 0;
		for (u32 i = 0; i < THEATER_FORMAT_WEAPON_SLOTS; i++) {
			if (match->weapon_ids[i][0]) return 0;
		}
	} else if (match->spawn_weapon_mode < THEATER_FORMAT_SPAWN_WEAPON_SPECIFIC
			|| match->spawn_weapon_mode > THEATER_FORMAT_SPAWN_WEAPON_FIESTA) {
		return 0;
	}
	if (require_digest && !digestPresent(match->manifest_digest)) return 0;
	return 1;
}

static s32 rosterEntryValid(const theater_format_roster_entry_t *entry)
{
	if (!entry || entry->slot >= THEATER_FORMAT_MAX_ROSTER) return 0;
	if (entry->kind < THEATER_FORMAT_PARTICIPANT_LOCAL
			|| entry->kind > THEATER_FORMAT_PARTICIPANT_BOT) return 0;
	if (!displayNameValid(entry->name) || entry->name[0] == '\0') return 0;
	if (!catalogIdValid(entry->body_id, 1)
			|| !catalogIdValid(entry->head_id, 1)
			|| !catalogIdValid(entry->profile_id, 0)) return 0;
	if (entry->kind == THEATER_FORMAT_PARTICIPANT_BOT
			&& !catalogIdValid(entry->profile_id, 1)) return 0;
	if (entry->role > THEATER_FORMAT_ROLE_CAMPAIGN_COUNTER_OPERATIVE
			|| entry->reserved[0] != 0 || entry->reserved[1] != 0
			|| entry->reserved[2] != 0) return 0;
	return 1;
}

static s32 rosterEntryMatchesMatch(const theater_format_match_t *match,
	const theater_format_roster_entry_t *entry)
{
	if (!match || !entry) return 0;
	if (match->mode == THEATER_FORMAT_MODE_COMBAT_SIMULATOR) {
		return entry->role == THEATER_FORMAT_ROLE_NONE;
	}
	return entry->kind != THEATER_FORMAT_PARTICIPANT_BOT
		&& entry->role >= THEATER_FORMAT_ROLE_CAMPAIGN_PRIMARY
		&& entry->role <= THEATER_FORMAT_ROLE_CAMPAIGN_COUNTER_OPERATIVE;
}

static s32 rosterCompositionValid(const theater_format_match_t *match,
	u32 count, u32 primary_count, u32 cooperative_count, u32 counter_count)
{
	if (!match || count == 0) return 0;
	if (match->mode == THEATER_FORMAT_MODE_COMBAT_SIMULATOR) {
		return primary_count == 0 && cooperative_count == 0
			&& counter_count == 0;
	}
	if (primary_count != 1) return 0;
	switch (match->campaign_variant) {
	case THEATER_FORMAT_CAMPAIGN_SOLO:
		return count == 1 && cooperative_count == 0 && counter_count == 0;
	case THEATER_FORMAT_CAMPAIGN_COOPERATIVE:
		return count == 2 && cooperative_count == 1 && counter_count == 0;
	case THEATER_FORMAT_CAMPAIGN_COUNTER_OPERATIVE:
		return count == 2 && cooperative_count == 0 && counter_count == 1;
	default:
		return 0;
	}
}

static void blobFree(theater_blob_t *blob)
{
	if (!blob) return;
	free(blob->data);
	memset(blob, 0, sizeof(*blob));
}

static s32 blobReserve(theater_blob_t *blob, u32 extra)
{
	if (!blob || blob->failed) return 0;
	if (extra > THEATER_FORMAT_MAX_RECORD_PAYLOAD - blob->size) {
		blob->failed = 1;
		return 0;
	}
	const u32 needed = blob->size + extra;
	if (needed <= blob->capacity) return 1;
	u32 capacity = blob->capacity ? blob->capacity : 256u;
	while (capacity < needed) {
		if (capacity > THEATER_FORMAT_MAX_RECORD_PAYLOAD / 2u) {
			capacity = THEATER_FORMAT_MAX_RECORD_PAYLOAD;
			break;
		}
		capacity *= 2u;
	}
	u8 *grown = (u8 *)realloc(blob->data, capacity);
	if (!grown) {
		blob->failed = 1;
		return 0;
	}
	blob->data = grown;
	blob->capacity = capacity;
	return 1;
}

static void blobBytes(theater_blob_t *blob, const void *data, u32 size)
{
	if (!blobReserve(blob, size)) return;
	if (size) memcpy(blob->data + blob->size, data, size);
	blob->size += size;
}

static void blobU8(theater_blob_t *blob, u8 value)
{
	blobBytes(blob, &value, 1);
}

static void blobU16(theater_blob_t *blob, u16 value)
{
	u8 bytes[2];
	putU16(bytes, value);
	blobBytes(blob, bytes, sizeof(bytes));
}

static void blobU32(theater_blob_t *blob, u32 value)
{
	u8 bytes[4];
	putU32(bytes, value);
	blobBytes(blob, bytes, sizeof(bytes));
}

static void blobU64(theater_blob_t *blob, u64 value)
{
	u8 bytes[8];
	putU64(bytes, value);
	blobBytes(blob, bytes, sizeof(bytes));
}

static void blobS32(theater_blob_t *blob, s32 value)
{
	blobU32(blob, (u32)value);
}

static void blobF32(theater_blob_t *blob, f32 value)
{
	if (!theaterFormatFloatIsFinite(value)) {
		blob->failed = 1;
		return;
	}
	u32 bits = 0;
	memcpy(&bits, &value, sizeof(bits));
	blobU32(blob, bits);
}

static void blobString(theater_blob_t *blob, const char *text, u32 capacity)
{
	const u32 length = boundedStringLength(text, capacity);
	if (length >= capacity || length > 0xffffu) {
		blob->failed = 1;
		return;
	}
	blobU16(blob, (u16)length);
	blobBytes(blob, text, length);
}

static void readerBytes(theater_reader_t *reader, void *out, u32 size)
{
	if (!reader || reader->failed || size > reader->size - reader->offset) {
		if (reader) reader->failed = 1;
		return;
	}
	if (size && out) memcpy(out, reader->data + reader->offset, size);
	reader->offset += size;
}

static u8 readerU8(theater_reader_t *reader)
{
	u8 value = 0;
	readerBytes(reader, &value, 1);
	return value;
}

static u16 readerU16(theater_reader_t *reader)
{
	u8 bytes[2] = {0};
	readerBytes(reader, bytes, sizeof(bytes));
	return getU16(bytes);
}

static u32 readerU32(theater_reader_t *reader)
{
	u8 bytes[4] = {0};
	readerBytes(reader, bytes, sizeof(bytes));
	return getU32(bytes);
}

static f32 readerF32(theater_reader_t *reader)
{
	const u32 bits = readerU32(reader);
	f32 value = 0.0f;
	memcpy(&value, &bits, sizeof(value));
	if (!theaterFormatFloatIsFinite(value)) reader->failed = 1;
	return value;
}

static u64 readerU64(theater_reader_t *reader)
{
	u8 bytes[8] = {0};
	readerBytes(reader, bytes, sizeof(bytes));
	return getU64(bytes);
}

static void readerString(theater_reader_t *reader, char *out, u32 capacity)
{
	const u16 length = readerU16(reader);
	if (!out || capacity == 0 || length >= capacity) {
		reader->failed = 1;
		return;
	}
	readerBytes(reader, out, length);
	if (!reader->failed) out[length] = '\0';
}

static theater_format_result_t syncStream(FILE *stream)
{
	if (!stream || fflush(stream) != 0) return THEATER_FORMAT_IO_ERROR;
#ifdef _WIN32
	return _commit(_fileno(stream)) == 0
		? THEATER_FORMAT_OK : THEATER_FORMAT_IO_ERROR;
#else
	return fsync(fileno(stream)) == 0
		? THEATER_FORMAT_OK : THEATER_FORMAT_IO_ERROR;
#endif
}

static theater_format_result_t replaceFile(const char *candidate, const char *final_path)
{
#ifdef _WIN32
	return MoveFileExA(candidate, final_path,
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
		? THEATER_FORMAT_OK : THEATER_FORMAT_IO_ERROR;
#else
	return rename(candidate, final_path) == 0
		? THEATER_FORMAT_OK : THEATER_FORMAT_IO_ERROR;
#endif
}

static s32 pathExists(const char *path)
{
	FILE *stream = path ? fopen(path, "rb") : NULL;
	if (!stream) return 0;
	fclose(stream);
	return 1;
}

static theater_format_result_t truncateStream(FILE *stream, u64 size)
{
	if (!stream || size > THEATER_FORMAT_MAX_FILE_SIZE) return THEATER_FORMAT_IO_ERROR;
	if (fflush(stream) != 0) return THEATER_FORMAT_IO_ERROR;
#ifdef _WIN32
	return _chsize_s(_fileno(stream), size) == 0
		? THEATER_FORMAT_OK : THEATER_FORMAT_IO_ERROR;
#else
	return ftruncate(fileno(stream), (off_t)size) == 0
		? THEATER_FORMAT_OK : THEATER_FORMAT_IO_ERROR;
#endif
}

static theater_format_result_t fileSize(FILE *stream, u64 *out_size)
{
	if (!stream || !out_size || fseek(stream, 0, SEEK_END) != 0) {
		return THEATER_FORMAT_IO_ERROR;
	}
	const long end = ftell(stream);
	if (end < 0 || fseek(stream, 0, SEEK_SET) != 0) {
		return THEATER_FORMAT_IO_ERROR;
	}
	*out_size = (u64)(unsigned long)end;
	return *out_size > THEATER_FORMAT_MAX_FILE_SIZE
		? THEATER_FORMAT_OVERSIZED : THEATER_FORMAT_OK;
}

static void encodeHeader(u8 out[THEATER_FORMAT_HEADER_SIZE],
	const theater_format_match_t *match, u32 flags, u64 file_size,
	u32 record_count, u32 checkpoint_count, u64 index_offset,
	u32 index_length, u32 roster_count, u32 manifest_count)
{
	memset(out, 0, THEATER_FORMAT_HEADER_SIZE);
	memcpy(out, THEATER_FORMAT_MAGIC, 4);
	putU16(out + 4, THEATER_FORMAT_VERSION);
	putU16(out + 6, THEATER_FORMAT_HEADER_SIZE);
	putU32(out + 8, flags);
	out[12] = match ? match->mode : 0;
	out[13] = match ? match->authority : 0;
	putU64(out + 16, match ? match->start_time_unix : 0);
	putU64(out + 24, file_size);
	putU32(out + 32, record_count);
	putU32(out + 36, checkpoint_count);
	putU64(out + 40, index_offset);
	putU32(out + 48, index_length);
	putU16(out + 52, (u16)roster_count);
	putU16(out + 54, (u16)manifest_count);
	putU32(out + 56, theaterCrc32(out, 56));
}

static theater_format_result_t decodeHeader(const u8 bytes[THEATER_FORMAT_HEADER_SIZE],
	theater_format_info_t *info)
{
	if (memcmp(bytes, THEATER_FORMAT_MAGIC, 4) != 0) return THEATER_FORMAT_CORRUPT;
	if (getU16(bytes + 4) != THEATER_FORMAT_VERSION) {
		return THEATER_FORMAT_UNSUPPORTED_VERSION;
	}
	if (getU16(bytes + 6) != THEATER_FORMAT_HEADER_SIZE
			|| getU32(bytes + 56) != theaterCrc32(bytes, 56)
			|| bytes[14] != 0 || bytes[15] != 0
			|| getU32(bytes + 60) != 0) return THEATER_FORMAT_CORRUPT;
	if (info) {
		memset(info, 0, sizeof(*info));
		info->version = getU16(bytes + 4);
		info->flags = getU32(bytes + 8);
		info->match.mode = bytes[12];
		info->match.authority = bytes[13];
		info->match.start_time_unix = getU64(bytes + 16);
		info->file_size = getU64(bytes + 24);
		info->record_count = getU32(bytes + 32);
		info->checkpoint_count = getU32(bytes + 36);
		info->index_offset = getU64(bytes + 40);
		info->index_length = getU32(bytes + 48);
		info->roster_count = getU16(bytes + 52);
		info->manifest_count = getU16(bytes + 54);
	}
	return THEATER_FORMAT_OK;
}

static void encodeRecordHeader(u8 out[THEATER_FORMAT_RECORD_HEADER_SIZE],
	u16 kind, u16 flags, u32 payload_length, u32 sequence, u64 tick,
	u32 payload_crc)
{
	memset(out, 0, THEATER_FORMAT_RECORD_HEADER_SIZE);
	putU16(out, kind);
	putU16(out + 2, flags);
	putU16(out + 4, THEATER_FORMAT_RECORD_HEADER_SIZE);
	putU32(out + 8, payload_length);
	putU32(out + 12, sequence);
	putU64(out + 16, tick);
	putU32(out + 24, payload_crc);
	putU32(out + 28, theaterCrc32(out, 28));
}

static theater_format_result_t decodeRecordHeader(
	const u8 bytes[THEATER_FORMAT_RECORD_HEADER_SIZE],
	theater_record_header_t *header)
{
	if (getU16(bytes + 4) != THEATER_FORMAT_RECORD_HEADER_SIZE
			|| getU16(bytes + 6) != 0
			|| getU32(bytes + 28) != theaterCrc32(bytes, 28)) {
		return THEATER_FORMAT_CORRUPT;
	}
	header->kind = getU16(bytes);
	header->flags = getU16(bytes + 2);
	header->payload_length = getU32(bytes + 8);
	header->sequence = getU32(bytes + 12);
	header->tick = getU64(bytes + 16);
	header->payload_crc = getU32(bytes + 24);
	header->header_crc = getU32(bytes + 28);
	if (header->flags != 0) return THEATER_FORMAT_CORRUPT;
	if (header->payload_length > THEATER_FORMAT_MAX_RECORD_PAYLOAD) {
		return THEATER_FORMAT_OVERSIZED;
	}
	return THEATER_FORMAT_OK;
}

static theater_format_result_t writeRecord(FILE *stream, u16 kind, u16 flags,
	u32 sequence, u64 tick, const u8 *payload, u32 payload_length,
	u64 *out_offset, u32 *out_payload_crc)
{
	if (!stream || (payload_length && !payload)
			|| payload_length > THEATER_FORMAT_MAX_RECORD_PAYLOAD) {
		return THEATER_FORMAT_INVALID_ARGUMENT;
	}
	const long position = ftell(stream);
	if (position < 0) return THEATER_FORMAT_IO_ERROR;
	const u32 payload_crc = theaterCrc32(payload, payload_length);
	u8 header[THEATER_FORMAT_RECORD_HEADER_SIZE];
	encodeRecordHeader(header, kind, flags, payload_length, sequence, tick,
		payload_crc);
	if (fwrite(header, 1, sizeof(header), stream) != sizeof(header)
			|| (payload_length && fwrite(payload, 1, payload_length, stream)
				!= payload_length)) return THEATER_FORMAT_IO_ERROR;
	if (out_offset) *out_offset = (u64)(unsigned long)position;
	if (out_payload_crc) *out_payload_crc = payload_crc;
	return THEATER_FORMAT_OK;
}

static theater_format_result_t encodeMatch(const theater_format_match_t *match,
	theater_blob_t *blob)
{
	if (!matchValid(match, 1)) return THEATER_FORMAT_INVALID_IDENTITY;
	blobU8(blob, match->mode);
	blobU8(blob, match->authority);
	blobU8(blob, match->difficulty);
	blobU8(blob, match->campaign_variant);
	blobU32(blob, match->tick_rate);
	blobU32(blob, match->checkpoint_interval_ticks);
	blobU32(blob, match->options);
	blobU32(blob, match->time_limit);
	blobU32(blob, match->score_limit);
	blobU32(blob, match->team_score_limit);
	blobU64(blob, match->start_time_unix);
	blobBytes(blob, match->manifest_digest, sizeof(match->manifest_digest));
	blobString(blob, match->stage_id, THEATER_FORMAT_ID_MAX);
	blobString(blob, match->mode_id, THEATER_FORMAT_ID_MAX);
	blobString(blob, match->mission_id, THEATER_FORMAT_ID_MAX);
	for (u32 i = 0; i < THEATER_FORMAT_WEAPON_SLOTS; i++) {
		blobString(blob, match->weapon_ids[i], THEATER_FORMAT_ID_MAX);
	}
	blobString(blob, match->spawn_weapon_id, THEATER_FORMAT_ID_MAX);
	blobU8(blob, match->spawn_weapon_mode);
	blobU8(blob, 0);
	blobU8(blob, 0);
	blobU8(blob, 0);
	return blob->failed ? THEATER_FORMAT_LIMIT_EXCEEDED : THEATER_FORMAT_OK;
}

static theater_format_result_t parseMatch(const u8 *payload, u32 length,
	theater_format_match_t *out_match)
{
	theater_format_match_t candidate;
	theater_reader_t reader = { payload, length, 0, 0 };
	memset(&candidate, 0, sizeof(candidate));
	candidate.mode = readerU8(&reader);
	candidate.authority = readerU8(&reader);
	candidate.difficulty = readerU8(&reader);
	candidate.campaign_variant = readerU8(&reader);
	candidate.tick_rate = readerU32(&reader);
	candidate.checkpoint_interval_ticks = readerU32(&reader);
	candidate.options = readerU32(&reader);
	candidate.time_limit = readerU32(&reader);
	candidate.score_limit = readerU32(&reader);
	candidate.team_score_limit = readerU32(&reader);
	candidate.start_time_unix = readerU64(&reader);
	readerBytes(&reader, candidate.manifest_digest,
		sizeof(candidate.manifest_digest));
	readerString(&reader, candidate.stage_id, sizeof(candidate.stage_id));
	readerString(&reader, candidate.mode_id, sizeof(candidate.mode_id));
	readerString(&reader, candidate.mission_id, sizeof(candidate.mission_id));
	for (u32 i = 0; i < THEATER_FORMAT_WEAPON_SLOTS; i++) {
		readerString(&reader, candidate.weapon_ids[i],
			sizeof(candidate.weapon_ids[i]));
	}
	readerString(&reader, candidate.spawn_weapon_id,
		sizeof(candidate.spawn_weapon_id));
	candidate.spawn_weapon_mode = readerU8(&reader);
	candidate.reserved[0] = readerU8(&reader);
	candidate.reserved[1] = readerU8(&reader);
	candidate.reserved[2] = readerU8(&reader);
	if (reader.failed || reader.offset != reader.size || !matchValid(&candidate, 1)) {
		return THEATER_FORMAT_CORRUPT;
	}
	if (out_match) *out_match = candidate;
	return THEATER_FORMAT_OK;
}

static s32 manifestEntryValid(const theater_format_manifest_entry_t *entry)
{
	if (!entry || entry->type > THEATER_FORMAT_MANIFEST_GENERIC_ASSET
			|| entry->reserved != 0
			|| (entry->flags & ~(THEATER_FORMAT_MANIFEST_REQUIRED
				| THEATER_FORMAT_MANIFEST_HAS_DIGEST
				| THEATER_FORMAT_MANIFEST_HAS_VERSION)) != 0
			|| !(entry->flags & THEATER_FORMAT_MANIFEST_REQUIRED)
			|| !catalogIdValid(entry->catalog_id, 1)
			|| !catalogIdValid(entry->category, 1)
			|| !printableIdentityValid(entry->version_id,
				THEATER_FORMAT_VERSION_ID_MAX, 0)) return 0;
	const s32 has_digest = digestPresent(entry->content_digest);
	if (!!(entry->flags & THEATER_FORMAT_MANIFEST_HAS_DIGEST) != has_digest) {
		return 0;
	}
	if (!!(entry->flags & THEATER_FORMAT_MANIFEST_HAS_VERSION)
			!= (entry->version_id[0] != '\0')) return 0;
	if (entry->type == THEATER_FORMAT_MANIFEST_GENERIC_ASSET) {
		if (entry->slot == 0 || entry->slot > THEATER_FORMAT_CATALOG_TYPE_MAX) {
			return 0;
		}
	} else if (entry->slot != THEATER_FORMAT_MANIFEST_SLOT_MATCH
			&& entry->slot >= THEATER_FORMAT_MAX_ROSTER) return 0;
	return 1;
}

static s32 manifestEntryCompare(const theater_format_manifest_entry_t *left,
	const theater_format_manifest_entry_t *right)
{
	const int by_id = strcmp(left->catalog_id, right->catalog_id);
	if (by_id) return by_id;
	if (left->type != right->type) return left->type < right->type ? -1 : 1;
	if (left->slot != right->slot) return left->slot < right->slot ? -1 : 1;
	return 0;
}

static theater_format_result_t encodeManifest(
	const theater_format_manifest_entry_t *entries, u32 count,
	theater_blob_t *blob)
{
	if (!entries || count == 0 || count > THEATER_FORMAT_MAX_MANIFEST_ENTRIES) {
		return THEATER_FORMAT_INVALID_ARGUMENT;
	}
	blobU32(blob, count);
	for (u32 i = 0; i < count; i++) {
		const theater_format_manifest_entry_t *entry = &entries[i];
		if (!manifestEntryValid(entry)
				|| (i > 0 && manifestEntryCompare(&entries[i - 1], entry) >= 0)) {
			return THEATER_FORMAT_INVALID_IDENTITY;
		}
		blobU16(blob, entry->type);
		blobU16(blob, entry->slot);
		blobU16(blob, entry->flags);
		blobU16(blob, 0);
		blobBytes(blob, entry->content_digest, sizeof(entry->content_digest));
		blobString(blob, entry->catalog_id, THEATER_FORMAT_ID_MAX);
		blobString(blob, entry->category, THEATER_FORMAT_CATEGORY_MAX);
		blobString(blob, entry->version_id, THEATER_FORMAT_VERSION_ID_MAX);
	}
	return blob->failed ? THEATER_FORMAT_LIMIT_EXCEEDED : THEATER_FORMAT_OK;
}

static theater_format_result_t parseManifest(const u8 *payload, u32 length,
	theater_format_manifest_entry_t **out_entries, u32 *out_count)
{
	theater_reader_t reader = { payload, length, 0, 0 };
	const u32 count = readerU32(&reader);
	if (count == 0 || count > THEATER_FORMAT_MAX_MANIFEST_ENTRIES) {
		return THEATER_FORMAT_CORRUPT;
	}
	theater_format_manifest_entry_t *entries =
		(theater_format_manifest_entry_t *)calloc(count, sizeof(*entries));
	if (!entries) return THEATER_FORMAT_IO_ERROR;
	for (u32 i = 0; i < count; i++) {
		theater_format_manifest_entry_t *entry = &entries[i];
		entry->type = readerU16(&reader);
		entry->slot = readerU16(&reader);
		entry->flags = readerU16(&reader);
		entry->reserved = readerU16(&reader);
		readerBytes(&reader, entry->content_digest,
			sizeof(entry->content_digest));
		readerString(&reader, entry->catalog_id, sizeof(entry->catalog_id));
		readerString(&reader, entry->category, sizeof(entry->category));
		readerString(&reader, entry->version_id, sizeof(entry->version_id));
		if (reader.failed || !manifestEntryValid(entry)
				|| (i > 0 && manifestEntryCompare(&entries[i - 1], entry) >= 0)) {
			free(entries);
			return THEATER_FORMAT_CORRUPT;
		}
	}
	if (reader.failed || reader.offset != reader.size) {
		free(entries);
		return THEATER_FORMAT_CORRUPT;
	}
	*out_entries = entries;
	*out_count = count;
	return THEATER_FORMAT_OK;
}

static s32 manifestContainsCompatibleType(
	const theater_format_manifest_entry_t *entries, u32 count, const char *id,
	u16 first_type, u16 second_type)
{
	s32 found = 0;
	for (u32 i = 0; i < count; i++) {
		if (strcmp(entries[i].catalog_id, id) != 0) continue;
		if (entries[i].type != first_type && entries[i].type != second_type) {
			return 0;
		}
		found = 1;
	}
	return found;
}

static s32 manifestContainsTypedId(
	const theater_format_manifest_entry_t *entries, u32 count, const char *id,
	u16 type, u16 slot)
{
	for (u32 i = 0; i < count; i++) {
		if (entries[i].type == type && entries[i].slot == slot
				&& strcmp(entries[i].catalog_id, id) == 0) return 1;
	}
	return 0;
}

static s32 manifestContainsMatchWeapons(
	const theater_format_manifest_entry_t *entries, u32 count,
	const theater_format_match_t *match)
{
	if (!match) return 0;
	for (u32 i = 0; i < THEATER_FORMAT_WEAPON_SLOTS; i++) {
		if (match->weapon_ids[i][0]
				&& !manifestContainsTypedId(entries, count,
					match->weapon_ids[i], THEATER_FORMAT_MANIFEST_WEAPON,
					THEATER_FORMAT_MANIFEST_SLOT_MATCH)) return 0;
	}
	return !match->spawn_weapon_id[0]
		|| manifestContainsTypedId(entries, count, match->spawn_weapon_id,
			THEATER_FORMAT_MANIFEST_WEAPON,
			THEATER_FORMAT_MANIFEST_SLOT_MATCH);
}

static theater_format_result_t encodeRoster(
	const theater_format_roster_entry_t *roster, u32 count,
	const theater_format_match_t *match, theater_blob_t *blob)
{
	if (!roster || count == 0 || count > THEATER_FORMAT_MAX_ROSTER) {
		return THEATER_FORMAT_INVALID_ARGUMENT;
	}
	blobU32(blob, count);
	u64 occupied = 0;
	u32 primary_count = 0;
	u32 cooperative_count = 0;
	u32 counter_count = 0;
	for (u32 i = 0; i < count; i++) {
		if (!rosterEntryValid(&roster[i])
				|| !rosterEntryMatchesMatch(match, &roster[i])) {
			return THEATER_FORMAT_INVALID_IDENTITY;
		}
		if (i > 0 && roster[i - 1].slot >= roster[i].slot) {
			return THEATER_FORMAT_INVALID_IDENTITY;
		}
		const u64 bit = (u64)1 << roster[i].slot;
		if (occupied & bit) return THEATER_FORMAT_INVALID_IDENTITY;
		occupied |= bit;
		blobU16(blob, roster[i].slot);
		blobU8(blob, roster[i].kind);
		blobU8(blob, roster[i].team);
		blobU8(blob, roster[i].role);
		blobU8(blob, 0);
		blobU8(blob, 0);
		blobU8(blob, 0);
		blobString(blob, roster[i].name, THEATER_FORMAT_NAME_MAX);
		blobString(blob, roster[i].body_id, THEATER_FORMAT_ID_MAX);
		blobString(blob, roster[i].head_id, THEATER_FORMAT_ID_MAX);
		blobString(blob, roster[i].profile_id, THEATER_FORMAT_ID_MAX);
		primary_count += roster[i].role == THEATER_FORMAT_ROLE_CAMPAIGN_PRIMARY;
		cooperative_count += roster[i].role
			== THEATER_FORMAT_ROLE_CAMPAIGN_COOPERATIVE;
		counter_count += roster[i].role
			== THEATER_FORMAT_ROLE_CAMPAIGN_COUNTER_OPERATIVE;
	}
	if (!rosterCompositionValid(match, count, primary_count,
			cooperative_count, counter_count)) return THEATER_FORMAT_INVALID_IDENTITY;
	return blob->failed ? THEATER_FORMAT_LIMIT_EXCEEDED : THEATER_FORMAT_OK;
}

static theater_format_result_t parseRoster(const u8 *payload, u32 length,
	u32 *out_count, const theater_format_manifest_entry_t *manifest,
	u32 manifest_count, theater_format_roster_entry_t *out_roster,
	const theater_format_match_t *match)
{
	theater_reader_t reader = { payload, length, 0, 0 };
	const u32 count = readerU32(&reader);
	if (count == 0 || count > THEATER_FORMAT_MAX_ROSTER) {
		return THEATER_FORMAT_CORRUPT;
	}
	u64 occupied = 0;
	u16 previous_slot = 0;
	u32 primary_count = 0;
	u32 cooperative_count = 0;
	u32 counter_count = 0;
	for (u32 i = 0; i < count; i++) {
		theater_format_roster_entry_t entry;
		memset(&entry, 0, sizeof(entry));
		entry.slot = readerU16(&reader);
		entry.kind = readerU8(&reader);
		entry.team = readerU8(&reader);
		entry.role = readerU8(&reader);
		entry.reserved[0] = readerU8(&reader);
		entry.reserved[1] = readerU8(&reader);
		entry.reserved[2] = readerU8(&reader);
		readerString(&reader, entry.name, sizeof(entry.name));
		readerString(&reader, entry.body_id, sizeof(entry.body_id));
		readerString(&reader, entry.head_id, sizeof(entry.head_id));
		readerString(&reader, entry.profile_id, sizeof(entry.profile_id));
		if (reader.failed || !rosterEntryValid(&entry)
				|| !rosterEntryMatchesMatch(match, &entry)
				|| (i > 0 && previous_slot >= entry.slot)) {
			return THEATER_FORMAT_CORRUPT;
		}
		if (!manifestContainsTypedId(manifest, manifest_count, entry.body_id,
				THEATER_FORMAT_MANIFEST_BODY, entry.slot)
				|| !manifestContainsTypedId(manifest, manifest_count, entry.head_id,
					THEATER_FORMAT_MANIFEST_HEAD, entry.slot)
				|| (entry.profile_id[0]
					&& !manifestContainsTypedId(manifest, manifest_count,
						entry.profile_id,
						THEATER_FORMAT_MANIFEST_GENERIC_ASSET,
						THEATER_FORMAT_CATALOG_TYPE_BOT_PROFILE))) {
			return THEATER_FORMAT_CORRUPT;
		}
		const u64 bit = (u64)1 << entry.slot;
		if (occupied & bit) return THEATER_FORMAT_CORRUPT;
		occupied |= bit;
		previous_slot = entry.slot;
		if (out_roster) out_roster[i] = entry;
		primary_count += entry.role == THEATER_FORMAT_ROLE_CAMPAIGN_PRIMARY;
		cooperative_count += entry.role
			== THEATER_FORMAT_ROLE_CAMPAIGN_COOPERATIVE;
		counter_count += entry.role
			== THEATER_FORMAT_ROLE_CAMPAIGN_COUNTER_OPERATIVE;
	}
	if (reader.failed || reader.offset != reader.size) return THEATER_FORMAT_CORRUPT;
	if (!rosterCompositionValid(match, count, primary_count,
			cooperative_count, counter_count)) return THEATER_FORMAT_CORRUPT;
	if (out_count) *out_count = count;
	return THEATER_FORMAT_OK;
}

static s32 entityIdentityValid(const theater_format_entity_t *entity)
{
	f32 orientation_length = 0.0f;
	if (entity) {
		for (u32 i = 0; i < 9; i++) {
			if (!theaterFormatFloatIsFinite(entity->orientation[i])) return 0;
			orientation_length += entity->orientation[i] * entity->orientation[i];
		}
	}
	return entity && entity->stable_id != 0
		&& entity->kind >= THEATER_FORMAT_ENTITY_PLAYER
		&& entity->kind <= THEATER_FORMAT_ENTITY_PROJECTILE
		&& (entity->state_flags & ~THEATER_FORMAT_ENTITY_STATE_DEAD) == 0
		&& (!(entity->state_flags & THEATER_FORMAT_ENTITY_STATE_DEAD)
			|| entity->kind == THEATER_FORMAT_ENTITY_PLAYER
			|| entity->kind == THEATER_FORMAT_ENTITY_BOT
			|| entity->kind == THEATER_FORMAT_ENTITY_NPC)
		&& entity->reserved[0] == 0 && entity->reserved[1] == 0
		&& entity->reserved[2] == 0
		&& entity->door_motion <= THEATER_FORMAT_DOOR_WAITING
		&& ((entity->kind == THEATER_FORMAT_ENTITY_DOOR)
			? entity->door_motion != THEATER_FORMAT_DOOR_NONE
				&& entity->door_fraction >= 0.0f
				&& entity->door_fraction <= 1.0f
			: entity->door_motion == THEATER_FORMAT_DOOR_NONE
				&& entity->door_fraction == 0.0f
				&& entity->door_speed == 0.0f)
		&& (entity->kind == THEATER_FORMAT_ENTITY_PLAYER
			|| (entity->score == 0 && entity->deaths == 0))
		&& catalogIdValid(entity->asset_id, 1)
		&& catalogIdValid(entity->secondary_asset_id, 0)
		&& theaterFormatFloatIsFinite(entity->health)
		&& theaterFormatFloatIsFinite(entity->position[0])
		&& theaterFormatFloatIsFinite(entity->position[1])
		&& theaterFormatFloatIsFinite(entity->position[2])
		&& theaterFormatFloatIsFinite(orientation_length)
		&& orientation_length >= 0.75f && orientation_length <= 12.0f
		&& theaterFormatFloatIsFinite(entity->velocity[0])
		&& theaterFormatFloatIsFinite(entity->velocity[1])
		&& theaterFormatFloatIsFinite(entity->velocity[2])
		&& theaterFormatFloatIsFinite(entity->door_fraction)
		&& theaterFormatFloatIsFinite(entity->door_speed);
}

static s32 entityManifestIdentityValid(
	const theater_format_manifest_entry_t *manifest, u32 manifest_count,
	const theater_format_entity_t *entity)
{
	u16 primary_type = THEATER_FORMAT_MANIFEST_MODEL;
	u16 secondary_type = THEATER_FORMAT_MANIFEST_HEAD;
	u16 alternate_secondary_type = THEATER_FORMAT_MANIFEST_HEAD;
	s32 secondary_allowed = 0;
	switch (entity->kind) {
	case THEATER_FORMAT_ENTITY_PLAYER:
		primary_type = THEATER_FORMAT_MANIFEST_BODY;
		secondary_allowed = 1;
		alternate_secondary_type = THEATER_FORMAT_MANIFEST_WEAPON;
		break;
	case THEATER_FORMAT_ENTITY_BOT:
	case THEATER_FORMAT_ENTITY_NPC:
		primary_type = THEATER_FORMAT_MANIFEST_BODY;
		secondary_allowed = 1;
		break;
	case THEATER_FORMAT_ENTITY_WEAPON:
	case THEATER_FORMAT_ENTITY_PROJECTILE:
		primary_type = THEATER_FORMAT_MANIFEST_WEAPON;
		break;
	case THEATER_FORMAT_ENTITY_OBJECT:
	case THEATER_FORMAT_ENTITY_DOOR:
	case THEATER_FORMAT_ENTITY_LIFT:
		break;
	default:
		return 0;
	}
	if (!manifestContainsCompatibleType(manifest, manifest_count,
			entity->asset_id, primary_type, primary_type)) return 0;
	if (!entity->secondary_asset_id[0]) return 1;
	return secondary_allowed
		&& manifestContainsCompatibleType(manifest, manifest_count,
			entity->secondary_asset_id, secondary_type,
			alternate_secondary_type);
}

static s32 sortedEntityIdPresent(const theater_format_entity_t *entities,
	u32 count, u32 stable_id);

static const theater_format_roster_entry_t *rosterFindSlot(
	const theater_format_roster_entry_t *roster, u32 count, u16 slot)
{
	if (!roster || count == 0 || count > THEATER_FORMAT_MAX_ROSTER) return NULL;
	for (u32 i = 0; i < count; i++) {
		if (roster[i].slot == slot) return &roster[i];
	}
	return NULL;
}

static u32 rosterViewCount(const theater_format_roster_entry_t *roster,
	u32 count)
{
	u32 view_count = 0;
	for (u32 i = 0; roster && i < count; i++) {
		view_count += roster[i].kind != THEATER_FORMAT_PARTICIPANT_BOT;
	}
	return view_count;
}

static s32 viewValid(const theater_format_view_t *view,
	const theater_format_entity_t *entities, u32 entity_count,
	const theater_format_roster_entry_t *roster, u32 roster_count)
{
	const theater_format_roster_entry_t *participant = view
		? rosterFindSlot(roster, roster_count, view->slot) : NULL;
	if (!view || view->slot >= THEATER_FORMAT_MAX_VIEWS
			|| !participant || participant->kind == THEATER_FORMAT_PARTICIPANT_BOT
			|| (!!(view->flags & THEATER_FORMAT_VIEW_LOCAL)
				!= (participant->kind == THEATER_FORMAT_PARTICIPANT_LOCAL))
			|| !(view->flags & THEATER_FORMAT_VIEW_ACTIVE)
			|| (view->flags & ~(THEATER_FORMAT_VIEW_ACTIVE
				| THEATER_FORMAT_VIEW_LOCAL
				| THEATER_FORMAT_VIEW_DERIVED_ORIENTATION)) != 0
			|| view->camera_mode < THEATER_FORMAT_CAMERA_FIRST_PERSON
			|| view->camera_mode > THEATER_FORMAT_CAMERA_EYESPY
			|| !sortedEntityIdPresent(entities, entity_count, view->entity_id)
			|| !theaterFormatFloatIsFinite(view->fov_y_degrees)
			|| view->fov_y_degrees <= 1.0f || view->fov_y_degrees >= 179.0f
			|| !theaterFormatFloatIsFinite(view->aspect) || view->aspect <= 0.1f
			|| view->aspect >= 10.0f
			|| !theaterFormatFloatIsFinite(view->aim_yaw_degrees)
			|| !theaterFormatFloatIsFinite(view->aim_pitch_degrees)) return 0;
	f32 forward_length = 0.0f;
	f32 up_length = 0.0f;
	for (u32 axis = 0; axis < 3; axis++) {
		if (!theaterFormatFloatIsFinite(view->position[axis])
				|| !theaterFormatFloatIsFinite(view->forward[axis])
				|| !theaterFormatFloatIsFinite(view->up[axis])) return 0;
		forward_length += view->forward[axis] * view->forward[axis];
		up_length += view->up[axis] * view->up[axis];
	}
	return forward_length > 0.0001f && up_length > 0.0001f;
}

static s32 sortedEntityIdPresent(const theater_format_entity_t *entities,
	u32 count, u32 stable_id)
{
	u32 low = 0;
	u32 high = count;
	while (low < high) {
		const u32 middle = low + (high - low) / 2u;
		if (entities[middle].stable_id < stable_id) low = middle + 1u;
		else high = middle;
	}
	return low < count && entities[low].stable_id == stable_id;
}

static theater_format_result_t encodeCheckpoint(
	const theater_format_checkpoint_t *checkpoint, theater_blob_t *blob,
	const theater_format_manifest_entry_t *manifest, u32 manifest_count,
	const theater_format_roster_entry_t *roster, u32 roster_count)
{
	if (!checkpoint || checkpoint->entity_count == 0
			|| checkpoint->entity_count > THEATER_FORMAT_MAX_ENTITIES
			|| !checkpoint->entities || checkpoint->view_count == 0
			|| checkpoint->view_count > THEATER_FORMAT_MAX_VIEWS
			|| !checkpoint->views) {
		return THEATER_FORMAT_INVALID_ARGUMENT;
	}
	if (checkpoint->view_count != rosterViewCount(roster, roster_count)) {
		return THEATER_FORMAT_INVALID_IDENTITY;
	}
	blobU64(blob, checkpoint->elapsed_ms);
	blobU64(blob, checkpoint->stage_flags);
	blobU64(blob, checkpoint->objective_flags);
	blobU32(blob, checkpoint->match_elapsed_ticks);
	blobU32(blob, checkpoint->entity_count);
	blobU32(blob, checkpoint->view_count);
	for (u32 i = 0; i < checkpoint->entity_count; i++) {
		const theater_format_entity_t *entity = &checkpoint->entities[i];
		if (!entityIdentityValid(entity)
				|| (i > 0 && checkpoint->entities[i - 1].stable_id
					>= entity->stable_id)
				|| entity->parent_id == entity->stable_id
				|| (entity->parent_id != 0
					&& !sortedEntityIdPresent(checkpoint->entities,
						checkpoint->entity_count, entity->parent_id))) {
			return THEATER_FORMAT_INVALID_IDENTITY;
		}
		if (!entityManifestIdentityValid(manifest, manifest_count, entity)) {
			return THEATER_FORMAT_INVALID_IDENTITY;
		}
		blobU32(blob, entity->stable_id);
		blobU32(blob, entity->parent_id);
		blobU16(blob, entity->kind);
		blobU16(blob, entity->state_flags);
		blobS32(blob, entity->room);
		blobF32(blob, entity->health);
		for (u32 axis = 0; axis < 3; axis++) blobF32(blob, entity->position[axis]);
		for (u32 value = 0; value < 9; value++) blobF32(blob, entity->orientation[value]);
		for (u32 axis = 0; axis < 3; axis++) blobF32(blob, entity->velocity[axis]);
		blobS32(blob, entity->score);
		blobS32(blob, entity->deaths);
		blobF32(blob, entity->door_fraction);
		blobF32(blob, entity->door_speed);
		blobU8(blob, entity->door_motion);
		blobU8(blob, 0);
		blobU8(blob, 0);
		blobU8(blob, 0);
		blobString(blob, entity->asset_id, THEATER_FORMAT_ID_MAX);
		blobString(blob, entity->secondary_asset_id, THEATER_FORMAT_ID_MAX);
	}
	for (u32 i = 0; i < checkpoint->view_count; i++) {
		const theater_format_view_t *view = &checkpoint->views[i];
		if (!viewValid(view, checkpoint->entities, checkpoint->entity_count,
				roster, roster_count)
				|| (i > 0 && checkpoint->views[i - 1].slot >= view->slot)) {
			return THEATER_FORMAT_INVALID_IDENTITY;
		}
		blobU16(blob, view->slot);
		blobU16(blob, view->flags);
		blobU32(blob, view->entity_id);
		blobS32(blob, view->camera_mode);
		for (u32 axis = 0; axis < 3; axis++) blobF32(blob, view->position[axis]);
		for (u32 axis = 0; axis < 3; axis++) blobF32(blob, view->forward[axis]);
		for (u32 axis = 0; axis < 3; axis++) blobF32(blob, view->up[axis]);
		blobF32(blob, view->fov_y_degrees);
		blobF32(blob, view->aspect);
		blobF32(blob, view->aim_yaw_degrees);
		blobF32(blob, view->aim_pitch_degrees);
	}
	return blob->failed ? THEATER_FORMAT_LIMIT_EXCEEDED : THEATER_FORMAT_OK;
}

static theater_format_result_t parseCheckpoint(const u8 *payload, u32 length,
	u64 expected_tick, theater_format_checkpoint_t *out_checkpoint,
	theater_format_entity_t **out_entities, theater_format_view_t **out_views,
	const theater_format_manifest_entry_t *manifest, u32 manifest_count,
	const theater_format_roster_entry_t *roster, u32 roster_count)
{
	theater_reader_t reader = { payload, length, 0, 0 };
	theater_format_checkpoint_t candidate;
	memset(&candidate, 0, sizeof(candidate));
	candidate.tick = expected_tick;
	candidate.elapsed_ms = readerU64(&reader);
	candidate.stage_flags = readerU64(&reader);
	candidate.objective_flags = readerU64(&reader);
	candidate.match_elapsed_ticks = readerU32(&reader);
	const u32 count = readerU32(&reader);
	const u32 view_count = readerU32(&reader);
	if (reader.failed || count == 0 || count > THEATER_FORMAT_MAX_ENTITIES
			|| view_count == 0 || view_count > THEATER_FORMAT_MAX_VIEWS
			|| view_count != rosterViewCount(roster, roster_count)) {
		return THEATER_FORMAT_CORRUPT;
	}
	theater_format_entity_t *entities =
		(theater_format_entity_t *)calloc(count, sizeof(*entities));
	theater_format_view_t *views =
		(theater_format_view_t *)calloc(view_count, sizeof(*views));
	u32 *parents = (u32 *)calloc(count, sizeof(u32));
	if (!entities || !views || !parents) {
		free(entities);
		free(views);
		free(parents);
		return THEATER_FORMAT_IO_ERROR;
	}
	u32 last_id = 0;
	for (u32 i = 0; i < count; i++) {
		theater_format_entity_t entity;
		memset(&entity, 0, sizeof(entity));
		entity.stable_id = readerU32(&reader);
		entity.parent_id = readerU32(&reader);
		entity.kind = readerU16(&reader);
		entity.state_flags = readerU16(&reader);
		entity.room = (s32)readerU32(&reader);
		entity.health = readerF32(&reader);
		for (u32 axis = 0; axis < 3; axis++) entity.position[axis] = readerF32(&reader);
		for (u32 value = 0; value < 9; value++) entity.orientation[value] = readerF32(&reader);
		for (u32 axis = 0; axis < 3; axis++) entity.velocity[axis] = readerF32(&reader);
		entity.score = (s32)readerU32(&reader);
		entity.deaths = (s32)readerU32(&reader);
		entity.door_fraction = readerF32(&reader);
		entity.door_speed = readerF32(&reader);
		entity.door_motion = readerU8(&reader);
		entity.reserved[0] = readerU8(&reader);
		entity.reserved[1] = readerU8(&reader);
		entity.reserved[2] = readerU8(&reader);
		readerString(&reader, entity.asset_id, sizeof(entity.asset_id));
		readerString(&reader, entity.secondary_asset_id,
			sizeof(entity.secondary_asset_id));
		if (reader.failed || entity.stable_id <= last_id
				|| !entityIdentityValid(&entity)
				|| entity.parent_id == entity.stable_id
				|| !entityManifestIdentityValid(manifest, manifest_count,
					&entity)) {
			free(entities);
			free(views);
			free(parents);
			return THEATER_FORMAT_CORRUPT;
		}
		entities[i] = entity;
		parents[i] = entity.parent_id;
		last_id = entity.stable_id;
	}
	for (u32 i = 0; i < count; i++) {
		if (!parents[i]) continue;
		u32 low = 0;
		u32 high = count;
		while (low < high) {
			const u32 middle = low + (high - low) / 2u;
			if (entities[middle].stable_id < parents[i]) low = middle + 1u;
			else high = middle;
		}
		if (low >= count || entities[low].stable_id != parents[i]) {
			free(entities);
			free(views);
			free(parents);
			return THEATER_FORMAT_CORRUPT;
		}
	}
	for (u32 i = 0; i < view_count; i++) {
		theater_format_view_t *view = &views[i];
		view->slot = readerU16(&reader);
		view->flags = readerU16(&reader);
		view->entity_id = readerU32(&reader);
		view->camera_mode = (s32)readerU32(&reader);
		for (u32 axis = 0; axis < 3; axis++) view->position[axis] = readerF32(&reader);
		for (u32 axis = 0; axis < 3; axis++) view->forward[axis] = readerF32(&reader);
		for (u32 axis = 0; axis < 3; axis++) view->up[axis] = readerF32(&reader);
		view->fov_y_degrees = readerF32(&reader);
		view->aspect = readerF32(&reader);
		view->aim_yaw_degrees = readerF32(&reader);
		view->aim_pitch_degrees = readerF32(&reader);
		if (reader.failed || !viewValid(view, entities, count, roster, roster_count)
				|| (i > 0 && views[i - 1].slot >= view->slot)) {
			free(entities);
			free(views);
			free(parents);
			return THEATER_FORMAT_CORRUPT;
		}
	}
	free(parents);
	if (reader.failed || reader.offset != reader.size) {
		free(entities);
		free(views);
		return THEATER_FORMAT_CORRUPT;
	}
	if (out_checkpoint && out_entities && out_views) {
		candidate.entity_count = count;
		candidate.entities = entities;
		candidate.view_count = view_count;
		candidate.views = views;
		*out_checkpoint = candidate;
		*out_entities = entities;
		*out_views = views;
	} else {
		free(entities);
		free(views);
	}
	return THEATER_FORMAT_OK;
}

static theater_format_result_t encodeEvent(const theater_format_event_t *event,
	theater_blob_t *blob)
{
	if (!event || event->kind < THEATER_FORMAT_EVENT_STAGE_BEGIN
			|| event->kind > THEATER_FORMAT_EVENT_STAGE_FLAGS
			|| event->reserved != 0
			|| !catalogIdValid(event->catalog_id, 0)) {
		return THEATER_FORMAT_INVALID_ARGUMENT;
	}
	blobU16(blob, event->kind);
	blobU16(blob, 0);
	blobU32(blob, event->actor_id);
	blobU32(blob, event->target_id);
	for (u32 i = 0; i < 4; i++) blobS32(blob, event->value[i]);
	blobString(blob, event->catalog_id, THEATER_FORMAT_ID_MAX);
	return blob->failed ? THEATER_FORMAT_LIMIT_EXCEEDED : THEATER_FORMAT_OK;
}

static theater_format_result_t parseEvent(const u8 *payload, u32 length)
{
	theater_reader_t reader = { payload, length, 0, 0 };
	const u16 kind = readerU16(&reader);
	const u16 reserved = readerU16(&reader);
	(void)readerU32(&reader);
	(void)readerU32(&reader);
	for (u32 i = 0; i < 4; i++) (void)readerU32(&reader);
	char id[THEATER_FORMAT_ID_MAX] = {0};
	readerString(&reader, id, sizeof(id));
	if (reader.failed || reader.offset != reader.size
			|| reserved != 0
			|| kind < THEATER_FORMAT_EVENT_STAGE_BEGIN
			|| kind > THEATER_FORMAT_EVENT_STAGE_FLAGS
			|| !catalogIdValid(id, 0)) return THEATER_FORMAT_CORRUPT;
	return THEATER_FORMAT_OK;
}

static theater_format_result_t ensureIndexCapacity(theater_format_writer_t *writer)
{
	if (writer->checkpoint_count >= THEATER_FORMAT_MAX_CHECKPOINTS) {
		return THEATER_FORMAT_LIMIT_EXCEEDED;
	}
	if (writer->checkpoint_count < writer->index_capacity) return THEATER_FORMAT_OK;
	u32 capacity = writer->index_capacity ? writer->index_capacity * 2u : 64u;
	if (capacity > THEATER_FORMAT_MAX_CHECKPOINTS) capacity = THEATER_FORMAT_MAX_CHECKPOINTS;
	void *grown = realloc(writer->index,
		(size_t)capacity * sizeof(theater_format_index_entry_t));
	if (!grown) return THEATER_FORMAT_IO_ERROR;
	writer->index = (theater_format_index_entry_t *)grown;
	writer->index_capacity = capacity;
	return THEATER_FORMAT_OK;
}

static theater_format_result_t writerAppendBlob(theater_format_writer_t *writer,
	u16 kind, u64 tick, const theater_blob_t *blob, u64 *out_offset,
	u32 *out_crc)
{
	if (!writer || !writer->stream || !blob || blob->failed) {
		return THEATER_FORMAT_INVALID_ARGUMENT;
	}
	if (writer->record_count >= THEATER_FORMAT_MAX_RECORDS) {
		return THEATER_FORMAT_LIMIT_EXCEEDED;
	}
	if (writer->has_tick && tick < writer->last_tick) {
		return THEATER_FORMAT_INVALID_ARGUMENT;
	}
	const u64 projected = writer->bytes_written
		+ THEATER_FORMAT_RECORD_HEADER_SIZE + blob->size;
	if (projected > THEATER_FORMAT_MAX_FILE_SIZE) return THEATER_FORMAT_OVERSIZED;
	theater_format_result_t result = writeRecord(writer->stream, kind, 0,
		writer->next_sequence, tick, blob->data, blob->size,
		out_offset, out_crc);
	if (result != THEATER_FORMAT_OK) return result;
	writer->next_sequence++;
	writer->record_count++;
	writer->bytes_written = projected;
	writer->last_tick = tick;
	writer->has_tick = 1;
	return THEATER_FORMAT_OK;
}

theater_format_writer_t *theaterFormatBegin(const char *final_path,
	const theater_format_match_t *match,
	const theater_format_manifest_entry_t *manifest, u32 manifest_count,
	const theater_format_roster_entry_t *roster, u32 roster_count,
	theater_format_result_t *out_result)
{
	theater_format_result_t result = THEATER_FORMAT_INVALID_ARGUMENT;
	if (out_result) *out_result = result;
	if (!final_path || !final_path[0] || strlen(final_path) >= 512
			|| !matchValid(match, 0) || !manifest || manifest_count == 0
			|| manifest_count > THEATER_FORMAT_MAX_MANIFEST_ENTRIES
			|| !roster || roster_count == 0
			|| roster_count > THEATER_FORMAT_MAX_ROSTER) {
		if (out_result && match && !matchValid(match, 0)) {
			*out_result = THEATER_FORMAT_INVALID_IDENTITY;
		}
		return NULL;
	}

	theater_blob_t manifest_blob = {0};
	theater_blob_t match_blob = {0};
	theater_blob_t roster_blob = {0};
	result = encodeManifest(manifest, manifest_count, &manifest_blob);
	theater_format_match_t candidate_match = *match;
	if (result == THEATER_FORMAT_OK) {
		u8 derived_digest[32];
		sha256Hash(manifest_blob.data, manifest_blob.size, derived_digest);
		if (digestPresent(match->manifest_digest)
				&& memcmp(match->manifest_digest, derived_digest,
					sizeof(derived_digest)) != 0) {
			result = THEATER_FORMAT_INVALID_IDENTITY;
		} else {
			memcpy(candidate_match.manifest_digest, derived_digest,
				sizeof(derived_digest));
		}
	}
	if (result == THEATER_FORMAT_OK
			&& !manifestContainsTypedId(manifest, manifest_count,
				candidate_match.stage_id, THEATER_FORMAT_MANIFEST_STAGE,
				THEATER_FORMAT_MANIFEST_SLOT_MATCH)) {
		result = THEATER_FORMAT_INVALID_IDENTITY;
	}
	if (result == THEATER_FORMAT_OK
			&& candidate_match.mode == THEATER_FORMAT_MODE_COMBAT_SIMULATOR
			&& !manifestContainsTypedId(manifest, manifest_count,
				candidate_match.mode_id,
				THEATER_FORMAT_MANIFEST_GENERIC_ASSET,
				THEATER_FORMAT_CATALOG_TYPE_GAMEMODE)) {
		result = THEATER_FORMAT_INVALID_IDENTITY;
	}
	if (result == THEATER_FORMAT_OK
			&& candidate_match.mode == THEATER_FORMAT_MODE_CAMPAIGN
			&& !manifestContainsTypedId(manifest, manifest_count,
				candidate_match.mission_id,
				THEATER_FORMAT_MANIFEST_GENERIC_ASSET,
				THEATER_FORMAT_CATALOG_TYPE_MISSION)) {
		result = THEATER_FORMAT_INVALID_IDENTITY;
	}
	if (result == THEATER_FORMAT_OK
			&& !manifestContainsMatchWeapons(manifest, manifest_count,
				&candidate_match)) {
		result = THEATER_FORMAT_INVALID_IDENTITY;
	}
	for (u32 i = 0; result == THEATER_FORMAT_OK && i < roster_count; i++) {
		if (!manifestContainsTypedId(manifest, manifest_count,
				roster[i].body_id, THEATER_FORMAT_MANIFEST_BODY,
				roster[i].slot)
				|| !manifestContainsTypedId(manifest, manifest_count,
					roster[i].head_id, THEATER_FORMAT_MANIFEST_HEAD,
					roster[i].slot)
				|| (roster[i].profile_id[0]
					&& !manifestContainsTypedId(manifest, manifest_count,
						roster[i].profile_id,
						THEATER_FORMAT_MANIFEST_GENERIC_ASSET,
						THEATER_FORMAT_CATALOG_TYPE_BOT_PROFILE))) {
			result = THEATER_FORMAT_INVALID_IDENTITY;
		}
	}
	if (result == THEATER_FORMAT_OK) result = encodeMatch(&candidate_match, &match_blob);
	if (result == THEATER_FORMAT_OK) {
		result = encodeRoster(roster, roster_count, &candidate_match,
			&roster_blob);
	}
	if (result != THEATER_FORMAT_OK) {
		blobFree(&manifest_blob);
		blobFree(&match_blob);
		blobFree(&roster_blob);
		if (out_result) *out_result = result;
		return NULL;
	}

	theater_format_writer_t *writer =
		(theater_format_writer_t *)calloc(1, sizeof(*writer));
	if (!writer) {
		blobFree(&manifest_blob);
		blobFree(&match_blob);
		blobFree(&roster_blob);
		if (out_result) *out_result = THEATER_FORMAT_IO_ERROR;
		return NULL;
	}
	strncpy(writer->final_path, final_path, sizeof(writer->final_path) - 1);
	snprintf(writer->part_path, sizeof(writer->part_path), "%s.part", final_path);
	if (pathExists(writer->final_path) || pathExists(writer->part_path)) {
		free(writer);
		blobFree(&manifest_blob);
		blobFree(&match_blob);
		blobFree(&roster_blob);
		if (out_result) *out_result = THEATER_FORMAT_IO_ERROR;
		return NULL;
	}
	writer->match = candidate_match;
	writer->manifest_count = manifest_count;
	writer->manifest = (theater_format_manifest_entry_t *)malloc(
		(size_t)manifest_count * sizeof(*writer->manifest));
	if (!writer->manifest) {
		free(writer);
		blobFree(&manifest_blob);
		blobFree(&match_blob);
		blobFree(&roster_blob);
		if (out_result) *out_result = THEATER_FORMAT_IO_ERROR;
		return NULL;
	}
	memcpy(writer->manifest, manifest,
		(size_t)manifest_count * sizeof(*writer->manifest));
	writer->roster_count = roster_count;
	memcpy(writer->roster, roster,
		(size_t)roster_count * sizeof(*writer->roster));
	writer->stream = fopen(writer->part_path, "wb+");
	if (!writer->stream) {
		free(writer->manifest);
		free(writer);
		blobFree(&manifest_blob);
		blobFree(&match_blob);
		blobFree(&roster_blob);
		if (out_result) *out_result = THEATER_FORMAT_IO_ERROR;
		return NULL;
	}
	u8 header[THEATER_FORMAT_HEADER_SIZE];
	encodeHeader(header, &candidate_match, 0, 0, 0, 0, 0, 0,
		roster_count, manifest_count);
	if (fwrite(header, 1, sizeof(header), writer->stream) != sizeof(header)) {
		result = THEATER_FORMAT_IO_ERROR;
	} else {
		writer->bytes_written = THEATER_FORMAT_HEADER_SIZE;
		result = writerAppendBlob(writer, THEATER_FORMAT_RECORD_MATCH, 0,
			&match_blob, NULL, NULL);
		if (result == THEATER_FORMAT_OK) {
			result = writerAppendBlob(writer, THEATER_FORMAT_RECORD_MANIFEST, 0,
				&manifest_blob, NULL, NULL);
		}
		if (result == THEATER_FORMAT_OK) {
			result = writerAppendBlob(writer, THEATER_FORMAT_RECORD_ROSTER, 0,
				&roster_blob, NULL, NULL);
		}
	}
	blobFree(&manifest_blob);
	blobFree(&match_blob);
	blobFree(&roster_blob);
	if (result != THEATER_FORMAT_OK) {
		theaterFormatAbort(writer);
		if (out_result) *out_result = result;
		return NULL;
	}
	if (out_result) *out_result = THEATER_FORMAT_OK;
	return writer;
}

theater_format_result_t theaterFormatAppendCheckpoint(
	theater_format_writer_t *writer,
	const theater_format_checkpoint_t *checkpoint)
{
	if (!writer || !writer->stream || !checkpoint) return THEATER_FORMAT_INVALID_ARGUMENT;
	if (writer->checkpoint_count > 0
			&& checkpoint->tick <= writer->index[writer->checkpoint_count - 1].tick) {
		return THEATER_FORMAT_INVALID_ARGUMENT;
	}
	theater_format_result_t result = ensureIndexCapacity(writer);
	if (result != THEATER_FORMAT_OK) return result;
	theater_blob_t blob = {0};
	result = encodeCheckpoint(checkpoint, &blob, writer->manifest,
		writer->manifest_count, writer->roster, writer->roster_count);
	u64 offset = 0;
	u32 crc = 0;
	if (result == THEATER_FORMAT_OK) {
		result = writerAppendBlob(writer, THEATER_FORMAT_RECORD_CHECKPOINT,
			checkpoint->tick, &blob, &offset, &crc);
	}
	if (result == THEATER_FORMAT_OK) {
		theater_format_index_entry_t *entry = &writer->index[writer->checkpoint_count++];
		entry->tick = checkpoint->tick;
		entry->file_offset = offset;
		entry->sequence = writer->next_sequence - 1;
		entry->record_crc = crc;
	}
	blobFree(&blob);
	return result;
}

theater_format_result_t theaterFormatAppendEvent(theater_format_writer_t *writer,
	const theater_format_event_t *event)
{
	if (!writer || !writer->stream || !event) return THEATER_FORMAT_INVALID_ARGUMENT;
	theater_blob_t blob = {0};
	theater_format_result_t result = encodeEvent(event, &blob);
	if (result == THEATER_FORMAT_OK) {
		result = writerAppendBlob(writer, THEATER_FORMAT_RECORD_EVENT,
			event->tick, &blob, NULL, NULL);
	}
	blobFree(&blob);
	return result;
}

theater_format_result_t theaterFormatMark(theater_format_writer_t *writer,
	theater_format_mark_t *out_mark)
{
	if (!writer || !writer->stream || !out_mark) {
		return THEATER_FORMAT_INVALID_ARGUMENT;
	}
	out_mark->file_offset = writer->bytes_written;
	out_mark->last_tick = writer->last_tick;
	out_mark->record_count = writer->record_count;
	out_mark->checkpoint_count = writer->checkpoint_count;
	out_mark->next_sequence = writer->next_sequence;
	out_mark->has_tick = writer->has_tick;
	return THEATER_FORMAT_OK;
}

theater_format_result_t theaterFormatRollback(theater_format_writer_t *writer,
	const theater_format_mark_t *mark)
{
	if (!writer || !writer->stream || !mark
			|| mark->file_offset < THEATER_FORMAT_HEADER_SIZE
			|| mark->file_offset > writer->bytes_written
			|| mark->record_count > writer->record_count
			|| mark->checkpoint_count > writer->checkpoint_count
			|| mark->next_sequence != mark->record_count) {
		return THEATER_FORMAT_INVALID_ARGUMENT;
	}
	theater_format_result_t result = truncateStream(writer->stream,
		mark->file_offset);
	if (result == THEATER_FORMAT_OK
			&& fseek(writer->stream, (long)mark->file_offset, SEEK_SET) != 0) {
		result = THEATER_FORMAT_IO_ERROR;
	}
	if (result != THEATER_FORMAT_OK) return result;
	writer->bytes_written = mark->file_offset;
	writer->last_tick = mark->last_tick;
	writer->record_count = mark->record_count;
	writer->checkpoint_count = mark->checkpoint_count;
	writer->next_sequence = mark->next_sequence;
	writer->has_tick = mark->has_tick;
	return THEATER_FORMAT_OK;
}

static theater_format_result_t encodeIndex(
	const theater_format_index_entry_t *entries, u32 count, theater_blob_t *blob)
{
	if (!entries || count == 0 || count > THEATER_FORMAT_MAX_CHECKPOINTS) {
		return THEATER_FORMAT_INCOMPLETE;
	}
	blobU32(blob, count);
	for (u32 i = 0; i < count; i++) {
		blobU64(blob, entries[i].tick);
		blobU64(blob, entries[i].file_offset);
		blobU32(blob, entries[i].sequence);
		blobU32(blob, entries[i].record_crc);
	}
	return blob->failed ? THEATER_FORMAT_LIMIT_EXCEEDED : THEATER_FORMAT_OK;
}

static theater_format_result_t publishCompleteHeader(FILE *stream,
	const theater_format_match_t *match, u32 completion_flags,
	u64 file_size, u32 record_count,
	u32 checkpoint_count, u64 index_offset, u32 index_length,
	u32 roster_count, u32 manifest_count)
{
	u8 header[THEATER_FORMAT_HEADER_SIZE];
	encodeHeader(header, match, completion_flags, file_size,
		record_count, checkpoint_count, index_offset, index_length,
		roster_count, manifest_count);
	if (fseek(stream, 0, SEEK_SET) != 0
			|| fwrite(header, 1, sizeof(header), stream) != sizeof(header)) {
		return THEATER_FORMAT_IO_ERROR;
	}
	return syncStream(stream);
}

static theater_format_result_t finalizeWriter(theater_format_writer_t *writer,
	u32 completion_flags)
{
	if (!writer || !writer->stream || writer->checkpoint_count == 0) {
		return THEATER_FORMAT_INCOMPLETE;
	}
	if (!(completion_flags & THEATER_FORMAT_FLAG_COMPLETE)
			|| (completion_flags & ~(THEATER_FORMAT_FLAG_COMPLETE
				| THEATER_FORMAT_FLAG_RECOVERED))) {
		return THEATER_FORMAT_INVALID_ARGUMENT;
	}
	theater_blob_t index_blob = {0};
	theater_format_result_t result = encodeIndex(writer->index,
		writer->checkpoint_count, &index_blob);
	const u64 final_tick = writer->has_tick ? writer->last_tick
		: writer->index[writer->checkpoint_count - 1].tick;
	u64 index_offset = 0;
	if (result == THEATER_FORMAT_OK) {
		result = writerAppendBlob(writer, THEATER_FORMAT_RECORD_SEEK_INDEX,
			final_tick,
			&index_blob, &index_offset, NULL);
	}
	const u32 index_length = index_blob.size;
	blobFree(&index_blob);
	if (result != THEATER_FORMAT_OK) return result;
	theater_blob_t empty = {0};
	result = writerAppendBlob(writer, THEATER_FORMAT_RECORD_END,
		final_tick, &empty, NULL, NULL);
	if (result != THEATER_FORMAT_OK) return result;
	result = syncStream(writer->stream);
	if (result != THEATER_FORMAT_OK) return result;
	return publishCompleteHeader(writer->stream, &writer->match,
		completion_flags,
		writer->bytes_written, writer->record_count, writer->checkpoint_count,
		index_offset, index_length, writer->roster_count,
		writer->manifest_count);
}

theater_format_result_t theaterFormatFinish(theater_format_writer_t *writer)
{
	if (!writer) return THEATER_FORMAT_INVALID_ARGUMENT;
	theater_format_result_t result = finalizeWriter(writer,
		THEATER_FORMAT_FLAG_COMPLETE);
	if (writer->stream) {
		if (fclose(writer->stream) != 0 && result == THEATER_FORMAT_OK) {
			result = THEATER_FORMAT_IO_ERROR;
		}
		writer->stream = NULL;
	}
	if (result == THEATER_FORMAT_OK && pathExists(writer->final_path)) {
		result = THEATER_FORMAT_IO_ERROR;
	}
	if (result == THEATER_FORMAT_OK) {
		result = replaceFile(writer->part_path, writer->final_path);
	}
	free(writer->manifest);
	free(writer->index);
	free(writer);
	return result;
}

void theaterFormatAbort(theater_format_writer_t *writer)
{
	if (!writer) return;
	if (writer->stream) fclose(writer->stream);
	remove(writer->part_path);
	free(writer->manifest);
	free(writer->index);
	free(writer);
}

void theaterFormatAbandon(theater_format_writer_t *writer)
{
	if (!writer) return;
	if (writer->stream) {
		(void)syncStream(writer->stream);
		fclose(writer->stream);
	}
	free(writer->manifest);
	free(writer->index);
	free(writer);
}

typedef struct theater_scan_s {
	theater_format_info_t info;
	theater_format_manifest_entry_t *manifest;
	theater_format_roster_entry_t roster[THEATER_FORMAT_MAX_ROSTER];
	theater_format_index_entry_t *checkpoints;
	u32 checkpoint_capacity;
	u32 parsed_records;
	u32 parsed_checkpoints;
	u32 parsed_roster;
	u32 parsed_manifest;
	u64 last_good_offset;
	u64 last_record_tick;
	u64 index_record_offset;
	u32 index_payload_length;
	s32 saw_match;
	s32 saw_manifest;
	s32 saw_roster;
	s32 saw_index;
	s32 saw_end;
	s32 has_record_tick;
} theater_scan_t;

static void scanFree(theater_scan_t *scan)
{
	free(scan->manifest);
	scan->manifest = NULL;
	free(scan->checkpoints);
	scan->checkpoints = NULL;
	scan->checkpoint_capacity = 0;
}

static theater_format_result_t scanAddCheckpoint(theater_scan_t *scan,
	const theater_format_index_entry_t *entry)
{
	if (scan->parsed_checkpoints >= THEATER_FORMAT_MAX_CHECKPOINTS) {
		return THEATER_FORMAT_LIMIT_EXCEEDED;
	}
	if (scan->parsed_checkpoints > 0
			&& entry->tick <= scan->checkpoints[scan->parsed_checkpoints - 1].tick) {
		return THEATER_FORMAT_CORRUPT;
	}
	if (scan->parsed_checkpoints >= scan->checkpoint_capacity) {
		u32 capacity = scan->checkpoint_capacity ? scan->checkpoint_capacity * 2u : 64u;
		if (capacity > THEATER_FORMAT_MAX_CHECKPOINTS) capacity = THEATER_FORMAT_MAX_CHECKPOINTS;
		void *grown = realloc(scan->checkpoints,
			(size_t)capacity * sizeof(theater_format_index_entry_t));
		if (!grown) return THEATER_FORMAT_IO_ERROR;
		scan->checkpoints = (theater_format_index_entry_t *)grown;
		scan->checkpoint_capacity = capacity;
	}
	scan->checkpoints[scan->parsed_checkpoints++] = *entry;
	return THEATER_FORMAT_OK;
}

static theater_format_result_t parseAndCompareIndex(const u8 *payload, u32 length,
	const theater_scan_t *scan, theater_format_index_entry_t *out_entries,
	u32 capacity, u32 *out_count)
{
	theater_reader_t reader = { payload, length, 0, 0 };
	const u32 count = readerU32(&reader);
	if (count == 0 || count != scan->parsed_checkpoints
			|| count > THEATER_FORMAT_MAX_CHECKPOINTS) return THEATER_FORMAT_CORRUPT;
	for (u32 i = 0; i < count; i++) {
		theater_format_index_entry_t entry;
		entry.tick = readerU64(&reader);
		entry.file_offset = readerU64(&reader);
		entry.sequence = readerU32(&reader);
		entry.record_crc = readerU32(&reader);
		if (reader.failed
				|| entry.tick != scan->checkpoints[i].tick
				|| entry.file_offset != scan->checkpoints[i].file_offset
				|| entry.sequence != scan->checkpoints[i].sequence
				|| entry.record_crc != scan->checkpoints[i].record_crc) {
			return THEATER_FORMAT_CORRUPT;
		}
		if (out_entries && i < capacity) out_entries[i] = entry;
	}
	if (reader.failed || reader.offset != reader.size) return THEATER_FORMAT_CORRUPT;
	if (out_entries && capacity < count) return THEATER_FORMAT_LIMIT_EXCEEDED;
	if (out_count) *out_count = count;
	return THEATER_FORMAT_OK;
}

static theater_format_result_t scanStream(FILE *stream, u64 size,
	s32 require_complete, s32 allow_truncated_tail, theater_scan_t *scan,
	theater_format_index_entry_t *out_entries, u32 out_capacity,
	u32 *out_count)
{
	if (size < THEATER_FORMAT_HEADER_SIZE) return THEATER_FORMAT_TRUNCATED;
	u8 header_bytes[THEATER_FORMAT_HEADER_SIZE];
	if (fread(header_bytes, 1, sizeof(header_bytes), stream) != sizeof(header_bytes)) {
		return THEATER_FORMAT_IO_ERROR;
	}
	theater_format_result_t result = decodeHeader(header_bytes, &scan->info);
	if (result != THEATER_FORMAT_OK) return result;
	const u8 header_mode = scan->info.match.mode;
	const u8 header_authority = scan->info.match.authority;
	const u64 header_start_time = scan->info.match.start_time_unix;
	if (scan->info.file_size > THEATER_FORMAT_MAX_FILE_SIZE
			|| scan->info.record_count > THEATER_FORMAT_MAX_RECORDS
			|| scan->info.checkpoint_count > THEATER_FORMAT_MAX_CHECKPOINTS
			|| scan->info.manifest_count > THEATER_FORMAT_MAX_MANIFEST_ENTRIES) {
		return THEATER_FORMAT_OVERSIZED;
	}
	if (scan->info.flags & ~(THEATER_FORMAT_FLAG_COMPLETE
			| THEATER_FORMAT_FLAG_RECOVERED)) {
		return THEATER_FORMAT_CORRUPT;
	}
	if ((scan->info.flags & THEATER_FORMAT_FLAG_RECOVERED)
			&& !(scan->info.flags & THEATER_FORMAT_FLAG_COMPLETE)) {
		return THEATER_FORMAT_CORRUPT;
	}
	if (require_complete && !(scan->info.flags & THEATER_FORMAT_FLAG_COMPLETE)) {
		return THEATER_FORMAT_INCOMPLETE;
	}
	scan->last_good_offset = THEATER_FORMAT_HEADER_SIZE;

	for (;;) {
		const long record_position_long = ftell(stream);
		if (record_position_long < 0) return THEATER_FORMAT_IO_ERROR;
		const u64 record_position = (u64)(unsigned long)record_position_long;
		if (record_position == size) break;
		if (size - record_position < THEATER_FORMAT_RECORD_HEADER_SIZE) {
			return allow_truncated_tail ? THEATER_FORMAT_TRUNCATED : THEATER_FORMAT_CORRUPT;
		}
		u8 record_bytes[THEATER_FORMAT_RECORD_HEADER_SIZE];
		if (fread(record_bytes, 1, sizeof(record_bytes), stream) != sizeof(record_bytes)) {
			return THEATER_FORMAT_IO_ERROR;
		}
		theater_record_header_t record;
		result = decodeRecordHeader(record_bytes, &record);
		if (result != THEATER_FORMAT_OK) {
			return result;
		}
		if (record.sequence != scan->parsed_records
				|| (scan->has_record_tick && record.tick < scan->last_record_tick)) {
			return THEATER_FORMAT_CORRUPT;
		}
		if (record.payload_length > size - record_position
					- THEATER_FORMAT_RECORD_HEADER_SIZE) {
			return allow_truncated_tail ? THEATER_FORMAT_TRUNCATED : THEATER_FORMAT_CORRUPT;
		}
		u8 *payload = NULL;
		if (record.payload_length) {
			payload = (u8 *)malloc(record.payload_length);
			if (!payload) return THEATER_FORMAT_IO_ERROR;
			if (fread(payload, 1, record.payload_length, stream) != record.payload_length) {
				free(payload);
				return THEATER_FORMAT_IO_ERROR;
			}
		}
		if (theaterCrc32(payload, record.payload_length) != record.payload_crc) {
			free(payload);
			return THEATER_FORMAT_CORRUPT;
		}

		if (scan->saw_end
				|| (scan->saw_index && record.kind != THEATER_FORMAT_RECORD_END)) {
			free(payload);
			return THEATER_FORMAT_CORRUPT;
		}
		switch (record.kind) {
		case THEATER_FORMAT_RECORD_MATCH:
			if (scan->parsed_records != 0 || scan->saw_match) result = THEATER_FORMAT_CORRUPT;
			else {
				theater_format_match_t candidate;
				result = parseMatch(payload, record.payload_length, &candidate);
				if (result == THEATER_FORMAT_OK
						&& (candidate.mode != header_mode
							|| candidate.authority != header_authority
							|| candidate.start_time_unix != header_start_time)) {
					result = THEATER_FORMAT_CORRUPT;
				}
				if (result == THEATER_FORMAT_OK) scan->info.match = candidate;
			}
			if (result == THEATER_FORMAT_OK) scan->saw_match = 1;
			break;
		case THEATER_FORMAT_RECORD_MANIFEST:
			if (scan->parsed_records != 1 || !scan->saw_match
					|| scan->saw_manifest) {
				result = THEATER_FORMAT_CORRUPT;
			} else {
				result = parseManifest(payload, record.payload_length,
					&scan->manifest, &scan->parsed_manifest);
				if (result == THEATER_FORMAT_OK) {
					u8 digest[32];
					sha256Hash(payload, record.payload_length, digest);
					if (memcmp(digest, scan->info.match.manifest_digest,
							sizeof(digest)) != 0
							|| !manifestContainsTypedId(scan->manifest,
								scan->parsed_manifest,
								scan->info.match.stage_id,
								THEATER_FORMAT_MANIFEST_STAGE,
								THEATER_FORMAT_MANIFEST_SLOT_MATCH)
							|| (scan->info.match.mode
								== THEATER_FORMAT_MODE_COMBAT_SIMULATOR
								&& !manifestContainsTypedId(scan->manifest,
									scan->parsed_manifest,
									scan->info.match.mode_id,
									THEATER_FORMAT_MANIFEST_GENERIC_ASSET,
									THEATER_FORMAT_CATALOG_TYPE_GAMEMODE))
							|| (scan->info.match.mode
								== THEATER_FORMAT_MODE_CAMPAIGN
								&& !manifestContainsTypedId(scan->manifest,
									scan->parsed_manifest,
									scan->info.match.mission_id,
									THEATER_FORMAT_MANIFEST_GENERIC_ASSET,
									THEATER_FORMAT_CATALOG_TYPE_MISSION))
							|| !manifestContainsMatchWeapons(scan->manifest,
								scan->parsed_manifest, &scan->info.match)) {
						result = THEATER_FORMAT_CORRUPT;
					} else {
						scan->saw_manifest = 1;
					}
				}
			}
			break;
		case THEATER_FORMAT_RECORD_ROSTER:
			if (scan->parsed_records != 2 || !scan->saw_manifest
					|| scan->saw_roster) {
				result = THEATER_FORMAT_CORRUPT;
			} else {
				result = parseRoster(payload, record.payload_length,
					&scan->parsed_roster, scan->manifest,
					scan->parsed_manifest, scan->roster,
					&scan->info.match);
				if (result == THEATER_FORMAT_OK) scan->saw_roster = 1;
			}
			break;
		case THEATER_FORMAT_RECORD_CHECKPOINT: {
			if (!scan->saw_roster || scan->saw_index) {
				result = THEATER_FORMAT_CORRUPT;
				break;
			}
			result = parseCheckpoint(payload, record.payload_length, record.tick,
				NULL, NULL, NULL, scan->manifest, scan->parsed_manifest,
				scan->roster, scan->parsed_roster);
			if (result == THEATER_FORMAT_OK) {
				theater_format_index_entry_t entry;
				entry.tick = record.tick;
				entry.file_offset = record_position;
				entry.sequence = record.sequence;
				entry.record_crc = record.payload_crc;
				result = scanAddCheckpoint(scan, &entry);
			}
			break;
		}
		case THEATER_FORMAT_RECORD_EVENT:
			result = !scan->saw_roster || scan->saw_index
				? THEATER_FORMAT_CORRUPT
				: parseEvent(payload, record.payload_length);
			break;
		case THEATER_FORMAT_RECORD_SEEK_INDEX:
			if (!scan->saw_roster || scan->saw_index || scan->parsed_checkpoints == 0) {
				result = THEATER_FORMAT_CORRUPT;
			} else {
				result = parseAndCompareIndex(payload, record.payload_length, scan,
					out_entries, out_capacity, out_count);
				if (result == THEATER_FORMAT_OK) {
					scan->saw_index = 1;
					scan->index_record_offset = record_position;
					scan->index_payload_length = record.payload_length;
				}
			}
			break;
		case THEATER_FORMAT_RECORD_END:
			if (!scan->saw_index || record.payload_length != 0) result = THEATER_FORMAT_CORRUPT;
			else {
				result = THEATER_FORMAT_OK;
				scan->saw_end = 1;
			}
			break;
		default:
			result = THEATER_FORMAT_CORRUPT;
			break;
		}
		free(payload);
		if (result != THEATER_FORMAT_OK) return result;
		scan->parsed_records++;
		scan->last_record_tick = record.tick;
		scan->has_record_tick = 1;
		const long after_long = ftell(stream);
		if (after_long < 0) return THEATER_FORMAT_IO_ERROR;
		scan->last_good_offset = (u64)(unsigned long)after_long;
		if (scan->parsed_records > THEATER_FORMAT_MAX_RECORDS) {
			return THEATER_FORMAT_LIMIT_EXCEEDED;
		}
	}

	if (!scan->saw_match || !scan->saw_manifest || !scan->saw_roster) {
		return THEATER_FORMAT_INCOMPLETE;
	}
	const s32 complete = (scan->info.flags & THEATER_FORMAT_FLAG_COMPLETE) != 0;
	if (require_complete || complete) {
		if (!scan->saw_index || !scan->saw_end
				|| scan->info.file_size != size
				|| scan->info.record_count != scan->parsed_records
				|| scan->info.checkpoint_count != scan->parsed_checkpoints
				|| scan->info.roster_count != scan->parsed_roster
				|| scan->info.manifest_count != scan->parsed_manifest
				|| scan->info.index_offset != scan->index_record_offset
				|| scan->info.index_length != scan->index_payload_length
				|| !matchValid(&scan->info.match, 1)) return THEATER_FORMAT_CORRUPT;
	} else if (scan->info.file_size != 0 || scan->info.record_count != 0
			|| scan->info.checkpoint_count != 0 || scan->info.index_offset != 0
			|| scan->info.index_length != 0
			|| scan->info.roster_count != scan->parsed_roster
			|| scan->info.manifest_count != scan->parsed_manifest) {
		return THEATER_FORMAT_CORRUPT;
	}
	return THEATER_FORMAT_OK;
}

theater_format_result_t theaterFormatValidate(const char *path,
	theater_format_info_t *out_info)
{
	if (!path || !out_info) return THEATER_FORMAT_INVALID_ARGUMENT;
	FILE *stream = fopen(path, "rb");
	if (!stream) return errno == ENOENT
		? THEATER_FORMAT_NOT_FOUND : THEATER_FORMAT_IO_ERROR;
	u64 size = 0;
	theater_format_result_t result = fileSize(stream, &size);
	theater_scan_t scan;
	memset(&scan, 0, sizeof(scan));
	if (result == THEATER_FORMAT_OK) {
		result = scanStream(stream, size, 1, 0, &scan, NULL, 0, NULL);
	}
	fclose(stream);
	if (result == THEATER_FORMAT_OK) *out_info = scan.info;
	scanFree(&scan);
	return result;
}

theater_format_result_t theaterFormatReadIndex(const char *path,
	theater_format_index_entry_t *entries, u32 capacity, u32 *out_count)
{
	if (!path || !out_count || (capacity && !entries)) {
		return THEATER_FORMAT_INVALID_ARGUMENT;
	}
	FILE *stream = fopen(path, "rb");
	if (!stream) return errno == ENOENT
		? THEATER_FORMAT_NOT_FOUND : THEATER_FORMAT_IO_ERROR;
	u64 size = 0;
	theater_format_result_t result = fileSize(stream, &size);
	theater_scan_t scan;
	memset(&scan, 0, sizeof(scan));
	if (result == THEATER_FORMAT_OK) {
		result = scanStream(stream, size, 1, 0, &scan, NULL, 0, NULL);
	}
	fclose(stream);
	if (result == THEATER_FORMAT_OK && capacity < scan.parsed_checkpoints) {
		result = THEATER_FORMAT_LIMIT_EXCEEDED;
	}
	if (result == THEATER_FORMAT_OK) {
		if (scan.parsed_checkpoints) {
			memcpy(entries, scan.checkpoints,
				(size_t)scan.parsed_checkpoints * sizeof(*entries));
		}
		*out_count = scan.parsed_checkpoints;
	}
	scanFree(&scan);
	return result;
}

theater_format_result_t theaterFormatReadManifest(const char *path,
	theater_format_manifest_entry_t *entries, u32 capacity, u32 *out_count)
{
	if (!path || !out_count || (capacity && !entries)) {
		return THEATER_FORMAT_INVALID_ARGUMENT;
	}
	FILE *stream = fopen(path, "rb");
	if (!stream) return errno == ENOENT
		? THEATER_FORMAT_NOT_FOUND : THEATER_FORMAT_IO_ERROR;
	u64 size = 0;
	theater_format_result_t result = fileSize(stream, &size);
	theater_scan_t scan;
	memset(&scan, 0, sizeof(scan));
	if (result == THEATER_FORMAT_OK) {
		result = scanStream(stream, size, 1, 0, &scan, NULL, 0, NULL);
	}
	fclose(stream);
	if (result == THEATER_FORMAT_OK && capacity < scan.parsed_manifest) {
		result = THEATER_FORMAT_LIMIT_EXCEEDED;
	}
	if (result == THEATER_FORMAT_OK) {
		memcpy(entries, scan.manifest,
			(size_t)scan.parsed_manifest * sizeof(*entries));
		*out_count = scan.parsed_manifest;
	}
	scanFree(&scan);
	return result;
}

theater_format_result_t theaterFormatReadRoster(const char *path,
	theater_format_roster_entry_t *entries, u32 capacity, u32 *out_count)
{
	if (!path || !out_count || (capacity && !entries)) {
		return THEATER_FORMAT_INVALID_ARGUMENT;
	}
	FILE *stream = fopen(path, "rb");
	if (!stream) return errno == ENOENT
		? THEATER_FORMAT_NOT_FOUND : THEATER_FORMAT_IO_ERROR;
	u64 size = 0;
	theater_format_result_t result = fileSize(stream, &size);
	theater_scan_t scan;
	memset(&scan, 0, sizeof(scan));
	if (result == THEATER_FORMAT_OK) {
		result = scanStream(stream, size, 1, 0, &scan, NULL, 0, NULL);
	}
	fclose(stream);
	if (result == THEATER_FORMAT_OK && capacity < scan.parsed_roster) {
		result = THEATER_FORMAT_LIMIT_EXCEEDED;
	}
	if (result == THEATER_FORMAT_OK) {
		memcpy(entries, scan.roster,
			(size_t)scan.parsed_roster * sizeof(*entries));
		*out_count = scan.parsed_roster;
	}
	scanFree(&scan);
	return result;
}

theater_format_result_t theaterFormatReadCheckpoint(const char *path,
	u32 checkpoint_index, theater_format_checkpoint_t *out_checkpoint,
	theater_format_entity_t *entities, u32 entity_capacity,
	theater_format_view_t *views, u32 view_capacity)
{
	if (!path || !out_checkpoint || (entity_capacity && !entities)
			|| (view_capacity && !views)) return THEATER_FORMAT_INVALID_ARGUMENT;
	FILE *stream = fopen(path, "rb");
	if (!stream) return errno == ENOENT
		? THEATER_FORMAT_NOT_FOUND : THEATER_FORMAT_IO_ERROR;
	u64 size = 0;
	theater_format_result_t result = fileSize(stream, &size);
	theater_scan_t scan;
	memset(&scan, 0, sizeof(scan));
	if (result == THEATER_FORMAT_OK) {
		result = scanStream(stream, size, 1, 0, &scan, NULL, 0, NULL);
	}
	if (result == THEATER_FORMAT_OK
			&& checkpoint_index >= scan.parsed_checkpoints) {
		result = THEATER_FORMAT_INVALID_ARGUMENT;
	}
	theater_format_checkpoint_t candidate;
	memset(&candidate, 0, sizeof(candidate));
	theater_format_entity_t *candidate_entities = NULL;
	theater_format_view_t *candidate_views = NULL;
	if (result == THEATER_FORMAT_OK) {
		const theater_format_index_entry_t *index =
			&scan.checkpoints[checkpoint_index];
		if (fseek(stream, (long)index->file_offset, SEEK_SET) != 0) {
			result = THEATER_FORMAT_IO_ERROR;
		} else {
			u8 header_bytes[THEATER_FORMAT_RECORD_HEADER_SIZE];
			theater_record_header_t header;
			if (fread(header_bytes, 1, sizeof(header_bytes), stream)
					!= sizeof(header_bytes)) {
				result = THEATER_FORMAT_IO_ERROR;
			} else {
				result = decodeRecordHeader(header_bytes, &header);
				if (result == THEATER_FORMAT_OK
						&& (header.kind != THEATER_FORMAT_RECORD_CHECKPOINT
							|| header.sequence != index->sequence
							|| header.tick != index->tick
							|| header.payload_crc != index->record_crc)) {
					result = THEATER_FORMAT_CORRUPT;
				}
				if (result == THEATER_FORMAT_OK) {
					u8 *payload = header.payload_length
						? (u8 *)malloc(header.payload_length) : NULL;
					if (header.payload_length && !payload) {
						result = THEATER_FORMAT_IO_ERROR;
					} else if (header.payload_length
							&& fread(payload, 1, header.payload_length, stream)
								!= header.payload_length) {
						result = THEATER_FORMAT_IO_ERROR;
					} else if (theaterCrc32(payload, header.payload_length)
							!= header.payload_crc) {
						result = THEATER_FORMAT_CORRUPT;
					} else {
						result = parseCheckpoint(payload, header.payload_length,
							header.tick, &candidate, &candidate_entities,
							&candidate_views, scan.manifest,
							scan.parsed_manifest, scan.roster,
							scan.parsed_roster);
					}
					free(payload);
				}
			}
		}
	}
	if (result == THEATER_FORMAT_OK
			&& (candidate.entity_count > entity_capacity
				|| candidate.view_count > view_capacity)) {
		result = THEATER_FORMAT_LIMIT_EXCEEDED;
	}
	if (result == THEATER_FORMAT_OK) {
		memcpy(entities, candidate_entities,
			(size_t)candidate.entity_count * sizeof(*entities));
		memcpy(views, candidate_views,
			(size_t)candidate.view_count * sizeof(*views));
		candidate.entities = entities;
		candidate.views = views;
		*out_checkpoint = candidate;
	}
	free(candidate_entities);
	free(candidate_views);
	fclose(stream);
	scanFree(&scan);
	return result;
}

theater_format_result_t theaterFormatRecoverInterrupted(const char *final_path)
{
	if (!final_path || !final_path[0] || strlen(final_path) >= 512) {
		return THEATER_FORMAT_INVALID_ARGUMENT;
	}
	if (pathExists(final_path)) return THEATER_FORMAT_IO_ERROR;
	char part_path[520];
	snprintf(part_path, sizeof(part_path), "%s.part", final_path);
	FILE *stream = fopen(part_path, "rb+");
	if (!stream) return errno == ENOENT
		? THEATER_FORMAT_NOT_FOUND : THEATER_FORMAT_IO_ERROR;
	u64 size = 0;
	theater_format_result_t result = fileSize(stream, &size);
	theater_scan_t scan;
	memset(&scan, 0, sizeof(scan));
	if (result == THEATER_FORMAT_OK) {
		result = scanStream(stream, size, 0, 1, &scan, NULL, 0, NULL);
	}
	if (result == THEATER_FORMAT_OK
			&& (scan.info.flags & THEATER_FORMAT_FLAG_COMPLETE)) {
		fclose(stream);
		result = replaceFile(part_path, final_path);
		scanFree(&scan);
		return result;
	}
	if ((scan.info.flags & THEATER_FORMAT_FLAG_COMPLETE)
			&& result != THEATER_FORMAT_OK) {
		fclose(stream);
		scanFree(&scan);
		return result;
	}
	if (result != THEATER_FORMAT_OK && result != THEATER_FORMAT_TRUNCATED) {
		fclose(stream);
		scanFree(&scan);
		return result;
	}
	if (result == THEATER_FORMAT_TRUNCATED && scan.last_good_offset == 0) {
		fclose(stream);
		scanFree(&scan);
		return THEATER_FORMAT_TRUNCATED;
	}
	if (!(scan.info.flags & THEATER_FORMAT_FLAG_COMPLETE)
			&& (scan.info.file_size != 0 || scan.info.record_count != 0
				|| scan.info.checkpoint_count != 0 || scan.info.index_offset != 0
				|| scan.info.index_length != 0
				|| scan.info.roster_count != scan.parsed_roster
				|| scan.info.manifest_count != scan.parsed_manifest)) {
		fclose(stream);
		scanFree(&scan);
		return THEATER_FORMAT_CORRUPT;
	}
	if (!scan.saw_match || !scan.saw_manifest || !scan.saw_roster
			|| scan.parsed_checkpoints == 0) {
		fclose(stream);
		scanFree(&scan);
		return THEATER_FORMAT_INCOMPLETE;
	}
	if (scan.saw_end) {
		if (result != THEATER_FORMAT_OK) {
			fclose(stream);
			scanFree(&scan);
			return THEATER_FORMAT_CORRUPT;
		}
		result = truncateStream(stream, scan.last_good_offset);
		if (result == THEATER_FORMAT_OK) {
			/* END and seek index were already durable: only the incomplete
			 * header publication was lost, so this is a clean COMPLETE repair. */
			result = publishCompleteHeader(stream, &scan.info.match,
				THEATER_FORMAT_FLAG_COMPLETE,
				scan.last_good_offset, scan.parsed_records,
				scan.parsed_checkpoints, scan.index_record_offset,
				scan.index_payload_length, scan.parsed_roster,
				scan.parsed_manifest);
		}
		if (fclose(stream) != 0 && result == THEATER_FORMAT_OK) {
			result = THEATER_FORMAT_IO_ERROR;
		}
		if (result == THEATER_FORMAT_OK) result = replaceFile(part_path, final_path);
		scanFree(&scan);
		return result;
	}

	u64 append_offset = scan.saw_index
		? scan.index_record_offset : scan.last_good_offset;
	u32 append_records = scan.parsed_records - (scan.saw_index ? 1u : 0u);
	result = truncateStream(stream, append_offset);
	if (result == THEATER_FORMAT_OK && fseek(stream, (long)append_offset, SEEK_SET) != 0) {
		result = THEATER_FORMAT_IO_ERROR;
	}
	if (result != THEATER_FORMAT_OK) {
		fclose(stream);
		scanFree(&scan);
		return result;
	}

	theater_format_writer_t writer;
	memset(&writer, 0, sizeof(writer));
	writer.stream = stream;
	strncpy(writer.final_path, final_path, sizeof(writer.final_path) - 1);
	strncpy(writer.part_path, part_path, sizeof(writer.part_path) - 1);
	writer.match = scan.info.match;
	writer.manifest_count = scan.parsed_manifest;
	writer.roster_count = scan.parsed_roster;
	writer.record_count = append_records;
	writer.checkpoint_count = scan.parsed_checkpoints;
	writer.next_sequence = append_records;
	writer.bytes_written = append_offset;
	writer.last_tick = scan.last_record_tick;
	writer.has_tick = 1;
	writer.index = scan.checkpoints;
	writer.index_capacity = scan.checkpoint_capacity;
	scan.checkpoints = NULL;
	scan.checkpoint_capacity = 0;
	/* No durable END existed. Publishing the maximal accepted prefix is valid,
	 * but it must remain distinguishable from a normally finalized match. */
	result = finalizeWriter(&writer, THEATER_FORMAT_FLAG_COMPLETE
		| THEATER_FORMAT_FLAG_RECOVERED);
	if (fclose(stream) != 0 && result == THEATER_FORMAT_OK) result = THEATER_FORMAT_IO_ERROR;
	writer.stream = NULL;
	if (result == THEATER_FORMAT_OK) result = replaceFile(part_path, final_path);
	free(writer.index);
	scanFree(&scan);
	return result;
}

const char *theaterFormatResultString(theater_format_result_t result)
{
	switch (result) {
	case THEATER_FORMAT_OK: return "ok";
	case THEATER_FORMAT_INVALID_ARGUMENT: return "invalid_argument";
	case THEATER_FORMAT_INVALID_IDENTITY: return "invalid_identity";
	case THEATER_FORMAT_NOT_AUTHORITY: return "not_authority";
	case THEATER_FORMAT_IO_ERROR: return "io_error";
	case THEATER_FORMAT_UNSUPPORTED_VERSION: return "unsupported_version";
	case THEATER_FORMAT_TRUNCATED: return "truncated";
	case THEATER_FORMAT_CORRUPT: return "corrupt";
	case THEATER_FORMAT_OVERSIZED: return "oversized";
	case THEATER_FORMAT_LIMIT_EXCEEDED: return "limit_exceeded";
	case THEATER_FORMAT_INCOMPLETE: return "incomplete";
	case THEATER_FORMAT_NOT_FOUND: return "not_found";
	default: return "unknown";
	}
}
