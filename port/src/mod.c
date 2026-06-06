#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <PR/libaudio.h>
#include <PR/ultratypes.h>
#include "platform.h"
#include "system.h"
#include "fs.h"
#include "utils.h"
#include "mod.h"
#include "data.h"
#include "assetcatalog.h"
#include "assetcatalog_load.h"
#include "modmusic.h"
#include "modasset_compiler.h"

#define MOD_TEXTURES_DIR "textures"
#define MOD_ANIMATIONS_DIR "animations"
#define MOD_SEQUENCES_DIR "sequences"

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

static s32 modSequenceSiblingPath(const char *path, const char *member,
		char *out, size_t out_n)
{
	const char *sep;
	const char *slash;
	size_t prefix_len;

	if (!path || !member || !out || out_n == 0) {
		return -1;
	}

	sep = strstr(path, "::");
	if (sep) {
		prefix_len = (size_t)(sep - path) + 2;
		if (prefix_len + strlen(member) >= out_n) {
			return -1;
		}
		memcpy(out, path, prefix_len);
		strcpy(out + prefix_len, member);
		return 0;
	}

	slash = strrchr(path, '/');
	if (!slash) {
		slash = strrchr(path, '\\');
	}
	if (slash) {
		prefix_len = (size_t)(slash - path) + 1;
		if (prefix_len + strlen(member) >= out_n) {
			return -1;
		}
		memcpy(out, path, prefix_len);
		strcpy(out + prefix_len, member);
		return 0;
	}

	if (strlen(member) >= out_n) {
		return -1;
	}
	strcpy(out, member);
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

static s32 modSequenceParseU32(const char *s, u32 *out)
{
	char *endp = NULL;
	unsigned long value;

	if (!s || !out || !s[0]) {
		return -1;
	}

	value = strtoul(s, &endp, 0);
	if (endp == s || *endp != '\0' || value > 0xfffffffful) {
		return -1;
	}
	*out = (u32)value;
	return 0;
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

static s32 modSequenceLoadEventsTsv(const char *path,
		mod_seq_track_t *tracks, u32 *out_events)
{
	u32 size = 0;
	char *text;
	char *line;
	char *save_line = NULL;
	u32 events = 0;

	if (!path || !tracks || !out_events) {
		return -1;
	}

	text = fsFileLoad(path, &size);
	if (!text || size == 0) {
		if (text) {
			sysMemFree(text);
		}
		return -1;
	}
	text[size] = '\0';

	for (line = strtok_r(text, "\r\n", &save_line); line; line = strtok_r(NULL, "\r\n", &save_line)) {
		char *cols[10];
		char *save_col = NULL;
		char *field;
		u32 col = 0;
		u32 track_index = 0;
		u32 value = 0;
		mod_seq_event_t event;

		memset(&event, 0, sizeof(event));

		field = strtok_r(line, "\t", &save_col);
		while (field && col < 10) {
			cols[col++] = field;
			field = strtok_r(NULL, "\t", &save_col);
		}

		if (col < 10 || strcmp(cols[0], "tick") == 0) {
			continue;
		}

		if (modSequenceParseU32(cols[0], &event.tick) != 0 ||
				modSequenceParseU32(cols[1], &track_index) != 0 ||
				track_index >= 16 ||
				modSequenceParseU32(cols[3], &value) != 0) {
			sysMemFree(text);
			return -1;
		}
		event.status = (u8)value;
		strncpy(event.type, cols[2], sizeof(event.type) - 1);

		if (modSequenceParseU32(cols[5], &value) == 0) event.byte1 = (u8)value;
		if (modSequenceParseU32(cols[6], &value) == 0) event.byte2 = (u8)value;
		if (modSequenceParseU32(cols[7], &event.duration) != 0) event.duration = 0;
		if (modSequenceParseU32(cols[8], &event.tempo_us) != 0) event.tempo_us = 0;
		if (modSequenceParseU32(cols[9], &event.loop_count) != 0) event.loop_count = 0;

		if (modSequenceTrackPush(&tracks[track_index], &event) != 0) {
			sysMemFree(text);
			return -1;
		}
		events++;
	}

	sysMemFree(text);
	*out_events = events;
	return events > 0 ? 0 : -1;
}

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
		u32 loop_start;
		u32 event_end;
		u32 offset;
		u32 loop_count = event->loop_count ? event->loop_count : 0xff;

		if (track->loop_start_count == 0 || loop_count > 0xff) {
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
	} else if (strcmp(event->type, "track_end") == 0 ||
			strcmp(event->type, "sequence_end") == 0) {
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
	char tsv_path[FS_MAXPATH + 1];
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
			modSequenceSiblingPath(r->path, "sequence.tsv", tsv_path, sizeof(tsv_path)) != 0 ||
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
	if (modSequenceLoadEventsTsv(tsv_path, tracks, &event_count) != 0) {
		modSequenceTracksFree(tracks);
		return NULL;
	}

	compiled = modSequenceBuildAlcBuffer(tracks, division, out_size);
	modSequenceTracksFree(tracks);

	if (compiled) {
		sysLogPrintf(LOG_NOTE,
		             "CATALOG: music sequence %d -> public sequence source mid=\"%s\" tsv=\"%s\" (%u events, %u bytes)",
		             (s32)num, mid_path, tsv_path, event_count, *out_size);
	}

	return compiled;
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
			if (dot && (
					strcasecmp(dot, ".png") == 0 ||
					strcasecmp(dot, ".tga") == 0 ||
					strcasecmp(dot, ".jpg") == 0 ||
					strcasecmp(dot, ".jpeg") == 0 ||
					strcasecmp(dot, ".bmp") == 0)) {
				sysLogPrintf(LOG_WARNING,
				             "MOD: texture %d catalog path is public image source (%s); "
				             "skipping legacy compressed texture loader",
				             (s32)num, r.path);
				return -1;
			}
			const s32 ret = fsFileLoadTo(r.path, dst, dstSize);
			if (ret > 0) {
				sysLogPrintf(LOG_NOTE, "CATALOG: tex %d → mod override \"%s\" (entry %d)",
				             (s32)num, r.path, r.catalog_id);
				return ret;
			}
			sysLogPrintf(LOG_WARNING, "MOD: texture %d catalog override failed (%s), falling back to legacy path",
			             (s32)num, r.path);
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
	}

	if (r.source_only_blocked) {
		const asset_entry_t *entry = assetCatalogGetByIndex(r.catalog_id);
		sysFatalError("ASSET.SOURCE_ONLY: music sequence %d maps to audio '%s' "
		              "but sequencer-native public source compile failed; "
		              "refusing ROM/static fallback.",
		              (s32)num, entry ? entry->id : "?");
		return NULL;
	}

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
		if (r.source_only_blocked) {
			const asset_entry_t *entry = assetCatalogGetByIndex(r.catalog_id);
			sysFatalError("ASSET.SOURCE_ONLY: music sequence %d maps to public audio "
			              "source '%s' for audio '%s' but streaming playback failed; "
			              "refusing ROM/static fallback.",
			              (s32)num, r.path, entry ? entry->id : "?");
		}
		sysLogPrintf(LOG_WARNING,
		             "MOD: music sequence %d catalog audio source failed (%s), falling back to legacy sequence",
		             (s32)num, r.path);
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
			|| !modAssetCompilerIsExternalSource(r->path)) {
		return NULL;
	}

	entry = assetCatalogGetByIndex(r->catalog_id);
	if (!entry || entry->type != ASSET_ANIMATION) {
		return NULL;
	}

	if (!catalogLoadTypedAsset(ASSET_ANIMATION, entry->id)) {
		return NULL;
	}

	clip_data = catalogGetLoadedAnimationClip(entry->id,
		&compiled_entry, &clip_size);
	if (!clip_data || clip_size == 0) {
		return NULL;
	}

	if (compiled_entry && num < g_NumAnimations && g_Anims) {
		g_Anims[num] = *compiled_entry;
		g_Anims[num].data = 0xffffffff;
	}

	sysLogPrintf(LOG_NOTE,
		"CATALOG: anim %d -> generated clip \"%s\" (entry %d, %u bytes)",
		(s32)num, r->path, r->catalog_id, clip_size);
	return (void *)clip_data;
}

void *modAnimationLoadData(u16 num)
{
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
			if (modAssetCompilerIsExternalSource(r.path)) {
				void *clip = modAnimationLoadCatalogClip(&r, num);
				if (clip) {
					return clip;
				}
				sysFatalError("External animation %04x failed to compile from %s.", num, r.path);
			}
			void *data = fsFileLoad(r.path, NULL);
			if (data) {
				sysLogPrintf(LOG_NOTE, "CATALOG: anim %d → mod override \"%s\" (entry %d)",
				             (s32)num, r.path, r.catalog_id);
				return data;
			}
			sysLogPrintf(LOG_WARNING, "MOD: animation %d catalog override failed (%s), falling back to legacy path",
			             (s32)num, r.path);
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
	/* C-6 supplement: catalog-only check for ROM-based animations.
	 * Called from animLoadFrame/animLoadHeader when data != 0xffffffff.
	 * Returns file data if a mod override is registered, NULL otherwise.
	 * No legacy fallback, no sysFatalError — caller uses ROM DMA on NULL. */
	const char *path = catalogGetAnimOverride((s32)num);
	if (path) {
		if (modAssetCompilerIsExternalSource(path)) {
			CatalogResolveResult r = catalogResolveAnim((s32)num);
			void *clip = modAnimationLoadCatalogClip(&r, num);
			if (clip) {
				return clip;
			}
			sysLogPrintf(LOG_WARNING,
				"C-6: generated clip for ROM anim %d failed to build: %s",
				(s32)num, path);
			return NULL;
		}
		void *data = fsFileLoad(path, NULL);
		if (data) {
			sysLogPrintf(LOG_NOTE, "CATALOG: anim %d → mod override (ROM base) \"%s\"", (s32)num, path);
			return data;
		}
		sysLogPrintf(LOG_WARNING, "C-6: catalog override for ROM anim %d failed to load: %s", (s32)num, path);
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
