#include <string.h>

#include "net/grid_match_result.h"
#include "net/netbuf.h"

#define GRID_MATCH_RESULT_WIRE_VERSION 1
#define GRID_MATCH_RESULT_WIRE_FIXED (GRID_MATCH_RESULT_WIRE_MAX - (CATALOG_ID_LEN - 1))
#define GRID_MATCH_SCORE_ROW_BYTES (6u + MAX_MPCHRS * 2u)

_Static_assert(CATALOG_ID_LEN - 1 <= 0xff, "Grid result arena ID must fit its wire length");
_Static_assert(MAX_MPCHRS <= 0xff, "Grid result scores must fit their wire count");

static s32 digestPresent(const u8 digest[SHA256_DIGEST_SIZE])
{
	static const u8 empty[SHA256_DIGEST_SIZE] = {0};
	return memcmp(digest, empty, sizeof(empty)) != 0;
}

static s32 resultValid(const grid_match_result_t *result)
{
	if (!result || result->offer.room_id == 0xff || !result->offer.generation
			|| !result->stage_epoch || result->reason < GRID_MATCH_END_TIME
			|| result->reason > GRID_MATCH_END_OTHER
			|| result->offer.manifest_digest[SHA256_HEX_SIZE - 1] != '\0'
			|| strspn(result->offer.manifest_digest, "0123456789abcdef") != SHA256_HEX_SIZE - 1
			|| strspn(result->offer.manifest_digest, "0") == SHA256_HEX_SIZE - 1
			|| !digestPresent(result->arena_sha256) || !digestPresent(result->score_sha256)) {
		return 0;
	}

	const char *id = result->arena_id;
	const char *end = memchr(id, '\0', sizeof(result->arena_id));
	if (!end || strspn(id, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:_.-")
			!= (size_t)(end - id)) return 0;
	const char *separator = strchr(id, ':');
	return separator && separator != id && separator[1] && !strchr(separator + 1, ':');
}

s32 gridMatchResultWrite(struct netbuf *dst, const grid_match_result_t *result)
{
	if (!dst || !dst->data || dst->error || dst->wp > dst->size || !resultValid(result)) return 0;
	const u8 id_length = (u8)strlen(result->arena_id);
	if (dst->size - dst->wp < GRID_MATCH_RESULT_WIRE_FIXED + (u32)id_length) return 0;

	u8 data[GRID_MATCH_RESULT_WIRE_MAX];
	struct netbuf wire = { .data = data, .size = sizeof(data) };
	netbufWriteU8(&wire, GRID_MATCH_RESULT_WIRE_VERSION);
	netbufWriteU8(&wire, result->offer.room_id);
	netbufWriteU64(&wire, result->offer.generation);
	netbufWriteData(&wire, result->offer.manifest_digest, SHA256_HEX_SIZE - 1);
	netbufWriteU32(&wire, result->stage_epoch);
	netbufWriteU32(&wire, result->end_tick);
	netbufWriteU8(&wire, (u8)result->reason);
	netbufWriteU8(&wire, id_length);
	netbufWriteData(&wire, result->arena_id, id_length);
	netbufWriteData(&wire, result->arena_sha256, SHA256_DIGEST_SIZE);
	netbufWriteData(&wire, result->score_sha256, SHA256_DIGEST_SIZE);
	return !wire.error && netbufWriteData(dst, wire.data, wire.wp) == wire.wp;
}

s32 gridMatchResultRead(struct netbuf *src, grid_match_result_t *result)
{
	if (!src || !src->data || src->error || src->rp > src->wp || src->wp > src->size
			|| src->wp - src->rp < GRID_MATCH_RESULT_WIRE_FIXED || !result) return 0;

	struct netbuf wire = *src;
	grid_match_result_t candidate = {0};
	if (netbufReadU8(&wire) != GRID_MATCH_RESULT_WIRE_VERSION) return 0;
	candidate.offer.room_id = netbufReadU8(&wire);
	candidate.offer.generation = netbufReadU64(&wire);
	netbufReadData(&wire, candidate.offer.manifest_digest, SHA256_HEX_SIZE - 1);
	candidate.stage_epoch = netbufReadU32(&wire);
	candidate.end_tick = netbufReadU32(&wire);
	candidate.reason = (grid_match_end_reason_e)netbufReadU8(&wire);
	const u8 id_length = netbufReadU8(&wire);
	if (!id_length || id_length >= sizeof(candidate.arena_id)
			|| wire.wp - wire.rp < (u32)id_length + 2 * SHA256_DIGEST_SIZE) return 0;
	netbufReadData(&wire, candidate.arena_id, id_length);
	netbufReadData(&wire, candidate.arena_sha256, SHA256_DIGEST_SIZE);
	netbufReadData(&wire, candidate.score_sha256, SHA256_DIGEST_SIZE);
	if (wire.error || memchr(candidate.arena_id, '\0', id_length) || !resultValid(&candidate)) return 0;

	*result = candidate;
	*src = wire;
	return 1;
}

s32 gridMatchResultMatchesStage(const grid_match_result_t *result,
	const grid_match_offer_t *offer, u32 stage_epoch, const char *arena_id,
	const u8 arena_sha256[SHA256_DIGEST_SIZE])
{
	return resultValid(result) && offer && arena_id && arena_sha256
		&& result->offer.room_id == offer->room_id
		&& result->offer.generation == offer->generation
		&& memcmp(result->offer.manifest_digest, offer->manifest_digest, SHA256_HEX_SIZE) == 0
		&& result->stage_epoch == stage_epoch && strcmp(result->arena_id, arena_id) == 0
		&& memcmp(result->arena_sha256, arena_sha256, SHA256_DIGEST_SIZE) == 0;
}

s32 gridMatchResultMatchesScores(const grid_match_result_t *result,
	const void *payload, size_t size)
{
	if (!resultValid(result) || !payload || !size) return 0;
	u8 digest[SHA256_DIGEST_SIZE];
	sha256Hash(payload, size, digest);
	return memcmp(result->score_sha256, digest, SHA256_DIGEST_SIZE) == 0;
}

s32 gridMatchResultReadScores(const grid_match_result_t *result,
	const void *payload, size_t size, grid_match_scorecard_t *out)
{
	if (!out || !payload || !size) return 0;
	const u8 count = *(const u8 *)payload;
	if (count > MAX_MPCHRS || size != 1u + count * GRID_MATCH_SCORE_ROW_BYTES
			|| !gridMatchResultMatchesScores(result, payload, size)) return 0;

	struct netbuf wire;
	netbufStartReadData(&wire, payload, (u32)size);
	grid_match_scorecard_t candidate = {0};
	candidate.count = netbufReadU8(&wire);
	for (u8 i = 0; i < candidate.count; ++i) {
		grid_match_score_row_t *row = &candidate.rows[i];
		row->host_slot = netbufReadU8(&wire);
		if (row->host_slot >= MAX_MPCHRS || (i && row->host_slot <= candidate.rows[i - 1].host_slot)) return 0;
		row->team = netbufReadU8(&wire);
		row->deaths = netbufReadS16(&wire);
		row->points = netbufReadS16(&wire);
		for (s32 opponent = 0; opponent < MAX_MPCHRS; ++opponent) {
			const s16 kills = netbufReadS16(&wire);
			if (opponent != row->host_slot) row->kills += kills;
		}
	}
	if (wire.error || netbufReadLeft(&wire) != 0) return 0;
	*out = candidate;
	return 1;
}
