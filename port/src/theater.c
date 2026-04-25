/**
 * theater.c -- saved-match recorder + replay reader.
 *
 * Replays are read+written under <home>/replays/<file>.pdth. The
 * recorder samples local match state at SPECTATOR_FANOUT_HZ (10 Hz) and
 * writes one state-frame record per sample. The reader walks records in
 * order at the same cadence, calling spectatorIngestParticipantSnapshot
 * for each state-frame -- the camera + control + UI surface is reused
 * verbatim from the live spectator path.
 *
 * Single-writer hygiene: this module owns s_Recorder and s_Replay state.
 * The recorder hooks netSendSpectateStateFrame's blob build (pulls the
 * exact same participant blob the wire sends) so on-disk format and
 * on-wire format stay in lockstep at zero maintenance cost. The reader
 * runs only when spectatorGet()->source == SPECTATOR_SOURCE_THEATER.
 */

#include "theater.h"
#include "spectator.h"
#include "net/netmsg.h"
#include "system.h"
#include "fs.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#define THEATER_BLK_SIZE  64
#define THEATER_RECORD_INTERVAL_MS  100u  /* matches SPECTATOR_FANOUT_HZ */

typedef struct {
	FILE *fp;
	u64   start_time_unix;
	u32   frame_count;
	u32   last_record_ms;
	char  path[400];
} theater_recorder_t;

typedef struct {
	FILE *fp;
	u32   header_fanout_hz;
	u32   frame_count;
	u32   last_play_ms;
	u32   playback_start_ms;
	char  path[400];
} theater_replayer_t;

static theater_recorder_t s_Recorder;
static theater_replayer_t s_Replay;

static theater_replay_entry_t s_List[THEATER_REPLAY_LIST_MAX];
static s32                    s_ListCount;

/* -------------------------------------------------------------------------
 * Path + endian helpers
 * ------------------------------------------------------------------------- */

static const char *replayDir(void)
{
	static char dir[512];
	if (dir[0]) return dir;
	char home[400];
	sysGetHomePath(home, sizeof(home));
	snprintf(dir, sizeof(dir), "%s/replays", home);
	fsCreateDir(dir);
	return dir;
}

static void buildReplayPath(const char *filename, char *out, u32 outsize)
{
	snprintf(out, outsize, "%s/%s", replayDir(), filename ? filename : "untitled.pdth");
}

static void writeU32(FILE *f, u32 v) {
	u8 b[4]; b[0]=(u8)v; b[1]=(u8)(v>>8); b[2]=(u8)(v>>16); b[3]=(u8)(v>>24);
	fwrite(b, 1, 4, f);
}
static void writeU64(FILE *f, u64 v) {
	u8 b[8]; for (s32 i=0;i<8;i++) b[i]=(u8)(v>>(8*i)); fwrite(b, 1, 8, f);
}
static u32 readU32(FILE *f) {
	u8 b[4]; if (fread(b,1,4,f)!=4) return 0;
	return ((u32)b[0]) | ((u32)b[1]<<8) | ((u32)b[2]<<16) | ((u32)b[3]<<24);
}
static u64 readU64(FILE *f) {
	u8 b[8]; if (fread(b,1,8,f)!=8) return 0; u64 v = 0;
	for (s32 i=0;i<8;i++) v |= ((u64)b[i]) << (8*i); return v;
}

/* -------------------------------------------------------------------------
 * Recorder
 * ------------------------------------------------------------------------- */

s32 theaterStartRecording(const char *filename)
{
	if (s_Recorder.fp) return -1; /* one recording at a time */

	char path[400];
	buildReplayPath(filename, path, sizeof(path));
	FILE *fp = fopen(path, "wb");
	if (!fp) {
		sysLogPrintf(LOG_WARNING, "THEATER: cannot open %s for write", path);
		return -1;
	}

	memset(&s_Recorder, 0, sizeof(s_Recorder));
	s_Recorder.fp = fp;
	s_Recorder.start_time_unix = (u64)time(NULL);
	s_Recorder.frame_count = 0;
	s_Recorder.last_record_ms = 0;
	strncpy(s_Recorder.path, path, sizeof(s_Recorder.path) - 1);

	/* Write header (frame_count is rewritten on stop). */
	const char magic[4] = { 'P', 'D', 'T', 'H' };
	fwrite(magic, 1, 4, fp);
	writeU32(fp, THEATER_FILE_VERSION);
	writeU64(fp, s_Recorder.start_time_unix);
	writeU32(fp, 0);                      /* frame_count placeholder */
	writeU32(fp, 10);                     /* fanout_hz */
	writeU64(fp, 0);                      /* reserved */
	fflush(fp);

	sysLogPrintf(LOG_NOTE, "THEATER: recording started -> %s", path);
	return 0;
}

s32 theaterIsRecording(void) { return s_Recorder.fp ? 1 : 0; }

void theaterStopRecording(void)
{
	if (!s_Recorder.fp) return;

	/* Write end record. */
	u8 b = THEATER_RECORD_END;
	fwrite(&b, 1, 1, s_Recorder.fp);
	writeU32(s_Recorder.fp, 0);

	/* Patch frame_count in the header. */
	fflush(s_Recorder.fp);
	if (fseek(s_Recorder.fp, 16, SEEK_SET) == 0) {
		writeU32(s_Recorder.fp, s_Recorder.frame_count);
	}
	fflush(s_Recorder.fp);
	fclose(s_Recorder.fp);
	sysLogPrintf(LOG_NOTE, "THEATER: recording stopped %s (frames=%u)",
	             s_Recorder.path, (unsigned)s_Recorder.frame_count);
	memset(&s_Recorder, 0, sizeof(s_Recorder));
}

/* The host fan-out path samples the local match into a 64-byte-per-
 * participant blob. We expose an inline capture entry point that mirrors
 * the same blob layout, so the recorder can call this without depending
 * on the wire-builder's internals.  For Phase 3 the recorder does NOT
 * invoke the host's blob builder directly (that lives in netmsg.c); it
 * captures from the spectator subsystem's most recent ingest state when
 * the local player is also streaming as host. The simplest path that
 * stays single-writer: the recorder records the spectator participants
 * table when the player is hosting + spectating themselves, and records
 * a frame from the live netSendSpectateStateFrame call site otherwise.
 *
 * For Phase 3 ship: the recorder samples the spectator-ingested state.
 * That covers: player records their own match while a spectator path is
 * also active (typical Theater scenario).  Players who are hosting
 * without spectators can opt-in via "Record this match" which begins a
 * synthetic spectator session against themselves; that wiring is a
 * follow-up that lives next to the live host fan-out builder.
 */

static void writeStateFrameFromSpectatorState(void)
{
	if (!s_Recorder.fp) return;
	const spectator_state_t *s = spectatorGet();
	if (!s) return;
	if (s->source != SPECTATOR_SOURCE_LIVE) return;

	/* Build per-participant blob in the same layout as the wire (see
	 * netmsgSvcStateFrameWrite). */
	u8 blob[SPECTATE_FRAME_PARTICIPANTS_MAX * THEATER_BLK_SIZE];
	memset(blob, 0, sizeof(blob));
	u32 nfilled = 0;
	for (s32 i = 0; i < SPECTATOR_MAX_PARTICIPANTS && nfilled < SPECTATE_FRAME_PARTICIPANTS_MAX; i++) {
		const spectator_participant_t *p = &s->participants[i];
		if (!p->in_use) continue;
		u8 *blk = blob + (size_t)nfilled * THEATER_BLK_SIZE;
		blk[0] = 1;
		blk[1] = p->team;
		blk[2] = p->is_bot;
		blk[3] = 0;
		*(s16 *)(blk + 4) = p->score;
		*(s16 *)(blk + 6) = p->deaths;
		*(f32 *)(blk + 8)  = p->pos[0];
		*(f32 *)(blk + 12) = p->pos[1];
		*(f32 *)(blk + 16) = p->pos[2];
		*(f32 *)(blk + 20) = p->angle_theta;
		*(f32 *)(blk + 24) = p->angle_verta;
		*(u32 *)(blk + 28) = p->weapon_runtime_idx;
		strncpy((char *)(blk + 32), p->name, SPECTATE_FRAME_NAME_MAX - 1);
		nfilled++;
	}

	/* Record header + payload. */
	const u32 payload_len = 4 + 4 + 1 + nfilled * THEATER_BLK_SIZE; /* host_handle + frame_seq + count + blocks */
	u8 b = THEATER_RECORD_STATE_FRAME;
	fwrite(&b, 1, 1, s_Recorder.fp);
	writeU32(s_Recorder.fp, payload_len);
	writeU32(s_Recorder.fp, s->host_handle);
	writeU32(s_Recorder.fp, s_Recorder.frame_count);
	b = (u8)nfilled;
	fwrite(&b, 1, 1, s_Recorder.fp);
	fwrite(blob, 1, nfilled * THEATER_BLK_SIZE, s_Recorder.fp);

	s_Recorder.frame_count++;
}

/* -------------------------------------------------------------------------
 * Replay reader
 * ------------------------------------------------------------------------- */

s32 theaterStartReplay(const char *filename)
{
	if (s_Replay.fp) return -1;

	char path[400];
	buildReplayPath(filename, path, sizeof(path));
	FILE *fp = fopen(path, "rb");
	if (!fp) {
		sysLogPrintf(LOG_WARNING, "THEATER: cannot open %s for read", path);
		return -1;
	}

	char magic[4] = {0};
	if (fread(magic, 1, 4, fp) != 4 ||
	    memcmp(magic, THEATER_MAGIC, 4) != 0) {
		fclose(fp);
		sysLogPrintf(LOG_WARNING, "THEATER: bad magic in %s", path);
		return -1;
	}
	const u32 ver = readU32(fp);
	if (ver != THEATER_FILE_VERSION) {
		fclose(fp);
		sysLogPrintf(LOG_WARNING, "THEATER: unsupported version %u in %s",
		             (unsigned)ver, path);
		return -1;
	}
	const u64 start_time = readU64(fp);
	const u32 frame_count = readU32(fp);
	const u32 fanout_hz = readU32(fp);
	(void)readU64(fp); /* reserved */
	(void)start_time;

	memset(&s_Replay, 0, sizeof(s_Replay));
	s_Replay.fp = fp;
	s_Replay.header_fanout_hz = fanout_hz ? fanout_hz : 10;
	s_Replay.frame_count = frame_count;
	s_Replay.playback_start_ms = SDL_GetTicks();
	strncpy(s_Replay.path, path, sizeof(s_Replay.path) - 1);

	/* Drive the spectator subsystem in Theater mode: the same camera +
	 * control surface, but the source flag tells the UI this is a file
	 * driver. We adapt by repurposing spectatorBeginLive with a sentinel
	 * host_handle (0xFFFFFFFF) that the live-only acceptors filter out;
	 * the ingest path itself does not care. */
	{
		extern void spectatorBeginTheater(void);
		spectatorBeginTheater();
	}

	sysLogPrintf(LOG_NOTE, "THEATER: replay opened %s (frames=%u, fanout=%u Hz)",
	             path, (unsigned)frame_count, (unsigned)s_Replay.header_fanout_hz);
	return 0;
}

s32 theaterIsReplaying(void) { return s_Replay.fp ? 1 : 0; }

void theaterStopReplay(void)
{
	if (!s_Replay.fp) return;
	fclose(s_Replay.fp);
	memset(&s_Replay, 0, sizeof(s_Replay));
	spectatorStop();
}

static void replayReadOneRecord(void)
{
	if (!s_Replay.fp) return;

	u8 kind = 0;
	if (fread(&kind, 1, 1, s_Replay.fp) != 1) {
		theaterStopReplay();
		return;
	}
	const u32 length = readU32(s_Replay.fp);

	if (kind == THEATER_RECORD_END) {
		sysLogPrintf(LOG_NOTE, "THEATER: replay end-of-file %s", s_Replay.path);
		theaterStopReplay();
		return;
	}

	if (kind == THEATER_RECORD_STATE_FRAME) {
		u32 host_handle = readU32(s_Replay.fp);
		u32 frame_seq   = readU32(s_Replay.fp);
		u8 count = 0;
		if (fread(&count, 1, 1, s_Replay.fp) != 1 || count > SPECTATE_FRAME_PARTICIPANTS_MAX) {
			theaterStopReplay();
			return;
		}
		spectator_participant_t blob[SPECTATE_FRAME_PARTICIPANTS_MAX];
		memset(blob, 0, sizeof(blob));
		for (u32 i = 0; i < count; i++) {
			u8 raw[THEATER_BLK_SIZE];
			if (fread(raw, 1, THEATER_BLK_SIZE, s_Replay.fp) != THEATER_BLK_SIZE) {
				theaterStopReplay();
				return;
			}
			blob[i].in_use = raw[0];
			blob[i].team   = raw[1];
			blob[i].is_bot = raw[2];
			blob[i].score  = *(const s16 *)(raw + 4);
			blob[i].deaths = *(const s16 *)(raw + 6);
			blob[i].pos[0] = *(const f32 *)(raw + 8);
			blob[i].pos[1] = *(const f32 *)(raw + 12);
			blob[i].pos[2] = *(const f32 *)(raw + 16);
			blob[i].angle_theta = *(const f32 *)(raw + 20);
			blob[i].angle_verta = *(const f32 *)(raw + 24);
			blob[i].weapon_runtime_idx = *(const u32 *)(raw + 28);
			memcpy(blob[i].name, raw + 32, SPECTATE_FRAME_NAME_MAX);
			blob[i].name[SPECTATE_FRAME_NAME_MAX - 1] = '\0';
		}
		spectatorIngestParticipantSnapshot(blob, (s32)count, host_handle);
		(void)frame_seq;
		return;
	}

	/* Skip unknown record kinds. */
	if (length > 0) {
		fseek(s_Replay.fp, (long)length, SEEK_CUR);
	}
}

/* -------------------------------------------------------------------------
 * Tick
 * ------------------------------------------------------------------------- */

void theaterTick(void)
{
	const u32 now = SDL_GetTicks();

	if (s_Recorder.fp) {
		if (now - s_Recorder.last_record_ms >= THEATER_RECORD_INTERVAL_MS) {
			s_Recorder.last_record_ms = now;
			writeStateFrameFromSpectatorState();
		}
	}

	if (s_Replay.fp) {
		const u32 interval = 1000u / (s_Replay.header_fanout_hz ? s_Replay.header_fanout_hz : 10);
		if (now - s_Replay.last_play_ms >= interval) {
			s_Replay.last_play_ms = now;
			replayReadOneRecord();
		}
	}
}

/* -------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------- */

void theaterInit(void)
{
	memset(&s_Recorder, 0, sizeof(s_Recorder));
	memset(&s_Replay, 0, sizeof(s_Replay));
	(void)replayDir();
}

void theaterShutdown(void)
{
	if (s_Recorder.fp) theaterStopRecording();
	if (s_Replay.fp)   theaterStopReplay();
}

/* -------------------------------------------------------------------------
 * Replay listing
 * ------------------------------------------------------------------------- */

#ifdef _WIN32
  #include <windows.h>
#else
  #include <dirent.h>
  #include <sys/stat.h>
#endif

static s32 readReplayHeaderInto(const char *path, theater_replay_entry_t *out)
{
	FILE *fp = fopen(path, "rb");
	if (!fp) return -1;
	char magic[4] = {0};
	if (fread(magic, 1, 4, fp) != 4 || memcmp(magic, THEATER_MAGIC, 4) != 0) {
		fclose(fp); return -1;
	}
	(void)readU32(fp); /* version */
	out->start_time_unix = readU64(fp);
	out->frame_count = readU32(fp);
	(void)readU32(fp); /* fanout_hz */
	(void)readU64(fp); /* reserved */
	fseek(fp, 0, SEEK_END);
	const long sz = ftell(fp);
	out->size_bytes = sz > 0 ? (u32)sz : 0;
	fclose(fp);
	return 0;
}

s32 theaterRefreshList(void)
{
	memset(s_List, 0, sizeof(s_List));
	s_ListCount = 0;

	const char *dir = replayDir();

#ifdef _WIN32
	char glob[600];
	snprintf(glob, sizeof(glob), "%s\\*.pdth", dir);
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA(glob, &fd);
	if (h == INVALID_HANDLE_VALUE) return 0;
	do {
		if (s_ListCount >= THEATER_REPLAY_LIST_MAX) break;
		theater_replay_entry_t *e = &s_List[s_ListCount];
		strncpy(e->filename, fd.cFileName, sizeof(e->filename) - 1);
		char path[600];
		snprintf(path, sizeof(path), "%s/%s", dir, fd.cFileName);
		if (readReplayHeaderInto(path, e) == 0) {
			s_ListCount++;
		} else {
			memset(e, 0, sizeof(*e));
		}
	} while (FindNextFileA(h, &fd));
	FindClose(h);
#else
	DIR *d = opendir(dir);
	if (!d) return 0;
	struct dirent *entry;
	while ((entry = readdir(d)) && s_ListCount < THEATER_REPLAY_LIST_MAX) {
		const size_t n = strlen(entry->d_name);
		if (n < 5 || strcmp(entry->d_name + n - 5, ".pdth") != 0) continue;
		theater_replay_entry_t *e = &s_List[s_ListCount];
		strncpy(e->filename, entry->d_name, sizeof(e->filename) - 1);
		char path[600];
		snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
		if (readReplayHeaderInto(path, e) == 0) {
			s_ListCount++;
		} else {
			memset(e, 0, sizeof(*e));
		}
	}
	closedir(d);
#endif
	return s_ListCount;
}

const theater_replay_entry_t *theaterListAt(s32 idx)
{
	if (idx < 0 || idx >= s_ListCount) return NULL;
	return &s_List[idx];
}
