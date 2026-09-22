/**
 * theater.h -- authoritative Theater recorder adapter and validated listing.
 *
 * The v2 foundation defines bounded capture for Campaign, local Combat
 * Simulator, and in-client listen authority directly from the live simulation,
 * using the exact process-lifetime generation of every prop allocation. It
 * never depends on a synthetic spectator session. Effects/explosions/smoke,
 * animation/audio/cutscene streams, and coherent world playback remain
 * explicit T-THEATER-001 work; opening a v2 file cannot mutate spectator/live
 * game state until that transaction exists. Production recording fails closed
 * if any recordable prop lacks its exact lifecycle generation;
 * pointer/backing-address reuse is never accepted as identity.
 */

#ifndef _IN_THEATER_H
#define _IN_THEATER_H

#include <PR/ultratypes.h>
#include "theater_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define THEATER_MAGIC THEATER_FORMAT_MAGIC
#define THEATER_FILE_VERSION THEATER_FORMAT_VERSION

#define THEATER_REPLAY_LIST_MAX 32
#define THEATER_REPLAY_NAME_MAX 96

typedef struct theater_replay_entry_s {
	char filename[THEATER_REPLAY_NAME_MAX];
	u64 start_time_unix;
	u32 checkpoint_count;
	u32 frame_count; /* compatibility display count: identical to checkpoint_count */
	u32 record_count;
	u32 size_bytes;
	u32 format_flags;
	u8 mode;
	u8 authority;
	u8 campaign_variant;
	u8 recovered; /* validated interrupted-prefix publication, never clean finish */
	char stage_id[THEATER_FORMAT_ID_MAX];
	char mode_id[THEATER_FORMAT_ID_MAX];
	char mission_id[THEATER_FORMAT_ID_MAX];
} theater_replay_entry_t;

void theaterInit(void);
void theaterShutdown(void);
void theaterTick(void);

/**
 * Begin recording the loaded authoritative session to <home>/replays using the
 * shared exact prop-lifecycle generation contract.
 * The caller supplies only the filename. Match/catalog identity, roster, and
 * world state are captured from authoritative production state.
 */
s32 theaterStartRecording(const char *filename);
s32 theaterIsRecording(void);

/** Finalize the deterministic seek index and atomically publish the .pdth. */
void theaterStopRecording(void);

/**
 * Explicit typed event seam for authoritative producers. Numeric values are
 * semantic event values, never runtime asset identity.
 */
s32 theaterRecordEvent(u16 kind, u32 actor_id, u32 target_id,
	const s32 values[4], const char *catalog_id);

/** Force a checkpoint on the next authoritative tick. */
void theaterRequestCheckpoint(void);

/**
 * Validate a replay without applying it. Coherent world playback is a second
 * structural unit; this function currently rejects after validation and never
 * enters spectator mode or mutates live state.
 */
s32 theaterStartReplay(const char *filename);
void theaterStopReplay(void);
s32 theaterIsReplaying(void);

/** Recover valid interrupted candidates, then list only fully validated files. */
s32 theaterRefreshList(void);
const theater_replay_entry_t *theaterListAt(s32 idx);

#ifdef __cplusplus
}
#endif

#endif /* _IN_THEATER_H */
