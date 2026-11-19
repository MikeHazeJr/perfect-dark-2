#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <PR/libaudio.h>
#include <PR/ultratypes.h>
#include <n_libaudio.h>
#include "platform.h"
#include "system.h"
#include "fs.h"
#include "utils.h"
#include "mod.h"
#include "mod_sequence_path.h"
#include "data.h"
#include "assetcatalog.h"
#include "assetcatalog_load.h"
#include "assetcatalog_scanner.h"
#include "bss.h"
#include "catalog_animation_generation.h"
#include "grid_match_package_io.h"
#include "modarchive.h"
#include "net/net.h"
#include "scenario_source_runtime.h"
#include "asset_fallback_telemetry.h" /* c3849 Wave 1 */
#include "lib/anim.h"   /* c3849 Wave 2: animGetTotalCount */
#include "modmusic.h"
#include "modasset_compiler.h"

#define MOD_TEXTURES_DIR "textures"
#define MOD_ANIMATIONS_DIR "animations"
#define MOD_SEQUENCES_DIR "sequences"
#define MOD_SEQUENCE_VIRTUAL_BASE 0x4000
#define MOD_SEQUENCE_VIRTUAL_SLOTS 32

#define PARSE_INT(sec, name, v, min, max, ret) \
	p = modConfigParseIntValue(p, token, &v); \
	if (!p || v < (min) || v > (max)) { \
		sysLogPrintf(LOG_ERROR, "mod: %s: invalid " name " value: %s", sec, token); \
		return ret; \
	}

static inline char *modConfigParseIntValue(char *p, char *token, s32 *out)
{
	p = strParseToken(p, token, NULL);
	if (!token[0]) {
		return NULL;
	}
	char *endp = token;
	const s32 num = strtol(token, &endp, 0);
	if (num == 0 && (endp == token || *endp != '\0')) {
		return NULL;
	}
	*out = num;
	return p;
}

static s32 modSequencePathHasAudioExtension(const char *path)
{
	const char *dot;

	if (!path || !path[0]) {
		return 0;
	}

	dot = strrchr(path, '.');
	if (!dot) {
		return 0;
	}

	return strcasecmp(dot, ".wav") == 0
		|| strcasecmp(dot, ".ogg") == 0
		|| strcasecmp(dot, ".mp3") == 0;
}

typedef struct {
	u8 *data;
	u32 len;
	u32 cap;
} mod_seq_buf_t;

static char s_ModSequenceVirtualIds[MOD_SEQUENCE_VIRTUAL_SLOTS][CATALOG_ID_LEN];
static u8 s_ModSequenceGridSlots[MOD_SEQUENCE_VIRTUAL_SLOTS];

extern s32 g_TemporaryPrimaryTrack;
extern s32 g_TemporaryAmbientTrack;
extern u32 g_SeqBufferSize;

static s32 modSequenceVirtualIndex(s32 tracknum)
{
	if (tracknum < MOD_SEQUENCE_VIRTUAL_BASE) {
		return -1;
	}

	tracknum -= MOD_SEQUENCE_VIRTUAL_BASE;
	if (tracknum < 0 || tracknum >= MOD_SEQUENCE_VIRTUAL_SLOTS) {
		return -1;
	}

	return tracknum;
}

s32 modSequenceIsVirtualTrack(s32 tracknum)
{
	const s32 index = modSequenceVirtualIndex(tracknum);
	return index >= 0 && s_ModSequenceVirtualIds[index][0] != '\0';
}

const char *modSequenceVirtualTrackId(s32 tracknum)
{
	const s32 index = modSequenceVirtualIndex(tracknum);
	if (index < 0 || s_ModSequenceVirtualIds[index][0] == '\0') {
		return NULL;
	}
	return s_ModSequenceVirtualIds[index];
}

static s32 modSequenceFindVirtualTrack(const char *catalog_id)
{
	for (s32 i = 0; i < MOD_SEQUENCE_VIRTUAL_SLOTS; i++) {
		if (strcmp(s_ModSequenceVirtualIds[i], catalog_id) == 0) {
			return MOD_SEQUENCE_VIRTUAL_BASE + i;
		}
	}
	return -1;
}

static s32 modSequenceAllocateVirtualTrack(const char *catalog_id, s32 grid)
{
	for (s32 i = 0; i < MOD_SEQUENCE_VIRTUAL_SLOTS; i++) {
		if (s_ModSequenceVirtualIds[i][0] == '\0') {
			strncpy(s_ModSequenceVirtualIds[i], catalog_id,
				sizeof(s_ModSequenceVirtualIds[i]) - 1);
			s_ModSequenceVirtualIds[i][sizeof(s_ModSequenceVirtualIds[i]) - 1] = '\0';
			s_ModSequenceGridSlots[i] = grid != 0;
			sysLogPrintf(LOG_NOTE,
				"CATALOG: music '%s' assigned private sequence slot %d",
				catalog_id, MOD_SEQUENCE_VIRTUAL_BASE + i);
			return MOD_SEQUENCE_VIRTUAL_BASE + i;
		}
	}

	sysLogPrintf(LOG_WARNING,
		"CATALOG: music '%s' could not get a private sequence slot", catalog_id);
	return -1;
}

s32 modSequenceVirtualTrackForCatalogId(const char *catalog_id)
{
	const asset_entry_t *entry = catalog_id && catalog_id[0] ? assetCatalogResolve(catalog_id) : NULL;
	if (!entry || entry->type != ASSET_AUDIO || entry->ext.audio.category != AUDIO_CAT_MUSIC
			|| entry->ext.audio.file_path[0]) return -1;
	const s32 track = modSequenceFindVirtualTrack(catalog_id);
	if (entry->temporary && entry->grid_offer_only) {
		return track >= 0 && s_ModSequenceGridSlots[modSequenceVirtualIndex(track)] ? track : -1;
	}
	return track >= 0 ? track : modSequenceAllocateVirtualTrack(catalog_id, 0);
}

s32 modSequenceReserveGridTrack(const char *catalog_id)
{
	if (!catalog_id || !catalog_id[0] || strlen(catalog_id) >= CATALOG_ID_LEN
			|| modSequenceFindVirtualTrack(catalog_id) >= 0) return -1;
	return modSequenceAllocateVirtualTrack(catalog_id, 1);
}

void modSequenceForgetGridStageTracks(void)
{
	const s32 primary = modSequenceVirtualIndex(g_TemporaryPrimaryTrack);
	const s32 ambient = modSequenceVirtualIndex(g_TemporaryAmbientTrack);
	if (primary >= 0 && s_ModSequenceGridSlots[primary]) g_TemporaryPrimaryTrack = -1;
	if (ambient >= 0 && s_ModSequenceGridSlots[ambient]) g_TemporaryAmbientTrack = -1;
}

s32 modSequenceCanRetireGridTrack(const char *catalog_id, s32 tracknum)
{
	const s32 index = modSequenceVirtualIndex(tracknum);
	if (!catalog_id || !catalog_id[0] || index < 0 || !s_ModSequenceGridSlots[index]
			|| strcmp(s_ModSequenceVirtualIds[index], catalog_id) != 0
			|| g_TemporaryPrimaryTrack == tracknum || g_TemporaryAmbientTrack == tracknum
			|| scenarioSourceStagePrimaryMusic(g_MusicStageNum) == tracknum) return 0;
	if (g_MusicEventQueueLength < 0 || g_MusicEventQueueLength > ARRAYCOUNT(g_MusicEventQueue)) return 0;
	for (s32 i = 0; i < g_MusicEventQueueLength; i++) {
		const struct musicevent *event = &g_MusicEventQueue[i];
		if (event->eventtype == MUSICEVENTTYPE_PLAY && event->tracknum == tracknum) return 0;
	}
	for (s32 i = 0; i < ARRAYCOUNT(g_SeqInstances); i++) {
		const struct seqinstance *seq = &g_SeqInstances[i];
		if (seq->tracknum == tracknum && seq->seqp && n_alCSPGetState(seq->seqp) != AL_STOPPED) return 0;
	}
	return 1;
}

s32 modSequenceRetireGridTrack(const char *catalog_id, s32 tracknum)
{
	if (!modSequenceCanRetireGridTrack(catalog_id, tracknum)) return 0;
	const s32 index = modSequenceVirtualIndex(tracknum);
	s_ModSequenceVirtualIds[index][0] = '\0';
	s_ModSequenceGridSlots[index] = 0;
	return 1;
}

typedef struct {
	u32 tick;
	u8 status;
	u8 byte1;
	u8 byte2;
	u32 duration;
	u32 tempo_us;
	u32 loop_count;
	char type[16];
} mod_seq_event_t;

typedef struct {
	mod_seq_event_t *events;
	u32 count;
	u32 cap;
	mod_seq_buf_t track;
	u32 last_tick;
	u32 loop_start_stack[16];
	u32 loop_start_count;
} mod_seq_track_t;

static void modSequenceBufFree(mod_seq_buf_t *b)
{
	if (b && b->data) {
		sysMemFree(b->data);
	}
	if (b) {
		b->data = NULL;
		b->len = 0;
		b->cap = 0;
	}
}

static s32 modSequenceBufReserve(mod_seq_buf_t *b, u32 extra)
{
	u32 need;
	u32 cap;
	u8 *next;

	if (!b) {
		return -1;
	}

	if (extra > 0xffffffffu - b->len) {
		return -1;
	}

	need = b->len + extra;
	if (need <= b->cap) {
		return 0;
	}

	cap = b->cap ? b->cap : 256;
	while (cap < need) {
		if (cap > 0x80000000u) {
			return -1;
		}
		cap *= 2;
	}

	next = b->data ? sysMemRealloc(b->data, cap) : sysMemAlloc(cap);
	if (!next) {
		return -1;
	}

	b->data = next;
	b->cap = cap;
	return 0;
}

static s32 modSequenceBufPut(mod_seq_buf_t *b, u8 v)
{
	if (modSequenceBufReserve(b, 1) != 0) {
		return -1;
	}
	b->data[b->len++] = v;
	return 0;
}

static s32 modSequenceBufPutVarLen(mod_seq_buf_t *b, u32 value)
{
	u8 bytes[5];
	u32 count = 0;

	bytes[count++] = (u8)(value & 0x7f);
	value >>= 7;
	while (value && count < sizeof(bytes)) {
		bytes[count++] = (u8)((value & 0x7f) | 0x80);
		value >>= 7;
	}
	if (value) {
		return -1;
	}
	while (count > 0) {
		if (modSequenceBufPut(b, bytes[--count]) != 0) {
			return -1;
		}
	}
	return 0;
}

static s32 modSequenceParseDivision(const char *ini, u32 size, u32 *out_division)
{
	char *copy;
	char *line;
	char *save = NULL;
	s32 found = 0;

	if (!ini || !out_division) {
		return -1;
	}

	copy = sysMemAlloc(size + 1);
	if (!copy) {
		return -1;
	}
	memcpy(copy, ini, size);
	copy[size] = '\0';

	for (line = strtok_r(copy, "\r\n", &save); line; line = strtok_r(NULL, "\r\n", &save)) {
		char *eq;
		char *key = strTrim(line);
		if (!key || key[0] == '#' || key[0] == ';' || key[0] == '[') {
			continue;
		}
		eq = strchr(key, '=');
		if (!eq) {
			continue;
		}
		*eq++ = '\0';
		key = strTrim(key);
		eq = strTrim(eq);
		if (key && eq && strcasecmp(key, "division") == 0) {
			char *endp = eq;
			unsigned long value = strtoul(eq, &endp, 0);
			if (endp == eq || value == 0 || value > 0x7fff) {
				sysMemFree(copy);
				return -1;
			}
			*out_division = (u32)value;
			found = 1;
			break;
		}
	}

	sysMemFree(copy);
	return found ? 0 : -1;
}

typedef struct {
	const char *start;
	const char *end;
} mod_json_span_t;

static const char *modJsonSkipWs(const char *p, const char *end)
{
	if (!p) return NULL;
	while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
		p++;
	}
	return p;
}

static const char *modJsonSkipString(const char *p, const char *end)
{
	if (p >= end || *p != '"') return NULL;
	p++;
	while (p < end) {
		if (*p == '\\') {
			p += 2;
		} else if (*p == '"') {
			return p + 1;
		} else {
			p++;
		}
	}
	return NULL;
}

static const char *modJsonFindMatching(const char *p, const char *end,
		char open, char close)
{
	s32 depth = 0;
	while (p < end) {
		if (*p == '"') {
			p = modJsonSkipString(p, end);
			if (!p) return NULL;
			continue;
		}
		if (*p == open) depth++;
		if (*p == close) {
			depth--;
			if (depth == 0) return p + 1;
		}
		p++;
	}
	return NULL;
}

static const char *modJsonValueEnd(const char *p, const char *end)
{
	p = modJsonSkipWs(p, end);
	if (!p || p >= end) return NULL;
	if (*p == '"') return modJsonSkipString(p, end);
	if (*p == '{') return modJsonFindMatching(p, end, '{', '}');
	if (*p == '[') return modJsonFindMatching(p, end, '[', ']');
	while (p < end && *p != ',' && *p != '}' && *p != ']') p++;
	return p;
}

static const char *modJsonFindKey(mod_json_span_t object, const char *key)
{
	const char *p = modJsonSkipWs(object.start, object.end);
	const size_t key_len = strlen(key);
	if (p >= object.end || *p != '{') return NULL;
	p++;
	while (p < object.end) {
		const char *key_start;
		const char *key_end;
		const char *value;
		p = modJsonSkipWs(p, object.end);
		if (p >= object.end || *p == '}') return NULL;
		key_start = p + 1;
		key_end = modJsonSkipString(p, object.end);
		if (!key_end) return NULL;
		p = modJsonSkipWs(key_end, object.end);
		if (p >= object.end || *p != ':') return NULL;
		value = modJsonSkipWs(p + 1, object.end);
		if ((size_t)(key_end - key_start - 1) == key_len &&
				strncmp(key_start, key, key_len) == 0) {
			return value;
		}
		p = modJsonValueEnd(value, object.end);
		if (!p) return NULL;
		p = modJsonSkipWs(p, object.end);
		if (p < object.end && *p == ',') p++;
	}
	return NULL;
}

static s32 modJsonReadArray(const char *value, const char *end,
		mod_json_span_t *out)
{
	const char *p = modJsonSkipWs(value, end);
	const char *m;
	if (!p || p >= end || *p != '[') return 0;
	m = modJsonFindMatching(p, end, '[', ']');
	if (!m) return 0;
	out->start = p;
	out->end = m;
	return 1;
}

static s32 modJsonObjectArray(mod_json_span_t object, const char *key,
		mod_json_span_t *out)
{
	const char *value = modJsonFindKey(object, key);
	return modJsonReadArray(value, object.end, out);
}

static s32 modJsonObjectU32(mod_json_span_t object, const char *key, u32 *out)
{
	const char *value = modJsonFindKey(object, key);
	const char *end;
	char *endp = NULL;
	unsigned long parsed;
	if (!value || !out) return 0;
	end = modJsonValueEnd(value, object.end);
	if (!end) return 0;
	value = modJsonSkipWs(value, end);
	parsed = strtoul(value, &endp, 10);
	if (endp == value || parsed > 0xfffffffful ||
			modJsonSkipWs(endp, end) != end) {
		return 0;
	}
	*out = (u32)parsed;
	return 1;
}

static s32 modJsonObjectString(mod_json_span_t object, const char *key,
		char *out, size_t out_size)
{
	const char *value = modJsonFindKey(object, key);
	const char *p;
	size_t n = 0;
	if (!value || !out || out_size == 0) return 0;
	p = modJsonSkipWs(value, object.end);
	if (p >= object.end || *p != '"') return 0;
	p++;
	while (p < object.end && *p != '"') {
		char c = *p++;
		if (c == '\\' && p < object.end) {
			c = *p++;
		}
		if (n + 1 >= out_size) return 0;
		out[n++] = c;
	}
	if (p >= object.end || *p != '"') return 0;
	out[n] = '\0';
	return 1;
}

static s32 modJsonArrayNextObject(mod_json_span_t array, const char **cursor,
		mod_json_span_t *out)
{
	const char *p = *cursor ? *cursor : array.start + 1;
	p = modJsonSkipWs(p, array.end);
	if (p < array.end && *p == ',') {
		p = modJsonSkipWs(p + 1, array.end);
	}
	if (p >= array.end || *p == ']') return 0;
	if (*p != '{') return 0;
	out->start = p;
	out->end = modJsonFindMatching(p, array.end, '{', '}');
	if (!out->end) return 0;
	*cursor = out->end;
	return 1;
}

static s32 modSequenceTrackPush(mod_seq_track_t *track,
		const mod_seq_event_t *event)
{
	mod_seq_event_t *next;
	u32 cap;

	if (track->count == track->cap) {
		cap = track->cap ? track->cap * 2 : 64;
		next = track->events
			? sysMemRealloc(track->events, cap * sizeof(*track->events))
			: sysMemAlloc(cap * sizeof(*track->events));
		if (!next) {
			return -1;
		}
		track->events = next;
		track->cap = cap;
	}

	track->events[track->count++] = *event;
	return 0;
}

static void modSequenceTracksFree(mod_seq_track_t *tracks)
{
	u32 i;

	if (!tracks) {
		return;
	}

	for (i = 0; i < 16; i++) {
		if (tracks[i].events) {
			sysMemFree(tracks[i].events);
		}
		modSequenceBufFree(&tracks[i].track);
	}
}

static s32 modSequenceParseEventsJson(const char *text, u32 size,
		mod_seq_track_t *tracks, u32 *out_events)
{
	u32 events = 0;
	mod_json_span_t root;
	mod_json_span_t events_array;
	mod_json_span_t object;
	const char *cursor = NULL;

	if (!text || !size || !tracks || !out_events) return -1;

	root.start = text;
	root.end = text + size;
	if (!modJsonObjectArray(root, "events", &events_array)) {
		return -1;
	}

	while (modJsonArrayNextObject(events_array, &cursor, &object)) {
		u32 track_index = 0;
		u32 value = 0;
		mod_seq_event_t event;

		memset(&event, 0, sizeof(event));

		if (!modJsonObjectU32(object, "tick", &event.tick) ||
				!modJsonObjectU32(object, "track", &track_index) ||
				track_index >= 16 ||
				!modJsonObjectString(object, "type", event.type, sizeof(event.type))) {
			return -1;
		}

		if (modJsonObjectU32(object, "status", &value)) event.status = (u8)value;
		if (modJsonObjectU32(object, "byte1", &value)) event.byte1 = (u8)value;
		if (modJsonObjectU32(object, "byte2", &value)) event.byte2 = (u8)value;
		if (!modJsonObjectU32(object, "duration", &event.duration)) event.duration = 0;
		if (!modJsonObjectU32(object, "tempo_us", &event.tempo_us)) event.tempo_us = 0;
		if (!modJsonObjectU32(object, "loop_count", &event.loop_count)) event.loop_count = 0;

		if (modSequenceTrackPush(&tracks[track_index], &event) != 0) {
			return -1;
		}
		events++;
	}

	*out_events = events;
	return events > 0 ? 0 : -1;
}

static s32 modSequenceLoadEventsJson(const char *path,
		mod_seq_track_t *tracks, u32 *out_events)
{
	if (!path) return -1;
	u32 size = 0;
	char *text = fsFileLoad(path, &size);
	if (!text) return -1;
	text[size] = '\0';
	const s32 result = modSequenceParseEventsJson(text, size, tracks, out_events);
	sysMemFree(text);
	return result;
}

static s32 modSequenceEmitLoopEndBody(mod_seq_track_t *track, u32 loop_count);
static s32 modSequenceCloseOpenLoopsAtTrackEnd(mod_seq_track_t *track);

static s32 modSequenceEmitEvent(mod_seq_track_t *track,
		const mod_seq_event_t *event)
{
	u32 delta;

	if (!track || !event || event->tick < track->last_tick) {
		return -1;
	}

	delta = event->tick - track->last_tick;
	if (modSequenceBufPutVarLen(&track->track, delta) != 0) {
		return -1;
	}

	if (strcmp(event->type, "midi") == 0) {
		u8 kind = event->status & 0xf0;
		if (event->status < 0x80) {
			return -1;
		}
		if (modSequenceBufPut(&track->track, event->status) != 0 ||
				modSequenceBufPut(&track->track, event->byte1) != 0) {
			return -1;
		}
		if (kind != AL_MIDI_ProgramChange && kind != AL_MIDI_ChannelPressure) {
			if (modSequenceBufPut(&track->track, event->byte2) != 0) {
				return -1;
			}
			if (kind == AL_MIDI_NoteOn &&
					modSequenceBufPutVarLen(&track->track, event->duration) != 0) {
				return -1;
			}
		}
	} else if (strcmp(event->type, "tempo") == 0) {
		if (event->tempo_us > 0xffffffu) {
			return -1;
		}
		if (modSequenceBufPut(&track->track, AL_MIDI_Meta) != 0 ||
				modSequenceBufPut(&track->track, AL_MIDI_META_TEMPO) != 0 ||
				modSequenceBufPut(&track->track, (u8)((event->tempo_us >> 16) & 0xff)) != 0 ||
				modSequenceBufPut(&track->track, (u8)((event->tempo_us >> 8) & 0xff)) != 0 ||
				modSequenceBufPut(&track->track, (u8)(event->tempo_us & 0xff)) != 0) {
			return -1;
		}
	} else if (strcmp(event->type, "loop_start") == 0) {
		if (track->loop_start_count >= 16 || event->loop_count > 0xffffu) {
			return -1;
		}
		track->loop_start_stack[track->loop_start_count++] = track->track.len - 1;
		if (modSequenceBufPut(&track->track, AL_MIDI_Meta) != 0 ||
				modSequenceBufPut(&track->track, AL_CMIDI_LOOPSTART_CODE) != 0 ||
				modSequenceBufPut(&track->track, (u8)((event->loop_count >> 8) & 0xff)) != 0 ||
				modSequenceBufPut(&track->track, (u8)(event->loop_count & 0xff)) != 0) {
			return -1;
		}
	} else if (strcmp(event->type, "loop_end") == 0) {
		u32 loop_count = event->loop_count ? event->loop_count : 0xff;

		if (modSequenceEmitLoopEndBody(track, loop_count) != 0) {
			return -1;
		}
	} else if (strcmp(event->type, "track_end") == 0 ||
			strcmp(event->type, "sequence_end") == 0) {
		if (modSequenceCloseOpenLoopsAtTrackEnd(track) != 0) {
			return -1;
		}
		if (modSequenceBufPut(&track->track, AL_MIDI_Meta) != 0 ||
				modSequenceBufPut(&track->track, AL_MIDI_META_EOT) != 0) {
			return -1;
		}
	} else {
		return -1;
	}

	track->last_tick = event->tick;
	return 0;
}

static s32 modSequenceEmitLoopEndBody(mod_seq_track_t *track, u32 loop_count)
{
	u32 loop_start;
	u32 event_end;
	u32 offset;

	if (!track || track->loop_start_count == 0 || loop_count > 0xff) {
		return -1;
	}

	loop_start = track->loop_start_stack[--track->loop_start_count];
	event_end = track->track.len + 8;
	offset = event_end - loop_start;

	if (modSequenceBufPut(&track->track, AL_MIDI_Meta) != 0 ||
			modSequenceBufPut(&track->track, AL_CMIDI_LOOPEND_CODE) != 0 ||
			modSequenceBufPut(&track->track, (u8)loop_count) != 0 ||
			modSequenceBufPut(&track->track, (u8)loop_count) != 0 ||
			modSequenceBufPut(&track->track, (u8)((offset >> 24) & 0xff)) != 0 ||
			modSequenceBufPut(&track->track, (u8)((offset >> 16) & 0xff)) != 0 ||
			modSequenceBufPut(&track->track, (u8)((offset >> 8) & 0xff)) != 0 ||
			modSequenceBufPut(&track->track, (u8)(offset & 0xff)) != 0) {
		return -1;
	}

	return 0;
}

static s32 modSequenceCloseOpenLoopsAtTrackEnd(mod_seq_track_t *track)
{
	while (track && track->loop_start_count > 0) {
		if (modSequenceEmitLoopEndBody(track, 0xff) != 0) {
			return -1;
		}
	}

	return 0;
}

static s32 modSequenceBuildTrack(mod_seq_track_t *track)
{
	u32 i;

	for (i = 0; i < track->count; i++) {
		if (modSequenceEmitEvent(track, &track->events[i]) != 0) {
			return -1;
		}
	}

	if (track->count > 0 && track->loop_start_count != 0) {
		return -1;
	}

	return 0;
}

static void modSequencePutBe16(u8 *dst, u32 value)
{
	dst[0] = (u8)((value >> 8) & 0xff);
	dst[1] = (u8)(value & 0xff);
}

static void modSequencePutBe32(u8 *dst, u32 value)
{
	dst[0] = (u8)((value >> 24) & 0xff);
	dst[1] = (u8)((value >> 16) & 0xff);
	dst[2] = (u8)((value >> 8) & 0xff);
	dst[3] = (u8)(value & 0xff);
}

static void *modSequenceBuildAlcBuffer(mod_seq_track_t *tracks,
		u32 division, u32 *out_size)
{
	u32 i;
	u32 offset = sizeof(ALCMidiHdr);
	u32 total = sizeof(ALCMidiHdr);
	u8 *data;

	if (!tracks || !out_size || division == 0 || division > 0x7fff) {
		return NULL;
	}

	for (i = 0; i < 16; i++) {
		if (modSequenceBuildTrack(&tracks[i]) != 0) {
			return NULL;
		}
		total += tracks[i].track.len;
	}

	if (total <= sizeof(ALCMidiHdr)) {
		return NULL;
	}

	data = sysMemZeroAlloc(total);
	if (!data) {
		return NULL;
	}

	for (i = 0; i < 16; i++) {
		if (tracks[i].track.len > 0) {
			modSequencePutBe32(data + i * 4, offset);
			memcpy(data + offset, tracks[i].track.data, tracks[i].track.len);
			offset += tracks[i].track.len;
		}
	}
	modSequencePutBe32(data + 64, division);

	*out_size = total;
	return data;
}

static void *modSequenceCompilePublicSource(const CatalogResolveResult *r,
		u16 num, u32 *out_size)
{
	char mid_path[FS_MAXPATH + 1];
	char json_path[FS_MAXPATH + 1];
	char ini_path[FS_MAXPATH + 1];
	u32 ini_size = 0;
	char *ini;
	u32 division = 0;
	u32 event_count = 0;
	mod_seq_track_t tracks[16];
	void *compiled;

	if (!r || !r->is_mod_override || !r->path || !out_size) {
		return NULL;
	}

	if (modSequencePathHasAudioExtension(r->path)) {
		return NULL;
	}

	if (modSequenceSiblingPath(r->path, "sequence.mid", mid_path, sizeof(mid_path)) != 0 ||
			modSequenceSiblingPath(r->path, "sequence.json", json_path, sizeof(json_path)) != 0 ||
			modSequenceSiblingPath(r->path, "music.ini", ini_path, sizeof(ini_path)) != 0) {
		return NULL;
	}

	if (fsFileSize(mid_path) <= 0) {
		return NULL;
	}

	ini = fsFileLoad(ini_path, &ini_size);
	if (!ini || modSequenceParseDivision(ini, ini_size, &division) != 0) {
		if (ini) {
			sysMemFree(ini);
		}
		return NULL;
	}
	sysMemFree(ini);

	memset(tracks, 0, sizeof(tracks));
	if (modSequenceLoadEventsJson(json_path, tracks, &event_count) != 0) {
		modSequenceTracksFree(tracks);
		return NULL;
	}

	compiled = modSequenceBuildAlcBuffer(tracks, division, out_size);
	modSequenceTracksFree(tracks);

	if (compiled) {
		sysLogPrintf(LOG_NOTE,
		             "CATALOG: music sequence %d -> public sequence source mid=\"%s\" events=\"%s\" (%u events, %u bytes)",
		             (s32)num, mid_path, json_path, event_count, *out_size);
	}

	return compiled;
}

typedef struct mod_grid_sequence_members {
	u32 midi_size, events_size;
	s32 midi_count, events_count;
} mod_grid_sequence_members_t;

static s32 modSequenceMeasureGridMember(const char *name, u32 size, void *context)
{
	mod_grid_sequence_members_t *members = context;
	if (strcmp(name, "sequence.mid") == 0) {
		members->midi_size = size;
		members->midi_count++;
	} else if (strcmp(name, "sequence.json") == 0) {
		members->events_size = size;
		members->events_count++;
	}
	return 0;
}

static char *modSequenceReadGridDescriptor(const char *name, const void *bytes, u32 size,
	u32 *out_size)
{
	const char *suffix = name ? strrchr(name, '.') : NULL;
	if (!suffix || strcmp(suffix, ".pdsong") != 0 || !size || size > NET_DISTRIB_MAX_COMP) return NULL;
	u32 descriptor_size = 0;
	char *descriptor = gridMatchPackageReadMember(bytes, size, "music.ini",
		NET_DISTRIB_MAX_COMP, &descriptor_size, NULL);
	if (!descriptor) return NULL;
	ini_section_t *ini = malloc(sizeof(*ini));
	s32 valid = ini && !memchr(descriptor, '\0', descriptor_size)
		&& iniParseText(descriptor, descriptor_size, ini, NULL, 0);
	if (valid) {
		const char *category = iniGet(ini, "audio_category", iniGet(ini, "kind", iniGet(ini, "category", "")));
		const char *midi = iniGet(ini, "music_file", iniGet(ini, "midi_file", ""));
		valid = strcmp(ini->type, "music") == 0 && !iniGet(ini, "file_path", "")[0]
			&& (!category[0] || strcasecmp(category, "music") == 0
				|| strcasecmp(category, "track") == 0 || strcmp(category, "1") == 0)
			&& strcmp(midi, "sequence.mid") == 0
			&& strcmp(iniGet(ini, "events_file", "sequence.json"), "sequence.json") == 0;
	}
	free(ini);
	mod_grid_sequence_members_t members = {0};
	if (!valid || modArchiveMemForEachEntry(bytes, size, modSequenceMeasureGridMember, &members) != MODARCHIVE_OK
			|| members.midi_count != 1 || members.events_count != 1 || members.midi_size < 14
			|| !members.events_size || (u64)descriptor_size + members.midi_size + members.events_size > NET_DISTRIB_MAX_SESSION) {
		free(descriptor);
		return NULL;
	}
	*out_size = descriptor_size;
	return descriptor;
}

s32 modSequenceGridSourceShareable(const char *name, const void *bytes, u32 size)
{
	u32 descriptor_size = 0;
	char *descriptor = modSequenceReadGridDescriptor(name, bytes, size, &descriptor_size);
	const s32 shareable = descriptor != NULL;
	free(descriptor);
	return shareable;
}

s32 modSequenceValidateGridSource(const char *name, const void *bytes, u32 size,
	char *err, size_t err_cap)
{
	u32 descriptor_size = 0, midi_size = 0, events_size = 0;
	char *descriptor = modSequenceReadGridDescriptor(name, bytes, size, &descriptor_size);
	void *midi = descriptor ? gridMatchPackageReadMember(bytes, size, "sequence.mid",
		NET_DISTRIB_MAX_SESSION, &midi_size, NULL) : NULL;
	char *events = midi ? gridMatchPackageReadMember(bytes, size, "sequence.json",
		NET_DISTRIB_MAX_SESSION, &events_size, NULL) : NULL;
	mod_seq_track_t tracks[16] = {0};
	u32 division = 0, event_count = 0, compiled_size = 0;
	void *compiled = NULL;
	if (events && midi_size >= 14 && memcmp(midi, "MThd", 4) == 0 && !memchr(events, '\0', events_size)
			&& modSequenceParseDivision(descriptor, descriptor_size, &division) == 0
			&& modSequenceParseEventsJson(events, events_size, tracks, &event_count) == 0) {
		compiled = modSequenceBuildAlcBuffer(tracks, division, &compiled_size);
	}
	const u64 native_size = (((u64)compiled_size + 15u) & ~15ull) + 0x40u;
	const s32 valid = compiled && (g_SndDisabled || native_size < g_SeqBufferSize);
	if (compiled) sysMemFree(compiled);
	modSequenceTracksFree(tracks);
	free(events);
	free(midi);
	free(descriptor);
	if (err && err_cap) {
		if (valid) err[0] = '\0';
		else snprintf(err, err_cap, "%s", "Grid song requires a valid public MIDI/event sequence that fits the native player");
	}
	return valid;
}

s32 modTextureLoad(u16 num, void *dst, u32 dstSize)
{
	/* PC: When g_NotLoadMod is set (title screen, CI main menu, solo stages),
	 * suppress mod texture overlay so base-game textures are used.  Without
	 * this check, mod texture packs replace textures
	 * globally for ALL stages, causing wrong textures on the CI background
	 * environment and in non-mod multiplayer arenas. */
	extern s32 g_NotLoadMod;
	if (g_NotLoadMod) {
		return -1;
	}

	/* C-5: catalog is primary texture router.
	 * catalogResolveTexture() returns the full routing decision: mod override
	 * (load from path), base-game ROM (catalog_id >= 0), or not cataloged. */
	{
		CatalogResolveResult r = catalogResolveTexture((s32)num);
		if (r.source_only_blocked) {
			const asset_entry_t *entry = assetCatalogGetByIndex(r.catalog_id);
			sysFatalError("ASSET.SOURCE_ONLY: texture %d maps to '%s' but has "
			              "no public FileProvider source; refusing ROM/static fallback.",
			              (s32)num, entry ? entry->id : "?");
			return -1;
		}
		if (r.is_mod_override && r.path) {
			const char *dot = strrchr(r.path, '.');
			s32 is_public_image = dot && (
				strcasecmp(dot, ".png") == 0 ||
				strcasecmp(dot, ".tga") == 0 ||
				strcasecmp(dot, ".jpg") == 0 ||
				strcasecmp(dot, ".jpeg") == 0 ||
				strcasecmp(dot, ".bmp") == 0);
			if (is_public_image) {
				sysLogPrintf(LOG_WARNING,
				             "MOD: texture %d catalog path is public image source (%s); "
				             "skipping legacy compressed texture loader",
				             (s32)num, r.path);
				return -1;
			}
			sysFatalError("ASSET.CHAIN: texture %d maps to public source '%s' "
				"but the catalog path is not an editable image source; "
				"refusing legacy compressed texture fallback.",
				(s32)num, r.path);
			return -1;
		} else if (r.catalog_id >= 0) {
			sysLogPrintf(LOG_NOTE, "CATALOG: tex %d → base (entry %d)", (s32)num, r.catalog_id);
		} else {
			sysLogPrintf(LOG_VERBOSE, "CATALOG: tex %d → base (not cataloged)", (s32)num);
		}
	}

	static s32 dirExists = -1;
	if (dirExists < 0) {
		dirExists = (fsFileSize(MOD_TEXTURES_DIR "/") >= 0);
	}

	if (!dirExists) {
		return -1;
	}

	char path[FS_MAXPATH + 1];
	snprintf(path, sizeof(path), MOD_TEXTURES_DIR "/%04x.bin", num);

	const s32 ret = fsFileLoadTo(path, dst, dstSize);
	if (ret > 0) {
		sysLogPrintf(LOG_NOTE, "MOD: texture %d loaded from legacy path", (s32)num);
	}

	return ret;
}

void *modSequenceLoad(u16 num, u32 *outSize)
{
	CatalogResolveResult r = catalogResolveMusicSequence((s32)num);
	if (r.is_mod_override && r.path) {
		void *compiled = modSequenceCompilePublicSource(&r, num, outSize);
		if (compiled) {
			return compiled;
		}
		{
			const asset_entry_t *entry = assetCatalogGetByIndex(r.catalog_id);
			sysFatalError("ASSET.CHAIN: music sequence %d maps to public "
				"audio source '%s' for audio '%s' but sequencer-native "
				"public source compile failed; refusing legacy sequence fallback.",
				(s32)num, r.path, entry ? entry->id : "?");
			return NULL;
		}
	}

	if (r.source_only_blocked) {
		const asset_entry_t *entry = assetCatalogGetByIndex(r.catalog_id);
		sysFatalError("ASSET.SOURCE_ONLY: music sequence %d maps to audio '%s' "
		              "but has no public FileProvider source; refusing ROM/static fallback.",
		              (s32)num, entry ? entry->id : "?");
		return NULL;
	}
	const s32 index = modSequenceVirtualIndex(num);
	if (index >= 0 && s_ModSequenceGridSlots[index]) return NULL;

	static s32 dirExists = -1;
	if (dirExists < 0) {
		dirExists = (fsFileSize(MOD_SEQUENCES_DIR "/") >= 0);
	}

	if (!dirExists) {
		return NULL;
	}

	char path[FS_MAXPATH + 1];
	snprintf(path, sizeof(path), MOD_SEQUENCES_DIR "/%04x.bin", num);
	if (fsFileSize(path) > 0) {
		void *ret = fsFileLoad(path, outSize);
		if (ret) {
			sysLogPrintf(LOG_NOTE, "mod: loaded external sequence %04x", num);
			return ret;
		}
	}

	return NULL;
}

s32 modSequencePlayAudioSource(u16 num)
{
	CatalogResolveResult r = catalogResolveMusicSequence((s32)num);

	if (!r.is_mod_override || !r.path || !modSequencePathHasAudioExtension(r.path)) {
		return 0;
	}

	modMusicPlay(r.path);

	if (!modMusicIsPlaying()) {
		const asset_entry_t *entry = assetCatalogGetByIndex(r.catalog_id);
		sysFatalError("ASSET.CHAIN: music sequence %d maps to public audio "
			"source '%s' for audio '%s' but streaming playback failed; "
			"refusing legacy sequence fallback.",
			(s32)num, r.path, entry ? entry->id : "?");
		return 0;
	}

	sysLogPrintf(LOG_NOTE,
	             "CATALOG: music sequence %d -> public audio source \"%s\" (entry %d)",
	             (s32)num, r.path, r.catalog_id);
	return 1;
}

static void *modAnimationLoadCatalogClip(const CatalogResolveResult *r, u16 num)
{
	const asset_entry_t *entry;
	const struct animtableentry *compiled_entry = NULL;
	u32 clip_size = 0;
	const void *clip_data;

	if (!r || r->catalog_id < 0 || !r->path
			|| !modAssetCompilerIsAnimationSource(r->path)) {
		return NULL;
	}

	entry = assetCatalogGetByIndex(r->catalog_id);
	if (!entry || entry->type != ASSET_ANIMATION) {
		return NULL;
	}

	if (!catalogLoadStageAsset(ASSET_ANIMATION, entry->id)) {
		return NULL;
	}

	clip_data = catalogGetLoadedAnimationClip(entry->id,
		&compiled_entry, &clip_size);
	if (!clip_data || clip_size == 0) {
		return NULL;
	}

	if (compiled_entry && num < animGetTotalCount() && g_Anims) { /* c3849: customs too */
		g_Anims[num] = *compiled_entry;
		g_Anims[num].data = 0xffffffff;
	}

	sysLogPrintf(LOG_NOTE,
		"CATALOG: anim %d -> generated clip \"%s\" (entry %d, %u bytes)",
		(s32)num, r->path, r->catalog_id, clip_size);
	return (void *)clip_data;
}

static void modAnimationFatalPublicSourceFailure(
	u16 num, const CatalogResolveResult *r, const char *reason)
{
	const asset_entry_t *entry = r ? assetCatalogGetByIndex(r->catalog_id) : NULL;

	sysFatalError("ASSET.SOURCE_ONLY: animation %d maps to public animation source "
	              "'%s' for animation '%s' but %s; refusing ROM/static fallback.",
	              (s32)num, r && r->path ? r->path : "?",
	              entry ? entry->id : "?",
	              reason ? reason : "runtime clip build failed");
}

void *modAnimationLoadData(u16 num)
{
    const void *generation = catalogAnimationGenerationData(num);
    if (generation) return (void *)generation;
	/* C-6: catalog is primary animation router.
	 * catalogResolveAnim() returns the full routing decision: mod override
	 * (load from path), base-game ROM (catalog_id >= 0), or not cataloged. */
	{
		CatalogResolveResult r = catalogResolveAnim((s32)num);
		if (r.source_only_blocked) {
			const asset_entry_t *entry = assetCatalogGetByIndex(r.catalog_id);
			sysFatalError("ASSET.SOURCE_ONLY: animation %d maps to '%s' but has "
			              "no public FileProvider source; refusing ROM/static fallback.",
			              (s32)num, entry ? entry->id : "?");
			return NULL;
		}
		if (r.is_mod_override && r.path) {
			if (modAssetCompilerIsAnimationSource(r.path)) {
				void *clip = modAnimationLoadCatalogClip(&r, num);
				if (clip) {
					return clip;
				}
				modAnimationFatalPublicSourceFailure(num, &r,
					"runtime clip compilation failed");
				return NULL;
			}
			modAnimationFatalPublicSourceFailure(num, &r,
				"the selected public source is not editable GLTF/GLB animation source");
			return NULL;
		} else if (r.catalog_id >= 0) {
			sysLogPrintf(LOG_NOTE, "CATALOG: anim %d → base (entry %d)", (s32)num, r.catalog_id);
		} else {
			sysLogPrintf(LOG_VERBOSE, "CATALOG: anim %d → base (not cataloged)", (s32)num);
		}
	}

	/* Legacy path: load from animations/ directory */
	char path[FS_MAXPATH + 1];
	snprintf(path, sizeof(path), MOD_ANIMATIONS_DIR "/%04x.bin", num);
	void *data = fsFileLoad(path, NULL);
	if (!data) {
		sysFatalError("External animation %04x has no data file.\nEnsure that it is placed at %s or delete the descriptor.", num, path);
	}
	return data;
}

void *modAnimationTryCatalogOverride(u16 num)
{
    const void *generation = catalogAnimationGenerationData(num);
    if (generation) return (void *)generation;
	/* C-6 supplement: catalog-only check for ROM-based animations.
	 * Called from animLoadFrame/animLoadHeader when data != 0xffffffff.
	 * Returns file data if a mod override is registered, NULL otherwise.
	 * Outside source-only checks, no legacy fallback is done here; the caller
	 * uses ROM DMA on NULL. Source-only mode fails closed before that DMA. */
	CatalogResolveResult r = catalogResolveAnim((s32)num);
	if (r.source_only_blocked) {
		const asset_entry_t *entry = assetCatalogGetByIndex(r.catalog_id);
		sysFatalError("ASSET.SOURCE_ONLY: animation %d maps to '%s' but has "
		              "no public FileProvider source; refusing ROM/static fallback.",
		              (s32)num, entry ? entry->id : "?");
		return NULL;
	}
	if (r.is_mod_override && r.path) {
		const char *path = r.path;
		if (modAssetCompilerIsAnimationSource(path)) {
			void *clip = modAnimationLoadCatalogClip(&r, num);
			if (clip) {
				return clip;
			}
			modAnimationFatalPublicSourceFailure(num, &r,
				"runtime clip compilation failed");
			return NULL;
		}
		modAnimationFatalPublicSourceFailure(num, &r,
			"the selected public source is not editable GLTF/GLB animation source");
		return NULL;
	}
	return NULL;
}

s32 modAnimationLoadDescriptor(u16 num, struct animtableentry *anim)
{
	static s32 dirExists = -1;
	if (dirExists < 0) {
		dirExists = (fsFileSize(MOD_ANIMATIONS_DIR "/") >= 0);
	}

	if (!dirExists) {
		return false;
	}

	char path[FS_MAXPATH + 1];

	// load the descriptor, if any
	snprintf(path, sizeof(path), MOD_ANIMATIONS_DIR "/%04x.txt", num);
	if (fsFileSize(path) <= 0) {
		return false;
	}

	char *desc = fsFileLoad(path, NULL);
	if (!desc) {
		return false;
	}

	// parse the descriptor
	char token[UTIL_MAX_TOKEN + 1] = { 0 };
	char *p = strParseToken(desc, token, NULL);
	s32 tmp = 0;
	while (p && token[0]) {
		if (!strcmp(token, "numframes")) {
			PARSE_INT(path, "numframes", tmp, 0, 0xFFFF, false);
			anim->numframes = tmp;
		} else if (!strcmp(token, "bytesperframe")) {
			PARSE_INT(path, "bytesperframe", tmp, 0, 0xFFFF, false);
			anim->bytesperframe = tmp;
		} else if (!strcmp(token, "headerlen")) {
			PARSE_INT(path, "headerlen", tmp, 0, 0xFFFF, false);
			anim->headerlen = tmp;
		} else if (!strcmp(token, "framelen")) {
			PARSE_INT(path, "framelen", tmp, 0, 0xFF, false);
			anim->framelen = tmp;
		} else if (!strcmp(token, "flags")) {
			PARSE_INT(path, "flags", tmp, 0, 0xFF, false);
			anim->flags = tmp;
		} else {
			sysLogPrintf(LOG_ERROR, "mod: %s: invalid key: %s", path, token);
			return false;
		}
		p = strParseToken(p, token, NULL);
	}

	sysMemFree(desc);

	sysLogPrintf(LOG_NOTE, "mod: loaded external animation %04x", num);

	return true;
}
