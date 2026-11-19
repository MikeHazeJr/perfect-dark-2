#ifndef PD_GRID_MATCH_RESULT_H
#define PD_GRID_MATCH_RESULT_H

#include "grid_match_cache.h"
#ifdef __cplusplus
#include "pdgui_constants.h"
#else
#include "types.h"
#endif

struct netbuf;

#ifdef __cplusplus
extern "C" {
#endif

typedef enum grid_match_end_reason {
	GRID_MATCH_END_TIME = 1,
	GRID_MATCH_END_SCORE,
	GRID_MATCH_END_TEAM_SCORE,
	GRID_MATCH_END_MANUAL,
	GRID_MATCH_END_OTHER,
} grid_match_end_reason_e;

typedef struct grid_match_result {
	grid_match_offer_t offer;
	u32 stage_epoch;
	u32 end_tick;
	grid_match_end_reason_e reason;
	char arena_id[CATALOG_ID_LEN];
	u8 arena_sha256[SHA256_DIGEST_SIZE];
	u8 score_sha256[SHA256_DIGEST_SIZE];
} grid_match_result_t;

typedef struct grid_match_score_row {
	u8 host_slot;
	u8 team;
	s16 deaths;
	s16 points;
	s32 kills;
	s32 score;
	s32 has_score;
	char name[16];
} grid_match_score_row_t;

typedef struct grid_match_scorecard {
	u8 count;
	grid_match_score_row_t rows[MAX_MPCHRS];
} grid_match_scorecard_t;

/* Version, room, generation, manifest digest, stage epoch, end tick,
 * reason, arena ID length and bytes, arena digest, score digest. */
#define GRID_MATCH_RESULT_WIRE_MAX \
	(1 + 1 + 8 + (SHA256_HEX_SIZE - 1) + 4 + 4 + 1 + 1 \
	 + (CATALOG_ID_LEN - 1) + 2 * SHA256_DIGEST_SIZE)

/* Return 1 on success. Refusal leaves the buffer and output unchanged.
 * The caller owns packet framing and authenticates the match authority. */
s32 gridMatchResultWrite(struct netbuf *dst, const grid_match_result_t *result);
s32 gridMatchResultRead(struct netbuf *src, grid_match_result_t *result);

s32 gridMatchResultMatchesStage(const grid_match_result_t *result,
	const grid_match_offer_t *offer, u32 stage_epoch, const char *arena_id,
	const u8 arena_sha256[SHA256_DIGEST_SIZE]);

/* Pass the exact existing final-score bytes used to populate score_sha256. */
s32 gridMatchResultMatchesScores(const grid_match_result_t *result,
	const void *payload, size_t size);

/* Decode SVC_PLAYER_SCORES after its opcode. Slots retain host numbering.
 * Kills sum the signed non-self counts. The caller adds names and ranked scores.
 * A refusal leaves the output unchanged. */
s32 gridMatchResultReadScores(const grid_match_result_t *result,
	const void *payload, size_t size, grid_match_scorecard_t *out);

/* The last accepted receipt is available in the same connected room. */
const grid_match_result_t *netGridLastMatchResult(void);
const grid_match_scorecard_t *netGridLastMatchScores(void);
void netGridClearMatchResult(void);

#ifdef __cplusplus
}
#endif

#endif
